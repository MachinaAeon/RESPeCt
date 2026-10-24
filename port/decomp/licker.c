/* The licker (enemy program ENEMPROG 53, loaded in enemy slot B at
 * 0x80100000; ENEMPROG 37 is the same code for slot A). Identified by
 * what it does: it clings to the ceiling (y below -3000: upside down) or
 * a wall, drops to the floor, and lashes out with a tongue that grabs the
 * player.
 *
 * licker_Update (0x80107094) runs every frame for each one: it picks the
 * licker's work block, runs the handler of its state (0x801077C4), the
 * tongue (licker_Tongue), the walking speed by distance, the hit check,
 * and keeps a shadow / ceiling marker object in step (licker_Marker).
 * In the active state (licker_StateActive) it runs an action, with a pair
 * of handlers per action (0x801076EC every frame, 0x80107758 while the
 * action's phase is negative), like the zombie.
 * Decompiled actions include 0 (waiting), 2 (crawling), 3 (hunting: after
 * the player round the walls, along the ceiling or the floor), 11 (the
 * leap up to the ceiling) and the intro.
 *
 * Each licker has a 0x74-byte work block (0x801077F0 + slot * 0x74; the
 * current one's address is kept at 0x801078D8, see LW_*).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"

void n_licker_TryDrop(void);
void n_licker_StepSound(void);
void n_licker_ShowParts(void);
void n_licker_AnimEvent(void);
void n_licker_IdleThink(void);
void n_licker_IdlePre(void);
void n_licker_TurnStep(void);
void n_licker_IntroPre(void);
void n_licker_IntroStep(void);
void n_licker_RoomPre(void);
void n_licker_RoomSetup(void);
void n_licker_StateActive(void);
void n_licker_Tongue(void);
void n_licker_SpeedByDistance(void);
void n_licker_Marker(void);
void n_licker_Setup(void);
void n_licker_Update(void);
void n_licker_TryLeap(void);
void n_licker_WakeHit(void);
void n_licker_PlayerInCone(void);
void n_licker_CrossedWall(void);
void n_licker_WallAhead(void);
void n_licker_CrossedEdge(void);
void n_licker_EdgeAhead(void);
void n_licker_CalmDown(void);
void n_licker_Spot(void);
void n_licker_AvoidWalls(void);
void n_licker_HuntPre(void);
void n_licker_HuntStep(void);
void n_licker_LeapPre(void);
void n_licker_LeapStep(void);

/* ---- the licker's work block ---- */
#define LW               GV(u32, 0x801078D8)
#define LW_BASE_SPEED    GV(s32, LW + 0x00)
#define LW_FLAGS         GV(u32, LW + 0x0C)   /* 1 asleep, 0x1000 on the ceiling, 0x4000 no drop, ... */
#define LW_VY            GV(s32, LW + 0x18)   /* intro: vertical speed */
#define LW_TIMER         GV(s32, LW + 0x1C)
#define LW_TONGUE        GV(u32, LW + 0x38)   /* the tongue's object (parts shown one by one) */
#define LW_TONGUE_STATE  GV(s32, LW + 0x3C)
#define LW_TONGUE_LEN    GV(s32, LW + 0x40)   /* the last part shown */
#define LICKER_WORK_BASE 0x801077F0u
#define LICKER_WORK_SIZE 0x74

#define LICKER_ACT_PRE   0x801076ECu
#define LICKER_ACT_STEP  0x80107758u
#define LICKER_STATES    0x801077C4u
#define FN_EN_DIST       0x8007F4E8u   /* (e): +0x868 */
#define FN_EN_ANIM_SOUND 0x8008127Cu   /* (e, bank, sound) */
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_FLASH         0x800731BCu
#define FN_MUSIC_PLAY    0x8001209Cu
#define FN_OBJ_NEW       0x80077044u   /* (): a new model object */
#define FN_OBJ_ADD       0x8006D158u   /* (obj): to the draw list */
/* ovl53 functions (decompiled below: licker_EdgeAhead, licker_WallAhead,
 * licker_PlayerInCone, licker_WakeHit) */
#define FN_L_SEES_PLAYER 0x80100F10u   /* (e): the edge ahead's height, 0 if none */
#define FN_L_WALL_TURN   0x80100C34u   /* (e, dist) */
#define FN_L_FACING      0x80100830u   /* (e, spread) */
#define FN_L_WAKE_HIT    0x8010044Cu
/* ovl53 functions not decompiled */
#define FN_L_CAN_GRAB    0x8010214Cu
#define FN_L_ON_HIT      0x8010260Cu
#define FN_L_PLAYER_SIDE 0x801007A8u   /* (e): side of its facing line the player is on */
#define FN_L_WALL_DIST   0x8010074Cu   /* (e, wall) */
#define FN_L_EDGE_DIST   0x80100E88u   /* (e, edge) */
#define FN_L_EDGE_HEIGHT 0x80100EECu   /* (edge) */
#define FN_L_DIST2D      0x801006FCu   /* (a, b): x/z distance */
#define FN_L_PICK_MOVE   0x80101940u   /* (e): 0-3, what to do on seeing the player */
#define FN_L_LUNGE       0x80101680u   /* (e, target) */
#define FN_EN_HIT_ZONE   0x8008216Cu   /* (e): where the player is relative to it */
#define EVENT_FLAGS      0x80062FC0u

/* Is the player firing with the sight on screen? */
static int licker_PlayerFiring(void) {
    return g_fireRequest != 0 && g_reticleX >= 0 && g_reticleY >= 0;
}

static u32 licker_call2(void (*f)(void), u32 a0, u32 a1) {
    A0 = a0; A1 = a1;
    f();
    return V0;
}

static u32 licker_AnimEventCall(u32 e, u32 part, u32 k) {
    A0 = e; A1 = part; A2 = k;
    n_licker_AnimEvent();
    return V0;
}

/* 801001CC (e): on the ceiling with a clear view of the player, sometimes
 * (178/256) drop down (action 8) unless shot, held or told not to.
 * Returns 1 if it dropped. */
