/*
 * Host-side test for the extent mapping layer. Runs on any C compiler.
 * Build: cc -I. -o test_extents test_extents.c UfsExtents.c
 * Run:   ./test_extents
 */
#include <stdio.h>
#include <string.h>
#include "UfsExtents.h"

int main(void)
{
    /* Two extents: file blocks 0..1791 -> part 47990000..; 1792..2303 -> part 48010000.. */
    uint64_t ext[] = {
        0,        47990000ULL, 1792,
        1792,     48010000ULL, 512,
    };
    uint64_t part, run;
    int rc;

    rc = ux_load(ext, 2, 2304, 62480375);
    if (rc != 0) { printf("FAIL ux_load rc=%d\n", rc); return 1; }
    printf("ux_load OK: %u extents, ready=%d\n", ux_count(), ux_ready());

    /* map file block 0 -> part 47990000, run 1792 */
    if (!ux_map(0, &part, &run) || part != 47990000ULL || run != 1792) {
        printf("FAIL map(0): part=%llu run=%llu\n",
               (unsigned long long)part, (unsigned long long)run); return 1;
    }
    printf("map(0) OK: part=%llu run=%llu\n",
           (unsigned long long)part, (unsigned long long)run);

    /* map file block 1792 -> part 48010000, run 512 (second extent) */
    if (!ux_map(1792, &part, &run) || part != 48010000ULL || run != 512) {
        printf("FAIL map(1792): part=%llu run=%llu\n",
               (unsigned long long)part, (unsigned long long)run); return 1;
    }
    printf("map(1792) OK: part=%llu run=%llu\n",
           (unsigned long long)part, (unsigned long long)run);

    /* map mid-extent: file 100 -> part 47990100, run 1692 */
    if (!ux_map(100, &part, &run) || part != 47990100ULL || run != 1692) {
        printf("FAIL map(100): part=%llu run=%llu\n",
               (unsigned long long)part, (unsigned long long)run); return 1;
    }
    printf("map(100) OK: part=%llu run=%llu\n",
           (unsigned long long)part, (unsigned long long)run);

    /* out of range */
    if (ux_map(2304, &part, &run)) { printf("FAIL map(2304) should be out of range\n"); return 1; }
    printf("map(2304) correctly refused\n");

    /* write guard: inside extent 1 yes, spanning boundary no, outside no */
    /* extent 1 covers part 47990000..47991791 (1792 blocks) */
    if (!ux_write_allowed(47990000ULL, 100)) { printf("FAIL write guard inside\n"); return 1; }
    if (ux_write_allowed(47991791ULL, 2)) { printf("FAIL write guard spanning\n"); return 1; }
    if (ux_write_allowed(1000, 1)) { printf("FAIL write guard outside\n"); return 1; }
    printf("write guard OK\n");

    /* invalid tables must be rejected */
    if (ux_load(ext, 2, 2304, 47990001) == 0) { printf("FAIL partsize bound\n"); return 1; }
    printf("invalid part bound correctly rejected\n");

    printf("ALL TESTS PASSED\n");
    return 0;
}
