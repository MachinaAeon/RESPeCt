/* Stage program 31 (STAGPROG overlay 31: stage 16): the per-frame room
 * hook (rooms 6-8).
 *
 * Room 6 repeats a message every 30 frames once event flag 0xA4 is set;
 * room 7 starts a script on the player's model; room 8 has two breakable
 * things (0x5C-byte records at 0x800F4C38 and 0x800F4C94) broken by a
 * shot (0x800F3930 says whether it reached one) or, the second, at once
 * by event flag 0x64, like stage 11's (stage26.c).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"

void n_stage31_RoomFrame(void);

#define g_msgTimer      GV(s32, 0x800F4C00)
#define g_room7Script   GV(s32, 0x800F4DA8)
#define g_room8State    GV(s32, 0x800F4BFC)
#define g_lastButtons   0x800F4DACu
#define TARGET_1        0x800F4C38u
#define TARGET_2        0x800F4C94u
#define TARGET_IDS      0x800F4BD4u           /* s16 x 2 */
#define PATCH_PLACES    0x800F4BCCu           /* (x, y) u16 x 2 */
#define F_TARGET_HIT    0x800F3930u           /* (target, range); ovl31, not decompiled */
#define F_ROOM8_EVENT   0x800F34CCu           /* ovl31, not decompiled */
#define F_MODEL_SCRIPT  0x8008D51Cu           /* (slot, script, model) */

/* Break target i (its patch, sounds, flag 0x49 + i, debris in `mode`,
 * target record pushed away). */
static void target_break(u32 sp, u32 list, u32 i, u32 t, u32 mode_flag) {
    sound_at(6, 2);
    sound_at(6, 3);
    GCALL3(F_MOVE_IMAGE, sp + 0x18, GV(u16, PATCH_PLACES + i * 4), GV(u16, PATCH_PLACES + i * 4 + 2));
    GCALL1(F_DRAW_SYNC, 0);
    flag_set(EVENT_FLAGS, 0x49 + i);
    u32 mode = 0xE;
    if (mode_flag && flag_test(EVENT_FLAGS, 0x64)) mode = 0x11;
    GS(u32, sp + 0x10) = mode;
    GCALL4(F_EN_DRAW, t, 0, 0, 0);
    GV(s32, list + (u32)(s32)GV(s16, TARGET_IDS + i * 2) * 20 + 0x10) = -1000;
}

/* 800F3C04: every frame, by room. Returns 0. */
void n_stage31_RoomFrame(void) {
    FRAME(0x38);
    switch (g_room) {
    case 6:
        if (flag_test(EVENT_FLAGS, 0xA4)) {
            if (g_msgTimer % 30 == 0) GCALL2(F_MESSAGE, 0, 6);
            g_msgTimer = g_msgTimer + 1;
        }
        if (!flag_test(EVENT_FLAGS, 0xA4)) break;
        if (flag_test(EVENT_FLAGS, 0x46)) break;
        flag_set(EVENT_FLAGS, 0x46);
        break;
    case 7:
        if (g_room7Script == 0) break;
        if (g_eventBusy != 0) break;
        GCALL3(F_MODEL_SCRIPT, 0, 0x800F4BF8u, g_playerObj + 0x6C);
        break;
    case 8: {
        if (g_room8State == 0) {
            if (flag_test(EVENT_FLAGS, 0x64)) {
                GCALL0(F_ROOM8_EVENT);
                g_room8State = 1;
            }
        } else if (g_room8State == 1) {
            if (flag_test(ROOM_FLAGS, 0xEB) && flag_test(ROOM_FLAGS, 0xEC)) {
                GCALL1(F_MUSIC_STOP, 0);
                g_room8State = 2;
            }
        }
        u32 idle = stage_NotFiring(g_lastButtons);
        if (idle != 0 && !flag_test(EVENT_FLAGS, 0x64)) break;
        stage_Rect(sp + 0x18, 0x380, 0x70, 0x2A, 0x70);
        u32 list = g_targetList;
        if (!flag_test(EVENT_FLAGS, 0x49) &&
            GCALL2(F_TARGET_HIT, TARGET_1, (s32)GV(s16, TARGET_IDS)) != 0 && idle == 0)
            target_break(sp, list, 0, TARGET_1, 0);
        if (flag_test(EVENT_FLAGS, 0x4A)) break;
        if (GCALL2(F_TARGET_HIT, TARGET_2, (s32)GV(s16, TARGET_IDS + 2)) == 0 &&
            !flag_test(EVENT_FLAGS, 0x64))
            break;
        target_break(sp, list, 1, TARGET_2, 1);
        break;
    }
    }
    V0 = 0;
}