void n_licker_TryDrop(void) {
    FRAME(0x20);
    u32 e = A0;
    V0 = 0;
    if (EN_FLAGS(e) & EF_SEES) return;
    if ((s32)GCALL1(FN_L_SEES_PLAYER, e) >= 0) { V0 = 0; return; }
    if (!(GV(s32, LW + 0x28) < -0xB9F)) { V0 = 0; return; }
    if ((en_rand() & 0xFF) >= 0xB2) { V0 = 0; return; }
    if (EN_FLAGS(e) & 0x200) { V0 = 0; return; }
    if (LW_FLAGS & 0x4000) { V0 = 0; return; }
    GCALL3(FN_EN_SET_ACTION, e, 8, (s32)EN_ANIM(e));
    V0 = 1;
}

/* 801003CC (e): crawling sounds: every 32 frames in animation 1, every
 * 16 in animations 2 and 0x11. */
void n_licker_StepSound(void) {
    FRAME(0x18);
    u32 e = A0;
    u32 mask;
    switch (EN_ANIM(e)) {
    case 1: mask = 0x1F; break;
    case 2: case 0x11: mask = 0xF; break;
    default: return;
    }
    if ((ep_frameCount & mask) == 0) GCALL3(FN_EN_ANIM_SOUND, e, 0xC, 5);
}

/* 801004B4 (obj, show): set the visible byte of parts 1.. (+0x28 of
 * them). */
void n_licker_ShowParts(void) {
    u32 o = A0;
    u32 i = 1;
    while ((s32)i < (s32)GV(u8, o + 0x28)) {
        GV(u8, o + i) = (u8)A1;
        i++;
    }
    V0 = o + i;
}

/* 801004E8 (e, part, k): the animation event bit k frames ahead (see
 * zombie_AnimEvent; the same code). */
void n_licker_AnimEvent(void) {
    u32 e = A0, k = A2;
    u32 p = EN_ANIMPART(e, A1);
    u32 frames = GV(u32, EN_ANIMDATA(e) + 8) + 4;
    u32 w;
    if (GV(s8, p + 0xD) >= 0) w = GV(u32, frames + (u32)GV(u16, p + 0x16) * 4 + k * 4);
    else w = GV(u32, frames + (u32)GV(u16, p + 0x14) * 4 - k * 4);
    V0 = w >> 31;
}

/* 801016C8 (e): waiting: wake when shot, when the player fires, comes
 * within 2000 (5000 if LW +0xC & 0x2000) or runs (unless & 0x1000).
 * Awake, it drops from the ceiling (action 0xC) if up there, gets up
 * (action 1, from sleep: & 1), reacts to the shot, or sets off (action 1
 * with a random pause). */
void n_licker_IdleThink(void) {
    FRAME(0x20);
    u32 e = A0;
    int wake = (EN_FLAGS(e) & EF_SHOT) || licker_PlayerFiring();
    if (!wake) {
        if (EN_DIST(e) < 0x7D1) wake = 1;               /* FN_EN_DIST */
        else if ((LW_FLAGS & 0x2000) && EN_DIST(e) < 0x1389) wake = 1;
        else if (LW_FLAGS & 0x1000) return;
        else if (g_playerMoveSpeed < -0x50) wake = 1;
        else return;
    }
    if ((LW_FLAGS & 0x3000) && EN_Y(e) != 0) {
        GCALL4(FN_EN_SET_ACTION_B, e, 0xC, 0x12, 2);
        GV(s16, e + 0x862) = 6;
        EN_STATE(e) = 2;
        return;
    }
    if (LW_FLAGS & 1) {
        GCALL4(FN_EN_SET_ACTION_B, e, 1, 0x16, 4);
        LW_FLAGS = ((LW_FLAGS | 8) & ~1u) | 2;
        GV(s16, e + 0x862) = 6;
        return;
    }
    if (EN_FLAGS(e) & EF_SHOT) {
        GCALL1(FN_L_WAKE_HIT, e);
        return;
    }
    u32 r = en_rand() & 1;
    u32 w = LW;
    GV(u32, w + 8) = r * 30 + 2;
    GV(u32, w + 0xC) |= 5;
    GCALL4(FN_EN_SET_ACTION_B, e, 1, 0x16, 4);
}

/* 80102A40 (e): action 0 every frame: drop, or else wait. */
void n_licker_IdlePre(void) {
    FRAME(0x18);
    u32 e = A0;
    A0 = e;
    n_licker_TryDrop();
    if (V0 == 0) {
        A0 = e;
        n_licker_IdleThink();
    }
}

/* 80102A74 (e): crawling: when +0x320 & 0x20 (blocked), test a quarter
 * turn either way (0x80100C34) and turn (0x350 units, 11 a frame) to the
 * free side or a random one; otherwise crawl on, and at the end of a
 * stride sometimes (51/256) pause (step 3) and set off again at the
 * type's speed. */
void n_licker_TurnStep(void) {
    FRAME(0x28);
    u32 e = A0;
    LW_FLAGS &= ~0x400000u;
    switch (EN_STEP(e)) {
    case 0:
        if (EN_FLAGS(e) & 0x20) {
            u16 yaw = GV(u16, e + 0x1C6);
            GV(u16, e + 0x1C6) = (yaw + 0x400) & 0xFFF;
            s32 left = (s32)GCALL2(FN_L_WALL_TURN, e, -(s32)EN_SPEED(e));
            GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) - 0x800) & 0xFFF;
            s32 right = (s32)GCALL2(FN_L_WALL_TURN, e, -(s32)EN_SPEED(e));
            GV(u16, e + 0x1C6) = yaw;
            if (left >= 0 && right < 0) { GV(s32, e + 0x350) = 0x400; EN_STEP(e) = 2; return; }
            if (left < 0 && right >= 0) { GV(s32, e + 0x350) = 0x400; EN_STEP(e) = 1; return; }
            GV(s32, e + 0x350) = 0x7E8;
            EN_STEP(e) = (s16)((en_rand() & 1) + 1);
            return;
        }
        if (licker_AnimEventCall(e, 1, 1) != 0) {
            if ((en_rand() & 0xFF) >= 0x33) return;
            if (EN_ANIM(e) == 0x11) GCALL3(FN_EN_SET_ANIM_B, e, 3, 2);
            else GCALL2(FN_EN_SET_ANIM, e, 3);
            GCALL2(FN_EN_SET_SPEED, e, 6);
            EN_STEP(e) = 3;
            return;
        }
        GCALL3(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (s32)EN_SPEED(e));
        return;
    case 1:
    case 2: {
        s32 left = GV(s32, e + 0x350);
        if (left > 0) {
            GV(s32, e + 0x350) = left - 0xB;
            GV(u16, e + 0x1C6) += EN_STEP(e) == 1 ? 0xB : (u16)-0xB;
            return;
        }
        EN_STEP(e) = 0;
        EN_FLAGS(e) &= ~0x20u;
        return;
    }
    case 3:
        if (licker_AnimEventCall(e, 1, 1) == 0) return;
        u32 fast;
        if ((en_rand() & 0xFF) < 0x19) { GCALL3(FN_EN_SET_ANIM_B, e, 0x11, 4); fast = 1; }
        else { GCALL2(FN_EN_SET_ANIM, e, 1); fast = 0; }
        EN_STEP(e) = 0;
        LW_BASE_SPEED = (s16)GCALL2(FN_EN_BASE_SPEED, e, fast);
        return;
    }
}

