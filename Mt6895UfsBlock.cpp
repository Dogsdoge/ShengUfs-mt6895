/*
 * Mt6895UfsBlock.cpp - Layer 3 glue: IOMedia publication with extent mapping.
 *
 * This is the design-level glue that connects:
 *   Layer 1 (UfsHc.c)     - raw UFS READ(10) on 0x112b0000
 *   Layer 2 (UfsExtents.c)- file block -> userdata raw block mapping
 *   UniXNU RootPublication- IOBlockStorageDevice -> IOMedia -> APFS mount
 *
 * The stock UniXNU NativeUfsBlock reads a continuous window at a fixed
 * "absolute" byte offset on LUN 0. Our root is a pinned file whose blocks are
 * described by an extent table, so we override the read path: a window-relative
 * 4K block is first mapped through ux_map() to a partition-relative 4K block,
 * then handed to the UFS controller.
 *
 * NOTE: This file is a design skeleton. It compiles against IOKit headers but
 * is not yet wired into the full ShengUfs build (Layer 1 + Layer 2 are ready;
 * this is the integration point). See README.md for the 3-layer architecture.
 */

#include <IOKit/IOService.h>
#include <IOKit/storage/IOBlockStorageDevice.h>
#include <IOKit/IOLocks.h>

extern "C" {
#include "UfsExtents.h"
#include "UfsHc.h"
}

/* One RAM bounce buffer, large enough for a bounded read chunk. */
static constexpr uint64_t kDataMax = 1u << 20;   /* 1 MiB */

class Mt6895UfsBlock : public IOBlockStorageDevice {
    OSDeclareDefaultStructors (Mt6895UfsBlock)

    uint64_t windowBytes = 0;    /* size of the published window */
    uint8_t *bounce = nullptr;
    IOLock *lock = nullptr;
    bool online = false;

public:
    bool configure (const char *name, uint64_t size);
    bool start (IOService *provider) override;
    void free () override;
    IOReturn doEjectMedia () override { return kIOReturnUnsupported; }
    IOReturn doAsyncReadWrite (IOMemoryDescriptor *buffer, UInt64 block,
                               UInt64 count, IOStorageAttributes *,
                               IOStorageCompletion *completion) override;
};

bool Mt6895UfsBlock::configure (const char *name, uint64_t size)
{
    if (!name || size == 0 || (size & 511)) return false;
    windowBytes = size;
    lock = IOLockAlloc ();
    if (!lock) return false;
    bounce = static_cast<uint8_t *> (IOMalloc (kDataMax));
    if (!bounce) return false;
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
    IOLog ("Mt6895Ufs: published root with %u extents, %llu bytes\n",
           ux_count (), (unsigned long long) windowBytes);
    return true;
}

IOReturn Mt6895UfsBlock::doAsyncReadWrite (IOMemoryDescriptor *buffer, UInt64 block,
                                           UInt64 count, IOStorageAttributes *,
                                           IOStorageCompletion *completion)
{
    if (!buffer || !completion || !completion->action || !count ||
        block >= windowBytes / 512 || count > windowBytes / 512 - block ||
        buffer->getLength () < count * 512)
        return kIOReturnBadArgument;
    if (buffer->getDirection () != kIODirectionIn) return kIOReturnNotWritable;
    if (!online) return kIOReturnNotReady;

    IOLockLock (lock);
    uint64_t done = 0;
    IOReturn result = kIOReturnSuccess;
    while (done < count) {
        uint64_t fileBlock = (block + done) * 512 / 4096;   /* window-relative 4K */
        uint64_t partBlock = 0, run = 0;
        if (!ux_map (fileBlock, &partBlock, &run)) {
            result = kIOReturnIOError;
            break;
        }
        uint64_t blocks = (count - done) * 512 / 4096 + 1;
        if (blocks > run) blocks = run;
        if (blocks * 4096 > kDataMax) blocks = kDataMax / 4096;

        /* Layer 1: raw READ(10) from the mapped partition block on LUN 2 */
        if (ufs_read_blocks (partBlock, blocks, bounce) != 0) {
            result = kIOReturnIOError;
            break;
        }
        buffer->writeBytes (done * 512, bounce, blocks * 4096);
        done += blocks * 4096 / 512;
    }
    IOLockUnlock (lock);

    completion->action (completion->target, completion->parameter, result, done);
    return kIOReturnSuccess;
}

void Mt6895UfsBlock::free ()
{
    if (bounce) { IOFree (bounce, kDataMax); bounce = nullptr; }
    if (lock) { IOLockFree (lock); lock = nullptr; }
    IOBlockStorageDevice::free ();
}

OSDefineMetaClassAndStructors (Mt6895UfsBlock, IOBlockStorageDevice)
