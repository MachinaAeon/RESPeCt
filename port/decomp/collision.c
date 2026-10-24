/* Wall collision and 2D geometry helpers (TEST overlay).
 *
 * The room's collision data is described by a header at 0x800B60C8:
 *   +0x00 u8   number of walls          +0x04 walls (16 bytes each)
 *   +0x18 u8   number of extra lines    +0x1C lines (20 bytes each)
 * A wall or line is a segment on the floor plane: s32 x0, z0, x1, z1
 * (the extra lines have 4 more bytes the collision code doesn't read).
 *
 * move_Collide (pos, step, flag) moves a position by a step: the start
 * goes to 0x800B6098, the wanted end to 0x800B60A8. Every wall the move
 * crosses pushes the end back out, one unit at a time diagonally, until
 * it is on the wall's open side; the corrected end is then also written
 * into the caller's step (x and z). The number of walls that pushed is
 * counted in 0x800B60BC and returned (0 = the move was clear, so the
 * caller adds the step; otherwise step x/z hold the new position).
 * With flag set, the extra lines are checked too.
 *
 * Points passed in by other code are 3- or 4-word vectors (x, y, z[, pad]);
 * the "floor" functions use x (+0) and z (+8). Callers may keep them on a
 * stack in the scratchpad, so they're accessed with MEM_R32/MEM_W32.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "collision.h"

void n_geom_Distance(void);
void n_geom_Cross2D(void);
void n_geom_PointLineDist(void);
void n_geom_SnapToAxisLine(void);
void n_geom_SideOfLine(void);
void n_geom_SegmentsCross(void);
void n_geom_SegmentsCrossXZ(void);
void n_wall_PushOut(void);
void n_wall_CheckExtraLines(void);
void n_wall_CheckWalls(void);
void n_move_Collide(void);
void n_wall_FirstCrossing(void);
void n_line_Blocks(void);
void n_line_FirstCrossing(void);

#define F_ISQRT          0x8002A3D8u   /* (n): integer square root */

#define COL_START        0x800B6098u   /* start of the move (x, y, z, pad) */
#define COL_END          0x800B60A8u   /* wanted end, corrected by walls */
#define g_colStepAddr    GV(u32, 0x800B60B8)   /* caller's step vector (address) */
#define g_colHits        GV(s32, 0x800B60BC)   /* walls that pushed this move */
#define g_colCheckExtra  GV(s32, 0x800B60C0)   /* move_Collide's flag */
#define COL_HEADER       0x800B60C8u
#define COL_WALL_COUNT   GV(u8, COL_HEADER)
#define COL_WALLS        GV(u32, COL_HEADER + 0x04)
#define COL_EXTRA_COUNT  GV(u8, COL_HEADER + 0x18)
#define COL_EXTRA        GV(u32, COL_HEADER + 0x1C)

#define NEAR_Z           60000         /* walls further away in z are skipped */

/* Wrapping 32-bit arithmetic as the MIPS does it. */
static inline s32 sub32(s32 a, s32 b) { return (s32)((u32)a - (u32)b); }
static inline s32 mul32(s32 a, s32 b) { return (s32)((u32)a * (u32)b); }
static inline s32 abs32(s32 a) { return a < 0 ? (s32)(0u - (u32)a) : a; }
static inline s32 rd(u32 addr) { return (s32)MEM_R32(addr); }
static inline void wr(u32 addr, s32 v) { MEM_W32(addr, (u32)v); }

/* 8007BDDC (a, b): distance between two points (x, y, z). */
void n_geom_Distance(void) {
    FRAME(0x18);
    u32 a = A0, b = A1;
    s32 dx = sub32(rd(a), rd(b)), dy = sub32(rd(a + 4), rd(b + 4)), dz = sub32(rd(a + 8), rd(b + 8));
    GCALL1(F_ISQRT, (u32)mul32(dx, dx) + (u32)mul32(dy, dy) + (u32)mul32(dz, dz));
}

