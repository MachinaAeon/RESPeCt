/* The Tyrant, "Mr. X" (enemy program ENEMPROG 58, loaded in enemy slot B
 * at 0x80100000). Identified by how it looks in the area scenarios that
 * load it (area26: stage 12 room 5, area28 and area29: stage 11, area31
 * and area32: stage 2: the giant in the long coat) and by what the code
 * does: it strides after the player round the walls, punches and swings
 * (grabbing the player), sidesteps when they run past, staggers under
 * heavy fire and, in the scripted stages, walks set paths. Its face is
 * animated by a filter over recent "morph" samples (tyrant_MorphFilter).
 *
 * tyrant_Update (0x80105270) runs every frame for each one: it picks the
 * work block, the first time sets up (tyrant_Setup), runs the stage's
 * check (tyrant_StageCheck), counts animation frames, runs the state's
 * handler (0x80105F1C; state 1, tyrant_StateActive, runs the action's
 * handler from 0x80105EC0), filters the morph weights, takes the hits
 * (tyrant_OnHit), and when the player dies holds them in front of it.
 * The action handlers act by what it is doing (TW_MODE: 0 / 1 walking, 2
 * attacking, 7 / 8 the player dead, 10 dying, ...), through jump tables
 * in the overlay's first words.
 *
 * Each one has a 0x1E8-byte work block (0x80105F30 + slot * 0x1E8; the
 * current one's address is kept at 0x801066D0, see TW_*). Animations are
 * started like the Cerberus' (en_start_anim), also restarting the morph
 * sampling (+0x1E4).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"
#include "poll.h"

void n_tyrant_MorphFilter(void);
void n_tyrant_MorphPush(void);
void n_tyrant_MorphPick(void);
void n_tyrant_PlayerFiring(void);
void n_tyrant_Grab(void);
void n_tyrant_PlayerInCone(void);
void n_tyrant_WallDist(void);
void n_tyrant_CrossedWall(void);
void n_tyrant_WallAhead(void);
void n_tyrant_Wander(void);
void n_tyrant_TurnTo(void);
void n_tyrant_NotFacing(void);
void n_tyrant_WallToPlayer(void);
void n_tyrant_SetPartsDir(void);
void n_tyrant_AnimEvent(void);
void n_tyrant_Anim(void);
void n_tyrant_CanCharge(void);
void n_tyrant_PlayerSide(void);
void n_tyrant_Steer(void);
void n_tyrant_ScriptCheck(void);
void n_tyrant_PickAttack(void);
void n_tyrant_WalkStep(void);
void n_tyrant_Standoff(void);
void n_tyrant_IdleAct(void);
void n_tyrant_WalkAct(void);
void n_tyrant_PunchAct(void);
void n_tyrant_Flinch1Act(void);
void n_tyrant_Flinch2Act(void);
void n_tyrant_SwingAct(void);
void n_tyrant_SlamAct(void);
void n_tyrant_LungeAct(void);
void n_tyrant_ReelAct(void);
void n_tyrant_DodgeAct(void);
void n_tyrant_OnHit(void);
void n_tyrant_JointHook(void);
void n_tyrant_Stage2Check(void);
void n_tyrant_StageCheck(void);
void n_tyrant_StateActive(void);
void n_tyrant_StateNone(void);
void n_tyrant_Setup(void);
void n_tyrant_Update(void);

/* ---- the work block ---- */
#define TW               GV(u32, 0x801066D0)
#define TW_TURN          GV(u8, TW + 0x01)    /* steering round a wall (en_wander) */
#define TW_MODE          GV(u8, TW + 0x02)    /* what it is doing (see the top) */
#define TW_CHARGED       GV(u8, TW + 0x03)    /* tried a charge since last far off */
#define TW_STEER         GV(u8, TW + 0x04)    /* 0 turning to the player, 1 striding, 2 round a wall */
#define TW_STARTED       GV(u8, TW + 0x05)
#define TW_MORPH_HOLD    GV(u8, TW + 0x06)    /* the sub-object keeps its place */
#define TW_DODGED        GV(u8, TW + 0x07)
#define TW_STAGE_DONE    GV(u8, TW + 0x08)
#define TW_SLOT          GV(u8, TW + 0x0A)    /* which (its own slot, or slot + 2 for the copy) */
#define TW_HIT           GV(u8, TW + 0x0C)    /* hit and the player standing still */
#define TW_MORPH         GV(s16, TW + 0x0E)   /* morph entry picked this frame (-1 none) */
#define TW_TIME          GV(s16, TW + 0x14)
#define TW_FRAME         GV(s16, TW + 0x16)   /* animation frame counter */
#define TW_PREV_FRAME    GV(s16, TW + 0x18)
#define TW_ANIM_FRAME    GV(s16, TW + 0x1A)   /* last frame's animation frame */
#define TW_DAMAGE        GV(s16, TW + 0x1C)   /* damage since it last reeled */
#define TW_TICK          GV(u16, TW + 0x1E)
#define TW_SEED          GV(u16, TW + 0x20)
#define TW_YAW_OFS       GV(u16, TW + 0x24)
#define TW_SUB           GV(u32, TW + 0x28)   /* the sub-object's enemy record */
#define TW_MORPH_SET     GV(u32, TW + 0x2C)   /* the morph target set */
#define TW_MORPHS        GV(u32, TW + 0x30)   /* 16 morph entries (16 bytes: weights, period, target, sum) */
#define TW_MORPH_OUT(i)  GV(u32, TW + 0x34 + (u32)(i) * 4)
#define TW_HISTORY       GV(u32, TW + 0x1D4)  /* 16 x 40 words */
#define TW_RING          GV(u32, TW + 0x1DC)  /* 16 words */
#define TW_RING_BASE     GV(s16, TW + 0x1E0)
#define TW_RING_POS      GV(s16, TW + 0x1E2)
#define TW_SAMPLED       GV(s16, TW + 0x1E4)  /* the action's morph samples were pushed */
#define TYRANT_WORK_BASE 0x80105F30u
#define TYRANT_WORK_SIZE 0x1E8

