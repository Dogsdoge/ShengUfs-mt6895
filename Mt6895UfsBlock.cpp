/*
 * Mt6895UfsBlock.cpp - Layer 3 glue: IOMedia publication with extent mapping.
 *
 * Based on UniXNU ShengUfs NativeUfsBlock (IOBlockStorageDevice subclass),
 * adapted for MT6895 file-backed root. The read path maps window-relative
 * blocks through ux_map() (Layer 2) and reads raw UFS via ufs_read_blocks()
 * (Layer 1), instead of UniXNU's fixed absolute-offset window.
 *
 * Reference: UniXNU/ShengUfs NativeUfsBlock interface (fd420df).
 */
#include <IOKit/IOService.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/IOLocks.h>

extern "C" {
#include "UfsExtents.h"
#include "UfsHc.h"
}

static constexpr uint64_t kDataMax = 1u << 20;

class Mt6895UfsBlock : public IOBlockStorageDevice {
    OSDeclareDefaultStructors (Mt6895UfsBlock)

    uint64_t bytes = 0;
    char label[32] = {};
    static constexpr unsigned kBounces = 4;
    uint8_t *bounces[kBounces] = {};
    uint32_t bounceFree = 0;
    IOLock *windowLock = nullptr;
    bool online = false;

    uint8_t *takeBounce ()
    {
        IOLockLock (windowLock);
        while (!bounceFree)
            IOLockSleep (windowLock, &bounceFree, THREAD_UNINT);
        unsigned k = static_cast<unsigned> (__builtin_ctz (bounceFree));
        bounceFree &= ~(1U << k);
        IOLockUnlock (windowLock);
        return bounces[k];
    }
    void giveBounce (uint8_t *b)
    {
        IOLockLock (windowLock);
        for (unsigned k = 0; k < kBounces; ++k)
            if (bounces[k] == b) bounceFree |= 1U << k;
        IOLockWakeup (windowLock, &bounceFree, true);
        IOLockUnlock (windowLock);
    }

public:
    bool configure (const char *name, uint64_t size);
    bool start (IOService *provider) override;
    void free () override;

    const char *windowLabel () const { return label; }
    IOReturn doEjectMedia () override { return kIOReturnUnsupported; }
    IOReturn doFormatMedia (UInt64) override { return kIOReturnNotWritable; }
    UInt32 doGetFormatCapacities (UInt64 *, UInt32) const override { return 0; }
    char *getVendorString () override { return const_cast<char *> ("MT6895"); }
    char *getProductString () override { return label; }
    char *getRevisionString () override { return const_cast<char *> ("1.0"); }
    char *getAdditionalDeviceInfoString () override
        { return const_cast<char *> ("UFS file-backed root (extent-mapped, read-only)"); }
    IOReturn reportBlockSize (UInt64 *v) override { *v = 512; return kIOReturnSuccess; }
    IOReturn reportEjectability (bool *v) override { *v = false; return kIOReturnSuccess; }
    IOReturn reportMaxValidBlock (UInt64 *v) override { *v = bytes / 512 - 1; return kIOReturnSuccess; }
    IOReturn reportMediaState (bool *present, bool *changed) override
        { *present = online; if (changed) *changed = false; return kIOReturnSuccess; }
    IOReturn reportRemovability (bool *v) override { *v = false; return kIOReturnSuccess; }
    IOReturn reportWriteProtection (bool *v) override { *v = true; return kIOReturnSuccess; }
    IOReturn doSynchronizeCache () override { return online ? kIOReturnSuccess : kIOReturnNotReady; }
    IOReturn doSynchronize (UInt64, UInt64, IOOptionBits) override { return doSynchronizeCache (); }
    IOReturn getWriteCacheState (bool *v) override { *v = false; return kIOReturnSuccess; }
    IOReturn setWriteCacheState (bool) override { return kIOReturnUnsupported; }

    IOReturn doAsyncReadWrite (IOMemoryDescriptor *, UInt64, UInt64,
                               IOStorageAttributes *, IOStorageCompletion *) override;
};

bool Mt6895UfsBlock::configure (const char *name, uint64_t size)
{
    if (!name || size == 0 || (size & 511)) return false;
    strlcpy (label, name, sizeof (label));
    bytes = size;
    windowLock = IOLockAlloc ();
    if (!windowLock) return false;
    for (unsigned k = 0; k < kBounces; ++k) {
        bounces[k] = static_cast<uint8_t *> (IOMalloc (kDataMax));
        if (!bounces[k]) {
            if (!k) return false;
            break;
        }
        bounceFree |= 1U << k;
    }
    return true;
}

bool Mt6895UfsBlock::start (IOService *provider)
{
    if (!IOBlockStorageDevice::start (provider)) return false;
    if (!ux_ready ()) {
        IOLog ("Mt6895Ufs: extent table not loaded; refusing to publish\n");
        return false;
    }
    online = true;
    IOLog ("Mt6895Ufs: %s: %u extents, %llu bytes\n",
           label, ux_count (), (unsigned long long) bytes);
    return true;
}

IOReturn Mt6895UfsBlock::doAsyncReadWrite (IOMemoryDescriptor *buffer, UInt64 block,
                                           UInt64 count, IOStorageAttributes *,
                                           IOStorageCompletion *completion)
{
    if (!buffer || !completion || !completion->action || !count ||
        block >= bytes / 512 || count > bytes / 512 - block ||
        buffer->getLength () < count * 512)
        return kIOReturnBadArgument;
    if (buffer->getDirection () != kIODirectionIn) return kIOReturnNotWritable;
    if (!online) return kIOReturnNotReady;

    uint8_t *bounce = takeBounce ();
    uint64_t done = 0;
    IOReturn result = kIOReturnSuccess;

    while (done < count) {
        uint64_t fileBlock = (block + done) * 512 / 4096;
        uint64_t partBlock = 0, run = 0;
        if (!ux_map (fileBlock, &partBlock, &run)) {
            result = kIOReturnIOError;
            break;
        }
        uint64_t n4k = (count - done) * 512 / 4096 + 1;
        if (n4k > run) n4k = run;
        if (n4k * 4096 > kDataMax) n4k = kDataMax / 4096;

        if (ufs_read_blocks (partBlock, n4k, bounce) != 0) {
            result = kIOReturnIOError;
            break;
        }
        buffer->writeBytes (done * 512, bounce, n4k * 4096);
        done += n4k * 4096 / 512;
    }

    giveBounce (bounce);
    completion->action (completion->target, completion->parameter, result, done);
    return kIOReturnSuccess;
}

void Mt6895UfsBlock::free ()
{
    for (unsigned k = 0; k < kBounces; ++k)
        if (bounces[k]) { IOFree (bounces[k], kDataMax); bounces[k] = nullptr; }
    if (windowLock) { IOLockFree (windowLock); windowLock = nullptr; }
    IOBlockStorageDevice::free ();
}

OSDefineMetaClassAndStructors (Mt6895UfsBlock, IOBlockStorageDevice)
