/* The zombie (enemy program ENEMPROG 50, loaded in enemy slot B at
 * 0x80100000; ENEMPROG 34 is the same code for slot A).
 *
 * zombie_Update (0x80105180) runs every frame for each zombie: it picks
 * the zombie's work block, runs the handler of its state (0x80105AA8:
 * 0 set up, 1 active, 2 hit, 3 dying, 4 steered by the second pad), takes
 * the shots it received this frame (zombie_OnHit) and does the per-frame
 * housekeeping (walking speed by distance, the grab cool-down, limbs
 * falling off, the torso twist easing back).
 *
 * In the active state the zombie runs an action, with a pair of handlers
 * per action (0x80105A30 every frame, 0x80105A6C while the action's phase
 * is negative):
 *    0 idle: zombie_IdleThink decides when to start walking
 *    4 walk: steer round walls towards the player (zombie_AvoidWalls),
 *      sway, moan, lunge or grab when close (zombie_WalkStep)
 *    5 grab the player, 6 knocked back by a shot, 7 die, 9 collapse,
 *   14 bite the player (actions 1-3, 8, 10-13 aren't decompiled).
 *
 * Each zombie has a 0xAC-byte work block (0x80105B08 + slot * 0xAC; the
 * current one's address is kept at 0x80105FBC, see ZW_*).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"

/* ---- the zombie's work block ---- */
#define ZW                GV(u32, 0x80105FBC)
#define ZW_AVOID          GV(s16, ZW + 0x00)   /* wall avoidance: 0 none, 1 turning left, 2 right */
#define ZW_AVOID_YAW      GV(s16, ZW + 0x02)
#define ZW_BASE_SPEED     GV(s32, ZW + 0x04)
#define ZW_SPEED2         GV(s32, ZW + 0x08)
#define ZW_KNOCK_FORCE    GV(s32, ZW + 0x0C)   /* push of the last hit (by distance) */
#define ZW_KNOCK          GV(s32, ZW + 0x10)   /* push accumulated for this knock-back */
#define ZW_KNOCK_TIME     GV(s32, ZW + 0x14)
#define ZW_LUNGE          GV(s32, ZW + 0x18)   /* close to the player: 0 undecided, 1 lunge, 2 not */
#define ZW_HEAD_OFF       GV(s32, ZW + 0x1C)   /* head shot off (1: this frame) */
#define ZW_ANIM_SYNC      GV(s32, ZW + 0x84)   /* walk animation sync: 0 idle, 1 scale by speed, 2 normal */
#define ZW_VOICE_BANK     GV(s32, ZW + 0x90)
#define ZW_FRAMES         GV(u32, ZW + 0x94)   /* animation frame table */
#define ZW_GROUP          GV(s32, ZW + 0x98)   /* 0-7: which frame of eight it thinks on */
#define ZW_IN_CONE        GV(s32, ZW + 0x9C)
#define ZW_CONE_CACHE     GV(s32, ZW + 0xA0)   /* zombie_PlayerInCone this frame (-1 unknown) */
#define ZW_ZONE           GV(s32, ZW + 0xA4)
#define ZW_KILLED         GV(s32, ZW + 0xA8)   /* already counted as killed */
#define ZOMBIE_WORK_BASE  0x80105B08u
#define ZOMBIE_WORK_SIZE  0xAC

#define g_grabCooldown    GV(s32, 0x80105FC0)  /* frames before any zombie may grab again */
#define g_biteYaw         GV(s32, 0x80105FC4)
#define g_lastHit         GV(s32, 0x80105AF8)  /* hit picked by zombie_PickHitReaction */
#define g_wallCount       GV(u8, 0x800B60C8)   /* the room's wall segments ... */
#define g_walls           GV(u32, 0x800B60CC)  /* ... 16 bytes each: x0, z0, x1, z1 */
#define g_gameMode        GV(s32, 0x800BB19C)

/* the per-action handlers, per state handlers, hit reaction table */
#define ZOMBIE_ACT_PRE    0x80105A30u
#define ZOMBIE_ACT_STEP   0x80105A6Cu
#define ZOMBIE_STATES     0x80105AA8u
#define ZOMBIE_REACTIONS  0x801059E0u   /* 20 animation ids, by hit kind */
#define ZOMBIE_LIMBS_A    0x801059D4u   /* limb tables for zombie_LoseLimbs, by type */
#define ZOMBIE_LIMBS_B    0x801059D8u
#define ZOMBIE_LIMBS_C    0x801059DCu
#define FN_SEGMENT_HIT    0x80100944u   /* (pos, target): ovl50, not decompiled */
#define FN_ZONE_INIT      0x80104ADCu   /* ovl50, not decompiled */

void n_zombie_Voice(void);
void n_zombie_Sound(void);
void n_zombie_VoiceAt(void);
void n_zombie_SoundAt(void);
void n_zombie_CanGrab(void);
void n_zombie_SetPartsDir(void);
void n_zombie_AnimEvent(void);
void n_zombie_PlayerInCone(void);
void n_zombie_PlayerInConeCached(void);
void n_zombie_CrossedWall(void);
void n_zombie_WallAhead(void);
void n_zombie_PlayerSide(void);
void n_zombie_WallDistance(void);
void n_zombie_AvoidWalls(void);
void n_zombie_NoDeathEvent(void);
void n_zombie_EaseSway(void);
void n_zombie_SwayToPlayer(void);
void n_zombie_WalkAnimFor(void);
void n_zombie_OnHit(void);
void n_zombie_PickHitReaction(void);
void n_zombie_PickFatalReaction(void);
void n_zombie_TryGrab(void);
void n_zombie_IdleThink(void);
void n_zombie_WalkAnim(void);
void n_zombie_WalkThink(void);
void n_zombie_DropLimb(void);
void n_zombie_LoseLimbs(void);
void n_zombie_ApplyDamage(void);
void n_zombie_ArmourBonus(void);
void n_zombie_IdlePre(void);
void n_zombie_CrossedEdge(void);
void n_zombie_LurkPre(void);
void n_zombie_LurkStep(void);
void n_zombie_IdleStep(void);
void n_zombie_WalkPre(void);
void n_zombie_WalkStep(void);
void n_zombie_GrabPre(void);
void n_zombie_GrabStep(void);
void n_zombie_KnockPre(void);
void n_zombie_KnockStep(void);
void n_zombie_DiePre(void);
void n_zombie_DieStep(void);
void n_zombie_CollapsePre(void);
void n_zombie_CollapseStep(void);
void n_zombie_BitePre(void);
void n_zombie_BiteStep(void);
void n_zombie_StateActive(void);
void n_zombie_StateHit(void);
void n_zombie_StateDying(void);
void n_zombie_StatePad(void);
void n_zombie_StepSound(void);
void n_zombie_JointHook(void);
void n_zombie_Setup(void);
void n_zombie_ZoneCheck(void);
void n_zombie_ZoneTrack(void);
void n_zombie_UpdateLostHead(void);
void n_zombie_SpeedByDistance(void);
void n_zombie_SyncWalkAnim(void);
void n_zombie_GrabCooldown(void);
void n_zombie_Update(void);

/* Small wrappers for the native-to-native calls below. */
static u32 zombie_call(void (*f)(void), u32 a0, u32 a1, u32 a2) {
    A0 = a0; A1 = a1; A2 = a2;
    f();
    return V0;
}

/* ------------------------------------------------------------------ */
/* Sounds */

/* 80100560 (e, sound): one of the zombie's voice sounds (its type's bank
 * at work +0x90, offset by +0x84E). */
void n_zombie_Voice(void) {
    FRAME(0x18);
    u32 e = A0;
    gcall(FN_EN_SOUND, e, (u32)GV(s16, e + 0x84E) + (u32)ZW_VOICE_BANK + 3, A1, 0);
}

/* 80100598 (e, sound): a sound from the common bank 2. */
void n_zombie_Sound(void) {
    FRAME(0x18);
    gcall(FN_EN_SOUND, A0, 2, A1, 0);
}

/* 801005BC (e, sound, x): voice sound, enemy_SoundEx variant. */
void n_zombie_VoiceAt(void) {
    FRAME(0x18);
    u32 e = A0;
    gcall(FN_EN_SOUND_EX, e, (u32)GV(s16, e + 0x84E) + (u32)ZW_VOICE_BANK + 3, A1, A2);
}

/* 801005FC (e, sound, x): bank 2 sound, enemy_SoundEx variant. */
void n_zombie_SoundAt(void) {
    FRAME(0x18);
    gcall(FN_EN_SOUND_EX, A0, 2, A1, A2);
}

/* ------------------------------------------------------------------ */
/* Queries */

/* 80100628 (e): may this zombie grab the player now? Not while grabs are
 * locked, while it was just shot, in game modes 2 and up, or while any
 * other living enemy is holding the player. */
void n_zombie_CanGrab(void) {
    u32 e = A0;
    u32 rec = ep_enemies;
    if (ep_grabLock != 0 || (EN_FLAGS(e) & EF_SHOT) || g_gameMode >= 2 || ep_grabLock2 != 0) {
        V0 = 0;
        return;
    }
    s32 n = ep_enemyCount;
    for (s32 i = 0; i < n; i++, rec += ENEMY_SIZE) {
        if (rec != e && EN_HP(rec) > 0 && (EN_FLAGS(rec) & EF_GRABBING)) { V0 = 0; return; }
    }
    V0 = 1;
}

