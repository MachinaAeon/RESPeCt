/* The Cerberus, the zombie dog (enemy program ENEMPROG 51, loaded in
 * enemy slot B at 0x80100000). Identified by what it does and how it
 * looks in the area scenarios that load it (area11: the dogs in stage 11
 * room 8): it roams, circles the player at a distance, closes in at a
 * run, leaps and bites (grabs the player).
 *
 * cerberus_Update (0x801031FC) runs every frame for each dog: it picks
 * the dog's work block, counts the animation frames, runs the handler of
 * its state (0x801041E8; state 1, cerberus_StateActive, runs the action's
 * handler from 0x80104190: 1 roam, 2 chase, 15 knocked back, 17 run and
 * leap, 18 bite), takes the shots it received (cerberus_OnHit) and starts
 * a howl when the player comes into view.
 *
 * This program starts animations itself rather than through the TEST
 * module's enemy_SetAction: it writes the animation wanted (+0x31A, +0x860),
 * the blend frames (+0x8B0) and +0x85C = 1 (cerberus_Anim).
 *
 * Each dog has a 0x40-byte work block (0x801041FC + slot * 0x40; the
 * current one's address is kept at 0x801043BC, see CW_*).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"

void n_cerberus_TargetInCone(void);
void n_cerberus_CrossedWall(void);
void n_cerberus_AvoidWalls(void);
void n_cerberus_Steer(void);
void n_cerberus_SteerTo(void);
void n_cerberus_Circle(void);
void n_cerberus_BlockedSides(void);
void n_cerberus_CanLeap(void);
void n_cerberus_Wander(void);
void n_cerberus_RoamStep(void);
void n_cerberus_ChaseStep(void);
void n_cerberus_KnockStep(void);
void n_cerberus_LeapStep(void);
void n_cerberus_BiteStep(void);
void n_cerberus_OnHit(void);
void n_cerberus_PlaceInRoom(void);
void n_cerberus_TrackPos(void);
void n_cerberus_Setup(void);
void n_cerberus_Update(void);
void n_cerberus_JointHook(void);
void n_cerberus_StateActive(void);

/* ---- the dog's work block ---- */
#define CW               GV(u32, 0x801043BC)
#define CW_TURN          GV(u8, CW + 0x00)    /* turning: 0 none, 1 left, 2 right, 3 circling */
#define CW_LEAP          GV(u8, CW + 0x01)    /* 1 while leaping at the player */
#define CW_STARTED       GV(u8, CW + 0x03)
#define CW_SIDE          GV(u8, CW + 0x04)    /* which way it circles next */
#define CW_CIRCLING      GV(u8, CW + 0x05)
#define CW_DODGE         GV(u8, CW + 0x06)    /* hit: dodge 1 left, 2 right */
#define CW_DODGE_TIME    GV(u8, CW + 0x07)
#define CW_WAIT          GV(u8, CW + 0x08)    /* frames before it moves (dropping in) */
#define CW_DROP          GV(u8, CW + 0x09)    /* dropping in from above: 1, 2 */
#define CW_WANDER        GV(u8, CW + 0x0A)
#define CW_EDGES         GV(u8, CW + 0x0B)    /* also keep off the 0x800B60E0 edges */
#define CW_DROPPED       GV(u8, CW + 0x0C)
#define CW_CIRCLE_TIME   GV(s16, CW + 0x0E)
#define CW_FRAME         GV(s16, CW + 0x10)   /* animation frame counter, last frame's */
#define CW_PREV_FRAME    GV(s16, CW + 0x12)
#define CW_ROAM_TIME     GV(s16, CW + 0x14)
#define CW_DAMAGE        GV(s16, CW + 0x16)
#define CW_HIT_TIME      GV(s16, CW + 0x18)
#define CW_TURN_TIME     GV(s16, CW + 0x1A)
#define CW_WANDER_TIME   GV(s16, CW + 0x1C)
#define CW_HOWLED        GV(s32, CW + 0x20)
#define CW_HITS          GV(s32, CW + 0x24)
#define CW_TARGET_X      GV(s32, CW + 0x28)   /* the point it circles to */
#define CW_TARGET_Z      GV(s32, CW + 0x2C)
#define CW_LAST_POS      (CW + 0x30)          /* last frame's position (16 bytes) */
#define CERBERUS_WORK_BASE 0x801041FCu
#define CERBERUS_WORK_SIZE 0x40

#define g_cerberusAlert  GV(s32, 0x801043C0)  /* 1 one dog started howling, 2 ... */
#define CERBERUS_SPEED   0x80104188u          /* s32 run speed (its low half for +0x866) */
#define CERBERUS_HIT_COOLDOWN GV(u16, 0x8010418C)
#define CERBERUS_ACTIONS 0x80104190u
#define CERBERUS_STATES  0x801041E8u
#define g_gameMode       GV(s32, 0x800BB19C)
#define FN_EN_ANIM_SOUND 0x8008127Cu   /* (e, bank, sound) */
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_DISTANCE      0x8007BDDCu   /* (from, to) */
#define F_ROT_MATRIX     0x8002B128u   /* (rotation, matrix) */
#define F_SET_ROT_MATRIX 0x8002A778u
#define F_ROT_TRANS      0x8002A958u   /* (short vector, long vector out, flag out) */
#define FN_TRACK_SLOT    0x80082708u   /* (e): its position-tracking slot, or < 0 */
#define TRACK_TABLE      0x800B3EA8u   /* 12-byte entries */
#define ANIM_FLAGS_KEEP  0xFFFF7FF3u   /* +0x320 bits an animation start clears */

