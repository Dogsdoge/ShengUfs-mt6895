/*
 * UfsExtents.c - extent mapping for file-backed root on MT6895 userdata.
 *
 * Ported from ios-fuxi's 0088-fuxi-extent-root.patch (bangprovn/ios-fuxi).
 * The iOS root image lives in one pinned, preallocated file inside Android's
 * userdata; the file's physical blocks are described by an extent table
 * supplied in the Apple Device Tree (ADT) by our patched mkadt.py.
 *
 * Extent table format in /chosen/unixnu-root-extents:
 *   u64 triples {fileBlock, partBlock, count} in 4 KiB units, plus
 *   /chosen/unixnu-root-blocks (u64 total size in 4 KiB blocks).
 *
 * This module maps a file-relative 4K block to a userdata-partition-relative
 * 4K block, and validates the table against the partition bounds.
 */

#include "UfsExtents.h"

#define UX_MAX_EXTENTS 512

struct ux_extent {
    uint64_t file;   /* file-relative 4K block */
    uint64_t part;   /* userdata-partition-relative 4K block */
    uint64_t count;  /* number of 4K blocks */
};

static struct ux_extent ux_ext[UX_MAX_EXTENTS];
static unsigned ux_next;
static int ux_root;              /* 1 = table loaded and validated */
static uint64_t ux_part_blocks;  /* userdata partition size in 4K blocks */

/* Map a file-relative 4K block to partition-relative block.
 * Returns 1 and sets *part_block + *run (blocks left in this extent). */
int ux_map(uint64_t file_block, uint64_t *part_block, uint64_t *run)
{
    unsigned i;
    if (!ux_root) return 0;
    for (i = 0; i < ux_next; i++) {
        if (file_block >= ux_ext[i].file &&
            file_block - ux_ext[i].file < ux_ext[i].count) {
            uint64_t off = file_block - ux_ext[i].file;
            *part_block = ux_ext[i].part + off;
            *run = ux_ext[i].count - off;
            return 1;
        }
    }
    return 0;
}

/* True only if partition blocks [part, part+count) lie within one extent.
 * Used as a write guard so no I/O escapes the file's allocated blocks. */
int ux_write_allowed(uint64_t part, uint64_t count)
{
    unsigned i;
    if (!ux_root || count == 0) return 0;
    for (i = 0; i < ux_next; i++) {
        uint64_t first = ux_ext[i].part;
        if (part >= first && part - first < ux_ext[i].count &&
            count <= ux_ext[i].count - (part - first)) {
            return 1;
        }
    }
    return 0;
}

/* Load and validate an extent table.
 * ext: array of u64 triples {file, part, count}; n = number of triples.
 * total_blocks: expected total file size in 4K blocks.
 * part_blocks: userdata partition size in 4K blocks (for bounds check).
 * Returns 0 on success, negative errno on failure. */
int ux_load(const uint64_t *ext, unsigned n, uint64_t total_blocks,
            uint64_t part_blocks)
{
    uint64_t pos = 0;
    unsigned i, j;

    if (!ext || n == 0 || n > UX_MAX_EXTENTS || total_blocks == 0)
        return -1;

    ux_next = 0;
    ux_root = 0;
    ux_part_blocks = part_blocks;

    for (i = 0; i < n && pos < total_blocks; i++) {
        uint64_t file = ext[3 * i];
        uint64_t part = ext[3 * i + 1];
        uint64_t count = ext[3 * i + 2];

        /* file blocks must be contiguous from 0 */
        if (file != pos || count == 0) return -2;
        /* partition range must be within userdata */
        if (part >= part_blocks || count > part_blocks - part) return -3;
        /* clamp last extent to the declared size */
        if (count > total_blocks - pos) count = total_blocks - pos;
        /* no overlaps */
        for (j = 0; j < ux_next; j++) {
            if (part < ux_ext[j].part + ux_ext[j].count &&
                ux_ext[j].part < part + count) return -4;
        }
        ux_ext[ux_next].file = file;
        ux_ext[ux_next].part = part;
        ux_ext[ux_next].count = count;
        ux_next++;
        pos += count;
    }
    if (pos != total_blocks) { ux_next = 0; return -5; }

    ux_root = 1;
    return 0;
}

int ux_ready(void) { return ux_root; }
unsigned ux_count(void) { return ux_next; }
