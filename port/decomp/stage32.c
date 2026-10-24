/* Stage program 32 (STAGPROG overlay 32: stage 17): the per-frame room
 * hook (rooms 0 and 2).
 *
 * Room 0 cycles its lights by palette (brighter once event flag 0x74 is
 * set); room 2 swaps the music back once event flag 0xAD is set, steps a
 * palette animation from a table, and sets event flags with their
 * messages.
 *
 * Also one of its script ops (stage32_OpBlank).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"

void n_stage32_RoomFrame(void);
void n_stage32_OpBlank(void);

#define LIGHT_IDX       0x800F3AB8u          /* s8 */
#define LIGHT_TIMER     0x800F3AB9u          /* s8 */
#define g_lightIdx      GV(u8, LIGHT_IDX)
#define g_lightTimer    GV(u8, LIGHT_TIMER)
#define g_musicSwapped  GV(s32, 0x800F3ABC)
#define LIGHT_TABLE     0x800F3A70u          /* (frames, row) pairs */

/* The light frame for this step: (s8)(idx / 3) + k, as 16 bits. */
static s32 frame3(s32 k) { return s16v((s8)((s8)g_lightIdx / 3) + k); }

/* 800F2F80: every frame, by room. Returns 0. */
void n_stage32_RoomFrame(void) {
    FRAME(0x20);
    switch (g_room) {
    case 0:
        g_lightIdx = (u8)(g_lightIdx + 1);
        if ((s8)g_lightIdx >= 5) g_lightIdx = 1;
        clut_anim(0x240, 0x1F8, frame3(1));
        clut_anim(0x240, 0x1F5, frame3(1));
        clut_anim(0x240, 0x1F0, frame3(flag_test(EVENT_FLAGS, 0x74) ? 3 : 1));
        break;
    case 2:
        if (g_musicSwapped == 0 && flag_test(EVENT_FLAGS, 0xAD)) {
            GCALL1(F_MUSIC_STOP, 1);
            g_musicTrack = 0;
            GCALL1(F_MUSIC_PLAY, 0);
            g_musicSwapped = 1;
        }
        g_lightTimer = (u8)(g_lightTimer - 1);
        if ((s8)g_lightTimer <= 0)
            stage_ClutTableStep(LIGHT_IDX, LIGHT_TIMER, LIGHT_TABLE, 0x240, 0x1F0);
        if (flag_test(ROOM_FLAGS, 0xF4) && !flag_test(EVENT_FLAGS, 0xB6)) {
            GCALL2(F_MESSAGE, 0, 9);
            flag_set(EVENT_FLAGS, 0xB6);
        }
        if (flag_test(EVENT_FLAGS, 0x9A)) break;
        if (!flag_test(ROOM_FLAGS, 0x119)) break;
        flag_set(EVENT_FLAGS, 0x9A);
        GCALL2(F_MESSAGE, 0, 0);
        break;
    }
    V0 = 0;
}

/* 800F3404 (thread): script op: when it starts, the display goes off
 * (SetDispMask 0) and the game flow goes to handler 3 (g_flowHandler
 * 0x800BB19C), step 0x11 (g_flowStep 0x800A28CC). Moves on (pc + 4).
 * Returns 0. */
void n_stage32_OpBlank(void) {
    FRAME(0x18);
    u32 th = A0;
    if (GV(s16, th + 4) == 0) {
        GCALL1(0x800352DCu, 0);
        GV(s32, 0x800BB19C) = 3;
        GV(s32, 0x800A28CC) = 0x11;
    }
    GV(u32, th) += 4;
    V0 = 0;
}
