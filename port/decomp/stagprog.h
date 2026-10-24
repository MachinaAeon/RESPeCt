/* What the stage programs (STAGPROG overlays, loaded in the stage slot at
 * 0x800F2C50; stage s is overlay 15 + s) share: the game's flag banks and
 * the TEST-module functions their room hooks call, and the few routines
 * the overlays each carry a copy of (compiled into each overlay, same
 * instructions). Used by stage17.c, stage18.c, stage26.c, stage31.c and
 * stage32.c (stage22.c and stage23.c keep their own definitions). */
#ifndef STAGPROG_H
#define STAGPROG_H

#include "game_vars.h"

/* ---- flags (bit banks; F_FLAG_* take the bank and a bit number) ---- */
#define EVENT_FLAGS      0x80062FC0u
#define FLAGS_2FA0       0x80062FA0u           /* the bank before EVENT_FLAGS */
#define ROOM_FLAGS       0x80062F60u
#define F_FLAG_TEST      0x8008F64Cu
#define F_FLAG_SET       0x8008F5F8u
#define F_FLAG_CLEAR     0x8008F620u

/* ---- stage state ---- */
#define g_room           GV(s32, 0x800B3F08)
#define g_roomIndex      GV(s32, 0x800B3E20)
#define ROOM_INFO        0x800B3D28u           /* room table, entry, enemy layout */
#define g_musicTrack     GV(s16, 0x80062E78)
#define g_eventBusy      GV(s32, 0x800BBB58)   /* an event (script) is running */
#define g_eventMode      GV(s32, 0x800BB19C)
#define g_weaponFiring   GV(s32, 0x800C3A20)   /* -1 while weapons 7/8 fire */
#define g_targetList     GV(u32, 0x800B60D4)   /* 0x14-byte records; +0x10 depth */
#define g_playerObj      GV(u32, 0x800AB52C)   /* the player's model record */
#define LIGHTS           0x800B60C8u           /* bytes at +0 and +8: room light levels */

/* ---- TEST functions ---- */
#define F_LINE_SIDE      0x8007C04Cu   /* (a, b, x, z): which side of line a-b */
#define F_MESSAGE        0x8008D390u   /* (a, b): room message / event */
#define F_EVENT_SCRIPT   0x8008D4BCu   /* (slot, script): start a script; 0 if none free */
#define F_MUSIC_PLAY     0x8001209Cu   /* (track) */
#define F_MUSIC_STOP     0x8001215Cu   /* (track) */
#define F_CLUT_ANIM      0x80092D44u   /* (x, y, n): copy VRAM row y+n (16 px) to row y */
#define F_SOUND_AT       0x800805F4u   /* (sound, bank, left, right) */
#define F_SCREEN_FLASH   0x800731BCu   /* (r, g, b, ?) */
#define F_EN_DRAW        0x80099068u   /* (obj, a, b, c, mode on the stack) */
#define F_LOAD_SOUNDS    0x8009806Cu   /* (kind, buffer) */
#define F_MOVE_IMAGE     0x800356E0u   /* (rect, x, y) */
#define F_DRAW_SYNC      0x80035374u
#define SOUND_BUFFER     0x800C3E50u

static inline u32 flag_test(u32 bank, u32 bit) { return GCALL2(F_FLAG_TEST, bank, bit); }
static inline void flag_set(u32 bank, u32 bit) { GCALL2(F_FLAG_SET, bank, bit); }
static inline void clut_anim(u32 x, u32 y, s32 n) { GCALL3(F_CLUT_ANIM, x, y, (u32)n); }
static inline void sound_at(u32 sound, u32 bank) { GCALL4(F_SOUND_AT, sound, bank, 0x7F, 0x7F); }
static inline void screen_flash(void) { GCALL4(F_SCREEN_FLASH, 0xFF, 0xFF, 0x16, 9); }
/* Has the player crossed the line (a, a + 0x10)? The line test's result. */
static inline s32 player_line_side(u32 a, u32 b) {
    return (s32)GCALL4(F_LINE_SIDE, a, b, (u32)PLAYER_POS[0], (u32)PLAYER_POS[2]);
}
/* A 16-bit value as the original passes it after sll/sra 16. */
static inline s32 s16v(s32 x) { return (s16)x; }

/* Step a palette animation from a table of (frames, row) byte pairs,
 * ending with -1: the index (s8 at idx) moves on, wrapping at the end; the
 * timer (byte) gets the entry's frame count; row y+entry's row is copied
 * to row y. */
static inline void stage_ClutTableStep(u32 idx, u32 timer, u32 table, u32 x, u32 y) {
    u8 i = (u8)(GV(u8, idx) + 1);
    u32 e = table + (u32)((s32)(s8)i * 2);
    GV(u8, idx) = i;
    if (GV(s8, e) == -1) {
        GV(u8, idx) = 0;
        e = table;
    }
    GV(u8, timer) = GV(u8, e);
    clut_anim(x, y, GV(s8, e + 1));
}

/* Is the player not shooting this frame (1), by the pad type's rule: the
 * light gun (0x200000) shoots with the sight on screen, DualShock and
 * digital pads by the fire button; any other pad counts as shooting (0).
 * Weapons 7 and 8 go by their own firing state instead; for the others a
 * press of button 4 held from last frame (kept at lastButtons) also counts
 * as not shooting. */
