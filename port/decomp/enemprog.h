/* Enemy records and the TEST-module functions the enemy programs
 * (ENEMPROG overlays) call. Used by zombie.c.
 *
 * Enemies live in an array of 0x8C4-byte records (base pointer at
 * 0x800AB510, count at 0x800AF530; 0x800AB518 points at the one being
 * updated). The 3D object part of a record starts at +0x180 and has the
 * same layout as the player record (g_player): position at +0x198
 * (x, y, z), rotation at +0x1C4 (x, yaw, z; 4096 = 360 degrees).
 *
 * Every enemy runs a two-level state machine: a state (+0x316: 0 set up,
 * 1 active, 2 hit, 3 dying, ...) and inside it an action (+0x318) with a
 * step counter (+0x31C). An action has two handlers in the overlay's
 * tables: one run every frame and one run while the action's phase
 * (+0x31A) is negative. enemy_SetAction (0x800802C8) and friends set the
 * action and queue an animation. */
#ifndef ENEMPROG_H
#define ENEMPROG_H

#include "game_vars.h"

/* ---- enemy record fields (e is the record's game address) ---- */
#define EN_ALIVE(e)       GV(u8, (e) + 0x000)    /* drawn / active */
#define EN_OBJ(e)         ((e) + 0x180)          /* 3D object (player-record layout) */
#define EN_POS(e)         ((e) + 0x198)          /* x, y, z (s32) */
#define EN_X(e)           GV(s32, (e) + 0x198)
#define EN_Y(e)           GV(s32, (e) + 0x19C)
#define EN_Z(e)           GV(s32, (e) + 0x1A0)
#define EN_ROT(e)         ((e) + 0x1C4)          /* x, yaw, z (s16) */
#define EN_ROTX(e)        GV(s16, (e) + 0x1C4)
#define EN_YAW(e)         GV(s16, (e) + 0x1C6)
#define EN_ROTZ(e)        GV(s16, (e) + 0x1C8)
#define EN_NPARTS(e)      GV(s32, (e) + 0x1D0)   /* animated model parts */
#define EN_ANIMPART(e, i) GV(u32, (e) + 0x1D4 + (u32)(i) * 4)
#define EN_SLOT(e)        GV(u8, (e) + 0x314)    /* index of the record */
#define EN_TYPE(e)        GV(u8, (e) + 0x315)    /* variant */
#define EN_STATE(e)       GV(s16, (e) + 0x316)
#define EN_ACTION(e)      GV(s16, (e) + 0x318)
#define EN_PHASE(e)       GV(s16, (e) + 0x31A)
#define EN_STEP(e)        GV(s16, (e) + 0x31C)
#define EN_HP(e)          GV(s16, (e) + 0x31E)
#define EN_FLAGS(e)       GV(u32, (e) + 0x320)
#define EF_SHOT       0x10       /* hit by a shot this frame (enemy_AddHit) */
#define EF_SEES       0x40       /* the player is in view */
#define EF_GRABBING   0x20000    /* holding the player */
/* Hits this frame: count, then per hit (1..4) the body part, the side it
 * came from, the damage and the shot's value (enemy_AddHit). */
#define EN_NHITS(e)       GV(s16, (e) + 0x324)
#define EN_HIT_PART(e, i) GV(u8, (e) + 0x326 + (u32)(i))
#define EN_HIT_SIDE(e, i) GV(u8, (e) + 0x32B + (u32)(i))
#define EN_HIT_DMG(e, i)  GV(s16, (e) + 0x330 + (u32)(i) * 2)
#define EN_HIT_VAL(e, i)  GV(s16, (e) + 0x33A + (u32)(i) * 2)
/* Body parts: 0x5C-byte records from +0x392 (list ends with a negative
 * +0x392); +0x39A is the part's kind (-1 once shot off). */