/* 8007C084 (a, b, c): cross(b - a, c - a) of (x, y) pairs of words. */
void n_geom_Cross2D(void) {
    u32 a = A0, b = A1, c = A2;
    s32 ax = rd(a), ay = rd(a + 4);
    V0 = (u32)sub32(mul32(sub32(rd(b + 4), ay), sub32(rd(c), ax)), mul32(sub32(rd(b), ax), sub32(rd(c + 4), ay)));
}

/* 8007BE48 (a, b, p): distance of p from the line through a and b on the
 * floor plane: |cross(b - a, p - a)| / |b - a|. */
void n_geom_PointLineDist(void) {
    FRAME(0x28);
    u32 a = A0, b = A1, p = A2;
    s32 ax = rd(a), az = rd(a + 8);
    s32 dx = sub32(rd(b), ax), dz = sub32(rd(b + 8), az);
    s32 px = sub32(rd(p), ax), pz = sub32(rd(p + 8), az);
    s32 len = (s32)GCALL1(F_ISQRT, (u32)mul32(dx, dx) + (u32)mul32(dz, dz));
    s32 cross = abs32(sub32(mul32(dz, px), mul32(dx, pz)));
    V0 = (u32)mips_div(cross, len);
}

/* 8007BFF0 (a, b, &x, &z): if the line a-b runs along an axis, put the
 * point on it: a vertical line (same x) sets x to a's, a horizontal one
 * (same z) sets z to a's (as 20-bit signed values). A degenerate line
 * (a == b) changes nothing. */
void n_geom_SnapToAxisLine(void) {
    u32 a = A0, b = A1, px = A2, pz = A3;
    s32 ax = rd(a);
    s32 dx = sub32(rd(b), ax);
    s32 dz = sub32(rd(b + 8), rd(a + 8));
    u32 v0 = (u32)rd(a + 8);
    if (dx != 0 || dz != 0) {
        v0 = (u32)ax << 12;
        if (dx == 0) { v0 = (u32)((s32)v0 >> 12); wr(px, (s32)v0); }
        if (dz == 0) { v0 = (u32)((s32)((u32)rd(a + 8) << 12) >> 12); wr(pz, (s32)v0); }
    }
    V0 = v0;
}

/* 8007C04C (a, b, x, z): which side of the line a-b the point (x, z) is
 * on: cross(b - a, p - a) on the floor plane (0 = on the line). */
static s32 geom_Side(s32 ax, s32 az, s32 bx, s32 bz, s32 x, s32 z) {
    return sub32(mul32(sub32(bz, az), sub32(x, ax)), mul32(sub32(bx, ax), sub32(z, az)));
}
void n_geom_SideOfLine(void) {
    u32 a = A0, b = A1;
    V0 = (u32)geom_Side(rd(a), rd(a + 8), rd(b), rd(b + 8), (s32)A2, (s32)A3);
}

/* Do the segments p1-p2 and p3-p4 cross? The coordinates are words at +0
 * and +zo. Touching counts when the touching end of one segment is
 * strictly between the sides of the other. */
static u32 segments_cross(u32 p1, u32 p2, u32 p3, u32 p4, u32 zo) {
    s32 x1 = rd(p1), y1 = rd(p1 + zo), x2 = rd(p2), y2 = rd(p2 + zo);
    s32 x3 = rd(p3), y3 = rd(p3 + zo), x4 = rd(p4), y4 = rd(p4 + zo);
    s32 ey = sub32(y2, y1), ex = sub32(x2, x1);
    s32 d1 = sub32(mul32(ey, sub32(x3, x1)), mul32(ex, sub32(y3, y1)));   /* p3 against p1-p2 */
    s32 d2 = sub32(mul32(ey, sub32(x4, x1)), mul32(ex, sub32(y4, y1)));   /* p4 */
    ey = sub32(y4, y3); ex = sub32(x4, x3);
    s32 d3 = sub32(mul32(ey, sub32(x1, x3)), mul32(ex, sub32(y1, y3)));   /* p1 against p3-p4 */
    s32 d4 = sub32(mul32(ey, sub32(x2, x3)), mul32(ex, sub32(y2, y3)));   /* p2 */
    int split12 = (d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0);
    int split34 = (d3 > 0 && d4 < 0) || (d3 < 0 && d4 > 0);
    int touch12 = d1 == 0 || d2 == 0;
    int touch34 = d3 == 0 || d4 == 0;
    return (split12 && (split34 || touch34)) || (touch12 && split34);
}

