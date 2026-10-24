/* Stage program 22 (STAGPROG 22, loaded in the stage slot at 0x800F2C50):
 * the first area's rooms.
 *
 * A stage program gives the game per-room hooks: one run when a room
 * starts (stage22_RoomStart: sound bank, burning-fire effects, enemy
 * placement, music) and one every frame (stage22_RoomFrame: a trigger
 * line, the music changing with event flags, more flames).
 *
 * The fires are built from the game's effect records (0x44 bytes each,
 * from 0x800C09D8; the one being set up or updated is at 0x800C39A8, see
 * EF_*). A flame (stage22_FlameUpdate) flickers, sways and now and then
 * gives off a puff of smoke (stage22_SmokeUpdate) that rises, drifts and
 * shrinks away.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"

void n_stage22_InitHooks(void);
void n_stage22_RoomTable(void);
void n_stage22_RoomStart(void);
void n_stage22_MusicCheck(void);
void n_stage22_RoomFrame(void);
void n_stage22_PlaceEnemies(void);
void n_stage22_SpawnFlame(void);
void n_stage22_FlameUpdate(void);
void n_stage22_SmokeUpdate(void);
void n_stage22_SpawnGlow(void);

/* ---- the game's effect records ---- */
#define EFFECTS          0x800C09D8u           /* 0x44-byte records */
#define EFFECT_SIZE      0x44
#define EF_CUR           GV(u32, 0x800C39A8)   /* the record being run */
/* fields (p is the record's address) */
#define EF_FLAGS2(p)     GV(u16, (p) + 0x02)
#define EF_SIZE_W(p)     GV(u16, (p) + 0x08)   /* base width, height */
#define EF_SIZE_H(p)     GV(u16, (p) + 0x0A)
#define EF_X(p)          GV(u16, (p) + 0x10)
#define EF_Y(p)          GV(u16, (p) + 0x12)
#define EF_W(p)          GV(u16, (p) + 0x14)   /* drawn width, height */
#define EF_H(p)          GV(u16, (p) + 0x16)
#define EF_Z(p)          GV(u16, (p) + 0x18)
#define EF_LIFE(p)       GV(u16, (p) + 0x1A)
#define EF_FLAGS(p)      GV(u16, (p) + 0x1C)   /* 1: free */
#define EF_FRAME(p)      GV(u16, (p) + 0x1E)
#define EF_X0(p)         GV(u16, (p) + 0x26)   /* drift from the start (x, y, z) */
#define EF_Y0(p)         GV(u16, (p) + 0x28)
#define EF_Z0(p)         GV(u16, (p) + 0x2A)
#define EF_ANGLE(p)      GV(u16, (p) + 0x2C)
#define EF_SCALE_X(p)    GV(u16, (p) + 0x2E)
#define EF_VY(p)         GV(u16, (p) + 0x30)   /* flames: height scale; smoke: rise speed */
#define EF_SWAY(p)       GV(u16, (p) + 0x32)
#define EF_FADE(p)       GV(u16, (p) + 0x34)
#define EF_UPDATE(p)     GV(u32, (p) + 0x3C)   /* run every frame */
#define F_EFFECT_ALLOC   0x80092E3Cu   /* (): a free record's index, or -1 */
#define F_EFFECT_INIT    0x800945B4u   /* (record, template) */
#define F_EFFECT_SPRITE  0x800947D4u   /* (sprite def): start it (uses EF_CUR) */
#define F_EFFECT_ANIM    0x800940C4u   /* (): animate EF_CUR; also a stock update */
#define F_RAND_SDK       0x800398A8u   /* rand() */
#define F_SIN            0x80029FF8u   /* (angle) 4096 = 1.0 */
#define FLAME_TEMPLATE   0x800A7038u
#define GLOW_TEMPLATE    0x800A7010u
#define FLAME_SPRITE     0x800A7B00u
#define GLOW_SPRITE      0x800F42FCu
#define g_frameTick      GV(u32, 0x800C3A90)

