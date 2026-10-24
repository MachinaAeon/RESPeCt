/* The final boss (enemy program ENEMPROG 63, loaded in enemy slot B at
 * 0x80100000): the mutated Tyrant on the heliport. Identified by how it
 * looks in the area scenarios that load it (area34, area35, area37:
 * stage 17 room 2, the clawed giant on the heliport) and by what the
 * code does: it stalks the player, keeping to their side, rushes in and
 * leaps (lifting off and landing through its model's height, +0x8BC +0xB0),
 * swipes with its claws (grabbing the player), staggers after enough
 * hits and gloats when the player is dead.
 *
 * finalboss_Update (0x80102E98) runs every frame for each one: it picks
 * the work block, counts animation frames, runs the state's handler
 * (0x80103434; state 1, finalboss_StateActive, runs the action's handler
 * from 0x801033E4), and takes the hits (finalboss_TakeHits).
 * Actions (and the animation of the same number): 0 idle, 1 walk, 2 run,
 * 3 and 4 swipes, 5 leap up, 7 stagger, 9 gloat, 10 airborne, 12 land,
 * 18 and 19 the close attack.
 *
 * What it is doing (FB_MODE): 0 idle, 1 stalking, 2 rushing, 3 player
 * dead, 4 staggered (dodging), 5 dying, 6 leaping in.
 *
 * Each one has a 0x18-byte work block (0x80103448 + slot * 0x18; the
 * current one's address is kept at 0x80103478, see FB_*). Animations are
 * started like the Cerberus' (en_start_anim).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"
#include "poll.h"

void n_finalboss_CrossedWall(void);
void n_finalboss_PlayerWalls(void);
void n_finalboss_PlayerInCone(void);
void n_finalboss_TurnTo(void);
void n_finalboss_InPlayerCone(void);
void n_finalboss_PlayerSide(void);
void n_finalboss_TurnBeside(void);
void n_finalboss_NotFacing(void);
void n_finalboss_Grab(void);
void n_finalboss_AnimEvent(void);
void n_finalboss_Lift(void);
void n_finalboss_Land(void);
void n_finalboss_StepSound(void);
void n_finalboss_Anim(void);
void n_finalboss_PickSwipe(void);
void n_finalboss_CheckClose(void);
void n_finalboss_CheckRange(void);
void n_finalboss_CheckMid(void);
void n_finalboss_TryGrab(void);
void n_finalboss_StartMoving(void);
void n_finalboss_StartGloat(void);
void n_finalboss_IdleWake(void);
void n_finalboss_IdleWait(void);
void n_finalboss_StalkStep(void);
void n_finalboss_RushStep(void);
void n_finalboss_LeapUp(void);
void n_finalboss_LeapIn(void);
void n_finalboss_IdleAct(void);
void n_finalboss_WalkAct(void);
void n_finalboss_RunAct(void);
void n_finalboss_SwipeAct(void);
void n_finalboss_Swipe2Act(void);
void n_finalboss_LeapAct(void);
void n_finalboss_GloatAct(void);
void n_finalboss_AirAct(void);
void n_finalboss_LandAct(void);
void n_finalboss_CloseAct(void);
void n_finalboss_Close2Act(void);
void n_finalboss_OnHit(void);
void n_finalboss_TakeHits(void);
void n_finalboss_Nothing(void);
void n_finalboss_StateActive(void);
void n_finalboss_StateNone(void);
void n_finalboss_ModelHook(void);
void n_finalboss_Update(void);

/* ---- the work block ---- */
#define FB               GV(u32, 0x80103478)
#define FB_MODE          GV(u8, FB + 0x01)    /* what it is doing (see the top) */
#define FB_STARTED       GV(u8, FB + 0x02)
#define FB_FAR           GV(u8, FB + 0x04)    /* leapt from far */
#define FB_SIDE          GV(u8, FB + 0x06)
#define FB_HITS          GV(u8, FB + 0x07)    /* hits towards a stagger */
#define FB_MID           GV(u8, FB + 0x08)    /* leapt from mid range */
#define FB_ROARED        GV(u8, FB + 0x09)
#define FB_TIME          GV(s16, FB + 0x0A)   /* timer, or vertical speed */
#define FB_TIME2         GV(s16, FB + 0x0C)
#define FB_FRAME         GV(s16, FB + 0x0E)   /* animation frame counter */
#define FB_PREV_FRAME    GV(s16, FB + 0x10)
#define FB_ANIM_FRAME    GV(s16, FB + 0x12)   /* last frame's animation frame */
#define FINALBOSS_WORK_BASE 0x80103448u
#define FINALBOSS_WORK_SIZE 0x18

