/* Stage program 15 (STAGPROG overlay 15: stage 0, the hospital): the
 * per-frame room hook (rooms 2, 4 and 6).
 *
 * Room 2 has four breakable things (0x5C-byte records from 0x800F5270):
 * while the player shoots, each one not yet broken (event flags 7-10)
 * that the shot reached (0x800F3948) breaks (0x800F3BF8: sounds, its VRAM
 * patch, the flag, debris). Its music (track 1) fades out once three of
 * the room's enemies are dead and then goes back to track 0. Rooms 4 and
 * 6 stop or swap the music by event and room flags.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"
#include "libgs.h"
#include "poll.h"

void n_stage15_RoomFrame(void);
void n_stage15_InitHooks(void);
void n_stage15_RoomTable(void);
void n_stage15_RoomEnter(void);

#define g_lastButtons   0x800F53E0u           /* u16 */
#define g_musicState    GV(s32, 0x800F51F8)   /* room 2: -1 track 1 on, -2 fading; room 4: 1 stopped */
#define g_room6Music    GV(s32, 0x800F51FC)   /* room 6: music swapped */
#define g_enemies       GV(u32, 0x800AB510)   /* 0x8C4-byte records; +0x31E s16 health */
#define g_lightObj      GV(u32, 0x800B60CC)
#define TARGETS         0x800F5270u           /* 0x5C-byte records */
#define TARGET_SIZE     0x5C
#define F_TARGET_HIT    0x800F3948u           /* (target, i); ovl15, not decompiled */
#define F_TARGET_BREAK  0x800F3BF8u           /* (i, flag, debris mode); ovl15 */
#define F_ROOM4_FRAME   0x800F4134u           /* (0); ovl15 */
#define F_MUSIC_FADE    0x80011EC4u           /* (track, speed) */
#define F_MUSIC_FADING  0x80011FE8u           /* nonzero while a fade runs */

/* 800F3D34: every frame, by room. Returns 0. */
void n_stage15_RoomFrame(void) {
    FRAME(0x20);
    switch (g_room) {
    case 2:
        if (stage_NotFiring(g_lastButtons) == 0) {
            for (u32 i = 0; i < 4; i++) {
                if (flag_test(EVENT_FLAGS, 7 + i)) continue;
                if (GCALL2(F_TARGET_HIT, TARGETS + i * TARGET_SIZE, i) == 0) continue;
                GCALL3(F_TARGET_BREAK, i, 7 + i, 0xE);
            }
        }
        if (g_musicTrack == 1) {
            if (g_musicState == -1) {
                s32 dead = 0;
                u32 e = g_enemies;
                for (s32 i = 0; i < 3; i++, e += 0x8C4)
                    if (GV(s16, e + 0x31E) <= 0) dead++;
                if (dead >= 3) {
                    g_musicState = -2;
                    GCALL2(F_MUSIC_FADE, (u32)(s32)g_musicTrack, 0x100);
                }
            } else if (g_musicState == -2) {
                if (GCALL0(F_MUSIC_FADING) == 0) {
                    GCALL1(F_MUSIC_STOP, 1);
                    g_musicTrack = 0;
                    GCALL1(F_MUSIC_PLAY, 0);
                }
            }
        }
        if (flag_test(EVENT_FLAGS, 0x23)) GV(u32, g_lightObj + 0xC8) = 0xD00;
        break;
    case 4:
        if (!flag_test(EVENT_FLAGS, 0x28)) flag_set(EVENT_FLAGS, 0x28);
        if (flag_test(EVENT_FLAGS, 4)) {
            if (g_musicState != 0) break;
            GCALL1(F_MUSIC_STOP, (u32)(s32)g_musicTrack);
            g_musicState = 1;
        } else {
            GCALL1(F_ROOM4_FRAME, 0);
        }
        break;
    case 6:
        if (g_room6Music != 0) break;
        if (!flag_test(ROOM_FLAGS, 0x1F)) break;
        if (!flag_test(ROOM_FLAGS, 0x20)) break;
        if (!flag_test(ROOM_FLAGS, 0x21)) break;
        GCALL1(F_MUSIC_STOP, (u32)(s32)g_musicTrack);
        g_musicTrack = 0;
        GCALL1(F_MUSIC_PLAY, 0);
        g_room6Music = 1;
        break;
    }
    V0 = 0;
}

