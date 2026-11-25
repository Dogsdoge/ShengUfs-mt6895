/*
 * ufs-xnu-compat.h — minimal shims for compiling the ShengUfs driver as a
 * standalone archive against the UniXNU framework's trimmed kernel headers.
 *
 * WHY: UfsHc.c is a real XNU kernel driver (from SLNeko). In the kernel it uses
 * LCK_MTX_*, SYSCTL_*, ml_* and mach clocks. The framework's fakeroot headers
 * are a trimmed subset that does not declare LCK_MTX_DECLARE, and the archive
 * build has no sysctl registration or MMU anyway. This header supplies just
 * enough for the archive to compile and link; the real kernel gets the genuine
 * APIs from its own headers (guard with UFS_STANDALONE_ARCHIVE if needed).
 *
 * Long-term: keep every standalone-build shim here, not in the driver sources.
 * New XNU dependencies go in this file, never scattered through UfsHc.c.
 */
#ifndef _UFS_XNU_COMPAT_H_
#define _UFS_XNU_COMPAT_H_

#include <stdbool.h>
#include <stdint.h>

/* ---- locks ----
 * The trimmed headers do define lck_mtx_t (struct __lck_mtx_t__) and msleep,
 * but not the LCK_*_DECLARE macros. Declare the real type so pointers passed
 * to msleep()/lck_mtx_* type-check; implement the lock ops as a spinlock over
 * the opaque struct's first word.
 */
typedef struct { int unused; } ufs_compat_lck_grp_t;
typedef lck_mtx_t ufs_compat_lock_t;

/* Caller writes `static LCK_MTX_DECLARE(...)`; macro adds no `static`. */
#define LCK_GRP_DECLARE(name, desc) ufs_compat_lck_grp_t name
#define LCK_MTX_DECLARE(name, grp)  ufs_compat_lock_t name

static inline void ufs_compat_lock(lck_mtx_t *l) {
    volatile int *w = (volatile int *)l;
    while (__sync_lock_test_and_set(w, 1)) { while (*w) { } }
}
static inline void ufs_compat_unlock(lck_mtx_t *l) {
    __sync_lock_release((volatile int *)l);
}
#define lck_mtx_lock(l)   ufs_compat_lock(l)
#define lck_mtx_unlock(l) ufs_compat_unlock(l)

/* ---- sysctl: stats nodes are not registered in the standalone archive ---- */
#define SYSCTL_DECL(parent)
#define SYSCTL_QUAD(...) /* no sysctl tree in archive */

/* ---- kernel memory allocation (stable XNU ABI) ----
 * These functions/constants aren't in the trimmed headers. The functions are
 * resolved at final link by the kernel; only declarations are needed here.
 */
typedef void *vm_map_t;
extern vm_map_t kernel_map;
extern void *kalloc_data(unsigned int size, unsigned int flags);
extern void kfree_data(void *data, unsigned int size);
extern int kmem_alloc_contig(vm_map_t map, vm_offset_t *addr, unsigned long size,
                             unsigned long mask, unsigned long low, unsigned long high,
                             unsigned int flags, unsigned int tag);
#define Z_WAITOK       0x00000008u
#define KMA_KOBJECT    0x00000008u
#define KMA_ZERO       0x00000020u
#define KMA_DATA       0x00000100u
#define VM_KERN_MEMORY_IOKIT  10

/* ---- clocks / MMU / sleep ----
 * UfsHc.c declares its own MMU/IOSleep externs, and the trimmed kernel headers
 * provide the mach clock functions. Only the lock and sysctl macros above are
 * missing. Keep this header minimal to avoid conflicting declarations.
 */

#endif /* _UFS_XNU_COMPAT_H_ */
