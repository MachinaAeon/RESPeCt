/* Grenades, rockets and their explosions (TEST overlay), and the dripping
 * water effect.
 *
 * Everything here is a HUD panel flagged 0x400: a world sprite (drawn by
 * hud_draw.c) whose update callback (+0x3C) moves it every frame. A world
 * sprite's place is +10 x/4, +12 y, +18 z/16.
 *
 * A grenade or rocket in flight keeps its velocity at +2E/+30/+32 (x, y,
 * z), its kind at +24 (0 normal, 1 acid, 2 flame grenade; 3 a rocket
 * that hit something) and a shot number (+26, the low two bits of the
 * shot counter 0x800C3A0C; 0x800C3A08 has a byte per shot number so that
 * the hit statistic 0x8006344C counts each shot once). Each frame it
 * tests its path against the room's collision lines (0x800822C4) and the
 * boxes of the room's objects (0x8009C97C), and falls (grenades) until it
 * reaches the player's height (0x800A85DC). On impact it backs off along
 * its path (0x8009C7C8) and explodes (0x8009C75C): the panel turns into
 * the blast (0x8009E1FC) and an explosion is spawned (0x8009DE6C,
 * 0x8009DF94) that hurts the room objects it touches and leaves smoke
 * (0x8009D71C) or, for acid, a second cloud.
 *
 * Room objects: 0x800AB510 points at their records (0x8C4 bytes each,
 * 0x800AF530 of them): +198/+1A0 x/z, +315 type (< 0x32 an enemy), +29 /
 * +2A / +2B state bytes, +320 flags, +868 draw order, and from +38C
 * their parts (0x5C bytes each, ended by a negative s16 at +6 in the
 * next): a box (+0/+2/+4 and +8/+A/+C corner offsets from +24/+28/+2C)
 * in the part's coordinate system (+30; its parent at +30 of the part
 * before, +3BC of the record).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "hud.h"
#include "game_stack.h"
#include "poll.h"

void n_gl_Fire(void);
void n_proj_RecordHit(void);
void n_gl_GrenadeUpdate(void);
void n_proj_BounceSound(void);
void n_proj_Explode(void);
void n_proj_Impact(void);
void n_proj_HitRoomObject(void);
void n_fx_SmokeSpawn(void);
void n_fx_FlameBurst(void);
void n_fx_FlameUpdate(void);
void n_fx_SmokeUpdate(void);
void n_fx_ExplosionSpawn(void);
void n_fx_ExplosionUpdate(void);
void n_fx_BlastUpdate(void);
void n_rl_Fire(void);
void n_rl_RocketUpdate(void);
void n_rl_TrailUpdate(void);
void n_fx_DripEmitter(void);
void n_fx_DropUpdate(void);
void n_hud_StartAnimA(void);

#define CAM_X           GV(s32, CAMERA_OBJ + 0x38)   /* the camera's world position */
#define CAM_Y           GV(s32, CAMERA_OBJ + 0x3C)
#define CAM_Z           GV(s32, CAMERA_OBJ + 0x40)
#define g_floorY        GV(s32, 0x800A85DC)          /* the player's height */
#define g_roomObjs      GV(u32, 0x800AB510)
#define g_roomObjCount  GV(s32, 0x800AF530)
#define ROOM_OBJ_SIZE   0x8C4
#define g_shotCount     GV(u32, 0x800C3A0C)
#define SHOT_COUNTED    0x800C3A08u                  /* [4] bytes, by shot number */
#define g_statHits      GV(u32, 0x8006344C)
#define HIT_MARKS       0x800C3A20u                  /* [5] {panel, x, y, z, line}, 0x14 bytes */
#define FLAME_PELLETS   0x800A8338u                  /* [5] {s16 x, s16 y, s16 speed, s16 angle} */
#define TRAIL_LOOKS     0x800A8360u                  /* rocket trail: {u, v, w, h} per age */
#define IDENTITY        0x80065B90u

#define F_UNPROJECT     0x8009B71Cu   /* (sx, sy, depth, out, flag): screen -> world */
#define F_LINE_HIT      0x800822C4u   /* (from, to): the room collision line crossed, -1 none */
#define F_ROT_MATRIX_Y  0x8002ACF8u
#define F_APPLY_MATRIX  0x8002A5B8u
#define F_RATAN2        0x8002BB48u
#define F_RCOS          0x8002A0C8u
#define F_RSIN          0x80029FF8u
#define F_SQRT          0x8002A3D8u
#define F_COORD_WORLD   0x800339E8u   /* (coordinate, matrix out): its world matrix */
#define F_SOUND_AT      0x800805F4u   /* (sound, bank, left, right) */
#define F_ENEMY_HIT     0x800710FCu   /* (record, kind, part, 0) */
#define F_ACID_SPLASH   0x8009E704u
#define F_GRENADE_BURST 0x8009E2D0u
#define F_SPARK         0x80098608u
#define F_ANIM_ONCE_A   0x8009414Cu
#define F_START_ANIM_C  0x800948BCu
#define F_SHELL_DROP    0x8009D638u
#define F_SHELL_SMOKE   0x8009D800u
#define F_SHELL_LAND    0x8009D3F4u
#define CB_GRENADE      0x8009C230u
#define CB_ROCKET       0x8009F330u
#define CB_TRAIL        0x8009F9B4u
#define CB_BLAST        0x8009E1FCu
#define CB_EXPLOSION    0x8009DF94u
#define CB_SMOKE        0x8009D71Cu
#define CB_DROP         0x8009FBF8u
#define CB_BURNT_OUT    0x800943D0u
#define CB_FLAME        0x8009DAA4u
#define F_RAND_         0x800398A8u   /* BIOS rand() */
#define F_ANIM_LOOP_A   0x800940C4u

/* Allocate a panel: its index (-1 when the pool is full). */
static s16 alloc_panel(void) {
    n_hud_AllocPanel();
    return (s16)V0;
}

static void set_sprite(u32 p, u32 sprite) {
    A0 = p; A1 = sprite;
    n_hud_SetSprite();
}

static void start_anim(u32 anim) {
    A0 = anim;
    n_hud_StartAnimA();
}

/* Copy the 4x4 identity MATRIX (8 words) to the stack. */
static void identity_to(u32 m) {
    for (u32 i = 0; i < 0x20; i += 4) GS(u32, m + i) = GV(u32, IDENTITY + i);
}

/* The record of room object (index / 16) for a 0x8009C97C result - 1. */
static u32 hit_record_offset(s32 hit) {
    return (u32)div_pow2(hit, 4) * ROOM_OBJ_SIZE;
}

/* The shot is done with: its number's hit flag cleared, the counter on. */
static void next_shot(void) {
    GV(u8, SHOT_COUNTED + (g_shotCount & 3)) = 0;
    g_shotCount = g_shotCount + 1;
    V0 = g_shotCount;
}

/* 8009BBF0 (kind): fire the grenade launcher at the reticle: a grenade
 * from the camera towards the point 500 deep under it (flame grenades: a
 * spread of five pellets, 0x800A8338). If the path is blocked at once it
 * explodes there. */