#define TYRANT_ACTIONS   0x80105EC0u
#define TYRANT_STATES    0x80105F1Cu
#define TYRANT_ACT_SAMPLES 0x80105E34u        /* u8 per action: morph samples */
#define TYRANT_ACT_THRESH  0x80105E4Cu        /* per action: thresholds (u8 x n) */
#define TYRANT_SAMPLES   0x801066E0u          /* s16 */
#define TYRANT_MORPH_A   0x80105B10u          /* morph target sets */
#define TYRANT_MORPH_B   0x80105BB0u
#define TYRANT_MORPH_C   0x80105C50u
#define TYRANT_MORPH_D   0x80105CF0u
#define g_tyrantMorphQ   GA(s32, 0x800BAA38)  /* 16 queued morph entries (-1 none) */
#define g_tyrantMorphN   GV(s32, 0x800BAA78)
#define g_tyrantGrabArg  GV(s16, 0x80105904)
#define g_tyrantNoRise   GV(u8, 0x801067B0)
#define g_tyrantAmmoLeft GV(u8, 0x801067B2)
#define g_tyrantSaved    GV(u32, 0x801067B8)
#define D_800AB51C       GV(u32, 0x800AB51C)
#define g_gameMode       GV(s32, 0x800BB19C)
#define g_playerHp       GV(s16, 0x800A8584)
#define g_currentWeapon  GV(u32, 0x800A8580)   /* pointer: *it is the weapon ID */
#define g_stage          GV(s32, 0x800A866C)
#define g_room           GV(s32, 0x800B3F08)
#define g_playerZone     GV(s32, 0x800A8614)
#define g_playerAction   GV(s16, 0x800A1324)
#define g_areaTable      GV(u32, 0x800A89B0)
#define FN_EN_ANIM_SOUND 0x8008127Cu   /* (e, bank, sound) */
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_TRACK_SLOT    0x80082708u   /* (e): its position-tracking slot, or < 0 */
#define FN_IN_AREA       0x80082CD0u   /* (x, z, area) */
/* translated (not reached by the scenarios) */
#define FN_TY_TO_POINT   0x8010118Cu   /* (e, point) */
#define FN_TY_STAGE2_R16 0x80103C40u
#define FN_TY_STAGE5     0x80104DB0u

static u32 ty_call(void (*f)(void), u32 a0, u32 a1, u32 a2) {
    A0 = a0; A1 = a1; A2 = a2;
    f();
    return V0;
}
static void ty_anim(u32 e, u32 anim, u32 blend) { ty_call(n_tyrant_Anim, e, anim, blend); }

/* MIPS div remainder (x / 0 leaves x). */
static s32 ty_rem(s32 x, s32 y) {
    if (y == 0) return x;
    if (x == INT32_MIN && y == -1) return 0;
    return x % y;
}

/* 80100160 (e): the morph weights: each entry's target weight (0x1000
 * for the entries queued this frame) goes into its history at the
 * current tick, and its output is the history filtered with its 40
 * weights (>> 12). */
void n_tyrant_MorphFilter(void) {
    u32 w = TW;
    u32 hist = GV(u32, w + 0x1D4), ent = GV(u32, w + 0x30);
    u32 t = GV(u16, w + 0x1E);
    for (s32 i = 15;; ) {
        GV(u32, ent + (u32)i * 16 + 8) = 0;
        i--;
        if (i < 0) break;
        LOOP_POLL(0x80100184);
    }
    for (u32 i = 0;; ) {
        s32 q = g_tyrantMorphQ[i];
        if (q != -1) {
            GV(u32, ent + ((u32)q << 4) + 8) += 0x1000;
            g_tyrantMorphQ[i] = -1;
        }
        i++;
        if (!(i < 16)) break;
        LOOP_POLL(0x801001CC);
    }
    for (u32 i = 0;; ) {
        u32 en = ent + i * 16;
        s32 rem = ty_rem((s32)t, GV(s32, en + 4));
        GV(u32, hist + i * 0xA0 + ((u32)rem << 2)) = GV(u32, en + 8);
        GV(u32, en + 0xC) = 0;
        for (u32 j = 0;; ) {
            s32 k = ((s32)t - (s32)j) % 40;
            u32 h = GV(u32, hist + ((u32)k + i * 40) * 4);
            if (h != 0) GV(u32, en + 0xC) += h * GV(u32, GV(u32, en) + j * 4);
            j++;
            if (!((s32)j < 0x28)) break;
            LOOP_POLL(0x8010028C);
        }
        GV(s32, en + 0xC) >>= 12;
        i++;
        if (!(i < 16)) break;
        LOOP_POLL(0x801002B0);
    }
    for (u32 i = 0;; ) {
        TW_MORPH_OUT(i) = GV(u32, ent + i * 16 + 0xC);
        i++;
        if (!(i < 16)) break;
        LOOP_POLL(0x801002DC);
    }
    g_tyrantMorphN = 0;
}

/* 801002F0 (first, n): push n morph samples (from 0x801066E0 + first) into
 * the ring; returns the ring position they start at. */
void n_tyrant_MorphPush(void) {
    u32 src = TYRANT_SAMPLES + A0 * 2;
    s32 n = (s32)A1;
    s16 start = TW_RING_POS;
    for (s32 i = 0; i < n; ) {
        u32 w = TW;
        GV(s32, GV(u32, w + 0x1DC) + (u32)(s32)GV(s16, w + 0x1E2) * 4) = GV(s16, src);
        GV(u16, w + 0x1E2) += 1;
        if (GV(s16, w + 0x1E2) >= 0x10) GV(u16, w + 0x1E2) = 0;
        i++;
        src += 2;
        if (!(i < n)) break;
        LOOP_POLL(0x80100370);
    }
    GV(u16, TW + 0x1E4) += 1;
    V0 = (u32)(s32)start;
}

/* 8010039C (e): queue the morph entry for this animation frame (the first
 * of the action's thresholds the frame reaches, counted from the ring
 * base) and set its target set. */
