/* Stage program 26 (STAGPROG overlay 26: stage 11): the room table, the
 * room-start hook and the per-frame hook (rooms 2-8).
 *
 * Rooms 2, 3 and 6 have breakable things (0x5C-byte records from
 * 0x800F57F8, set up at room start from a table and a common template):
 * shooting one (0x800F3FB0 says whether the shot reached it) breaks it:
 * a sound, its VRAM patch copied over the intact picture (MoveImage), an
 * event flag, its debris (F_EN_DRAW) and its target record pushed away
 * (depth -1000). Ones already broken get their patch at room start.
 * Rooms 2, 4 and 7 swap the music by event and room flags; room 2's
 * machine hums while the player stands on one side of a line
 * (stage26_HumUpdate); several rooms animate their lights by palette.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"
#include "poll.h"

void n_stage26_RoomTable(void);
void n_stage26_Room4Leap(void);
void n_stage26_RoomStart(void);
void n_stage26_PlayerSide(void);
void n_stage26_HumUpdate(void);
void n_stage26_NotFiring(void);
void n_stage26_RoomFrame(void);
void n_stage22_MusicCheck(void);

#define g_lightIdx      GV(u16, 0x800F5DC2)   /* s16 */
#define g_musicState    GV(s32, 0x800F57A0)   /* 0 track 1 playing, 1 back to 0, 2 waiting for the event */
#define g_lastButtons   0x800F5DD0u           /* u16 */
#define g_humSide       GV(s32, 0x800F5DC4)   /* stage26_PlayerSide when last looked */
#define g_humTimer      GV(s32, 0x800F5DC8)
#define g_humBlock      GV(s32, 0x800BAA00)   /* bit 0 flag; else frames left */
#define g_room4Flag     GV(u8, 0x800F5DBB)
#define g_room4Lit      GV(s32, 0x800AB51C)
#define LAMP_A          0x800A8FF0u           /* +0x2B bit 2: switched */
#define LAMP_B          0x800A9308u           /* (LAMP_A + 0x318) */
#define TARGETS         0x800F57F8u           /* 0x5C-byte records */
#define TARGET_SIZE     0x5C
#define TARGET_TEMPLATE 0x80065B90u           /* 0x20 bytes copied to +0x10 */
#define F_TARGET_HIT    0x800F3FB0u           /* (target, range); ovl26, not decompiled */
#define F_HUM_SOUND     0x80080670u           /* (sound, bank, left, right, 5th on the stack) */
#define F_ROOM2_START   0x800F38B4u           /* ovl26, not decompiled */
#define F_ROOM3_START   0x800F3AB0u
#define F_ROOM3_FRAME   0x800F3AF0u
#define F_ROOM4_LAMP    0x800F3570u           /* stage26_Room4Leap (0 start, 1 step) */
#define F_ROOM4_EVENT   0x800F4DCCu

/* Room 4's leaping enemy (the one with room id 0x26): its index and the
 * jump's frame, phase and velocity. */
#define LEAP_INDEX      GV(u8, 0x800F5DB8)
#define LEAP_FRAME      GV(u8, 0x800F5DB9)
#define LEAP_PHASE      GV(u8, 0x800F5DBA)
#define LEAP_VX         GV(u16, 0x800F5DBC)   /* s16 */
#define LEAP_VY         GV(u16, 0x800F5DBE)
#define LEAP_VZ         GV(u16, 0x800F5DC0)
#define ENEMY_BASE      GV(u32, 0x800AB510)
#define ENEMY_COUNT_    GV(s32, 0x800AF530)

/* The player's place where the leap ends (0xCE4, 0, -0x5DC, facing
 * 0x800). */
static void leap_player(void) {
    u32 o = g_playerObj + 0x24;
    GV(s32, o + 4) = 0xCE4;
    GV(s32, o + 0xC) = -0x5DC;
    GV(s32, o + 8) = 0;
    GV(u16, o + 0x12) = 0x800;
}

/* 800F3570 (go): room 4, the lights back on: an enemy (room id 0x26)
 * leaps in. 0: find it and set up the jump; then each frame it moves by
 * its velocity: rising for 8 frames, then falling and slowing towards
 * z -0x5DC until it lands (y 0), which puts the player in place. Once
 * it has landed it (and the player) are put straight there. */