#define FINALBOSS_ACTIONS 0x801033E4u
#define FINALBOSS_STATES  0x80103434u
#define g_finalbossGrabArg GV(s16, 0x801031E8)
#define g_gameMode       GV(s32, 0x800BB19C)
#define g_playerHp       GV(s16, 0x800A8584)
#define g_currentWeapon  GV(u32, 0x800A8580)   /* pointer: *it is the weapon ID */
#define FN_EN_ANIM_SOUND 0x8008127Cu   /* (e, bank, sound) */
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_MODEL_RESET   0x80073C5Cu   /* (e) */
/* translated (not reached by the scenarios) */
#define FN_FB_WALL_AHEAD  0x801001F8u  /* (e, dist) */
#define FN_FB_DODGE       0x8010115Cu  /* (e) */
#define FN_FB_DODGE_STEP  0x801017E0u
#define FN_FB_NOP         0x801017D8u
#define FN_FB_RUN_TURN    0x80101D98u
#define FN_FB_DIE         0x80101DF8u

static u32 fb_call(void (*f)(void), u32 a0, u32 a1, u32 a2) {
    A0 = a0; A1 = a1; A2 = a2;
    f();
    return V0;
}

/* Has the animation reached its end (frame +0x16 not behind +0x14)? */
static int fb_anim_done(u32 e) {
    u32 an = EN_ANIMSTATE(e);
    return GV(u16, an + 0x16) >= GV(u16, an + 0x14);
}

static s32 fb_sign(s32 v) {
    if (v == 0) return -1;
    return mips_div(v, v >= 0 ? v : (s32)(0u - (u32)v));
}

/* 80100004 (from, to): the first wall the path crosses coming from its
 * front, or -1 (each side taken as v / |v|, 0 counting as -1). The wall's
 * ends are copied to the stack for the side tests. */
void n_finalboss_CrossedWall(void) {
    FRAME(0x60);
    u32 from = A0, to = A1;
    u32 n = GV(u8, EP_WALLS_N);
    u32 w = GV(u32, EP_WALLS_N + 4);
    u32 a = sp + 0x10, b = sp + 0x20;
    GS(u32, sp + 0x30) = n;
    for (u32 i = 0; (s32)i < (s32)n; i++, w += 16) {
        GS(u32, a) = GV(u32, w);
        GS(u32, a + 8) = GV(u32, w + 4);
        GS(u32, b) = GV(u32, w + 8);
        GS(u32, b + 8) = GV(u32, w + 12);
        s32 s2 = (s32)gcall(FN_LINE_SIDE, a, b, MEM_R32(from), MEM_R32(from + 8));
        s32 s1 = (s32)gcall(FN_LINE_SIDE, a, b, MEM_R32(to), MEM_R32(to + 8));
        s32 s0 = (s32)gcall(FN_LINE_SIDE, from, to, GS(u32, a), GS(u32, a + 8));
        s32 s3 = (s32)gcall(FN_LINE_SIDE, from, to, GS(u32, b), GS(u32, b + 8));
        s32 g2 = fb_sign(s2), g1 = fb_sign(s1), g0 = fb_sign(s0), g3 = fb_sign(s3);
        if ((s32)((u32)g2 * (u32)g1) < 0 && (s32)((u32)g0 * (u32)g3) < 0 && s2 <= 0) { V0 = i; return; }
        if ((s32)(i + 1) < (s32)n) LOOP_POLL(0x801001BC);
    }
    V0 = (u32)-1;
}

/* 801002D4 (k): walls beside the player, k x 1000 out: bit 1 on their
 * right (yaw + 0x400), bit 0 on their left (yaw + 0xC00). */
void n_finalboss_PlayerWalls(void) {
    FRAME(0x38);
    V0 = en_player_walls(sp + 0x10, A0, 0x80100004u);
}

/* 801004A8 (e, half): is the player within yaw +-half of e? */
void n_finalboss_PlayerInCone(void) {
    FRAME(0x50);
    V0 = en_player_in_cone_r(sp, A0, A1);
}

/* 801005BC (e, dist, step): unless the target is already in its cone
 * (+-0x71), turn by step towards it: the player (dist 0), or the point
 * dist x 1000 in front of them. Returns the turn made (0: none). */
void n_finalboss_TurnTo(void) {
    FRAME(0x48);
    V0 = en_turn_to(sp, A0, A1, A2, 0x801004A8u);
}