void n_tyrant_MorphPick(void) {
    u32 e = A0;
    u32 w = TW;
    GV(s16, w + 0xE) = -1;
    s32 act = EN_ACTION(e);
    u32 th = GV(u32, TYRANT_ACT_THRESH + (u32)act * 4);
    u32 n = GV(u8, TYRANT_ACT_SAMPLES + (u32)act);
    if (n != 0) {
        s32 f = GV(s16, w + 0x16);
        for (u32 i = 0;; ) {
            if (!((s32)(GV(u8, th + i) >> 1) < f)) {
                GV(u16, w + 0xE) = (GV(u16, w + 0x1E0) + i) & 0xF;
                i = n;
            }
            i++;
            if (!((s32)i < (s32)n)) break;
            LOOP_POLL(0x80100420);
        }
    }
    s32 m = TW_MORPH;
    if (m >= 0) {
        g_tyrantMorphQ[g_tyrantMorphN] = m;
        g_tyrantMorphN += 1;
    }
    w = TW;
    u32 en = GV(u32, w + 0x30) + ((u32)(s32)GV(s16, w + 0xE) << 4);
    if (GV(u32, en) != GV(u32, w + 0x2C)) GV(u32, en) = GV(u32, w + 0x2C);
}

/* 8010049C (): 0 when the player fires a weapon of type 7 or 8 (with ammo,
 * by pad type: the button or, with the mouse, the reticle on screen);
 * else 1. */
void n_tyrant_PlayerFiring(void) {
    u32 r;
    if (GV(s16, g_currentWeapon + 2) <= 0 && g_tyrantAmmoLeft == 0) { V0 = 1; return; }
    u32 pad = GV(u32, 0x80062E38);
    if (pad == 0x200000) {
        r = 1;
        if (GV(u8, 0x80062E40) != 0 && GV(s32, 0x80062DDC) >= 0) r = GV(u32, 0x80062DE0) >> 31;
    } else if ((s32)pad > 0x200000) {
        r = 1;
        if (pad == 0x400000) r = GV(u8, 0x80062E40) == 0;
    } else {
        r = 1;
        if (pad == 0x80000) r = GV(u8, 0x80062E40) == 0;
    }
    if (r == 0) {
        s32 wid = GV(s16, g_currentWeapon);
        r = wid < 9 ? (u32)(wid < 7) : 1;
    }
    V0 = r;
}

/* 8010057C (e, arg): grab the player (argument kept at 0x80105904) if the
 * game allows it. */
void n_tyrant_Grab(void) {
    FRAME(0x20);
    u32 e = A0, arg = A1;
    if (g_gameMode != 1 || ep_grabLock != 0 || ep_grabLock2 != 0) return;
    gcall(FN_EN_SET_SPEED, e, 0, 0, 0);
    g_tyrantGrabArg = (s16)arg;
    gcall(FN_EN_GRAB_PLAYER, e, 6, 0, 0);
    GCALL1(FN_EN_GRAB_START, e);
}

/* 80100604 (e, half): is the player within yaw +-half of e? */
void n_tyrant_PlayerInCone(void) {
    FRAME(0x50);
    u32 e = A0;
    V0 = en_point_in_cone(sp + 0x30, sp + 0x20, sp + 0x10, e, (s32)A1, 100, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z);
}

/* 80100718 (e, wall): its distance to wall `wall`. */
void n_tyrant_WallDist(void) {
    FRAME(0x38);
    u32 e = A0;
    u32 w = GV(u32, EP_WALLS_N + 4) + (A1 << 4);
    GS(u32, sp + 0x10) = GV(u32, w + 0);
    GS(u32, sp + 0x18) = GV(u32, w + 4);
    GS(u32, sp + 0x20) = GV(u32, w + 8);
    GS(u32, sp + 0x28) = GV(u32, w + 12);
    V0 = gcall(FN_LINE_DIST, sp + 0x10, sp + 0x20, EN_POS(e), 0);
}

/* 80100774 (from, to): the first wall the path crosses from its front,
 * or -1. */
void n_tyrant_CrossedWall(void) {
    FRAME(0x40);
    V0 = (u32)en_crossed_segment_poll(A0, A1, GV(u32, EP_WALLS_N + 4), GV(u8, EP_WALLS_N), 16, 1, 0x801008EC);
}

/* 8010091C (e, k): the wall within k x 1000 ahead, or -1. */
void n_tyrant_WallAhead(void) {
    FRAME(0x30);
    V0 = en_wall_ahead(sp + 0x10, A0, A1, 0x80100774u);
}

/* 801009F8 (e, k, turn): steer round the wall ahead (en_wander). */
void n_tyrant_Wander(void) {
    FRAME(0x30);
    V0 = (u32)en_wander(A0, A1, A2, TW, 0x8010091Cu, 0x80100718u);
}

/* 80100C1C (e, dist): turn 0x30 towards the player (en_turn_to). */
void n_tyrant_TurnTo(void) {
    FRAME(0x40);
    V0 = en_turn_to(sp, A0, A1, 0x30, 0x80100604u);
}

/* 80100D68 (e): 1 unless it and the player face each other. */
void n_tyrant_NotFacing(void) {
    V0 = en_not_facing(A0, 0x47D, 0x707);
}

/* 80100D9C (e): is there a wall between it and the player? */
void n_tyrant_WallToPlayer(void) {
    FRAME(0x18);
    V0 = (s32)ty_call(n_tyrant_CrossedWall, EN_POS(A0), PLAYER_POS_ADDR, 0) != -1;
}

/* 80100DC8 (e, dir): play every animated part in direction dir (+0xD),
 * restarting (+0x10). */
void n_tyrant_SetPartsDir(void) {
    u32 e = A0;
    s32 n = EN_NPARTS(e);
    for (s32 i = 0; i < n; ) {
        u32 p = EN_ANIMPART(e, i);
        GV(u8, p + 0xD) = (u8)A1;
        GV(u16, EN_ANIMPART(e, i) + 0x10) = 0;
        i++;
        if (!(i < n)) break;
        LOOP_POLL(0x80100DFC);
    }
}

/* 80100E0C (e, part, k): the animation event bit (en_anim_event). */
void n_tyrant_AnimEvent(void) {
    V0 = en_anim_event(A0, A1, A2);
}

/* 80100ED0 (e, anim, blend): start an action / animation (and the morph
 * sampling). */
void n_tyrant_Anim(void) {
    GV(s16, TW + 0x1E4) = 0;
    en_start_anim(A0, A1, A2);
    V0 = 1;
}