void n_gl_Fire(void) {
    FRAME(0xA0);
    u32 kind = A0;
    GS(u32, sp + 0xA0) = kind;                       /* kept in its argument slot */
    s16 i = alloc_panel();
    if (i == -1) { V0 = (u32)i << 16; return; }
    u32 at = sp + 0x18, from = sp + 0x30, to = sp + 0x40;
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_UNPROJECT, (u32)(g_reticleX - 0xA0), (u32)(g_reticleY - 0x3C), 0x1F4, at);
    u32 p = panel_addr(i);
    P16(p, PANEL_FLAGS) |= 0x41C;
    g_curPanel = p;
    set_sprite(p, 0x800A704Cu);
    p = g_curPanel;
    P16(p, 0x14) = 8;
    P16(p, 0x16) = 8;
    P16(p, 0x08) = 0x10;
    P16(p, 0x0A) = 0x10;
    P16(p, 0x04) = (u16)(P16(p, 0x04) + 0x40);
    P16(p, 0x06) = (u16)(P16(p, 0x06) + 0x18);
    GS(s32, from) = CAM_X;
    GS(s32, from + 4) = CAM_Y;
    GS(s32, from + 8) = CAM_Z;
    GS(u32, to) = GS(u32, at);
    GS(u32, to + 4) = GS(u32, at + 4);
    GS(u32, to + 8) = GS(u32, at + 8);
    s32 line = (s32)GCALL2(F_LINE_HIT, from, to);
    if (line != -1) {                                /* blocked: explode here */
        GS(u32, to + 8) = 0;
        GS(u32, to + 4) = 0;
        GS(u32, to) = 0;
        GS(u32, sp + 0x10) = to;
        GCALL4(0x8009C7C8u, 0, 0, 0, at);
        GCALL2(0x8009C17Cu, g_curPanel, (u32)line);
        return;
    }
    if (kind != 2) {
        p = g_curPanel;
        P16(p, 0x10) = (u16)div_pow2(GS(s32, at), 2);
        P16(p, 0x12) = (u16)(GS(u16, at + 4) - 0x1C);
        P16(p, 0x18) = (u16)div_pow2(GS(s32, at + 8), 4);
        P32(p, PANEL_UPDATE) = CB_GRENADE;
        P16(p, 0x34) = (u16)(P16(p, 0x34) + 0x66);
        P16(p, 0x2E) = (u16)(((u32)GS(s32, at) - (u32)CAM_X) << 1);
        P16(p, 0x30) = (u16)(((u32)GS(s32, at + 4) - ((u32)CAM_Y + 0x1C)) << 1);
        P16(p, 0x22) = 2;
        P16(p, 0x24) = GS(u16, sp + 0xA0);
        P16(p, 0x26) = (u16)(GV(u16, 0x800C3A0C) & 3);
        P16(p, 0x32) = (u16)(((u32)GS(s32, at + 8) - (u32)CAM_Z) << 1);
        next_shot();
        return;
    }
    /* flame: five pellets, each turned by the camera's yaw and spread
     * around the direction of the shot */
    s32 ang = (s32)GCALL2(F_RATAN2, (u32)GS(s32, at + 8) - (u32)CAM_Z, (u32)GS(s32, at) - (u32)CAM_X);
    if (ang < 0) ang += 0x1000;
    s32 dir = (s16)ang;
    u32 m = sp + 0x50, sv = sp + 0x28, off = sp + 0x30;
    u32 first = g_curPanel;
    GS(u32, sp + 0x70) = first;
    for (s32 n = 0; n < 5; n++) {
        u32 t = FLAME_PELLETS + (u32)n * 8;
        identity_to(m);
        GCALL2(F_ROT_MATRIX_Y, (u32)(s32)GV(s16, 0x800A8606), m);
        GS(u16, sv) = GV(u16, t);
        GS(u16, sv + 2) = GV(u16, t + 2);
        GS(u16, sv + 4) = 0;
        GCALL3(F_APPLY_MATRIX, m, sv, off);
        p = g_curPanel;
        for (u32 k = 0; k < PANEL_SIZE; k += 4) GV(u32, p + k) = GV(u32, first + k);
        P16(p, 0x10) = (u16)div_pow2((s32)((u32)GS(s32, at) + (u32)GS(s32, off)), 2);
        P16(p, 0x12) = (u16)(GS(u16, at + 4) + (u16)(GS(u16, off + 4) - 0x1C));
        P16(p, 0x18) = (u16)div_pow2((s32)((u32)GS(s32, at + 8) + (u32)GS(s32, off + 8)), 4);
        P32(p, PANEL_UPDATE) = CB_GRENADE;
        identity_to(m);
        s32 c = (s32)GCALL1(F_RCOS, (u32)(dir + GV(s16, t + 6)) & 0xFFF);
        s32 speed = GV(s16, t + 4);
        P16(g_curPanel, 0x2E) = (u16)((s32)((u32)speed * (u32)c) >> 12);
        s32 dy = (s32)((u32)GS(s32, at + 4) - (GV(u32, 0x800A8654) + 0x1C));
        P16(g_curPanel, 0x30) = (u16)((s32)((u32)dy * (u32)(s32)GV(s16, t + 4)) / 500);
        s32 s = (s32)GCALL1(F_RSIN, (u32)(dir + GV(s16, t + 6)) & 0xFFF);
        p = g_curPanel;
        P16(p, 0x22) = 2;
        P16(p, 0x24) = GS(u16, sp + 0xA0);
        P16(p, 0x26) = (u16)(GV(u16, 0x800C3A0C) & 3);
        P16(p, 0x32) = (u16)((s32)((u32)(s32)GV(s16, t + 4) * (u32)s) >> 12);
        if (n < 4) {
            s16 j = alloc_panel();
            if (j == -1) break;
            g_curPanel = panel_addr(j);
        }
    }
    next_shot();
}

/* 8009C17C (panel, line): remember where a projectile hit a collision
 * line, in the first free slot of the table at 0x800C3A20 (5 slots). */
void n_proj_RecordHit(void) {
    u32 p = A0, line = A1;
    u32 e = HIT_MARKS;
    s32 n;
    for (n = 0; n < 5; n++, e += 0x14)
        if (GV(s32, e) == -1) break;
    if (n == 5) { V0 = 5; return; }
    /* the panel's index, (p - PANELS) / 0x44 by multiplying by the inverse */
    u32 d = p - PANELS, t = d * 15;
    t += t << 8;
    t += t << 16;
    GV(s32, e) = (s32)(0u - t) >> 2;
    GV(s32, e + 4) = (s32)(s16)P16(p, 0x10) << 2;
    GV(s32, e + 8) = (s16)P16(p, 0x12);
    GV(u32, e + 0x10) = line;
    GV(s32, e + 0xC) = (s32)(s16)P16(p, 0x18) << 4;
    V0 = (u32)((s32)(s16)P16(p, 0x18) << 4);
}

/* 8009C230: a grenade in flight. It hits a room object (an enemy: hurt
 * it; acid / normal grenades splash or burst on it, flame ones spark),
 * a collision line (explode there), or the floor (normal grenades lie
 * there burning; the others explode); else it moves, falls, and leaves a
 * smoke puff every other frame. */