/* 80100714 (e, half): is e within the player's rear cone (yaw + 0x800
 * +-half)? */
void n_finalboss_InPlayerCone(void) {
    FRAME(0x50);
    u32 e = A0, half = A1;
    u32 rot = sp + 0x30, left = sp + 0x20, right = sp + 0x10;
    GS(u16, rot + 0) = GV(u16, g_player + 0x44);
    u32 y = (u32)GV(u16, g_player + 0x46) + 0x800;
    GS(u16, rot + 4) = GV(u16, g_player + 0x48);
    GS(u16, rot + 2) = (u16)((y - half) & 0xFFF);
    gcall(F_ROT_STEP, rot, left, (u32)-100, 0);
    MEM_W32(left, MEM_R32(left) + GV(u32, PLAYER_POS_ADDR));
    MEM_W32(left + 8, MEM_R32(left + 8) + GV(u32, PLAYER_POS_ADDR + 8));
    GS(u16, rot + 2) = (u16)(GS(u16, rot + 2) + (half << 1));
    gcall(F_ROT_STEP, rot, right, (u32)-100, 0);
    MEM_W32(right, MEM_R32(right) + GV(u32, PLAYER_POS_ADDR));
    MEM_W32(right + 8, MEM_R32(right + 8) + GV(u32, PLAYER_POS_ADDR + 8));
    if ((s32)gcall(FN_LINE_SIDE, PLAYER_POS_ADDR, left, (u32)EN_X(e), (u32)EN_Z(e)) <= 0) { V0 = 0; return; }
    V0 = (s32)gcall(FN_LINE_SIDE, PLAYER_POS_ADDR, right, (u32)EN_X(e), (u32)EN_Z(e)) < 0;
}

/* 80100840 (e): which side of the player's facing e is on: 0 straight
 * ahead or behind, 1 one side, 2 the other. */
void n_finalboss_PlayerSide(void) {
    FRAME(0x30);
    u32 e = A0, v = sp + 0x10;
    gcall(F_ROT_STEP, PLAYER_ROT_ADDR, v, (u32)-100, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) + GV(u32, PLAYER_POS_ADDR));
    MEM_W32(v + 4, MEM_R32(v + 4) + GV(u32, PLAYER_POS_ADDR + 4));
    MEM_W32(v + 8, MEM_R32(v + 8) + GV(u32, PLAYER_POS_ADDR + 8));
    s32 s = (s32)gcall(FN_LINE_SIDE, PLAYER_POS_ADDR, v, (u32)EN_X(e), (u32)EN_Z(e));
    V0 = s == 0 ? 0 : s > 0 ? 1 : 2;
}

/* 801008E4 (e, dist, step): turn towards a point beside the player (their
 * facing turned a quarter, away from the walls round them, else to e's
 * side), as finalboss_TurnTo. */
void n_finalboss_TurnBeside(void) {
    FRAME(0x30);
    u32 e = A0, dist = A1, step = A2;
    GS(u32, sp + 0x10) = GV(u32, g_player + 0x44);
    GS(u32, sp + 0x14) = GV(u32, g_player + 0x48);
    s32 w = (s32)fb_call(n_finalboss_PlayerWalls, 1, 0, 0);
    if (w == 1) {
        GV(u16, g_player + 0x46) -= 0x400;
    } else if (w == 2) {
        GV(u16, g_player + 0x46) += 0x400;
    } else if (w == 0) {
        if ((s32)fb_call(n_finalboss_PlayerSide, e, 0, 0) < 2) GV(u16, g_player + 0x46) += 0x400;
        else GV(u16, g_player + 0x46) -= 0x400;
    }
    fb_call(n_finalboss_TurnTo, e, dist, step);
    GV(u32, g_player + 0x44) = GS(u32, sp + 0x10);
    GV(u32, g_player + 0x48) = GS(u32, sp + 0x14);
    V0 = 0;
}

/* 80100A08 (e): 1 unless e and the player face each other (yaws
 * 0x671..0x98F apart). */
void n_finalboss_NotFacing(void) {
    V0 = en_not_facing(A0, 0x671, 0x31F);
}

/* 80100A3C (e, arg): grab the player (argument kept at 0x801031E8) if
 * the game allows it. */
void n_finalboss_Grab(void) {
    FRAME(0x18);
    u32 e = A0, arg = A1;
    if (g_gameMode != 1 || ep_grabLock != 0 || ep_grabLock2 != 0) return;
    g_finalbossGrabArg = (s16)arg;
    gcall(FN_EN_GRAB_PLAYER, e, 6, 0, 0);
    GCALL1(FN_EN_GRAB_START, e);
}