/* ---- stage state ---- */
#define g_roomIndex      GV(s32, 0x800B3E20)
#define g_room           GV(s32, 0x800B3F08)
#define ROOM_INFO        0x800B3D28u           /* room table, entry, enemy layout */
#define g_musicTrack     GV(s16, 0x80062E78)
#define g_musicWanted    GV(s16, 0x80062E7C)
#define g_flameTimer     GV(s32, 0x800F4304)   /* room 1: frames to the next flare */
#define g_musicSwapped   GV(s32, 0x800F3DF0)   /* room 2: the music has changed */
#define EVENT_FLAGS      0x80062FC0u
#define ROOM_FLAGS       0x80062F60u
#define F_FLAG_TEST      0x8008F64Cu
#define F_FLAG_SET       0x8008F5F8u
#define F_LINE_SIDE      0x8007C04Cu   /* (a, b, x, z) */
#define F_LOAD_SOUNDS    0x8009806Cu   /* (kind, buffer) */
#define F_MUSIC_PLAY     0x8001209Cu   /* (track) */
#define F_MUSIC_STOP     0x8001215Cu
#define F_MUSIC_WHICH    0x8008FB40u
#define F_MESSAGE        0x8008D390u   /* (a, b): room message / event */
#define F_FLARE          0x800F3120u   /* ovl22, not decompiled */

static u32 flag_test(u32 flags, u32 bit) { return GCALL2(F_FLAG_TEST, flags, bit); }
static s32 sdk_rand(void) { return (s32)GCALL0(F_RAND_SDK); }

/* 800F2C68: the stage's hooks (two tables the game calls through). */
void n_stage22_InitHooks(void) {
    GV(u32, 0x800B4BAC) = 0x800F4198u;
    GV(u32, 0x800BBB5C) = 0x800F4194u;
}

/* 800F2C8C: the room table and this room's entry (enemy layout list
 * and its length). Returns 0. */
void n_stage22_RoomTable(void) {
    u32 table = 0x800F401Cu;
    GV(u32, ROOM_INFO) = table;
    GV(u32, 0x800B3D14) = 0x800F40E8u & 0x7FFFFFFF;
    u32 entry = table + (u32)g_roomIndex * 8;
    GV(u32, ROOM_INFO + 8) = GV(u32, entry);
    GV(u32, 0x800A89B0) = 0;
    GV(u16, ROOM_INFO + 0xE) = GV(u16, entry + 4);
    V0 = 0;
}

/* 800F2CEC: a room starts. Rooms 1-3 load the sound bank and light their
 * fires; room 1 places its enemies and arms the flare timer, room 2 picks
 * its music by event flags, room 4 lights a row of flames from a table.
 * Returns 0. */
void n_stage22_RoomStart(void) {
    FRAME(0x28);
    switch ((u32)g_room) {
    case 0:
        GV(s32, 0x800A8610) = 1;
        break;
    case 1:
        GCALL2(F_LOAD_SOUNDS, 3, 0x800C3E50u);
        GCALL3(0x800F3D10u, 0x4E20, -0xA8C, -0x125C);
        GCALL3(0x800F3D10u, 0x4E20, -0xA8C, 0x640);
        GCALL0(0x800F34C8u);
        if (flag_test(EVENT_FLAGS, 0x2B) == 0) g_flameTimer = 0x78;
        break;
    case 2:
        GCALL2(F_LOAD_SOUNDS, 3, 0x800C3E50u);
        GCALL3(0x800F3D10u, -0x8FC, -0x960, 0x157C);
        GCALL3(0x800F3D10u, -0x8FC, -0x960, -0x157C);
        g_musicSwapped = 0;
        if (flag_test(ROOM_FLAGS, 0x6D) && flag_test(ROOM_FLAGS, 0x6E)) {
            g_musicTrack = 1;
            GCALL1(F_MUSIC_PLAY, 1);
            g_musicSwapped = 1;
        } else {
            u32 track;
            if (flag_test(EVENT_FLAGS, 0x3E)) { g_musicTrack = 0; track = 0; }
            else { track = 1; g_musicTrack = 1; }
            GCALL1(F_MUSIC_PLAY, track);
            g_musicSwapped = 0;
        }
        V0 = 0;
        return;
    case 3:
        GCALL2(F_LOAD_SOUNDS, 3, 0x800C3E50u);
        GCALL3(0x800F3D10u, 0x4E20, -0xAF0, 0xC80);
        GCALL3(0x800F3D10u, 0x4E20, -0xAF0, -0xC80);
        GCALL0(0x800F2F54u);
        if (flag_test(EVENT_FLAGS, 0x87) == 0) GV(s32, 0x800A8954) = 1;
        GCALL2(F_MESSAGE, 0, 1);
        V0 = 0;
        return;
    case 4:
        /* flames from the table: x, y, z, (unused), and a fifth word on
         * the stack (two halves: horizontal and vertical scale) */
        for (u32 t = 0x800F40F0u; GV(s32, t) != -1; t += 0x14) {
            GS(u32, sp + 0x10) = GV(u32, t + 0x10);
            GCALL4(0x800F36BCu, GV(u32, t), GV(u32, t + 4), GV(u32, t + 8), GV(u32, t + 0xC));
        }
        GCALL0(0x800F2F54u);
        GV(u8, 0x80062DEC) = 0;
        GCALL2(F_MESSAGE, 0, 1);
        V0 = 0;
        return;
    }
    GCALL0(0x800F2F54u);
    V0 = 0;
}