#define EN_BODY(e, p)     ((e) + (u32)(p) * 0x5C)
#define EN_BODY_END(e, p) GV(s16, EN_BODY(e, p) + 0x392)
#define EN_BODY_KIND(e, p) GV(s16, EN_BODY(e, p) + 0x39A)
#define EN_BODY_OBJ(e, p) GV(u32, EN_BODY(e, p) + 0x3BC)
#define EN_ANIMDATA(e)    GV(u32, (e) + 0x88C)
#define EN_ROOMID(e)      GV(s16, (e) + 0x850)
#define EN_TIMER(e)       GV(s16, (e) + 0x852)
#define EN_ANIM(e)        GV(s16, (e) + 0x85E)   /* animation playing */
#define EN_SPEED(e)       GV(s16, (e) + 0x866)
#define EN_DIST(e)        GV(s32, (e) + 0x868)   /* distance to the player */
#define EN_VEL(e)         ((e) + 0x87C)          /* movement this frame x, y, z */
#define EN_VX(e)          GV(s32, (e) + 0x87C)
#define EN_VY(e)          GV(s32, (e) + 0x880)
#define EN_VZ(e)          GV(s32, (e) + 0x884)
#define EN_ANIMSTATE(e)   GV(u32, (e) + 0x1D8)   /* animation player: +0xE last frame, +0x14 frame, +0x16 time */

#define ep_enemies         GV(u32, 0x800AB510)
#define ep_enemyCount      GV(s32, 0x800AF530)
#define ep_enemyCur        GV(u32, 0x800AB518)    /* the enemy being updated */
#define ENEMY_SIZE        0x8C4
#define ep_frameCount      GV(u32, 0x80062E10)
#define EP_PLAYER_X      GV(s32, PLAYER_POS_ADDR)
#define EP_PLAYER_Z      GV(s32, PLAYER_POS_ADDR + 8)
#define ep_buttonsHi       GV(u32, 0x800B4B98)
#define ep_eventFlags      0x80062FC0u            /* flag bits (flag_Test etc.) */
#define ep_roomFlags       0x80062F60u
#define ep_grabLock        GV(s32, 0x800BBB58)
#define ep_grabLock2        GV(s32, 0x800A28D0)
#define D_800BBB80_ADDR   0x800BBB80u            /* per-enemy model table */

/* ---- TEST-module functions ---- */
#define FN_RAND             0x80011570u   /* kernel: random number */
#define FN_EN_SET_ANIM      0x80080268u   /* (e, anim): queue an animation (0x8000: no blend) */
#define FN_EN_SET_ACTION    0x800802C8u   /* (e, action, anim) */
#define FN_EN_SET_ANIM_B    0x80080334u   /* (e, anim, blend frames) */
#define FN_EN_SET_ACTION_B  0x8008035Cu   /* (e, action, anim, blend frames) */
#define FN_EN_SOUND         0x8008080Cu   /* (e, bank, sound): volume by distance */
#define FN_EN_SOUND_EX      0x80081098u   /* (e, bank, sound, x) */
#define FN_EN_PARAM         0x80082C38u   /* (e, k): the type's parameter k (+0x844) */
#define FN_ROT_VELOCITY     0x8008286Cu   /* (rot, out, speed): out = forward * speed */
#define FN_LINE_SIDE        0x8007C04Cu   /* (a, b, x, z): which side of a->b (x, z) is */
#define FN_LINE_DIST        0x8007BE48u   /* (a, b, p): distance from p to the line a-b */
#define FN_EN_TURN_TO_PLAYER 0x8007F210u
#define FN_EN_DIST_UPDATE   0x8007F4F4u   /* (e): distance to the player, also stored at +0x868 */
#define FN_EN_HIT_PRE       0x8008159Cu
#define FN_EN_HIT_POST      0x800800E0u
#define FN_EN_DRAW          0x80099068u   /* (obj, a, b, c, mode on the stack) */
#define FN_FLAG_TEST        0x8008F64Cu   /* (flags, bit) */
#define FN_FLAG_SET         0x8008F5F8u
#define FN_FLAG_CLEAR       0x8008F620u
#define FN_ANGLE_TO         0x800849D0u   /* (obj, pos): yaw difference to pos */
#define FN_EN_GRAB_PLAYER   0x80084D84u
#define FN_EN_GRAB_START    0x8008445Cu
#define FN_EN_KILLED        0x8007FD44u   /* (e, id) */
#define FN_PLAYER_BITTEN    0x80078D38u
#define FN_EN_BASE_SPEED    0x800829D8u   /* (e, 0): the type's walking speed */
#define FN_SOUND_CUE2       0x800805F4u   /* (a, b, vol l, vol r) */

static inline u32 en_rand(void) { return gcall(FN_RAND, 0, 0, 0, 0); }

/* Call a game function with a fifth argument, stored where the original
 * puts it: at sp+0x10 of the caller's frame (sp from FRAME). */
static inline u32 gcall5(u32 sp, u32 f, u32 a0, u32 a1, u32 a2, u32 a3, u32 a4) {
    GS(u32, sp + 0x10) = a4;
    return gcall(f, a0, a1, a2, a3);
}