static u32 cerberus_call(void (*f)(void), u32 a0, u32 a1, u32 a2, u32 a3) {
    A0 = a0; A1 = a1; A2 = a2; A3 = a3;
    f();
    return V0;
}

#define cerberus_Anim en_start_anim

/* Is (tx, tz) within yaw +-half of e (see en_point_in_cone)? */
static u32 cerberus_InCone(u32 rot, u32 left, u32 right, u32 e, u32 half, u32 tx, u32 tz) {
    return en_point_in_cone(rot, left, right, e, (s32)half, 100, tx, tz);
}

/* Ease the body sway (+0x364) back to straight, 0x80 a frame. */
static void cerberus_EaseSway(u32 e) {
    s32 v = GV(s32, e + 0x364);
    if (v == 0) return;
    if (v > 0) { v -= 0x80; GV(s32, e + 0x364) = v >= 0 ? v : 0; }
    else { v += 0x80; GV(s32, e + 0x364) = v <= 0 ? v : 0; }
}

/* Sway the body (+0x364, within +-0x300) 0x80 towards the player's side;
 * rot (8 bytes) and out (12) are stack vectors of the caller. */
static void cerberus_SwayToPlayer(u32 e, u32 rot, u32 out) {
    s32 sway = GV(s32, e + 0x364);
    MEM_W32(rot + 0, GV(u32, e + 0x1C4));
    MEM_W32(rot + 4, GV(u32, e + 0x1C8));
    MEM_W16(rot + 2, MEM_R16(rot + 2) + (u32)sway);
    gcall(F_ROT_STEP, rot, out, 20, 0);
    MEM_W32(out + 0, MEM_R32(out + 0) + (u32)EN_X(e));
    MEM_W32(out + 8, MEM_R32(out + 8) + (u32)EN_Z(e));
    s32 step = (s32)gcall(FN_LINE_SIDE, EN_POS(e), out, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) > 0 ? -0x80 : 0x80;
    MEM_W16(rot + 2, (u32)step);
    if (step < 0 ? sway > -0x300 : sway < 0x300) GV(s32, e + 0x364) += step;
}

/* The point dist x 10 ahead (F_ROT_STEP by -100) into stack vector v. */
static void cerberus_Ahead(u32 e, u32 v, u32 dist) {
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) * 10 * dist + (u32)EN_X(e));
    MEM_W32(v + 4, MEM_R32(v + 4) * 10 * dist + (u32)EN_Y(e));
    MEM_W32(v + 8, MEM_R32(v + 8) * 10 * dist + (u32)EN_Z(e));
}

/* Running and barking: the run sound on two frames of the stride. */
static void cerberus_StepSound(u32 e, s16 a, s16 b) {
    s16 f = CW_FRAME;
    if ((f == a && CW_PREV_FRAME != a) || (f == b && CW_PREV_FRAME != b))
        GCALL3(FN_EN_ANIM_SOUND, e, 0xC, 1);
}

/* ------------------------------------------------------------------ */
/* Queries */

/* 8010001C (e): is its circling target (CW +0x28, +0x2C) within +-0x2C
 * of its facing? */
void n_cerberus_TargetInCone(void) {
    FRAME(0x50);
    u32 e = A0;
    V0 = cerberus_InCone(sp + 0x30, sp + 0x20, sp + 0x10, e, 0x2C, (u32)CW_TARGET_X, (u32)CW_TARGET_Z);
}

/* 8010012C (from, to): the first wall the path crosses from its front,
 * then (if CW_EDGES) the first edge of the 0x800B60E0 list; -1 none. */
void n_cerberus_CrossedWall(void) {
    FRAME(0x40);
    u32 from = A0, to = A1;
    s32 i = en_crossed_wall(from, to);
    if (i == -1 && CW_EDGES != 0)
        i = en_crossed_segment(from, to, GV(u32, EP_EDGES2_N + 4), GV(u8, EP_EDGES2_N), 0x14, 1);
    V0 = (u32)i;
}

/* 80100454 (e, dist, turn): with a wall dist x 10 ahead pick a way round
 * it (CW_TURN 1 or 2, from the walls after a quarter turn either way, at
 * random when both or neither are open or the wall is near) and turn by
 * `turn` (51-150 at random if 0); returns the wall ahead or -1. */