/* 801006E4 (e, dir): set every animated part's direction (+0xD) and
 * clear its +0x10. */
void n_zombie_SetPartsDir(void) {
    u32 e = A0;
    s32 n = EN_NPARTS(e);
    for (s32 i = 0; i < n; i++) {
        u32 p = EN_ANIMPART(e, i);
        GV(u8, p + 0xD) = (u8)A1;
        GV(s16, p + 0x10) = 0;
    }
}

/* 80100728 (e, part, k): does the animation of model part `part` have its
 * event bit set k frames ahead (in the direction it plays)? The bit is
 * the sign of the frame's word in the animation data. */
void n_zombie_AnimEvent(void) {
    u32 e = A0, k = A2;
    u32 p = EN_ANIMPART(e, A1);
    u32 frames = GV(u32, EN_ANIMDATA(e) + 8) + 4;
    u32 w;
    if (GV(s8, p + 0xD) >= 0) w = GV(u32, frames + (u32)GV(u16, p + 0x16) * 4 + k * 4);
    else w = GV(u32, frames + (u32)GV(u16, p + 0x14) * 4 - k * 4);
    V0 = w >> 31;
}

/* 8010078C (e, spread, cache): is the player inside the zombie's view
 * cone (its yaw +-11*spread)? With `cache` (and spread != 90) the answer
 * is kept for the frame in the work block (+0xA0, -1 = not yet asked). */
void n_zombie_PlayerInCone(void) {
    FRAME(0x58);
    u32 e = A0;
    s32 spread = (s32)A1;
    u32 cache = A2;
    int use_cache = cache != 0 && spread != 0x5A;
    if (use_cache && ZW_CONE_CACHE >= 0) { V0 = (u32)ZW_CONE_CACHE; return; }

    u32 right = sp + 0x10, left = sp + 0x20, rot = sp + 0x30;
    s32 half = spread * 11;
    MEM_W16(rot + 4, 0);
    MEM_W16(rot + 0, 0);
    MEM_W16(rot + 2, (u32)GV(u16, e + 0x1C6) + (u32)half);
    gcall(F_ROT_STEP, rot, left, 100, 0);
    MEM_W32(left, MEM_R32(left) + (u32)EN_X(e));
    MEM_W32(left + 8, MEM_R32(left + 8) + (u32)EN_Z(e));
    MEM_W16(rot + 2, MEM_R16(rot + 2) - (u32)(half * 2));
    gcall(F_ROT_STEP, rot, right, 100, 0);
    MEM_W32(right, MEM_R32(right) + (u32)EN_X(e));
    MEM_W32(right + 8, MEM_R32(right + 8) + (u32)EN_Z(e));
    u32 inside = 0;
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), left, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) > 0 &&
        (s32)gcall(FN_LINE_SIDE, EN_POS(e), right, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) < 0)
        inside = 1;
    if (use_cache) ZW_CONE_CACHE = (s32)inside;
    V0 = inside;
}

/* 80100924 (e, spread): zombie_PlayerInCone, cached. */
void n_zombie_PlayerInConeCached(void) {
    FRAME(0x18);
    A2 = 1;
    n_zombie_PlayerInCone();
}

/* 80100AE8 (from, to): the first of the room's wall segments that the
 * path from `from` to `to` (x, z at +0, +8) crosses from its front, or -1. */
void n_zombie_CrossedWall(void) {
    FRAME(0x40);
    V0 = (u32)en_crossed_wall(A0, A1);
}

/* 80100944 (from, to): the first of the room's area edges (the second
 * list: 0x14-byte records) that the path from `from` to `to` crosses,
 * from either side, or -1. */
void n_zombie_CrossedEdge(void) {
    FRAME(0x40);
    V0 = (u32)en_crossed_segment_poll(A0, A1, GV(u32, EP_EDGES_N + 4), GV(u8, EP_EDGES_N), 0x14, 0, 0x80100AB8u);
}

/* 80100C90 (e, dist): the wall crossed by walking `dist` x 20 ahead, or -1. */
void n_zombie_WallAhead(void) {
    FRAME(0x30);
    u32 e = A0;
    u32 v = sp + 0x10;
    gcall(FN_ROT_VELOCITY, EN_ROT(e), v, (u32)-(s32)A1, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) * 20 + (u32)EN_X(e));
    MEM_W32(v + 4, MEM_R32(v + 4) * 20 + (u32)EN_Y(e));
    MEM_W32(v + 8, MEM_R32(v + 8) * 20 + (u32)EN_Z(e));
    A0 = EN_POS(e); A1 = v;
    n_zombie_CrossedWall();
}

/* 80100D40 (e): which side of the zombie's facing line the player is on. */
void n_zombie_PlayerSide(void) {
    FRAME(0x30);
    u32 e = A0;
    u32 v = sp + 0x10;
    gcall(FN_ROT_VELOCITY, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) + (u32)EN_X(e));
    MEM_W32(v + 4, MEM_R32(v + 4) + (u32)EN_Y(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    gcall(FN_LINE_SIDE, EN_POS(e), v, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z);
}

/* 80100DC8 (e, wall): distance from the zombie to wall segment `wall`
 * (the y words of the two points are left unset; the callee ignores them). */
void n_zombie_WallDistance(void) {
    FRAME(0x38);
    u32 e = A0;
    u32 w = g_walls + A1 * 16;
    u32 a = sp + 0x10;
    MEM_W32(a + 0x00, GV(u32, w + 0));
    MEM_W32(a + 0x08, GV(u32, w + 4));
    MEM_W32(a + 0x10, GV(u32, w + 8));
    MEM_W32(a + 0x18, GV(u32, w + 12));
    gcall(FN_LINE_DIST, a, a + 0x10, EN_POS(e), 0);
}

/* Which way to turn round a wall (1 left, 2 right, 0 don't), from the wall
 * ahead, the walls after a quarter turn either way, and the player's side. */
static s32 zombie_AvoidDirection(u32 e, s32 ahead, s32 left, s32 right, s32 side) {
    if (side >= 0) { if (left == -1) return 1; }
    else if (right == -1) return 2;
    if (ahead == left && ahead != -1) return 2;
    if (ahead == right && ahead != -1) return 1;
    if (left != -1) {
        if (right == -1 && side < 0) return 2;
    } else if (right != -1) {
        if (side >= 0) return 1;
    } else {
        return side < 0 ? 2 : 1;
    }
    /* close to the wall ahead: turn towards the player's side */
    A0 = e; A1 = (u32)ahead;
    n_zombie_WallDistance();
    if ((s32)V0 < 500) {
        if (ahead == -1) return 0;
        return side >= 0 ? 1 : 2;
    }
    return 0;
}

/* 80100E24 (e): walking: when a wall is between the zombie and the
 * player, turn along it (0x16 a frame) until the way ahead is clear;
 * otherwise walk on. */
void n_zombie_AvoidWalls(void) {
    FRAME(0x30);
    u32 e = A0;
    if (ZW_AVOID == 0) {
        s32 dir = 0;
        s32 ahead = (s32)zombie_call(n_zombie_WallAhead, e, 100, 0);
        if (ahead != -1 || zombie_call(n_zombie_PlayerInConeCached, e, 0x5A, 0) == 0) {
            u16 yaw = GV(u16, e + 0x1C6);
            GV(u16, e + 0x1C6) = (yaw + 0x400) & 0xFFF;
            s32 left = (s32)zombie_call(n_zombie_WallAhead, e, 100, 0);
            GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) - 0x800) & 0xFFF;
            s32 right = (s32)zombie_call(n_zombie_WallAhead, e, 100, 0);
            GV(u16, e + 0x1C6) = yaw;
            s32 side = (s32)zombie_call(n_zombie_PlayerSide, e, 0, 0);
            dir = zombie_AvoidDirection(e, ahead, left, right, side);
        }
        ZW_AVOID = (s16)dir;
        if (dir == 1 || dir == 2) {
            u32 yaw = GV(u16, e + 0x1C6) & 0xFFF;
            GV(u16, e + 0x1C6) = (u16)(yaw + 0x1000);
            ZW_AVOID_YAW = (s16)(dir == 1 ? yaw + 0x1400 : yaw + 0xC00);
        }
    }
    switch (ZW_AVOID) {
    case 0:
        gcall(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (u32)(s32)EN_SPEED(e), 0);
        return;
    case 1:
    case 2:
        GV(u16, e + 0x1C6) += ZW_AVOID == 1 ? 0x16 : (u16)-0x16;
        if ((s32)zombie_call(n_zombie_WallAhead, e, 100, 0) == -1) {
            GV(u16, e + 0x1C6) &= 0xFFF;
            ZW_AVOID = 0;
        }
        return;
    }
}

/* 801010DC: an empty check (always 0), called before the death event. */
void n_zombie_NoDeathEvent(void) {
    FRAME(0x10);
    V0 = 0;
}

/* 801010EC (e): ease the body sway (+0x364) back to straight, 20 a frame. */
void n_zombie_EaseSway(void) {
    u32 e = A0;
    s32 v = GV(s32, e + 0x364);
    if (v == 0) return;
    if (v > 0) { v -= 20; GV(s32, e + 0x364) = v > 0 ? v : 0; }
    else { v += 20; GV(s32, e + 0x364) = v < 0 ? v : 0; }
}