void n_gl_GrenadeUpdate(void) {
    FRAME(0x88);
    u32 p = g_curPanel;
    u32 pos = sp + 0x18, to = sp + 0x28, step = sp + 0x38, box = sp + 0x48, coord = sp + 0x68;
    s16 vx = (s16)P16(p, 0x2E);                      /* the box swept this frame */
    if (vx < 0) { GS(s32, box) = vx; GS(s32, box + 0x10) = 0; }
    else { GS(s32, box) = 0; GS(s32, box + 0x10) = (s16)P16(p, 0x2E); }
    GS(s32, box + 4) = -100;
    p = g_curPanel;
    GS(s32, box + 0x14) = 100;
    s16 vz = (s16)P16(p, 0x32);
    if (vz < 0) { GS(s32, box + 8) = vz; GS(s32, box + 0x18) = 0; }
    else { GS(s32, box + 8) = 0; GS(s32, box + 0x18) = (s16)P16(p, 0x32); }
    GS(u32, sp + 0x10) = 0;
    s32 hit = (s32)GCALL4(0x8009C97Cu, coord, pos, box, 0x578);
    if (hit != 0) {                                  /* a room object */
        GCALL1(0x8009C6F4u, pos);
        s32 h = hit - 1;
        P16(g_curPanel, PANEL_FLAGS) = 1;
        u32 rec = hit_record_offset(h);
        GCALL4(F_ENEMY_HIT, g_roomObjs + rec, 5, (u32)h & 0xF, 0);
        u32 g = g_curPanel;
        s16 kind = (s16)P16(g, 0x24);
        if (kind == 0) {
            GCALL3(F_GRENADE_BURST, GS(u32, coord), pos, (u32)h);
        } else if (kind == 1) {
            GCALL3(F_ACID_SPLASH, GS(u32, coord), pos, g_roomObjs + rec);
        } else {
            GS(u32, sp + 0x10) = g_roomObjs + rec;
            GCALL4(F_SPARK, (u32)((s32)(s16)P16(g, 0x10) << 2), (u32)(s32)(s16)P16(g, 0x12),
                   (u32)((s32)(s16)P16(g, 0x18) << 4), 0);
            g_curPanel = g;
            n_proj_Explode();
        }
        return;
    }
    p = g_curPanel;
    s32 x = (s32)(s16)P16(p, 0x10) << 2, y = (s16)P16(p, 0x12), z = (s32)(s16)P16(p, 0x18) << 4;
    GS(s32, pos) = x;
    GS(s32, pos + 4) = y;
    GS(s32, pos + 8) = z;
    GS(s32, to) = (s32)((u32)x + (u32)(s32)(s16)P16(p, 0x2E));
    GS(s32, to + 4) = (s32)((u32)y + (u32)(s32)(s16)P16(p, 0x30));
    GS(s32, to + 8) = (s32)((u32)z + (u32)(s32)(s16)P16(p, 0x32));
    for (u32 k = 0; k < 0x10; k += 4) GS(u32, step + k) = GS(u32, to + k);
    s32 line = (s32)GCALL2(F_LINE_HIT, pos, to);
    if (line != -1) {                                /* a wall */
        GCALL0(0x8009C6F4u);
        u32 g = g_curPanel;
        s32 dx = div_pow2((s16)P16(g, 0x2E), 4);
        s32 dy = div_pow2((s16)P16(g, 0x30), 4);
        s32 dz = div_pow2((s16)P16(g, 0x32), 4);
        GS(u32, sp + 0x10) = step;
        GCALL4(0x8009C7C8u, (u32)dx, (u32)dy, (u32)dz, pos);
        GCALL2(0x8009C17Cu, g_curPanel, (u32)line);
        return;
    }
    p = g_curPanel;                                  /* move, and fall */
    P16(p, 0x10) = (u16)(P16(p, 0x10) + div_pow2((s16)P16(p, 0x2E), 2));
    u16 vy = (u16)(P16(p, 0x30) + 0xE);
    P16(p, 0x30) = vy;
    P16(p, 0x12) = (u16)(P16(p, 0x12) + vy);
    P16(p, 0x18) = (u16)(P16(p, 0x18) + div_pow2((s16)P16(p, 0x32), 4));
    if (!((s16)P16(p, 0x12) < g_floorY)) {           /* on the floor */
        GCALL0(0x8009C6F4u);
        if ((s16)P16(g_curPanel, 0x24) != 0) { n_proj_Explode(); return; }
        GCALL0(F_SHELL_DROP);
        GCALL0(F_SHELL_SMOKE);
        p = g_curPanel;
        P16(p, PANEL_FLAGS) |= 0x41C;
        set_sprite(p, 0x800A6FACu);
        p = g_curPanel;
        P16(p, 0x10) = (u16)(P16(p, 0x10) - (s16)P16(p, 0x2E) / 200);
        P16(p, 0x18) = (u16)(P16(p, 0x18) - (s16)P16(p, 0x32) / 800);
        GCALL1(F_START_ANIM_C, 0x800A7C48u);
        P32(g_curPanel, PANEL_UPDATE) = CB_BURNT_OUT;
        return;
    }
    u16 t = (u16)(P16(p, 0x22) + 1);                 /* a smoke puff every other frame */
    P16(p, 0x22) = t;
    if (t & 1) return;
    s16 i = alloc_panel();
    if (i == (s16)line) return;                      /* -1: the pool is full */
    u32 q = panel_addr(i);
    u32 g = g_curPanel;
    P16(q, PANEL_FLAGS) |= 0x41C;
    g_curPanel = q;
    set_sprite(q, 0x800A6FACu);
    q = g_curPanel;
    P16(q, 0x10) = (u16)(P16(g, 0x10) + 0x28);
    P16(q, 0x12) = (u16)(P16(g, 0x12) + 0x78);
    P16(q, 0x18) = P16(g, 0x18);
    start_anim(0x800A7B60u);
    q = g_curPanel;
    g_curPanel = g;
    P32(q, PANEL_UPDATE) = F_ANIM_ONCE_A;
}

/* 8009C6F4: the bounce / impact sound of a grenade (kinds 0-2). */
void n_proj_BounceSound(void) {
    FRAME(0x18);
    s16 kind = (s16)P16(g_curPanel, 0x24);
    if (kind == 0 || kind == 1 || kind == 2) GCALL4(F_SOUND_AT, 1, 4, 0x7F, 0x7F);
}

/* 8009C75C: the current projectile explodes: spawn the explosion and turn
 * the panel itself into the blast flash. */
void n_proj_Explode(void) {
    FRAME(0x18);
    n_fx_ExplosionSpawn();
    u32 p = g_curPanel;
    P16(p, PANEL_FLAGS) |= 0x40C;
    set_sprite(p, 0x800A6FACu);
    p = g_curPanel;
    P16(p, 0x14) = 0x30;
    P16(p, 0x08) = 0x30;
    P16(p, 0x16) = 0x20;
    P16(p, 0x0A) = 0x20;
    P32(p, PANEL_UPDATE) = CB_BLAST;
    P16(p, 0x2C) = 0;
}

/* 8009C7C8 (dx, dy, dz, pos, end): the current projectile hit something
 * between pos and end: step end back by (dx, dy, dz) until the way from
 * pos is clear (at most 16 times) and put it two steps further back (at
 * pos when there's no step); then explode (kinds 1-3) or, a normal
 * grenade, drop it there. */
void n_proj_Impact(void) {
    FRAME(0x40);
    s32 dx = (s32)A0, dy = (s32)A1, dz = (s32)A2;
    u32 pos = A3;
    u32 end = GS(u32, sp + 0x40 + 0x10);             /* argument 5 */
    u32 p;
    s32 z;
    if (dx == 0 && dy == 0 && dz == 0) {
        p = g_curPanel;
        P16(p, 0x10) = (u16)div_pow2(GS(s32, pos), 2);
        P16(p, 0x12) = GS(u16, pos + 4);
        z = GS(s32, pos + 8);
    } else {
        for (s32 n = 0;;) {
            GS(u32, end) -= (u32)dx;
            GS(u32, end + 8) -= (u32)dz;
            GS(u32, end + 4) -= (u32)dy;
            for (u32 k = 0; k < 0x10; k += 4) GS(u32, sp + 0x10 + k) = GS(u32, end + k);
            if ((s32)GCALL2(F_LINE_HIT, pos, sp + 0x10) == -1) break;
            if (++n >= 0x10) break;
        }
        p = g_curPanel;
        P16(p, 0x10) = (u16)div_pow2((s32)(GS(u32, end) - ((u32)dx << 1)), 2);
        P16(p, 0x12) = (u16)(GS(u16, end + 4) - (u16)((u32)dy << 1));
        z = (s32)(GS(u32, end + 8) - ((u32)dz << 1));
    }
    P16(p, 0x18) = (u16)div_pow2(z, 4);
    if ((u32)P16(g_curPanel, 0x24) - 1 < 3) {
        n_proj_Explode();
    } else {
        GCALL0(F_SHELL_DROP);
        GCALL0(F_SHELL_LAND);
    }
}

/* Clamp to [-80, 80]. */
static s32 clamp80(s32 v) {
    if (v < -0x50) v = -0x50;
    if (!(v < 0x51)) v = 0x50;
    return v;
}

/* 8009C97C (coord out, push out, box, range; no-stat): does the current
 * panel touch a room object? The live objects within range in x and z
 * are tried in draw order (+868); for each of their parts the 8 corners
 * of its box go to world space, and their bounding box widened by box
 * ({min x, y, z, _, max x, y, z}) is tested against the panel's place.
 * On a hit: the part's coordinate system goes to *coord, a push away
 * from its centre (x, z clamped to 80) to push (4 words), the shot is
 * counted as a hit (once per shot) unless no-stat, and the result is
 * object * 16 + part + 1. 0: no hit. */