/* 80104EF8: the intro action, nothing every frame. */
void n_licker_IntroPre(void) {
}

/* 80104F00 (e): the intro in its room: wait on the ceiling until the
 * player fires or moves, then drop: shriek, flash, music, a leap up and a
 * fall to the floor, and land into action 3. */
void n_licker_IntroStep(void) {
    FRAME(0x20);
    u32 e = A0;
    switch ((u32)EN_STEP(e)) {
    case 0:
        LW_FLAGS |= 0x1000000;
        EN_X(e) = 0x1388;
        EN_Z(e) = -0x17E;
        GV(u16, e + 0x1C6) = 0x400;
        GV(u16, e + 0x1C8) = 0;
        EN_STEP(e) += 1;
        /* fall through */
    case 1:
        if (!licker_PlayerFiring() && !((g_playerMoveFlags & 1) | ((g_playerMoveFlags >> 1) & 1))) return;
        EN_STEP(e) += 1;
        return;
    case 2:
        if (licker_AnimEventCall(e, 1, 2) == 0) return;
        GCALL3(FN_EN_SET_ANIM_B, e, 0x14, 2);
        EN_STEP(e) += 1;
        return;
    case 3:
        if (GV(u16, EN_ANIMSTATE(e) + 0x14) != 0x3D0) return;
        EN_ALIVE(e) = 0;
        LW_VY = 0x190;
        LW_TIMER = 0x14;
        EN_STEP(e) += 1;
        GCALL4(FN_SOUND_CUE2, 6, 2, 0x7F, 0x7F);
        GCALL4(FN_SOUND_CUE2, 6, 3, 0x7F, 0x7F);
        GCALL4(FN_FLASH, 0xFF, 0xFF, 0x20, 9);
        GCALL2(FN_FLAG_SET, EVENT_FLAGS, 0x3E);
        GV(s16, 0x80062E78) = 0;
        GCALL1(FN_MUSIC_PLAY, 0);
        return;
    case 4: {
        if (EN_X(e) < 0x960) {
            LW_FLAGS &= ~0x1000u;
            GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 0;
        }
        u32 w = LW;
        if (GV(s32, w + 0x1C) != 0) {
            GV(s32, w + 0x1C) -= 1;
            GCALL3(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), -100);
            EN_VX(e) = (s32)((u32)EN_VX(e) << 1);
            EN_VZ(e) = (s32)((u32)EN_VZ(e) << 1);
            EN_VY(e) = (s32)((u32)EN_VY(e) << 1);
            w = LW;
            EN_Y(e) = (s32)((u32)EN_Y(e) - (u32)GV(s32, w + 0x18));
            s32 vy = GV(s32, w + 0x18);
            GV(s32, w + 0x18) = vy - vy / 8;
            if (EN_Y(e) < 0x5DC) GCALL2(0x801004B4u, e, 1);
            return;
        }
        GV(s32, w + 0x1C) = 10;
        GV(s32, w + 0x18) = 0x14;
        EN_ALIVE(e) = 1;
        EN_STEP(e) += 1;
        GCALL2(FN_EN_SET_SPEED, e, 2);
        return;
    }
    case 5: {
        u32 w = LW;
        if (GV(s32, w + 0x1C) != 0) {
            GV(s32, w + 0x1C) -= 1;
            GCALL3(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), -100);
        }
        w = LW;
        EN_Y(e) = (s32)((u32)EN_Y(e) + (u32)GV(s32, w + 0x18));
        s32 vy = GV(s32, w + 0x18);
        GV(s32, w + 0x18) = vy + vy / 8;
        if (EN_Y(e) >= 0) {
            EN_Y(e) = 0;
            EN_ALIVE(e) = 1;
            EN_STEP(e) += 1;
            return;
        }
        if (EN_ANIM(e) == 0x15) {
            if (GV(u16, EN_ANIMSTATE(e) + 0x14) == 0x3D3) EN_ALIVE(e) = 0;
            return;
        }
        if (licker_AnimEventCall(e, 1, 3) == 0) return;
        GCALL3(FN_EN_SET_ANIM_B, e, 0x15, 2);
        return;
    }
    case 6:
        if (licker_AnimEventCall(e, 1, 1) == 0) return;
        GCALL4(FN_EN_SET_ACTION_B, e, 3, 0x11, 2);
        LW_FLAGS = ((LW_FLAGS & ~0x1000000u) | 2) & ~0x380u;
        EN_STATE(e) = 1;
        return;
    }
}

/* 80105508: the room-setup action, nothing every frame. */
void n_licker_RoomPre(void) {
}

/* 80105510 (e): place the licker by its room (+0x850) and the event
 * flags: room 0x6D: on the ceiling for the intro (action 0x12) the first
 * time, else on the floor (action 2) or asleep (action 0); room 0x6E: on
 * the floor (action 2). */