/* 80100F10 (e): charge now? Once per approach, within 6000, a quarter of
 * the time, seeing the player in its cone, not already striding (parts'
 * direction 0x10). */
void n_tyrant_CanCharge(void) {
    FRAME(0x18);
    u32 e = A0;
    V0 = 0;
    if (TW_CHARGED != 0) return;
    TW_CHARGED = 1;
    if (!((u32)EN_DIST(e) < 0x1771)) return;
    int ok = (s32)en_rand() % 100 < 0x19;
    V0 = 0;
    if (!ok) return;
    if (!(EN_FLAGS(e) & EF_SEES)) return;
    if (GV(s8, EN_ANIMSTATE(e) + 0xD) == 0x10) return;
    V0 = ty_call(n_tyrant_PlayerInCone, e, 0x71, 0);
}

/* 80100FE8 (e): which side of its facing the player is: 0 straight, 1, 2. */
void n_tyrant_PlayerSide(void) {
    FRAME(0x30);
    u32 e = A0, v = sp + 0x10;
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v + 0, MEM_R32(v + 0) + (u32)EN_X(e));
    MEM_W32(v + 4, MEM_R32(v + 4) + (u32)EN_Y(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    s32 s = (s32)gcall(FN_LINE_SIDE, EN_POS(e), v, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z);
    V0 = s == 0 ? 0 : s > 0 ? 1 : 2;
}

/* 80101088 (e): steer when a wall is between it and the player: turn to
 * them, stride on until a wall is ahead, then round it. */
void n_tyrant_Steer(void) {
    FRAME(0x20);
    u32 e = A0;
    switch (TW_STEER) {
    case 0:
        if (!ty_call(n_tyrant_TurnTo, e, 0, 0)) TW_STEER += 1;
        return;
    case 1:
        if ((s32)ty_call(n_tyrant_WallAhead, e, 2, 0) == -1) return;
        TW_STEER = ty_call(n_tyrant_PlayerInCone, e, 0x38E, 0) ? 2 : 0;
        return;
    case 2:
        if ((s32)ty_call(n_tyrant_Wander, e, 1, 0x38) == -1) TW_STEER = 1;
        return;
    }
}

/* 80101228 (e): stage 6's scripted walk: 1 to act freely (other stages;
 * once dead it collapses), 0 while it walks its path (by the player's
 * zone and position, which part of the path: TW_SLOT). */
void n_tyrant_ScriptCheck(void) {
    FRAME(0x18);
    u32 e = A0;
    if (g_stage != 6) { V0 = 1; return; }
    if (EN_HP(e) <= 0) {
        TW_MODE = 0xA;
        ty_anim(e, 9, 0x10);
        V0 = 1;
        return;
    }
    int free_ = 0;
    switch (TW_SLOT) {
    case 0: free_ = g_playerZone < 5; break;
    case 1: free_ = (u32)(g_playerZone - 4) < 4; break;
    case 2: free_ = !(g_playerZone < 7) && !(0x2AB34 < GV(s32, PLAYER_POS_ADDR + 8)); break;
    case 3: free_ = 0x2C04B < GV(s32, PLAYER_POS_ADDR + 8); break;
    }
    if (free_) { V0 = 1; return; }
    gcall(FN_TY_TO_POINT, e, TW_SLOT, 0, 0);
    V0 = 0;
}

/* 80101368 (e): attack (mode 2): 2 punch (10%), 16 lunge (10%), 5 swing
 * (40%), 11 slam (40%). */
void n_tyrant_PickAttack(void) {
    FRAME(0x18);
    u32 e = A0;
    TW_MODE = 2;
    s32 r = (s32)en_rand() % 100;
    u32 a = r < 0xA ? 2 : r < 0x14 ? 0x10 : r < 0x3C ? 5 : 0xB;
    ty_anim(e, a, 0x40);
}

/* 80101404 (e): walk (26 a frame, 52 striding): stop to aim when the
 * player fires a heavy weapon; attack when close (1500); stride when far
 * (3000), else maybe charge; up close sidestep when the player runs past;
 * steer round walls; turn to the player and keep the morphs going. */
void n_tyrant_WalkStep(void) {
    FRAME(0x28);
    u32 e = A0;
    s32 speed = -0x1A;
    int steered = 0;
    if (EN_STEP(e) == 0) {
        EN_STEP(e) += 1;
        TW_STEER = 0;
    }
    if (!ty_call(n_tyrant_ScriptCheck, e, 0, 0)) return;
    if (ty_call(n_tyrant_WallToPlayer, e, 0, 0)) {
        if (!((u32)EN_DIST(e) < 0xBB9) && GV(s8, EN_ANIMSTATE(e) + 0xD) != 0x20) {
            TW_CHARGED = 0;
            ty_call(n_tyrant_SetPartsDir, e, 0x20, 0);
        }
        ty_call(n_tyrant_Steer, e, 0, 0);
        steered = 1;
    } else {
        TW_STEER = 0;
        A0 = 0;
        n_tyrant_PlayerFiring();
        if (V0 == 0) {
            ty_anim(e, 0x13, 8);
            return;
        }
        if ((u32)EN_DIST(e) < 0x5DD && ty_call(n_tyrant_PlayerInCone, e, 0x71, 0)) {
            if (EN_FLAGS(e) & EF_SEES) ty_call(n_tyrant_PickAttack, e, 0, 0);
            return;
        }
        if (!((u32)EN_DIST(e) < 0xBB9) && GV(s8, EN_ANIMSTATE(e) + 0xD) != 0x20) {
            TW_CHARGED = 0;
            ty_call(n_tyrant_SetPartsDir, e, 0x20, 0);
        } else {
            if (TW_CHARGED == 0 && ty_call(n_tyrant_CanCharge, e, 0, 0)) {
                TW_MODE = 2;
                ty_anim(e, 0x11, 0x40);
                return;
            }
            if ((u32)EN_DIST(e) < 0xBB9) {
                if (GV(s8, EN_ANIMSTATE(e) + 0xD) != 0x10) {
                    ty_call(n_tyrant_SetPartsDir, e, 0x10, 0);
                } else if (!ty_call(n_tyrant_NotFacing, e, 0, 0) && (g_playerMoveFlags & 1)) {
                    TW_DODGED = 1;
                    u32 side = ty_call(n_tyrant_PlayerSide, e, 0, 0);
                    if (side == 1) {
                        TW_MODE = 2;
                        ty_anim(e, 5, 0x40);
                    } else if (side == 2) {
                        TW_MODE = 2;
                        ty_anim(e, 0x15, 0x40);
                    }
                }
            }
        }
    }
    if (GV(s8, EN_ANIMSTATE(e) + 0xD) == 0x20) {
        speed <<= 1;
        TW_MORPH_SET = TYRANT_MORPH_B;
    } else {
        TW_MORPH_SET = TYRANT_MORPH_A;
    }
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)speed, 0);
    if (!steered) ty_call(n_tyrant_TurnTo, e, 0, 0);
    A0 = e;
    n_tyrant_MorphPick();
}