void n_proj_HitRoomObject(void) {
    FRAME(0x258);
    u32 out_coord = A0, out_push = A1, box = A2;
    s32 range = (s32)A3;
    GS(u32, sp + 0x258) = A0;                        /* the argument home slots */
    GS(u32, sp + 0x25C) = A1;
    GS(u32, sp + 0x260) = A2;
    GS(u32, sp + 0x264) = A3;
    u32 no_stat = GS(u32, sp + 0x268);               /* argument 5 */
    u32 base = g_roomObjs;
    u32 p = g_curPanel;
    s32 px = (s32)(s16)P16(p, 0x10) << 2;
    s32 pz = (s32)(s16)P16(p, 0x18) << 4;
    u32 list = sp + 0x170;                           /* candidates, -1 ends */
    GS(s32, list) = -1;
    s32 n = 0;
    s32 count = g_roomObjCount;
    for (s32 i = 0; i < count; i++) {
        u32 r = base + (u32)i * ROOM_OBJ_SIZE;
        if (GV(u8, r + 0x315) >= 0x32) continue;
        if (GV(u8, r + 0x29) == 0) continue;
        u32 f = GV(u32, r + 0x320);
        if (f & 2) continue;
        if (!(f & 0x40)) continue;
        if (GV(u8, r + 0x2B) & 1) continue;
        if (GV(u8, r + 0x2A) != 1) continue;
        GS(s32, list + (u32)n * 4) = i;
        n++;
        GS(s32, list + (u32)n * 4) = -1;
    }
    for (s32 i = 1; i < n; i++) {                    /* insertion sort by +868 */
        u32 objs = g_roomObjs;
        for (s32 j = i; j > 0; j--) {
            s32 a = GS(s32, list + (u32)j * 4), b = GS(s32, list + (u32)(j - 1) * 4);
            if (!(GV(u32, objs + (u32)a * ROOM_OBJ_SIZE + 0x868) < GV(u32, objs + (u32)b * ROOM_OBJ_SIZE + 0x868)))
                break;
            GS(s32, list + (u32)j * 4) = b;
            GS(s32, list + (u32)(j - 1) * 4) = a;
        }
    }
    u32 cs = sp + 0x10;                              /* a coordinate system: +18 t, +38 world t, +4C parent */
    u32 corners = sp + 0x70, world = sp + 0xB0, mn = sp + 0x130, mx = sp + 0x140, push = sp + 0x60;
    for (s32 k = 0; k < n; k++) {
        s32 obj = GS(s32, list + (u32)k * 4);
        u32 r = g_roomObjs + (u32)obj * ROOM_OBJ_SIZE;
        s32 dx = (s32)(GV(u32, r + 0x198) - (u32)px);
        if (dx < 0) dx = (s32)(0u - (u32)dx);
        s32 dz = (s32)(GV(u32, r + 0x1A0) - (u32)pz);
        if (dz < 0) dz = (s32)(0u - (u32)dz);
        if (!(dx < range) || !(dz < range)) continue;
        if (GV(s16, r + 0x392) < 0) continue;
        for (s32 part = 0;; part++) {
            u32 pr = r + (u32)part * 0x5C;
            GS(u32, cs + 0x4C) = GV(u32, pr + 0x3BC);
            u32 b = pr + 0x38C;
            u16 bx = GV(u16, b + 0x24), by = GV(u16, b + 0x28), bz = GV(u16, b + 0x2C);
            u16 x0 = (u16)(bx + GV(u16, b + 0)), x1 = (u16)(bx + GV(u16, b + 8));
            u16 y0 = (u16)(by + GV(u16, b + 2)), y1 = (u16)(by + GV(u16, b + 0xA));
            u16 z0 = (u16)(bz + GV(u16, b + 4)), z1 = (u16)(bz + GV(u16, b + 0xC));
            for (u32 c = 0; c < 8; c++) {
                u32 v = corners + c * 8;
                GS(u16, v) = (c & 4) ? x1 : x0;
                GS(u16, v + 2) = (c & 1) ? y1 : y0;
                GS(u16, v + 4) = (c & 2) ? z1 : z0;
            }
            for (u32 c = 0; c < 8; c++) {
                u32 v = corners + c * 8;
                GS(s32, cs + 0x18) = GS(s16, v);
                GS(s32, cs + 0x1C) = GS(s16, v + 2);
                GS(u32, cs) = 0;
                GS(s32, cs + 0x20) = GS(s16, v + 4);
                GCALL2(F_COORD_WORLD, cs, sp + 0x150);
                u32 w = world + c * 0x10;
                GS(u32, w) = GS(u32, cs + 0x38);
                GS(u32, w + 4) = GS(u32, cs + 0x3C);
                GS(u32, w + 8) = GS(u32, cs + 0x40);
            }
            for (u32 a = 0; a < 0xC; a += 4) {
                GS(s32, mx + a) = GS(s32, world + a);
                GS(s32, mn + a) = GS(s32, world + a);
            }
            for (u32 c = 1; c < 8; c++) {
                u32 w = world + c * 0x10;
                for (u32 a = 0; a < 0xC; a += 4) {
                    s32 v = GS(s32, w + a);
                    if (v < GS(s32, mn + a)) GS(s32, mn + a) = v;
                    if (GS(s32, mx + a) < v) GS(s32, mx + a) = v;
                }
            }
            int inside = 0;
            u32 lo = (u32)GS(s32, mn) + GS(u32, box), hi = (u32)GS(s32, mx) + GS(u32, box + 0x10);
            GS(u32, mn) = lo;
            GS(u32, mx) = hi;
            if (!(px < (s32)lo) && !((s32)hi < px)) {
                lo = (u32)GS(s32, mn + 4) + GS(u32, box + 4);
                hi = (u32)GS(s32, mx + 4) + GS(u32, box + 0x14);
                GS(u32, mn + 4) = lo;
                GS(u32, mx + 4) = hi;
                s32 py = (s16)P16(g_curPanel, 0x12);
                if (!(py < (s32)lo) && !((s32)hi < py)) {
                    lo = (u32)GS(s32, mn + 8) + GS(u32, box + 8);
                    hi = (u32)GS(s32, mx + 8) + GS(u32, box + 0x18);
                    GS(u32, mn + 8) = lo;
                    GS(u32, mx + 8) = hi;
                    inside = !(pz < (s32)lo) && !((s32)hi < pz);
                }
            }
            if (inside) {
                u32 c = GV(u32, b + 0x30);           /* the part's coordinate system */
                GS(u32, out_coord) = c;
                GS(s32, cs + 0x18) = 1000;
                GS(s32, cs + 0x1C) = 1000;
                GS(s32, cs + 0x20) = 1000;
                GS(u32, cs) = 0;
                GS(u32, cs + 0x4C) = GV(u32, b + 0x30);
                GCALL2(F_COORD_WORLD, cs, sp + 0x150);
                u32 g = g_curPanel;
                s32 ox = GS(s32, c + 0x38);
                s32 d = clamp80((s32)(((u32)(s32)(s16)P16(g, 0x10) << 2) - (u32)ox));
                GS(s32, push) = (s32)((u32)d * (GS(u32, cs + 0x38) - (u32)ox)) / 1000;
                s32 oy = GS(s32, c + 0x3C);
                d = (s32)((u32)(s32)(s16)P16(g, 0x12) - (u32)oy);
                GS(s32, push + 4) = (s32)((u32)d * (GS(u32, cs + 0x3C) - (u32)oy)) / 1000;
                s32 oz = GS(s32, c + 0x40);
                d = clamp80((s32)(((u32)(s32)(s16)P16(g, 0x18) << 4) - (u32)oz));
                GS(s32, push + 8) = (s32)((u32)d * (GS(u32, cs + 0x40) - (u32)oz)) / 1000;
                for (u32 a = 0; a < 0x10; a += 4) GS(u32, out_push + a) = GS(u32, push + a);
                if (no_stat == 0) {
                    u32 hits = g_statHits;
                    if (hits < 0xFFFFFFFFu) {
                        u32 flag = SHOT_COUNTED + (P16(g_curPanel, 0x26) & 3);
                        if (GV(u8, flag) == 0) {
                            g_statHits = hits + 1;
                            GV(u8, SHOT_COUNTED + (P16(g_curPanel, 0x26) & 3)) = 1;
                        }
                    }
                }
                V0 = (u32)((obj << 4) + part + 1);
                return;
            }
            if (GV(s16, r + (u32)(part + 1) * 0x5C + 0x392) < 0) break;
        }
    }
    V0 = 0;
}