void n_licker_RoomSetup(void) {
    FRAME(0x20);
    u32 e = A0;
    switch (EN_ROOMID(e)) {
    case 0x6D:
        GCALL2(FN_FLAG_SET, EVENT_FLAGS, 0x3B);
        GCALL2(FN_FLAG_SET, EVENT_FLAGS, 0x34);
        if (GCALL2(FN_FLAG_TEST, EVENT_FLAGS, 0x3B) == 0) {
            LW_FLAGS |= 0x5001;
            GCALL2(FN_FLAG_SET, EVENT_FLAGS, 0x3B);
            GCALL3(FN_EN_SET_ACTION, e, 0, (s32)EN_ANIM(e));
            return;
        }
        LW_FLAGS |= 0x4000;
        s32 z;
        if (GCALL2(FN_FLAG_TEST, EVENT_FLAGS, 0x34) != 0) {
            if (GCALL2(FN_FLAG_TEST, EVENT_FLAGS, 0x3E) == 0) {
                LW_FLAGS |= 0x1000;
                GCALL2(0x801004B4u, e, 0);
                EN_X(e) = 0x1388;
                EN_Y(e) = 0x9C4;
                EN_Z(e) = -0x17E;
                GV(u16, e + 0x1C8) = 0;
                GV(u16, e + 0x1C4) = 0;
                GV(u16, e + 0x1C6) = 0x400;
                GCALL3(FN_EN_SET_ACTION, e, 0x12, 0x13);
                return;
            }
            EN_X(e) = -0x1F4;
            z = -0x180;
        } else {
            EN_X(e) = -0x1F4;
            z = 0x7D0;
        }
        EN_Y(e) = 0;
        EN_Z(e) = z;
        GV(u16, e + 0x1C8) = 0;
        GV(u16, e + 0x1C6) = 0;
        GV(u16, e + 0x1C4) = 0;
        GCALL3(FN_EN_SET_ACTION, e, 2, 1);
        return;
    case 0x6E:
        if (GCALL2(FN_FLAG_TEST, EVENT_FLAGS, 0x3B) != 0) {
            EN_X(e) = -0x3E8;
            EN_Y(e) = 0;
            EN_Z(e) = 0x1388;
        } else {
            GCALL2(FN_FLAG_SET, EVENT_FLAGS, 0x3B);
        }
        GCALL3(FN_EN_SET_ACTION, e, 2, (s32)EN_ANIM(e));
        LW_FLAGS |= 0x4000;
        return;
    }
}

/* 801067EC (e): state 1, active: crawling sounds, the action's handlers;
 * at the end of the animation stop moving. */
void n_licker_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    A0 = e;
    n_licker_StepSound();
    GCALL1(GV(u32, LICKER_ACT_PRE + (u32)((s32)EN_ACTION(e) << 2)), e);
    if (EN_PHASE(e) < 0)
        GCALL1(GV(u32, LICKER_ACT_STEP + (u32)((s32)EN_ACTION(e) << 2)), e);
    u32 p = EN_ANIMSTATE(e);
    if (GV(u16, p + 0x14) == GV(u16, p + 0xE)) {
        EN_VZ(e) = 0;
        EN_VX(e) = 0;
    }
}

/* Grab the player with the tongue if it's facing them, close enough and
 * allowed. */
static void licker_TongueGrab(u32 e, s32 reach) {
    if (!(EN_FLAGS(e) & EF_SEES)) return;
    if (GCALL2(FN_L_FACING, e, 0xC) == 0) return;
    if (!((s32)GCALL1(FN_EN_DIST_UPDATE, e) < reach)) return;
    if (GCALL1(FN_L_CAN_GRAB, e) == 0) return;
    GCALL3(FN_EN_GRAB_PLAYER, e, 8, 0);
    GCALL1(FN_EN_GRAB_START, e);
}

/* 801069D8 (e): the tongue (LW +0x38, its parts shown one by one):
 * 1 aim it from the head, 2 shoot it out, 3 draw it back in a part a
 * frame (grabbing at length 4 within 2500), 6 grab within 3500,
 * 7 run it out again and stop. Then update its matrix. */
void n_licker_Tongue(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 w = LW;
    u32 state = GV(u32, w + 0x3C);
    u32 t = GV(u32, w + 0x38);
    switch (state) {
    case 1:
        GV(s32, t + 0x1CC) = GV(s32, e + 0x16C) + 0x500;
        GV(u16, t + 0x1C4) = GV(u16, 0x801077D8);
        GV(u16, t + 0x1C6) = GV(u16, 0x801077DA);
        GV(u16, t + 0x1C8) = GV(u16, 0x801077DC);
        LW_TONGUE_STATE += 1;
        break;
    case 2: {
        s32 n = GV(s32, t + 0x1D0);
        LW_TONGUE_LEN = n;
        GV(u8, t + (u32)n) = 1;
        LW_TONGUE_STATE += 1;
        break;
    }
    case 3: {
        if (EN_FLAGS(e) & EF_SHOT) {
            LW_TONGUE_STATE = 6;
            LW_TONGUE_LEN -= 1;
            break;
        }
        s32 n = LW_TONGUE_LEN;
        GV(u8, t + (u32)n) = 0;
        LW_TONGUE_LEN = n - 1;
        if (LW_TONGUE_LEN == 4) licker_TongueGrab(e, 0x9C5);
        if (LW_TONGUE_LEN == 0) {
            LW_TONGUE_LEN = 1;
            LW_TONGUE_STATE += 1;
        }
        GV(u8, t + (u32)LW_TONGUE_LEN) = 1;
        break;
    }
    case 4:
    case 5:
        LW_TONGUE_STATE += 1;
        break;
    case 6:
        licker_TongueGrab(e, 0xDAD);
        LW_TONGUE_STATE += 1;
        break;
    case 7: {
        s32 n = LW_TONGUE_LEN;
        GV(u8, t + (u32)n) = 0;
        LW_TONGUE_LEN = n + 1;
        if (LW_TONGUE_LEN == GV(s32, t + 0x1D0)) LW_TONGUE_STATE = 0;
        else GV(u8, t + (u32)LW_TONGUE_LEN) = 1;
        break;
    }
    default:
        break;
    }
    GCALL2(F_OBJ_MATRIX, t + 0x180, t + 0x170);
}

/* 80106CCC (e): walking speed from the base speed by distance to the
 * player (x1.2 to 10000, x1.5 to 15000, x1.75 to 20000, x2 farther);
 * never faster than -0x7F. */
void n_licker_SpeedByDistance(void) {
    FRAME(0x18);
    u32 e = A0;
    EN_SPEED(e) = (s16)GV(u16, LW);
    if (EN_DIST(e) >= 0x1389) {                       /* FN_EN_DIST */
        s32 v = EN_SPEED(e);
        if (EN_DIST(e) < 0x2711) v = v * 6 / 5;
        else if (EN_DIST(e) < 0x3A99) v = v * 3 / 2;
        else if (EN_DIST(e) < 0x4E21) v = v * 7 / 4;
        else v = v * 2;
        EN_SPEED(e) = (s16)v;
    }
    if (EN_SPEED(e) < -0x7F) EN_SPEED(e) = -0x7F;
}