/* 80100AEC (e, part, k): the event bit of animated part `part`, k frames
 * ahead (or back when playing backwards). */
void n_finalboss_AnimEvent(void) {
    V0 = en_anim_event(A0, A1, A2);
}

/* 80100B50 (e, dy): rise by dy (model height +0xB0 and y). */
void n_finalboss_Lift(void) {
    en_lift(A0, A1);
}

/* 80100B7C (e): 1 while still in the air; else land (height 0) with a
 * thud, 0. */
void n_finalboss_Land(void) {
    FRAME(0x18);
    u32 e = A0;
    if (GV(s32, GV(u32, e + 0x8BC) + 0xB0) < 0) { V0 = 1; return; }
    GV(u32, e + 0x19C) = 0;
    GV(u32, GV(u32, e + 0x8BC) + 0xB0) = 0;
    EN_VY(e) = 0;
    gcall(FN_EN_ANIM_SOUND, e, 0xB, 0xB, 0);
    V0 = 0;
}

/* 80100BD0 (e, f1, f2, run): a footstep on reaching animation frame f1
 * or f2 (sound 0 14 walking, 2 15 running). */
void n_finalboss_StepSound(void) {
    FRAME(0x18);
    u32 e = A0;
    s32 f1 = (s32)A1, f2 = (s32)A2;
    u32 run = A3;
    s32 f = FB_FRAME;
    if (!((f == f1 && FB_PREV_FRAME != f) || (f == f2 && FB_PREV_FRAME != f2))) return;
    if (run) gcall(FN_EN_ANIM_SOUND, e, 2, 0xF, 0);
    else gcall(FN_EN_ANIM_SOUND, e, 0, 0xE, 0);
}

/* 80100C48 (e, anim, blend): start an action / animation. */
void n_finalboss_Anim(void) {
    u32 e = A0;
    FB_FRAME = 0;
    en_start_anim(e, A1, A2);
    V0 = 1;
}
static void fb_anim(u32 e, u32 anim, u32 blend) { fb_call(n_finalboss_Anim, e, anim, blend); }

/* rand() % n (signed) */
static s32 fb_rand(s32 n) { return (s32)en_rand() % n; }

/* 80100C88 (e): rush in with one of the two swipes (3, 4: even odds). */
void n_finalboss_PickSwipe(void) {
    FRAME(0x18);
    u32 e = A0;
    u32 anim = fb_rand(100) < 0x32 ? 4 : 3;
    FB_MODE = 2;
    fb_anim(e, anim, 0x40);
}

/* 80100D0C (e): close enough to swipe? 0 when it starts a swipe, else
 * why not: 4 player not in its cone, 3 not seen, 2 not facing, 1 too far
 * (2000). */
void n_finalboss_CheckClose(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!fb_call(n_finalboss_PlayerInCone, e, 0x71, 0)) { V0 = 4; return; }
    if (!(EN_FLAGS(e) & EF_SEES)) { V0 = 3; return; }
    if (fb_call(n_finalboss_NotFacing, e, 0, 0)) { V0 = 2; return; }
    if (!((u32)EN_DIST(e) < 0x7D0)) { V0 = 1; return; }
    A0 = e;
    n_finalboss_PickSwipe();
    V0 = 0;
}

/* 80100D80 (e): leap in from far (5001..7999 away, walking, 40% of the
 * time)? 0 when it leaps, else 7 not in its cone, 1 not facing, 2 not
 * seen, 3 not walking, 4 too far (8000), 5 too close, 6 by chance. */
void n_finalboss_CheckRange(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!fb_call(n_finalboss_PlayerInCone, e, 0x71, 0)) { V0 = 7; return; }
    if (fb_call(n_finalboss_NotFacing, e, 0, 0)) { V0 = 1; return; }
    if (!(EN_FLAGS(e) & EF_SEES)) { V0 = 2; return; }
    if (EN_ACTION(e) != 2) { V0 = 3; return; }
    u32 d = (u32)EN_DIST(e);
    if (!(d < 0x1F40)) { FB_FAR = 0; V0 = 4; return; }
    if (d < 0x1389) { V0 = 5; return; }
    if (fb_rand(100) < 0x3C) { V0 = 6; return; }
    FB_FAR = 1;
    fb_anim(e, 5, 0x20);
    V0 = 0;
}

/* 80100E84 (e): leap in from mid range (2501..2999)? 0 when it leaps,
 * 1 not in range for finalboss_CheckRange, 2 too far, 3 too close,
 * 4 by chance (never: the roll is never negative). */