/* 8009D638: smoke (0x8009D71C) where the current panel is, a little back
 * along its path. The current panel is kept. */
void n_fx_SmokeSpawn(void) {
    FRAME(0x18);
    s16 i = alloc_panel();
    if (i == -1) { V0 = (u32)(s32)i << 16; return; }
    u32 q = panel_addr(i);
    u32 src = g_curPanel;
    P16(q, PANEL_FLAGS) |= 0x400;
    P16(q, 0x10) = (u16)(P16(src, 0x10) - (s16)P16(src, 0x2E) / 200);
    P16(q, 0x12) = P16(src, 0x12);
    P16(q, 0x2C) = 0;
    P32(q, PANEL_UPDATE) = CB_SMOKE;
    s32 dz = (s16)P16(src, 0x32) / 800;
    P16(q, 0x18) = (u16)(P16(src, 0x18) - dz);
    V0 = (u32)dz;
}

/* A flame sprite (0x8009DAA4) where panel src is; it becomes the current
 * panel. */
static void flame_sprite(u32 q, u32 src) {
    g_curPanel = q;
    P16(q, PANEL_FLAGS) |= 0x41C;
    set_sprite(q, 0x800A7038u);
    q = g_curPanel;
    P16(q, 0x10) = P16(src, 0x10);
    P16(q, 0x12) = P16(src, 0x12);
}

/* 8009D800: a grenade's fire on the floor: three flames spreading from
 * where the current panel is (at 0x300 apart, starting a little to one
 * side of the way from the camera; random frame) and one staying (random
 * phase). The current panel is kept. */
void n_fx_FlameBurst(void) {
    FRAME(0x30);
    u32 p = g_curPanel;
    s32 a = (s32)GCALL2(F_RATAN2, (u32)(((s32)(s16)P16(p, 0x18) << 4) - CAM_Z),
                        (u32)(((s32)(s16)P16(p, 0x10) << 2) - CAM_X));
    if (a < 0) a = (a + 0x1000) & 0xFFF;
    u32 ang = (u32)(a - 0x300) & 0xFFF;
    u32 src = g_curPanel;
    for (s32 k = 0;;) {
        s16 i = alloc_panel();
        if (i == -1) break;
        flame_sprite(panel_addr(i), src);
        u32 q = g_curPanel;
        P16(q, 0x18) = P16(src, 0x18);
        u32 c = GCALL1(F_RCOS, ang & 0xFFFF);
        P16(g_curPanel, 0x2E) = (u16)((c * 125) >> 9);
        u32 sn = GCALL1(F_RSIN, ang & 0xFFFF);
        q = g_curPanel;
        P16(q, 0x32) = (u16)((sn * 125) >> 9);
        P16(q, 0x34) = 0xCC;
        start_anim(0x800A7B00u);
        s32 r = (s32)GCALL0(F_RAND_);
        ang = (ang + 0x300) & 0xFFF;
        q = g_curPanel;
        P32(q, PANEL_UPDATE) = CB_FLAME;
        P16(q, 0x2C) = 0;
        P16(q, 0x1E) = (u16)(r % 12);
        if (++k >= 3) break;
        LOOP_POLL(0x8009D9B0u);
    }
    s16 i = alloc_panel();
    if (i != -1) {
        flame_sprite(panel_addr(i), src);
        u32 q = g_curPanel;
        P16(q, 0x32) = 0;
        P16(q, 0x2E) = 0;
        P16(q, 0x18) = P16(src, 0x18);
        s32 r = (s32)GCALL0(F_RAND_);
        q = g_curPanel;
        P16(q, 0x30) = (u16)((r >> 4) & 0xFFF);
        P16(q, 0x34) = 0xCC;
        start_anim(0x800A7B00u);
        q = g_curPanel;
        P32(q, PANEL_UPDATE) = CB_FLAME;
        P16(q, 0x32) = 0;
        P16(q, 0x2E) = 0;
        P16(q, 0x2C) = 0;
    }
    g_curPanel = src;
}

/* 8009DAA4: a floor flame: animates (its frame count +0x1A slows it),
 * flickers in size by a phase (+0x30: low 12 bits the phase, top 4 its
 * speed), standing on its base (+0x28); after 76 frames it shrinks and
 * goes. Every 8 frames it creeps (+0x2E / 45, +0x32 / 180) unless a wall
 * is in the way; every 10 it burns an enemy it touches (one with no hits
 * queued). */
void n_fx_FlameUpdate(void) {
    FRAME(0x78);
    u32 p = g_curPanel;
    s16 f = (s16)P16(p, 0x1A);
    if (f >= 2) {
        P16(p, 0x1A) = 1;
        P16(p, 0x1E) = (u16)(P16(p, 0x1E) - 1);
        GCALL0(F_ANIM_LOOP_A);
        P16(g_curPanel, 0x1A) = (u16)(f - 1);
    } else {
        GCALL0(F_ANIM_LOOP_A);
    }
    if ((s16)P16(g_curPanel, 0x2C) == 0) {
        u32 r = GCALL0(F_RAND_);
        P16(g_curPanel, 0x30) = (u16)(((s32)r >> 2) & 0xFFF);
        r = GCALL0(F_RAND_);
        P16(g_curPanel, 0x30) |= (u16)((r << 9) & 0x7000);
    }
    p = g_curPanel;
    u16 t = (u16)(P16(p, 0x2C) + 1);
    P16(p, 0x2C) = t;
    if ((s16)t >= 0x4C) {
        u16 sc = (u16)(P16(p, 0x34) - 10);
        P16(p, 0x34) = sc;
        if (sc < 0x33) { P16(p, PANEL_FLAGS) = 1; return; }
    }
    p = g_curPanel;
    u16 v = P16(p, 0x30);
    u32 ph = ((v & 0xFFFu) + ((v & 0xF000u) >> 8)) & 0xFFF;
    P16(p, 0x30) = (u16)((v & 0xF000u) | ph);
    s32 sn = (s32)GCALL1(F_RSIN, ph);
    p = g_curPanel;
    u32 k = (u32)((sn >> 7) + 0x100);
    s32 h = (s32)(P16(p, 0xA) * k * P16(p, 0x34)) >> 16;
    s32 w = (s32)(P16(p, 0x8) * k * P16(p, 0x34)) >> 16;
    P16(p, 0x16) = (u16)h;
    P16(p, 0x14) = (u16)w;
    P16(p, 0x12) = (u16)(P16(p, 0x28) - (u16)h * 10);
    u32 from = sp + 0x18, to = sp + 0x28;
    if ((P16(p, 0x2C) & 7) == 0) {
        s32 x = (s32)(s16)P16(p, 0x10) << 2, y = (s16)P16(p, 0x12), z = (s32)(s16)P16(p, 0x18) << 4;
        GS(s32, from) = x;
        GS(s32, from + 4) = y;
        GS(s32, from + 8) = z;
        s32 dx = (s16)((s16)P16(p, 0x2E) / 45);
        s32 dz = (s16)((s16)P16(p, 0x32) / 180);
        GS(s32, to + 4) = y;
        GS(s32, to) = x + dx * 4;
        GS(s32, to + 8) = z + dz * 16;
        if ((s32)GCALL2(F_LINE_HIT, from, to) == -1) {
            u32 q = g_curPanel;
            s32 rx = (s16)P16(q, 0x10) - (s16)P16(q, 0x26);
            u16 bx = (u16)(P16(q, 0x26) + dx);
            P16(q, 0x26) = bx;
            P16(q, 0x10) = (u16)(bx + rx);
            s32 rz = (s16)P16(q, 0x18) - (s16)P16(q, 0x2A);
            u16 bz = (u16)(P16(q, 0x2A) + dz);
            P16(q, 0x2A) = bz;
            P16(q, 0x18) = (u16)(bz + rz);
        }
    }
    if ((s16)P16(g_curPanel, 0x2C) % 10 != 0) return;
    u32 box = sp + 0x38;
    GS(s32, box + 0x18) = 0x140;
    GS(s32, box + 0x10) = 0x140;
    GS(s32, box + 0x14) = 0x1F4;
    GS(s32, box + 8) = -0x140;
    GS(s32, box) = -0x140;
    GS(s32, box + 4) = -0x140;
    GS(u32, sp + 0x10) = 1;
    s32 hit = (s32)GCALL4(0x8009C97Cu, sp + 0x58, from, box, 0x578);
    if (hit == 0) return;
    s32 hh = hit - 1;
    u32 cur = g_curPanel;
    u32 rec = g_roomObjs + hit_record_offset(hh);
    if (GV(s16, rec + 0x324) == -1) GCALL4(F_ENEMY_HIT, rec, 5, (u32)hh & 0xF, 0);
    g_curPanel = cur;
}