/* 80106DE8 (e): keep the marker object (+0x8BC) at the licker's height
 * (3000 lower and shown while on the ceiling, & 0x1000; 0 when upside
 * down); count down LW +0x44 while +0x320 & 0x200. */
void n_licker_Marker(void) {
    u32 e = A0;
    u32 m = GV(u32, e + 0x8BC);
    if (LW_FLAGS & 0x1000) {
        GV(s32, m + 0xB0) = EN_Y(e) - 0xBB8;
        GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 1;
    } else {
        GV(s32, m + 0xB0) = 0;
        if (EN_ROTX(e) != 0x800) GV(s32, GV(u32, e + 0x8BC) + 0xB0) = EN_Y(e);
    }
    V0 = 0;
    if (!(EN_FLAGS(e) & 0x200)) return;
    u32 w = LW;
    s32 c = GV(s32, w + 0x44);
    if (c == 0) return;
    GV(s32, w + 0x44) = c - 1;
    V0 = (u32)(c - 1);
    if (c - 1 < 0) GV(s32, w + 0x44) = 0;
}

/* 80106E94 (spawn, e): set a licker up: model, hit factors and
 * parameters (type 0xE has its own), upside down on the ceiling (y below
 * -3000) or sideways on a wall (y below 0), its work block, the tongue
 * object; in some rooms it never drops (LW +0xC & 0x4000). */
void n_licker_Setup(void) {
    FRAME(0x18);
    u32 spawn = A0, e = A1;
    GV(u32, D_800BBB80_ADDR + (u32)GV(s32, spawn) * 4) = 0x80107550u;
    GV(u32, e + 0x840) = EN_TYPE(e) == 0xE ? 0x8010745Cu : 0x80107368u;
    s32 y = EN_Y(e);
    GV(u32, e + 0x844) = 0x8010762Cu;
    if (y < -0xBB7) {
        GV(u16, e + 0x1C4) = 0x800;
        EN_FLAGS(e) |= 0x8200;
    } else if (y < 0) {
        GV(u16, e + 0x1C4) = (u16)-0x400;
        GV(u16, e + 0x1C8) = 0x400;
        EN_FLAGS(e) |= 0x8000;
    }
    LW = LICKER_WORK_BASE + (u32)EN_SLOT(e) * LICKER_WORK_SIZE;
    s16 speed = (s16)GCALL2(FN_EN_BASE_SPEED, e, 0);
    u32 w = LW;
    GV(s32, w + 0x00) = speed;
    for (u32 off = 4; off <= 0x2C; off += 4) GV(u32, w + off) = 0;
    GV(u16, w + 0x30) = 0;
    GV(u16, w + 0x32) = 0;
    GV(u32, w + 0x34) = 0;
    GV(u32, w + 0x44) = 0;
    GV(u32, w + 0x48) = 0;
    GV(u32, w + 0x4C) = 0;
    u32 t = GCALL0(FN_OBJ_NEW);
    w = LW;
    GV(u32, w + 0x38) = t;
    GV(u32, w + 0x40) = 0;
    GV(u32, w + 0x3C) = 0;
    GV(u32, w + 0x70) = 0;
    GCALL1(FN_OBJ_ADD, t);

    s32 stage = GV(s32, 0x800A866C), room = GV(s32, 0x800B3F08);
    int stay = (stage == 1 && room == 0) || (stage == 2 && (room == 6 || room == 9)) ||
               (stage == 0xB && room == 0) || (stage == 0xD && room == 2);
    if (stay) {
        LW_FLAGS |= 0x4000;
        V0 = LW_FLAGS;
    } else {
        V0 = stage == 0xD ? (u32)room : 0x800B0000u;
    }
}

/* 80107094 (e): a licker's frame (see the top of the file). */
void n_licker_Update(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 handler = GV(u32, LICKER_STATES + (u32)((s32)EN_STATE(e) << 2));
    LW = LICKER_WORK_BASE + (u32)EN_SLOT(e) * LICKER_WORK_SIZE;
    GCALL1(handler, e);
    A0 = e;
    n_licker_Tongue();
    A0 = e;
    n_licker_SpeedByDistance();
    int hit = (EN_FLAGS(e) & EF_SHOT) != 0;
    if (!hit) {
        u32 w = LW;
        if ((GV(u32, w + 0xC) & 0x3000000) == 0x2000000) {
            EN_HP(e) = 0;
            GV(u32, w + 0xC) &= ~0x2000000u;
            hit = 1;
        }
    }
    if (hit) {
        GCALL1(FN_L_ON_HIT, e);
        GCALL1(FN_EN_HIT_POST, e);
    }
    A0 = e;
    n_licker_Marker();
}

/* ------------------------------------------------------------------ */
/* Moving about: the hunt (action 3) and the leap to the ceiling (11). */

/* 80100294 (e): far from the player (5000 and more) with an edge ahead
 * whose height is low (a ceiling above -0xBA0) within 3000: now and then
 * (not on every 90th frame of its counter) leap: action 1 (76/256) or,
 * unless shot or told not to, drop (action 8). Returns 1 if it did. */
void n_licker_TryLeap(void) {
    FRAME(0x20);
    u32 e = A0;
    s32 d = (s32)GCALL1(FN_EN_DIST_UPDATE, e);
    V0 = 0;
    if (EN_FLAGS(e) & EF_SEES) return;
    if (d < 0x1389) return;
    A0 = e;
    n_licker_EdgeAhead();
    if (V0 == 0) return;
    u32 w = LW;
    V0 = 0;
    if (GV(s32, w + 0x2C) >= 0xBB9) return;
    if (GV(s32, w + 0x28) >= -0xB9F) return;
    s32 c = GV(s32, w + 8);
    if (c == c / 90 * 90) return;
    if ((en_rand() & 0xFF) < 0x4C) {
        GCALL3(FN_EN_SET_ACTION, e, 1, (s32)EN_ANIM(e));
    } else {
        if (EN_FLAGS(e) & 0x200) { V0 = 0; return; }
        if (LW_FLAGS & 0x4000) { V0 = 0; return; }
        GCALL3(FN_EN_SET_ACTION, e, 8, (s32)EN_ANIM(e));
    }
    V0 = 1;
}