/* 8007C0D4 (p1, p2, p3, p4): 1 if the segments p1-p2 and p3-p4 cross.
 * Points here are (x, y) pairs of words. */
void n_geom_SegmentsCross(void) {
    V0 = segments_cross(A0, A1, A2, A3, 4);
}

/* 8007C290 (p1, p2, p3, p4): the same on the floor plane: points are
 * vectors, x at +0 and z at +8. */
void n_geom_SegmentsCrossXZ(void) {
    V0 = segments_cross(A0, A1, A2, A3, 8);
}

/* 8007C60C (wall, start, end): push the move's end out of a wall: step it
 * diagonally, one unit at a time, away from the wall until it's no longer
 * on the wall's closed side; it goes to end and the caller's step (x, z).
 * Returns the walls that pushed so far. The wall's ends and the end point
 * are copied to the stack (as the original does) for geom_SnapToAxisLine:
 * on a line along an axis the end goes straight onto it. */
void n_wall_PushOut(void) {
    FRAME(0x48);
    u32 wall = A0, end = A2;
    const u32 la = sp + 0x10, lb = sp + 0x20, px = sp + 0x30, pz = sp + 0x34;
    GS(s32, la) = rd(wall); GS(s32, la + 4) = 0; GS(s32, la + 8) = rd(wall + 4);
    GS(s32, lb) = rd(wall + 8); GS(s32, lb + 4) = 0; GS(s32, lb + 8) = rd(wall + 12);
    GS(s32, px) = rd(end);
    GS(s32, pz) = rd(end + 8);
    A0 = la; A1 = lb; A2 = px; A3 = pz;
    n_geom_SnapToAxisLine();
    s32 dxl = sub32(rd(wall + 8), rd(wall)), dzl = sub32(rd(wall + 12), rd(wall + 4));
    /* direction: bit 1 = wall runs towards -x, bit 0 = towards -z */
    int dir = (dxl < 0 ? 2 : 0) | (dzl < 0 ? 1 : 0);
    for (;;) {
        s32 x = GS(s32, px), z = GS(s32, pz);
        if (sub32(mul32(dzl, sub32(x, rd(wall))), mul32(dxl, sub32(z, rd(wall + 4)))) <= 0) break;
        GS(s32, la) = sub32(x, rd(end));             /* (dead stores, kept) */
        GS(s32, la + 8) = sub32(z, rd(end + 8));
        switch (dir) {
        case 0: x = sub32(x, 1); z = sub32(z, -1); break;
        case 1: x = sub32(x, -1); z = sub32(z, -1); break;
        case 2: x = sub32(x, 1); z = sub32(z, 1); break;
        case 3: x = sub32(x, -1); z = sub32(z, 1); break;
        }
        GS(s32, px) = x;
        GS(s32, pz) = z;
    }
    s32 x = GS(s32, px), z = GS(s32, pz);
    u32 step = g_colStepAddr;
    s32 hits = g_colHits;
    wr(end + 4, 0);
    wr(end, x);
    wr(end + 8, z);
    wr(step, x);
    wr(step + 8, z);
    g_colHits = hits + 1;
    V0 = (u32)(hits + 1);
}

static void push_out(u32 wall, u32 start, u32 end) {
    A0 = wall; A1 = start; A2 = end;
    n_wall_PushOut();
}

/* 8007C7A0: check the end of the move against the extra lines (the
 * 20-byte list): push it out of each one the move crosses. */