void n_stage26_Room4Leap(void) {
    if (A0 == 0) {
        LEAP_PHASE = 0;
        LEAP_FRAME = 0;
        LEAP_VX = 0xFA;
        LEAP_VY = 0;
        LEAP_VZ = 0x190;
        g_room4Flag = 0;
        if (ENEMY_COUNT_ <= 0) { V0 = 0x800F0000u; return; }
        u32 base = ENEMY_BASE;
        for (s32 i = 0;;) {
            if (GV(s16, base + (u32)i * 0x8C4 + 0x850) == 0x26) {
                LEAP_INDEX = (u8)i;
                i = ENEMY_COUNT_;
            }
            i++;
            if (!(i < ENEMY_COUNT_)) break;
            LOOP_POLL(0x800F3618u);
        }
        V0 = 0;
        return;
    }
    if (g_room4Flag != 0) {
        s32 n = ENEMY_COUNT_;
        if (n <= 0) { V0 = (u32)n; return; }
        u32 base = ENEMY_BASE;
        for (s32 i = 0;;) {
            if (GV(s16, base + (u32)i * 0x8C4 + 0x850) == 0x26) {
                LEAP_INDEX = (u8)i;
                u32 e = base + (u32)(i & 0xFF) * 0x8C4;
                if (GV(s32, e + 0x19C) != 0) {
                    GV(s32, e + 0x198) = 0xCE4;
                    GV(s32, e + 0x19C) = 0;
                    GV(s32, e + 0x1A0) = -0x5DC;
                    u32 o = g_playerObj + 0x24;
                    GV(s32, o + 4) = 0xCE4;
                    GV(s32, o + 8) = 0;
                    GV(s32, o + 0xC) = -0x5DC;
                    GV(u16, o + 0x12) = 0x800;
                }
                i = ENEMY_COUNT_;
            }
            i++;
            if (!(i < n)) break;
            LOOP_POLL(0x800F3850u);
        }
        V0 = 0;
        return;
    }
    u32 e = ENEMY_BASE + (u32)LEAP_INDEX * 0x8C4;
    GV(u32, e + 0x198) += (u32)(s32)(s16)LEAP_VX;
    GV(u32, e + 0x1A0) += (u32)(s32)(s16)LEAP_VZ;
    GV(u32, e + 0x19C) += (u32)(s32)(s16)LEAP_VY;
    u8 phase = LEAP_PHASE;
    if (phase == 0) {
        LEAP_VY = (u16)(LEAP_VY + 8);
        if (LEAP_FRAME >= 8) LEAP_PHASE = 1;
    } else if (phase == 1) {
        LEAP_VZ = (u16)(LEAP_VZ - 10);
        LEAP_VY = (u16)(LEAP_VY + 0x28);
        if (!(GV(s32, e + 0x1A0) < -0x5DC)) {
            GV(s32, e + 0x1A0) = -0x5DC;
            LEAP_VZ = 0;
            LEAP_VX = 0;
        }
        u32 e2 = ENEMY_BASE + (u32)LEAP_INDEX * 0x8C4;
        if (GV(s32, e2 + 0x19C) >= 0) {
            GV(s32, e2 + 0x19C) = 0;
            g_room4Flag = phase;
            leap_player();
        }
    }
    u32 f = (u32)LEAP_FRAME + 1;
    LEAP_FRAME = (u8)f;
    V0 = f;
}

/* 800F2CB4: the room table and this room's entry (enemy layout list and
 * its length); room 8 also sets two flags. Returns 0. */
void n_stage26_RoomTable(void) {
    u32 table = 0x800F5578u;
    s32 room = g_roomIndex;
    GV(u32, ROOM_INFO) = table;
    GV(u32, 0x800B3D14) = 0x800F5644u;
    u32 entry = table + (u32)room * 8;
    GV(u32, ROOM_INFO + 8) = GV(u32, entry);
    GV(u32, 0x800A89B0) = 0;
    GV(u16, ROOM_INFO + 0xE) = GV(u16, entry + 4);
    if (room == 8) {
        GV(u16, 0x800B3D3C) = 1;
        GV(u16, 0x800AF568) = 1;
    }
    V0 = 0;
}

/* Set up n breakable things from a table of 0x10-byte entries: each
 * record gets its entry (+0), the template (+0x10..0x2F) and the word at
 * 0x800A8B2C (+0x30). */