/* 8010044C (e): woken by a shot: into action 3 (hunting) at the type's
 * fast speed. */
void n_licker_WakeHit(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL4(FN_EN_SET_ACTION_B, e, 3, 0x11, 2);
    s16 speed = (s16)GCALL2(FN_EN_BASE_SPEED, e, 1);
    u32 w = LW;
    GV(s32, w + 0) = speed;
    GV(u32, w + 8) = 0;
    GV(u32, w + 0xC) = (GV(u32, w + 0xC) | 2) & ~0x380u;
    V0 = (u32)-0x381;
}

/* 80100830 (e, spread): is the player in its view cone (turned round
 * when it is upside down)? */
void n_licker_PlayerInCone(void) {
    FRAME(0x50);
    V0 = en_player_in_cone(sp, A0, (s32)A1);
}

/* 801009B8 (from, to): the first wall the path crosses from its front, or
 * -1 (zombie_CrossedWall). */
void n_licker_CrossedWall(void) {
    FRAME(0x40);
    V0 = (u32)en_crossed_wall(A0, A1);
}

/* 80100C34 (e, dist): the wall crossed walking `dist` x 20 ahead, or -1. */
void n_licker_WallAhead(void) {
    FRAME(0x30);
    u32 e = A0;
    u32 v = sp + 0x10;
    en_point_ahead(e, v, (s32)A1);
    A0 = EN_POS(e); A1 = v;
    n_licker_CrossedWall();
}

/* 80100CE4 (from, to): the first area edge (0x800B60D0 list) the path
 * crosses, either way, or -1. */
void n_licker_CrossedEdge(void) {
    FRAME(0x40);
    V0 = (u32)en_crossed_segment(A0, A1, GV(u32, EP_EDGES_N + 4), GV(u8, EP_EDGES_N), 0x14, 0);
}

/* 80100F10 (e): the area edge 2000 ahead (kept in LW +0x24, -1 none):
 * its distance (+0x2C) and height (+0x28, returned); 0 if none. */
void n_licker_EdgeAhead(void) {
    FRAME(0x30);
    u32 e = A0;
    u32 v = sp + 0x10;
    en_point_ahead(e, v, 100);
    A0 = EN_POS(e); A1 = v;
    n_licker_CrossedEdge();
    s32 edge = (s32)V0;
    GV(s32, LW + 0x24) = edge;
    if (edge < 0) { V0 = 0; return; }
    u32 d = GCALL2(FN_L_EDGE_DIST, e, (u32)edge);
    u32 w = LW;
    GV(u32, w + 0x2C) = d;
    u32 h = GCALL1(FN_L_EDGE_HEIGHT, GV(u32, w + 0x24));
    GV(u32, LW + 0x28) = h;
    V0 = h;
}

/* 80101B18 (e): while the player keeps still and quiet (not running
 * within 8000, walking within 3000, firing, nor this +0x40000), count
 * frames (LW +8); after 600 settle down to crawling (action 2). Returns 1
 * then. */
void n_licker_CalmDown(void) {
    FRAME(0x20);
    u32 e = A0;
    int reset = 0;
    if (g_playerMoveSpeed < -0x50 && (s32)GCALL1(FN_EN_DIST, e) < 0x1F41) reset = 1;
    else if (((g_playerMoveFlags & 1) | ((g_playerMoveFlags >> 1) & 1)) &&
             (s32)GCALL1(FN_EN_DIST, e) < 0xBB9) reset = 1;
    else if (licker_PlayerFiring()) reset = 1;
    else if (EN_FLAGS(e) & 0x40000) reset = 1;
    if (reset) {
        GV(u32, LW + 8) = 0;
        V0 = 0;
        return;
    }
    u32 w = LW;
    s32 n = GV(s32, w + 8) + 1;
    GV(s32, w + 8) = n;
    if (n < 600) { V0 = 0; return; }
    GV(u32, w + 0xC) &= ~2u & ~4u;
    GCALL4(FN_EN_SET_ACTION_B, e, 2, (s32)EN_ANIM(e), 2);
    LW_BASE_SPEED = (s16)GCALL2(FN_EN_BASE_SPEED, e, 0);
    V0 = 1;
}

/* 80101C68 (e): the player in view, in its cone (+-11*12) and it on or
 * above the floor (y >= 0): pick what to do (0x80101940): 0 rear up
 * (action 1, facing kept in LW +0x20), 1 attack (action 5), 2 rear up,
 * 3 go for the player (action 4 or 0x18). */
void n_licker_Spot(void) {
    FRAME(0x20);
    u32 e = A0;
    if (!(EN_FLAGS(e) & EF_SEES)) return;
    if (licker_call2(n_licker_PlayerInCone, e, 0xC) == 0) return;
    if (EN_Y(e) < 0) return;
    LW_FLAGS |= 0x800000;
    switch ((s32)GCALL1(FN_L_PICK_MOVE, e)) {
    case 0:
        LW_FLAGS |= 0x14;
        GCALL4(FN_EN_SET_ACTION_B, e, 1, 0x16, 4);
        GV(s32, LW + 0x20) = (s16)GCALL2(FN_ANGLE_TO, g_player, EN_POS(e));
        return;
    case 1:
        GV(s32, LW + 0x70) += 1;
        GCALL4(FN_EN_SET_ACTION_B, e, 5, 0xD, 4);
        return;
    case 2:
        LW_FLAGS |= 0x24;
        GCALL4(FN_EN_SET_ACTION_B, e, 1, 0x16, 4);
        return;
    case 3:
        GCALL3(FN_EN_SET_ACTION, e, (en_rand() & 0xFF) < 0x80 ? 4 : 0x18, (s32)EN_ANIM(e));
        LW_FLAGS &= ~0x10000u & ~0x40000u;
        return;
    }
}

/* Which way to turn round a wall (1, 2, 0 don't) from the walls ahead
 * (ahead, at 100), after a quarter turn either way (left, right, at 50)
 * and the player's side. */