void n_cerberus_AvoidWalls(void) {
    FRAME(0x68);
    u32 e = A0, dist = A1, turn = A2;
    if ((turn & 0xFFFF) == 0) turn = (u32)((s32)en_rand() % 100) + 0x33;
    u32 v = sp + 0x10;
    cerberus_Ahead(e, v, dist);
    s32 ahead = (s32)cerberus_call(n_cerberus_CrossedWall, EN_POS(e), v, 0, 0);
    if (ahead == -1) {
        CW_TURN = 0;
        V0 = (u32)ahead;
        return;
    }
    if (CW_TURN == 0) {
        u16 yaw = GV(u16, e + 0x1C6);
        GV(u16, e + 0x1C6) = (yaw + 0x400) & 0xFFF;
        cerberus_Ahead(e, v, dist);
        s32 left = (s32)cerberus_call(n_cerberus_CrossedWall, EN_POS(e), v, 0, 0);
        GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) + 0x800) & 0xFFF;
        cerberus_Ahead(e, v, dist);
        s32 right = (s32)cerberus_call(n_cerberus_CrossedWall, EN_POS(e), v, 0, 0);
        int random = 0;
        if (ahead == left) CW_TURN = 1;
        else if (ahead == right) CW_TURN = 2;
        else if (left == -1) {
            if (right == -1) random = 1;
            else CW_TURN = 2;
        } else if (right == -1) CW_TURN = 1;
        else {
            /* walls both ways: random when near the wall ahead */
            u32 w = GV(u32, EP_WALLS_N + 4) + (u32)ahead * 16;
            u32 a = sp + 0x20, b = sp + 0x30;
            MEM_W32(a + 0, GV(u32, w + 0));
            MEM_W32(a + 8, GV(u32, w + 4));
            MEM_W32(b + 0, GV(u32, w + 8));
            MEM_W32(b + 8, GV(u32, w + 12));
            if ((s32)gcall(FN_LINE_DIST, a, b, EN_POS(e), 0) < 1000) random = 1;
            else CW_TURN = 0;
        }
        if (random) CW_TURN = (u8)((en_rand() & 1) + 1);
        GV(u16, e + 0x1C6) = yaw;
    }
    if (CW_TURN == 1) GV(u16, e + 0x1C6) -= (u16)turn;
    else if (CW_TURN == 2) GV(u16, e + 0x1C6) += (u16)turn;
    GV(u16, e + 0x1C6) &= 0xFFF;
    V0 = (u32)ahead;
}

/* 80100870 (e, keep_sway, lead, turn): head for the player (lead != 0:
 * the point lead x 10 ahead of the player, checked against the circling
 * target): 0 when it is already within the cone; else sway towards the
 * player (unless keep_sway, then ease it) and turn by `turn` (a third of
 * it unless it sees the player) to the side of the target; returns the
 * turn's sign times 0x30 (sees) or 0x10. */
void n_cerberus_Steer(void) {
    FRAME(0x70);
    u32 e = A0, keep = A1, lead = A2, turn = A3;
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
    u32 in;
    if (lead == 0)
        in = cerberus_InCone(sp + 0x50, sp + 0x40, sp + 0x30, e, 0x2C, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z);
    else
        in = cerberus_call(n_cerberus_TargetInCone, e, 0, 0, 0);
    if (in != 0) {
        cerberus_EaseSway(e);
        V0 = 0;
        return;
    }
    if (keep == 0) cerberus_SwayToPlayer(e, sp + 0x30, sp + 0x38);
    else cerberus_EaseSway(e);
    u32 v = sp + 0x20;
    gcall(FN_ROT_VELOCITY, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) + (u32)EN_X(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    int sees = (EN_FLAGS(e) & EF_SEES) != 0;
    u32 amount = sees ? turn : (u32)((s16)turn / 3);
    u16 yaw = GV(u16, e + 0x1C6);
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), v, MEM_R32(t + 0), MEM_R32(t + 8)) >= 0) {
        yaw += (u16)amount;
        V0 = sees ? 0x30 : 0x10;
    } else {
        yaw -= (u16)amount;
        V0 = sees ? (u32)-0x30 : (u32)-0x10;
    }
    GV(u16, e + 0x1C6) = yaw & 0xFFF;
}

/* 80100C7C (e, x, z, keep_sway, [sp+0x10] turn): turn by `turn` towards
 * (x, z) unless the circling target is already in its cone (0); returns
 * +-0x10. */
void n_cerberus_SteerTo(void) {
    FRAME(0x58);
    u32 e = A0, tx = A1, tz = A2, keep = A3;
    u16 turn = GS(u16, sp + 0x68);
    if (cerberus_call(n_cerberus_TargetInCone, e, 0, 0, 0) != 0) {
        cerberus_EaseSway(e);
        V0 = 0;
        return;
    }
    u32 v = sp + 0x10;
    gcall(FN_ROT_VELOCITY, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) + (u32)EN_X(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    if (keep == 0) cerberus_SwayToPlayer(e, sp + 0x20, sp + 0x28);
    else cerberus_EaseSway(e);
    u16 yaw = GV(u16, e + 0x1C6);
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), v, tx, tz) >= 0) { yaw += turn; V0 = 0x10; }
    else { yaw -= turn; V0 = (u32)-0x10; }
    GV(u16, e + 0x1C6) = yaw & 0xFFF;
}

/* 80100FD4 (e, keep_sway, turn): circle the player: pick a point on the
 * player's either side (+-60 degrees round from the dog, alternately)
 * and run to it for up to 40 frames (CW_CIRCLE_TIME), steering round
 * walls. */