/* 800F2F54: stop the music unless the track playing is the one wanted. */
void n_stage22_MusicCheck(void) {
    FRAME(0x20);
    if (g_musicTrack == 0) {
        s32 wanted = g_musicWanted;
        if ((s32)GCALL2(F_MUSIC_WHICH, GV(u32, 0x800B3D4C), GV(u32, 0x800B3CB8)) == wanted) return;
    }
    g_musicTrack = 0;
    GCALL1(F_MUSIC_PLAY, 0);
}

/* 800F2FB8: every frame. Room 0: crossing a line sets event flag 0x95
 * (and a message). Room 1: until flag 0x2B, a flare every 120 frames
 * (0x800F3120). Room 2: once room flags 0x6D and 0x6E are set, change
 * the music. Returns 0. */
void n_stage22_RoomFrame(void) {
    FRAME(0x20);
    switch (g_room) {
    case 0:
        if (flag_test(EVENT_FLAGS, 0x95) != 0) break;
        if ((s32)GCALL4(F_LINE_SIDE, 0x800F4174u, 0x800F4184u, (u32)PLAYER_POS[0], (u32)PLAYER_POS[2]) < 0)
            break;
        GCALL2(F_MESSAGE, 0, 3);
        GCALL2(F_FLAG_SET, EVENT_FLAGS, 0x95);
        break;
    case 1:
        if (flag_test(EVENT_FLAGS, 0x2B) != 0) break;
        if (++g_flameTimer >= 0x78) {
            GCALL1(F_FLARE, 0);
            g_flameTimer = 0;
        }
        GCALL1(F_FLARE, 1);
        break;
    case 2:
        if (g_musicSwapped != 0) break;
        if (!flag_test(ROOM_FLAGS, 0x6D) || !flag_test(ROOM_FLAGS, 0x6E)) break;
        GCALL1(F_MUSIC_STOP, 0);
        g_musicTrack = 1;
        GCALL1(F_MUSIC_PLAY, 1);
        g_musicSwapped = 1;
        break;
    }
    V0 = 0;
}

/* 800F34C8: room 1: if this room's layout (ROOM_INFO +0xC, looked up in
 * the list at +8) is one of the special ones (its +6 negative: layout
 * -0x900 or -0xA00 in the high byte), move the first enemies to their
 * places unless room flags 0x6A / 0x6B say they're gone. */
void n_stage22_PlaceEnemies(void) {
    FRAME(0x20);
    u32 e = GV(u32, 0x800AB510);      /* the enemy records */
    s16 id = GV(s16, ROOM_INFO + 0xC);
    if (id == -1) return;
    s16 n = GV(s16, ROOM_INFO + 0xE);
    u32 lay = GV(u32, ROOM_INFO + 8);
    int found = 0;
    for (s32 i = 0; i < n; ) {
        if (GV(s16, lay) == id) { found = 1; break; }
        i++;
        lay += 0x18;
    }
    if (!found) return;
    if (GV(s16, lay + 6) >= 0) return;
    u32 kind = ((u32)(-(u32)GV(u16, lay + 6)) << 16) >> 24;
    u32 places;                       /* two places: x, y, z (s32), yaw */
    if (kind == 9) places = 0x800F41A4u + 0x30;
    else if (kind == 10) places = 0x800F41A4u + 0x60;
    else return;
    if (flag_test(ROOM_FLAGS, 0x6A) == 0) {
        GV(u32, e + 0x198) = GV(u32, places + 0);
        GV(u32, e + 0x19C) = GV(u32, places + 4);
        GV(u32, e + 0x1A0) = GV(u32, places + 8);
        GV(u16, e + 0x1C6) = GV(u16, places + 0xC);
        e += 0x8C4;
    }
    if (flag_test(ROOM_FLAGS, 0x6B) == 0) {
        GV(u32, e + 0x198) = GV(u32, places + 0x10);
        GV(u32, e + 0x19C) = GV(u32, places + 0x14);
        GV(u32, e + 0x1A0) = GV(u32, places + 0x18);
        GV(u16, e + 0x1C6) = GV(u16, places + 0x1C);
    }
}