/* 8009D71C: smoke: after 3 frames it shows, plays once, and drifts up
 * and sideways (its animation base +26/+28 moves). */
void n_fx_SmokeUpdate(void) {
    FRAME(0x18);
    u32 p = g_curPanel;
    u16 t = (u16)(P16(p, 0x2C) + 1);
    P16(p, 0x2C) = t;
    if ((s16)t == 4) {
        P16(p, PANEL_FLAGS) |= 0x41C;
        set_sprite(p, 0x800A6FACu);
        start_anim(0x800A7BE0u);
    }
    if ((s16)P16(g_curPanel, 0x2C) < 4) return;
    GCALL0(F_ANIM_ONCE_A);
    p = g_curPanel;
    if (P16(p, PANEL_FLAGS) & 1) return;
    s32 rx = (s32)(s16)P16(p, 0x10) - (s16)P16(p, 0x26);
    u16 bx = (u16)(P16(p, 0x26) - 2);
    s32 ry = (s32)(s16)P16(p, 0x12) - (s16)P16(p, 0x28);
    u16 by = (u16)(P16(p, 0x28) - 0x35);
    P16(p, 0x26) = bx;
    P16(p, 0x10) = (u16)(bx + rx);
    P16(p, 0x28) = by;
    P16(p, 0x12) = (u16)(by + ry);
}

/* 8009DE6C: spawn the explosion of the current projectile, a little back
 * along its path; acid grenades (kind 1) get the second stage. */
void n_fx_ExplosionSpawn(void) {
    FRAME(0x20);
    s16 i = alloc_panel();
    if (i == -1) return;
    u32 q = panel_addr(i);
    u32 src = g_curPanel;
    P16(q, PANEL_FLAGS) |= 0x41C;
    g_curPanel = q;
    set_sprite(q, 0x800A6FACu);
    q = g_curPanel;
    P16(q, 0x10) = (u16)(P16(src, 0x10) - (s16)P16(src, 0x2E) / 200);
    P16(q, 0x12) = P16(src, 0x12);
    P16(q, 0x18) = (u16)(P16(src, 0x18) - (s16)P16(src, 0x32) / 800);
    start_anim(0x800A7CA8u);
    q = g_curPanel;
    P32(q, PANEL_UPDATE) = CB_EXPLOSION;
    P16(q, 0x2C) = (s16)P16(src, 0x24) == 1 ? 0 : 0x100;
    g_curPanel = src;
}

/* 8009DF94: the explosion: on its first frame it hurts a room object in
 * reach (box +-0x140, a spark where), then plays once; at its end, acid
 * (+2C 1) leaves a second cloud, the others smoke (0x8009D71C). (A new
 * panel index 0 is taken as "none".) */
void n_fx_ExplosionUpdate(void) {
    FRAME(0x68);
    if (GV(u8, g_curPanel + 0x2C) == 0) {
        u32 box = sp + 0x18, push = sp + 0x38, coord = sp + 0x48;
        GS(s32, box + 8) = -0x140;
        GS(s32, box + 4) = -0x140;
        GS(s32, box) = -0x140;
        GS(s32, box + 0x18) = 0x140;
        GS(s32, box + 0x14) = 0x140;
        GS(s32, box + 0x10) = 0x140;
        GS(u32, sp + 0x10) = 1;
        s32 hit = (s32)GCALL4(0x8009C97Cu, coord, push, box, 0x578);
        if (hit != 0) {
            s32 h = hit - 1;
            u32 rec = hit_record_offset(h);
            GCALL4(F_ENEMY_HIT, g_roomObjs + rec, 5, (u32)h & 0xF, 0);
            u32 c = GS(u32, coord);
            u32 g = g_curPanel;
            u32 x = GS(u32, c + 0x38) + GS(u32, push);
            u32 y = GS(u32, c + 0x3C) + GS(u32, push + 4);
            u32 z = GS(u32, c + 0x40) + GS(u32, push + 8);
            GS(u32, sp + 0x10) = g_roomObjs + rec;
            GCALL4(F_SPARK, x, y, z, 0);
            g_curPanel = g;
        }
        u32 p = g_curPanel;
        P16(p, 0x2C) = (u16)(P16(p, 0x2C) + 1);
    }
    GCALL0(F_ANIM_ONCE_A);
    if (!(P16(g_curPanel, PANEL_FLAGS) & 1)) return;
    s16 i = alloc_panel();
    if (i == 0) return;
    u32 q = panel_addr(i);
    u32 src = g_curPanel;
    g_curPanel = q;
    if (GV(u8, src + 0x2C) == 1) {                   /* acid: the second cloud */
        P16(q, PANEL_FLAGS) |= 0x41C;
        set_sprite(q, 0x800A6FACu);
        q = g_curPanel;
        P16(q, 0x0E) = (u16)(P16(q, 0x0E) + 3);
        P16(q, 0x10) = P16(src, 0x26);
        P16(q, 0x12) = P16(src, 0x28);
        P16(q, 0x18) = P16(src, 0x2A);
        start_anim(0x800A7DD8u);
        q = g_curPanel;
        P32(q, PANEL_UPDATE) = CB_EXPLOSION;
        P16(q, 0x2C) = (u16)(P16(src, 0x2C) + 1);
    } else {                                         /* smoke */
        P16(q, PANEL_FLAGS) |= 0x400;
        P16(q, 0x10) = P16(src, 0x26);
        P16(q, 0x12) = P16(src, 0x28);
        P32(q, PANEL_UPDATE) = CB_SMOKE;
        P16(q, 0x2C) = 3;
        P16(q, 0x18) = P16(src, 0x2A);
    }
    g_curPanel = src;
}

/* 8009E1FC: the blast flash where a projectile exploded: set up on its
 * first frame (+2C bit 4 then set), plays once, dimmer near its end. */
void n_fx_BlastUpdate(void) {
    FRAME(0x18);
    u32 p = g_curPanel;
    u16 t = P16(p, 0x2C);
    if (!(t & 0x10)) {
        t = (u16)(t + 1);
        P16(p, 0x2C) = t;
        if ((s16)t == 1) {
            P16(p, PANEL_FLAGS) &= 0xFFEF;
            set_sprite(p, 0x800A6FACu);
            p = g_curPanel;
            P16(p, 0x0E) = (u16)(P16(p, 0x0E) + 3);
            start_anim(0x800A7D78u);
            P16(g_curPanel, 0x2C) = 0x10;
        }
        return;
    }
    if ((s16)P16(p, 0x1A) == 1 && (s16)P16(p, 0x1E) == 10) {
        P16(p, 0x0E) = (u16)(P16(p, 0x0E) - 2);
        P16(p, PANEL_FLAGS) |= 0x10;
    }
    GCALL0(F_ANIM_ONCE_A);
}

/* 8009F0D0: fire the rocket launcher at the reticle (as 0x8009BBF0, a
 * smaller sprite and four times the speed, no fall). */