void n_finalboss_CheckMid(void) {
    FRAME(0x18);
    u32 e = A0;
    if ((s32)fb_call(n_finalboss_CheckRange, e, 0, 0) != 5) { V0 = 1; return; }
    u32 d = (u32)EN_DIST(e);
    if (!(d < 0xBB8)) { FB_MID = 0; V0 = 2; return; }
    if (d < 0x9C5) { V0 = 3; return; }
    if (fb_rand(100) < 0) { V0 = 4; return; }
    FB_MID = 1;
    FB_MODE = 6;
    fb_anim(e, 5, 0x20);
    V0 = 0;
}

/* 80100F5C (e, range, arg): grab the player if seen, facing, nearer than
 * range and in its cone (0); else 1 not seen, 2 not facing, 3 too far,
 * 4 not in its cone. */
void n_finalboss_TryGrab(void) {
    FRAME(0x20);
    u32 e = A0, range = A1, arg = A2;
    if (!(EN_FLAGS(e) & EF_SEES)) { V0 = 1; return; }
    if (fb_call(n_finalboss_NotFacing, e, 0, 0)) { V0 = 2; return; }
    if (!((u32)EN_DIST(e) < range)) { V0 = 3; return; }
    if (!fb_call(n_finalboss_PlayerInCone, e, 0x71, 0)) { V0 = 4; return; }
    fb_call(n_finalboss_Grab, e, (u32)(s32)(s16)arg, 0);
    V0 = 0;
}

/* 80101214 (e): walk when it sees the player (1), else stand (0). */
void n_finalboss_StartMoving(void) {
    FRAME(0x18);
    u32 e = A0;
    if (EN_FLAGS(e) & EF_SEES) { fb_anim(e, 2, 0x10); V0 = 1; }
    else { fb_anim(e, 1, 0x10); V0 = 0; }
}

/* 80101260 (e): gloat (action 9) once it sees the player. */
void n_finalboss_StartGloat(void) {
    FRAME(0x18);
    u32 e = A0;
    if (EN_FLAGS(e) & EF_SEES) fb_anim(e, 9, 0x80);
}

/* 80101294 (e): idle: start stalking (run) at the animation's event. */
void n_finalboss_IdleWake(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!fb_call(n_finalboss_AnimEvent, e, 1, 1)) return;
    FB_MODE = 1;
    fb_anim(e, 2, 0x80);
}

/* 801012E4 (e): idle: wait 20..39 frames, then swipe or move. */
void n_finalboss_IdleWait(void) {
    FRAME(0x18);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        FB_TIME = (s16)(fb_rand(20) + 0x14);
        EN_STEP(e) += 1;
        /* fall through */
    case 1:
        FB_TIME -= 1;
        if (FB_TIME != -1) return;
        EN_STEP(e) += 1;
        /* fall through */
    case 2:
        if (fb_call(n_finalboss_CheckClose, e, 0, 0)) fb_call(n_finalboss_StartMoving, e, 0, 0);
        return;
    }
}

/* 801013EC (e): stalk: walk on (80 a frame), leap or swipe when it can,
 * else turn to the player, or beside them when it is behind them. */
void n_finalboss_StalkStep(void) {
    FRAME(0x18);
    u32 e = A0;
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x50, 0);
    if (!fb_call(n_finalboss_CheckRange, e, 0, 0)) return;
    if (!fb_call(n_finalboss_CheckClose, e, 0, 0)) return;
    if (!fb_call(n_finalboss_InPlayerCone, e, 0x38E, 0)) fb_call(n_finalboss_TurnTo, e, 6, 0x20);
    else fb_call(n_finalboss_TurnBeside, e, 6, 0x20);
    if (EN_FLAGS(e) & EF_SEES) fb_anim(e, 2, 0x80);
}

/* 8010148C (e): rush: run on (180 a frame), leap or swipe when it can,
 * else turn to the player; stand when it loses sight of them. */
void n_finalboss_RushStep(void) {
    FRAME(0x18);
    u32 e = A0;
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0xB4, 0);
    if (!fb_call(n_finalboss_CheckRange, e, 0, 0)) return;
    if (!fb_call(n_finalboss_CheckMid, e, 0, 0)) return;
    if (!fb_call(n_finalboss_CheckClose, e, 0, 0)) return;
    fb_call(n_finalboss_TurnTo, e, 0, 0x80);
    if (!(EN_FLAGS(e) & EF_SEES)) fb_anim(e, 1, 0x10);
}

