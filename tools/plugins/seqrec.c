/* Bench plugin: plays every room sound set's sequences in turn, so a
 * --wav recording of the run has each one (log lines say when).
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

static const RespectApi *api;
/* a room of each sound set (stage, room), from TEST.BIN's table at 0x800A42AC */
static const int rooms[][3] = {{0, 17, 21}, {1, 10, 28}, {3, 4, 13}, {4, 13, 20}, {6, 0, 2}, {7, 9, 25}, {8, 9, 26}, {10, 0, 0},
    {11, 3, 14}, {12, 0, 4}, {13, 3, 18}, {14, 4, 18}, {15, 4, 27}, {16, 6, 29}, {17, 0, 12}, {18, 0, 16}, {19, 12, 26},
    {20, 0, 29}, {21, 4, 22}, {25, 3, 13}, {26, 3, 24}, {27, 12, 21}, {28, 3, 23}, {29, 13, 16}, {30, 0, 7}, {31, 0, 25},
    {32, 13, 29}};
#define NROOMS (int)(sizeof rooms / sizeof rooms[0])
static int step = -1, wait = 600;

static int frame(void *user, uint32_t addr) {
    (void)user, (void)addr;
    if (--wait > 0) return 0;
    wait = 450;                                  /* 15 s each (DrawSync: 30 a second) */
    step++;
    int r = step / 2, seq = step % 2;
    if (r >= NROOMS) { if (r == NROOMS && !seq) api->log("seqrec: done"); return 0; }
    if (!seq) {
        api->call(SEQ_STOP, 0, 0, 0, 0);
        api->call(SEQ_STOP, 1, 0, 0, 0);
        int32_t *st = RESPECT_RAM(api, STAGE), *rm = RESPECT_RAM(api, ROOM);
        *st = rooms[r][1];
        *rm = rooms[r][2];
        api->call(LOAD_SET, 0, 0, 0, 0);
    } else
        api->call(SEQ_STOP, 0, 0, 0, 0);
    int16_t *ids = RESPECT_RAM(api, SEQ_IDS);
    if (ids[seq] < 0) { api->log("seqrec: set %d music%d none (frame %u)", rooms[r][0], seq + 1, api->frame()); wait = 1; return 0; }
    api->call(SEQ_PLAY, (uint32_t)seq, 0, 0, 0);
    api->log("seqrec: set %d music%d from frame %u (now %d)", rooms[r][0], seq + 1, api->frame(), *(int16_t *)RESPECT_RAM(api, SET_NOW));
    return 0;
}

RESPECT_EXPORT int respect_plugin_init(const RespectApi *a) {
    api = a;
    return a->hook(SCRIPT_FRAME, frame, 0, 0) ? 1 : 0;
}
