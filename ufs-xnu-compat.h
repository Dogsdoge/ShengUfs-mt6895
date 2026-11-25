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

/* ---- locks: a tiny spinlock stands in for lck_mtx in the archive ---- */
typedef volatile int ufs_compat_lock_t;
typedef struct { int unused; } ufs_compat_lck_grp_t;

#define LCK_GRP_DECLARE(name, desc) static ufs_compat_lck_grp_t name
#define LCK_MTX_DECLARE(name, grp)  static ufs_compat_lock_t name = 0

static inline void ufs_compat_lock(volatile int *l) {
    while (__sync_lock_test_and_set(l, 1)) { while (*l) { } }
}
static inline void ufs_compat_unlock(volatile int *l) { __sync_lock_release(l); }
#define lck_mtx_lock(l)   ufs_compat_lock(l)
#define lck_mtx_unlock(l) ufs_compat_unlock(l)

/* ---- sysctl: stats nodes are not registered in the standalone archive ---- */
#define SYSCTL_DECL(parent)
#define SYSCTL_QUAD(...) /* no sysctl tree in archive */

/* ---- clocks / MMU / sleep ----
 * UfsHc.c already declares ml_*/IOSleep/mach_* externs itself; the trimmed
 * kernel headers provide mach_absolute_time/absolutetime_to_nanoseconds.
 * Only the lock and sysctl macros above are missing. Keep this header minimal
 * to avoid duplicate/conflicting declarations.
 */

#endif /* _UFS_XNU_COMPAT_H_ */