void n_cerberus_Circle(void) {
    FRAME(0x80);
    u32 e = A0, keep = A1, turn = A2;
    u32 v = sp + 0x18;
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) * 10 + (u32)EN_X(e));
    MEM_W32(v + 4, MEM_R32(v + 4) * 10 + (u32)EN_Y(e));
    MEM_W32(v + 8, MEM_R32(v + 8) * 10 + (u32)EN_Z(e));
    s32 wall = (s32)cerberus_call(n_cerberus_CrossedWall, EN_POS(e), v, 0, 0);
    if (CW_CIRCLING == 0) {
        if (CW_CIRCLE_TIME == 0 || wall != -1) {
            if ((u32)EN_DIST(e) < 30000) {
                u32 r = sp + 0x28, m = sp + 0x30, out = sp + 0x50;
                MEM_W16(r + 0, 0);
                MEM_W16(r + 4, 0);
                if (CW_SIDE == 0) { MEM_W16(r + 2, 0x155); CW_SIDE = 1; }
                else { MEM_W16(r + 2, (u32)-0x155); CW_SIDE = 0; }
                gcall(F_ROT_MATRIX, r, m, 0, 0);
                gcall(F_SET_ROT_MATRIX, m, 0, 0, 0);
                MEM_W16(r + 2, 0);
                MEM_W16(r + 0, (u32)GV(u16, g_player + 0x18) - GV(u16, e + 0x198));
                MEM_W16(r + 4, (u32)GV(u16, g_player + 0x20) - GV(u16, e + 0x1A0));
                gcall(F_ROT_TRANS, r, out, sp + 0x60, 0);
                CW_TARGET_X = (s32)(MEM_R32(out + 0) + (u32)EN_X(e));
                CW_TARGET_Z = (s32)(MEM_R32(out + 8) + (u32)EN_Z(e));
            }
            s32 n = (s32)en_rand();
            CW_CIRCLING = 1;
            CW_CIRCLE_TIME = (s16)(n % 40);
            return;
        }
    } else if (wall != -1) {
        cerberus_call(n_cerberus_Steer, e, 0, 0, 0x80);
        return;
    }
    CW_CIRCLING = 0;
    GS(u32, sp + 0x10) = (u32)(s32)(s16)turn;
    cerberus_call(n_cerberus_SteerTo, e, (u32)CW_TARGET_X, (u32)CW_TARGET_Z, keep);
    CW_CIRCLE_TIME -= 1;
}

/* 80101248 (e, dist): walls dist x 10 away an eighth of a turn to its
 * left (bit 0) and right (bit 1). */
void n_cerberus_BlockedSides(void) {
    FRAME(0x38);
    u32 e = A0, dist = A1;
    u16 yaw = GV(u16, e + 0x1C6);
    u32 v = sp + 0x10;
    GV(u16, e + 0x1C6) = (yaw + 0x200) & 0xFFF;
    cerberus_Ahead(e, v, dist);
    s32 left = (s32)cerberus_call(n_cerberus_CrossedWall, EN_POS(e), v, 0, 0);
    GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) - 0x400) & 0xFFF;
    cerberus_Ahead(e, v, dist);
    s32 right = (s32)cerberus_call(n_cerberus_CrossedWall, EN_POS(e), v, 0, 0);
    u32 r = left != -1 ? 1 : 0;
    if (right != -1) r |= 2;
    GV(u16, e + 0x1C6) = yaw;
    V0 = r;
}

/* 8010141C (e): may it leap at the player? Not while turning away, not
 * if the player is out of its cone (+-0x2C), only between 4000 and 6000
 * away with the player within +-0x155. */
void n_cerberus_CanLeap(void) {
    FRAME(0x48);
    u32 e = A0;
    V0 = 0;
    if (CW_TURN_TIME != 0) return;
    if (cerberus_call(n_cerberus_Steer, e, 0, 0, 0x30) != 0) { V0 = 0; return; }
    u32 d = (u32)EN_DIST(e);
    if (d >= 0x1771 || d < 0xFA0) { V0 = 0; return; }
    V0 = cerberus_InCone(sp + 0x30, sp + 0x20, sp + 0x10, e, 0x155, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z);
}

/* 80101568 (e): wander: every 10-69 frames pick a way to turn (away from
 * a wall close by, else at random) and turn 0x40 a frame for 0-9 frames;
 * spin (0x100) while +0x320 & 0x40000; otherwise keep off the walls. */
void n_cerberus_Wander(void) {
    FRAME(0x20);
    u32 e = A0;
    s16 t = CW_WANDER_TIME;
    if (t != 0 && t < 0x33) {
        CW_WANDER_TIME = t - 1;
    } else {
        CW_TURN_TIME = (s16)((s32)en_rand() % 10);
        CW_WANDER_TIME = (s16)((s32)en_rand() % 60 + 10);
        s32 r = (s32)cerberus_call(n_cerberus_BlockedSides, e, 2, 0, 0);
        switch (r) {
        case 1: CW_TURN = 1; break;
        case 2: CW_TURN = 0; break;
        case 0: case 3: CW_TURN = (u8)(en_rand() & 1); break;
        }
    }
    if (EN_FLAGS(e) & 0x40000) {
        GV(u16, e + 0x1C6) += CW_TURN == 0 ? 0x100 : (u16)-0x100;
        return;
    }
    if (CW_TURN_TIME != 0) {
        GV(u16, e + 0x1C6) += CW_TURN == 0 ? 0x40 : (u16)-0x40;
        CW_TURN_TIME -= 1;
        return;
    }
    cerberus_call(n_cerberus_AvoidWalls, e, 1, 0x100, 0);
}

/* ------------------------------------------------------------------ */
/* Actions */

/* 8010176C (e): action 1, roaming: wander about (turning 0x14 a frame for
 * CW_ROAM_TIME frames when a wall is 2000 ahead), circle the player when
 * the way is clear; run (action 2) when the player fires or comes within
 * 6000, or (game mode 3) within 1000 (action 0xC). After the howl
 * (g_cerberusAlert 2) it only heads for the player. */
