/* The giant cockroach (enemy program ENEMPROG 54, loaded in enemy slot B
 * at 0x80100000). Identified by what it does and how it looks in the
 * area scenario that loads it (area10: the sewer, stage 3 room 13, ten of
 * them): it scuttles about the floor, climbs the walls (the room's area
 * edges, 0x800B60D0) and across the ceiling, drops onto the player and
 * clings to them (grabs), and leaps.
 *
 * cockroach_Update (0x801026A8) runs every frame for each one: it picks
 * the work block, counts animation frames, runs the state's handler
 * (0x80102E0C; state 1, cockroach_StateActive, runs the action's handler
 * from 0x80102DF8: 0 idle, 1 crawl, 2 crouch, 3 leap, 4 die), moves it
 * by the step in the work block and takes a shot (one kills it).
 *
 * Where it is (CR_MODE): 1 on the floor, 2 climbing a wall, 3 coming down
 * one, 4 on the ceiling, 5-7 leaping (from the floor, the ceiling, a
 * wall), 8 clinging to the player, 9 falling dead.
 *
 * Each one has a 0x74-byte work block (0x80102E20 + slot * 0x74; the
 * current one's address is kept at 0x80103478, see CR_*). Animations are
 * started like the Cerberus' (en_start_anim).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"

void n_cockroach_CrossedEdge(void);
void n_cockroach_OntoWall(void);
void n_cockroach_Steer(void);
void n_cockroach_CeilingStep(void);
void n_cockroach_FloorStep(void);
void n_cockroach_ClimbStep(void);
void n_cockroach_DescendStep(void);
void n_cockroach_LeapFloor(void);
void n_cockroach_LeapCeiling(void);
void n_cockroach_ClingStep(void);
void n_cockroach_FallDead(void);
void n_cockroach_IdleStep(void);
void n_cockroach_CrawlStep(void);
void n_cockroach_CrouchStep(void);
void n_cockroach_DieStep(void);
void n_cockroach_Update(void);
void n_cockroach_LeapStep(void);
void n_cockroach_StateActive(void);

/* ---- the work block ---- */
#define CR               GV(u32, 0x80103478)
#define CR_STARTED       GV(s8, CR + 0x00)
#define CR_TURN          GV(u8, CR + 0x01)    /* wandering: 0 left, 1 right */
#define CR_MODE          GV(u8, CR + 0x02)    /* where it is (see the top) */
#define CR_ACTIVE        GV(u8, CR + 0x03)    /* may move off the spot */
#define CR_SPIN          GV(u8, CR + 0x04)
#define CR_EDGE          GV(u8, CR + 0x06)    /* the wall it is on */
#define CR_MAY_LEAP      GV(u8, CR + 0x07)
#define CR_PLACED        GV(u8, CR + 0x08)
#define CR_STEPS         GV(u8, CR + 0x09)    /* crawl steps (sound every 10) */
#define CR_TIME          GV(s16, CR + 0x0A)   /* timer, or vertical speed */
#define CR_TIME2         GV(s16, CR + 0x0C)
#define CR_FRAME         GV(s16, CR + 0x0E)   /* animation frame counter */
#define CR_PREV_FRAME    GV(s16, CR + 0x10)
#define CR_ANIM_FRAME    GV(s16, CR + 0x12)   /* last frame's animation frame */
#define CR_STEP_VEC      (CR + 0x14)          /* x, y, z added to its position */
#define COCKROACH_WORK_BASE 0x80102E20u
#define COCKROACH_WORK_SIZE 0x74

#define COCKROACH_ACTIONS 0x80102DF8u
#define COCKROACH_STATES  0x80102E0Cu
#define g_gameMode       GV(s32, 0x800BB19C)
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_C_LEAP_WALL   0x80101744u   /* (e): mode 7, not decompiled */
#define ANIM2(e)         GV(u32, (e) + 0x1E0)   /* a second animation player: +0xE last, +0x14 frame */