/* 800F36BC (x, y, z, -, scale): light a flame (stage22_FlameUpdate) at
 * x, y, z; scale (the fifth argument, on the stack) is two halves. */
void n_stage22_SpawnFlame(void) {
    FRAME(0x18);
    s32 x = (s32)A0, z = (s32)A2;
    u32 y = A1;
    u32 arg5 = sp + 0x18 + 0x10;      /* in the caller's frame */
    GS(u32, sp + 0x18) = A0;          /* the arguments' home slots */
    GS(u32, sp + 0x1C) = A1;
    GS(u32, sp + 0x20) = A2;
    GS(u32, sp + 0x24) = A3;
    s16 idx = (s16)GCALL0(F_EFFECT_ALLOC);
    if (idx == -1) return;
    u32 p = EFFECTS + (u32)(s32)idx * EFFECT_SIZE;
    EF_CUR = p;
    EF_FLAGS(p) |= 0x418;
    GCALL2(F_EFFECT_INIT, p, FLAME_TEMPLATE);
    p = EF_CUR;
    EF_FLAGS2(p) |= 0x20;
    EF_X(p) = (u16)(x / 4);
    EF_Y(p) = (u16)y;
    EF_Z(p) = (u16)(z / 16);
    GCALL1(F_EFFECT_SPRITE, FLAME_SPRITE);
    s32 r = sdk_rand() >> 5;
    p = EF_CUR;
    EF_FRAME(p) = (u16)(r % 12);
    EF_SCALE_X(p) = GS(u16, arg5);
    EF_VY(p) = GS(u16, arg5 + 2);
    r = sdk_rand();
    p = EF_CUR;
    EF_UPDATE(p) = 0x800F3838u;           /* stage22_FlameUpdate */
    EF_SWAY(p) = (u16)((r >> 3) % 20 + 0x10);
}

/* 800F3838: a flame's frame: animate (holding a frame now and then),
 * sway, scale; and now and then (less often for tall flames) give off a
 * puff of smoke (stage22_SmokeUpdate) a little above it. */
void n_stage22_FlameUpdate(void) {
    FRAME(0x20);
    u32 p = EF_CUR;
    u16 life = EF_LIFE(p);
    if ((s16)life >= 2) {
        EF_LIFE(p) = 1;
        EF_FRAME(p) -= 1;
        GCALL1(F_EFFECT_ANIM, p);
        EF_LIFE(EF_CUR) = (u16)(life - 1);
    } else {
        GCALL1(F_EFFECT_ANIM, p);
    }
    if ((sdk_rand() >> 5) == 0) EF_LIFE(EF_CUR) += 1;
    EF_FLAGS(EF_CUR) |= 4;

    s32 r = sdk_rand();
    p = EF_CUR;
    u32 sway = (u32)EF_SWAY(p) - 4 + (u32)(r % 8);
    s32 w = (s32)EF_SIZE_W(p) * GV(s16, p + 0x2E);
    s32 h = (s32)EF_SIZE_H(p) * (s16)EF_VY(p);
    EF_SWAY(p) = (u16)sway;
    EF_ANGLE(p) += (u16)sway;
    u32 baseH = EF_SIZE_H(p);
    EF_W(p) = (u16)(w >> 7);
    EF_H(p) = (u16)(h >> 7);
    /* the bottom stays put as it grows */
    EF_Y(p) = (u16)(EF_Y0(p) + baseH * 10 - (u32)EF_H(p) * 10);

    u32 mask = (s16)EF_VY(p) < 0x61 ? 0x1FC : 0x1E0;
    if ((u32)sdk_rand() & mask) return;

    s16 idx = (s16)GCALL0(F_EFFECT_ALLOC);
    if (idx == -1) return;
    u32 s = EFFECTS + (u32)(s32)idx * EFFECT_SIZE;
    u32 flame = EF_CUR;
    EF_CUR = s;
    EF_FLAGS(s) |= 0x41C;
    GCALL2(F_EFFECT_INIT, s, FLAME_TEMPLATE);
    EF_FLAGS2(EF_CUR) |= 0x20;
    r = sdk_rand();
    s = EF_CUR;
    EF_X(s) = (u16)(EF_X(flame) + (u32)((r % 12 - 6) * 10));
    s32 lift = ((s32)(s16)EF_VY(s) * 7) >> 4;
    EF_Y(s) = (u16)(EF_Y(flame) - (u32)(lift * 20));
    r = sdk_rand();
    s = EF_CUR;
    EF_ANGLE(s) = 0;
    EF_Z(s) = (u16)(EF_Z(flame) + (u32)((r % 4 - 2) * 10));
    r = sdk_rand();
    EF_VY(EF_CUR) = (u16)-(r % 11 + 20);
    r = sdk_rand();
    EF_FADE(EF_CUR) = (u16)(r % 13 + 8);
    GCALL1(F_EFFECT_SPRITE, FLAME_SPRITE);
    s = EF_CUR;
    EF_CUR = flame;
    EF_UPDATE(s) = 0x800F3B80u;           /* stage22_SmokeUpdate */
}