static void targets_init(u32 table, s32 n) {
    u32 t = TARGETS;
    for (s32 i = 0; i < n; i++, t += TARGET_SIZE) {
        for (u32 k = 0; k < 0x10; k++) GV(u8, t + k) = GV(u8, table + (u32)i * 0x10 + k);
        GV(u32, t + 0x30) = GV(u32, 0x800A89C0u + 0x16C);
        for (u32 k = 0; k < 0x20; k += 4) GV(u32, t + 0x10 + k) = GV(u32, TARGET_TEMPLATE + k);
    }
}

/* A broken thing's target record: pushed away. */
static void target_remove(u32 list, s16 id) {
    GV(s32, list + (u32)(s32)id * 20 + 0x10) = -1000;
}

/* The patches of things already broken (event flags flag0 + i): one
 * MoveImage per thing (rect at sp+r) to the place in `places` (x, y
 * halves); target ids from `ids`. */
static void patches_restore(u32 r, s32 n, u32 flag0, u32 places, u32 ids) {
    u32 list = g_targetList;
    for (s32 i = 0; i < n; i++) {
        if (!flag_test(EVENT_FLAGS, flag0 + (u32)i)) continue;
        GCALL3(F_MOVE_IMAGE, r, GV(u16, places + (u32)i * 4), GV(u16, places + (u32)i * 4 + 2));
        GCALL1(F_DRAW_SYNC, 0);
        target_remove(list, GV(s16, ids + (u32)i * 2));
    }
}

static void load_sounds(void) {
    GCALL2(F_LOAD_SOUNDS, 0, SOUND_BUFFER);
    GCALL2(F_LOAD_SOUNDS, 6, SOUND_BUFFER);
}

/* 0x800F350C (stop the music unless the track playing is the one wanted)
 * is the same code as stage22_MusicCheck: the room start runs that. */

static void music_to(u32 stop, s16 track) {
    GCALL1(F_MUSIC_STOP, stop);
    g_musicTrack = track;
    GCALL1(F_MUSIC_PLAY, (u32)(s32)track);
}

/* 800F2D24: a room starts (rooms 2-8): breakable things and their
 * patches, sound banks, the music by flags, room 4's lamps. Returns 0. */
void n_stage26_RoomStart(void) {
    FRAME(0x50);
    switch (g_room) {
    case 2:
        GCALL0(F_ROOM2_START);
        g_lightIdx = 0;
        targets_init(0x800F566Cu, 2);
        stage_Rect(sp + 0x10, 0x36C, 0x80, 0x10, 0x40);
        stage_Rect(sp + 0x18, 0x1D0, 0x1E1, 0x10, 1);
        patches_restore(sp + 0x10, 2, 0x4E, 0x800F568Cu, 0x800F569Cu);
        load_sounds();
        g_musicState = 0;
        if (flag_test(EVENT_FLAGS, 0xB0)) {
            if (!flag_test(ROOM_FLAGS, 0x97) || !flag_test(ROOM_FLAGS, 0x98)) {
                music_to(0, 1);
                g_musicState = 0;
            } else {
                n_stage22_MusicCheck();
                g_musicState = 1;
            }
        } else {
            n_stage22_MusicCheck();
            g_musicState = 2;
        }
        break;
    case 3:
        GCALL0(F_ROOM3_START);
        targets_init(0x800F56A0u, 8);
        GS(u16, sp + 0x18) = 0x280;
        GS(u16, sp + 0x1A) = 0x40;
        GS(u16, sp + 0x1C) = 0x10;
        GS(u16, sp + 0x1E) = 0x40;
        patches_restore(sp + 0x18, 8, 0x50, 0x800F5720u, 0x800F5740u);
        load_sounds();
        n_stage22_MusicCheck();
        break;
    case 4:
        GV(s32, 0x800A8610) = 1;
        g_lightIdx = 0;
        g_musicState = 0;
        if (flag_test(EVENT_FLAGS, 0x47)) {
            if (flag_test(ROOM_FLAGS, 0x9B)) {
                n_stage22_MusicCheck();
                g_musicState = 1;
            } else {
                music_to(0, 1);
                g_musicState = 0;
            }
        } else {
            n_stage22_MusicCheck();
            g_musicState = 2;
        }
        if (flag_test(EVENT_FLAGS, 0x47)) {
            GV(u8, LAMP_A + 0x2B) |= 4;
            clut_anim(0x220, 0x1E0, 1);
        } else {
            GV(u8, LAMP_B + 0x2B) |= 4;
        }
        g_room4Flag = 1;
        if (!flag_test(EVENT_FLAGS, 0x8C)) GV(s32, 0x800A8954) = 1;
        GV(s32, 0x800A8610) = 1;
        GCALL2(F_MESSAGE, 0, 4);
        break;
    case 5:
        g_lightIdx = 0;
        n_stage22_MusicCheck();
        break;
    case 6: {
        targets_init(0x800F5750u, 3);
        stage_Rect(sp + 0x20, 0x2F0, 0x80, 0x10, 0x80);
        stage_Rect(sp + 0x28, 0x1D0, 0x1E0, 0x10, 1);
        for (s32 i = 0; i < 3; i++) {
            if (!flag_test(EVENT_FLAGS, 0x4B + (u32)i)) continue;
            GCALL3(F_MOVE_IMAGE, sp + 0x20, GV(u16, 0x800F5780u + (u32)i * 4), GV(u16, 0x800F5782u + (u32)i * 4));
            GCALL3(F_MOVE_IMAGE, sp + 0x28, GV(u16, 0x800F578Cu + (u32)i * 4), GV(u16, 0x800F578Eu + (u32)i * 4));
            GCALL1(F_DRAW_SYNC, 0);
        }
        load_sounds();
        GV(s32, 0x800A8610) = 1;
        n_stage22_MusicCheck();
        break;
    }
    case 7:
        g_musicState = 0;
        if (flag_test(EVENT_FLAGS, 0xB3)) {
            if (flag_test(ROOM_FLAGS, 0xA0)) {
                n_stage22_MusicCheck();
                g_musicState = 1;
            } else {
                music_to(0, 1);
                g_musicState = 0;
            }
        } else {
            n_stage22_MusicCheck();
            g_musicState = 0;
        }
        break;
    case 8:
        GV(s32, 0x800A9304) = 0;
        n_stage22_MusicCheck();
        break;
    default:
        n_stage22_MusicCheck();
        break;
    }
    V0 = 0;
}