/* 80101708 (e): mode 2 idle: a standoff of 15 frames, then attack when
 * close (or stand), else walk on (mode 0). */
void n_tyrant_Standoff(void) {
    FRAME(0x18);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if (step == 0) {
        TW_TIME = 0xF;
        EN_STEP(e) += 1;
    } else if (step != 1) {
        return;
    }
    if (TW_TIME == 0) {
        if ((u32)EN_DIST(e) < 0x5DD && ty_call(n_tyrant_PlayerInCone, e, 0x71, 0)) {
            if (EN_FLAGS(e) & EF_SEES) ty_call(n_tyrant_PickAttack, e, 0, 0);
            else ty_anim(e, 0, 0x40);
        } else {
            TW_MODE = 0;
            EN_STEP(e) = 0;
        }
    }
    TW_TIME -= 1;
}

/* An action's first frame: push its morph samples (from `first`; how many
 * by action) and reset the sub-object's offset (+0x1CC). */
static void ty_begin(u32 e, u32 first, u32 hold) {
    if (TW_SAMPLED != 0) return;
    s16 base = (s16)ty_call(n_tyrant_MorphPush, first, GV(u8, TYRANT_ACT_SAMPLES + (u32)(s32)EN_ACTION(e)), 0);
    u32 w = TW;
    GV(s16, w + 0x1E0) = base;
    GV(u32, GV(u32, w + 0x28) + 0x1CC) = 0;
    GV(u8, w + 6) = (u8)hold;
}

/* The animation hasn't moved this frame (+0x14 at +0xE)? */
static int ty_anim_still(u32 e) {
    u32 an = EN_ANIMSTATE(e);
    return GV(u16, an + 0x14) == GV(u16, an + 0xE);
}

/* Set the speed to k on reaching frame f. */
static void ty_speed_at(u32 e, s32 f, u32 k) {
    if (TW_FRAME == f && TW_PREV_FRAME != f) gcall(FN_EN_SET_SPEED, e, k, 0, 0);
}

/* 80101E24 (e): action 0, idle, by mode. */
void n_tyrant_IdleAct(void) {
    FRAME(0x20);
    u32 e = A0;
    ty_begin(e, 0, 0);
    if (ty_anim_still(e)) return;
    u32 m = TW_MODE;
    if (!(m < 0x11)) return;
    LOOP_POLL(0x80101ECC);
    switch (m) {
    case 0: TW_MODE = 1; ty_anim(e, 1, 0x40); return;
    case 2: A0 = e; n_tyrant_Standoff(); return;
    case 7: GCALL1(0x801019F4u, e); return;
    case 8: ty_anim(e, 1, 0x40); return;
    case 10: TW_MODE = 0xA; ty_anim(e, 9, 0x10); return;
    case 12: GCALL1(0x80101AA0u, e); return;
    case 13: GCALL1(0x80101BE8u, e); return;
    case 16:
        GV(u32, TW_SUB + 0x1CC) = GV(u32, e + 0x16C) + 0x50;
        GV(u16, e + 0x1C4) = 0x400;
        EN_FLAGS(e) |= 0x8002;
        return;
    }
}

/* 80101FB0 (e): action 1, walk: footsteps, by mode. */
void n_tyrant_WalkAct(void) {
    FRAME(0x20);
    u32 e = A0;
    ty_begin(e, 0, 0);
    if (ty_anim_still(e)) return;
    s32 f = TW_FRAME;
    if ((f == 0xE && TW_PREV_FRAME != f) || (f == 0x22 && TW_PREV_FRAME != f))
        gcall(FN_EN_ANIM_SOUND, e, 3, 8, 0);
    u32 m = TW_MODE;
    if (!(m < 0x10)) return;
    LOOP_POLL(0x801020AC);
    switch (m) {
    case 0: TW_MODE = 1; /* fall through */
    case 1: A0 = e; n_tyrant_WalkStep(); return;
    case 8: GCALL1(0x80101A18u, e); return;
    case 10: TW_MODE = 0xA; ty_anim(e, 9, 0x10); return;
    case 15: GCALL1(0x80101D34u, e); return;
    }
}

/* The attacks' steps: step 0 grab (arg) at frame `at` when seen (if
 * `seen`) and close (2000), on to step 1 at the animation's event (k
 * frames ahead); step 1 back to idle. */
static void ty_attack_steps(u32 e, s32 at, u32 arg, int seen, u32 k) {
    s16 step = EN_STEP(e);
    if (step == 0) {
        if (TW_FRAME == at && (!seen || (EN_FLAGS(e) & EF_SEES)) && (u32)EN_DIST(e) < 0x7D1)
            ty_call(n_tyrant_Grab, e, arg, 0);
        if (ty_call(n_tyrant_AnimEvent, e, 1, k)) EN_STEP(e) += 1;
    } else if (step == 1) {
        ty_anim(e, 0, 0x40);
    }
}

/* 80102128 (e): action 2, the punch: speed 1 and a grab at frame 28. */
void n_tyrant_PunchAct(void) {
    FRAME(0x20);
    u32 e = A0;
    ty_begin(e, 5, 0);
    if (ty_anim_still(e)) return;
    ty_speed_at(e, 0x1C, 1);
    ty_attack_steps(e, 0x1C, 0x28, 1, 1);
    TW_MORPH_SET = TYRANT_MORPH_D;
    A0 = e;
    n_tyrant_MorphPick();
}