static u32 cockroach_call(void (*f)(void), u32 a0, u32 a1, u32 a2) {
    A0 = a0; A1 = a1; A2 = a2;
    f();
    return V0;
}

/* Walk on: the step for this frame (CR_STEP_VEC) at `speed`. */
static void cockroach_Move(u32 e, s32 speed) {
    gcall(F_ROT_STEP, EN_ROT(e), CR_STEP_VEC, (u32)speed, 0);
}

/* Has the second animation player finished (frame == last)? */
static int cockroach_AnimDone(u32 e) {
    u32 a = ANIM2(e);
    return GV(u16, a + 0x14) == GV(u16, a + 0xE);
}

/* Sink (or rise) by dy: its height and its shadow object's (+0x8BC
 * +0xB0), no vertical speed. */
static void cockroach_Lift(u32 e, s32 dy) {
    u32 o = GV(u32, e + 0x8BC);
    GV(s32, o + 0xB0) += dy;
    GV(u32, e + 0x880) = 0;
    EN_Y(e) += dy;
}

/* Fall 0x50; returns 1 once on the floor (all heights 0). */
static int cockroach_Fall(u32 e) {
    u32 o = GV(u32, e + 0x8BC);
    GV(s32, o + 0xB0) += 0x50;
    u32 o2 = GV(u32, e + 0x8BC);
    GV(u32, e + 0x880) = 0;
    EN_Y(e) += 0x50;
    if (GV(s32, o2 + 0xB0) < 0) return 0;
    EN_Y(e) = 0;
    GV(s32, o2 + 0xB0) = 0;
    GV(u32, e + 0x880) = 0;
    return 1;
}

/* Far from the player (5000 and more) and allowed: now and then (60%)
 * when it sees the player, go for the ceiling (mode 4). */
static int cockroach_TryCeiling(u32 e) {
    if (CR_MAY_LEAP == 0) return 0;
    if ((u32)EN_DIST(e) < 0x1388) return 0;
    if ((s32)en_rand() % 100 < 0x28) return 0;
    if (!(EN_FLAGS(e) & EF_SEES)) return 0;
    CR_MODE = 4;
    EN_STEP(e) = 0;
    return 1;
}

/* The jump's vertical speed (CR_TIME) to rise by h over the distance
 * to the player (one step per 80), for at most 500 frames (CR_TIME2). */
static void cockroach_JumpSpeed(u32 e, s32 h) {
    u32 d = (u32)EN_DIST(e) / 80;
    CR_TIME2 = 0x1F4;
    CR_TIME = (s16)-mips_div(h, (s32)d);
}

/* ------------------------------------------------------------------ */

/* 801000AC (from, to): the first area edge (0x800B60D0) the path crosses
 * from its front, or -1. */
void n_cockroach_CrossedEdge(void) {
    FRAME(0x40);
    V0 = (u32)en_crossed_segment(A0, A1, GV(u32, EP_EDGES_N + 4), GV(u8, EP_EDGES_N), 0x14, 1);
}

/* 80100254 (e, k): onto the wall of edge k's side k (0-3): against it
 * (0x14 from its x or z) and turned up it, the wall's way of climbing
 * (CR_MODE 2: going up, else down). */