/* 80101130 (e): sway the body (+0x364, limited to +-0x400) towards the
 * side the player is on, 20 a frame. */
void n_zombie_SwayToPlayer(void) {
    FRAME(0x38);
    u32 e = A0;
    s32 sway = GV(s32, e + 0x364);
    u32 rot = sp + 0x10, v = sp + 0x18;
    MEM_W32(rot + 0, GV(u32, e + 0x1C4));
    MEM_W32(rot + 4, GV(u32, e + 0x1C8));
    MEM_W16(rot + 2, MEM_R16(rot + 2) + (u32)sway);
    gcall(F_ROT_STEP, rot, v, 20, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) + (u32)EN_X(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    s32 step = (s32)gcall(FN_LINE_SIDE, EN_POS(e), v, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) > 0 ? -20 : 20;
    MEM_W16(rot + 2, (u32)step);
    if (step < 0 ? sway > -0x400 : sway < 0x400) GV(s32, e + 0x364) += step;
}

/* 8010122C (e, walk): the walking animation to blend into from the one
 * playing (some hit animations have their own way back), else `walk`. */
void n_zombie_WalkAnimFor(void) {
    u32 e = A0;
    s16 anim = EN_ANIM(e);
    switch (anim) {
    case 8:  V0 = 0x800E; return;
    case 9:  V0 = 0xE; return;
    case 12: V0 = (EN_FLAGS(e) & 0x80) ? 0x2F : 0x8010; return;
    case 13: case 19: case 21: V0 = 0x10; return;
    case 14: case 16: V0 = (u32)(s32)anim; return;
    case 23: V0 = 0x11; return;
    case 24: V0 = 0xF; return;
    case 47: V0 = 0x2F; return;
    default: V0 = A1; return;
    }
}

/* ------------------------------------------------------------------ */
/* Getting shot */

/* The hit (index 0..n) the reaction is based on: the last pair of hits
 * compared, the one with the smaller value. */
static u32 zombie_ReactionHit(u32 e) {
    s32 n = EN_NHITS(e);
    u32 pick = 0;
    for (s32 i = 0; i < n; i++)
        pick = EN_HIT_VAL(e, i + 1) < EN_HIT_VAL(e, i) ? (u32)(i + 1) : (u32)i;
    return pick;
}

/* 801012CC (e): the zombie was shot this frame: take the damage, pick the
 * reaction (or death) animation, and maybe lose limbs. */
void n_zombie_OnHit(void) {
    FRAME(0x20);
    u32 e = A0;
    EN_FLAGS(e) &= ~(u32)EF_SHOT;
    gcall(FN_EN_HIT_PRE, e, 0, 0, 0);
    s16 anim = EN_ANIM(e);
    if (anim < 13 && anim >= 11 && D_80062DEC == 0) D_80062DEC = 1;   /* hit while grabbing: free the player */
    if (EN_HP(e) > 0) {
        s16 state = EN_STATE(e), action = EN_ACTION(e);
        EN_STATE(e) = 2;
        EN_ACTION(e) = 6;
        if (zombie_call(n_zombie_ApplyDamage, e, 0, 0) != 0) {
            if ((en_rand() & 0xFF) < 0x99) zombie_call(n_zombie_Voice, e, 0, 0);
            u32 a = zombie_call(n_zombie_PickHitReaction, e, 0, 0);
            if (a & 0x8000) {                 /* no knock-back: carry on */
                EN_STATE(e) = state;
                EN_ACTION(e) = action;
            } else if (a == (u32)(s32)EN_ANIM(e)) {
                gcall(FN_EN_SET_ACTION, e, (u32)(s32)EN_ACTION(e), a, 0);
            } else {
                gcall(FN_EN_SET_ACTION_B, e, (u32)(s32)EN_ACTION(e), a, 4);
            }
        } else {
            u32 a = zombie_call(n_zombie_PickFatalReaction, e, 0, 0);
            if (a & 0x8000) gcall(FN_EN_SET_ACTION_B, e, (u32)(s32)EN_ACTION(e), a & 0x7FFF, 4);
            else gcall(FN_EN_SET_ACTION, e, (u32)(s32)EN_ACTION(e), a, 0);
        }
    }
    switch (EN_TYPE(e)) {
    case 0: case 1: case 2: case 8: case 9:
        zombie_call(n_zombie_LoseLimbs, e, ZOMBIE_LIMBS_A, 0); break;
    case 4: case 5: case 6:
        zombie_call(n_zombie_LoseLimbs, e, ZOMBIE_LIMBS_B, 0); break;
    case 10:
        zombie_call(n_zombie_LoseLimbs, e, ZOMBIE_LIMBS_C, 0); break;
    }
}

/* 80101474 (e): the reaction to a hit it survives: an animation from
 * ZOMBIE_REACTIONS by where it was hit (upper/lower body, front/back,
 * 4 groups) with the push of the shot by distance; or the playing
 * animation | 0x8000 to stay in it (0x20 while +0x320 & 0x1000). */
void n_zombie_PickHitReaction(void) {
    FRAME(0x20);
    u32 e = A0;
    if (EN_FLAGS(e) & 0x1000) { V0 = 0x20; return; }
    s16 anim = EN_ANIM(e);
    s32 group, keep;
    switch (anim) {
    case 6: case 8: case 12: case 21:
        V0 = (u32)(s32)EN_ANIM(e); return;
    case 13:
        V0 = 0x19; return;
    case 7: case 10: case 11:
        group = 4; keep = 0; break;
    case 0: case 1: case 2: case 3: case 4: case 5: case 20: case 27: case 28:
    case 31: case 38: case 39: case 40: case 41: {
        u32 r = en_rand() & 0xFF;
        if (r == 0) group = 4;
        else if (r < 0x15) group = 0;
        else if (r < 0x1A) group = 2;
        else if (r < 0x1F) group = 3;
        else group = 4;
        keep = 2;
        break;
    }
    default:
        group = 4; keep = EN_ANIM(e); break;
    }
    s32 k = group * 4;
    u32 hit = zombie_ReactionHit(e);
    g_lastHit = (s32)hit;
    s16 kind = EN_BODY_KIND(e, EN_HIT_PART(e, hit));
    if (kind >= 7 && kind < 11) k += 2;             /* legs */
    u8 side = EN_HIT_SIDE(e, hit);
    if (side >= 2 && side <= 5) k += 1;             /* from behind */
    if (k < 16) {
        ZW_KNOCK = 0;
        s32 d = (s32)gcall(FN_EN_DIST_UPDATE, e, 0, 0, 0);
        if (d >= 10001) ZW_KNOCK_FORCE = 200;
        else ZW_KNOCK_FORCE = (s32)gcall(FN_EN_DIST_UPDATE, e, 0, 0, 0) < 5001 ? 1000 : 500;
        GV(s32, e + 0x368) = 0;
        V0 = GV(u32, ZOMBIE_REACTIONS + (u32)k * 4);
    } else {
        ZW_KNOCK_FORCE = 0;
        GV(s32, e + 0x368) = GV(u32, ZOMBIE_REACTIONS + (u32)k * 4) ? 0x180 : -0x180;
        V0 = (u32)keep | 0x8000;
    }
}

/* 80101700 (e): the animation for a hit that kills (| 0x8000: blend over
 * 4 frames), by the animation playing, then by the body part and side hit.
 * A killing shot to body part kind 0 makes it collapse (state 1,
 * action 9). */
void n_zombie_PickFatalReaction(void) {
    FRAME(0x20);
    u32 e = A0;
    switch (EN_ANIM(e)) {
    case 8: case 18: case 19: case 37: case 47: V0 = 0x8009; return;
    case 9: case 21: V0 = (u32)(s32)EN_ANIM(e); return;
    case 12: case 16: case 17: case 23: V0 = 0xD; return;
    case 13: V0 = 0x1A; return;
    case 14: case 15: case 24: V0 = 9; return;
    case 35: case 36: V0 = 0x800D; return;
    default: break;
    }
    u32 hit = zombie_ReactionHit(e);
    g_lastHit = (s32)hit;
    s16 kind = EN_BODY_KIND(e, EN_HIT_PART(e, hit));
    switch (kind) {
    case 0:
        EN_STATE(e) = 1;
        EN_ACTION(e) = 9;
        V0 = 3;
        return;
    case 1: {
        u8 side = EN_HIT_SIDE(e, hit);
        if (side >= 8) break;
        V0 = (side >= 2 && side <= 5) ? 0xC : 8;
        return;
    }
    case 3: case 4: {
        if ((en_rand() & 0xFF) >= 0x20) { V0 = 0x800B; return; }
        u8 side = EN_HIT_SIDE(e, hit);
        if (side >= 8) break;
        int back = side >= 2 && side <= 5;
        zombie_call(n_zombie_Sound, e, 0xE, 0);
        int lo = (en_rand() & 0xFF) < 0xCC;
        V0 = (lo != back) ? 0x8006 : 0x800A;
        return;
    }
    case 2: case 7: case 8: case 9: case 10:
        V0 = 0x800B;
        return;
    default:
        break;
    }
    V0 = (u32)(s32)EN_ANIM(e);
}

/* 80101948 (e): grab the player (action 14) when close and allowed. */
void n_zombie_TryGrab(void) {
    FRAME(0x20);
    u32 e = A0;
    if (ep_grabLock != 0 || g_gameMode != 1) return;
    if (EN_DIST(e) >= 1001) return;
    if (EN_FLAGS(e) & EF_SEES) return;
    if (g_grabCooldown != 0) return;
    if (zombie_call(n_zombie_CanGrab, e, 0, 0) == 0) return;
    gcall(FN_EN_SET_ACTION_B, e, 0xE, (u32)(s32)EN_ANIM(e), 4);
    g_grabCooldown = 0xF0;
}

/* 801019F0 (e): standing: start walking (action 4) when one of the
 * type's wake-up conditions (parameter 1, a bit each) holds: 0 shot,
 * 1 the player fires, 2 a timer (parameter 5) ran out, 3 the player came
 * within parameter 4, 4 the player came into view, 5 its turn of eight
 * frames, 6 the player runs, 7 event flag 0xB8. Returns 1 if it started. */
void n_zombie_IdleThink(void) {
    FRAME(0x30);
    u32 e = A0;
    for (s16 k = 0; k < 8; k++) {
        s16 mask = (s16)gcall(FN_EN_PARAM, e, 1, 0, 0);
        if (!((mask >> k) & 1)) continue;
        int go = 0;
        int myturn = (ep_frameCount & 7) == (u32)ZW_GROUP;
        switch (k) {
        case 0: go = (EN_FLAGS(e) & EF_SHOT) != 0; break;
        case 1: go = g_fireRequest != 0 && g_reticleX >= 0 && g_reticleY >= 0; break;
        case 2: {
            s16 t = EN_TIMER(e);
            EN_TIMER(e) = (s16)(t + 1);
            go = !(t < (s16)gcall(FN_EN_PARAM, e, 5, 0, 0));
            break;
        }
        case 3:
            if (!myturn) break;
            go = EN_DIST(e) < (s16)gcall(FN_EN_PARAM, e, 4, 0, 0);
            break;
        case 4:
            if (!myturn || !(EN_FLAGS(e) & EF_SEES)) break;
            go = (s32)gcall(FN_SEGMENT_HIT, EN_POS(e), PLAYER_POS_ADDR, 0, 0) >= 0;
            break;
        case 5: go = myturn; break;
        case 6: go = myturn && g_playerMoveSpeed < -0x50; break;
        case 7: go = gcall(FN_FLAG_TEST, ep_eventFlags, 0xB8, 0, 0) != 0; break;
        }
        if (!go) continue;

        if (EN_ANIM(e) == 0x17) zombie_call(n_zombie_VoiceAt, e, 0xA, 3);
        if (EN_ANIM(e) == 0) {
            u32 a = zombie_call(n_zombie_WalkAnim, e, (en_rand() & 1) | 2, 0);
            gcall(FN_EN_SET_ACTION_B, e, 4, a, 4);
        } else {
            u32 a = zombie_call(n_zombie_WalkAnim, e, (en_rand() & 1) | 2, 0);
            a = zombie_call(n_zombie_WalkAnimFor, e, a, 0);
            gcall(FN_EN_SET_ACTION, e, 4, a, 0);
        }
        EN_TIMER(e) = 0;
        GV(s32, e + 0x370) = (s32)(en_rand() & 0xFF) + 0x96;   /* next moan */
        V0 = 1;
        return;
    }
    V0 = 0;
}

/* 80101CE4 (e, walk): the type's own walking animation (+0x848 -> +0x14)
 * if it has one (1-5, 22, 38-41), else `walk`. */
void n_zombie_WalkAnim(void) {
    u32 t = GV(u32, A0 + 0x848);
    u16 a = GV(u16, t + 0x14);
    if ((a >= 1 && a <= 5) || a == 22 || (a >= 38 && a <= 41)) V0 = (u32)(s32)GV(s16, t + 0x14);
    else V0 = A1;
}

/* 80101D44 (e): walking: stop (or change gait) when one of the type's
 * conditions (parameter 2) holds: 0 lost sight of the player, 1 a timer
 * (parameter 6) ran out, 2 the player is farther than parameter 4; or
 * always while +0x320 & 0x800. Parameter 3 is the action to go to.
 * Returns 1 if it changed. */
void n_zombie_WalkThink(void) {
    FRAME(0x20);
    u32 e = A0;
    int go = 0;
    for (s16 k = 0; k < 3 && !go; k++) {
        s16 mask = (s16)gcall(FN_EN_PARAM, e, 2, 0, 0);
        if (!((mask >> k) & 1)) continue;
        if (k == 0) go = !(EN_FLAGS(e) & EF_SEES);
        else if (k == 1) {
            s16 t = EN_TIMER(e);
            EN_TIMER(e) = (s16)(t + 1);
            go = !(t < (s16)gcall(FN_EN_PARAM, e, 6, 0, 0));
        } else {
            s32 d = EN_DIST(e);
            go = (s16)gcall(FN_EN_PARAM, e, 4, 0, 0) < d;
        }
    }
    if (!go && !(EN_FLAGS(e) & 0x800)) { V0 = 0; return; }

    s16 next = (s16)gcall(FN_EN_PARAM, e, 3, 0, 0);
    if (next == 0) {
        u32 a = zombie_call(n_zombie_WalkAnimFor, e, 0, 0);
        gcall(FN_EN_SET_ACTION, e, 0, a, 0);
    } else if (next > 0 && next < 4) {
        u32 walk;
        if (EN_TYPE(e) == 9) {
            static const u8 crawl[4] = {0x26, 0x27, 0x28, 0x28};
            walk = crawl[en_rand() & 3];
        } else {
            walk = (en_rand() & 1) | 2;
        }
        u32 a = zombie_call(n_zombie_WalkAnim, e, walk, 0);
        a = zombie_call(n_zombie_WalkAnimFor, e, a, 0);
        gcall(FN_EN_SET_ACTION, e, (u32)(s32)next, a, 0);
    }
    EN_TIMER(e) = 0;
    V0 = 1;
}

/* 80101F88 (e, part, kind, table): knock off every piece of the limb
 * table (entries {kind, flag offset, model}, ends with kind < 0) of this
 * kind: clear its flag byte in the record, play the sound, show the
 * falling piece. Returns how many. */
void n_zombie_DropLimb(void) {
    FRAME(0x30);
    u32 e = A0, part = A1, kind = A2, t = A3;
    u32 n = 0;
    for (; GV(s32, t) >= 0; t += 12) {
        if (GV(u32, t) != kind) continue;
        n++;
        GV(u8, e + GV(u32, t + 4)) = 0;
        zombie_call(n_zombie_Sound, e, 0xE, 0);
        gcall5(sp, FN_EN_DRAW, EN_BODY_OBJ(e, part), 0, 0, 0, GV(u32, t + 8));
    }
    V0 = n;
}

static u32 zombie_DropLimbCall(u32 e, u32 part, u32 kind, u32 table) {
    A0 = e; A1 = part; A2 = kind; A3 = table;
    n_zombie_DropLimb();
    return V0;
}

/* 80102054 (e, table): shots may take off limbs: part kind 3 (arms) and 4
 * on a 1/8 chance pieces 5/6, else 1/16 the whole limb (3/4); kind 0
 * (the head) always, unless already gone. Clears the hit list. */
void n_zombie_LoseLimbs(void) {
    FRAME(0x30);
    u32 e = A0, table = A1;
    for (s32 i = 0; i <= EN_NHITS(e); i++) {
        if (EN_HIT_DMG(e, i) == 0) continue;
        u32 part = EN_HIT_PART(e, i);
        s16 kind = EN_BODY_KIND(e, part);
        s32 piece;                       /* > 0: a piece (keeps the limb); < 0: the limb */
        if (kind == 0) {
            if (ZW_HEAD_OFF != 0) continue;
            piece = -1 - 0;              /* limb kind 0 */
        } else if (kind == 3 || kind == 4) {
            if ((en_rand() & 0xFF) < 0x20) piece = kind == 3 ? 5 : 6;
            else if ((en_rand() & 0xFF) < 0x10) piece = -1 - kind;
            else continue;
        } else {
            continue;
        }
        if (piece > 0) {
            if (zombie_DropLimbCall(e, part, (u32)piece, table) != 0)
                GV(s16, EN_BODY(e, part) + 0x396) = 0x140;
        } else {
            if (zombie_DropLimbCall(e, part, (u32)(-1 - piece), table) != 0)
                EN_BODY_KIND(e, part) = -1;
        }
        zombie_call(n_zombie_Sound, e, 0xE, 0);
    }
    EN_NHITS(e) = -1;
}

/* 80102224 (e): take this frame's hits off the hit points; parts of kind
 * 3/4 still attached soften them (zombie_ArmourBonus). A hit on the head
 * (part kind 0), or a hit leaving 0 or less, kills: state 3, action 7.
 * Returns 1 if it survives. */
void n_zombie_ApplyDamage(void) {
    FRAME(0x40);
    u32 e = A0;
    if (EN_NHITS(e) < 0) { V0 = 1; return; }
    for (s32 k = 0;; ) {
        u32 part = EN_HIT_PART(e, k);
        if (EN_BODY_KIND(e, part) == 0) {
            zombie_call(n_zombie_Sound, e, 0xE, 0);
            zombie_call(n_zombie_VoiceAt, e, 6, 1);
            EN_HP(e) = -1;
            break;
        }
        u32 dmg = sp + 0x10;
        GS(u16, dmg) = (u16)EN_HIT_DMG(e, k);
        u32 r = zombie_call(n_zombie_ArmourBonus, e, dmg, 0);
        u16 d = GS(u16, dmg);
        EN_HIT_DMG(e, k) = (s16)d;
        if (EN_BODY_KIND(e, part) == 1 && (r & 0xFFFF) == 0) {
            EN_HP(e) = -1;
            zombie_call(n_zombie_VoiceAt, e, 6, 1);
            break;
        }
        EN_HP(e) = (s16)((u16)EN_HP(e) - d);
        if (EN_HP(e) > 0) {
            k++;
            if (EN_NHITS(e) < k) { V0 = 1; return; }
            continue;
        }
        if (EN_BODY_KIND(e, part) == 1) EN_HP(e) = -1;
        zombie_call(n_zombie_VoiceAt, e, 6, 1);
        break;
    }
    EN_STATE(e) = 3;
    EN_ACTION(e) = 7;
    V0 = 0;
}

/* 801023C8 (e, &damage): with fewer than two body parts of kind 3/4,
 * the damage grows by 35% per missing one. Returns how many it has. */
void n_zombie_ArmourBonus(void) {
    u32 e = A0, pd = A1;
    s32 n = 0;
    for (u32 p = e; GV(s16, p + 0x392) >= 0; p += 0x5C)
        if ((u32)GV(u16, p + 0x39A) - 3 < 2) n++;
    u32 d = MEM_R16(pd);
    for (s32 i = n; i < 2; i++) d = (u32)((s32)(s16)d * 135 / 100);
    MEM_W16(pd, d);
    V0 = (u32)(s32)(s16)n;
}

/* ------------------------------------------------------------------ */
/* Actions (pairs: every frame / while the phase is negative) */

/* 80102468 (e): action 0, standing: wait for a reason to walk. */
void n_zombie_IdlePre(void) {
    FRAME(0x18);
    n_zombie_IdleThink();
}

/* 80102488: action 0, nothing more. */
void n_zombie_IdleStep(void) {
}

/* 801027C0 (e): action 4, walking: turn towards the player, or along the
 * wall in the way; maybe stop; sway; grab when close. */
void n_zombie_WalkPre(void) {
    FRAME(0x18);
    u32 e = A0;
    if ((s32)zombie_call(n_zombie_CrossedWall, EN_POS(e), PLAYER_POS_ADDR, 0) >= 0)
        zombie_call(n_zombie_AvoidWalls, e, 0, 0);
    else
        gcall(FN_EN_TURN_TO_PLAYER, e, 0, 0, 0);
    zombie_call(n_zombie_WalkThink, e, 0, 0);
    zombie_call(n_zombie_SwayToPlayer, e, 0, 0);
    zombie_call(n_zombie_TryGrab, e, 0, 0);
}

/* 80102828 (e): action 4: moan now and then; close to the player,
 * decide once whether to lunge; by distance band (1: 3000-5000,
 * 2: 1400-1600, 3: 1000-1400, 4: closer) maybe reach out (once per band,
 * +0x35C bits), or after 16 frames in view, grab. */
void n_zombie_WalkStep(void) {
    FRAME(0x20);
    u32 e = A0;
    s32 t = GV(s32, e + 0x370) - 1;
    GV(s32, e + 0x370) = t;
    if (t < 0) {
        if (!(EN_FLAGS(e) & EF_SEES) || (en_rand() & 0xFF) < 0x7F)
            zombie_call(n_zombie_VoiceAt, e, 0xA, 3);
        GV(s32, e + 0x370) = (s32)(en_rand() & 0xFF) + 0xB4;
    }
    if (EN_DIST(e) < 0x5DD) {
        if (ZW_LUNGE == 0) ZW_LUNGE = (en_rand() & 0xFF) < 0x3F ? 1 : 2;
        if (ZW_LUNGE == 1) {
            s16 a = EN_ANIM(e);
            if (a >= 0 && (a < 4 || (a < 0x2A && a >= 0x26))) gcall(FN_EN_SET_ANIM, e, 4, 0, 0);
        }
    }
    switch (EN_STEP(e)) {
    case 0:
        ZW_AVOID = 0;
        GV(s32, e + 0x358) = 0;
        GV(s32, e + 0x35C) = 0;
        GV(s32, e + 0x360) = 0;
        EN_STEP(e) = 1;
        /* fall through */
    case 1: break;
    default: return;
    }
    s32 d = EN_DIST(e);
    if (d < 5001 && d >= 3001) { GV(s32, e + 0x358) = 1; GV(u32, e + 0x35C) &= ~0x17u; }
    else if (d < 1601 && d >= 1401) { GV(s32, e + 0x358) = 2; GV(u32, e + 0x35C) &= ~0x1Bu; }
    else if (d < 1401 && d >= 1001) { GV(s32, e + 0x358) = 3; GV(u32, e + 0x35C) &= ~0x1Du; }
    else if (d < 1001) { GV(s32, e + 0x358) = 4; GV(u32, e + 0x35C) &= ~0x1Eu; }

    s32 band = GV(s32, e + 0x358);
    u32 seen = GV(u32, e + 0x35C);
    switch (band) {
    case 1:
        if (seen & 8) return;
        GV(u32, e + 0x35C) = seen | 8;
        if ((en_rand() & 0xFF) < 0xC && zombie_call(n_zombie_PlayerInConeCached, e, 0x28, 0) != 0)
            gcall(FN_EN_SET_ACTION_B, e, 0xC, 0x23, 4);
        return;
    case 2:
    case 3: {
        u32 bit = band == 2 ? 4 : 2;
        if (seen & bit) return;
        GV(u32, e + 0x35C) = seen | bit;
        if ((en_rand() & 0xFF) < (band == 2 ? 0x14u : 0x33u) &&
            zombie_call(n_zombie_PlayerInConeCached, e, 0x28, 0) != 0)
            gcall(FN_EN_SET_ACTION_B, e, 0xB, 0x1C, 2);
        return;
    }
    case 4:
        if (!(EN_FLAGS(e) & EF_SEES)) GV(s32, e + 0x360) = 0;
        GV(s32, e + 0x360) += 1;
        if (GV(s32, e + 0x360) < 16) return;
        if (zombie_call(n_zombie_CanGrab, e, 0, 0) != 0) {
            gcall(FN_EN_GRAB_PLAYER, e, 6, 0, 0);
            gcall(FN_EN_GRAB_START, e, 0, 0, 0);
            zombie_call(n_zombie_Voice, e, 2, 0);
            zombie_call(n_zombie_Sound, e, 8, 0);
            gcall(FN_EN_SET_ACTION, e, 5, (u32)(s32)EN_ANIM(e), 0);
        }
        if (g_gameMode == 3) {
            EN_STATE(e) = 4;
            gcall(FN_EN_SET_ACTION, e, 0, 0x17, 0);
        }
        return;
    }
}

/* 80102C30: action 5, nothing every frame. */
void n_zombie_GrabPre(void) {
}

/* 80102C38 (e): action 5, grabbing the player: hold on, step towards
 * them (push-out by 0x80081FF0), then face them. */
void n_zombie_GrabStep(void) {
    FRAME(0x38);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0: {
        EN_FLAGS(e) |= EF_GRABBING;
        if (EN_ANIM(e) == 0x2F) gcall(FN_EN_SET_ANIM, e, 0x24, 0, 0);
        else gcall(FN_EN_SET_ANIM_B, e, 0x1B, 2, 0);
        u32 pos = sp + 0x10, out = sp + 0x20;
        GS(s32, pos + 0) = EN_X(e);
        GS(s32, pos + 4) = EN_Y(e);
        GS(s32, pos + 8) = EN_Z(e);
        if (gcall(0x80081FF0u, PLAYER_POS_ADDR, pos, out, 0) != 0) {
            EN_VX(e) += GS(s32, out + 0);
            EN_VZ(e) += GS(s32, out + 8);
        }
        EN_STEP(e) += 1;
        return;
    }
    case 1: {
        s16 turn = (s16)gcall(FN_ANGLE_TO, EN_OBJ(e), PLAYER_POS_ADDR, 0, 0);
        GV(u16, e + 0x1C6) -= (u16)turn;
        EN_STEP(e) += 1;
        return;
    }
    }
}