void n_cerberus_RoamStep(void) {
    FRAME(0x30);
    u32 e = A0;
    if (g_cerberusAlert == 2 && CW_HOWLED == 0) {
        cerberus_call(n_cerberus_Steer, e, 0, 0, 0x30);
    } else if (CW_ROAM_TIME == 0) {
        u32 v = sp + 0x10;
        gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
        MEM_W32(v + 0, MEM_R32(v + 0) * 20 + (u32)EN_X(e));
        MEM_W32(v + 4, MEM_R32(v + 4) * 20 + (u32)EN_Y(e));
        MEM_W32(v + 8, MEM_R32(v + 8) * 20 + (u32)EN_Z(e));
        if ((s32)cerberus_call(n_cerberus_CrossedWall, EN_POS(e), v, 0, 0) != -1) {
            CW_CIRCLE_TIME = 0;
            if (CW_TURN < 3) {
                CW_ROAM_TIME = 10;
            } else {
                CW_ROAM_TIME = (s16)((en_rand() & 0x1F) + 0x33);
                CW_TURN = (u8)(en_rand() & 1);
            }
        } else if (CW_ROAM_TIME == 0) {
            CW_TURN = 3;
            cerberus_call(n_cerberus_Circle, e, 1, 0x10, 0);
        } else {
            goto turning;
        }
    } else {
    turning:
        GV(u16, e + 0x1C6) += CW_TURN == 0 ? 0x14 : (u16)-0x14;
        GV(u16, e + 0x1C6) &= 0xFFF;
        CW_ROAM_TIME -= 1;
    }
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x15, 0);
    cerberus_StepSound(e, 8, 0x2A);
    int alert = (g_fireRequest != 0 && g_reticleX >= 0 && g_reticleY >= 0) || (u32)EN_DIST(e) < 0x1771;
    if (alert && g_gameMode != 3) {
        CW_TURN = 0;
        cerberus_Anim(e, 2, 0x10);
        return;
    }
    if (g_gameMode != 3) return;
    if ((u32)EN_DIST(e) < 1000) {
        CW_TURN = 0;
        cerberus_Anim(e, 0xC, 0x10);
    }
}

/* 80101ABC (e): action 2, the chase: run at the player round the walls
 * (when one is between them: away from it if it has moved, else turning),
 * wander when close (within 4000) until 6000 away again, circle from
 * afar, dodge when shot (CW_DODGE), and leap (action 0x11) when it can. */
void n_cerberus_ChaseStep(void) {
    FRAME(0x40);
    u32 e = A0;
    if (g_gameMode == 3) {
        cerberus_Anim(e, 1, 0x10);
        return;
    }
    if (EN_STEP(e) == 0) {
        s32 r = (s32)en_rand();
        GV(u16, CW + 0x1E) = (u16)((r % 3 + 5) * 1000);
        EN_STEP(e) += 1;
    }
    switch (CW_DODGE) {
    case 1: GV(u16, e + 0x1C6) -= 0x100; CW_DODGE_TIME = 10; break;
    case 2: GV(u16, e + 0x1C6) += 0x100; CW_DODGE_TIME = 10; break;
    }
    CW_DODGE = 0;
    if ((u32)EN_DIST(e) < 0xFA1) CW_WANDER = 1;
    if (CW_DODGE_TIME != 0) {
        CW_DODGE_TIME -= 1;
    } else if (CW_LEAP == 0) {
        if ((s32)cerberus_call(n_cerberus_CrossedWall, EN_POS(e), PLAYER_POS_ADDR, 0, 0) != -1) {
            u32 a = sp + 0x10, b = sp + 0x20;
            for (u32 k = 0; k < 16; k += 4) {
                MEM_W32(a + k, GV(u32, CW_LAST_POS + k));
                MEM_W32(b + k, GV(u32, e + 0x198 + k));
            }
            if ((s32)gcall(FN_DISTANCE, a, b, 0, 0) >= 0x51)
                cerberus_call(n_cerberus_AvoidWalls, e, 1, 0x120, 0);
            else
                GV(u16, e + 0x1C6) += 0x80;
        } else if (CW_WANDER != 0) {
            cerberus_call(n_cerberus_Wander, e, 0, 0, 0);
            if ((u32)EN_DIST(e) >= 0x1770) CW_WANDER = 0;
        } else if (!(EN_FLAGS(e) & EF_SEES)) {
            cerberus_call(n_cerberus_Steer, e, 0, 10, 0x100);
        } else if (CW_TURN_TIME != 0) {
            CW_TURN_TIME -= 1;
            if (cerberus_call(n_cerberus_Steer, e, 0, 0, 0x40) == 0) CW_TURN_TIME = 0;
        } else if ((u32)EN_DIST(e) >= 0x1770) {
            cerberus_call(n_cerberus_Circle, e, 1, 0x40, 0);
        } else if (cerberus_call(n_cerberus_CanLeap, e, 0, 0, 0) != 0) {
            cerberus_Anim(e, 0x11, 0x10);
            GCALL2(FN_EN_SET_SPEED, e, 4);
            CW_LEAP = 1;
            CW_TURN_TIME = 0x1E;
        } else {
            cerberus_call(n_cerberus_Steer, e, 0, 0, 0x40);
        }
    } else if (CW_LEAP == 1) {
        if ((s32)cerberus_call(n_cerberus_AvoidWalls, e, 1, 0x120, 0) != -1) {
            if (CW_TURN == 1) GV(u16, e + 0x1C6) -= 0x10;
            else if (CW_TURN == 2) GV(u16, e + 0x1C6) += 0x10;
        }
        s16 t = (s16)(CW_TURN_TIME - 1);
        CW_TURN_TIME = t;
        if (t == 0) {
            CW_LEAP = 0;
            CW_TURN = 0;
        }
    }
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), GV(u32, CERBERUS_SPEED), 0);
    cerberus_StepSound(e, 0x10, 5);
}