void n_cockroach_OntoWall(void) {
    u32 e = A0, k = A1;
    u32 edge = GV(u32, 0x800B60D4) + k * 20;
    u32 w = CR;
    u16 rx = CR_MODE == 2 ? 0xC00 : 0x400;
    switch (k) {
    case 0:
        GV(u32, w + 0x1C) = 0;
        GV(u16, e + 0x1C4) = rx;
        GV(u16, e + 0x1C6) = CR_MODE == 2 ? 0x800 : 0;
        GV(u16, e + 0x1C8) = 0;
        EN_Z(e) = GV(s32, edge + 4) - 0x14;
        break;
    case 1:
        GV(u32, w + 0x14) = 0;
        GV(u16, e + 0x1C4) = rx;
        GV(u16, e + 0x1C6) = CR_MODE == 2 ? 0x400 : 0xC00;
        GV(u16, e + 0x1C8) = 0;
        EN_X(e) = GV(s32, edge + 0) + 0x14;
        break;
    case 2:
        GV(u32, w + 0x1C) = 0;
        GV(u16, e + 0x1C4) = rx;
        GV(u16, e + 0x1C6) = CR_MODE == 2 ? 0 : 0x800;
        GV(u16, e + 0x1C8) = 0;
        EN_Z(e) = GV(s32, edge + 4) + 0x14;
        break;
    case 3:
        GV(u32, w + 0x14) = 0;
        GV(u16, e + 0x1C4) = rx;
        GV(u16, e + 0x1C6) = rx;
        GV(u16, e + 0x1C8) = 0;
        EN_X(e) = GV(s32, edge + 0) - 0x14;
        break;
    }
}

/* 801003FC (e, lead, turn): unless the player is in its cone (yaw
 * +-0x71), turn by `turn` towards the player (lead != 0: the point
 * lead x 10 ahead of the player); returns the turn (0 if in the cone). */
void n_cockroach_Steer(void) {
    FRAME(0x70);
    u32 e = A0, lead = A1, turn = A2;
    u32 t = sp + 0x10;
    if (lead == 0) {
        MEM_W32(t + 0, GV(u32, g_player + 0x18));
        MEM_W32(t + 4, GV(u32, g_player + 0x1C));
        MEM_W32(t + 8, GV(u32, g_player + 0x20));
    } else {
        gcall(F_ROT_STEP, PLAYER_ROT_ADDR, t, (u32)-100, 0);
        MEM_W32(t + 0, MEM_R32(t + 0) * 10 * lead + GV(u32, g_player + 0x18));
        MEM_W32(t + 8, MEM_R32(t + 8) * 10 * lead + GV(u32, g_player + 0x20));
    }
    if (en_point_in_cone(sp + 0x50, sp + 0x40, sp + 0x30, e, -0x71, -100, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z)) {
        V0 = 0;
        return;
    }
    u32 v = sp + 0x20;
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) + (u32)EN_X(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    u32 a = turn;
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), v, MEM_R32(t + 0), MEM_R32(t + 8)) < 0) a = (u32)-(s32)turn;
    GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) + a) & 0xFFF;
    V0 = (u32)(s32)(s16)a;
}

/* 8010060C (e): on the ceiling: wander (turning 0x10 a frame for 10-19
 * frames), turn towards the player, pause; drop (anim 2) when the player
 * is within 6000. */
void n_cockroach_CeilingStep(void) {
    FRAME(0x28);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        CR_TURN = (u8)(en_rand() & 1);
        CR_TIME = (s16)((s32)en_rand() % 10 + 10);
        EN_STEP(e) += 1;
        /* fall through */
    case 1:
        GV(u16, e + 0x1C6) += CR_TURN == 0 ? 0x10 : (u16)-0x10;
        if (CR_TIME > 0) { CR_TIME -= 1; break; }
        CR_TIME = (s16)((s32)en_rand() % 10 + 10);
        EN_STEP(e) += 1;
        /* fall through */
    case 2:
        if (cockroach_call(n_cockroach_Steer, e, 0, 0xA0) != 0) break;
        EN_STEP(e) += 1;
        /* fall through */
    case 3:
        if (CR_TIME > 0) { CR_TIME -= 1; break; }
        CR_TIME = (s16)((s32)en_rand() % 10 + 10);
        EN_STEP(e) = 0;
        break;
    }
    cockroach_Move(e, -0x50);
    if ((u32)EN_DIST(e) < 0x1770) en_start_anim(e, 2, 0x80);
}

