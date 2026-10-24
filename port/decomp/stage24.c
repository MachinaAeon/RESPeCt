/* Stage program 24 (STAGPROG overlay 24: stage 9, the area of the bird,
 * area56): the per-frame room hook (rooms 0, 2 and 3).
 *
 * Room 0 swaps the music once no enemy is left alive (enemy_CountAlive).
 * Room 2 has two breakable things: shot (or all at once when event flag
 * 0x3D is set), each patches its picture in VRAM, plays its sounds and
 * sets its event flag; the music swaps once room flags 0x82-0x85 are all
 * set, and back once flag 0x3D is. Room 3 blinks a palette row.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"

void n_stage24_RoomFrame(void);
void n_stage24_Room3Blink(void);

#define g_musicState     GV(s32, 0x800F4050)   /* 0, 1 swapped, 2 */
#define g_lastButtons    0x800F4330u
#define TARGETS          0x800F4278u           /* 0x5C-byte target records */
#define TARGET_IDS       0x800F4230u           /* s16 per target */
#define TARGET_PLACES    0x800F4228u           /* u16 x, y per target */
#define g_blink          GV(s16, 0x800F4268)
#define F_TARGET_HIT     0x800F361Cu           /* (target, id): shot (ovl24, not decompiled) */
#define F_COUNT_ALIVE    0x80085CF0u           /* enemy_CountAlive */

/* Is the player not shooting (1)? stage_NotFiring, except that here a
 * pad of any other type counts as not shooting. */
static u32 not_firing(void) {
    u32 t = g_padTypeBit, idle;
    if (t == 0x200000) {
        if (g_fireRequest == 0 || g_reticleX < 0) idle = 1;
        else idle = (u32)g_reticleY >> 31;
    } else if (t == 0x400000 || t == 0x80000) {
        idle = g_fireRequest == 0;
    } else {
        idle = 1;
    }
    s16 weapon = GV(s16, g_currentWeaponAddr);
    if (weapon < 9 && weapon >= 7) return g_weaponFiring == -1;
    if (GV(u16, g_lastButtons) & 4) idle = 1;
    GV(u16, g_lastButtons) = D_800AFCB0;
    return idle;
}

/* Stop the music playing and start track 0; state 1. */
static void music_swap(void) {
    GCALL1(F_MUSIC_STOP, (u32)(s32)g_musicTrack);
    g_musicTrack = 0;
    GCALL1(F_MUSIC_PLAY, 0);
    g_musicState = 1;
}

/* 800F38D0: every frame, by room. Returns 0. */
void n_stage24_RoomFrame(void) {
    FRAME(0x50);
    switch (g_room) {
    case 0:
        if (g_musicState == 0 && GCALL0(F_COUNT_ALIVE) == 0) music_swap();
        break;
    case 2: {
        u32 idle = not_firing();
        u32 all = flag_test(EVENT_FLAGS, 0x3D) != 0;
        if (all) idle = 0;
        if (idle == 0) {
            u32 list = g_targetList;
            GS(u32, sp + 0x20) = list;
            stage_Rect(sp + 0x18, 0x3C0, 0x80, 0x1F, 0x7F);
            for (u32 i = 0; i < 2; i++) {
                u32 flag = 0x17 + i;
                if (i != 0) RT_POLL_NOSAVE(0x800F3BF4);
                if (flag_test(EVENT_FLAGS, flag)) continue;
                u32 t = TARGETS + i * 0x5C;
                if (GCALL2(F_TARGET_HIT, t, (u32)(s32)GV(s16, TARGET_IDS + i * 2)) == 0 && !all) continue;
                GCALL3(F_MOVE_IMAGE, sp + 0x18, GV(u16, TARGET_PLACES + i * 4), GV(u16, TARGET_PLACES + i * 4 + 2));
                GCALL1(F_DRAW_SYNC, 0);
                flag_set(EVENT_FLAGS, flag);
                if (!all) {
                    GCALL4(F_SOUND_AT, 6, 4, 0x7F, 0x7F);
                    GS(u32, sp + 0x10) = 0xE;
                    GCALL4(F_EN_DRAW, t, 0, 0, 0);
                } else {
                    GCALL4(F_SOUND_AT, 6, 2, 0x7F, 0x7F);
                    GCALL4(F_SOUND_AT, 6, 1, 0x7F, 0x7F);
                    GS(u32, sp + 0x10) = 0x11;
                    GCALL4(F_EN_DRAW, t, 0, 0, 0);
                    screen_flash();
                }
                GV(s32, GS(u32, sp + 0x20) + (u32)(s32)GV(s16, TARGET_IDS + i * 2) * 0x14 + 0x10) = -0x4F7;
            }
        }
        if (g_musicState == 0) {
            if (flag_test(ROOM_FLAGS, 0x82) && flag_test(ROOM_FLAGS, 0x83) &&
                flag_test(ROOM_FLAGS, 0x84) && flag_test(ROOM_FLAGS, 0x85))
                music_swap();
        } else if (g_musicState == 2) {
            if (flag_test(EVENT_FLAGS, 0x3D)) {
                GCALL1(F_MUSIC_STOP, 0);
                g_musicTrack = 1;
                GCALL1(F_MUSIC_PLAY, 1);
                g_musicState = 0;
            }
        }
        break;
    }
    case 3:
        n_stage24_Room3Blink();
        break;
    }
    V0 = 0;
}

/* 800F3E14: room 3: copy palette row 0xFC + g_blink (2 or 3, alternating;
 * negative: off) to row 0xFC. */
void n_stage24_Room3Blink(void) {
    FRAME(0x18);
    s16 b = g_blink;
    if (b < 0) return;
    clut_anim(0x300, 0xFC, b);
    g_blink = g_blink == 2 ? 3 : 2;
    V0 = 2;
}