/* 80101518 (e): leap up (from far): forward 4 x 120 a frame, rising 400
 * less 50 a frame, then airborne (action 12). */
void n_finalboss_LeapUp(void) {
    FRAME(0x20);
    u32 e = A0;
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x78, 0);
    fb_call(n_finalboss_TurnTo, e, 0, 0x80);
    EN_VZ(e) = (s32)((u32)EN_VZ(e) << 2);
    EN_VX(e) = (s32)((u32)EN_VX(e) << 2);
    switch (EN_STEP(e)) {
    case 0:
        FB_TIME = 0x190;
        EN_STEP(e) += 1;
        /* fall through */
    case 1:
        fb_call(n_finalboss_Lift, e, (u32)(s32)FB_TIME, 0);
        FB_TIME -= 0x32;
        if (FB_TIME > 0) return;
        EN_STEP(e) += 1;
        return;
    case 2:
        fb_anim(e, 0xC, 0);
        return;
    }
}

/* 8010162C (e): come down (from far): forward 3 x 120 a frame, falling,
 * land, then swipe or walk; it can grab the player from its 5th frame. */
void n_finalboss_LeapIn(void) {
    FRAME(0x20);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        FB_TIME = 0x190;
        FB_TIME2 = 0;
        EN_STEP(e) += 1;
        /* fall through */
    case 1:
        gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x78, 0);
        EN_VX(e) = (s32)((u32)EN_VX(e) * 3);
        EN_VZ(e) = (s32)((u32)EN_VZ(e) * 3);
        fb_call(n_finalboss_Lift, e, 0u - (u32)(s32)FB_TIME, 0);
        FB_TIME += 0x32;
        if (fb_call(n_finalboss_Land, e, 0, 0)) break;
        EN_STEP(e) += 1;
        break;
    case 2:
        if (fb_call(n_finalboss_AnimEvent, e, 1, 1)) EN_STEP(e) += 1;
        break;
    case 3:
        if (fb_call(n_finalboss_CheckClose, e, 0, 0)) fb_anim(e, 2, 0x80);
        break;
    }
    if (FB_TIME2 == 5) gcall(FN_EN_SET_SPEED, e, 4, 0, 0);
    if (FB_TIME2 >= 5) fb_call(n_finalboss_TryGrab, e, 0x8FC, 0xF);
    FB_TIME2 += 1;
}

/* 80101E7C (e): action 0, idle. */
void n_finalboss_IdleAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!fb_anim_done(e)) return;
    switch (FB_MODE) {
    case 0: A0 = e; n_finalboss_StartGloat(); return;
    case 2: FB_MODE = 1; /* fall through */
    case 1: A0 = e; n_finalboss_IdleWait(); return;
    }
}

/* 80101F14 (e): action 1, walk. */
void n_finalboss_WalkAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!fb_anim_done(e)) return;
    A0 = e; A1 = 3; A2 = 7; A3 = 0;
    n_finalboss_StepSound();
    switch (FB_MODE) {
    case 1: A0 = e; n_finalboss_StalkStep(); return;
    case 5: GCALL1(FN_FB_RUN_TURN, e); return;
    }
}

/* 80101FA4 (e): action 2, run. */
void n_finalboss_RunAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!fb_anim_done(e)) return;
    A0 = e; A1 = 4; A2 = 8; A3 = 1;
    n_finalboss_StepSound();
    switch (FB_MODE) {
    case 2: FB_MODE = 1; /* fall through */
    case 1: A0 = e; n_finalboss_RushStep(); return;
    case 4: GCALL1(FN_FB_DODGE_STEP, e); return;
    }
}

/* The swipes' first steps: 0 set the timer, 1 on; 2 back to stalking
 * (idle) at the animation's event. */
static void fb_swipe_steps(u32 e) {
    s16 step = EN_STEP(e);
    if (step == 1) {
        EN_STEP(e) = (s16)(step + 1);
    } else if (step == 0) {
        FB_TIME = 0;
        EN_STEP(e) += 1;
    } else if (step == 2) {
        if (fb_call(n_finalboss_AnimEvent, e, 1, 1)) {
            FB_MODE = 1;
            fb_anim(e, 0, 0x80);
        }
    }
}

/* 80102058 (e): action 3, the swipe: grabs (2200) from frame 17. */
void n_finalboss_SwipeAct(void) {
    FRAME(0x20);
    u32 e = A0;
    if (!fb_anim_done(e)) return;
    fb_swipe_steps(e);
    if (FB_TIME >= 0x11) fb_call(n_finalboss_TryGrab, e, 0x898, 0x14);
    if (FB_TIME == 0x11) gcall(FN_EN_SET_SPEED, e, 4, 0, 0);
    FB_TIME += 1;
}