/* 801008B8 (e): on the floor: scuttle on (spinning CR_TIME2 frames), up
 * a wall when it runs into one, or now and then for the ceiling; after
 * 45 frames stop (anim 0) or carry on. */
void n_cockroach_FloorStep(void) {
    FRAME(0x48);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0: {
        CR_TIME = 0x2D;
        s32 r = (s32)en_rand();
        CR_TIME2 = (s16)(r % 16 + 16);
        CR_SPIN = (u8)(en_rand() & 1);
        EN_STEP(e) += 1;
        break;
    }
    case 1:
        break;
    default:
        return;
    }
    if (cockroach_TryCeiling(e)) return;
    cockroach_Move(e, -0x50);
    if (CR_TIME2 != 0) {
        GV(u16, e + 0x1C6) += CR_SPIN == 0 ? 0x20 : (u16)-0x20;
        CR_TIME2 -= 1;
    }
    u32 a = sp + 0x18, b = sp + 0x28;
    u32 w = CR;
    MEM_W32(a + 0, (u32)EN_X(e));
    MEM_W32(a + 4, (u32)EN_Y(e));
    MEM_W32(a + 8, (u32)EN_Z(e));
    MEM_W32(b + 0, (u32)EN_X(e) + GV(u32, w + 0x14));
    MEM_W32(b + 4, (u32)EN_Y(e) + GV(u32, w + 0x18));
    MEM_W32(b + 8, (u32)EN_Z(e) + GV(u32, w + 0x1C));
    s32 k = (s32)cockroach_call(n_cockroach_CrossedEdge, a, b, 0);
    if (k != -1) {
        CR_MODE = EN_Y(e) != 0 ? 3 : 2;
        cockroach_call(n_cockroach_OntoWall, e, (u32)k, 0);
        CR_EDGE = (u8)k;
        return;
    }
    if (CR_TIME != 0) { CR_TIME -= 1; return; }
    if ((s32)en_rand() % 100 < 0x14) en_start_anim(e, 0, 0x80);
    else EN_STEP(e) = 0;
}

/* Turned over onto the ceiling or the floor at the top or foot of the
 * wall CR_EDGE: rot.z flip (0x800 on the ceiling) and the yaw by side. */
static void cockroach_OffWall(u32 e, u16 rz) {
    switch (CR_EDGE) {
    case 0: GV(u16, e + 0x1C4) = 0; GV(u16, e + 0x1C6) = 0; GV(u16, e + 0x1C8) = rz; break;
    case 1: GV(u16, e + 0x1C6) = 0xC00; GV(u16, e + 0x1C4) = 0; GV(u16, e + 0x1C8) = rz; break;
    case 2: GV(u16, e + 0x1C4) = 0; GV(u16, e + 0x1C6) = 0x800; GV(u16, e + 0x1C8) = rz; break;
    case 3: GV(u16, e + 0x1C6) = 0x400; GV(u16, e + 0x1C4) = 0; GV(u16, e + 0x1C8) = rz; break;
    }
}

/* High on a wall (y -0xA8C..-0x640): maybe for the ceiling now. */
static int cockroach_WallLeap(u32 e) {
    if ((u32)(EN_Y(e) + 0xA8C) >= 0x44D) return 0;
    return cockroach_TryCeiling(e);
}

/* 80100C24 (e): climbing a wall, 0x50 a frame, onto the ceiling at
 * -0xBB8 (mode 1 there: it walks the ceiling as a floor). */
void n_cockroach_ClimbStep(void) {
    FRAME(0x20);
    u32 e = A0;
    if (cockroach_WallLeap(e)) return;
    cockroach_Lift(e, -0x50);
    if (EN_Y(e) >= -0xBB8) return;
    u32 o = GV(u32, e + 0x8BC);
    EN_Y(e) = -0xBB8;
    GV(s32, o + 0xB0) = -0xBB8;
    cockroach_OffWall(e, 0x800);
    CR_MODE = 1;
}