/* 800F3860: room 2: which side of the machine's line the player is on:
 * 0 in front (x from 0x1060, on the line's positive side), else -1. */
void n_stage26_PlayerSide(void) {
    FRAME(0x18);
    if (PLAYER_POS[0] < 0x1060) { V0 = (u32)-1; return; }
    V0 = player_line_side(0x800F57A4u, 0x800F57B4u) >= 0 ? 0 : (u32)-1;
}

/* Sound 6 with the fifth argument (0x10) on the stack. */
static void hum_sound(u32 sp, u32 bank, u32 vol) {
    GS(u32, sp + 0x10) = 0x10;
    GCALL4(F_HUM_SOUND, 6, bank, vol, vol);
}

/* 800F38FC: room 2, every frame (no event running): the machine's hum.
 * Stepping to the other side of the line starts it (event flag 0x60) for
 * 150 frames; then it stops and the side is looked at again. The lights
 * flicker with it (g_lightIdx / 3: frames 1-2, or 3-4 while 0x800BAA00
 * counts down). */
void n_stage26_HumUpdate(void) {
    FRAME(0x20);
    if (g_eventBusy != 0) return;
    if (g_humBlock != 0) {
        if (flag_test(0x800BAA00u, 0)) {
            g_humBlock = 0x96;
        } else {
            clut_anim(0x240, 0x1F0, s16v((s16)g_lightIdx / 3 + 3));
            s32 t = g_humBlock - 1;
            g_humBlock = t;
            if (t <= 0) g_humBlock = 0;
        }
        g_humSide = (s32)GCALL0(0x800F3860u);
        return;
    }
    s32 t = g_humTimer;
    if (t != 0) {
        t--;
        g_humTimer = t;
        if (t <= 0) {
            hum_sound(sp, 0, 0);
            g_humSide = (s32)GCALL0(0x800F3860u);
            g_humTimer = 0;
            GCALL2(F_FLAG_CLEAR, EVENT_FLAGS, 0x60);
        }
    } else if ((s32)GCALL0(0x800F3860u) != g_humSide) {
        hum_sound(sp, 2, 0x7F);
        g_humTimer = 0x96;
        flag_set(EVENT_FLAGS, 0x60);
    }
    clut_anim(0x240, 0x1F0, s16v((s16)g_lightIdx / 3 + 1));
}