void n_rl_Fire(void) {
    FRAME(0x60);
    s16 i = alloc_panel();
    if (i == -1) return;
    u32 at = sp + 0x18, from = sp + 0x28, to = sp + 0x38;
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_UNPROJECT, (u32)(g_reticleX - 0xA0), (u32)(g_reticleY - 0x3C), 0x1F4, at);
    u32 p = panel_addr(i);
    P16(p, PANEL_FLAGS) |= 0x41C;
    g_curPanel = p;
    set_sprite(p, 0x800A704Cu);
    p = g_curPanel;
    P16(p, 0x14) = 4;
    P16(p, 0x16) = 4;
    P16(p, 0x08) = 0x10;
    P16(p, 0x0A) = 0x10;
    P16(p, 0x04) = (u16)(P16(p, 0x04) + 0xA8);
    P16(p, 0x06) = (u16)(P16(p, 0x06) + 0x20);
    GS(s32, from) = CAM_X;
    GS(s32, from + 4) = CAM_Y;
    GS(s32, from + 8) = CAM_Z;
    GS(u32, to) = GS(u32, at);
    GS(u32, to + 4) = GS(u32, at + 4);
    GS(u32, to + 8) = GS(u32, at + 8);
    s32 line = (s32)GCALL2(F_LINE_HIT, from, to);
    if (line != -1) {                                /* blocked: explode here */
        u32 ex = GS(u32, to), ey = GS(u32, to + 4), ez = GS(u32, to + 8);
        GS(u32, to + 8) = 0;
        GS(u32, to + 4) = 0;
        GS(u32, to) = 0;
        GS(u32, at) = ex;
        GS(u32, at + 4) = ey;
        GS(u32, at + 8) = ez;
        GS(u32, sp + 0x10) = to;
        GCALL4(0x8009C7C8u, 0, 0, 0, at);
        GCALL2(0x8009C17Cu, g_curPanel, (u32)line);
        return;
    }
    p = g_curPanel;
    P16(p, 0x10) = (u16)div_pow2(GS(s32, at), 2);
    P16(p, 0x12) = (u16)(GS(u16, at + 4) - 0x1C);
    P16(p, 0x18) = (u16)div_pow2(GS(s32, at + 8), 4);
    P32(p, PANEL_UPDATE) = CB_ROCKET;
    P16(p, 0x34) = (u16)(P16(p, 0x34) + 0x66);
    P16(p, 0x2E) = (u16)(((u32)GS(s32, at) - (u32)CAM_X) << 2);
    P16(p, 0x30) = (u16)(((u32)GS(s32, at + 4) - ((u32)CAM_Y + 0x1C)) << 2);
    P16(p, 0x2C) = 0;
    P16(p, 0x32) = (u16)(((u32)GS(s32, at + 8) - (u32)CAM_Z) << 2);
    P16(p, 0x26) = (u16)(GV(u16, 0x800C3A0C) & 3);
    next_shot();
}

/* 8009F330: a rocket in flight: it hits a room object (hurt it, burst,
 * explode), a collision line, or the floor (explode where it crossed
 * it); else it flies on and leaves a trail puff for its first 6 frames.
 * After 30 frames it explodes anyway. */
void n_rl_RocketUpdate(void) {
    FRAME(0x98);
    u32 p = g_curPanel;
    u32 pos = sp + 0x18, to = sp + 0x28, step = sp + 0x38, box = sp + 0x48, coord = sp + 0x68;
    s16 vx = (s16)P16(p, 0x2E);                      /* the box: the step, and half of it back */
    if (vx < 0) {
        GS(s32, box) = vx;
        GS(s32, box + 0x10) = (s32)(0u - (u32)(s32)(s16)P16(p, 0x2E)) / 2;
    } else {
        GS(s32, box) = (s32)(0u - (u32)(s32)vx) / 2;
        GS(s32, box + 0x10) = (s16)P16(p, 0x2E);
    }
    GS(s32, box + 4) = -100;
    p = g_curPanel;
    GS(s32, box + 0x14) = 100;
    s16 vz = (s16)P16(p, 0x32);
    if (vz < 0) {
        GS(s32, box + 8) = vz;
        GS(s32, box + 0x18) = (s32)(0u - (u32)(s32)(s16)P16(p, 0x32)) / 2;
    } else {
        GS(s32, box + 8) = (s32)(0u - (u32)(s32)vz) / 2;
        GS(s32, box + 0x18) = (s16)P16(p, 0x32);
    }
    GS(u32, sp + 0x10) = 0;
    s32 hit = (s32)GCALL4(0x8009C97Cu, coord, pos, box, 0x578);
    if (hit != 0) {                                  /* a room object */
        s32 h = hit - 1;
        GCALL4(F_ENEMY_HIT, g_roomObjs + hit_record_offset(h), 5, (u32)h & 0xF, 0);
        GCALL3(F_GRENADE_BURST, GS(u32, coord), pos, (u32)h);
        u32 c = GS(u32, coord);
        GS(u32, step + 8) = 0;
        GS(u32, step + 4) = 0;
        GS(u32, step) = 0;
        GS(u32, pos) = GS(u32, c + 0x38) + GS(u32, pos);
        GS(u32, pos + 4) = GS(u32, c + 0x3C) + GS(u32, pos + 4);
        GS(u32, pos + 8) = GS(u32, c + 0x40) + GS(u32, pos + 8);
        P16(g_curPanel, 0x24) = 3;
        GS(u32, sp + 0x10) = step;
        GCALL4(0x8009C7C8u, 0, 0, 0, pos);
        return;
    }
    p = g_curPanel;
    s32 x = (s32)(s16)P16(p, 0x10) << 2, y = (s16)P16(p, 0x12), z = (s32)(s16)P16(p, 0x18) << 4;
    GS(s32, pos) = x;
    GS(s32, pos + 4) = y;
    GS(s32, pos + 8) = z;
    GS(s32, to) = (s32)((u32)x + (u32)(s32)(s16)P16(p, 0x2E));
    GS(s32, to + 4) = (s32)((u32)y + (u32)(s32)(s16)P16(p, 0x30));
    GS(s32, to + 8) = (s32)((u32)z + (u32)(s32)(s16)P16(p, 0x32));
    for (u32 k = 0; k < 0x10; k += 4) GS(u32, step + k) = GS(u32, to + k);
    s32 line = (s32)GCALL2(F_LINE_HIT, pos, to);
    if (line != -1) {                                /* a wall */
        u32 g = g_curPanel;
        P16(g, 0x24) = 3;
        s32 dx = div_pow2((s16)P16(g, 0x2E), 4);
        s32 dy = div_pow2((s16)P16(g, 0x30), 4);
        s32 dz = div_pow2((s16)P16(g, 0x32), 4);
        GS(u32, sp + 0x10) = step;
        GCALL4(0x8009C7C8u, (u32)dx, (u32)dy, (u32)dz, pos);
        GCALL2(0x8009C17Cu, g_curPanel, (u32)line);
        return;
    }
    p = g_curPanel;
    GS(s32, pos) = (s32)(s16)P16(p, 0x10) << 2;
    GS(s32, pos + 4) = (s16)P16(p, 0x12);
    GS(s32, pos + 8) = (s32)(s16)P16(p, 0x18) << 4;
    s32 floor = g_floorY;
    if (!((s16)P16(p, 0x12) < floor)) {              /* below the floor: back to where it crossed */
        s32 below = (s32)((u32)(s32)(s16)P16(p, 0x12) - (u32)floor);
        s32 bx = mips_div((s32)((u32)(s32)(s16)P16(p, 0x2E) * (u32)below), (s16)P16(p, 0x30));
        s32 bz = mips_div((s32)((u32)(s32)(s16)P16(p, 0x32) * (u32)below), (s16)P16(p, 0x30));
        P16(p, 0x10) = (u16)(P16(p, 0x10) - (u16)div_pow2(bx, 2));
        P16(p, 0x24) = 3;
        P16(p, 0x12) = (u16)floor;
        P16(p, 0x18) = (u16)(P16(p, 0x18) - (u16)div_pow2(bz, 4));
        GS(s32, pos) = (s32)(s16)P16(p, 0x10) << 2;
        GS(s32, pos + 4) = (s16)P16(p, 0x12);
        GS(u32, step + 8) = 0;
        GS(u32, step + 4) = 0;
        GS(u32, step) = 0;
        GS(s32, pos + 8) = (s32)(s16)P16(p, 0x18) << 4;
        GS(u32, sp + 0x10) = step;
        GCALL4(0x8009C7C8u, 0, 0, 0, pos);
        return;
    }
    P16(p, 0x10) = (u16)(P16(p, 0x10) + div_pow2((s16)P16(p, 0x2E), 2));
    P16(p, 0x12) = (u16)(P16(p, 0x12) + P16(p, 0x30));
    s32 sz = div_pow2((s16)P16(p, 0x32), 4);
    u16 age = (u16)(P16(p, 0x2C) + 1);
    P16(p, 0x2C) = age;
    P16(p, 0x18) = (u16)(P16(p, 0x18) + sz);
    if ((s16)age < 7) {                              /* a trail puff */
        s16 i = alloc_panel();
        if (i != (s16)line) {                        /* -1: the pool is full */
            u32 r = g_curPanel;
            s32 sx = (s16)div_pow2((s16)P16(r, 0x2E), 2);
            s32 sy = (s16)P16(r, 0x30);
            s32 szz = (s16)div_pow2((s16)P16(r, 0x32), 4);
            u32 q = panel_addr(i);
            s32 a = (s16)P16(r, 0x2C);
            P16(q, PANEL_FLAGS) |= 0x41C;
            g_curPanel = q;
            u32 look = TRAIL_LOOKS + (u32)(a - 1) * 8;
            set_sprite(q, 0x800A6FACu);
            q = g_curPanel;
            P16(q, 0x04) = (u16)(P16(q, 0x04) + GV(u16, look));
            P16(q, 0x06) = (u16)(P16(q, 0x06) + GV(u16, look + 2));
            P16(q, 0x08) = GV(u16, look + 4);
            P16(q, 0x14) = (u16)(GV(u16, look + 4) >> 2);
            P32(q, 0x38) = r;                        /* the rocket it follows */
            P16(q, 0x0A) = GV(u16, look + 6);
            P32(q, PANEL_UPDATE) = CB_TRAIL;
            P16(q, 0x16) = (u16)(P16(q, 0x0A) >> 2);
            /* its offset behind the rocket: speed * age - speed / 3 */
            P16(q, 0x22) = (u16)((u32)sx * (u32)a - (u32)(sx / 3));
            P16(q, 0x24) = (u16)((u32)sy * (u32)a - (u32)(sy / 3));
            u16 oz = (u16)((u32)szz * (u32)a - (u32)(szz / 3));
            P16(q, 0x26) = oz;
            P16(q, 0x10) = (u16)(P16(r, 0x10) - P16(q, 0x22));
            P16(q, 0x12) = (u16)(P16(r, 0x12) - P16(q, 0x24));
            g_curPanel = r;
            P16(q, 0x18) = (u16)(P16(r, 0x18) - oz);
        }
    }
    p = g_curPanel;
    if ((s16)P16(p, 0x2C) >= 0x1F) {                 /* burnt out: explode */
        P16(p, 0x32) = 0;
        P16(p, 0x2E) = 0;
        n_proj_Explode();
    }
}

