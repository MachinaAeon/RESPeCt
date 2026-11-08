#include "modmem.h"
#include "config.h"
#include "rt.h"
#include <stdlib.h>
#include <string.h>
static ModMemoryState room;
static int sync_busy;
int modmem_sync_busy(void) { return sync_busy; }
void modmem_sync_begin(void) {
    if (sync_busy) rt_fatal("mods: synchronous model loader is already active");
    sync_busy=1;
}
void modmem_sync_end(void) { sync_busy=0; }
#define RAM_END 0x80800000u
#define HEAP_CURSOR 0x800B4D78u
int modmem_enabled(void) {
    const char *s = getenv("RT_MOD_ARENA");
    return s ? !strcmp(s, "1") : g_cfg.dynamic_mod_buffers != 0;
}
int modmem_active(void) { return room.active != 0; }
void modmem_require_fresh(void) {
    if (!room.active && modmem_enabled() && modfs_dynamic_models())
        rt_fatal("mods: dynamic models require a fresh room load; resume an arena state or enter a new room first");
}
void modmem_clear(void) { memset(&room, 0, sizeof room); }
static int stage_io_pending(void) {
    uint32_t left,queue;
    uint16_t head,tail,max;
    memcpy(&left,ram_ptr(0x80062F18),4);
    memcpy(&queue,ram_ptr(0x80062D74),4);
    memcpy(&head,ram_ptr(0x80062F02),2);memcpy(&tail,ram_ptr(0x80062EFE),2);
    memcpy(&max,ram_ptr(0x80062EFC),2);
    if (left || head!=tail) return 1;
    if (!queue) return 0;
    uint32_t physical=queue&0x1FFFFFFFu,count=(uint32_t)max+1;
    if ((queue&3) || physical>=RAM_GAME || (uint64_t)count*16>RAM_GAME-physical || head>=count) return 1;
    /* A completely full ring also has head==tail. Its READ commands still
     * own their destinations even before the ready callback starts. */
    for(uint32_t i=0;i<count;i++) {
        uint16_t command;memcpy(&command,g_ram+physical+i*16,2);
        if(command==2) return 1;
    }
    return 0;
}
void modmem_begin(uint32_t owner) {
    if (room.stage_base && stage_io_pending())
        rt_fatal("mods: cannot replace compressed staging while the previous CD queue is active");
    modmem_clear();
    if (!modmem_enabled() || !modfs_dynamic_models()) return;
    uint64_t base = ((uint64_t)modfs_static_end() + 15) & ~UINT64_C(15);
    if (base < 0x80200000u || base >= RAM_END)
        rt_fatal("mods: dynamic model arena has no space after static resources");
    room.abi = MODMEM_ABI; room.active = 1;
    room.base = room.cursor = (uint32_t)base;
    room.limit = room.scratch_floor = RAM_END;
    room.owner = owner;
    g_ram_ext_used = 1;
    rt_log("mods: dynamic model room arena %08X-%08X, owner %08X", room.base, room.limit, owner);
}
void modmem_budget(uint64_t bytes, uint64_t scratch) {
    if (!room.active) return;
    uint64_t capacity = room.limit - room.base;
    if (bytes > capacity || scratch > capacity - bytes)
        rt_fatal("mods: room model preflight needs %llu + %llu scratch bytes; arena holds %llu",
                 (unsigned long long)bytes, (unsigned long long)scratch, (unsigned long long)capacity);
}
void modmem_require(uint32_t address, uint64_t bytes, const char *purpose) {
    if (!room.active) return;
    if (address < room.base || address > room.scratch_floor || bytes > (uint64_t)room.scratch_floor - address)
        rt_fatal("mods: dynamic model arena exhausted before %s (%08X, %llu bytes)",
                 purpose, address, (unsigned long long)bytes);
    uint32_t end = address + (uint32_t)bytes;
    if (end > room.cursor) room.cursor = end;
}
uint32_t modmem_scratch(uint64_t bytes, const char *purpose) {
    if (!room.active) rt_fatal("mods: dynamic scratch requested without an arena");
    uint64_t rounded = (bytes + 15) & ~UINT64_C(15);
    uint32_t ceiling = room.stage_base ? room.stage_base : room.limit;
    if (bytes > RAM_SIZE || rounded > (uint64_t)ceiling - room.cursor)
        rt_fatal("mods: dynamic model scratch exhausted before %s (%llu bytes)", purpose, (unsigned long long)bytes);
    uint32_t start = ceiling - (uint32_t)rounded;
    if (start < room.scratch_floor) room.scratch_floor = start;
    return start;
}
/* Packed data stays live across loader steps and asynchronous sector writes.
 * Scratch must be below it; both retain a conservative high-water reservation
 * until the next room. No packed acquisition advances the model heap cursor. */