/* ---- code compiled into several enemy programs ---- */

/* The room's wall segments (16 bytes each: x0, z0, x1, z1) and the two
 * lists of area edges (20 bytes each: the same and a height at +0x10). */
#define EP_WALLS_N        0x800B60C8u   /* u8 count, pointer at +4 */
#define EP_EDGES_N        0x800B60D0u   /* u8 count, pointer at +4 */
#define EP_EDGES2_N       0x800B60E0u   /* u8 count, pointer at +4 */

/* The first of n segments (stride bytes apart from `list`) that the path
 * from `from` to `to` (x, z at +0, +8) crosses, or -1; with front_only
 * only one crossed from its front (the side where the cross product is
 * negative). The loop in zombie_CrossedWall and its copies. */
static inline s32 en_crossed_segment_poll(u32 from, u32 to, u32 list, u32 n, u32 stride, int front_only, u32 poll_pc);
static inline s32 en_crossed_segment(u32 from, u32 to, u32 list, u32 n, u32 stride, int front_only) {
    return en_crossed_segment_poll(from, to, list, n, stride, front_only, 0);
}
/* The same, polling (RT_POLL_NOSAVE) at the loop's backward branch at
 * poll_pc as the translation does (0: no polls). */
static inline s32 en_crossed_segment_poll(u32 from, u32 to, u32 list, u32 n, u32 stride, int front_only, u32 poll_pc) {
    u32 w = list;
    for (u32 i = 0; i < n; i++, w += stride) {
        if (i != 0 && poll_pc) RT_POLL_NOSAVE(poll_pc);
        s32 ax = GV(s32, w), az = GV(s32, w + 4), bx = GV(s32, w + 8), bz = GV(s32, w + 12);
        s32 px = (s32)MEM_R32(from), pz = (s32)MEM_R32(from + 8);
        s32 qx = (s32)MEM_R32(to), qz = (s32)MEM_R32(to + 8);
        /* which side of the segment each end is on, which side of the path each segment end is on */
        s32 sp = (s32)((u32)(bz - az) * (u32)(px - ax) - (u32)(bx - ax) * (u32)(pz - az));
        s32 sq = (s32)((u32)(bz - az) * (u32)(qx - ax) - (u32)(bx - ax) * (u32)(qz - az));
        s32 sa = (s32)((u32)(qz - pz) * (u32)(ax - px) - (u32)(qx - px) * (u32)(az - pz));
        s32 sb = (s32)((u32)(qz - pz) * (u32)(bx - px) - (u32)(qx - px) * (u32)(bz - pz));
        int s1 = sp > 0 ? 1 : -1, s2 = sq > 0 ? 1 : -1;
        int s3 = sa > 0 ? 1 : -1, s4 = sb > 0 ? 1 : -1;
        if (s1 * s2 >= 0) continue;          /* both ends on one side of the segment */
        if (s3 * s4 >= 0) continue;          /* the path passes beside it */
        if (!front_only || sp <= 0) return (s32)i;
    }
    return -1;
}
static inline s32 en_crossed_wall(u32 from, u32 to) {
    return en_crossed_segment(from, to, GV(u32, EP_WALLS_N + 4), GV(u8, EP_WALLS_N), 16, 1);
}

/* Is the player inside the view cone (yaw +-11*spread) of enemy e? The
 * cone turns round when the enemy is upside down (on the ceiling: +0x320
 * & 0x200 and rot.x & 0x800, or else rot.x and rot.z both 0x800). The
 * stack frame (sp, the caller's FRAME(0x50)) holds the cone's ends. */
static inline u32 en_player_in_cone(u32 sp, u32 e, s32 spread) {
    u32 right = sp + 0x10, left = sp + 0x20, rot = sp + 0x30;
    u16 yaw = GV(u16, e + 0x1C6);
    MEM_W16(rot + 4, 0);
    MEM_W16(rot + 0, 0);
    MEM_W16(rot + 2, yaw);
    if (EN_FLAGS(e) & 0x200) {
        if (GV(u16, e + 0x1C4) & 0x800) MEM_W16(rot + 2, (u32)yaw - 0x800);
    } else if ((GV(u16, e + 0x1C4) & 0xFFF) == 0x800 && (GV(u16, e + 0x1C8) & 0xFFF) == 0x800) {
        MEM_W16(rot + 2, (u32)yaw - 0x800);
    }
    u32 half = (u32)spread * 11;
    MEM_W16(rot + 2, MEM_R16(rot + 2) + half);
    gcall(F_ROT_STEP, rot, left, 100, 0);
    MEM_W32(left, MEM_R32(left) + (u32)EN_X(e));
    MEM_W32(left + 8, MEM_R32(left + 8) + (u32)EN_Z(e));
    MEM_W16(rot + 2, MEM_R16(rot + 2) - half * 2);
    gcall(F_ROT_STEP, rot, right, 100, 0);
    MEM_W32(right, MEM_R32(right) + (u32)EN_X(e));
    MEM_W32(right + 8, MEM_R32(right + 8) + (u32)EN_Z(e));
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), left, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) <= 0) return 0;
    return (s32)gcall(FN_LINE_SIDE, EN_POS(e), right, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) < 0;
}