/* 800F2C70: the stage's hooks (0x800B4BAC, 0x800BBB5C). */
void n_stage15_InitHooks(void) {
    GV(u32, 0x800B4BAC) = 0x800F5254u;
    GV(u32, 0x800BBB5C) = 0x800F5240u;
}

/* 800F2C94: the room table and this room's entry (enemy layout list and
 * its length). Returns 0. */
void n_stage15_RoomTable(void) {
    u32 table = 0x800F50F0u;
    s32 idx = g_roomIndex;
    GV(u32, ROOM_INFO) = table;
    GV(u32, 0x800B3D14) = 0x800F5184u;
    u32 entry = table + (u32)idx * 8;
    GV(u32, ROOM_INFO + 8) = GV(u32, entry);
    GV(u32, 0x800A89B0) = 0;
    GV(u16, ROOM_INFO + 0xE) = GV(u16, entry + 4);
    V0 = 0;
}

/* 800F2CE8: entering a room (by g_room). Room 1: an effect slot, a
 * light ramp, the music check and a message unless continuing. Room 2:
 * the four targets' records built; the broken ones (event flags 7-10)
 * patched in VRAM and pushed far back; after event flag 0x23 the model
 * at 0x800A8FF0's parts set to their broken frames (else a light
 * copied); after flag 3 the window patch and the room's enemies already
 * gone (room flags 0x11A-0x11C) put down; effect slots; the music (track
 * 1 after flag 0x23, else the room's ambience). Room 4: the music by
 * flag 0x4E of 0x80062FA0. Room 6: the music by room flags 0x1F-0x21.
 * Others: the music check (0x800F34E4). Returns 0. */