/* 80102D54 (e): action 6, every frame: sway. */
void n_zombie_KnockPre(void) {
    FRAME(0x18);
    n_zombie_SwayToPlayer();
}

/* Knock-back movement: the push vector (+0x378) times the frames left;
 * returns 1 when it ran out. */
static int zombie_KnockMove(u32 e) {
    u32 w = ZW;
    EN_VX(e) += (s32)((u32)GV(s32, e + 0x378) * (u32)GV(s32, w + 0x14));
    EN_VZ(e) += (s32)((u32)GV(s32, e + 0x380) * (u32)GV(s32, w + 0x14));
    s32 t = GV(s32, w + 0x14) - 2;
    GV(s32, w + 0x14) = t;
    if (t > 0) return 0;
    GV(s32, w + 0x14) = 0;
    GV(s32, w + 0x10) = 0;
    return 1;
}

/* 80102D74 (e): action 6, knocked back by a shot: set up the push by the
 * reaction animation, slide back, then walk again (or fall). */
void n_zombie_KnockStep(void) {
    FRAME(0x20);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0: {
        s16 step = 2;
        u16 a = (u16)EN_ANIM(e);
        u32 p;
        switch ((s16)a) {
        case 6: case 10: ZW_KNOCK += ZW_KNOCK_FORCE / 2; step = EN_STEP(e) + 1; break;
        case 7: case 11: ZW_KNOCK += ZW_KNOCK_FORCE; step = EN_STEP(e) + 1; break;
        case 8: case 12: ZW_KNOCK += ZW_KNOCK_FORCE * 6 / 5; step = EN_STEP(e) + 1; break;
        case 15:
        case 17:
            p = EN_ANIMPART(e, 0);
            if ((s32)((u32)GV(u16, p + 0x16) - GV(u16, p + 0x18)) >= ((s16)a == 15 ? 0x78 : 0x98) &&
                GV(s32, e + 0x368) == 0)
                GV(s32, e + 0x368) = -0x180;
            break;
        case 23: GV(s32, e + 0x368) = -0x180; break;
        case 32: ZW_KNOCK = 500; step = EN_STEP(e) + 1; break;
        default: break;
        }
        EN_STEP(e) = step;
        if (ZW_KNOCK_TIME == 0) ZW_KNOCK_TIME = 20;
        u8 side = EN_HIT_SIDE(e, g_lastHit);
        if (side < 8) {
            s32 push = ZW_KNOCK / 100;
            if (side >= 2 && side <= 5) push = -push;
            gcall(FN_ROT_VELOCITY, EN_ROT(e), e + 0x378, (u32)push, 0);
        }
        EN_TIMER(e) = 0;
        return;
    }
    case 1:
        if (zombie_KnockMove(e)) {
            EN_VZ(e) = 0;
            EN_VX(e) = 0;
            EN_STEP(e) += 1;
        }
        if (zombie_call(n_zombie_AnimEvent, e, 1, 1) == 0) return;
        /* fall through */
    case 2:
        ZW_KNOCK_TIME = 0;
        ZW_KNOCK = 0;
        if (EN_ANIM(e) == 0x20) {
            u32 p = EN_ANIMSTATE(e);
            if ((u32)GV(u16, p + 0x16) == GV(u32, ZW_FRAMES + 0x80) + 0xA && (en_rand() & 0xFF) < 0xCC) {
                gcall(FN_EN_SET_ANIM_B, e, 8, 4, 0);
                ZW_KNOCK_TIME = 20;
                EN_STEP(e) += 1;
                return;
            }
        }
        if (zombie_call(n_zombie_AnimEvent, e, 1, 1) == 0) return;
        EN_FLAGS(e) &= ~0x1000u;
        GV(s32, e + 0x374) = 0;
        if (EN_ANIM(e) == 0x19 || EN_ANIM(e) == 0x2F) {
            EN_STATE(e) = 1;
            gcall(FN_EN_SET_ACTION, e, 4, 0x10, 0);
            zombie_call(n_zombie_Sound, e, 2, 0);
        } else {
            GV(s32, e + 0x374) = 1;
            EN_STATE(e) = 1;
            u32 a = zombie_call(n_zombie_WalkAnim, e, (en_rand() & 1) | 2, 0);
            a = zombie_call(n_zombie_WalkAnimFor, e, a, 0);
            gcall(FN_EN_SET_ACTION, e, 4, a, 0);
        }
        return;
    case 3:
        if (zombie_call(n_zombie_AnimEvent, e, 1, 1) != 0) {
            EN_STATE(e) = 1;
            EN_FLAGS(e) &= ~0x1000u;
            gcall(FN_EN_SET_ACTION, e, 4, 0xE, 0);
            zombie_call(n_zombie_Sound, e, 2, 0);
        }
        zombie_KnockMove(e);
        return;
    }
}

