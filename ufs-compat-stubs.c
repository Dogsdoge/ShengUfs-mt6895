/*
 * ufs-compat-stubs.c — link-time stubs for symbols the standalone UFS archive
 * references but the trimmed UniXNU framework does not provide.
 *
 * arm64_platform_sleep_hook: libArmCpu.a calls it; upstream ArmCpu component
 *   does not define it. A no-op is safe on this board (we don't use ARM sleep
 *   hook for the UFS bring-up path).
 * loader_log_putc: SLNeko-style late logging; the host has no loader console.
 *   Drop the characters (normal hc_say uses kprintf and still works).
 * kmem_alloc_contig: XNU's real API lives in the kernel but isn't exported to
 *   archive linking here. Return failure so ufshc_probe() takes its existing
 *   "no memory for slot buffers" graceful-degradation path.
 */
#include <stdint.h>
#include <stddef.h>

void arm64_platform_sleep_hook(void) { }
void loader_log_putc(char c) { (void)c; }

typedef void *vm_map_t;
int kmem_alloc_contig(vm_map_t map, void **addr, unsigned long size,
                      unsigned long mask, unsigned long low, unsigned long high,
                      unsigned int flags, unsigned int tag) {
    (void)map; (void)addr; (void)size; (void)mask; (void)low; (void)high;
    (void)flags; (void)tag;
    return 1;  /* KERN_FAILURE */
}