/* The point `dist` x 20 ahead of enemy e (ROT_VELOCITY by -dist) into the
 * stack vector v (x, y, z). */
static inline void en_point_ahead(u32 e, u32 v, s32 dist) {
    gcall(FN_ROT_VELOCITY, EN_ROT(e), v, (u32)-dist, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) * 20 + (u32)EN_X(e));
    MEM_W32(v + 4, MEM_R32(v + 4) * 20 + (u32)EN_Y(e));
    MEM_W32(v + 8, MEM_R32(v + 8) * 20 + (u32)EN_Z(e));
}

/* Is the point (tx, tz) inside the cone in front of enemy e between
 * yaw + half and yaw - half (F_ROT_STEP by `speed` each way)? rot (8
 * bytes), left and right (16) are stack vectors of the caller. */
static inline u32 en_point_in_cone(u32 rot, u32 left, u32 right, u32 e, s32 half, s32 speed, u32 tx, u32 tz) {
    MEM_W16(rot + 4, 0);
    MEM_W16(rot + 0, 0);
    MEM_W16(rot + 2, (u32)GV(u16, e + 0x1C6) + (u32)half);
    gcall(F_ROT_STEP, rot, left, (u32)speed, 0);
    MEM_W32(left, MEM_R32(left) + (u32)EN_X(e));
    MEM_W32(left + 8, MEM_R32(left + 8) + (u32)EN_Z(e));
    MEM_W16(rot + 2, MEM_R16(rot + 2) - (u32)half * 2);
    gcall(F_ROT_STEP, rot, right, (u32)speed, 0);
    MEM_W32(right, MEM_R32(right) + (u32)EN_X(e));
    MEM_W32(right + 8, MEM_R32(right + 8) + (u32)EN_Z(e));
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), left, tx, tz) <= 0) return 0;
    return (s32)gcall(FN_LINE_SIDE, EN_POS(e), right, tx, tz) < 0;
}

/* The event bit of animated part 1 one frame ahead (zombie_AnimEvent with
 * part 1, k 1). */
static inline u32 en_anim_event1(u32 e) {
    u32 p = EN_ANIMPART(e, 1);
    u32 frames = GV(u32, EN_ANIMDATA(e) + 8) + 4;
    u32 w;
    if (GV(s8, p + 0xD) >= 0) w = GV(u32, frames + (u32)GV(u16, p + 0x16) * 4 + 4);
    else w = GV(u32, frames + (u32)GV(u16, p + 0x14) * 4 - 4);
    return w >> 31;
}

/* Start animation `anim` the way the later enemy programs do it
 * themselves: the animation wanted (+0x31A, +0x860), +0x85C = 1, the blend
 * frames (+0x8B0), and clear +0x320 bits 2, 3 and 15. */
static inline void en_start_anim(u32 e, u32 anim, u32 blend) {
    EN_ALIVE(e) = 1;
    GV(u16, e + 0x31A) = (u16)anim;
    GV(u16, e + 0x860) = (u16)anim;
    GV(u16, e + 0x85C) = 1;
    GV(u32, e + 0x8B0) = blend;
    EN_FLAGS(e) &= 0xFFFF7FF3u;
}


/* ---- code compiled into the boss programs (the Tyrant, ENEMPROG 58; the
 * Hunter, 60; the final boss, 63) ---- */

/* The event bit of animated part `part`, k frames ahead (or back when
 * playing backwards). */
static inline u32 en_anim_event(u32 e, u32 part, u32 k) {
    u32 p = EN_ANIMPART(e, part);
    u32 frames = GV(u32, EN_ANIMDATA(e) + 8) + 4;
    u32 w;
    if (GV(s8, p + 0xD) >= 0) w = GV(u32, frames + (u32)GV(u16, p + 0x16) * 4 + k * 4);
    else w = GV(u32, frames + (u32)GV(u16, p + 0x14) * 4 - k * 4);
    return w >> 31;
}