/* A flinch: back to walking (mode 1) at the animation's event. */
static void ty_flinch(u32 e, u32 first) {
    ty_begin(e, first, 0);
    if (ty_anim_still(e)) return;
    s16 step = EN_STEP(e);
    if (step == 0) {
        if (ty_call(n_tyrant_AnimEvent, e, 1, 1)) EN_STEP(e) += 1;
    } else if (step == 1) {
        TW_MODE = 1;
        ty_anim(e, 1, 0x40);
    }
    TW_MORPH_SET = TYRANT_MORPH_C;
    A0 = e;
    n_tyrant_MorphPick();
}

/* 801022A4 (e): action 3, flinching (one way). */
void n_tyrant_Flinch1Act(void) {
    FRAME(0x20);
    ty_flinch(A0, 0xD);
}

/* 801023AC (e): action 4, flinching (the other way). */
void n_tyrant_Flinch2Act(void) {
    FRAME(0x20);
    ty_flinch(A0, 0x10);
}

/* 801024B4 (e): action 5, the swing (also the sidestep's: grab at frame
 * 5, else 7 when seen): speed 1 at frame 6. */
void n_tyrant_SwingAct(void) {
    FRAME(0x20);
    u32 e = A0;
    ty_begin(e, 0x14, 0);
    if (ty_anim_still(e)) return;
    ty_speed_at(e, 6, 1);
    s16 step = EN_STEP(e);
    if (TW_DODGED == 0) {
        if (step == 0) {
            if (TW_FRAME == 7 && (EN_FLAGS(e) & EF_SEES) && (u32)EN_DIST(e) < 0x7D1)
                ty_call(n_tyrant_Grab, e, 0xF, 0);
            if (ty_call(n_tyrant_AnimEvent, e, 1, 1)) EN_STEP(e) += 1;
        } else if (step == 1) {
            ty_anim(e, 0, 0x40);
        }
    } else {
        if (step == 0) {
            if (TW_FRAME == 5 && (u32)EN_DIST(e) < 0x7D1) ty_call(n_tyrant_Grab, e, 0xF, 0);
            if (ty_call(n_tyrant_AnimEvent, e, 1, 1)) EN_STEP(e) += 1;
        } else if (step == 1) {
            TW_DODGED = 0;
            ty_anim(e, 0, 0x40);
        }
    }
    TW_MORPH_SET = TYRANT_MORPH_C;
    A0 = e;
    n_tyrant_MorphPick();
}

/* 80102A40 (e): action 11, the slam: speed 2 at frame 30, grab at 31,
 * on at the event 8 frames ahead (the sub-object let go). */
void n_tyrant_SlamAct(void) {
    FRAME(0x30);
    u32 e = A0;
    if (TW_SAMPLED == 0) {
        s16 base = (s16)ty_call(n_tyrant_MorphPush, 0x32, GV(u8, TYRANT_ACT_SAMPLES + (u32)(s32)EN_ACTION(e)), 0);
        u32 w = TW;
        GV(s16, w + 0x1E0) = base;
        GV(u32, GV(u32, w + 0x28) + 0x1CC) = 0;
        GV(u8, w + 6) = 1;
    }
    if (ty_anim_still(e)) return;
    ty_speed_at(e, 0x1E, 2);
    s16 step = EN_STEP(e);
    if (step == 0) {
        if (TW_FRAME == 0x1F && (EN_FLAGS(e) & EF_SEES) && (u32)EN_DIST(e) < 0x7D1)
            ty_call(n_tyrant_Grab, e, 0x1E, 0);
        if (ty_call(n_tyrant_AnimEvent, e, 1, 8)) EN_STEP(e) += 1;
    } else if (step == 1) {
        GV(u16, TW_SUB + 0x1C8) = 0;
        ty_anim(e, 0, 0x40);
    }
    TW_MORPH_SET = TYRANT_MORPH_C;
    A0 = e;
    n_tyrant_MorphPick();
}

/* 80102BF0 (e): action 16, the lunge: speed 1 at frame 7, grab at 8 (the
 * sub-object follows its model). */
void n_tyrant_LungeAct(void) {
    FRAME(0x20);
    u32 e = A0;
    if (TW_SAMPLED == 0) {
        s16 base = (s16)ty_call(n_tyrant_MorphPush, 0x3C, GV(u8, TYRANT_ACT_SAMPLES + (u32)(s32)EN_ACTION(e)), 0);
        u32 w = TW;
        GV(s16, w + 0x1E0) = base;
        GV(u32, GV(u32, w + 0x28) + 0x1CC) = GV(u32, e + 0x16C) + 0x50;
        GV(u8, w + 6) = 0;
    }
    if (ty_anim_still(e)) return;
    ty_speed_at(e, 7, 1);
    ty_attack_steps(e, 8, 0x14, 1, 1);
    TW_MORPH_SET = TYRANT_MORPH_C;
    A0 = e;
    n_tyrant_MorphPick();
}

/* 80102FC8 (e): action 19, reeling (aimed at): 30 frames still, then walk
 * on. */
void n_tyrant_ReelAct(void) {
    FRAME(0x20);
    u32 e = A0;
    ty_begin(e, 0, 0);
    if (ty_anim_still(e)) return;
    s16 step = EN_STEP(e);
    if (step == 0) {
        ty_call(n_tyrant_SetPartsDir, e, 0, 0);
        TW_TIME = 0x1E;
        EN_STEP(e) += 1;
    } else if (step == 1) {
        TW_TIME -= 1;
        if (TW_TIME == -1) {
            ty_call(n_tyrant_SetPartsDir, e, 0x10, 0);
            ty_anim(e, 1, 0x80);
        }
    }
    TW_MORPH_SET = TYRANT_MORPH_C;
    A0 = e;
    n_tyrant_MorphPick();
}

/* 8010310C (e): action 21, the swing from a sidestep the other way: speed
 * 1 at frame 6, grab at 5. */