/* 801021BC (e): action 15, knocked back by a shot: pushed along the
 * player's facing; when the animation ends get up (action 5) or, dead,
 * stop the animation and lie (+0x320 | 0x4007). */
void n_cerberus_KnockStep(void) {
    FRAME(0x18);
    u32 e = A0;
    if (CW_FRAME == 0x10) GCALL2(FN_EN_SET_SPEED, e, 3);
    if (EN_HP(e) > 0) cerberus_call(n_cerberus_Steer, e, 1, 0, 0x30);
    gcall(FN_ROT_VELOCITY, PLAYER_ROT_ADDR, EN_VEL(e), (u32)-0x50, 0);
    if (!(EN_FLAGS(e) & 8)) return;
    if (EN_HP(e) > 0) {
        EN_FLAGS(e) &= 0xFFFF7FFFu;
        cerberus_Anim(e, 5, 0x80);
        return;
    }
    s32 n = EN_NPARTS(e);
    for (s32 i = 0; i < n; i++) GV(u8, EN_ANIMPART(e, i) + 0xD) = 0;
    EN_FLAGS(e) |= 0x4007;
}

/* 801022D4 (e): action 17, the run and leap at the player: run (with the
 * dropping-in from above: CW_DROP), dodge, and from frame 0x13 of the
 * leap bite (action 0x12) when close in front of it, or veer from the
 * walls; at the end run on (action 2; 1 in game mode 3). */
void n_cerberus_LeapStep(void) {
    FRAME(0x20);
    u32 e = A0;
    if (CW_WAIT != 0) return;
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), GV(u32, CERBERUS_SPEED), 0);
    cerberus_StepSound(e, 2, 0x17);
    u32 o = GV(u32, e + 0x8BC);
    s32 h = GV(s32, o + 0xB0);
    if (h < 0) {
        if (CW_DROP == 1) {
            EN_FLAGS(e) |= 0x8000;
        } else if (CW_DROP == 2) {
            GV(s32, o + 0xB0) = h + 100;
            EN_Y(e) += 100;
            u32 o2 = GV(u32, e + 0x8BC);
            if (GV(s32, o2 + 0xB0) >= 0) {
                EN_Y(e) = 0;
                GV(s32, o2 + 0xB0) = 0;
                CW_DROP = 0;
            }
        }
    }
    switch (CW_DODGE) {
    case 1: GV(u16, e + 0x1C6) -= 0x100; CW_DODGE_TIME = 0x14; break;
    case 2: GV(u16, e + 0x1C6) += 0x100; CW_DODGE_TIME = 0x14; break;
    }
    CW_DODGE = 0;
    if (CW_DODGE_TIME != 0) CW_DODGE_TIME -= 1;
    s16 f = CW_FRAME;
    if (f < 0x13) return;
    int veer;
    if ((u32)EN_DIST(e) < 0x5DC && (EN_FLAGS(e) & EF_SEES) && g_gameMode != 3 && CW_DROPPED == 0) {
        if (f < 0x1B) {
            CW_LEAP = 1;
            cerberus_Anim(e, 0x12, 0x10);
            veer = 0;
        } else {
            veer = 1;
        }
    } else {
        veer = !(CW_FRAME < 0x1B);
    }
    if (veer) {
        CW_DROPPED = 0;
        switch ((s32)cerberus_call(n_cerberus_BlockedSides, e, 1, 0, 0)) {
        case 0: case 1: case 3: GV(u16, e + 0x1C6) -= 0x80; break;
        case 2: GV(u16, e + 0x1C6) += 0x80; break;
        }
    }
    if (!(EN_FLAGS(e) & 8)) return;
    EN_FLAGS(e) &= 0xFFFF7FFFu;
    cerberus_Anim(e, g_gameMode == 3 ? 1 : 2, 0x10);
}

/* 8010265C (e): action 18, the bite: grab the player (in game mode 1,
 * unless grabs are locked); then circle round (0x75 a frame) until the
 * animation ends and run off (action 2) away from the walls or the
 * player's way. */
void n_cerberus_BiteStep(void) {
    FRAME(0x30);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if (step == 0) {
        EN_STEP(e) = 1;
        if (g_gameMode != 1 || ep_grabLock != 0 || ep_grabLock2 != 0) return;
        GCALL2(FN_EN_SET_SPEED, e, 9);
        GCALL3(FN_EN_GRAB_PLAYER, e, 6, 0);
        GCALL1(FN_EN_GRAB_START, e);
        return;
    }
    if (step != 1) return;
    if (g_gameMode == 3) {
        cerberus_Anim(e, 0x13, 0x10);
        return;
    }
    GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) + 0x75) & 0xFFF;
    if (!(EN_FLAGS(e) & 8)) return;
    switch ((s32)cerberus_call(n_cerberus_BlockedSides, e, 2, 0, 0)) {
    case 1:
        CW_TURN = 2;
        break;
    case 2:
        CW_TURN = 1;
        break;
    case 0:
    case 3: {
        u32 v = sp + 0x10;
        gcall(F_ROT_STEP, PLAYER_ROT_ADDR, v, (u32)g_playerMoveSpeed, 0);
        MEM_W32(v + 0, MEM_R32(v + 0) + GV(u32, g_player + 0x18));
        MEM_W32(v + 8, MEM_R32(v + 8) + GV(u32, g_player + 0x20));
        CW_TURN = (s32)gcall(FN_LINE_SIDE, PLAYER_POS_ADDR, v, (u32)EN_X(e), (u32)EN_Z(e)) >= 0 ? 2 : 1;
        break;
    }
    }
    GCALL3(FN_EN_ANIM_SOUND, e, 0xC, 1);
    cerberus_Anim(e, 2, 0x10);
    GV(u16, e + 0x866) = GV(u16, CERBERUS_SPEED);
}