/* 8009F9B4: a rocket trail puff: follows its rocket (+38) at its offset
 * (+22/24/26) while the rocket flies; then goes. */
void n_rl_TrailUpdate(void) {
    u32 p = g_curPanel;
    u32 r = P32(p, 0x38);
    V0 = 1;
    if (P32(r, PANEL_UPDATE) != CB_ROCKET || (P16(r, PANEL_FLAGS) & 0x101)) {
        P16(p, PANEL_FLAGS) = 1;
        return;
    }
    P16(p, PANEL_FLAGS) |= 4;
    P16(p, 0x10) = (u16)(P16(r, 0x10) - P16(p, 0x22));
    P16(p, 0x12) = (u16)(P16(r, 0x12) - P16(p, 0x24));
    V0 = (u32)P16(r, 0x18) - (u32)P16(p, 0x26);
    P16(p, 0x18) = (u16)V0;
}

/* 8009FB00: a dripping spot (from hit effect 15): every 150 frames a
 * drop forms (0x8009FBF8; retried next frame if no panel is free). */
void n_fx_DripEmitter(void) {
    FRAME(0x20);
    u32 p = g_curPanel;
    u16 t = (u16)(P16(p, 0x2C) - 1);
    P16(p, 0x2C) = t;
    if (t != 0) return;
    s16 i = alloc_panel();
    if (i == -1) {
        P16(g_curPanel, 0x2C) = 1;
        return;
    }
    u32 q = panel_addr(i);
    u32 src = g_curPanel;
    P16(q, PANEL_FLAGS) |= 0x40C;
    g_curPanel = q;
    set_sprite(q, 0x800A7060u);
    q = g_curPanel;
    P16(q, 0x10) = P16(src, 0x10);
    P16(q, 0x12) = P16(src, 0x12);
    P16(q, 0x34) = 0x28;
    P16(q, 0x18) = P16(src, 0x18);
    start_anim(0x800A8118u);
    q = g_curPanel;
    g_curPanel = src;
    P32(q, PANEL_UPDATE) = CB_DROP;
    P16(q, 0x2C) = 0;
    P16(src, 0x2C) = 0x96;
}

/* 8009FBF8: a drop: +2C 0 forming (at its end the falling drop takes
 * over), 1 falling (accelerating) until the floor height -0x28A, where
 * it splashes (+2C 2) with a sound by its distance from the camera. */
void n_fx_DropUpdate(void) {
    FRAME(0x20);
    u32 p = g_curPanel;
    if ((s16)P16(p, 0x2C) == 1) {                    /* falling */
        u16 vy = (u16)(P16(p, 0x30) + 8);
        u16 y = (u16)(P16(p, 0x12) + vy);
        P16(p, 0x12) = y;
        P16(p, 0x30) = vy;
        if ((s16)y < -0x28A) return;
        s32 dx = (s32)(((u32)(s32)(s16)P16(p, 0x10) << 2) - (u32)CAM_X);
        s32 dz = (s32)(((u32)(s32)(s16)P16(p, 0x18) << 4) - (u32)CAM_Z);
        s32 d = (s32)GCALL1(F_SQRT, (u32)dx * (u32)dx + (u32)dz * (u32)dz);
        s32 vol = 0x80 - (d >> 7);
        if (vol > 0) GCALL4(F_SOUND_AT, 6, 2, (u32)vol, (u32)vol);
        p = g_curPanel;
        P16(p, 0x10) = P16(p, 0x26);
        P16(p, 0x18) = P16(p, 0x2A);
        P16(p, 0x12) = (u16)(P16(p, 0x12) + 0x1E);
        start_anim(0x800A8120u);
        P16(g_curPanel, 0x2C) = 2;
        return;
    }
    GCALL0(F_ANIM_ONCE_A);
    p = g_curPanel;
    if (!(P16(p, PANEL_FLAGS) & 1)) return;
    if ((s16)P16(p, 0x2C) != 0) return;
    s16 i = alloc_panel();                           /* formed: the falling drop */
    if (i == -1) return;
    u32 q = panel_addr(i);
    u32 src = g_curPanel;
    P16(q, PANEL_FLAGS) |= 0x40C;
    g_curPanel = q;
    set_sprite(q, 0x800A7060u);
    q = g_curPanel;
    P16(q, 0x08) = 0x10;
    P16(q, 0x0A) = 0x10;
    P16(q, 0x04) = (u16)(P16(q, 0x04) + 0x50);
    P16(q, 0x10) = P16(src, 0x10);
    P16(q, 0x12) = P16(src, 0x12);
    P16(q, 0x34) = 0x28;
    g_curPanel = src;
    P16(q, 0x30) = 0;
    P16(q, 0x2C) = 1;
    P16(q, 0x18) = P16(src, 0x18);
    P32(q, PANEL_UPDATE) = CB_DROP;
    P16(q, 0x14) = (u16)((0x10 * 0x28) >> 8);
    P16(q, 0x16) = (u16)((P16(q, 0x0A) * 0x28) >> 8);
}