/* Rise by dy: the model's height (+0x8BC +0xB0) and y; no vertical
 * movement this frame. */
static inline void en_lift(u32 e, u32 dy) {
    u32 m = GV(u32, e + 0x8BC);
    GV(u32, m + 0xB0) -= dy;
    EN_VY(e) = 0;
    GV(u32, e + 0x19C) -= dy;
}

/* 1 unless e and the player face each other: their yaws lo..lo+width-1
 * apart. */
static inline u32 en_not_facing(u32 e, u32 lo, u32 width) {
    s32 d = (s32)GV(s16, g_player + 0x46) - (s32)EN_YAW(e);
    if (d < 0) d = -d;
    return !((((u32)d - lo) & 0xFFFF) < width);
}

/* The point k x 1000 ahead of e (F_ROT_STEP by -100, x 10 k) into the
 * stack vector v; is there a wall between (crossed_fn (from, to): the
 * program's wall test)? */
static inline u32 en_wall_ahead(u32 v, u32 e, u32 k, u32 crossed_fn) {
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    u32 x = MEM_R32(v + 0) * 10 * k, y = MEM_R32(v + 4) * 10 * k, z = MEM_R32(v + 8) * 10 * k;
    MEM_W32(v + 0, x + (u32)EN_X(e));
    MEM_W32(v + 4, y + (u32)EN_Y(e));
    MEM_W32(v + 8, z + (u32)EN_Z(e));
    return gcall(crossed_fn, EN_POS(e), v, 0, 0);
}

/* The same from the player along their facing. */
static inline s32 en_player_wall_ahead(u32 v, u32 k, u32 crossed_fn) {
    gcall(F_ROT_STEP, PLAYER_ROT_ADDR, v, (u32)-100, 0);
    u32 x = MEM_R32(v + 0) * 10 * k, y = MEM_R32(v + 4) * 10 * k, z = MEM_R32(v + 8) * 10 * k;
    MEM_W32(v + 0, x + GV(u32, PLAYER_POS_ADDR));
    MEM_W32(v + 4, y + GV(u32, PLAYER_POS_ADDR + 4));
    MEM_W32(v + 8, z + GV(u32, PLAYER_POS_ADDR + 8));
    return (s32)gcall(crossed_fn, PLAYER_POS_ADDR, v, 0, 0);
}

/* Walls beside the player, k x 1000 out: bit 1 on their right (yaw +
 * 0x400), bit 0 on their left (yaw + 0xC00); v a stack vector. */
static inline u32 en_player_walls(u32 v, u32 k, u32 crossed_fn) {
    u16 yaw = GV(u16, g_player + 0x46);
    GV(u16, g_player + 0x46) = (yaw + 0x400) & 0xFFF;
    s32 r = en_player_wall_ahead(v, k, crossed_fn);
    GV(u16, g_player + 0x46) = (GV(u16, g_player + 0x46) + 0x800) & 0xFFF;
    s32 l = en_player_wall_ahead(v, k, crossed_fn);
    GV(u16, g_player + 0x46) = yaw;
    u32 bits = (u32)(r != -1) << 1;
    if (l != -1) bits |= 1;
    return bits;
}

/* Is the player within yaw +-half of e (the cone's ends F_ROT_STEP by
 * -100: yaw - half first)? The caller's FRAME(0x50) holds the rotation
 * (sp + 0x30) and the ends (sp + 0x20, sp + 0x10). */
static inline u32 en_player_in_cone_r(u32 sp, u32 e, u32 half) {
    u32 rot = sp + 0x30, left = sp + 0x20, right = sp + 0x10;
    GS(u16, rot + 4) = 0;
    GS(u16, rot + 0) = 0;
    GS(u16, rot + 2) = (u16)(GV(u16, e + 0x1C6) - half);
    gcall(F_ROT_STEP, rot, left, (u32)-100, 0);
    MEM_W32(left, MEM_R32(left) + (u32)EN_X(e));
    MEM_W32(left + 8, MEM_R32(left + 8) + (u32)EN_Z(e));
    GS(u16, rot + 2) = (u16)(GS(u16, rot + 2) + ((u32)(s32)(s16)half << 1));
    gcall(F_ROT_STEP, rot, right, (u32)-100, 0);
    MEM_W32(right, MEM_R32(right) + (u32)EN_X(e));
    MEM_W32(right + 8, MEM_R32(right + 8) + (u32)EN_Z(e));
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), left, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) <= 0) return 0;
    return (s32)gcall(FN_LINE_SIDE, EN_POS(e), right, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) < 0;
}

