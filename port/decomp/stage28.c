/* Stage program 28 (STAGPROG overlay 28: stage 13): the per-frame room
 * hook (rooms 0, 3, 4 and 5) and its "not shooting" test.
 *
 * Room 0 has three breakable things (event flags 0x30-0x32, 0x5C-byte
 * records at 0x800F4B3C, 0x800F4B98, 0x800F4BF4): while the player shoots,
 * one the shot reached (0x800F37C8) breaks: two sounds, its broken
 * picture (MoveImage), its flag, debris and its target record's depth.
 * When the room's first enemy (room id 0xC0) is up and room flag 0xC7 is
 * set, event flag 0x33: music track 1, and the second and third things
 * break by themselves (other debris). Music back to track 0 with room
 * flags 0xC0 and 0xC1. Room 3 has six more (flags 0x58-0x5D, records from
 * 0x800F4B3C). Room 4 notes event flag 0x39; room 5 swaps the music by
 * flags.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"
#include "poll.h"

void n_stage28_NotFiring(void);
void n_stage28_RoomFrame(void);

#define g_lastButtons   0x800F4DC0u           /* u16 */
#define g_musicState    GV(s32, 0x800F4B18)   /* 1 track 1 on (room 0) / swapped (room 5), 2 waiting */
#define g_room4Flag     GV(s32, 0x800F48E8)
#define TARGET_SIZE     0x5C
#define ENEMIES          GV(u32, 0x800AB510)   /* 0x8C4-byte records */
#define ENEMY_COUNT      GV(s32, 0x800AF530)
#define ENEMY_SIZE       0x8C4u
#define TARGETS         0x800F4B3Cu           /* 0x5C-byte records */
#define ROOM0_IDS       0x800F4A74u           /* s16 target ids of room 0's three */
#define ROOM0_PLACES    0x800F4A68u           /* u16 x, y each */
#define ROOM3_IDS       0x800F4B08u
#define ROOM3_PLACES    0x800F4AECu
#define F_TARGET_HIT    0x800F37C8u           /* (target, id): ovl28, not decompiled */
#define F_NOT_FIRING    0x800F3A9Cu

/* 800F3A9C (result): *result = is the player not shooting
 * (stage_NotFiring, last buttons at 0x800F4DC0). */
void n_stage28_NotFiring(void) {
    V0 = stage_NotFiringResult(A0, g_lastButtons);
}

/* Break target t (id at `id`, place at `place`) with flag `flag`, sounds
 * in banks b and b + 1, the patch at `rect`, debris `mode`. */
static void stage28_break(u32 sp, u32 t, u32 flag, u32 rect, u32 place, u32 bank, u32 mode) {
    sound_at(6, bank);
    sound_at(6, bank + 1);
    GCALL3(F_MOVE_IMAGE, rect, GV(u16, place), GV(u16, place + 2));
    GCALL1(F_DRAW_SYNC, 0);
    flag_set(EVENT_FLAGS, flag);
    GS(u32, sp + 0x10) = mode;
    GCALL4(F_EN_DRAW, t, 0, 0, 0);
}

/* 800F3BA8: every frame, by room. Returns 0. */
void n_stage28_RoomFrame(void) {
    FRAME(0x58);
    u32 idle = sp + 0x28;
    switch (g_room) {
    case 0: {
        GCALL1(F_NOT_FIRING, idle);
        u32 calm = 1;
        u32 e = ENEMIES;
        if (GV(s16, e + 0x850) == 0xC0 && !flag_test(EVENT_FLAGS, 0x33) && flag_test(ROOM_FLAGS, 0xC7)) {
            e = ENEMIES;
            if (GV(u8, e + 0x29) != 0 && GV(u32, e + 0x170) != 0) {
                flag_set(EVENT_FLAGS, 0x33);
                GCALL1(F_MUSIC_STOP, 0);
                g_musicTrack = 1;
                GCALL1(F_MUSIC_PLAY, 1);
                calm = 0;
                g_musicState = 1;
                GS(u32, idle) = 0;
            }
        }
        if (GS(u32, idle) == 0) {
            u32 r = sp + 0x18;
            stage_Rect(r, 0x380, 0x88, 0x1D, 0x3F);
            u32 list = g_targetList;
            if (!flag_test(EVENT_FLAGS, 0x30) &&
                GCALL2(F_TARGET_HIT, TARGETS, (s32)GV(s16, ROOM0_IDS)) != 0) {
                stage28_break(sp, TARGETS, 0x30, r, ROOM0_PLACES, 4, 0xE);
                GV(s32, list + (u32)(s32)GV(s16, ROOM0_IDS) * 0x14 + 0x10) = -0x3E8;
            }
            stage_Rect(r, 0x3A0, 0x98, 0xF, 0x67);
            for (u32 k = 1; k < 3; k++) {
                if (flag_test(EVENT_FLAGS, 0x30 + k)) continue;
                u32 t = TARGETS + k * TARGET_SIZE;
                if (GCALL2(F_TARGET_HIT, t, (s32)GV(s16, ROOM0_IDS + k * 2)) == 0 && calm) continue;
                stage28_break(sp, t, 0x30 + k, r, ROOM0_PLACES + k * 4, 4, calm ? 0xE : 0x11);
                GV(s32, list + (u32)(s32)GV(s16, ROOM0_IDS + k * 2) * 0x14 + 0x10) = -0x3E8;
            }
        }
        if (g_musicState != 1) break;
        if (!flag_test(ROOM_FLAGS, 0xC0)) break;
        if (!flag_test(ROOM_FLAGS, 0xC1)) break;
        GCALL1(F_MUSIC_STOP, 1);
        g_musicTrack = 0;
        GCALL1(F_MUSIC_PLAY, 0);
        g_musicState = 0;
        break;
    }
    case 3: {
        GCALL1(F_NOT_FIRING, idle);
        if (GS(u32, idle) != 0) break;
        u32 r = sp + 0x20;
        stage_Rect(r, 0x2E8, 0x80, 0xC, 0x20);
        for (u32 k = 0; k < 6; k++) {
            if (k == 3) stage_Rect(r, 0x2F2, 0x40, 0xC, 0x20);
            else if (k) LOOP_POLL(k < 3 ? 0x800F40FCu : 0x800F421Cu);
            u32 t = TARGETS + k * TARGET_SIZE;
            if (flag_test(EVENT_FLAGS, 0x58 + k)) continue;
            if (GCALL2(F_TARGET_HIT, t, (s32)GV(s16, ROOM3_IDS + k * 2)) == 0) continue;
            stage28_break(sp, t, 0x58 + k, r, ROOM3_PLACES + k * 4, 2, 0xE);
        }
        break;
    }
    case 4:
        if (flag_test(EVENT_FLAGS, 0x39)) g_room4Flag = 1;
        break;
    case 5:
        if (flag_test(ROOM_FLAGS, 0xC7) && !flag_test(EVENT_FLAGS, 0x9F)) flag_set(EVENT_FLAGS, 0x9F);
        if (g_musicState == 0) {
            if (!flag_test(ROOM_FLAGS, 0xC7)) break;
            GCALL1(F_MUSIC_STOP, (s32)g_musicTrack);
            g_musicTrack = 0;
            GCALL1(F_MUSIC_PLAY, 0);
            g_musicState = 1;
        } else if (g_musicState == 2) {
            if (!flag_test(EVENT_FLAGS, 0x90)) break;
            GCALL1(F_MUSIC_STOP, 0);
            g_musicTrack = 1;
            GCALL1(F_MUSIC_PLAY, 1);
            g_musicState = 0;
        }
        break;
    }
    V0 = 0;
}
