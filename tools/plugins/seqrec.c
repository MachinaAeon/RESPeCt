/* Bench plugin: plays every sound set's sequences in turn, 45 s each, so a
 * --wav recording of the run has each one (log lines say when). Every set,
 * also those no room has in TEST's table (0x800A42AC): the set is written
 * into stage 0 room 17's entry before the room's set is loaded.
 * Built with: clang --target=x86_64-w64-mingw32 -shared -O2 -o seqrec.dll seqrec.c -I ../../port/include */
#include "respect_plugin.h"
#include <stdint.h>

#define SCRIPT_FRAME 0x80035374u   /* DrawSync, once a frame from the main loop */
#define LOAD_SET     0x8008F6A8u
#define SEQ_PLAY     0x8001209Cu
#define SEQ_STOP     0x8001215Cu
#define STAGE        0x800B4BA0u
#define ROOM         0x800B3E20u
#define SET_NOW      0x80062E7Cu
#define SEQ_IDS      0x80062E70u
#define ROOM_SETS    0x800A42ACu
#define SET_SEQS     0x800BBCC8u   /* u32 x 2: where the set's sequences are, 0 none */
#define NSETS        38            /* NOISE.BIN blocks 0xE6..0x10B */

static const RespectApi *api;
static int step = -1, wait = 600;

static int frame(void *user, uint32_t addr) {
    (void)user, (void)addr;
    if (--wait > 0) return 0;
    wait = 1350;                                 /* 45 s each (DrawSync: 30 a second) */
    step++;
    int set = step / 2, seq = step % 2;
    if (set >= NSETS) { if (set == NSETS && !seq) api->log("seqrec: done"); return 0; }
    api->call(SEQ_STOP, 0, 0, 0, 0);
    api->call(SEQ_STOP, 1, 0, 0, 0);
    *(int32_t *)RESPECT_RAM(api, STAGE) = 0;
    *(int32_t *)RESPECT_RAM(api, ROOM) = 17;
    uint32_t tbl = *(uint32_t *)RESPECT_RAM(api, ROOM_SETS);
    *(int16_t *)RESPECT_RAM(api, tbl + 17 * 4) = (int16_t)set;
    *(int16_t *)RESPECT_RAM(api, SET_NOW) = -1;  /* load it again */
    api->call(LOAD_SET, 0, 0, 0, 0);
    int16_t *ids = RESPECT_RAM(api, SEQ_IDS);   /* (a set with one sequence keeps the last set's second id) */
    if (ids[seq] < 0 || !((uint32_t *)RESPECT_RAM(api, SET_SEQS))[seq]) { api->log("seqrec: set %d music%d none (frame %u)", set, seq + 1, api->frame()); wait = 1; return 0; }
    api->call(SEQ_PLAY, (uint32_t)seq, 0, 0, 0);
    api->log("seqrec: set %d music%d from frame %u (now %d)", set, seq + 1, api->frame(), *(int16_t *)RESPECT_RAM(api, SET_NOW));
    return 0;
}

RESPECT_EXPORT int respect_plugin_init(const RespectApi *a) {
    api = a;
    return a->hook(SCRIPT_FRAME, frame, 0, 0) ? 1 : 0;
}