static s32 licker_AvoidDirection(u32 e, s32 ahead, s32 left, s32 right, s32 side) {
    if (side >= 0) { if (left == -1) return 1; }
    else if (right == -1) return 2;
    if (ahead == left && ahead != -1) return 2;
    if (ahead == right && ahead != -1) return 1;
    if (left != -1) {
        if (right == -1 && side < 0) return 2;
    } else {
        if (right != -1 && side >= 0) return 1;
        if (right == -1) return side < 0 ? 2 : 1;
    }
    /* close to the wall ahead: look an eighth of a turn either way */
    if ((s32)GCALL2(FN_L_WALL_DIST, e, (u32)ahead) >= 500) return 0;
    u16 yaw = GV(u16, e + 0x1C6);
    GV(u16, e + 0x1C6) = (yaw + 0x200) & 0xFFF;
    s32 l2 = (s32)licker_call2(n_licker_WallAhead, e, 100);
    GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) - 0x400) & 0xFFF;
    s32 r2 = (s32)licker_call2(n_licker_WallAhead, e, 100);
    GV(u16, e + 0x1C6) = yaw;
    if (l2 == -1) { if (r2 != -1) return 1; }
    else if (r2 == -1) return 2;
    s32 dr = (s32)GCALL2(FN_L_WALL_DIST, e, (u32)right);
    s32 dl = (s32)GCALL2(FN_L_WALL_DIST, e, (u32)left);
    if (ahead == -1) return 0;
    return dl < dr ? 2 : 1;
}

/* 80101E18 (e): crawling towards the player: when a wall is in the way
 * (or the player is out of its cone), turn along it (0x58 a frame) until
 * the way ahead is clear; else crawl on (LW +0x10: 0 straight, 1, 2
 * turning). */
void n_licker_AvoidWalls(void) {
    FRAME(0x30);
    u32 e = A0;
    if (GV(s32, LW + 0x10) == 0) {
        s32 dir = 0;
        s32 ahead = (s32)licker_call2(n_licker_WallAhead, e, 100);
        if (ahead != -1 || licker_call2(n_licker_PlayerInCone, e, 0x5A) == 0) {
            u16 yaw = GV(u16, e + 0x1C6);
            GV(u16, e + 0x1C6) = (yaw + 0x400) & 0xFFF;
            s32 left = (s32)licker_call2(n_licker_WallAhead, e, 0x32);
            GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) - 0x800) & 0xFFF;
            s32 right = (s32)licker_call2(n_licker_WallAhead, e, 0x32);
            GV(u16, e + 0x1C6) = yaw;
            s32 side = (s32)GCALL1(FN_L_PLAYER_SIDE, e);
            dir = licker_AvoidDirection(e, ahead, left, right, side);
        }
        u32 w = LW;
        GV(s32, w + 0x10) = dir;
        if (dir == 1 || dir == 2) {
            u32 yaw = (GV(u16, e + 0x1C6) & 0xFFF) + 0x1000;
            GV(u16, e + 0x1C6) = (u16)yaw;
            GV(u32, w + 0x14) = dir == 1 ? yaw + 0x400 : yaw - 0x400;
        }
    }
    switch (GV(s32, LW + 0x10)) {
    case 0:
        GCALL3(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (s32)EN_SPEED(e));
        return;
    case 1:
    case 2:
        GV(u16, e + 0x1C6) += GV(s32, LW + 0x10) == 1 ? 0x58 : (u16)-0x58;
        if ((s32)licker_call2(n_licker_WallAhead, e, 100) == -1) {
            GV(u16, e + 0x1C6) &= 0xFFF;
            GV(u32, LW + 0x10) = 0;
        }
        return;
    }
}

/* 80102D04 (e): action 3 every frame: drop from the ceiling, settle
 * down, or spot the player. */
void n_licker_HuntPre(void) {
    FRAME(0x18);
    u32 e = A0;
    A0 = e;
    n_licker_TryDrop();
    if (V0 != 0) return;
    A0 = e;
    n_licker_CalmDown();
    if (V0 != 0) return;
    A0 = e;
    n_licker_Spot();
}

/* 80102D48 (e): action 3, hunting: once the start animation is over,
 * crawl after the player round the walls; on the ceiling (+0x320 &
 * 0x200) aim at a point ahead of the player (LW +0x34 lead, shortened
 * while a wall is between) and lunge at it (0x80101680) when far, or
 * drop (action 0xE) or rear up when the player passes beneath; on the
 * floor lunge when it sees the player, else go for it (action 4) within
 * 3000 and stop within 6000. */