/* 801032AC (e): action 7, every frame: straighten up. */
void n_zombie_DiePre(void) {
    FRAME(0x18);
    n_zombie_EaseSway();
}

/* 801032CC (e): action 7, dying: count the kill, fall (maybe on the
 * other side), then lie still (+0x320 bits 1, 2, 4, 0x4000). */
void n_zombie_DieStep(void) {
    FRAME(0x20);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        if (zombie_call(n_zombie_NoDeathEvent, e, 0, 0) == 0) {
            EN_HP(e) = -1;
            if (ZW_KILLED == 0) gcall(FN_EN_KILLED, e, (u32)(s32)EN_ROOMID(e), 0, 0);
            gcall5(sp, FN_EN_DRAW, e, 1, 0, 0, 2);
        }
        EN_FLAGS(e) |= 4;
        EN_STEP(e) += 1;
        return;
    case 1:
        if (zombie_call(n_zombie_AnimEvent, e, 1, 1) == 0) return;
        if (EN_ANIM(e) == 0x19) gcall(FN_EN_SET_ANIM, e, 0x1A, 0, 0);
        else zombie_call(n_zombie_Sound, e, 0, 0);
        EN_STEP(e) += 1;
        return;
    case 2:
        EN_STEP(e) += 1;
        switch (EN_ANIM(e)) {
        case 0xC: case 0x13:
            if ((en_rand() & 0xFF) < 0x7F) gcall(FN_EN_SET_ANIM_B, e, 0x13, 2, 0);
            break;
        case 0x8: case 0x12:
            if ((en_rand() & 0xFF) < 0x7F) gcall(FN_EN_SET_ANIM_B, e, 0x12, 2, 0);
            break;
        }
        EN_FLAGS(e) |= 2;
        return;
    case 3:
        EN_FLAGS(e) |= 0x4001;
        EN_STEP(e) += 1;
        return;
    }
}