/* 800F4274 (result): *result = is the player not shooting
 * (stage_NotFiring, last buttons at 0x800F5DD0). */
void n_stage26_NotFiring(void) {
    u32 res = A0;
    u32 idle = stage_NotFiring(g_lastButtons);
    GS(u32, res) = idle;
    s16 weapon = GV(s16, g_currentWeaponAddr);
    V0 = (weapon < 9 && weapon >= 7) ? 1 : (u32)GV(u16, g_lastButtons);
}

/* Shooting a breakable thing (record t, its target id at `id`): two
 * sounds (bank b, b+1), its patch over the picture, the flag, debris. */
static void target_break(u32 sp, u32 t, u32 flag, u32 rect, u32 place, u32 bank) {
    sound_at(6, bank);
    sound_at(6, bank + 1);
    GCALL3(F_MOVE_IMAGE, rect, GV(u16, place), GV(u16, place + 2));
    GCALL1(F_DRAW_SYNC, 0);
    flag_set(EVENT_FLAGS, flag);
    GS(u32, sp + 0x10) = 0xE;
    GCALL4(F_EN_DRAW, t, 0, 0, 0);
}

/* Room 2's two machines: each breaks when shot, or at once when its event
 * flag (0x65, 0x66) says so (with a flash and bigger debris). */
static void room2_machine(u32 sp, u32 list, u32 i) {
    u32 flag = 0x4E + i, forced = 0x65 + i;
    u32 t = TARGETS + i * TARGET_SIZE;
    u32 id = 0x800F569Cu + i * 2;
    if ((u32)GCALL2(F_TARGET_HIT, t, (s32)GV(s16, id)) == 0 && !flag_test(EVENT_FLAGS, forced)) return;
    GCALL3(F_MOVE_IMAGE, sp + 0x18, GV(u16, 0x800F568Cu + i * 4), GV(u16, 0x800F568Eu + i * 4));
    GCALL1(F_DRAW_SYNC, 0);
    flag_set(EVENT_FLAGS, flag);
    u32 mode;
    if (flag_test(EVENT_FLAGS, forced)) {
        sound_at(6, 4);
        sound_at(6, 5);
        screen_flash();
        mode = 0x11;
    } else {
        sound_at(6, 4);
        sound_at(6, 5);
        mode = 0xE;
    }
    GS(u32, sp + 0x10) = mode;
    GCALL4(F_EN_DRAW, t, 0, 0, 0);
    target_remove(list, GV(s16, id));
}