/* ------------------------------------------------------------------ */
/* Per frame */

/* 80102AE8 (e): the shots it took this frame: every third hit while
 * running dodge (CW_DODGE, away from the walls); take the damage (all
 * of +0x330.., count +0x324 + 1); dead: killed, collapse (anim 0xF);
 * else maybe flinch (anim 0xF) or stagger (anim 8). */
void n_cerberus_OnHit(void) {
    FRAME(0x20);
    u32 e = A0;
    EN_FLAGS(e) &= ~0x10u;
    if (CW_DODGE_TIME == 0) {
        if (EN_HP(e) <= 0) return;
        CW_DAMAGE = 0;
        CW_DODGE = 0;
        s16 a = EN_ANIM(e);
        if (a < 0xD || a == 0x10 || a >= 0x13) {
            if (CW_HIT_TIME != 0 && GV(s32, GV(u32, e + 0x8BC) + 0xB0) == 0) {
                s32 n = CW_HITS + 1;
                CW_HITS = n;
                if (n == 3) {
                    CW_HITS = 0;
                    s32 r = (s32)cerberus_call(n_cerberus_BlockedSides, e, 1, 0, 0);
                    if (r == 1 || r == 2) { CW_DODGE = (u8)r; EN_NHITS(e) = -1; return; }
                    if (r == 0) {
                        CW_DODGE = (en_rand() & 1) ? 2 : 1;
                        EN_NHITS(e) = -1;
                        return;
                    }
                    CW_DODGE = 0;
                }
            } else {
                CW_HITS = 0;
            }
        }
    }
    CW_HIT_TIME = (s16)CERBERUS_HIT_COOLDOWN;
    for (s32 i = 0; i <= EN_NHITS(e); i++) {
        EN_HP(e) = (s16)(GV(u16, e + 0x31E) - GV(u16, e + 0x330 + i * 2));
        CW_DAMAGE = (s16)(GV(u16, CW + 0x16) + GV(u16, e + 0x330 + i * 2));
    }
    if (EN_HP(e) <= 0) {
        GCALL2(FN_EN_KILLED, e, (s32)EN_ROOMID(e));
        GCALL2(FN_EN_SET_SPEED, e, 0);
        cerberus_Anim(e, 0xF, 0x10);
        EN_NHITS(e) = -1;
        return;
    }
    if (CW_DODGE_TIME != 0) { EN_NHITS(e) = -1; return; }
    u32 k = (u32)(s32)(s16)(GV(u16, e + 0x85E) - 0xD);
    int flinch;
    if (k == 2) {
        EN_NHITS(e) = -1;
        return;
    }
    if (k < 6 && k != 3) {
        GCALL2(FN_EN_SET_SPEED, e, 2);
        flinch = 1;
    } else {
        GCALL2(FN_EN_SET_SPEED, e, 2);
        flinch = (s32)en_rand() % 100 < 8;
        if (!flinch) {
            if ((s32)en_rand() % 100 < 0x14) cerberus_Anim(e, 8, 0x10);
            EN_NHITS(e) = -1;
            return;
        }
    }
    EN_FLAGS(e) |= 0x8000;
    cerberus_Anim(e, 0xF, 0x10);
    EN_NHITS(e) = -1;
}

/* 80102EBC (e): set up by room: stage 13 room 0: hidden until event
 * 0xC7, then dropping in from 1000 above (once, event 0x33) or placed;
 * stage 7 room 3 and stage 13 room 6: keep off the edges too. */
void n_cerberus_PlaceInRoom(void) {
    FRAME(0x20);
    u32 e = A0;
    s32 stage = GV(s32, 0x800A866C), room = GV(s32, 0x800B3F08);
    if (stage == 0xD && room == 0) {
        if (GCALL2(FN_FLAG_TEST, ep_roomFlags, 0xC7) == 0) {
            GV(u8, e + 0x29) = 0;
            return;
        }
        if (GCALL2(FN_FLAG_TEST, ep_eventFlags, 0x33) == 0) {
            CW_WAIT = 0x16;
            CW_DROP = 2;
            CW_DROPPED = 1;
            u32 o = GV(u32, e + 0x8BC);
            GV(u32, e + 0x170) = 0;
            GV(u32, e + 0x174) = 0;
            GV(u32, e + 0x178) = 0;
            GV(s32, o + 0xB0) -= 1000;
            EN_Y(e) -= 1000;
            return;
        }
        EN_Z(e) = -0x18C0;
        return;
    }
    if ((stage == 7 && room == 3) || (stage == 0xD && room == 6)) CW_EDGES = 1;
}

/* 80102FF0 (e): stage 11 room 8: keep its position-tracking entry
 * (0x800B3EA8, +0x354) up to date (taking the entry's height when it has
 * one), and track only while in the corner x < -0x145F, z >= 0x1E14. */
void n_cerberus_TrackPos(void) {
    u32 e = A0;
    s32 k = GV(s32, e + 0x354);
    if (k >= 0) {
        u32 t = TRACK_TABLE + (u32)k * 12;
        if (GV(s16, t + 0) == 1) EN_Y(e) = GV(s16, t + 4);
        GV(u16, t + 2) = GV(u16, e + 0x198);
        GV(u16, t + 6) = GV(u16, e + 0x1A0);
        GV(u16, t + 8) = GV(u16, e + 0x1C6);
    }
    GV(u16, TRACK_TABLE + (u32)GV(s32, e + 0x354) * 12) = 0;
    if (EN_X(e) < -0x145F && EN_Z(e) >= 0x1E14) {
        V0 = GV(u32, e + 0x358);
    } else {
        V0 = (u32)-1;
    }
    GV(u32, e + 0x354) = V0;
}