/* 80103510: action 9, nothing every frame. */
void n_zombie_CollapsePre(void) {
}

/* 80103518 (e): action 9, collapsing (killed by a shot to body part kind
 * 0): counted as killed, keeps moving for 60 frames, then dies. */
void n_zombie_CollapseStep(void) {
    FRAME(0x18);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        gcall(FN_EN_KILLED, e, (u32)(s32)EN_ROOMID(e), 0, 0);
        ZW_KILLED = 1;
        EN_HP(e) = -1;
        EN_TIMER(e) = 0x3C;
        GV(s32, e + 0x35C) = 0;
        EN_STEP(e) += 1;
        /* fall through */
    case 1:
        EN_TIMER(e) -= 1;
        if (EN_TIMER(e) == 0) {
            EN_STATE(e) = 3;
            gcall(FN_EN_SET_ACTION, e, 7, GV(s32, e + 0x35C) != 0 ? 8 : 0x15, 0);
        }
        gcall(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (u32)(s32)EN_SPEED(e), 0);
        return;
    }
}

/* 80103F9C: action 14, nothing every frame. */
void n_zombie_BitePre(void) {
}

/* 80103FA4 (e): action 14, biting the player: lock the player, turn to
 * them, hurt them; when the player is free again, turn away and walk. */
void n_zombie_BiteStep(void) {
    FRAME(0x28);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if (step == 0) {
        D_80062DEC = 0;
        EN_FLAGS(e) |= EF_GRABBING;
        gcall(FN_SOUND_CUE2, 0, 8, 0x7F, 0x7F);
        zombie_call(n_zombie_Sound, e, 2, 0);
        g_biteYaw = (s16)gcall(FN_ANGLE_TO, EN_OBJ(e), PLAYER_POS_ADDR, 0, 0);
        GV(u16, e + 0x1C6) -= GV(u16, 0x80105FC4);
        gcall(FN_EN_SET_ANIM_B, e, 4, 2, 0);
        gcall5(sp, FN_PLAYER_BITTEN, e, 0, (u32)EN_DIST(e), 2, 1);
        EN_STEP(e) += 1;
    } else if (step == 1) {
        if (D_800A8670 != 0) return;
        GV(u16, e + 0x1C6) = (GV(u16, 0x800A8606) + 0x800) & 0xFFF;
        u32 a = zombie_call(n_zombie_WalkAnim, e, (en_rand() & 1) | 2, 0);
        gcall(FN_EN_SET_ACTION_B, e, 4, a, 4);
        D_80062DEC = (u8)step;
        EN_FLAGS(e) &= ~(u32)EF_GRABBING;
    }
}

/* ------------------------------------------------------------------ */
/* States */

/* Run the current action's handlers: the every-frame one, then the other
 * while the phase is negative (each read afresh: handlers change them). */
static void zombie_RunAction(u32 e) {
    gcall(GV(u32, ZOMBIE_ACT_PRE + (u32)((s32)EN_ACTION(e) << 2)), e, 0, 0, 0);
    if (EN_PHASE(e) < 0)
        gcall(GV(u32, ZOMBIE_ACT_STEP + (u32)((s32)EN_ACTION(e) << 2)), e, 0, 0, 0);
}

/* At the end of the animation, stop moving. */
static void zombie_StopAtAnimEnd(u32 e) {
    u32 p = EN_ANIMSTATE(e);
    if (GV(u16, p + 0x14) == GV(u16, p + 0xE)) {
        EN_VZ(e) = 0;
        EN_VX(e) = 0;
    }
}

/* 80104108 (e): state 1, active. Getting-up and turning animations
 * (10-17) play out first; otherwise run the action, redraw when it moved
 * or turned, and make footstep sounds. */