/* 80100DE4 (e): coming down a wall, 0x50 a frame, onto the floor. */
void n_cockroach_DescendStep(void) {
    FRAME(0x20);
    u32 e = A0;
    if (cockroach_WallLeap(e)) return;
    if (!cockroach_Fall(e)) return;
    EN_Y(e) = 0;
    GV(s32, GV(u32, e + 0x8BC) + 0xB0) = 0;
    cockroach_OffWall(e, 0);
    CR_MODE = 1;
}

/* Steps 3 and 4 of the leaps: cling to the player when it sees them
 * high up (mode 8), else fall to the floor and walk on (mode 1). */
static void cockroach_LeapEnd(u32 e, int from_step3) {
    if (from_step3) {
        if ((EN_FLAGS(e) & EF_SEES) && EN_Y(e) < -0x577) {
            CR_MODE = 8;
            en_start_anim(e, 1, 0x80);
            return;
        }
        EN_STEP(e) += 1;
    }
    if (!cockroach_Fall(e)) return;
    CR_MODE = 1;
    en_start_anim(e, 1, 0x80);
}

/* The leap's flight: on at the jump speed until near the player (600) or
 * out of time (500 frames: step 4). */
static void cockroach_Flight(u32 e) {
    if ((u32)EN_DIST(e) < 0x259) EN_STEP(e) += 1;
    else cockroach_Move(e, -0x50);
    if (CR_TIME2 <= 0) EN_STEP(e) = 4;
    CR_TIME2 -= 1;
}

/* 80100FBC (e): mode 5, the leap from the floor: crouch, then once facing
 * the player jump up at it (cockroach_JumpSpeed), fly, and land on it or
 * the floor. */
void n_cockroach_LeapFloor(void) {
    FRAME(0x28);
    u32 e = A0;
    u32 s = cockroach_call(n_cockroach_Steer, e, 0, 0x80);
    switch ((u32)(s32)EN_STEP(e)) {
    case 0:
        cockroach_Lift(e, -8);
        if (s != 0) return;
        cockroach_JumpSpeed(e, -0x640 - (s32)en_rand() % 40);
        EN_STEP(e) += 1;
        return;
    case 1:
        cockroach_Move(e, -0x50);
        cockroach_Lift(e, -(s32)CR_TIME);
        if (EN_Y(e) < -0x5DB) EN_STEP(e) += 1;
        /* fall through */
    case 2:
        cockroach_Flight(e);
        return;
    case 3:
        cockroach_LeapEnd(e, 1);
        return;
    case 4:
        cockroach_LeapEnd(e, 0);
        return;
    }
}

/* 80101330 (e): mode 6, the leap from the ceiling: roll right way up
 * (rot.z, 0x80 a frame), then once facing the player push off down at it,
 * fly, and land on it or the floor. */
void n_cockroach_LeapCeiling(void) {
    FRAME(0x28);
    u32 e = A0;
    u32 s = cockroach_call(n_cockroach_Steer, e, 0, 0x80);
    switch ((u32)(s32)EN_STEP(e)) {
    case 0:
        CR_TIME = 0;
        CR_TIME2 = 0;
        EN_STEP(e) += 1;
        /* fall through */
    case 1: {
        u32 o = GV(u32, e + 0x8BC);
        GV(s32, o + 0xB0) += 0x40;
        u32 w = CR;
        EN_Y(e) += 0x40;
        GV(u16, e + 0x1C8) -= 0x80;
        GV(u32, e + 0x880) = 0;
        s16 t = (s16)(GV(u16, w + 0xA) + 0x80);
        GV(s16, w + 0xA) = t;
        if (t < 0x400) return;
        GV(s16, w + 0xA) = 0;
        GV(u16, e + 0x1C8) = 0;
        EN_STEP(e) += 1;
        return;
    }
    case 2:
        if (s != 0) {
            cockroach_Lift(e, 8);
            return;
        }
        cockroach_JumpSpeed(e, (s32)((u32)((s32)en_rand() % 40 - 0x640) - (u32)EN_Y(e)));
        EN_STEP(e) += 1;
        return;
    case 3:
        cockroach_Move(e, -0x50);
        cockroach_Lift(e, -(s32)CR_TIME);
        if (!(EN_Y(e) < -0x5DC)) EN_STEP(e) += 1;
        /* fall through */
    case 4:
        cockroach_Flight(e);
        return;
    case 5:
        cockroach_LeapEnd(e, 1);
        return;
    case 6:
        cockroach_LeapEnd(e, 0);
        return;
    }
}