/* 80102190 (e): action 4, the double swipe: grabs at frames 17 (1600),
 * 18 (2300) and 19..23 (2500). */
void n_finalboss_Swipe2Act(void) {
    FRAME(0x20);
    u32 e = A0;
    if (!fb_anim_done(e)) return;
    fb_swipe_steps(e);
    s16 t = FB_TIME;
    if (t == 0x11) {
        gcall(FN_EN_SET_SPEED, e, 5, 0, 0);
        fb_call(n_finalboss_TryGrab, e, 0x640, 0xF);
    } else if (t == 0x12) {
        fb_call(n_finalboss_TryGrab, e, 0x8FC, 0xA);
    } else if (t > 0x12 && t < 0x18) {
        fb_call(n_finalboss_TryGrab, e, 0x9C4, 7);
    }
    FB_TIME += 1;
}

/* 80102300 (e): action 5, the leap's take-off: a roar, then up (12) or,
 * from mid range, the close attack (18). */
void n_finalboss_LeapAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!fb_anim_done(e)) return;
    if (EN_STEP(e) == 0) {
        gcall(FN_EN_ANIM_SOUND, e, 8, 0x20, 0);
        EN_STEP(e) += 1;
    }
    if (FB_MODE == 6) {
        if (fb_call(n_finalboss_AnimEvent, e, 1, 1)) fb_anim(e, 0x12, 0x80);
    } else {
        if (fb_call(n_finalboss_AnimEvent, e, 1, 1)) fb_anim(e, 0xA, 0);
    }
}

/* 80102578 (e): action 9, gloat: a roar, then by mode. */
void n_finalboss_GloatAct(void) {
    FRAME(0x20);
    u32 e = A0;
    if (!fb_anim_done(e)) return;
    if (FB_ROARED == 0) {
        FB_ROARED = 1;
        gcall(FN_EN_ANIM_SOUND, e, 8, 0x20, 0);
    }
    switch (FB_MODE) {
    case 0: A0 = e; n_finalboss_IdleWake(); return;
    case 3: GCALL1(FN_FB_NOP, e); return;
    case 5: GCALL1(FN_FB_DIE, e); return;
    }
}

/* 80102654 (e): action 10, airborne. */
void n_finalboss_AirAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!fb_anim_done(e)) return;
    if (FB_MODE == 1) { A0 = e; n_finalboss_LeapUp(); }
}

/* 80102704 (e): action 12, landing. */
void n_finalboss_LandAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (FB_MODE == 1) { A0 = e; n_finalboss_LeapIn(); }
}

/* 80102848 (e): action 18, closing in (200 a frame) after the mid-range
 * leap: the close attack (19) within 3000. */
void n_finalboss_CloseAct(void) {
    FRAME(0x18);
    u32 e = A0;
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0xC8, 0);
    fb_call(n_finalboss_TurnTo, e, 0, 0x80);
    switch (EN_STEP(e)) {
    case 0:
        if (!fb_anim_done(e)) return;
        FB_TIME = 0xC8;
        EN_STEP(e) += 1;
        return;
    case 1:
        if ((u32)EN_DIST(e) < 0xBB9) fb_anim(e, 0x13, 0x20);
        return;
    }
}

/* 80102908 (e): action 19, the close attack: grabs (1900) at the
 * animation's event, then back to walking. */
void n_finalboss_Close2Act(void) {
    FRAME(0x20);
    u32 e = A0;
    fb_call(n_finalboss_TurnTo, e, 0, 0x40);
    s16 step = EN_STEP(e);
    if (step == 0) {
        if (fb_anim_done(e)) {
            FB_TIME = 0;
            EN_STEP(e) += 1;
        }
    } else if (step == 1) {
        if (fb_call(n_finalboss_AnimEvent, e, 1, 1)) {
            fb_call(n_finalboss_TryGrab, e, 0x76C, 0xF);
            if (fb_call(n_finalboss_CheckClose, e, 0, 0)) {
                FB_MODE = 1;
                fb_anim(e, 2, 0x80);
            }
        }
    }
    if (FB_TIME == 3) gcall(FN_EN_SET_SPEED, e, 4, 0, 0);
    FB_TIME += 1;
}

/* 80102A24 (e): the shots it took (not while staggered or dying, nor in
 * actions 5, 6 and 10..16): lose their damage; die at 0 HP; else count
 * the hits (handguns 1, other weapons and the first part 2) towards a
 * stagger (4: dodge). */
