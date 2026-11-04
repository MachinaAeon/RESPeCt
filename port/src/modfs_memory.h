/* Boot-only archive metadata for the optional dynamic model heap. */
#ifndef MODFS_MEMORY_H
#define MODFS_MEMORY_H
#include <stddef.h>
#include <stdint.h>
void modfs_memory_reset(void);
int modfs_memory_index(uint32_t table, const uint8_t *file, size_t bytes,
                       const uint8_t *sectors, size_t table_bytes);
int modfs_model_request_size(uint32_t table, int index, uint32_t *packed,
                            uint32_t part_sizes[3]);
uint32_t modfs_static_end(void);
int modfs_dynamic_models(void);
#endif
