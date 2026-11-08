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
#include "libgs.h"
#include "poll.h"

void n_stage24_RoomFrame(void);
void n_stage24_Room3Blink(void);
void n_stage24_InitHooks(void);
void n_stage24_RoomTable(void);
void n_stage24_MusicCheck(void);
void n_stage24_RoomEnter(void);

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

/* 800F2C54: the stage's hooks (0x800B4BAC, 0x800BBB5C). */
void n_stage24_InitHooks(void) {
    GV(u32, 0x800B4BAC) = 0x800F425Cu;
    GV(u32, 0x800BBB5C) = 0x800F4254u;
}

/* 800F2C78: the room table and this room's entry (enemy layout list and
 * its length). In room 1, until area flag 6 is set, also sets the pause
 * flag 0x800BBD10 and the screen tint record 0x800BBD00 (code 0x60,
 * colour 16,16,16). Returns 0. */
void n_stage24_RoomTable(void) {
    FRAME(0x18);
    u32 table = 0x800F418Cu;
    s32 idx = g_roomIndex;
    GV(u32, ROOM_INFO) = table;
    GV(u32, 0x800B3D14) = 0x800F41ECu;
    u32 entry = table + (u32)idx * 8;
    GV(u32, ROOM_INFO + 8) = GV(u32, entry);
    GV(u32, 0x800A89B0) = 0;
    GV(u16, ROOM_INFO + 0xE) = GV(u16, entry + 4);
    if (idx == 1 && GCALL2(0x8008F64Cu, 0x80062FC0u, 6) == 0) {
        GV(u8, 0x800BBD10) = (u8)idx;
        GV(u32, 0x800BBD00) = 0x60000000u;
        GV(u8, 0x800BBD0C) = 0x10;
        GV(u8, 0x800BBD0D) = 0x10;
        GV(u8, 0x800BBD0E) = 0x10;
    }
    V0 = 0;
}

/* 800F31B8: the music back to track 0 when it was swapped (track 1 on)
 * or the track wanted (0x80062E7C) isn't the one playing (0x8008FB40). */
void n_stage24_MusicCheck(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x18, R[31]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 in_s0 = R[16], in_s1 = R[17];
    R[17] = 0x80060000u;
    if (g_musicTrack == 0) {
        R[16] = (u32)(s32)GV(s16, 0x80062E7Cu);
        const u32 v = gcall_ra(0x800F31F4u, 0x8008FB40u, GV(u32, 0x800B3D4Cu), GV(u32, 0x800B3CB8u), A2, A3);
        if (R[16] == v) goto out;
    }
    g_musicTrack = 0;
    gcall_ra(0x800F3208u, 0x8001209Cu, 0, A1, A2, A3);
out:
    R[16] = in_s0;
    R[17] = in_s1;
}

/* 800F2D2C: entering a room (by g_room). Room 0: the boss music while
 * enemies are alive. Room 1 (after event flag 6): its VRAM patched (the
 * picture and a palette row), a message the first time (flag 5) or the
 * effect at 0x800A8FF0, a sound; else 0x800A901B set. Room 2: the two
 * targets' records built (positions, the player's model's matrix, a
 * template), the broken ones (event flags 0x17, 0x18) patched in VRAM and
 * their targets pushed far back, two effect slots, and the music by the
 * flags. Room 3: the palette blink, after flag 6 a sound and blink 2, a
 * message unless continuing. Then the music check (stage24_MusicCheck).
 * Returns 0. */