/* 801030F0 (type, e): set up a dog: its model, tables, speed, joint hook
 * (cerberus_JointHook) and work block, and its tracking slot. */
void n_cerberus_Setup(void) {
    FRAME(0x20);
    u32 e = A1;
    u32 slot = EN_SLOT(e);
    GV(u32, D_800BBB80_ADDR + GV(u32, A0) * 4) = 0x80104098;
    GV(u32, e + 0x840) = 0x80103FA4;
    GV(u32, e + 0x844) = 0x80103E74;
    EN_SPEED(e) = (s16)GCALL2(FN_EN_BASE_SPEED, e, 0);
    GV(u32, e + 0x894) = 0x80103B80;
    u32 w = CERBERUS_WORK_BASE + slot * CERBERUS_WORK_SIZE;
    GV(u32, e + 0x364) = 0;
    GV(u8, w + 3) = 0;
    GV(u16, w + 0xE) = 0;
    GV(u8, w + 4) = (u8)(en_rand() & 1);
    GV(u8, w + 8) = 0;
    GV(u8, w + 9) = 0;
    GV(u32, w + 0x20) = 0;
    GV(u8, w + 0xC) = 0;
    GV(u8, w + 0xB) = 0;
    s32 t = (s32)GCALL1(FN_TRACK_SLOT, e);
    if (t >= 0) {
        GV(s32, e + 0x354) = t;
        GV(s32, e + 0x358) = t;
    } else {
        GV(s32, e + 0x354) = -1;
    }
    if (EN_SLOT(e) >= 3) GV(u8, e + 0x2B) |= 2;
    g_cerberusAlert = 0;
    V0 = 0x80100000;
}

/* 801031FC (e): a dog's frame (see the top of the file). */
void n_cerberus_Update(void) {
    FRAME(0x48);
    u32 e = A0;
    CW = CERBERUS_WORK_BASE + (u32)EN_SLOT(e) * CERBERUS_WORK_SIZE;
    if (CW_STARTED == 0) {
        A0 = e;
        n_cerberus_PlaceInRoom();
        CW_STARTED += 1;
        if (EN_ANIM(e) == 3) g_cerberusAlert = 1;
        u16 anim = GV(u16, e + 0x85E);
        EN_ALIVE(e) = 1;
        GV(u16, e + 0x85C) = 1;
        GV(u32, e + 0x8B0) = 2;
        EN_FLAGS(e) &= ANIM_FLAGS_KEEP;
        GV(u16, e + 0x318) = anim;
        GV(u16, e + 0x31A) = anim;
        GV(u16, e + 0x860) = anim;
    }
    u32 an = EN_ANIMSTATE(e);
    if (GV(u16, an + 0x16) < GV(u16, an + 0x14)) {
        CW_FRAME = 0;
    } else {
        CW_PREV_FRAME = CW_FRAME;
        if ((s32)CW_FRAME != (s32)GV(u16, EN_ANIMSTATE(e) + 0x14)) CW_FRAME += 1;
    }
    if (CW_WAIT != 0) {
        CW_WAIT -= 1;
    } else if (GV(u32, e + 0x170) == 0) {
        GV(u32, e + 0x170) = 0x1000;
        GV(u32, e + 0x174) = 0x1000;
        GV(u32, e + 0x178) = 0x1000;
    }
    GCALL1(GV(u32, CERBERUS_STATES + (u32)((s32)EN_STATE(e) << 2)), e);
    if (EN_STATE(e) != 1 && CW_TURN >= 3) CW_TURN = 0;
    if (EN_FLAGS(e) & EF_SHOT) {
        A0 = e;
        n_cerberus_OnHit();
        GCALL1(FN_EN_HIT_POST, e);
        if (CW_DODGE != 0) cerberus_Anim(e, 2, 0x10);
    } else if (g_cerberusAlert == 2) {
        if (EN_HP(e) > 0 && EN_ANIM(e) != 3 && CW_HOWLED == 0 &&
            cerberus_InCone(sp + 0x30, sp + 0x20, sp + 0x10, e, 0x1E, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z)) {
            cerberus_Anim(e, 3, 0x40);
            CW_HOWLED = 1;
        }
    }
    if (CW_HIT_TIME != 0) CW_HIT_TIME -= 1;
    if (GV(s32, 0x800A866C) == 0xB && GV(s32, 0x800B3F08) == 8) {
        A0 = e;
        n_cerberus_TrackPos();
    }
    for (u32 k = 0; k < 16; k += 4) GV(u32, CW_LAST_POS + k) = GV(u32, e + 0x198 + k);
}

/* 80103B80 (part, joint): the model's joint hook: joint 5 (ids 101-120)
 * turns with the body sway (+0x364) of the dog being drawn. */
void n_cerberus_JointHook(void) {
    if ((A1 - 1) / 20 == 5)
        GV(u16, A0 + 0x46) += GV(u16, ep_enemyCur + 0x364);
    V0 = 0;
}

/* 80103BCC (e): state 1: run the action's handler. */
void n_cerberus_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, CERBERUS_ACTIONS + (u32)((s32)EN_ACTION(e) << 2)), e);
}