/* 80101B88 (e): mode 8, on the player: climb (rot.x, 0x80 a frame,
 * coming at it unless within 400), cling while facing the player within
 * 500 and grab them after 20 frames (game mode 1, grabs not locked),
 * then roll off and drop to the floor (mode 1). */
void n_cockroach_ClingStep(void) {
    FRAME(0x20);
    u32 e = A0;
    switch ((u32)(s32)EN_STEP(e)) {
    case 0:
        CR_TIME = 0;
        CR_TIME2 = 0;
        EN_STEP(e) += 1;
        if (ep_grabLock2 != 0) EN_STEP(e) = 10;
        /* fall through */
    case 1: {
        if ((u32)EN_DIST(e) >= 0x191) cockroach_Move(e, -0x8C);
        u32 w = CR;
        GV(u16, e + 0x1C4) -= 0x80;
        s16 t = (s16)(GV(u16, w + 0xA) + 0x80);
        GV(s16, w + 0xA) = t;
        if (t < 0x400) return;
        GV(s16, w + 0xA) = 0;
        GV(s16, w + 0xC) = 0;
        EN_STEP(e) += 1;
        return;
    }
    case 2:
        if ((EN_FLAGS(e) & EF_SEES) && ep_grabLock2 == 0 && (u32)EN_DIST(e) < 0x1F5) {
            s32 d = (s32)GV(s16, 0x800A8606) - (s32)GV(s16, e + 0x1C6);
            if (d < 0) d = -d;
            if ((u16)(d - 0x47D) < 0x707) {
                s16 t = (s16)(CR_TIME2 + 1);
                CR_TIME2 = t;
                if (t < 0x14) return;
                if (g_gameMode == 1 && ep_grabLock == 0) {
                    GCALL3(FN_EN_GRAB_PLAYER, e, 7, 0);
                    GCALL1(FN_EN_GRAB_START, e);
                }
            }
        }
        CR_TIME = 0;
        CR_TIME2 = 0;
        EN_STEP(e) += 1;
        return;
    case 3: {
        u32 w = CR;
        GV(u16, e + 0x1C4) += 0x80;
        s16 t = (s16)(GV(u16, w + 0xA) + 0x80);
        GV(s16, w + 0xA) = t;
        if (t >= 0x400) EN_STEP(e) += 1;
    }
        /* fall through */
    case 4: {
        u32 w = CR;
        if (GV(s16, w + 0xC) == 0) {
            if (!cockroach_Fall(e)) return;
            GCALL2(FN_EN_SET_SPEED, e, 3);
            CR_TIME2 = 1;
            return;
        }
        if (GV(s16, w + 0xA) >= 0x400) {
            CR_MODE = 1;
            en_start_anim(e, 1, 0x80);
            return;
        }
        GV(u16, e + 0x1C4) += 0x80;
        GV(u16, w + 0xA) += 0x80;
        return;
    }
    case 10:
        if (!cockroach_Fall(e)) return;
        GCALL2(FN_EN_SET_SPEED, e, 3);
        CR_MODE = 1;
        en_start_anim(e, 1, 0x80);
        return;
    }
}

/* 80101F84 (e): mode 9, shot: fall, faster and faster, and lie dead
 * (anim 4). */
