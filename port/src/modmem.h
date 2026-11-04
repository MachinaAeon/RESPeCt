/* Experimental room-owned memory. Original 2 MiB path is never redirected
 * unless enabled and a model/animation mod is actually installed. */
#ifndef MODMEM_H
#define MODMEM_H
#include <stdint.h>
#define MODMEM_ABI 2u
#define MODMEM_MODELS 34u
typedef struct {
    uint32_t abi, active, base, limit, cursor, owner, scratch_floor, models;
    uint32_t checkpoints[MODMEM_MODELS];
    uint32_t stage_base, stage_bytes, stage_table, stage_index, stage_cancelled;
} ModMemoryState;
/* Only these identities are read as three compressed parts by the native loader.
 * Other OBJ/EV blocks include raw textures, bosses and overlay-specific data. */
static inline int modmem_split_identity(uint32_t table, uint32_t index) {
    return (table == 0x800A0CD8u && index < 0x1Eu) ||
           (table == 0x800A195Cu && index == 0x196u);
}
uint32_t modmem_stage_acquire(uint32_t table, uint32_t index, uint32_t bytes);
uint32_t modmem_stage_source(void);
int modmem_stage_read(uint32_t table, uint32_t index, uint32_t address, uint64_t bytes);
void modmem_stage_release(void);
void modmem_stage_cancel(void);
/* The synchronous native may suspend in a fiber while another thread runs.
 * Its C locals cannot be snapshotted, so saves/loads defer across all threads. */
int modmem_sync_busy(void);
void modmem_sync_begin(void);
void modmem_sync_end(void);
int modmem_enabled(void);
int modmem_active(void);
void modmem_begin(uint32_t owner);
void modmem_clear(void);
void modmem_require_fresh(void);
void modmem_budget(uint64_t bytes, uint64_t scratch);
void modmem_require(uint32_t address, uint64_t bytes, const char *purpose);
uint32_t modmem_scratch(uint64_t bytes, const char *purpose);
int modmem_heap_take(uint32_t top, uint64_t bytes, uint32_t *next);
void modmem_checkpoint(uint32_t models, uint32_t top);
uint32_t modmem_retain(uint32_t models);
void modmem_export(ModMemoryState *state);
int modmem_validate(const ModMemoryState *state, uint32_t saved_top);
void modmem_restore(const ModMemoryState *state);
/* Boot-prepared metadata; no CD reads or emulated time. */
uint32_t modfs_static_end(void);
int modfs_dynamic_models(void);
int modfs_model_request_size(uint32_t table, int index, uint32_t *packed, uint32_t parts[3]);
#endif