void n_tyrant_DodgeAct(void) {
    FRAME(0x20);
    u32 e = A0;
    ty_begin(e, 0x52, 0);
    if (ty_anim_still(e)) return;
    ty_speed_at(e, 6, 1);
    s16 step = EN_STEP(e);
    if (step == 0) {
        if (TW_FRAME == 5 && (u32)EN_DIST(e) < 0x7D1) ty_call(n_tyrant_Grab, e, 0xF, 0);
        if (ty_call(n_tyrant_AnimEvent, e, 1, 1)) EN_STEP(e) += 1;
    } else if (step == 1) {
        TW_DODGED = 0;
        ty_anim(e, 0, 0x40);
    }
    TW_MORPH_SET = TYRANT_MORPH_C;
    A0 = e;
    n_tyrant_MorphPick();
}

/* 8010385C (e): the shots it took (unless the sub-object is off, +0x29;
 * reeling only between animations): lose and count their damage; die at
 * 0 HP; else after 300 damage flinch by where it was hit (facing the
 * player) or now and then reel (25%, not facing). */
void n_tyrant_OnHit(void) {
    FRAME(0x20);
    u32 e = A0;
    EN_FLAGS(e) &= ~(u32)EF_SHOT;
    if (EN_HP(e) > 0 && !(EN_ACTION(e) == 0x13 && !ty_anim_still(e)) && GV(u8, TW_SUB + 0x29) != 0) {
        u32 w = TW;
        u8 parts = 0;
        if (EN_NHITS(e) >= 0) {
            for (s32 i = 0;; ) {
                u16 d = GV(u16, e + 0x330 + (u32)i * 2);
                GV(u16, e + 0x31E) = (u16)(GV(u16, e + 0x31E) - d);
                GV(u16, w + 0x1C) = (u16)(GV(u16, w + 0x1C) + GV(u16, e + 0x330 + (u32)i * 2));
                u8 p = GV(u8, e + 0x326 + (u32)i);
                if (p < 6) {
                    LOOP_POLL(0x80103944);
                    if (p == 0 || p == 2) parts |= 1;
                    else if (p == 1 || p == 3) parts |= 2;
                    else if (p == 5) parts |= 3;
                }
                i++;
                if (EN_NHITS(e) < i) break;
                LOOP_POLL(0x80103974);
            }
        }
        if (EN_HP(e) <= 0) {
            TW_MODE = 0xA;
            ty_anim(e, 9, 0x10);
            gcall(FN_EN_KILLED, e, (u32)(s32)EN_ROOMID(e), 0, 0);
        } else if (!ty_call(n_tyrant_NotFacing, e, 0, 0)) {
            if (TW_DAMAGE >= 0x12C && parts != 0) {
                u32 which;
                if (parts == 1) which = 0;
                else if (parts == 2) which = 1;
                else which = (en_rand() >> 2) & 1;
                if (which == 0) ty_anim(e, 3, 0x10);
                else if (which == 1) ty_anim(e, 4, 0x10);
                TW_DAMAGE = 0;
            }
        } else if (TW_DAMAGE >= 0x12C) {
            if ((s32)en_rand() % 100 < 0x19) {
                ty_anim(e, 7, 0x10);
                TW_DAMAGE = 0;
            }
        }
    }
    EN_NHITS(e) = -1;
    GCALL1(FN_EN_HIT_PRE, e);
}

/* 80103B08 (coord): the model's joint hook: its root keeps no rotation
 * (none either once dead); its joint 1 carries the sub-object (+0x28 of
 * the work block): placed at the joint (not while held, nor its height
 * while 0x801067B0 is set), turned with it, unless the sub-object has an
 * offset of its own. */
void n_tyrant_JointHook(void) {
    FRAME(0x18);
    u32 c = A0;
    u32 cur = ep_enemyCur;
    if (c == GV(u32, cur + 0x16C)) {
        GV(u32, c + 0x20) = 0;
        if (EN_HP(cur) <= 0) GV(u32, c + 0x18) = 0;
    }
    cur = ep_enemyCur;
    u32 root = GV(u32, cur + 0x16C);
    if (c == root + 0xA0) {
        u32 sub = GV(u32, TYRANT_WORK_BASE + (u32)EN_SLOT(cur) * TYRANT_WORK_SIZE + 0x28);
        if (GV(u32, sub + 0x1CC) == 0) {
            if (GV(u8, TW + 6) == 0) {
                GV(u32, sub + 0x198) = GV(u32, root + 0x88);
                GV(u32, sub + 0x1A0) = GV(u32, GV(u32, cur + 0x16C) + 0x90);
            }
            if (g_tyrantNoRise == 0) GV(u32, sub + 0x19C) = GV(u32, GV(u32, cur + 0x16C) + 0x8C);
            GV(u16, sub + 0x1C6) = (u16)(GV(u16, cur + 0x1C6) + GV(u16, TW + 0x24) + 0x400);
        } else {
            GV(u32, sub + 0x198) = 0;
            GV(u32, sub + 0x19C) = 0;
            GV(u32, sub + 0x1A0) = 0;
            GV(u16, sub + 0x1C6) = 0;
        }
        gcall(F_OBJ_MATRIX, sub + 0x180, sub + 0x170, 0, 0);
    }
    V0 = 0;
}

/* 801041F8 (e): stage 2: in room 4 keep 0x800AB51C (cleared while the
 * player is held, restored after); room 16's check. */
void n_tyrant_Stage2Check(void) {
    FRAME(0x18);
    if (g_room == 4) {
        if (ep_grabLock == 0) {
            D_800AB51C = g_tyrantSaved;
        } else if (D_800AB51C != 0) {
            g_tyrantSaved = D_800AB51C;
            D_800AB51C = 0;
        }
    } else if (g_room == 0x10) {
        GCALL0(FN_TY_STAGE2_R16);
    }
}

/* 80104940 (e): the stage's check (stages 2, 6, 11, 12, 15, 16): 0 to
 * skip the frame. */
void n_tyrant_StageCheck(void) {
    FRAME(0x18);
    u32 e = A0;
    u32 s = (u32)g_stage - 2;
    if (!(s < 0xF)) { V0 = 1; return; }
    LOOP_POLL(0x80104974);
    switch (s + 2) {
    case 2: A0 = e; n_tyrant_Stage2Check(); V0 = 1; return;
    case 6: GCALL1(0x80104510u, e); return;
    case 11: GCALL1(0x80104604u, e); V0 = 1; return;
    case 12: GCALL1(0x80104778u, e); V0 = 1; return;
    case 15: GCALL1(0x801047A8u, e); V0 = 1; return;
    case 16: GCALL1(0x80104910u, e); return;
    default: V0 = 1; return;
    }
}