void n_zombie_StateActive(void) {
    FRAME(0x28);
    u32 e = A0;
    switch (EN_ANIM(e)) {
    case 10: case 11:
        if (zombie_call(n_zombie_AnimEvent, e, 1, 1) != 0)
            gcall(FN_EN_SET_ANIM, e, zombie_call(n_zombie_WalkAnim, e, 2, 0), 0, 0);
        break;
    case 14:
        if (EN_FLAGS(e) & 8) {
            gcall(FN_EN_SET_ANIM_B, e, 0xF, 2, 0);
            if (EN_FLAGS(e) & 8) gcall(FN_EN_SET_ANIM_B, e, 0x11, 2, 0);
        }
        break;
    case 15: case 17:
        if (EN_FLAGS(e) & 8)
            gcall(FN_EN_SET_ANIM, e, zombie_call(n_zombie_WalkAnim, e, 2, 0), 0, 0);
        break;
    case 16:
        if (EN_FLAGS(e) & 8) gcall(FN_EN_SET_ANIM_B, e, 0x11, 2, 0);
        break;
    default: {
        u16 yaw = GV(u16, e + 0x1C6);
        zombie_RunAction(e);
        if (EN_VX(e) != 0 || EN_VY(e) != 0 || EN_VZ(e) != 0 || EN_YAW(e) != (s16)yaw)
            gcall5(sp, FN_EN_DRAW, e, 0, 0, 0, 2);
        zombie_call(n_zombie_StepSound, e, (u32)(s32)(s16)yaw, 0);
        break;
    }
    }
    zombie_StopAtAnimEnd(e);
}

/* 801042E0 (e): state 2, hit: run the action, redraw. */
void n_zombie_StateHit(void) {
    FRAME(0x20);
    u32 e = A0;
    zombie_RunAction(e);
    gcall5(sp, FN_EN_DRAW, e, 0, 0, 0, 2);
    zombie_StopAtAnimEnd(e);
}

/* 80104398 (e): state 3, dying: run the death action (animations 8, 9,
 * 12, 13, 18, 19, 21, 26; 11 while it still has hit points), or chain
 * the falling animations on their event frames. */
void n_zombie_StateDying(void) {
    FRAME(0x18);
    u32 e = A0;
    GV(s32, e + 0x368) = 0;
    s16 anim = EN_ANIM(e);
    switch (anim) {
    case 8: case 9: case 12: case 13: case 18: case 19: case 21: case 26:
        zombie_RunAction(e);
        break;
    case 11:
        if (EN_HP(e) > 0) { zombie_RunAction(e); break; }
        if (GV(s32, ZW_FRAMES + (u32)((s32)EN_ANIM(e) << 2)) + 0xF <
            (s32)GV(u16, EN_ANIMSTATE(e) + 0x16))
            gcall(FN_EN_SET_ANIM_B, e, 0xC, 8, 0);
        break;
    case 14:
        if (zombie_call(n_zombie_AnimEvent, e, 1, 1) != 0) gcall(FN_EN_SET_ANIM, e, 9, 0, 0);
        break;
    case 16:
        if (zombie_call(n_zombie_AnimEvent, e, 1, 1) != 0) gcall(FN_EN_SET_ANIM, e, 0xD, 0, 0);
        break;
    default:
        if (zombie_call(n_zombie_AnimEvent, e, 1, 1) != 0) gcall(FN_EN_SET_ANIM, e, 0xC, 0, 0);
        break;
    }
    zombie_StopAtAnimEnd(e);
}

/* 80104534 (e): state 4: walked about with the d-pad (hit points 300-500)
 * or the shape buttons (over 500) of the upper button bits. */
void n_zombie_StatePad(void) {
    FRAME(0x20);
    u32 e = A0;
    s16 hp = EN_HP(e);
    if (hp < 300) return;
    u32 rot = sp + 0x10;
    GS(u16, rot + 0) = GV(u16, e + 0x1C4);
    GS(u16, rot + 2) = GV(u16, e + 0x1C6);
    GS(u16, rot + 4) = GV(u16, e + 0x1C8);
    u32 b = ep_buttonsHi;
    u32 up, down, right, left;
    if (hp < 501) { up = 0x1000; down = 0x4000; right = 0x2000; left = 0x8000; }
    else { up = 0x10; down = 0x40; right = 0x20; left = 0x80; }
    s32 speed = EN_SPEED(e);
    int go = 1;
    if (b & up) {
    } else if (b & down) {
        speed = -speed;
    } else if (b & right) {
        GS(u16, rot + 2) = (u16)(GV(u16, e + 0x1C6) + 0x400);
    } else if (b & left) {
        GS(u16, rot + 2) = (u16)(GV(u16, e + 0x1C6) - 0x400);
    } else {
        go = 0;
    }
    if (go) gcall(FN_ROT_VELOCITY, rot, EN_VEL(e), (u32)speed, 0);
}

/* 80104660 (e, old yaw): every 32 frames while moving, a shuffling
 * sound (a different one on alternate beats when it didn't turn). */
void n_zombie_StepSound(void) {
    FRAME(0x18);
    u32 e = A0;
    s16 act = EN_ACTION(e);
    if (act == 0 || act == 10) return;
    u32 f = ep_frameCount;
    if (f & 0x1F) return;
    if (!(f & 0x20) && EN_YAW(e) == (s16)A1) zombie_call(n_zombie_SoundAt, e, 0x13, 0xA);
    else zombie_call(n_zombie_SoundAt, e, 0x11, 9);
}

/* 801046F4 (joint, index): model hook (+0x894) run for each joint of the
 * current zombie: the spine (21-40) takes the sway (+0x364); the hips
 * (41-60) lean (+0x368) and the legs (101-120) undo it. */
void n_zombie_JointHook(void) {
    u32 j = A0;
    u32 group = (A1 - 1) / 20;
    u32 e = ep_enemyCur;
    if (group == 2) MEM_W16(j + 0x46, MEM_R16(j + 0x46) + GV(u16, e + 0x364));
    if (group == 1) {
        MEM_W16(j + 0x48, MEM_R16(j + 0x48) + GV(u16, e + 0x368));
        GV(s32, e + 0x36C) = (s32)(s16)MEM_R16(j + 0x46);
    }
    if (group == 5) {
        MEM_W16(j + 0x48, MEM_R16(j + 0x48) - GV(u16, e + 0x368));
        if (GV(s32, e + 0x368) != 0) MEM_W16(j + 0x46, MEM_R16(j + 0x46) + GV(u16, e + 0x36C));
    }
    V0 = 0;
}

/* 801047B4 (spawn, e): set a zombie up: hooks and tables, its work
 * block, model by type, a random walking speed (0x80105ABC, weighted by
 * 0x80105ADC), voice bank; in two rooms, the zone it stands in. */
void n_zombie_Setup(void) {
    FRAME(0x20);
    u32 spawn = A0, e = A1;
    GV(u32, e + 0x894) = 0x801046F4u;               /* zombie_JointHook */
    GV(u32, e + 0x840) = 0x80105728u;               /* hit factors */
    GV(u32, e + 0x844) = 0x80105478u;               /* parameters */
    GV(s32, e + 0x364) = 0;
    GV(s32, e + 0x368) = 0;
    EN_SPEED(e) = (s16)gcall(FN_EN_BASE_SPEED, e, 0, 0, 0);
    u32 w = ZOMBIE_WORK_BASE + (u32)EN_SLOT(e) * ZOMBIE_WORK_SIZE;
    ZW = w;
    GV(s32, w + 0x0C) = 0;
    GV(s32, w + 0x10) = 0;
    GV(s32, w + 0x14) = 0;
    GV(s32, w + 0x18) = 0;
    GV(s32, w + 0x1C) = 0;
    GV(s32, w + 0x84) = 0;
    u32 r = en_rand();
    g_grabCooldown = 0;
    ZW_GROUP = (s32)(r & 7);
    ZW_IN_CONE = 0;
    ZW_KILLED = 0;
    u32 model = 0, frames = 0;
    switch (EN_TYPE(e)) {
    case 0: case 1: case 2: case 8: case 9: model = 0x8010581Cu; frames = 0x801055E8u; break;
    case 4: case 5: case 6:                 model = 0x801058E4u; frames = 0x80105690u; break;
    case 10:                                model = 0x801059ACu; frames = 0x801055E8u; break;
    }
    if (model) {
        GV(u32, D_800BBB80_ADDR + (u32)GV(s32, spawn) * 4) = model;
        ZW_FRAMES = frames;
    }
    r = en_rand() & 0xFF;
    s32 k = 0;
    for (; k < 7; k++)
        if (!(GV(s32, 0x80105ADC + (u32)k * 4) * 51 / 20 < (s32)r)) break;
    s32 speed = GV(s32, 0x80105ABC + (u32)k * 4);
    ZW_SPEED2 = speed;
    ZW_BASE_SPEED = speed;
    EN_SPEED(e) = (s16)GV(u16, ZW + 4);
    ZW_VOICE_BANK = EN_SLOT(e) & 1;

    s32 stage = GV(s32, 0x800B4BA0), room = GV(s32, 0x800B3E20);
    if (stage == 0xB && room == 8) {
        s32 z = (s32)gcall(0x80082708u, e, 0, 0, 0);
        GV(s32, e + 0x354) = z;
        if (z < 0) { GV(s32, e + 0x354) = -1; V0 = (u32)-1; return; }
        for (u32 i = GV(u8, GV(u32, 0x800A89B0) + 4); (s32)i <= (s32)GV(u8, GV(u32, 0x800A89B0) + 5); i++) {
            if (gcall(0x80082CD0u, (u32)EN_X(e), (u32)EN_Z(e), i, 0) != 0) {
                GV(s16, e + 0x854) = (s16)i;
                GV(s16, e + 0x856) = (s16)i;
                break;
            }
        }
        ZW_ZONE = GV(s32, e + 0x354);
        V0 = (u32)GV(s32, e + 0x354);
        return;
    }
    if (stage != 8) { V0 = 0x800B0000u; return; }
    if (room != 2) { V0 = 2; return; }
    GV(s32, e + 0x354) = (s32)gcall(0x80082708u, e, 0, 0, 0);
    GV(s16, e + 0x856) = -1;
    V0 = (u32)-1;
}