void n_wall_CheckExtraLines(void) {
    FRAME(0x30);
    u32 n = COL_EXTRA_COUNT;
    u32 line = COL_EXTRA;
    for (u32 i = 0; i < n; i++, line += 0x14) {
        s32 x0 = rd(line), z0 = rd(line + 4), x1 = rd(line + 8), z1 = rd(line + 12);
        s32 tz = GV(s32, COL_END + 8);
        s32 dz0 = sub32(tz, z0);
        if (abs32(dz0) > NEAR_Z) continue;
        s32 sx = GV(s32, COL_START), sz = GV(s32, COL_START + 8);
        s32 tx = GV(s32, COL_END);
        /* the line's ends against the move */
        s32 m0 = sub32(mul32(sub32(tz, sz), sub32(x0, sx)), mul32(sub32(tx, sx), sub32(z0, sz)));
        s32 m1 = sub32(mul32(sub32(tz, sz), sub32(x1, sx)), mul32(sub32(tx, sx), sub32(z1, sz)));
        /* the move's ends against the line */
        s32 lx = sub32(x1, x0), lz = sub32(z1, z0);
        s32 e = sub32(mul32(lz, sub32(tx, x0)), mul32(lx, dz0));
        if (e < 0) continue;
        s32 s = sub32(mul32(lz, sub32(sx, x0)), mul32(lx, sub32(sz, z0)));
        int ends_apart = (m0 < 0) != (m1 < 0) || (m0 >= 0 && m1 >= 0 && (m0 == 0 || m1 == 0));
        if (!ends_apart) continue;
        if (s > 0 && e > 0) continue;          /* both ends on the open side */
        if ((u32)m0 + (u32)m1 + (u32)e + (u32)s == 0) continue;
        push_out(line, COL_START, COL_END);
    }
}

/* 8007C98C: check the move against the room's walls. After a wall pushes,
 * the search starts again a few walls back (or from the second wall), the
 * wall just handled excepted; it stops after three pushes. Then the extra
 * lines, if move_Collide's flag asked for them. */
void n_wall_CheckWalls(void) {
    FRAME(0x30);
    g_colHits = 0;
    s32 n = COL_WALL_COUNT;
    u32 walls = COL_WALLS;
    u32 wall = walls;
    s32 last = -1;
    for (s32 i = 0; i < n; i++, wall += 0x10) {
        s32 tz = GV(s32, COL_END + 8);
        s32 z0 = rd(wall + 4);
        if (abs32(sub32(tz, z0)) > NEAR_Z) continue;
        if (i == last) continue;
        s32 sx = GV(s32, COL_START), sz = GV(s32, COL_START + 8);
        s32 tx = GV(s32, COL_END);
        s32 x0 = rd(wall);
        s32 mz = sub32(tz, sz), mx = sub32(tx, sx);
        s32 lx = sub32(rd(wall + 8), x0), lz = sub32(rd(wall + 12), z0);
        s32 den = sub32(mul32(mz, lx), mul32(mx, lz));
        if (den != 0) {
            /* intersection parameters along the move and along the wall */
            s32 u = sub32(mul32(sub32(z0, sz), mx), mul32(sub32(x0, sx), mz));
            s32 t = sub32(mul32(sub32(z0, sz), lx), mul32(sub32(x0, sx), lz));
            if (!(abs32(u) < abs32(den))) continue;
            if ((u > 0 ? den : (s32)(0u - (u32)den)) < 0) continue;
            if (!(abs32(t) < abs32(den))) continue;
            if ((t > 0 ? den : (s32)(0u - (u32)den)) < 0) continue;
            push_out(wall, COL_START, COL_END);
            last = i;
            if (i < 5) { i = 0; wall = walls; }
            else { i -= 4; wall -= 0x40; }
        }
        if (g_colHits >= 3) return;
    }
    if (g_colCheckExtra != 0) n_wall_CheckExtraLines();
}

/* 8007CB9C move_Collide (pos, step, flag): see the top of the file. The
 * wall checks run on a stack at the top of the scratchpad, unless the
 * stack is already there (a scratchpad address is positive as an s32). */