/* 800F4380: every frame, by room (2-8). Returns 0. */
void n_stage26_RoomFrame(void) {
    FRAME(0x60);
    u32 idle = sp + 0x30;
    switch (g_room) {
    case 2:
        g_lightIdx = (u16)(g_lightIdx + 1);
        if ((s16)g_lightIdx >= 5) g_lightIdx = 1;
        clut_anim(0x240, 0x1F5, s16v((s16)g_lightIdx / 3 + 1));
        GCALL0(0x800F38FCu);
        GCALL1(0x800F4274u, idle);
        if (flag_test(EVENT_FLAGS, 0x65)) GS(u32, idle) = 0;
        if (flag_test(EVENT_FLAGS, 0x66)) GS(u32, idle) = 0;
        if (GS(u32, idle) == 0) {
            stage_Rect(sp + 0x18, 0x36C, 0x80, 0x10, 0x40);
            stage_Rect(sp + 0x20, 0x1D0, 0x1E1, 0x10, 1);
            u32 list = g_targetList;
            if (!flag_test(EVENT_FLAGS, 0x4E)) room2_machine(sp, list, 0);
            if (!flag_test(EVENT_FLAGS, 0x4F)) room2_machine(sp, list, 1);
        }
        if (g_musicState == 0) {
            if (!flag_test(ROOM_FLAGS, 0x97)) break;
            if (!flag_test(ROOM_FLAGS, 0x98)) break;
            music_to(1, 0);
            g_musicState = 1;
        } else if (g_musicState == 2) {
            if (!flag_test(EVENT_FLAGS, 0xB0)) break;
            music_to(0, 1);
            g_musicState = 0;
        }
        break;
    case 3: {
        g_lightIdx = (u16)(g_lightIdx + 1);
        if ((s16)g_lightIdx >= 5) g_lightIdx = 1;
        GCALL0(F_ROOM3_FRAME);
        GCALL1(0x800F4274u, idle);
        if (GS(u32, idle) != 0) break;
        u32 list = g_targetList;
        stage_Rect(sp + 0x18, 0x280, 0x40, 0x10, 0x40);
        for (u32 i = 0; i < 8; i++) {
            u32 t = TARGETS + i * TARGET_SIZE;
            u32 id = 0x800F5740u + i * 2;
            if (flag_test(EVENT_FLAGS, 0x50 + i)) continue;
            if (GCALL2(F_TARGET_HIT, t, (s32)GV(s16, id)) == 0) continue;
            target_break(sp, t, 0x50 + i, sp + 0x18, 0x800F5720u + i * 4, 2);
            target_remove(list, GV(s16, id));
        }
        break;
    }
    case 4: {
        s32 st = g_musicState;
        if (st == 0) {
            if (flag_test(ROOM_FLAGS, 0x9B)) {
                music_to(1, 0);
                g_musicState = 1;
            }
        } else if (st == 2) {
            if (flag_test(EVENT_FLAGS, 0x47)) {
                music_to((u32)(s32)g_musicTrack, 1);
                g_musicState = 0;
            }
        }
        if (flag_test(EVENT_FLAGS, 0x47)) {
            u32 off;
            if ((GV(u8, LAMP_A + 0x2B) & 4) == 0) {
                g_room4Lit = 1;
                screen_flash();
                sound_at(6, 1);
                GV(u8, LAMP_B + 0x2B) &= 0xFB;
                GV(u8, LAMP_A + 0x2B) |= 4;
                clut_anim(0x220, 0x1E0, 1);
                GCALL0(F_ROOM4_EVENT);
                off = 0;
            } else {
                off = 1;
            }
            GCALL1(F_ROOM4_LAMP, off);
            if (flag_test(FLAGS_2FA0, 0x26)) GV(u16, g_playerObj + 0x42) = 0;
        } else {
            g_room4Lit = 0;
            u16 on = (s16)g_lightIdx == 0;
            g_lightIdx = on;
            clut_anim(0x230, 0x1E0, (s32)on + 1);
        }
        break;
    }
    case 5: {
        u16 on = (s16)g_lightIdx == 0;
        g_lightIdx = on;
        clut_anim(0x180, 0x1E0, (s32)on + 1);
        break;
    }
    case 6:
        GCALL1(0x800F4274u, idle);
        if (GS(u32, idle) == 0) {
            stage_Rect(sp + 0x20, 0x2F0, 0x80, 0x10, 0x80);
            stage_Rect(sp + 0x28, 0x1D0, 0x1E0, 0x10, 1);
            for (u32 i = 0; i < 3; i++) {
                u32 t = TARGETS + i * TARGET_SIZE;
                if (flag_test(EVENT_FLAGS, 0x4B + i)) continue;
                if (GCALL2(F_TARGET_HIT, t, (s32)GV(s16, 0x800F5798u + i * 2)) == 0) continue;
                sound_at(6, 1);
                sound_at(6, 2);
                GCALL3(F_MOVE_IMAGE, sp + 0x20, GV(u16, 0x800F5780u + i * 4), GV(u16, 0x800F5782u + i * 4));
                GCALL3(F_MOVE_IMAGE, sp + 0x28, GV(u16, 0x800F578Cu + i * 4), GV(u16, 0x800F578Eu + i * 4));
                GCALL1(F_DRAW_SYNC, 0);
                flag_set(EVENT_FLAGS, 0x4B + i);
                GS(u32, sp + 0x10) = 0xE;
                GCALL4(F_EN_DRAW, t, 0, 0, 0);
            }
        }
        if (flag_test(EVENT_FLAGS, 0xA3)) break;
        if (player_line_side(0x800F57C4u, 0x800F57D4u) < 0) break;
        GCALL2(F_MESSAGE, 0, 4);
        flag_set(EVENT_FLAGS, 0xA3);
        break;
    case 7:
        if (g_musicState != 0) break;
        if (!flag_test(EVENT_FLAGS, 0xB3)) break;
        if (!flag_test(ROOM_FLAGS, 0xA0)) break;
        music_to(1, 0);
        g_musicState = 1;
        break;
    case 8:
        GV(s32, 0x800A85DC) = GV(s16, 0x800AF56A) != 0 ? (s32)GV(s16, 0x800B4D6A) : 0;
        break;
    }
    V0 = 0;
}