void n_cockroach_FallDead(void) {
    FRAME(0x18);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        CR_TIME = 0x80;
        EN_STEP(e) += 1;
        /* fall through */
    case 1: {
        s32 dy = -(s32)CR_TIME;
        u32 o = GV(u32, e + 0x8BC);
        GV(s32, o + 0xB0) -= dy;
        u32 o2 = GV(u32, e + 0x8BC);
        GV(u32, e + 0x880) = 0;
        EN_Y(e) -= dy;
        if (GV(s32, o2 + 0xB0) >= 0) {
            EN_Y(e) = 0;
            GV(s32, o2 + 0xB0) = 0;
            GV(u32, e + 0x880) = 0;
            GCALL2(FN_EN_SET_SPEED, e, 3);
            en_start_anim(e, 4, 0x80);
            return;
        }
        CR_TIME = (s16)(GV(u16, CR + 0xA) << 1);
        return;
    }
    }
}

/* ------------------------------------------------------------------ */
/* Actions */

/* 801020A8 (e): action 0, idle: stay put until it may move (CR_ACTIVE),
 * then wait 45 frames and crawl off (anim 1), or (20%) wait again. */
void n_cockroach_IdleStep(void) {
    FRAME(0x18);
    u32 e = A0;
    if (cockroach_AnimDone(e)) return;
    switch (CR_MODE) {
    case 0:
        if (CR_ACTIVE != 0) CR_MODE = 1;
        return;
    case 1:
        switch (EN_STEP(e)) {
        case 0:
            CR_TIME = 0x2D;
            EN_STEP(e) += 1;
            /* fall through */
        case 1:
            if (CR_TIME != 0) { CR_TIME -= 1; return; }
            if ((s32)en_rand() % 100 < 0x14) EN_STEP(e) = 0;
            else en_start_anim(e, 1, 0x80);
            return;
        }
        return;
    }
}

/* 801021FC (e): action 1, crawling: the step sound every 10 frames, and
 * the handler of where it is (floor, wall up or down, ceiling, on the
 * player). */
void n_cockroach_CrawlStep(void) {
    FRAME(0x20);
    u32 e = A0;
    if (cockroach_AnimDone(e)) return;
    if (CR_MODE != 8) {
        if (CR_STEPS % 10 == 0) GCALL2(FN_EN_SET_SPEED, e, 1);
        CR_STEPS += 1;
    }
    switch (CR_MODE) {
    case 1: A0 = e; n_cockroach_FloorStep(); return;
    case 2: A0 = e; n_cockroach_ClimbStep(); return;
    case 3: A0 = e; n_cockroach_DescendStep(); return;
    case 4:
        if (EN_Y(e) == 0 || EN_Y(e) < -0xBB7) { A0 = e; n_cockroach_CeilingStep(); }
        else en_start_anim(e, 2, 0x80);
        return;
    case 8: A0 = e; n_cockroach_ClingStep(); return;
    }
}

/* 80102384 (e): action 2, the crouch before a leap: at the animation's
 * event pick the leap (mode 5 floor, 6 ceiling, 7 wall) and leap (anim
 * 3); shot (mode 9): fall dead. */
void n_cockroach_CrouchStep(void) {
    FRAME(0x18);
    u32 e = A0;
    if (cockroach_AnimDone(e)) return;
    if (CR_MODE == 9) {
        A0 = e;
        n_cockroach_FallDead();
        return;
    }
    if (!en_anim_event1(e)) return;
    s32 y = EN_Y(e);
    CR_MODE = y == 0 ? 5 : y < -0xBB7 ? 6 : 7;
    en_start_anim(e, 3, 0x80);
}

/* 801024BC (e): action 4, dying: at the animation's event stop every
 * part's animation and lie still (+0x320 | 0x10007). */
