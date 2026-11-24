#ifndef UFS_EXTENTS_H
#define UFS_EXTENTS_H

#include <stdint.h>

/* Map a file-relative 4K block to a userdata-partition-relative 4K block.
 * Returns 1 on success, 0 if the block is outside all extents. */
int ux_map(uint64_t file_block, uint64_t *part_block, uint64_t *run);

/* Write guard: 1 if [part, part+count) is fully inside one extent. */
int ux_write_allowed(uint64_t part, uint64_t count);

/* Load and validate extent table.
 * ext: u64 triples {file, part, count}; n triples; total/part blocks in 4K units.
 * Returns 0 on success, negative errno-style code on failure. */
int ux_load(const uint64_t *ext, unsigned n, uint64_t total_blocks,
            uint64_t part_blocks);

int ux_ready(void);
unsigned ux_count(void);

#endif /* UFS_EXTENTS_H */