/* Rooms with zones: stage 11 room 8, stage 8 room 2. */
static int zombie_ZoneRoom(void) {
    return (D_800A866C == 0xB && D_800B3F08 == 8) || (D_800A866C == 8 && D_800B3F08 == 2);
}

/* 80104BE8 (e): in the zoned rooms, the zone update (0x80104ADC). */
void n_zombie_ZoneCheck(void) {
    FRAME(0x18);
    if (zombie_ZoneRoom()) gcall(FN_ZONE_INIT, A0, 0, 0, 0);
}

/* 80104C50 (e): stage 11 room 8: which of the zones next to its last
 * (+0x856) it is in now. */
void n_zombie_ZoneTrack(void) {
    FRAME(0x20);
    u32 e = A0;
    if (!(D_800A866C == 0xB && D_800B3F08 == 8)) return;
    for (s32 i = GV(s16, e + 0x856) - 1; ; ) {
        if (gcall(0x80082CD0u, (u32)EN_X(e), (u32)EN_Z(e), (u32)i, 0) != 0) {
            GV(s16, e + 0x856) = (s16)i;
            return;
        }
        i++;
        if (GV(s16, e + 0x856) + 1 < i) return;
    }
}

/* 80104CEC (e): after the head was shot off: hide it (first frame only)
 * and move the falling head (work block +0x20). */
void n_zombie_UpdateLostHead(void) {
    FRAME(0x18);
    u32 e = A0;
    if (ZW_HEAD_OFF == 0) return;
    if (ZW_HEAD_OFF == 1) GV(u8, e + 3) = 0;
    if (ZW_HEAD_OFF != 0) {
        GV(u8, GV(u32, e + 0x1DC) + 0xD) = 0;
        gcall(0x80082760u, ZW + 0x20, 0, 0, 0);
    }
}

/* 80104D5C (e): walking speed from the base speed by distance to the
 * player: x1 near, x1.2 to 10000, x1.5 to 15000, x1.75 to 20000, x2
 * farther; never faster than -0x7F. */
void n_zombie_SpeedByDistance(void) {
    FRAME(0x18);
    u32 e = A0;
    EN_SPEED(e) = (s16)GV(u16, ZW + 4);
    s32 d = EN_DIST(e);
    if (d >= 5001) {
        s32 v = EN_SPEED(e);
        if (d < 10001) v = v * 6 / 5;
        else if (d < 15001) v = v * 3 / 2;
        else if (d < 20001) v = v * 7 / 4;
        else v = v * 2;
        EN_SPEED(e) = (s16)v;
    }
    if (EN_SPEED(e) < -0x7F) EN_SPEED(e) = -0x7F;
}

/* Walking animations: 1-5 and 38-41. */
static int zombie_IsWalkAnim(s16 a) {
    return (a > 0 && a < 6) || (a >= 0x26 && a < 0x2A);
}

/* 80104E78 (e): when a new animation is queued (+0x860), set its playing
 * direction/rate from the walking speed (walks) or to normal (others),
 * once the blend is done. */
void n_zombie_SyncWalkAnim(void) {
    FRAME(0x20);
    u32 e = A0;
    switch (ZW_ANIM_SYNC) {
    case 0: {
        s16 next = GV(s16, e + 0x860);
        if (next < 0) return;
        ZW_ANIM_SYNC = zombie_IsWalkAnim(next) ? 1 : 2;
        return;
    }
    case 1: {
        if (GV(s16, e + 0x85C) != 0) return;
        if (!zombie_IsWalkAnim(GV(s16, e + 0x860))) { ZW_ANIM_SYNC = 0; return; }
        u32 p = EN_ANIMSTATE(e);
        if (!(GV(u16, p + 0x14) < GV(u16, p + 0x16))) return;
        s32 d = ZW_BASE_SPEED - GV(s32, 0x80105ABC);
        if (d < 0) d = -d;
        s32 n = 0;
        while (d > 0) { d -= 4; n++; }
        zombie_call(n_zombie_SetPartsDir, e, (u32)((n + 1) << 4), 0);
        break;
    }
    case 2: {
        if (GV(s16, e + 0x85C) != 0) return;
        u32 p = EN_ANIMSTATE(e);
        if (!(GV(u16, p + 0x14) < GV(u16, p + 0x16))) return;
        zombie_call(n_zombie_SetPartsDir, e, 0x10, 0);
        break;
    }
    default:
        return;
    }
    GV(s16, e + 0x860) = -1;
    ZW_ANIM_SYNC = 0;
}

/* 80105160: count the grab cool-down down. */
void n_zombie_GrabCooldown(void) {
    if (g_grabCooldown != 0) g_grabCooldown -= 1;
}

/* 80105180 (e): a zombie's frame (see the top of the file). */
void n_zombie_Update(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 w = ZOMBIE_WORK_BASE + (u32)EN_SLOT(e) * ZOMBIE_WORK_SIZE;
    GV(s32, w + 0xA0) = -1;
    ZW = w;
    gcall(GV(u32, ZOMBIE_STATES + (u32)((s32)EN_STATE(e) << 2)), e, 0, 0, 0);
    if (EN_FLAGS(e) & EF_SHOT) {
        zombie_call(n_zombie_Sound, e, 7, 0);
        zombie_call(n_zombie_OnHit, e, 0, 0);
        gcall(FN_EN_HIT_POST, e, 0, 0, 0);
    }
    zombie_call(n_zombie_UpdateLostHead, e, 0, 0);
    zombie_call(n_zombie_ZoneCheck, e, 0, 0);
    zombie_call(n_zombie_ZoneTrack, e, 0, 0);
    zombie_call(n_zombie_GrabCooldown, e, 0, 0);
    zombie_call(n_zombie_SpeedByDistance, e, 0, 0);
    zombie_call(n_zombie_SyncWalkAnim, e, 0, 0);
    s32 lean = GV(s32, e + 0x368);
    if (lean != 0) GV(s32, e + 0x368) = lean > 0 ? lean - 0x20 : lean + 0x20;
}

/* 801035F4 (e): action 10, lurking: think as when idle. */
void n_zombie_LurkPre(void) {
    FRAME(0x18);
    gcall(0x801019F0u, A0, A1, A2, A3);
}

/* 80103614 (e): action 10, lurking (the stage 8 ambush): by step, a few
 * rounds of animation 0x14 (work +0x88), or a still pause of 0-3 seconds
 * (work +0x8C) in animation 0 (a sound at frame 0x44C of the first);
 * wakes (state 1, action 4, a random wait 150-405) once the ambush hasn't
 * happened (event flag 0x2A) and its breakable thing is broken (room id
 * 0x72: event flag 0x13, 0x73: 0x14). */
void n_zombie_LurkStep(void) {
    FRAME(0x20);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        if (EN_ANIM(e) == 0x14) {
            GV(s32, ZW + 0x88) = (s32)(en_rand() & 3) + 1;
            EN_STEP(e) += 1;
        } else {
            GV(s32, ZW + 0x8C) = (s32)(en_rand() & 3) * 0x3C;
            EN_STEP(e) = 2;
        }
        break;
    case 1:
        if (gcall(0x80100728u, e, 1, 1, 0) != 0) {
            s32 n = GV(s32, ZW + 0x88);
            if (n < -1) {
                GCALL2(FN_EN_SET_ANIM, e, 0);
                GV(s32, ZW + 0x8C) = (s32)(en_rand() & 3) * 0x3C;
                EN_STEP(e) += 1;
            } else {
                GV(s32, ZW + 0x88) = n - 1;
                GCALL3(FN_EN_SET_ANIM_B, e, 0x14, 4);
            }
        } else {
            u32 a = EN_ANIMSTATE(e);
            if (GV(u16, a + 0x14) == 0x44C && GV(s16, a + 0x10) == 0)
                GCALL4(FN_SOUND_CUE2, 6, 6, 0x7F, 0x7F);
        }
        break;
    case 2: {
        u32 w = ZW;
        s32 t = GV(s32, w + 0x8C);
        if (t < 0) {
            GCALL2(FN_EN_SET_ANIM, e, 0x14);
            EN_STEP(e) = 0;
        } else {
            GV(s32, w + 0x8C) = t - 1;
        }
        break;
    }
    }
    if (GCALL2(FN_FLAG_TEST, ep_eventFlags, 0x2A) != 0) return;
    u32 flag;
    if (EN_ROOMID(e) == 0x72) flag = 0x13;
    else if (EN_ROOMID(e) == 0x73) flag = 0x14;
    else return;
    if (GCALL2(FN_FLAG_TEST, ep_eventFlags, flag) == 0) return;
    EN_STATE(e) = 1;
    GCALL3(FN_EN_SET_ACTION, e, 4, 2);
    EN_TIMER(e) = 0;
    GV(u32, e + 0x370) = (en_rand() & 0xFF) + 0x96;
}