void n_move_Collide(void) {
    FRAME(0x20);
    u32 pos = A0, step = A1;
    g_colCheckExtra = (s32)A2;
    for (int k = 0; k < 4; k++) GV(s32, COL_START + k * 4) = rd(pos + k * 4);
    for (int k = 0; k < 3; k++) GV(s32, COL_END + k * 4) = (s32)((u32)rd(pos + k * 4) + (u32)rd(step + k * 4));
    g_colStepAddr = step;
    u32 old_sp = R[29];
    int switched = (s32)old_sp < 0;
    if (switched) R[29] = 0x1F800400u;
    n_wall_CheckWalls();
    if (switched) R[29] = old_sp;
    V0 = (u32)g_colHits;
}

static inline s32 sign_pm(s32 v) { return v > 0 ? 1 : -1; }

static s32 wall_FirstCrossing(s32 px, s32 pz, s32 qx, s32 qz) {
    s32 n = COL_WALL_COUNT;
    u32 wall = COL_WALLS;
    for (s32 i = 0; i < n; i++, wall += 0x10) {
        s32 x0 = rd(wall), z0 = rd(wall + 4), x1 = rd(wall + 8), z1 = rd(wall + 12);
        s32 sp = geom_Side(x0, z0, x1, z1, px, pz);    /* p against the wall */
        s32 sq = geom_Side(x0, z0, x1, z1, qx, qz);    /* q */
        s32 s0 = geom_Side(px, pz, qx, qz, x0, z0);    /* the wall's ends against p-q */
        s32 s1 = geom_Side(px, pz, qx, qz, x1, z1);
        if (sign_pm(sp) * sign_pm(sq) < 0 && sign_pm(s0) * sign_pm(s1) < 0 && sp <= 0)
            return i;
    }
    return -1;
}

/* 8007F8F0 (p, q): the first wall the segment p-q crosses with p on its
 * closed side (or exactly on it), -1 if none: is there a wall between an
 * enemy and the player. */
void n_wall_FirstCrossing(void) {
    FRAME(0x40);
    V0 = (u32)wall_FirstCrossing(rd(A0), rd(A0 + 8), rd(A1), rd(A1 + 8));
}

/* 800822BC (p, q, line): whether a crossed line blocks: always. */
void n_line_Blocks(void) { V0 = 1; }

/* 800822C4 (p, q): the first line of the 20-byte list at 0x800B60D4
 * (count at 0x800B60D0; x0, z0, x1, z1, height) that the segment p-q
 * crosses below q's height (y), -1 if none. The line's ends are copied to
 * the stack as the original does. */
void n_line_FirstCrossing(void) {
    FRAME(0x60);
    u32 p = A0, q = A1;
    s32 n = GV(u8, COL_HEADER + 0x8);
    u32 line = GV(u32, COL_HEADER + 0xC);
    for (s32 i = 0; i < n; i++, line += 0x14) {
        s32 x0 = GV(s32, line), z0 = GV(s32, line + 4), x1 = GV(s32, line + 8), z1 = GV(s32, line + 12);
        GS(s32, sp + 0x10) = x0; GS(s32, sp + 0x18) = z0;
        GS(s32, sp + 0x20) = x1; GS(s32, sp + 0x28) = z1;
        s32 px = rd(p), pz = rd(p + 8), qx = rd(q), qz = rd(q + 8);
        s32 sp_ = geom_Side(x0, z0, x1, z1, px, pz);    /* p against the line */
        s32 sq = geom_Side(x0, z0, x1, z1, qx, qz);     /* q */
        s32 a = mul32(sub32(qz, pz), sub32(x0, px));
        GS(s32, sp + 0x30) = a;
        s32 s0 = sub32(a, mul32(sub32(qx, px), sub32(z0, pz)));   /* the line's ends against p-q */
        s32 s1 = geom_Side(px, pz, qx, qz, x1, z1);
        if (sign_pm(sp_) * sign_pm(sq) < 0 && sign_pm(s0) * sign_pm(s1) < 0 &&
            GV(s32, line + 0x10) < rd(q + 4)) {
            if (GCALL3(0x800822BCu, p, q, line) != 0) { V0 = (u32)i; return; }
        }
    }
    V0 = 0xFFFFFFFFu;
}