void n_stage24_RoomEnter(void) {
    FRAME(0x50);
    u32 in_s[7];
    for (int i = 0; i < 7; i++) { in_s[i] = R[16 + i]; GS(u32, sp + 0x30 + 4 * i) = R[16 + i]; }
    GS(u32, sp + 0x4C) = R[31];
    const s32 room = g_room;
    R[16] = (u32)room;
    R[17] = 1;
    u32 play = 0;
    switch (room) {
    case 0:
        if (gcall_ra(0x800F2D9Cu, F_COUNT_ALIVE, A0, A1, A2, A3) != 0) {
            gcall_ra(0x800F2DACu, F_MUSIC_STOP, 0, A1, A2, A3);
            g_musicTrack = 1;
            play = 1;
            goto music;
        }
        gcall_ra(0x800F2DC4u, 0x800F31B8u, A0, A1, A2, A3);
        g_musicState = 1;
        goto out;
    case 1: {
        const u32 ev = EVENT_FLAGS;
        R[16] = ev;
        if (gcall_ra(0x800F2DE4u, F_FLAG_TEST, ev, 6, A2, A3) == 0) {
            GV(u8, 0x800A901Bu) = 1;
            break;
        }
        GS(u16, sp + 0x18) = 0x2C0;
        GS(u16, sp + 0x1C) = 0x40;
        GS(u16, sp + 0x1A) = 0;
        GS(u16, sp + 0x1E) = 0x100;
        gcall_ra(0x800F2E14u, 0x800356E0u, sp + 0x18, 0x300, 0, A3);
        gcall_ra(0x800F2E1Cu, 0x80035374u, 0, A1, A2, A3);
        GS(u16, sp + 0x20) = 0x200;
        GS(u16, sp + 0x22) = 0x1E3;
        GS(u16, sp + 0x24) = 0x10;
        GS(u16, sp + 0x26) = 1;
        gcall_ra(0x800F2E48u, 0x800356E0u, sp + 0x20, 0x200, 0x1E2, A3);
        gcall_ra(0x800F2E50u, 0x80035374u, 0, A1, A2, A3);
        if (gcall_ra(0x800F2E5Cu, F_FLAG_TEST, ev, 5, A2, A3) == 0) {
            gcall_ra(0x800F2E70u, F_FLAG_SET, ev, 5, A2, A3);
            gcall_ra(0x800F2E7Cu, F_MESSAGE, 0, 0, A2, A3);
        } else {
            gcall_ra(0x800F2E8Cu, 0x800925C4u, 0x800A8FF0u, A1, A2, A3);
        }
        GS(u32, sp + 0x10) = 0x10;
        gcall_ra(0x800F2EA8u, 0x80080670u, 6, 1, 0x7F, 0x7F);
        break;
    }
    case 2: {
        R[19] = 2;
        u32 t = TARGETS, src = 0x800F4208u;
        for (s32 i = 0;;) {
            for (u32 k = 0; k < 8; k++) GV(u8, t + k) = GV(u8, src + k);
            for (u32 k = 0; k < 8; k++) GV(u8, t + 8 + k) = GV(u8, src + 8 + k);
            src += 0x10;
            GV(u32, t + 0x30) = GV(u32, 0x800A89C0u + 0x16C);
            for (u32 k = 0; k < 0x20; k += 4) GV(u32, t + 0x10 + k) = GV(u32, 0x80065B90u + k);
            i++;
            t += 0x5C;
            if (!(i < 2)) break;
            LOOP_POLL(0x800F2F84u);
        }
        const u32 list = g_targetList;
        R[22] = list;
        R[21] = TARGET_PLACES;
        GS(u16, sp + 0x28) = 0x3C0;
        GS(u16, sp + 0x2A) = 0x80;
        GS(u16, sp + 0x2C) = 0x1F;
        GS(u16, sp + 0x2E) = 0x7F;
        for (s32 i = 0;;) {
            R[16] = (u32)i;
            R[19] = TARGET_IDS + (u32)i * 2;
            R[18] = 2 + (u32)i * 4;
            R[17] = TARGET_PLACES + (u32)i * 4;
            R[20] = 0x80060000u;
            if (gcall_ra(0x800F2FE0u, F_FLAG_TEST, EVENT_FLAGS, (u32)i + 0x17, A2, A3) != 0) {
                gcall_ra(0x800F2FF8u, 0x800356E0u, sp + 0x28, GV(u16, R[17]), GV(u16, R[18] + TARGET_PLACES), A3);
                GV(u32, list + (u32)(s32)GV(s16, R[19]) * 20 + 0x10) = (u32)-0x4F7;
                gcall_ra(0x800F301Cu, 0x80035374u, 0, A1, A2, A3);
            }
            i++;
            if (!(i < 2)) break;
            LOOP_POLL(0x800F302Cu);
        }
        gcall_ra(0x800F3048u, 0x8009806Cu, 0, 0x800C3E50u, A2, A3);
        gcall_ra(0x800F3054u, 0x8009806Cu, 6, 0x800C3E50u, A2, A3);
        R[17] = 0x800F0000u;
        g_musicState = 0;
        if (gcall_ra(0x800F306Cu, F_FLAG_TEST, EVENT_FLAGS, 0x3D, A2, A3) == 0) {
            R[16] = ROOM_FLAGS;
            gcall_ra(0x800F3100u, 0x800F31B8u, A0, A1, A2, A3);
            g_musicState = 2;
            goto out;
        }
        R[16] = ROOM_FLAGS;
        if (gcall_ra(0x800F3080u, F_FLAG_TEST, ROOM_FLAGS, 0x82, A2, A3) != 0
            && gcall_ra(0x800F3090u, F_FLAG_TEST, ROOM_FLAGS, 0x83, A2, A3) != 0
            && gcall_ra(0x800F30A0u, F_FLAG_TEST, ROOM_FLAGS, 0x84, A2, A3) != 0
            && gcall_ra(0x800F30B0u, F_FLAG_TEST, ROOM_FLAGS, 0x85, A2, A3) != 0) {
            gcall_ra(0x800F30C0u, 0x800F31B8u, A0, A1, A2, A3);
            g_musicState = 1;
            goto out;
        }
        gcall_ra(0x800F30D4u, F_MUSIC_STOP, 0, A1, A2, A3);
        g_musicTrack = 1;
        play = 1;
        goto music;
    }
    case 3:
        R[19] = 2;
        R[18] = 0x800F0000u;
        g_blink = -1;
        if (gcall_ra(0x800F3124u, F_FLAG_TEST, EVENT_FLAGS, 6, A2, A3) != 0) {
            GS(u32, sp + 0x10) = 0x10;
            gcall_ra(0x800F3144u, 0x80080670u, 6, 2, 0x7F, 0x7F);
            g_blink = 2;
            gcall_ra(0x800F3150u, 0x800F3DC8u, 2, A1, A2, A3);
        }
        gcall_ra(0x800F3158u, 0x800F31B8u, A0, A1, A2, A3);
        if (GV(u8, 0x80062DF4u) != (u32)room) {
            GV(u32, 0x800A8954u) = 1;
            gcall_ra(0x800F317Cu, F_MESSAGE, 0, 8, A2, A3);
        }
        goto out;
    default:
        break;
    }
    gcall_ra(0x800F318Cu, 0x800F31B8u, A0, A1, A2, A3);
    goto out;
music:
    gcall_ra(0x800F30ECu, F_MUSIC_PLAY, play, A1, A2, A3);
    g_musicState = 0;
out:
    for (int i = 0; i < 7; i++) R[16 + i] = in_s[i];
    V0 = 0;
}