void modmem_stage_release(void) {
    room.stage_base=room.stage_bytes=room.stage_table=room.stage_index=room.stage_cancelled=0;
}
uint32_t modmem_stage_acquire(uint32_t table, uint32_t index, uint32_t bytes) {
    if (!room.active || !modmem_split_identity(table,index) || !bytes || (bytes & 2047u))
        rt_fatal("mods: invalid compressed staging request");
    if (room.stage_base) {
        if (!room.stage_cancelled || stage_io_pending())
            rt_fatal("mods: compressed staging is still owned by an earlier read");
        modmem_stage_release();
    }
    uint64_t need=((uint64_t)bytes+4+15)&~UINT64_C(15);
    if (need > (uint64_t)room.limit-room.cursor)
        rt_fatal("mods: compressed staging exhausted before CD request");
    room.stage_base=room.limit-(uint32_t)need;
    room.stage_bytes=bytes; room.stage_table=table; room.stage_index=index;
    if (room.stage_base<room.scratch_floor) room.scratch_floor=room.stage_base;
    return room.stage_base;
}
uint32_t modmem_stage_source(void) {
    if (room.stage_cancelled) rt_fatal("mods: cancelled compressed staging cannot be decoded");
    return room.stage_base;
}
int modmem_stage_read(uint32_t table, uint32_t index, uint32_t address, uint64_t bytes) {
    return room.active && room.stage_base && !room.stage_cancelled &&
        table==room.stage_table && index==room.stage_index &&
        address==room.stage_base && bytes==room.stage_bytes;
}
void modmem_stage_cancel(void) {
    /* CancelRead only cancels the high-level request, not the kernel CD queue.
     * Keep ownership until queued sector writes have drained. */
    if (room.stage_base) room.stage_cancelled=1;
}
int modmem_heap_take(uint32_t top, uint64_t bytes, uint32_t *next) {
    if (!room.active || top < room.base || top > room.scratch_floor || bytes > (uint64_t)room.scratch_floor - top) return 0;
    *next = top + (uint32_t)bytes;
    if (*next > room.cursor) room.cursor = *next;
    return 1;
}
void modmem_checkpoint(uint32_t models, uint32_t top) {
    if (!room.active) return;
    if (!models || models > MODMEM_MODELS || top < room.base || top > room.scratch_floor)
        rt_fatal("mods: invalid dynamic model checkpoint (%u, %08X)", models, top);
    room.checkpoints[models - 1] = top;
    room.models = models;
    if (top > room.cursor) room.cursor = top;
}
uint32_t modmem_retain(uint32_t models) {
    if (!room.active || !models || models > room.models || !room.checkpoints[models - 1])
        rt_fatal("mods: dynamic model reload has no retained checkpoint (%u)", models);
    uint32_t top = room.checkpoints[models - 1];
    memset(room.checkpoints + models, 0, (MODMEM_MODELS - models) * sizeof(uint32_t));
    room.models = models; room.cursor = top;
    return top;
}
void modmem_export(ModMemoryState *state) {
    *state = room;
    if (room.active) memcpy(&state->cursor, ram_ptr(HEAP_CURSOR), 4);
}
int modmem_validate(const ModMemoryState *s, uint32_t top) {
    if (!s->active) {
        ModMemoryState zero = {0};
        return !memcmp(s, &zero, sizeof zero) && top < 0x80200000u;
    }
    uint32_t expected = (modfs_static_end() + 15u) & ~15u;
    if (!modmem_enabled() || !modfs_dynamic_models() || s->active != 1 || s->abi != MODMEM_ABI ||
        s->base != expected || s->base < 0x80200000u || s->limit != RAM_END ||
        s->owner < 0x80000000u || s->owner >= 0x80200000u || (s->owner & 3) ||
        s->scratch_floor < s->base || s->scratch_floor > s->limit || (s->scratch_floor & 15) ||
        s->cursor < s->base || s->cursor > s->scratch_floor || s->cursor != top || s->models > MODMEM_MODELS) return 0;
    if (s->stage_base) {
        uint64_t need=((uint64_t)s->stage_bytes+19)&~UINT64_C(15);
        uint32_t packed,parts[3];
        if (!modmem_split_identity(s->stage_table,s->stage_index) || !s->stage_bytes || (s->stage_bytes&2047u) ||
            need > (uint64_t)s->limit-s->base || s->stage_base != s->limit-need ||
            s->scratch_floor>s->stage_base || s->cursor>s->stage_base || s->stage_cancelled>1 ||
            !modfs_model_request_size(s->stage_table,(int)s->stage_index,&packed,parts) ||
            packed!=s->stage_bytes || !parts[0] || !parts[1] || !parts[2]) return 0;
    } else if (s->stage_bytes || s->stage_table || s->stage_index || s->stage_cancelled) return 0;
    uint32_t last = s->base;
    for (uint32_t i = 0; i < MODMEM_MODELS; i++) {
        uint32_t p = s->checkpoints[i];
        if (i < s->models) { if (p < last || p > s->cursor || (p & 3)) return 0; last = p; }
        else if (p) return 0;
    }
    return 1;
}
void modmem_restore(const ModMemoryState *state) { room = *state; if (room.active) g_ram_ext_used = 1; }