void n_licker_HuntStep(void) {
    FRAME(0x40);
    u32 e = A0;
    LW_FLAGS |= 0x400000;
    switch (EN_STEP(e)) {
    case 0: {
        u32 an = EN_ANIMSTATE(e);
        if (!(GV(u16, an + 0x14) < GV(u16, an + 0x16))) return;
        s16 speed = (s16)GCALL2(FN_EN_BASE_SPEED, e, 1);
        u32 w = LW;
        GV(s32, w + 0) = speed;
        GV(u32, w + 8) = 0;
        GV(u32, w + 0x34) = 600;
        EN_STEP(e) += 1;
        GV(u32, w + 0xC) &= ~0x1000000u;
        return;
    }
    case 1:
        break;
    default:
        return;
    }
    A0 = e;
    n_licker_TryLeap();
    if (V0 != 0) return;
    u32 ppos = PLAYER_POS_ADDR;
    if ((s32)licker_call2(n_licker_CrossedWall, EN_POS(e), ppos) >= 0) {
        A0 = e;
        n_licker_AvoidWalls();
        return;
    }
    GCALL3(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (s32)EN_SPEED(e));
    u32 fl = EN_FLAGS(e);
    if (fl & 0x200) {
        u32 v = sp + 0x10, a = sp + 0x20;
        GCALL3(FN_ROT_VELOCITY, PLAYER_ROT_ADDR, v, (u32)-10);
        u32 lead = GV(u32, LW + 0x34);
        MEM_W32(a + 0, MEM_R32(v + 0) * lead + GV(u32, g_player + 0x18));
        MEM_W32(a + 4, MEM_R32(v + 4) * lead + GV(u32, g_player + 0x1C));
        MEM_W32(a + 8, MEM_R32(v + 8) * lead + GV(u32, g_player + 0x20));
        if ((s32)licker_call2(n_licker_CrossedWall, ppos, a) >= 0) {
            u32 w = LW;
            if (GV(s32, w + 0x34) != 0) GV(s32, w + 0x34) -= 100;
        }
        lead = GV(u32, LW + 0x34);
        MEM_W32(v + 0, MEM_R32(v + 0) * lead + GV(u32, g_player + 0x18));
        MEM_W32(v + 4, MEM_R32(v + 4) * lead + GV(u32, g_player + 0x1C));
        MEM_W32(v + 8, MEM_R32(v + 8) * lead + GV(u32, g_player + 0x20));
        if ((s32)GCALL2(FN_L_DIST2D, EN_POS(e), v) > 1000) {
            s32 d = (s32)GCALL1(FN_EN_DIST, e);
            if ((s32)(GV(u32, LW + 0x34) * 10) < d) {
                GCALL2(FN_L_LUNGE, e, v);
                return;
            }
        }
        u32 zone = GCALL1(FN_EN_HIT_ZONE, e) & 0xFF;
        if (zone >= 8) return;
        if ((zone == 3 || zone == 4) && (en_rand() & 0xFF) >= 0x7F) {
            LW_FLAGS |= 0x24;
            GCALL4(FN_EN_SET_ACTION_B, e, 1, 0x16, 4);
            return;
        }
        GCALL4(FN_EN_SET_ACTION_B, e, 0xE, 0xB, 4);
        GV(s16, e + 0x862) = 5;
        EN_STATE(e) = 2;
        return;
    }
    if (fl & EF_SEES) {
        GCALL2(FN_L_LUNGE, e, ppos);
        return;
    }
    if ((s32)GCALL1(FN_EN_DIST, e) < 0xBB8) {
        GCALL3(FN_EN_SET_ACTION, e, 4, (s32)EN_ANIM(e));
        LW_FLAGS &= ~0x10000u & ~0x40000u;
        return;
    }
    if ((s32)GCALL1(FN_EN_DIST, e) < 0x1770) {
        EN_VX(e) = 0;
        EN_VY(e) = 0;
        EN_VZ(e) = 0;
    }
}

/* 80104138: action 11 (the leap up), nothing every frame. */
void n_licker_LeapPre(void) {
}

/* 80104140 (e): action 11: leap up to the ceiling: crouch (anim 0xA),
 * jump at the edge ahead (LW +0x50 the step, from its distance over the
 * jump's frames, 0x80107688..0x8010768C), turning over frame by frame
 * (0x80107690) and stopping at the ceiling (LW +0x28); then crawl on
 * upside down (+0x320 | 0x200). */
void n_licker_LeapStep(void) {
    FRAME(0x28);
    u32 e = A0;
    switch ((u32)(s32)EN_STEP(e)) {
    case 0:
        EN_STEP(e) += 1;
        /* fall through */
    case 1: {
        GCALL3(FN_EN_SET_ANIM_B, e, 0xA, 4);
        s32 f0 = GV(s32, 0x80107688), f1 = GV(s32, 0x8010768C);
        u32 w = LW;
        s32 step = mips_div(GV(s32, w + 0x2C), (s32)((u32)f1 - ((u32)f0 - 0x10)));
        GCALL3(FN_ROT_VELOCITY, EN_ROT(e), w + 0x50, (u32)-step);
        w = LW;
        GV(u32, w + 0x50) <<= 1;
        GV(u32, w + 0x58) <<= 1;
        GV(u32, w + 0x54) <<= 1;
        u32 rot = sp + 0x10;
        MEM_W16(rot + 4, 0);
        MEM_W16(rot + 0, 0);
        MEM_W16(rot + 2, GV(u16, e + 0x1C6));
        GCALL3(FN_ROT_VELOCITY, rot, w + 0x60, 0x1C);
        EN_STEP(e) += 1;
        return;
    }
    case 2: {
        u32 an = EN_ANIMSTATE(e);
        if (!(GV(u16, an + 0x14) < GV(u16, an + 0x16))) return;
        EN_FLAGS(e) |= 0x200;
        EN_STEP(e) += 1;
        GV(u16, LW + 0x30) = GV(u16, e + 0x1C4) - 0x400;
        return;
    }
    case 3: {
        if (licker_AnimEventCall(e, 1, 1) != 0) {
            GCALL3(FN_EN_SET_ANIM_B, e, 0x11, 2);
            u16 rx = GV(u16, LW + 0x30);
            EN_STEP(e) += 1;
            GV(u16, e + 0x1C4) = rx;
            return;
        }
        u32 f0 = GV(u32, 0x80107688);
        u32 frame = GV(u16, EN_ANIMSTATE(e) + 0x14);
        GV(u16, e + 0x1C4) -= GV(u8, 0x80107690 + frame - f0);
        u32 w = LW;
        EN_X(e) += GV(s32, w + 0x50);
        EN_Y(e) += GV(s32, w + 0x54);
        EN_Z(e) += GV(s32, w + 0x58);
        if (!((s32)GV(u16, EN_ANIMSTATE(e) + 0x14) < (s32)(f0 + 0x10))) {
            EN_X(e) += GV(s32, w + 0x60);
            EN_Z(e) += GV(s32, w + 0x68);
        }
        s32 top = GV(s32, LW + 0x28);
        if (EN_Y(e) < top) EN_Y(e) = top;
        return;
    }
    case 4: {
        u32 an = EN_ANIMSTATE(e);
        if (GV(u16, an + 0x14) < GV(u16, an + 0x16)) EN_STEP(e) += 1;
        return;
    }
    case 5: {
        u32 w = LW;
        u32 f = GV(u32, w + 0xC) & ~2u & ~1u;
        GV(u32, w + 0xC) = f;
        if (f & 0x400000) {
            A0 = e;
            n_licker_WakeHit();
            LW_FLAGS &= ~0x400000u;
        } else {
            GCALL4(FN_EN_SET_ACTION_B, e, 2, 1, 2);
            LW_BASE_SPEED = (s16)GCALL2(FN_EN_BASE_SPEED, e, 0);
        }
        return;
    }
    case 6: {
        u32 w = LW;
        EN_STEP(e) = 1;
        GV(u16, e + 0x1C4) = 0;
        EN_X(e) = GV(s32, w + 0x60);
        EN_Y(e) = GV(s32, w + 0x64);
        EN_Z(e) = GV(s32, w + 0x68);
        return;
    }
    }
}