void n_finalboss_OnHit(void) {
    FRAME(0x20);
    u32 e = A0;
    if (FB_MODE == 4 || FB_MODE == 5) return;
    s16 act = EN_ACTION(e);
    if (act >= 5 && (act < 7 || (act < 0x11 && act >= 0xA))) return;
    s16 n = EN_NHITS(e);
    if (n >= 0) {
        for (s32 i = 0;; ) {
            u16 d = GV(u16, e + 0x330 + (u32)i * 2);
            i++;
            GV(u16, e + 0x31E) = (u16)(GV(u16, e + 0x31E) - d);
            if (n < i) break;
            LOOP_POLL(0x80102AB4);
        }
    }
    u8 part0 = 0;
    if (EN_NHITS(e) > 0) {
        /* the loop's count is reset inside it: one pass */
        if (GV(u8, e + 0x326) == 0) part0 = 1;
        EN_NHITS(e) = -1;
    }
    if (EN_HP(e) <= 0) {
        gcall(FN_EN_KILLED, e, (u32)(s32)EN_ROOMID(e), 0, 0);
        FB_MODE = 5;
        fb_anim(e, 1, 0x80);
        return;
    }
    s16 weapon = GV(s16, g_currentWeapon);
    if (weapon < 4 && weapon >= 0) {
        FB_HITS += 1;
        if (FB_HITS < 4) return;
        FB_MODE = 4;
        GCALL1(FN_FB_DODGE, e);
        FB_HITS = 0;
        return;
    }
    FB_HITS += 2;
    if (part0 == 0 && FB_HITS < 4) return;
    fb_anim(e, 7, 0x20);
    if (FB_HITS < 4) return;
    FB_MODE = 4;
    FB_HITS = 0;
}

/* 80102C38 (e): take this frame's shots. */
void n_finalboss_TakeHits(void) {
    FRAME(0x18);
    u32 e = A0;
    EN_FLAGS(e) &= ~(u32)EF_SHOT;
    A0 = e;
    n_finalboss_OnHit();
    EN_NHITS(e) = -1;
    GCALL1(FN_EN_HIT_PRE, e);
}

/* 80102CC0: nothing (0). */
void n_finalboss_Nothing(void) {
    V0 = 0;
}

/* 80102CD4 (e): state 1, active: the action's handler. */
void n_finalboss_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, FINALBOSS_ACTIONS + (u32)((s32)EN_ACTION(e) << 2)), e);
}

/* 80102D88: state 4: nothing. */
void n_finalboss_StateNone(void) {
}

/* 80102D90 (e): the model hook (+0x898): once the model's scale (+0xA0)
 * isn't 0x1000, reset it and remove the hook. */
void n_finalboss_ModelHook(void) {
    FRAME(0x18);
    u32 e = A0;
    if (GV(s32, GV(u32, e + 0x8BC) + 0xA0) == 0x1000) return;
    GCALL1(FN_MODEL_RESET, e);
    GV(u32, e + 0x898) = 0;
}

/* 80102E98 (e): every frame. */
void n_finalboss_Update(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 w = FINALBOSS_WORK_BASE + (u32)EN_SLOT(e) * FINALBOSS_WORK_SIZE;
    FB = w;
    if ((s32)GV(u16, EN_ANIMSTATE(e) + 0x14) < (s32)GV(s16, w + 0x12)) {
        GV(s16, w + 0x0E) = 0;
    } else {
        GV(s16, w + 0x10) = GV(s16, w + 0x0E);
        if ((s32)GV(s16, w + 0x12) != (s32)GV(u16, EN_ANIMSTATE(e) + 0x14)) GV(s16, w + 0x0E) += 1;
    }
    FB_ANIM_FRAME = (s16)GV(u16, EN_ANIMSTATE(e) + 0x14);
    if (FB_STARTED == 0) { A0 = e; n_finalboss_Nothing(); }
    GCALL1(GV(u32, FINALBOSS_STATES + (u32)((s32)EN_STATE(e) << 2)), e);
    if (g_playerHp <= 0 && FB_MODE != 3) {
        FB_MODE = 3;
        fb_anim(e, 9, 0x80);
    }
    if (EN_FLAGS(e) & EF_SHOT) {
        gcall(FN_EN_SET_SPEED, e, 6, 0, 0);
        A0 = e;
        n_finalboss_TakeHits();
        GCALL1(FN_EN_HIT_POST, e);
    }
}