/* 800F3B80: a puff of smoke's frame: rise; every fourth frame shrink;
 * gone when it has shrunk away or risen above -4000; drift sideways on a
 * sine. */
void n_stage22_SmokeUpdate(void) {
    FRAME(0x18);
    u32 p = EF_CUR;
    u16 y0 = EF_Y0(p);
    u32 ny0 = (u32)y0 + EF_VY(p);
    u32 ny = ny0 + ((u32)EF_Y(p) - y0);
    EF_Y0(p) = (u16)ny0;
    EF_Y(p) = (u16)ny;
    if ((s16)ny < -0xFA0) { EF_FLAGS(EF_CUR) = 1; return; }
    if ((g_frameTick & 3) == 0) {
        u16 f = (u16)(EF_FADE(p) - 1);
        EF_FADE(p) = f;
        if (f == 0) { EF_FLAGS(EF_CUR) = 1; return; }
    }
    p = EF_CUR;
    EF_W(p) = (u16)(((s32)EF_SIZE_W(p) * EF_FADE(p)) >> 7);
    EF_H(p) = (u16)(((s32)EF_SIZE_H(p) * EF_FADE(p)) >> 7);
    GCALL1(F_EFFECT_ANIM, EF_SIZE_H(p));
    s32 r = sdk_rand();
    p = EF_CUR;
    u32 ang = ((u32)EF_ANGLE(p) + (u32)(r % 7 + 0x40)) & 0xFFF;
    EF_ANGLE(p) = (u16)ang;
    s32 sn = (s32)GCALL1(F_SIN, ang);
    p = EF_CUR;
    s32 dx = (s32)((u32)sn * EF_FADE(p)) >> 14;
    u16 x0 = EF_X0(p), z0 = EF_Z0(p);
    u32 nx0 = (u32)x0 + (u32)dx;
    EF_X0(p) = (u16)nx0;
    EF_X(p) = (u16)(nx0 + ((u32)EF_X(p) - x0));
    u32 nz0 = (u32)z0 + (u32)((s16)dx / 4);
    EF_Z0(p) = (u16)nz0;
    EF_Z(p) = (u16)(nz0 + ((u32)EF_Z(p) - z0));
}

/* 800F3D10 (x, y, z): a glow effect (the stock animation 0x800940C4). */
void n_stage22_SpawnGlow(void) {
    FRAME(0x28);
    s32 x = (s32)A0, z = (s32)A2;
    u32 y = A1;
    s16 idx = (s16)GCALL0(F_EFFECT_ALLOC);
    if (idx == -1) return;
    u32 p = EFFECTS + (u32)(s32)idx * EFFECT_SIZE;
    EF_CUR = p;
    EF_FLAGS(p) |= 0x40C;
    GCALL2(F_EFFECT_INIT, p, GLOW_TEMPLATE);
    p = EF_CUR;
    EF_X(p) = (u16)(x / 4);
    EF_Y(p) = (u16)y;
    EF_Z(p) = (u16)(z / 16);
    EF_FADE(p) = 0x80;
    GCALL1(F_EFFECT_SPRITE, GLOW_SPRITE);
    EF_UPDATE(EF_CUR) = F_EFFECT_ANIM;
}