/* Unless the target is already within +-0x71 of its facing (cone_fn
 * (e, half): the program's en_player_in_cone_r), turn e by step towards
 * it: the player (dist 0), or the point dist x 1000 in front of them.
 * Returns the turn made (0: none). The caller's FRAME(0x48) holds the
 * target (sp + 0x10) and e's facing point (sp + 0x20). */
static inline u32 en_turn_to(u32 sp, u32 e, u32 dist, u32 step, u32 cone_fn) {
    u32 t = sp + 0x10;
    if (dist == 0) {
        MEM_W32(t + 0, GV(u32, PLAYER_POS_ADDR));
        MEM_W32(t + 4, GV(u32, PLAYER_POS_ADDR + 4));
        MEM_W32(t + 8, GV(u32, PLAYER_POS_ADDR + 8));
    } else {
        gcall(F_ROT_STEP, PLAYER_ROT_ADDR, t, (u32)-100, 0);
        u32 x = MEM_R32(t + 0) * 10 * dist, z = MEM_R32(t + 8) * 10 * dist;
        MEM_W32(t + 0, x + GV(u32, PLAYER_POS_ADDR));
        MEM_W32(t + 8, z + GV(u32, PLAYER_POS_ADDR + 8));
    }
    if (gcall(cone_fn, e, 0x71, 0, 0)) return 0;
    u32 v = sp + 0x20;
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v, MEM_R32(v) + (u32)EN_X(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    u32 turn = step;
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), v, MEM_R32(t), MEM_R32(t + 8)) < 0) turn = 0u - step;
    GV(u16, e + 0x1C6) = (u16)((GV(u16, e + 0x1C6) + turn) & 0xFFF);
    return (u32)(s32)(s16)turn;
}

/* Steer e round the wall within k x 1000 ahead (ahead_fn (e, k): the
 * program's wall-ahead test, dist_fn (e, wall): its distance to a wall),
 * by turn a frame (0: 51..150 at random, the BIOS rand() times the work
 * block's frame counter at +0x20): pick the way (work block +1: 1 left
 * when there is a wall both sides or on the right, 2 right; at random, or
 * straight on (0) when the wall is far) and keep turning that way.
 * Returns the wall ahead (-1: none, and stop steering). */
static inline s32 en_wander(u32 e, u32 k, u32 turn, u32 w, u32 ahead_fn, u32 dist_fn) {
    if ((s16)turn == 0) {
        u32 r = gcall(0x800398A8u, 0, 0, 0, 0) * (u32)GV(u16, w + 0x20);
        turn = (u32)((s32)r % 100 + 0x33);
    }
    s32 ahead = (s32)gcall(ahead_fn, e, k, 0, 0);
    if (ahead == -1) { GV(u8, w + 1) = 0; return ahead; }
    if (GV(u8, w + 1) == 0) {
        u16 yaw = GV(u16, e + 0x1C6);
        GV(u16, e + 0x1C6) = (yaw + 0x400) & 0xFFF;
        s32 l = (s32)gcall(ahead_fn, e, k, 0, 0);
        GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) + 0x800) & 0xFFF;
        s32 r = (s32)gcall(ahead_fn, e, k, 0, 0);
        if (ahead == l) GV(u8, w + 1) = 1;
        else if (ahead == r) GV(u8, w + 1) = 2;
        else if (l == -1) {
            if (r == l) GV(u8, w + 1) = (GV(u8, w + 0x20) & 1) + 1;
            else GV(u8, w + 1) = 2;
        } else if (r != -1) {
            if ((s32)gcall(dist_fn, e, (u32)ahead, 0, 0) < 0x3E8) GV(u8, w + 1) = (GV(u8, w + 0x20) & 1) + 1;
            else GV(u8, w + 1) = 0;
        } else GV(u8, w + 1) = 1;
        GV(u16, e + 0x1C6) = yaw;
    }
    if (GV(u8, w + 1) == 1) GV(u16, e + 0x1C6) -= turn;
    else if (GV(u8, w + 1) == 2) GV(u16, e + 0x1C6) += turn;
    GV(u16, e + 0x1C6) &= 0xFFF;
    return ahead;
}

#endif