void n_stage15_RoomEnter(void) {
    FRAME(0x30);
    u32 in_s[5];
    for (int i = 0; i < 5; i++) { in_s[i] = R[16 + i]; GS(u32, sp + 0x18 + 4 * i) = R[16 + i]; }
    GS(u32, sp + 0x2C) = R[31];
    const s32 room = g_room;
    R[16] = (u32)room;
    switch (room) {
    case 1:
        gcall_ra(0x800F2D58u, 0x8009806Cu, 8, 0x800C3E50u, A2, A3);
        gcall_ra(0x800F2D68u, 0x800F4AF0u, 8, (u32)-0x40, 0x40, A3);
        gcall_ra(0x800F2D70u, 0x800F34E4u, A0, A1, A2, A3);
        if (GV(u8, 0x80062DF4u) != 3) {
            GV(u32, 0x800A8954u) = (u32)room;
            gcall_ra(0x800F2D94u, F_MESSAGE, 0, 3, A2, A3);
        }
        goto out;
    case 2: {
        const u32 ev = EVENT_FLAGS;
        R[17] = ev;
        for (u32 k = 0; k < 4; k++)
            for (u32 b = 0; b < 0x10; b++) GV(u8, TARGETS + k * TARGET_SIZE + b) = GV(u8, 0x800F51A0u + k * 0x10 + b);
        GS(u16, sp + 0x10) = 0x3E0;
        GS(u16, sp + 0x12) = 0x80;
        R[18] = 0x800A89C0u;
        const u32 mtx = GV(u32, 0x800A89C0u + 0x16C);
        GS(u16, sp + 0x14) = 0x1F;
        for (u32 k = 0; k < 4; k++) GV(u32, TARGETS + k * TARGET_SIZE + 0x30) = mtx;
        for (u32 k = 0; k < 4; k++)
            for (u32 b = 0; b < 0x20; b += 4) GV(u32, TARGETS + k * TARGET_SIZE + 0x10 + b) = GV(u32, 0x80065B90u + b);
        R[19] = LIGHTS;
        const u32 list = GV(u32, LIGHTS + 0xC);
        R[16] = list;
        GS(u16, sp + 0x16) = 0x7F;
        static const u32 brk[4][7] = {   /* flag, x, y, id, ra test, ra move, ra sync */
            {7, 0x340, 0, 0x800F51F0u, 0x800F3024u, 0x800F3038u, 0x800F3060u},
            {8, 0x360, 0x80, 0x800F51F2u, 0x800F306Cu, 0x800F3080u, 0x800F30A8u},
            {9, 0x3A0, 0x80, 0x800F51F4u, 0x800F30B4u, 0x800F30C8u, 0x800F30F0u},
            {10, 0x3A0, 0, 0x800F51F6u, 0x800F30FCu, 0x800F3110u, 0x800F3138u},
        };
        for (int k = 0; k < 4; k++) {
            if (gcall_ra(brk[k][4], F_FLAG_TEST, ev, brk[k][0], A2, A3) == 0) continue;
            gcall_ra(brk[k][5], 0x800356E0u, sp + 0x10, brk[k][1], brk[k][2], A3);
            GV(u32, list + (u32)(s32)GV(s16, brk[k][3]) * 20 + 0x10) = (u32)-0x3E8;
            gcall_ra(brk[k][6], 0x80035374u, 0, A1, A2, A3);
        }
        if (gcall_ra(0x800F3144u, F_FLAG_TEST, ev, 0x23, A2, A3) != 0) {
            const u32 obj = 0x800A89C0u + 0x630;
            const s32 n = GV(s32, obj + 0x1D0);
            if (n > 0) {
                for (s32 j = 0;;) {
                    const u32 pp = 0x800A89C0u + 0x804 + (u32)j * 4;
                    GV(u32, obj + 0x274 + (u32)j * 4) = GV(u32, pp) + 0x18;
                    GV(u16, GV(u32, pp) + 8) = 0xFFFF;
                    GV(u16, GV(u32, pp) + 0x16) = 0xC8;
                    GV(u16, GV(u32, pp) + 0x14) = 0xC7;
                    GV(u16, GV(u32, pp) + 0xA) = 0xFFFF;
                    { const u32 q = GV(u32, pp); GV(u8, q + 0xC) = GV(u8, q + 0x1A); }
                    GV(u8, GV(u32, pp) + 0xD) = 0x10;
                    j++;
                    if (!(j < n)) break;
                    LOOP_POLL(0x800F31D8u);
                }
            }
        } else {
            const u32 l = GV(u32, LIGHTS + 4);
            GV(u32, l + 0xC8) = GV(u32, l + 0xC0);
        }
        if (gcall_ra(0x800F320Cu, F_FLAG_TEST, EVENT_FLAGS, 3, A2, A3) != 0) {
            GS(u16, sp + 0x10) = 0x3E0;
            GS(u16, sp + 0x12) = 0x80;
            GS(u16, sp + 0x14) = 0x1F;
            GS(u16, sp + 0x16) = 0x7F;
            gcall_ra(0x800F3240u, 0x800356E0u, sp + 0x10, 0x3A0, 0x80, A3);
            const u32 lay = GV(u32, 0x800BBB88u);
            R[16] = lay;
            gcall_ra(0x800F3250u, 0x80035374u, 0, A1, A2, A3);
            u32 e = lay + 4;
            if (GV(s16, e) != -1) {
                R[17] = 0x800A0000u; R[20] = 1; R[19] = (u32)-0x5DC;
                u32 f = lay + 6;
                for (;;) {
                    R[16] = f; R[18] = e;
                    if (gcall_ra(0x800F3284u, F_FLAG_TEST, ROOM_FLAGS, (u32)(s32)GV(s16, f), A2, A3) == 0) {
                        const s32 id = GV(s16, f);
                        u32 o = 0xFFFFFFFFu;
                        if (id == 0x11A) o = 0;
                        else if (id == 0x11B) o = 0x8C4;
                        else if (id == 0x11C) o = 0x1188;
                        if (o != 0xFFFFFFFFu) {
                            GV(u8, g_enemies + o + 0x29) = 1;
                            const u32 b = g_enemies;
                            GV(u32, b + o + 0x198) = (u32)-0x5DC;
                            GV(u16, b + o + 0x85E) = 0;
                            GV(u16, b + o + 0x860) = 0;
                        }
                    }
                    f += 0x18;
                    e += 0x18;
                    if (GV(s16, e) == -1) break;
                    LOOP_POLL(0x800F3340u);
                }
            }
            gcall_ra(0x800F3350u, 0x8006DB40u, A0, A1, A2, A3);
        }
        R[16] = 0x800C3E50u;
        gcall_ra(0x800F3360u, 0x8009806Cu, 0, 0x800C3E50u, A2, A3);
        gcall_ra(0x800F336Cu, 0x8009806Cu, 6, 0x800C3E50u, A2, A3);
        if (gcall_ra(0x800F3380u, F_FLAG_TEST, EVENT_FLAGS, 0x23, A2, A3) != 0) {
            R[16] = 0x80060000u;
            const s32 t = g_musicTrack;
            if (t != 0) gcall_ra(0x800F33A0u, F_MUSIC_STOP, (u32)t, A1, A2, A3);
            g_musicTrack = 1;
            goto play1;
        }
        R[17] = 0x80060000u;
        {
            const s32 t = g_musicTrack;
            if (t != 0) gcall_ra(0x800F33C8u, F_MUSIC_STOP, (u32)t, 0x46, A2, A3);
        }
        R[16] = 0x80060000u;
        g_musicTrack = 0;
        gcall_ra(0x800F33E0u, 0x8001DDA8u, (u32)(s32)GV(s16, 0x80062E70u), 0x46, 0x46, A3);
        gcall_ra(0x800F33F0u, 0x8001D118u, (u32)(s32)GV(s16, 0x80062E70u), 1, 3, A3);
        goto out;
    }
    case 4:
        if (gcall_ra(0x800F3408u, F_FLAG_TEST, FLAGS_2FA0, 0x4E, A2, A3) != 0) {
            g_musicState = 1;
            goto out;
        }
        R[16] = 0x80060000u;
        {
            const s32 t = g_musicTrack;
            if (t != 0) gcall_ra(0x800F3438u, F_MUSIC_STOP, (u32)t, A1, A2, A3);
        }
        g_musicTrack = 0;
        gcall_ra(0x800F3444u, F_MUSIC_PLAY, 0, A1, A2, A3);
        goto out;
    case 6:
        R[17] = 0x800F0000u;
        g_room6Music = 0;
        R[16] = ROOM_FLAGS;
        if (gcall_ra(0x800F3464u, F_FLAG_TEST, ROOM_FLAGS, 0x1F, A2, A3) != 0
            && gcall_ra(0x800F3474u, F_FLAG_TEST, ROOM_FLAGS, 0x20, A2, A3) != 0
            && gcall_ra(0x800F3484u, F_FLAG_TEST, ROOM_FLAGS, 0x21, A2, A3) != 0) {
            g_room6Music = 1;
            break;
        }
        gcall_ra(0x800F349Cu, F_MUSIC_STOP, 0, A1, A2, A3);
        g_musicTrack = 1;
        goto play1;
    default:
        break;
    }
    gcall_ra(0x800F34C0u, 0x800F34E4u, A0, A1, A2, A3);
    goto out;
play1:
    gcall_ra(0x800F34B0u, F_MUSIC_PLAY, 1, A1, A2, A3);
out:
    for (int i = 0; i < 5; i++) R[16 + i] = in_s[i];
    V0 = 0;
}