static inline u32 stage_NotFiring(u32 lastButtons) {
    u32 t = g_padTypeBit, idle;
    if (t == 0x200000) {
        if (g_fireRequest == 0 || g_reticleX < 0) idle = 1;
        else idle = (u32)g_reticleY >> 31;
    } else if (t == 0x400000 || t == 0x80000) {
        idle = g_fireRequest == 0;
    } else {
        idle = 0;
    }
    s16 weapon = GV(s16, g_currentWeaponAddr);
    if (weapon < 9 && weapon >= 7) return g_weaponFiring == -1;
    if (GV(u16, lastButtons) & 4) idle = 1;
    GV(u16, lastButtons) = D_800AFCB0;
    return idle;
}

/* The breakable things' VRAM patch: a RECT on the game's stack. */
static inline void stage_Rect(u32 r, u16 x, u16 y, u16 w, u16 h) {
    GS(u16, r + 0) = x;
    GS(u16, r + 2) = y;
    GS(u16, r + 4) = w;
    GS(u16, r + 6) = h;
}

/* ---- the breakable things' "did the shot reach it" test (stage27_
 * TargetHit, stage23_TargetHit: the same code after each one's own reach
 * checks) ---- */
#define F_HITBOX_PROJECT 0x80070930u
#define F_HITBOX_RETICLE 0x80070CF4u
#define HIT_MARKS        0x800C3A20u           /* {panel (-1 ends), x, y, z, line}, 0x14 bytes */

/* Weapons 7 and 8: is there a hit mark on line `id` within the target's
 * height (s16 y0 +2, y1 +0xA)? Polls at the loop's branch (poll_pc). */
static inline u32 stage_HitMarkOn(u32 t, s32 id, u32 poll_pc) {
    u32 mk = HIT_MARKS;
    if (GV(s32, mk) == -1) return 0;
    for (;;) {
        if (GV(s32, mk + 0x10) == id) {
            s32 y = GV(s32, mk + 8);
            if (!(y < GV(s16, t + 2)) && !(GV(s16, t + 0xA) < y)) return 1;
        }
        mk += 0x14;
        if (GV(s32, mk) == -1) break;
        RT_POLL_NOSAVE(poll_pc);
    }
    return 0;
}

/* Is the reticle on one of the 27 boxes the target's box (s16 x0, y0, z0,
 * -, x1, y1, z1 from +0 of a copy of the 0x5C-byte record at sp + 0x10)
 * splits into, 3 by 3 by 3? The copy loop polls at poll_pc, the three
 * box loops at poll_pc + 0x114, + 0x13C and + 0x168. */
static inline u32 stage_ReticleOnBox(u32 sp, u32 t, u32 poll_pc) {
    const u32 box = sp + 0x10;
    for (u32 k = 0;; k += 0x10) {
        for (u32 j = 0; j < 0x10; j += 4) GS(u32, box + k + j) = GV(u32, t + k + j);
        if (k + 0x10 == 0x50) break;
        RT_POLL_NOSAVE(poll_pc);
    }
    for (u32 j = 0; j < 0xC; j += 4) GS(u32, box + 0x50 + j) = GV(u32, t + 0x50 + j);
    s32 dy = ((s16)GS(u16, box + 0xA) - (s16)GS(u16, box + 2) + 2) / 3;
    s32 dz = ((s16)GS(u16, box + 0xC) - (s16)GS(u16, box + 4) + 2) / 3;
    s32 dx = ((s16)GS(u16, box + 8) - (s16)GS(u16, box) + 2) / 3;
    GS(u16, box + 0xA) = (u16)(GS(u16, box + 0xA) - dy * 2);
    s16 y1 = (s16)GS(u16, box + 0xA);
    GS(u16, box + 0xC) = (u16)(GS(u16, box + 0xC) - dz * 2);
    s16 z1 = (s16)GS(u16, box + 0xC);
    GS(u16, box + 8) = (u16)(GS(u16, box + 8) - dx * 2);
    for (s32 ix = 0;;) {
        for (s32 iy = 0;;) {
            for (s32 iz = 0;;) {
                GCALL3(F_HITBOX_PROJECT, box, 0, 0);
                if (GCALL1(F_HITBOX_RETICLE, box)) return 1;
                if (dz == 0) break;
                iz++;
                u16 zz = GS(u16, box + 0xC);
                GS(u16, box + 4) = zz;
                GS(u16, box + 0xC) = (u16)(zz + dz);
                if (!(iz < 3)) break;
                RT_POLL_NOSAVE(poll_pc + 0x114);
            }
            iy++;
            u16 yy = GS(u16, box + 0xA);
            GS(u16, box + 0xC) = (u16)z1;
            GS(u16, box + 2) = yy;
            GS(u16, box + 0xA) = (u16)(yy + dy);
            GS(u16, box + 4) = GV(u16, t + 4);
            if (!(iy < 3)) break;
            RT_POLL_NOSAVE(poll_pc + 0x13C);
        }
        if (dx == 0) break;
        ix++;
        u16 xx = GS(u16, box + 8);
        GS(u16, box + 0xA) = (u16)y1;
        GS(u16, box) = xx;
        GS(u16, box + 8) = (u16)(xx + dx);
        GS(u16, box + 2) = GV(u16, t + 2);
        if (!(ix < 3)) break;
        RT_POLL_NOSAVE(poll_pc + 0x168);
    }
    return 0;
}

/* *result = is the player not shooting (stage_NotFiring with the stage's
 * last buttons); returns what the copies leave in $v0. */
static inline u32 stage_NotFiringResult(u32 res, u32 lastButtons) {
    GS(u32, res) = stage_NotFiring(lastButtons);
    s16 weapon = GV(s16, g_currentWeaponAddr);
    return (weapon < 9 && weapon >= 7) ? 1 : (u32)GV(u16, lastButtons);
}

#endif