/* 801049FC (e): state 1, active: the action's handler. */
void n_tyrant_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, TYRANT_ACTIONS + (u32)((s32)EN_ACTION(e) << 2)), e);
}

/* 80104AB0: state 4: nothing. */
void n_tyrant_StateNone(void) {
}

/* 80105080 (e): set up: a lunge start (animation 17) goes straight to
 * mode 12; in stages 5 and 6 find its tracking slot and area, and copy
 * its work block for the copy two slots on. */
void n_tyrant_Setup(void) {
    FRAME(0x28);
    u32 e = A0;
    u32 slot = EN_SLOT(e);
    if (EN_ANIM(e) == 0x11) {
        TW_MODE = 0xC;
        ty_anim(e, 0, 0x40);
        V0 = 1;
        return;
    }
    if (!((u32)(g_stage - 5) < 2)) { V0 = 0; return; }
    s32 t = (s32)GCALL1(FN_TRACK_SLOT, e);
    GV(s32, e + 0x354) = t;
    if (t < 0) {
        GV(s32, e + 0x354) = -1;
    } else {
        u32 a = GV(u8, g_areaTable + 4);
        if (!((s32)GV(u8, g_areaTable + 5) < (s32)a)) {
            for (;;) {
                if (gcall(FN_IN_AREA, (u32)EN_X(e), (u32)EN_Z(e), a, 0)) {
                    GV(u16, e + 0x854) = (u16)a;
                    GV(u16, e + 0x856) = (u16)a;
                    break;
                }
                a++;
                if ((s32)GV(u8, g_areaTable + 5) < (s32)a) break;
                LOOP_POLL(0x8010514C);
            }
        }
    }
    if (!(EN_SLOT(e) < 3)) GV(u8, e + 0x2B) |= 2;
    u32 src = TYRANT_WORK_BASE + slot * TYRANT_WORK_SIZE;
    u32 dst = TYRANT_WORK_BASE + (slot + 2) * TYRANT_WORK_SIZE;
    for (u32 k = 0;; k += 0x10) {
        for (u32 j = 0; j < 0x10; j += 4) GV(u32, dst + k + j) = GV(u32, src + k + j);
        if (k + 0x10 == 0x1E0) break;
        LOOP_POLL(0x801051F4);
    }
    GV(u32, dst + 0x1E0) = GV(u32, src + 0x1E0);
    GV(u32, dst + 0x1E4) = GV(u32, src + 0x1E4);
    GV(u8, src + 0xA) = (u8)slot;
    GV(u8, dst + 0xA) = (u8)(slot + 2);
    V0 = 0;
}

/* 80105270 (e): every frame. */
void n_tyrant_Update(void) {
    FRAME(0x30);
    u32 e = A0;
    g_tyrantNoRise = 0;
    u32 w = TYRANT_WORK_BASE + (u32)EN_SLOT(e) * TYRANT_WORK_SIZE;
    TW = w;
    GV(u16, w + 0x24) = 0;
    if (GV(u8, w + 5) == 0) {
        GV(u8, w + 5) = 1;
        GV(u32, TW_SUB + 0x1CC) = GV(u32, e + 0x16C) + 0x50;
        gcall(F_OBJ_MATRIX, EN_OBJ(e), 0, 0, 0);
        gcall(F_OBJ_MATRIX, TW_SUB + 0x180, 0, 0, 0);
        GV(u8, TW_SUB + 0x29) = GV(u8, e + 0x29);
        A0 = e;
        n_tyrant_Setup();
        return;
    }
    if (GV(u8, w + 8) == 0) {
        A0 = e;
        n_tyrant_StageCheck();
        if (V0 == 0) return;
    }
    if ((u32)(g_stage - 5) < 2) GCALL1(FN_TY_STAGE5, e);
    u32 an = EN_ANIMSTATE(e);
    if (GV(u16, an + 0x16) < GV(u16, an + 0x14)) {
        TW_FRAME = 0;
    } else {
        TW_PREV_FRAME = TW_FRAME;
        if ((s32)TW_ANIM_FRAME != (s32)GV(u16, EN_ANIMSTATE(e) + 0x14)) TW_FRAME += 1;
        TW_ANIM_FRAME = (s16)GV(u16, EN_ANIMSTATE(e) + 0x14);
    }
    GCALL1(GV(u32, TYRANT_STATES + (u32)((s32)EN_STATE(e) << 2)), e);
    A0 = e;
    n_tyrant_MorphFilter();
    TW_TICK += 1;
    if (EN_FLAGS(e) & EF_SHOT) {
        gcall(FN_EN_SET_SPEED, e, 7, 0, 0);
        A0 = e;
        n_tyrant_OnHit();
        TW_HIT = 1;
    }
    if (TW_HIT != 0) {
        if (g_playerMoveFlags & 0x3F) TW_HIT = 0;
        else if (g_playerAction == 2) GCALL1(FN_EN_HIT_POST, e);
    }
    if (g_playerHp <= 0 && TW_MODE != 7 && TW_MODE != 8) {
        u32 v = sp + 0x10;
        gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-0xA, 0);
        u32 x = MEM_R32(v + 0) * 200, y = MEM_R32(v + 4) * 200, z = MEM_R32(v + 8) * 200;
        MEM_W32(v + 0, x);
        MEM_W32(v + 4, y);
        MEM_W32(v + 8, z);
        GV(u32, PLAYER_POS_ADDR + 0) = (u32)EN_X(e) + x;
        GV(u32, PLAYER_POS_ADDR + 4) = (u32)EN_Y(e) + y;
        GV(u32, PLAYER_POS_ADDR + 8) = (u32)EN_Z(e) + z;
        TW_MODE = 7;
        ty_anim(e, 0, 0x40);
    }
    TW_SEED += 1;
    g_tyrantAmmoLeft = GV(u8, g_currentWeapon + 2);
}