void n_cockroach_DieStep(void) {
    u32 e = A0;
    if (cockroach_AnimDone(e)) return;
    if (!en_anim_event1(e)) return;
    s32 n = EN_NPARTS(e);
    for (s32 i = 0; i < n; i++) {
        GV(u8, EN_ANIMPART(e, i) + 0xD) = 0;
        GV(u16, EN_ANIMPART(e, i) + 0x10) = 0;
    }
    EN_FLAGS(e) |= 0x10007;
}

/* 801026A8 (e): one cockroach's frame (see the top of the file); in
 * stage 3 room 13 it waits (hidden) for event 0x7A. */
void n_cockroach_Update(void) {
    FRAME(0x20);
    u32 e = A0;
    CR = COCKROACH_WORK_BASE + (u32)EN_SLOT(e) * COCKROACH_WORK_SIZE;
    if (CR_STARTED == 0) CR_STARTED = 1;
    u32 w = CR;
    GV(u32, w + 0x1C) = 0;
    GV(u32, w + 0x18) = 0;
    GV(u32, w + 0x14) = 0;
    if (GV(u8, w + 8) == 0 && GV(s32, 0x800A866C) == 3) {
        if (GV(s32, 0x800B3F08) == 0xD) {
            GV(u8, w + 3) = 1;
            if (GCALL2(FN_FLAG_TEST, ep_eventFlags, 0x7A) != 0) {
                GV(u32, e + 0x170) = 0x1000;
                GV(u32, e + 0x174) = 0x1000;
                GV(u32, e + 0x178) = 0x1000;
                CR_MAY_LEAP = 1;
                CR_PLACED = 1;
            } else {
                GV(u32, e + 0x170) = 0;
                GV(u32, e + 0x174) = 0;
                GV(u32, e + 0x178) = 0;
            }
        } else {
            GV(u8, w + 3) = 1;
        }
    }
    u32 a = ANIM2(e);
    if (GV(u16, a + 0x16) < GV(u16, a + 0x14)) {
        CR_FRAME = 0;
    } else {
        CR_PREV_FRAME = CR_FRAME;
        if ((s32)CR_ANIM_FRAME != (s32)GV(u16, ANIM2(e) + 0x14)) CR_FRAME += 1;
        CR_ANIM_FRAME = (s16)GV(u16, ANIM2(e) + 0x14);
    }
    GCALL1(GV(u32, COCKROACH_STATES + (u32)((s32)EN_STATE(e) << 2)), e);
    w = CR;
    EN_X(e) += GV(s32, w + 0x14);
    EN_Z(e) += GV(s32, w + 0x1C);
    u32 fl = EN_FLAGS(e);
    if (!(fl & EF_SHOT)) return;
    EN_FLAGS(e) = fl & ~0x10u;
    GCALL2(FN_EN_SET_SPEED, e, 2);
    EN_HP(e) = 0;
    en_start_anim(e, EN_Y(e) != 0 ? 2 : 4, 0x80);
    CR_MODE = 9;
    if (EN_HP(e) <= 0) GCALL2(FN_EN_KILLED, e, (s32)EN_ROOMID(e));
    EN_NHITS(e) = -1;
    GCALL1(FN_EN_HIT_PRE, e);
}

/* 8010293C (e): action 3, the leap: the leap sound at its first frame,
 * and the leap of its mode. */
void n_cockroach_LeapStep(void) {
    FRAME(0x18);
    u32 e = A0;
    if (cockroach_AnimDone(e)) return;
    if (CR_FRAME == 1 && CR_PREV_FRAME != 1) GCALL2(FN_EN_SET_SPEED, e, 0);
    switch (CR_MODE) {
    case 5: A0 = e; n_cockroach_LeapFloor(); return;
    case 6: A0 = e; n_cockroach_LeapCeiling(); return;
    case 7: GCALL1(FN_C_LEAP_WALL, e); return;
    }
}

/* 80102A24 (e): state 1: run the action's handler. */
void n_cockroach_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, COCKROACH_ACTIONS + (u32)((s32)EN_ACTION(e) << 2)), e);
}
