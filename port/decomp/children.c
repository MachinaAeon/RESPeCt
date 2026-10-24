/* The children, Lott and Lily (enemy program ENEMPROG 61, loaded in enemy
 * slot B at 0x80100000): the non-player characters run as enemy records.
 * Identified by how they look in the area scenario that loads it
 * (area21: stage 17 room 0, the two children on the catwalk) and by what
 * the code does: they follow scripted moves by stage and room, keep off
 * the walls, can be hurt (+0x31E) and, in stage 5, follow the player.
 * +0x350 is which one (1, 2; 0 for others).
 *
 * children_Update (0x80105CB8) runs every frame for each one: it picks
 * the work block, runs the state's handler (0x80106528; state 1,
 * children_StateActive, runs both of the action's handlers, 0x80106458
 * and 0x801064C0), takes hits, and in stage 5 follows the player.
 *
 * Each one has a 0x28-byte work block (0x80106560 + slot * 0x28; the
 * current one's address is kept at 0x80106678).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"
#include "poll.h"

void n_children_VoiceTimer(void);
void n_children_CrossedWall(void);
void n_children_Offset(void);
void n_children_KeepOffWalls(void);
void n_children_IdlePre(void);
void n_children_IdleStep(void);
void n_children_StateActive(void);
void n_children_Update(void);

#define CH               GV(u32, 0x80106678)
#define CHILDREN_WORK_BASE 0x80106560u
#define CHILDREN_WORK_SIZE 0x28
#define CHILDREN_ACT_PRE  0x80106458u
#define CHILDREN_ACT_STEP 0x801064C0u
#define CHILDREN_STATES   0x80106528u
#define g_childrenLost    0x8010667Cu   /* u16 per child: times left behind */
#define CHILD_ID(e)       GV(s32, (e) + 0x350)   /* 1 Lott, 2 Lily, 0 other */
#define CHILD_FLAGS(e)    GV(u32, (e) + 0x358)
#define FN_SOUND_STOP     0x8001F6A4u   /* (voice) */
#define F_ROT_MATRIX2     0x8002AA68u   /* (rotation, matrix) */
#define F_SET_ROT_MATRIX  0x8002A778u
#define F_SET_TRANS_MATRIX 0x8002A7A8u
#define F_ROT_TRANS       0x8002A958u   /* (short vector, long vector out, flag out) */
#define FN_C_SCRIPT_LIFT  0x80102484u   /* ovl61, not decompiled */
#define FN_C_SCRIPT_ROOM  0x8010238Cu
#define FN_C_SCRIPT_MOVE  0x801006D0u   /* (e, a, b) */
#define FN_C_SCRIPT_END   0x8010219Cu
#define FN_C_WANTS_GO     0x80102B78u
#define FN_C_GO           0x80101B0Cu   /* (e, speed) */
#define FN_C_WANTS_STOP   0x80102C1Cu
#define FN_C_STOP         0x80101AE0u
#define FN_C_HIT          0x80102D90u
#define FN_C_LEFT_BEHIND  0x80102060u
#define FN_C_FOLLOW_A     0x8010093Cu
#define FN_C_FOLLOW_B     0x8010096Cu
#define FN_C_FOLLOW_ME    0x801008B8u
#define FN_PART_UPDATE    0x800339E8u   /* (part, scratch) */
#define FN_EN_DIST_UPD    0x8007F4F4u

static u32 children_call(void (*f)(void), u32 a0, u32 a1, u32 a2) {
    A0 = a0; A1 = a1; A2 = a2;
    f();
    return V0;
}

/* Show (1) or hide (0) the model's parts from `from` on (+0x28 of them). */
static void children_ShowParts(u32 e, u32 from, u8 show) {
    for (u32 i = from; (s32)i < (s32)GV(u8, e + 0x28); i++) GV(u8, e + i) = show;
}

/* 8010062C (e): the voice playing (+0x36C, -1 none) stops when its timer
 * (+0x370) runs out. */
void n_children_VoiceTimer(void) {
    FRAME(0x20);
    u32 e = A0;
    if (GV(s32, e + 0x36C) == -1) return;
    s32 t = GV(s32, e + 0x370) - 1;
    GV(s32, e + 0x370) = t;
    if (t > 0) return;
    GCALL1(FN_SOUND_STOP, (s32)GV(s16, e + 0x36C));
    GV(s32, e + 0x36C) = -1;
}

static s32 children_Sign(s32 v) {
    if (v == 0) return -1;
    return mips_div(v, v >= 0 ? v : (s32)(0u - (u32)v));
}

/* 80100A84 (from, to): the first wall the path crosses coming from behind
 * it, or -1 (each end's side taken as v / |v|, 0 counting as -1). */
void n_children_CrossedWall(void) {
    FRAME(0x28);
    u32 from = A0, to = A1;
    u32 n = GV(u8, EP_WALLS_N);
    u32 w = GV(u32, EP_WALLS_N + 4);
    for (u32 i = 0; i < n; i++, w += 16) {
        s32 ax = GV(s32, w), az = GV(s32, w + 4), bx = GV(s32, w + 8), bz = GV(s32, w + 12);
        u32 ez = (u32)bz - (u32)az, ex = (u32)bx - (u32)ax;
        s32 sp_ = (s32)(ez * (MEM_R32(from) - (u32)ax) - ex * (MEM_R32(from + 8) - (u32)az));
        if (sp_ > 0) continue;
        s32 sq = (s32)(ez * (MEM_R32(to) - (u32)ax) - ex * (MEM_R32(to + 8) - (u32)az));
        if ((s32)((u32)children_Sign(sp_) * (u32)children_Sign(sq)) >= 0) continue;
        u32 px = MEM_R32(from), pz = MEM_R32(from + 8), qx = MEM_R32(to), qz = MEM_R32(to + 8);
        u32 mz = qz - pz, mx = qx - px;
        s32 sa = (s32)(mz * ((u32)ax - px) - mx * ((u32)az - pz));
        s32 sb = (s32)(mz * ((u32)bx - px) - mx * ((u32)bz - pz));
        if ((sa > 0 ? 1 : -1) * (sb > 0 ? 1 : -1) < 0) { V0 = i; return; }
    }
    V0 = (u32)-1;
}

/* 80100D30 (rot, out, dist): the point dist ahead along rot (the GTE's
 * rotation and translation from template 0x80065B90), rounded to 1/32
 * (out = round(v / 32)). */
void n_children_Offset(void) {
    FRAME(0x50);
    u32 rot = A0, out = A1;
    u32 m = sp + 0x10, v = sp + 0x30;
    for (u32 k = 0; k < 0x20; k += 4) GS(u32, m + k) = GV(u32, 0x80065B90 + k);
    GS(u16, v + 0) = 0;
    GS(u16, v + 2) = 0;
    GS(u16, v + 4) = (u16)(A2 << 5);
    gcall(F_ROT_MATRIX2, rot, m, 0, 0);
    gcall(F_SET_ROT_MATRIX, m, 0, 0, 0);
    gcall(F_SET_TRANS_MATRIX, m, 0, 0, 0);
    gcall(F_ROT_TRANS, v, out, sp + 0x38, 0);
    for (u32 k = 0; k < 3; k++) GS(u16, v + k * 2) = (u16)(((s32)MEM_R32(out + k * 4) >> 4) & 1);
    MEM_W32(out + 0, (u32)((s32)MEM_R32(out + 0) >> 5));
    MEM_W32(out + 8, (u32)((s32)MEM_R32(out + 8) >> 5));
    MEM_W32(out + 4, (u32)((s32)MEM_R32(out + 4) >> 5));
    for (u32 k = 0; k < 3; k++) MEM_W32(out + k * 4, MEM_R32(out + k * 4) + (u32)(s32)GS(s16, v + k * 2));
}

/* 80100E78 (e): keep off the walls: look 600 out diagonally all four
 * ways and step away (100 back along its facing) from each wall there. */
void n_children_KeepOffWalls(void) {
    FRAME(0x38);
    u32 e = A0;
    s16 yaw = EN_YAW(e);
    GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) + 0x200) & 0xFFF;
    u32 v = sp + 0x10;
    for (int i = 0; i < 4; i++) {
        children_call(n_children_Offset, EN_ROT(e), v, (u32)-0x258);
        MEM_W32(v + 0, MEM_R32(v + 0) + GV(u32, e + 0x1B8));
        MEM_W32(v + 8, MEM_R32(v + 8) + GV(u32, e + 0x1C0));
        if ((s32)children_call(n_children_CrossedWall, EN_POS(e), v, 0) != -1) {
            gcall(F_ROT_STEP, EN_ROT(e), v, 100, 0);
            EN_VX(e) += (s32)MEM_R32(v + 0);
            EN_VZ(e) += (s32)MEM_R32(v + 8);
        }
        GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) + 0x400) & 0xFFF;
    }
    GV(s16, e + 0x1C6) = yaw;
}

/* 80103268 (e): action 0 every frame: the scripted moves by stage and
 * room (hidden in stage 17 room 0 until event 0x74; the lift in stage 2
 * room 16; stage 10 room 3; stage 17 below -3000 until event 0xAC; the
 * end of stage 5), else go or stop when asked; in stage 5 shown and
 * following. Counting it as killed (to update the area's tally) is undone
 * for 0x80063450. */
void n_children_IdlePre(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 keep = GV(u32, 0x80063450);
    GCALL2(FN_EN_KILLED, e, (s32)EN_ROOMID(e));
    s32 stage = GV(s32, 0x800A866C);
    GV(u32, 0x80063450) = keep;
    if (stage == 0x11 && GV(s32, 0x800B3F08) == 0) {
        if (GCALL2(FN_FLAG_TEST, ep_eventFlags, 0x74) == 0) {
            if (CHILD_FLAGS(e) & 0x1000) return;
            EN_FLAGS(e) |= 0x8002;
            children_ShowParts(e, 0, 0);
            GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 1;
            GV(u16, CH + 0x10) = GV(u16, e + 0x31E);
            EN_HP(e) = -1;
            CHILD_FLAGS(e) |= 0x1000;
            return;
        }
        if (CHILD_FLAGS(e) & 0x1000) {
            EN_FLAGS(e) = EN_FLAGS(e) & 0xFFFF7FFFu & ~2u;
            children_ShowParts(e, 0, 1);
            GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 0;
            GV(u16, e + 0x31E) = GV(u16, CH + 0x10);
            CHILD_FLAGS(e) &= ~0x1000u;
        }
    }
    stage = GV(s32, 0x800A866C);
    s32 room = GV(s32, 0x800B3F08);
    if (stage == 2 && room == 0x10) {
        if (GCALL2(FN_FLAG_TEST, ep_eventFlags, 0x72) == 0) return;
        if (ep_grabLock != 0) return;
        GCALL1(FN_C_SCRIPT_LIFT, e);
        return;
    }
    if (stage == 0xA && room == 3 && EN_X(e) >= -0x289 && EN_X(e) < 0x1964 &&
        EN_Z(e) >= 0x547 && EN_Z(e) < 0x11F8) {
        GCALL1(FN_C_SCRIPT_ROOM, e);
        return;
    }
    if (stage == 0x11 && EN_Y(e) < -0xBB8) {
        if (GCALL2(FN_FLAG_TEST, ep_eventFlags, 0xAC) != 0) return;
        u32 w = CH;
        CHILD_ID(e) = 2;
        GV(s32, w + 0x18) = EN_X(e);
        GV(s32, w + 0x1C) = EN_Z(e);
        EN_ACTION(e) = 0x15;
        GCALL3(FN_C_SCRIPT_MOVE, e, 2, 0x10);
        GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 1;
        return;
    }
    if (GV(s32, 0x800A866C) == 5 && 0x14C07 < EN_Z(e)) {
        GCALL1(FN_C_SCRIPT_END, e);
        return;
    }
    if (GCALL1(FN_C_WANTS_GO, e) != 0) GCALL2(FN_C_GO, e, 0x10);
    else if (GCALL1(FN_C_WANTS_STOP, e) != 0) GCALL1(FN_C_STOP, e);
    if (GV(s32, 0x800A866C) != 5) return;
    GV(u32, e + 0x8B0) = 0;
    CHILD_FLAGS(e) &= ~0x200u;
    children_ShowParts(e, 1, 1);
    GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 0;
    GCALL1(FN_EN_DIST_UPD, e);
    GV(u32, e + 0x8A8) = 0;
    GV(u32, e + 0x8A4) = 0;
    GV(u32, e + 0x8A0) = 0;
}

/* 80103620: action 0's second handler, nothing. */
void n_children_IdleStep(void) {
}

/* 80105920 (e): state 1: both of the action's handlers, unless event
 * 0xAC is set. */
void n_children_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    if (GCALL2(FN_FLAG_TEST, ep_eventFlags, 0xAC) != 0) return;
    GCALL1(GV(u32, CHILDREN_ACT_PRE + (u32)((s32)EN_ACTION(e) << 2)), e);
    GCALL1(GV(u32, CHILDREN_ACT_STEP + (u32)((s32)EN_ACTION(e) << 2)), e);
}

/* 80105CB8 (e): a child's frame (see the top of the file): the hurt
 * timer (work +4, up to 60), the state's handler, hits (+0x358 | 4 when
 * hit again within 60 frames), in stage 5 left behind (9000 away: counted
 * per child and handled) or following, the turn easing (+0x368), the
 * voice, keeping off the walls (other NPCs, every other frame) and
 * Lott's parts. */
void n_children_Update(void) {
    FRAME(0x40);
    u32 e = A0;
    u32 w = CHILDREN_WORK_BASE + (u32)EN_SLOT(e) * CHILDREN_WORK_SIZE;
    s16 t = GV(s16, w + 4);
    CH = w;
    if (t < 0x3C) GV(u16, w + 4) = (u16)(t + 1);
    GCALL1(GV(u32, CHILDREN_STATES + (u32)((s32)EN_STATE(e) << 2)), e);
    u32 fl = EN_FLAGS(e);
    if (fl & EF_SHOT) {
        EN_FLAGS(e) = fl & ~0x10u;
        GCALL1(FN_C_HIT, e);
        GCALL1(FN_EN_HIT_POST, e);
        if (GV(s16, CH + 4) < 0x3C) CHILD_FLAGS(e) |= 4;
        GV(u16, CH + 4) = 0;
    }
    if (GV(s32, 0x800A866C) == 5 && (u32)EN_DIST(e) >= 0x2135 && EN_ACTION(e) != 0x11 && EN_HP(e) > 0) {
        EN_HP(e) = -1;
        if (CHILD_ID(e) == 1) GV(u16, g_childrenLost + 2) += 1;
        else GV(u16, g_childrenLost) += 1;
        GCALL1(FN_C_LEFT_BEHIND, e);
    }
    if (CHILD_ID(e) != 1 && EN_ACTION(e) != 0x11) {
        if (GV(s32, 0x800A866C) == 5) {
            GCALL1(FN_C_FOLLOW_A, e);
            GCALL1(FN_C_FOLLOW_B, e);
            CHILD_FLAGS(e) |= 0x200;
        }
    } else if (GV(s32, 0x800A866C) == 5 && EN_HP(e) > 0) {
        GCALL1(FN_C_FOLLOW_ME, e);
    }
    s32 turn = GV(s32, e + 0x368);
    if (turn != 0) GV(s32, e + 0x368) = turn > 0 ? turn - 0x20 : turn + 0x20;
    A0 = e;
    n_children_VoiceTimer();
    if (CHILD_ID(e) == 0 && EN_ACTION(e) != 0x18 && ((GV(u32, 0x800C3A90) ^ EN_SLOT(e)) & 1)) {
        A0 = e;
        n_children_KeepOffWalls();
    }
    if (CHILD_ID(e) == 1 && EN_HP(e) > 0) {
        u32 p = e + 0x2C;
        for (u32 i = 0; (s32)i < (s32)GV(u8, e + 0x28); i++, p += 8) {
            if (GV(u32, p + 4) == 0) continue;
            if (GV(u32, p) == 0) continue;
            gcall(FN_PART_UPDATE, GV(u32, p), sp + 0x10, 0, 0);
        }
    }
}

/* ---- the rest of ENEMPROG 61 the later-area scenarios reach (the
 * freezer, restaurant, library, prison ...: Lott and Lily as they follow,
 * hide, call out and get hurt) ---- */

void n_children_SpawnMark(void);
void n_children_PuffAnim(void);
void n_children_SpawnPuff(void);
void n_children_PlayVoice(void);
void n_children_VoiceStop(void);
void n_children_StartAnim(void);
void n_children_AnimEvent(void);
void n_children_WallAhead(void);
void n_children_PlayerInCone(void);
void n_children_NotFacing(void);
void n_children_TurnTo(void);
void n_children_WallDistance(void);
void n_children_SideOfPlayer(void);
void n_children_PlayerSide(void);
void n_children_SteerAroundWall(void);
void n_children_WallToPlayer(void);
void n_children_SetAnimDir(void);
void n_children_ZoneCheck(void);
void n_children_JointHook(void);
void n_children_HoldPlayer(void);
void n_children_StartWalk(void);
void n_children_StartSidestep(void);
void n_children_StartCall(void);
void n_children_StartVanish(void);
void n_children_LeftBehind(void);
void n_children_PickMove(void);
void n_children_OnHit(void);
void n_children_WalkPre(void);
void n_children_WalkStep(void);
void n_children_Act3Pre(void);
void n_children_Act3Step(void);
void n_children_SidestepPre(void);
void n_children_SidestepStep(void);
void n_children_Act5Step(void);
void n_children_Act5Pre(void);
void n_children_CallPre(void);
void n_children_CallStep(void);
void n_children_Act8Pre(void);
void n_children_Act8Step(void);
void n_children_Act12Pre(void);
void n_children_Act12Step(void);
void n_children_VanishPre(void);
void n_children_VanishStep(void);
void n_children_ReturnPre(void);
void n_children_ReturnStep(void);
void n_children_StateHit(void);
void n_children_StateDying(void);
void n_children_WantsGo(void);
void n_children_Act11Pre(void);
void n_children_Act11Step(void);
void n_children_State4(void);

#define g_effectCur       GV(u32, 0x800C39A8)   /* the effect record being set up / updated */
#define EFFECTS           0x800C09D8u           /* 0x44-byte effect records */
#define EFFECT_SIZE       0x44
#define F_EFFECT_ALLOC    0x80092E3Cu   /* (): a free record's index, or -1 */
#define F_EFFECT_INIT     0x800945B4u   /* (record, template) */
#define F_START_ANIM_A    0x800947D4u   /* (animation) */
#define F_ANIM_ONCE_A     0x8009414Cu   /* the effect's per-frame step */
#define F_GS_GET_LS       0x800339E8u   /* (coord, ls) */
#define F_SOUND_MOVE      0x8001F200u   /* (voice, sound, pan l, pan r, [frames]) */
#define F_PAD_HELD        0x80019268u   /* (mask): zero when that channel is free */
#define FN_EN_DIST        0x8007F4E8u   /* (e): distance to the player */
#define F_PAN_FOR         0x800122C4u   /* (0, volume, &pan[2]) */
#define F_SOUND_NOTE       0x80080670u   /* (bank, sound, l, r, [voice]) */
#define F_ISQRT           0x8002A3D8u   /* (n): integer square root */
#define FN_EN_CUE         0x80080718u   /* (e, k): the type's sound k */
#define FN_EN_ANIM_SOUND  0x8008127Cu   /* (e, bank, sound) */
#define FN_C_IN_BOX       0x80101850u   /* (e, x0, z0, x1, [z1, w]): ovl61, not decompiled */
#define FN_C_HUG          0x80101EBCu
#define FN_C_EVENT_FRAME  0x801020F4u
#define FN_C_LIE_DOWN     0x80102300u
#define FN_C_GETUP        0x80102648u
#define FN_C_DUCK_A       0x80101F38u
#define FN_C_DUCK_B       0x80101EFCu
#define FN_C_CALL_LOTT    0x801022A4u
#define FN_C_CALL_OTHER   0x801022C0u
#define FN_C_REAPPEAR     0x801023A0u
#define FN_C_CATCH_UP     0x80102904u
#define FN_C_CATCH_UP2    0x8010267Cu
#define FN_C_CAN_RETURN   0x80101168u
#define HIT_REACTIONS     0x80106440u   /* 3 x s16 per reaction: action, anim facing, anim not */
#define REACT_ODDS_LOW    0x80106428u   /* s16 odds per reaction (light, medium, heavy damage) */
#define REACT_ODDS_MID    0x80106430u
#define REACT_ODDS_HIGH   0x80106438u
#define g_childrenBack    0x8010667Cu   /* s16 x 2: children still to come back */
#define PLAYER_PREV_X     GV(s32, 0x800A8650)
#define PLAYER_PREV_Z     GV(s32, 0x800A8658)
#define CHILD_TURN(e)     GV(s32, (e) + 0x35C)
#define CHILD_COUNT(e)    GV(s32, (e) + 0x360)
#define CHILD_STEP(e)     GV(s32, (e) + 0x364)

static u32 children_frame_odd(u32 e) { return (EN_SLOT(e) ^ GV(u32, 0x800C3A90)) & 1; }
static s32 children_rand(void) { return (s32)en_rand(); }

/* 80100074 (e): a mark over the child's head (effect template 0x800A704C,
 * animation 0x800A70E4): at the point (-0x80, 0x4E0, -0x20) of its body
 * part's coordinates (+0x16C +0x2D0), or for Lott 1500 above its
 * position; 50 to the right in stage 5. */
void n_children_SpawnMark(void) {
    FRAME(0x90);
    u32 e = A0;
    u32 i = gcall(F_EFFECT_ALLOC, e, A1, A2, A3);
    if ((s16)i == -1) return;
    u32 r = EFFECTS + (u32)(s16)i * EFFECT_SIZE;
    g_effectCur = r;
    GV(u16, r + 0x1C) |= 0x41C;
    GCALL2(F_EFFECT_INIT, r, 0x800A704C);
    r = g_effectCur;
    GV(u16, r + 0xE) += 2;
    if (CHILD_ID(e) != 1) {
        GS(s32, sp + 0x28) = -0x80;
        GS(s32, sp + 0x2C) = 0x4E0;
        GS(s32, sp + 0x30) = -0x20;
        GS(u16, sp + 0x58) = 0;
        GS(u16, sp + 0x56) = 0;
        GS(u16, sp + 0x54) = 0;
        GS(u32, sp + 0x10) = 0;
        GS(u32, sp + 0x5C) = GV(u32, e + 0x16C) + 0x2D0;
        gcall(F_GS_GET_LS, sp + 0x10, sp + 0x60, 0, 0);
        s32 x = GS(s32, sp + 0x48) / 4;
        u32 m = g_effectCur;
        GV(u16, m + 0x10) = (u16)x;
        if (GV(s32, 0x800A866C) == 5) GV(u16, m + 0x10) = (u16)(x + 0x32);
        m = g_effectCur;
        GV(u16, m + 0x12) = GS(u16, sp + 0x4C);
        GV(u16, m + 0x18) = (u16)(GS(s32, sp + 0x50) / 16);
    } else {
        GV(u16, r + 0x10) = (u16)(EN_X(e) / 4);
        GV(u16, r + 0x12) = (u16)(GV(u16, e + 0x19C) - 0x5DC);
        GV(u16, r + 0x18) = (u16)(EN_Z(e) / 16);
    }
    GV(u16, g_effectCur + 0x34) = 0x80;
    GCALL1(F_START_ANIM_A, 0x800A70E4);
    GV(u32, g_effectCur + 0x3C) = F_ANIM_ONCE_A;
}

/* 80100224: a puff's frame: its animation, and unless held (+0x1C bit 0)
 * it moves by its speed (+0x28, gaining +0x30 a frame: y +0x12) and grows
 * (+0x34 by +0x2C). */
void n_children_PuffAnim(void) {
    FRAME(0x18);
    gcall(F_ANIM_ONCE_A, A0, A1, A2, A3);
    u32 r = g_effectCur;
    if (GV(u16, r + 0x1C) & 1) { V0 = 1; return; }
    u32 y = (u32)(s32)GV(s16, r + 0x12);
    u32 v = (u32)GV(u16, r + 0x28) + GV(u16, r + 0x30);
    u32 d = y - (u32)(s32)GV(s16, r + 0x28);
    u32 s = (u32)GV(u16, r + 0x34) + GV(u16, r + 0x2C);
    GV(u16, r + 0x28) = (u16)v;
    GV(u16, r + 0x12) = (u16)(v + d);
    GV(u16, r + 0x34) = (u16)s;
    V0 = v + d;
}

/* 80100298 (e): a puff (effect template 0x800A6FAC, animation 0x800A7BE0)
 * at one of the body parts 1-11 at random, with a random size (32-63),
 * rising speed (-50..-89) and growth (16-31). */
void n_children_SpawnPuff(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 i = gcall(F_EFFECT_ALLOC, e, A1, A2, A3);
    if ((s16)i == -1) return;
    u32 r = EFFECTS + (u32)(s16)i * EFFECT_SIZE;
    g_effectCur = r;
    GV(u16, r + 0x1C) |= 0x41C;
    GCALL2(F_EFFECT_INIT, r, 0x800A6FAC);
    s32 v = children_rand();
    u32 off = (u32)((v >> 4) % 11 + 1) * 0x50;
    u32 m = g_effectCur;
    GV(u16, m + 0x10) = (u16)(GV(s32, GV(u32, e + 0x16C) + off + 0x38) / 4);
    GV(u16, m + 0x12) = GV(u16, e + 0x19C);
    GV(u16, m + 0x18) = (u16)(GV(s32, GV(u32, e + 0x16C) + off + 0x40) / 16);
    v = children_rand();
    GV(u16, g_effectCur + 0x34) = (u16)(v % 32 + 0x20);
    v = children_rand();
    GV(u16, g_effectCur + 0x30) = (u16)(0 - (v % 40 + 0x32));
    v = children_rand();
    GV(u16, g_effectCur + 0x2C) = (u16)(v % 16 + 0x10);
    GCALL1(F_START_ANIM_A, 0x800A7BE0);
    GV(u32, g_effectCur + 0x3C) = 0x80100224u;
}

/* 80100460 (sound, frames, e): say sound (bank by the type, +0x84E): the
 * voice still playing (+0x36C) moves on to it; on voice channel 0x10 or
 * 0x11 (none free: no voice), panned and quieter with distance past 1000.
 * The voice stops after `frames` (+0x370). */
void n_children_PlayVoice(void) {
    FRAME(0x38);
    u32 snd = A0, frames = A1, e = A2;
    u32 bank = (u32)((s32)GV(s16, e + 0x84E) * 2 + 3);
    if (GV(s32, e + 0x36C) != -1) {
        GS(u32, sp + 0x10) = 0x3C;
        gcall(F_SOUND_MOVE, (u32)(s32)GV(s16, e + 0x36C), (u32)(s32)GV(s16, 0x800637C0 + bank * 12 + 8),
              GV(u8, e + 0x374) >> 4, GV(u16, e + 0x374) & 0xF);
    }
    u32 ch;
    if (GCALL1(F_PAD_HELD, 0x10000) == 0) ch = 0x10;
    else if (GCALL1(F_PAD_HELD, 0x20000) == 0) ch = 0x11;
    else { GV(s32, e + 0x36C) = -1; return; }
    if ((s32)GCALL1(FN_EN_DIST, e) >= 0x3E9) {
        s32 d = (s32)GCALL1(FN_EN_DIST, e) - 0x3E8;
        u32 q = (u32)mips_div(d, GV(s32, 0x80062E08));
        if ((s16)q >= 0xFF) q = 0xFF;
        gcall(F_PAN_FOR, 0, (u32)(s32)(s16)(0xFF - q), sp + 0x18, 0);
        GV(s32, e + 0x36C) = (s32)ch;
        s32 l = GS(s16, sp + 0x18) / 64, rr = GS(s16, sp + 0x1A) / 64;
        GS(u32, sp + 0x10) = ch;
        gcall(F_SOUND_NOTE, bank, snd, (u32)l, (u32)rr);
    } else {
        GV(s32, e + 0x36C) = (s32)ch;
        s32 vol = (s32)GV(s16, 0x80062DB6) * 127 / 100;
        GS(u32, sp + 0x10) = ch;
        gcall(F_SOUND_NOTE, bank, snd, (u32)vol, (u32)vol);
    }
    GV(u32, e + 0x370) = frames;
    GV(u32, e + 0x374) = snd;
}

/* 80100688 (e): stop the voice playing (+0x36C, -1 none). */
void n_children_VoiceStop(void) {
    FRAME(0x20);
    u32 e = A0;
    if (GV(s32, e + 0x36C) == -1) return;
    GCALL1(FN_SOUND_STOP, (s32)GV(s16, e + 0x36C));
    GV(s32, e + 0x36C) = -1;
}

/* 801006D0 (e, anim, blend): queue animation anim (+0x860, +0x85C = 1)
 * with blend frames (+0x8B0); clear +0x320 bits 2, 3 and 15. Returns 1. */
void n_children_StartAnim(void) {
    u32 e = A0;
    EN_ALIVE(e) = 1;
    GV(u16, e + 0x860) = (u16)A1;
    GV(u16, e + 0x85C) = 1;
    GV(u32, e + 0x8B0) = A2;
    EN_FLAGS(e) &= 0xFFFF7FF3u;
    V0 = 1;
}

/* 80100700 (e, part, k): the event bit of animated part `part`, k frames
 * ahead. */
void n_children_AnimEvent(void) {
    V0 = en_anim_event(A0, A1, A2);
}

/* 80100C54 (e, k): the wall (children_CrossedWall) within k x 1000 ahead,
 * or -1. */
void n_children_WallAhead(void) {
    FRAME(0x30);
    V0 = en_wall_ahead(sp + 0x10, A0, A1, 0x80100A84u);
}

/* 80100FF4 (e, mode): is the player inside the child's view cone? Mode 0
 * yaw -0x71..+0x71, 1 the wide one (-0x3F9..+0x3F9), 2 -0x22..+0x22,
 * 3 -0x153..+0x153 (the second end taken from the first). */
void n_children_PlayerInCone(void) {
    FRAME(0x48);
    u32 e = A0, mode = A1;
    u32 rot = sp + 0x30, l = sp + 0x20, r = sp + 0x10;
    GS(u16, rot + 4) = 0;
    GS(u16, rot + 0) = 0;
    u16 yaw = GV(u16, e + 0x1C6);
    u16 a;
    if (mode == 0) a = yaw + 0x71;
    else if (mode == 1) a = yaw + 0x3F9;
    else if (mode == 2) a = yaw + 0x22;
    else a = yaw + 0x153;
    GS(u16, rot + 2) = a;
    gcall(F_ROT_STEP, rot, l, 100, 0);
    MEM_W32(l, MEM_R32(l) + (u32)EN_X(e));
    MEM_W32(l + 8, MEM_R32(l + 8) + (u32)EN_Z(e));
    u16 b = GS(u16, rot + 2);
    if (mode == 0) b -= 0xE2;
    else if (mode == 1) b -= 0x7F2;
    else if (mode == 2) b -= 0x44;
    else b -= 0x2A6;
    GS(u16, rot + 2) = b;
    gcall(F_ROT_STEP, rot, r, 100, 0);
    MEM_W32(r, MEM_R32(r) + (u32)EN_X(e));
    MEM_W32(r + 8, MEM_R32(r + 8) + (u32)EN_Z(e));
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), l, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) <= 0) { V0 = 0; return; }
    V0 = (s32)gcall(FN_LINE_SIDE, EN_POS(e), r, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) >= 0 ? 0 : 1;
}

/* 80101294 (e): 1 unless the child and the player face each other. */
void n_children_NotFacing(void) {
    V0 = en_not_facing(A0, 0x47D, 0x707);
}

/* 801012C8 (e, dist, step, angle, [mode]): turn towards the player (dist
 * 0) or the point dist x 1000 from them at their yaw + angle: by step,
 * and on a frame of its own parity (the slot against the frame counter)
 * once more by step while the target stays on the same side, each turn
 * halving the result. Mode 0 doesn't turn when the player is already in
 * the narrow cone facing it. Returns the turn left (0: none). */
void n_children_TurnTo(void) {
    FRAME(0x58);
    u32 e = A0, dist = A1, step = A2, ang = A3;
    u32 mode = GS(u32, sp + 0x68);
    u32 t = sp + 0x18;
    if (dist == 0) {
        GS(u32, t + 0) = GV(u32, PLAYER_POS_ADDR);
        GS(u32, t + 4) = GV(u32, PLAYER_POS_ADDR + 4);
        GS(u32, t + 8) = GV(u32, PLAYER_POS_ADDR + 8);
    } else {
        u32 rot = sp + 0x10;
        GS(u16, rot + 0) = GV(u16, g_player + 0x44);
        GS(u16, rot + 2) = (u16)((GV(u16, g_player + 0x46) + ang) & 0xFFF);
        GS(u16, rot + 4) = GV(u16, g_player + 0x48);
        gcall(F_ROT_STEP, rot, t, (u32)-100, 0);
        u32 x = GS(u32, t + 0) * 10 * dist, z = GS(u32, t + 8) * 10 * dist;
        GS(u32, t + 0) = x + GV(u32, PLAYER_POS_ADDR);
        GS(u32, t + 8) = z + GV(u32, PLAYER_POS_ADDR + 8);
    }
    if (mode == 0 && GCALL2(0x80100FF4u, e, 2) != 0 && GCALL1(0x80101294u, e) == 0) { V0 = 0; return; }
    u32 v = sp + 0x28;
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    GS(u32, v + 0) += (u32)EN_X(e);
    GS(u32, v + 8) += (u32)EN_Z(e);
    u16 yaw0 = GV(u16, e + 0x1C6);
    u32 n = children_frame_odd(e);
    s32 side = (s32)gcall(FN_LINE_SIDE, EN_POS(e), v, GS(u32, t + 0), GS(u32, t + 8));
    int pos = side >= 0;
    u32 turn = pos ? step : 0u - step;
    for (;;) {
        if (pos) GV(u16, e + 0x1C6) = (u16)((GV(u16, e + 0x1C6) + step) & 0xFFF);
        else GV(u16, e + 0x1C6) = (u16)((GV(u16, e + 0x1C6) - step) & 0xFFF);
        gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
        GS(u32, v + 0) += (u32)EN_X(e);
        GS(u32, v + 8) += (u32)EN_Z(e);
        side = (s32)gcall(FN_LINE_SIDE, EN_POS(e), v, GS(u32, t + 0), GS(u32, t + 8));
        if (pos ? side >= 0 : side < 0) break;
        s32 h = (s16)turn;
        turn = (u32)((h + (h < 0)) >> 1);
        n -= 1;
        if ((s16)n < 0) break;
        LOOP_POLL(pos ? 0x801014C8u : 0x8010155Cu);
    }
    GV(u16, e + 0x1C6) = (u16)((yaw0 + turn) & 0xFFF);
    V0 = (u32)(s32)(s16)turn;
}

/* 801015A0 (e, wall): the distance from the child to wall segment `wall`. */
void n_children_WallDistance(void) {
    FRAME(0x38);
    u32 w = GV(u32, EP_WALLS_N + 4) + A1 * 16;
    GS(u32, sp + 0x10) = GV(u32, w + 0);
    GS(u32, sp + 0x18) = GV(u32, w + 4);
    GS(u32, sp + 0x20) = GV(u32, w + 8);
    GS(u32, sp + 0x28) = GV(u32, w + 12);
    V0 = gcall(FN_LINE_DIST, sp + 0x10, sp + 0x20, EN_POS(A0), 0);
}

/* 801015FC (e): which side of the player's facing the child is on (the
 * line's side test against the direction alone). */
void n_children_SideOfPlayer(void) {
    FRAME(0x30);
    u32 e = A0;
    gcall(F_ROT_STEP, PLAYER_ROT_ADDR, sp + 0x10, (u32)-100, 0);
    V0 = gcall(FN_LINE_SIDE, PLAYER_POS_ADDR, sp + 0x10, (u32)EN_X(e), (u32)EN_Z(e));
}

/* 80101650 (e): which side of the child's facing the player is on. */
void n_children_PlayerSide(void) {
    FRAME(0x28);
    u32 e = A0;
    gcall(F_ROT_STEP, EN_ROT(e), sp + 0x10, (u32)-100, 0);
    V0 = gcall(FN_LINE_SIDE, EN_POS(e), sp + 0x10, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z);
}

/* 8010169C (e, turn, k, wall): steer round the wall ahead: away from the
 * side with a wall k x 1000 out (left at yaw + 0x400, right at - 0x400);
 * with walls both sides or neither, and the wall ahead nearer than 1000,
 * towards the player when within 3000, else either way at random. */
void n_children_SteerAroundWall(void) {
    FRAME(0x28);
    u32 e = A0, turn = A1, k = A2, wall = A3;
    u16 yaw = GV(u16, e + 0x1C6);
    GV(u16, e + 0x1C6) = (yaw + 0x400) & 0xFFF;
    s32 l = (s32)GCALL2(0x80100C54u, e, k);
    GV(u16, e + 0x1C6) = (yaw - 0x400) & 0xFFF;
    s32 r = (s32)GCALL2(0x80100C54u, e, k);
    GV(u16, e + 0x1C6) = yaw;
    u32 to;
    if (l == -1 && r != -1) to = yaw + turn;
    else if (l != -1 && r == -1) to = yaw - turn;
    else {
        if ((s32)GCALL2(0x801015A0u, e, wall) >= 0x3E9) return;
        if ((u32)EN_DIST(e) < 0xBB8) to = (s32)GCALL1(0x80101650u, e) > 0 ? yaw - turn : yaw + turn;
        else to = children_rand() % 100 < 0x32 ? yaw - turn : yaw + turn;
    }
    GV(u16, e + 0x1C6) = (u16)(to & 0xFFF);
}

/* 801017E0 (e): is there a wall between the child and the player? */
void n_children_WallToPlayer(void) {
    FRAME(0x18);
    V0 = (s32)GCALL2(0x80100A84u, EN_POS(A0), PLAYER_POS_ADDR) != -1;
}

/* 8010180C (e, dir): every animated part plays in direction dir (+0xD)
 * from its start (+0x10 = 0). */
void n_children_SetAnimDir(void) {
    u32 e = A0;
    s32 n = GV(s32, e + 0x1D0);
    for (s32 i = 0; i < n; i++) {
        if (i) LOOP_POLL(0x80101840u);
        u32 p = GV(u32, e + 0x1D4 + (u32)i * 4);
        GV(u8, p + 0xD) = (u8)A1;
        GV(u16, GV(u32, e + 0x1D4 + (u32)i * 4) + 0x10) = 0;
    }
}

/* 80101980 (e): +0x320 bit 1 (out of play) for Lily while within 2000 in
 * x of the player's last position, and for child 3 while in its box. */
void n_children_ZoneCheck(void) {
    FRAME(0x20);
    u32 e = A0;
    s32 id = CHILD_ID(e);
    u32 in;
    if (id == 2) in = (u32)(EN_X(e) - PLAYER_PREV_X) + 0x7CF < 0xF9F;
    else if (id == 3) {
        GS(u32, sp + 0x10) = 0x1004;
        GS(u32, sp + 0x14) = 0x992;
        in = GCALL4(FN_C_IN_BOX, e, 0, 0x17D4, 0x8FC) != 0;
    } else return;
    if (in) EN_FLAGS(e) |= 2;
    else EN_FLAGS(e) &= ~2u;
}

/* 80101A2C (joint, n): the joint hook: joints 1-20 turn by the child's
 * turn easing (+0x368 of the enemy being updated). Returns 0. */
void n_children_JointHook(void) {
    if ((A1 - 1) / 20 == 0)
        GV(u16, A0 + 0x48) += GV(u16, ep_enemyCur + 0x368);
    V0 = 0;
}

/* 80101A6C (e, k): in event mode 1 with no grab running, take hold of
 * the player (k) and start the grab. */
void n_children_HoldPlayer(void) {
    FRAME(0x18);
    u32 e = A0;
    if (D_800BB19C != 1 || ep_grabLock2 != 0) return;
    GCALL3(FN_EN_GRAB_PLAYER, e, A1, 0);
    GCALL1(FN_EN_GRAB_START, e);
}

/* 80101B0C (e, blend): walk (action 2, animation 2) on a random bend
 * (+0x35C: -682..+681). */
void n_children_StartWalk(void) {
    FRAME(0x18);
    u32 e = A0;
    EN_ACTION(e) = 2;
    GCALL3(0x801006D0u, e, 2, A1);
    CHILD_FLAGS(e) &= ~1u;
    s32 r = children_rand() % 120;
    CHILD_TURN(e) = (s32)((u32)r << 12) / 360 - 0x2AA;
}

/* 80101BB0 (e): side step (action 4) away from the player's line of
 * sight (+0x35C = +-600). */
void n_children_StartSidestep(void) {
    FRAME(0x18);
    u32 e = A0;
    EN_ACTION(e) = 4;
    CHILD_TURN(e) = (s32)GCALL1(0x801015FCu, e) >= 0 ? 0x258 : -0x258;
    GCALL3(0x801006D0u, e, 2, 0x10);
    CHILD_COUNT(e) = 0;
    CHILD_FLAGS(e) &= ~4u;
}

/* 80101E58 (e): call out (action 7, animation 3, sound 4; the work
 * block's timer 6). */
void n_children_StartCall(void) {
    FRAME(0x18);
    u32 e = A0;
    EN_ACTION(e) = 7;
    GCALL3(0x801006D0u, e, 3, 0x10);
    CHILD_STEP(e) = 0;
    CHILD_FLAGS(e) &= ~6u;
    GCALL2(FN_EN_CUE, e, 4);
    GV(u16, CH + 0) = 6;
}

/* 80101FEC (e): vanish (action 15: out of play, blinking out; sound
 * 0xC); the other children's animation stops. */
void n_children_StartVanish(void) {
    FRAME(0x18);
    u32 e = A0;
    if (CHILD_ID(e) != 1) GCALL2(0x8010180Cu, e, 0);
    EN_ACTION(e) = 0xF;
    EN_FLAGS(e) |= 6;
    EN_FLAGS(e) |= 0x8000;
    GCALL2(FN_EN_CUE, e, 0xC);
    CHILD_STEP(e) = 0;
    EN_NHITS(e) = -1;
    GCALL1(0x80100688u, e);
}

/* 80102060 (e): left behind (action 0x11, state 2): hidden; Lott's mark
 * (work +0xE) goes. Returns 1. */
void n_children_LeftBehind(void) {
    u32 e = A0;
    for (u32 i = 0; (s32)i < (s32)GV(u8, e + 0x28); i++) {
        if (i) LOOP_POLL(0x80102084u);
        GV(u8, e + i) = 0;
    }
    EN_ACTION(e) = 0x11;
    EN_STATE(e) = 2;
    s32 id = CHILD_ID(e);
    if (id == 1) {
        u32 w = CH;
        s32 n = GV(s16, w + 0xE);
        if (n > 0) {
            GV(u16, EFFECTS + (u32)n * EFFECT_SIZE + 0x1C) = (u16)id;
            GV(u16, w + 0xE) = 0xFFFF;
        }
    }
    GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 1;
    V0 = 1;
}

/* 80102C40 (e): seeing the player (no wall between, inside the narrow
 * cone; past 0x898 only once, +0x358 bit 0), pick a move: 10% scared
 * (half of those: 0x80101DDC), 40% action 3, the rest half scared. Returns
 * 1 when one was picked. */
void n_children_PickMove(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!(EN_FLAGS(e) & EF_SEES)) { CHILD_FLAGS(e) &= ~1u; V0 = 0; return; }
    if (GCALL1(0x801017E0u, e) != 0) { V0 = 0; return; }
    if (GCALL2(0x80100FF4u, e, 0) == 0) { V0 = 0; return; }
    if ((u32)EN_DIST(e) >= 0x899 && (CHILD_FLAGS(e) & 1)) { V0 = 0; return; }
    CHILD_FLAGS(e) |= 1;
    s32 r = children_rand() % 100;
    if (r >= 10 && r < 0x32) { EN_ACTION(e) = 3; V0 = 1; return; }
    if (children_rand() % 100 < 0x32) GCALL1(0x80101DDCu, e);
    V0 = 1;
}

/* 80102D90 (e): hit by shots: (not while out of play, +0x358 bit 5, or
 * dying) sound 0xB; Lott and child 0x15 can't be hurt (health -1);
 * others lose the damage, noting hits on parts of kind 7-10. Dead: state
 * 3, lying down (+0x358 bit 10) or left behind, sound 0xC, counted
 * killed; Lily falls (action 0xC); others by damage (light hits on action
 * 0xB: 0xD). Alive: Lott flinches (work +2 = 151 frames); Lily and child 3
 * sway; others react by damage at random (HIT_REACTIONS, the animation by
 * whether they face the player, flipped for those part hits). */
void n_children_OnHit(void) {
    FRAME(0x28);
    u32 e = A0;
    if (CHILD_FLAGS(e) & 0x20) { EN_NHITS(e) = -1; return; }
    if (EN_STATE(e) == 3) return;
    GCALL2(FN_EN_CUE, e, 0xB);
    EN_FLAGS(e) &= 0xFFFF7FFFu;
    s32 dmg = 0;
    u32 parts = 0;
    if (CHILD_ID(e) == 1 || EN_ACTION(e) == 0x15) {
        EN_HP(e) = -1;
    } else {
        s32 n = EN_NHITS(e);
        for (s32 i = 0; i <= n; i++) {
            if (i) LOOP_POLL(0x80102EB8u);
            GV(u16, e + 0x31E) = (u16)(GV(u16, e + 0x31E) - GV(u16, e + 0x330 + (u32)i * 2));
            dmg += GV(s16, e + 0x330 + (u32)i * 2);
            s16 kind = EN_BODY_KIND(e, GV(u8, e + 0x326 + (u32)i));
            if (kind == 7 || kind == 9 || kind == 8 || kind == 10) parts = 1;
        }
    }
    EN_NHITS(e) = -1;
    GCALL1(FN_EN_HIT_PRE, e);
    if (EN_ACTION(e) == 0x10 || EN_ACTION(e) == 9) return;
    if (EN_HP(e) <= 0) {
        s32 id = CHILD_ID(e);
        EN_STATE(e) = 3;
        if (id == 1) {
            u32 w = CH;
            s32 m = GV(s16, w + 0xE);
            if (m > 0) {
                GV(u16, EFFECTS + (u32)m * EFFECT_SIZE + 0x1C) = (u16)id;
                GV(u16, w + 0xE) = 0xFFFF;
            }
            if (CHILD_FLAGS(e) & 0x400) GCALL1(FN_C_LIE_DOWN, e);
            else GCALL1(0x80102060u, e);
            s32 r = children_rand() % 30;
            GV(u16, CH + 6) = (u16)(r << 2);
            GCALL2(FN_EN_CUE, e, 0xC);
            EN_FLAGS(e) |= 2;
            GCALL2(FN_EN_KILLED, e, 0);
            GCALL1(0x80100688u, e);
            return;
        }
        u16 act = GV(u16, e + 0x318);
        if ((u32)(act - 0xC) >= 2) {
            if (id == 2) {
                EN_ACTION(e) = 0xC;
                GCALL2(FN_EN_KILLED, e, 0);
                u32 nf = GCALL1(0x80101294u, e);
                GCALL3(0x801006D0u, e, nf ? 9 : 10, 0x10);
                GCALL1(0x80100688u, e);
                return;
            }
            EN_ACTION(e) = ((s16)act == 0xB && dmg < 0x29) ? 0xD : 0xC;
            u32 nf = GCALL1(0x80101294u, e);
            GCALL3(0x801006D0u, e, nf ? 9 : 10, 0x10);
        }
        if (CHILD_FLAGS(e) & 0x800) GCALL2(FN_EN_KILLED, e, (s32)EN_ROOMID(e));
        else GCALL2(FN_EN_KILLED, e, 0);
        return;
    }
    s32 id = CHILD_ID(e);
    if (id == 1) { GV(u16, CH + 2) = 0x97; return; }
    u32 react;
    if ((u32)(id - 2) < 2) react = 3;
    else {
        u32 odds = dmg < 0x29 ? REACT_ODDS_LOW : dmg < 0x79 ? REACT_ODDS_MID : REACT_ODDS_HIGH;
        react = 0;
        s32 r = children_rand() % 100;
        for (;;) {
            s32 o = GV(s16, odds);
            if (r < o) break;
            r -= o;
            react++;
            odds += 2;
            LOOP_POLL(0x801031A0u);
        }
    }
    u32 nf = GCALL1(0x80101294u, e);
    if (parts && react != 0) nf ^= 1;
    if (react == 3) { GV(s32, e + 0x368) = nf ? 0x180 : -0x180; return; }
    u32 t = HIT_REACTIONS + react * 6;
    GV(u16, e + 0x318) = GV(u16, t);
    if (nf == 0) {
        GCALL3(0x801006D0u, e, (s32)GV(s16, t + 2), 0x10);
        CHILD_FLAGS(e) &= ~8u;
    } else {
        GCALL3(0x801006D0u, e, (s32)GV(s16, t + 4), 0x10);
        CHILD_FLAGS(e) |= 8;
    }
}

/* Action handlers' common end: the step sound on the animation's event. */
static void children_step_sound(u32 e) {
    if (GCALL3(0x80100700u, e, 1, 1) != 0) GCALL3(FN_EN_ANIM_SOUND, e, 1, 0);
}

/* 80103800 (e): action 2, walking: in event mode 3 the event's moves;
 * else a move when it sees the player, a hug when close and facing, the
 * walls ahead steered round; turning to the player (faster further off)
 * or wandering on its bend, walking on; kept near its last spot (work
 * +0x12, +0x14) by weaving every other frame. */
void n_children_WalkPre(void) {
    FRAME(0x28);
    u32 e = A0;
    u32 speed = R[17];          /* the caller's $s1 when no case sets it */
    if (D_800BB19C == 3) {
        gcall(FN_C_EVENT_FRAME, e, A1, A2, A3);
    } else if (GCALL1(0x80102C40u, e) == 0) {
        if ((EN_FLAGS(e) & EF_SEES) && (u32)EN_DIST(e) < 0x898 && GCALL1(0x80101294u, e) == 0 &&
            GCALL2(0x80100FF4u, e, 0) != 0) {
            GCALL1(FN_C_HUG, e);
            return;
        }
        if (!((u32)EN_DIST(e) < 0x9C5 && (GV(u8, e + 0x2B) & 0x40))) {
            speed = (u32)-0xA0;
            u32 k = GCALL1(0x801017E0u, e) != 0 ? 2 : 1;
            s32 w = (s32)GCALL2(0x80100C54u, e, k);
            if (w != -1) {
                GCALL4(0x8010169Cu, e, 0x71, 1, w);
                speed = (u32)-0x50;
            }
        }
        if (EN_FLAGS(e) & EF_SEES) {
            u32 d = (u32)EN_DIST(e);
            GS(u32, sp + 0x10) = 0;
            if (d < 0x579) {
                GCALL4(0x801012C8u, e, 0, 0x80, 0);
                speed = (u32)-0x14;
            } else if (d < 0xAF1) {
                GCALL4(0x801012C8u, e, 0, children_frame_odd(e) ? 0x100 : 0x80, 0);
            } else {
                GCALL4(0x801012C8u, e, 0, children_frame_odd(e) ? 0xC0 : 0x80, 0);
            }
        } else {
            if ((u32)EN_DIST(e) >= 0xBB8 && GCALL2(0x80100FF4u, e, 1) == 0) {
                GS(u32, sp + 0x10) = 0;
                GCALL4(0x801012C8u, e, 8, 0x20, GV(u32, e + 0x35C));
            } else {
                if ((s32)GCALL1(0x80101650u, e) > 0) GV(u16, e + 0x1C6) -= 0x60;
                else GV(u16, e + 0x1C6) += 0x60;
                GV(u16, e + 0x1C6) &= 0xFFF;
            }
            speed = (u32)-0xF0;
        }
        gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), speed, 0);
    }
    children_step_sound(e);
    if ((EN_FLAGS(e) & EF_SEES) && children_frame_odd(e)) {
        u32 w = CH;
        u32 dx = (u32)EN_X(e) - (u32)(s32)GV(s16, w + 0x12);
        u32 dz = (u32)EN_Z(e) - (u32)(s32)GV(s16, w + 0x14);
        u32 q = GCALL1(F_ISQRT, dx * dx + dz * dz);
        if ((s32)(0u - q) < -0x4F) {
            CHILD_FLAGS(e) &= ~0x40u;
        } else {
            if (!(CHILD_FLAGS(e) & 0x40)) {
                if ((s32)GCALL1(0x80101650u, e) < 0) CHILD_FLAGS(e) |= 0x80;
                else CHILD_FLAGS(e) &= ~0x80u;
                if ((u32)EN_DIST(e) < 0x898) CHILD_FLAGS(e) ^= 0x80;
                CHILD_FLAGS(e) |= 0x40;
            }
            if (CHILD_FLAGS(e) & 0x80) {
                GV(u16, e + 0x1C6) += 0x40;
                CHILD_FLAGS(e) &= ~0x80u;
            } else {
                GV(u16, e + 0x1C6) -= 0x40;
                CHILD_FLAGS(e) |= 0x80;
            }
            GV(u16, e + 0x1C6) &= 0xFFF;
        }
    }
    u32 w = CH;
    GV(u16, w + 0x12) = GV(u16, e + 0x198);
    GV(u16, w + 0x14) = GV(u16, e + 0x1A0);
}

/* 80103C0C: action 2's second handler, nothing. */
void n_children_WalkStep(void) {
}

/* 80103C14 (e): action 3, going to the player: in event mode 3 the
 * event's moves; walk on when it can't see them; a hug when close and
 * facing; else turn to them (facing away) and step on (but walk off when
 * past 6000 and in the narrow cone: call out). */
void n_children_Act3Pre(void) {
    FRAME(0x20);
    u32 e = A0;
    if (D_800BB19C == 3) {
        gcall(FN_C_EVENT_FRAME, e, A1, A2, A3);
    } else if (GCALL1(0x801017E0u, e) != 0 || !(EN_FLAGS(e) & EF_SEES)) {
        GCALL2(0x80101B0Cu, e, 0x10);
    } else {
        if (GCALL1(0x80101294u, e) == 0 && (u32)EN_DIST(e) < 0x898 && GCALL2(0x80100FF4u, e, 0) != 0) {
            GCALL1(FN_C_HUG, e);
            return;
        }
        int step = 1;
        if (GCALL1(0x80101294u, e) != 0) {
            GS(u32, sp + 0x10) = 0;
            GCALL4(0x801012C8u, e, 0, 0x20, 0);
        } else if ((u32)EN_DIST(e) >= 0x1770 && GCALL2(0x80100FF4u, e, 0) != 0) {
            GCALL1(0x80101E58u, e);
            step = 0;
        }
        if (step) gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0xA0, 0);
    }
    children_step_sound(e);
}

/* 80103D60: action 3's second handler, nothing. */
void n_children_Act3Step(void) {
}

/* 80103D68 (e): action 4, side step: for 8 frames (16 while it sees the
 * player; then walk on), steering round walls 2000 ahead, else turning to
 * the side of the player (closer than 0x1130: 6000 out at the side; else
 * 4000 out on its turn, +0x35C); slower when close. Pushed off the player
 * within 2000. */
void n_children_SidestepPre(void) {
    FRAME(0x28);
    u32 e = A0;
    u32 speed;
    int go = 1;
    if (D_800BB19C != 3) {
        s32 c = CHILD_COUNT(e) + 1;
        CHILD_COUNT(e) = c;
        if ((c >= 9 && !(EN_FLAGS(e) & EF_SEES)) || c >= 0x11) {
            GCALL2(0x80101B0Cu, e, 0x10);
            go = 0;
        }
    }
    if (go) {
        speed = (u32)-0xA0;
        s32 w = (s32)GCALL2(0x80100C54u, e, 2);
        if (w != -1) {
            GCALL4(0x8010169Cu, e, 0x71, 1, w);
            speed = (u32)-0x50;
        } else {
            GS(u32, sp + 0x10) = 1;
            if ((u32)EN_DIST(e) < 0x1130) GCALL4(0x801012C8u, e, 6, 0x20, 0);
            else GCALL4(0x801012C8u, e, 4, 0x40, GV(u32, e + 0x35C));
        }
        if ((u32)EN_DIST(e) < 0xAF0 && D_800BB19C != 3) speed = (u32)-0xA0;
        gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), speed, 0);
    }
    children_step_sound(e);
    if ((u32)EN_DIST(e) < 0x7D0) {
        EN_VX(e) += PLAYER_PREV_X < EN_X(e) ? 0x12C : -0x12C;
        EN_VZ(e) += PLAYER_PREV_Z < EN_Z(e) ? 0x12C : -0x12C;
    }
}

/* 80103F44: action 4's second handler, nothing. */
void n_children_SidestepStep(void) {
}

/* 8010400C: action 5's second handler, nothing. */
void n_children_Act5Step(void) {
}

/* 80104014 (e): action 5: on the animation's event go to the player
 * (seen, in the narrow cone, facing: action 3) or walk on; step on
 * slowly while past 2000 with no wall 1000 ahead. */
void n_children_Act5Pre(void) {
    FRAME(0x18);
    u32 e = A0;
    if (GCALL3(0x80100700u, e, 1, 1) != 0) {
        if ((EN_FLAGS(e) & EF_SEES) && GCALL2(0x80100FF4u, e, 2) != 0 && GCALL1(0x80101294u, e) == 0) {
            GCALL3(0x801006D0u, e, 2, 0x10);
            EN_ACTION(e) = 3;
        } else {
            GCALL2(0x80101B0Cu, e, 0x10);
        }
    }
    if ((s32)GCALL2(0x80100C54u, e, 1) == -1 && (u32)EN_DIST(e) >= 0x7D1)
        gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x64, 0);
}

/* 801040DC (e): action 7, calling out (+0x364 the phase): phase 0 on the
 * animation's event: the call (animation 4 or 5 by child and distance,
 * voice 0x30); phase 1 while the work block's timer runs: the mark over
 * its head, and seeing the player face to face (not too far for Lott:
 * odds 30, 15 or 5%) maybe holding on to them; then walking off by who it
 * is. Phase 2 plays the parts backwards once the animation is past its
 * end. */
void n_children_CallPre(void) {
    FRAME(0x28);
    u32 e = A0;
    GCALL1(0x80101980u, e);
    if (CHILD_STEP(e) == 2) {
        u32 a = EN_ANIMSTATE(e);
        if (GV(u16, a + 0x14) < GV(u16, a + 0x16)) {
            GCALL2(0x8010180Cu, e, (u32)-0x10);
            CHILD_STEP(e) += 1;
            CHILD_FLAGS(e) &= ~0x20u;
        }
        return;
    }
    if (GCALL3(0x80100700u, e, 1, 2) == 0) {
        if (CHILD_STEP(e) == 1 && GCALL3(0x80100700u, e, 1, 8) != 0) GCALL1(0x80100074u, e);
        return;
    }
    s32 c = CHILD_STEP(e);
    if (c == 0) {
        u32 anim;
        s32 id = CHILD_ID(e);
        if (D_800BB19C == 3 || id == 2) anim = 4;
        else if (id != 1) anim = 5;
        else anim = (u32)EN_DIST(e) < 0x1388 ? 4 : 5;
        GCALL3(0x801006D0u, e, anim, 2);
        CHILD_FLAGS(e) |= 2;
        GCALL3(0x80100460u, 3, 0x30, e);
    } else if (c == 1) {
        u32 w = CH;
        u16 t = (u16)(GV(u16, w + 0) - 1);
        GV(u16, w + 0) = t;
        if ((s16)t > 0) {
            CHILD_STEP(e) -= 1;
        } else {
            GCALL3(0x801006D0u, e, 3, 2);
            GV(u16, e + 0x862) = 6;
            CHILD_FLAGS(e) |= 0x20;
        }
        if (D_800BB19C != 3 && (EN_FLAGS(e) & EF_SEES) && GCALL1(0x80101294u, e) == 0) {
            s32 odds = 0x1E;
            if (CHILD_ID(e) != 1) {
                s32 d = EN_DIST(e);
                if (d >= 0x1770) odds = d < 0x2710 ? 0xF : 5;
            }
            if (!(EN_FLAGS(e) & 2)) {
                int boxed = 0;
                if (CHILD_ID(e) == 3) {
                    GS(u32, sp + 0x10) = 0x1004;
                    GS(u32, sp + 0x14) = 0xB22;
                    boxed = GCALL4(FN_C_IN_BOX, e, 0, 0x17D4, 0x8FC) != 0;
                }
                if (!boxed && !(odds < children_rand() % 100) && GCALL1(0x801017E0u, e) == 0 &&
                    GCALL2(0x80100FF4u, e, 3) != 0) {
                    GCALL2(0x80101A6Cu, e, 8);
                    GCALL2(FN_EN_CUE, e, 5);
                }
            }
        }
        GCALL1(0x80100074u, e);
    } else {
        s32 id = CHILD_ID(e);
        if (id == 1) GCALL1(FN_C_CALL_LOTT, e);
        else if ((u32)(id - 2) < 2) GCALL1(FN_C_CALL_OTHER, e);
        else GCALL1(0x80101BB0u, e);
    }
    CHILD_STEP(e) += 1;
}

/* 8010441C: action 7's second handler, nothing. */
void n_children_CallStep(void) {
}

/* 80104424 (e): action 8, holding on: on the first event let go
 * (0x80102648); on event 0x10 with the player seen within 0x961 and the
 * work count (+0x24) 1, hold them (6) and sound 8; at event 0x12 with the
 * count 0 sound 6; each sound counts. */
void n_children_Act8Pre(void) {
    FRAME(0x20);
    u32 e = A0;
    if (GCALL3(0x80100700u, e, 1, 1) != 0) { GCALL1(FN_C_GETUP, e); return; }
    u32 k;
    if (GCALL3(0x80100700u, e, 1, 0x10) != 0) {
        if (!(EN_FLAGS(e) & EF_SEES)) return;
        if ((u32)EN_DIST(e) >= 0x961) return;
        if (GV(s32, CH + 0x24) != 1) return;
        GCALL2(0x80101A6Cu, e, 6);
        k = 8;
    } else {
        if (GV(s32, CH + 0x24) != 0) return;
        if (GCALL3(0x80100700u, e, 1, 0x12) == 0) return;
        k = 6;
    }
    GCALL2(FN_EN_CUE, e, k);
    GV(s32, CH + 0x24) += 1;
}

/* 80104528: action 8's second handler, nothing. */
void n_children_Act8Step(void) {
}

/* 801046D0 (e): action 12, hurt: sound 9 at event 3; at event 2 vanish
 * when dead, else duck one way or the other at random. */
void n_children_Act12Pre(void) {
    FRAME(0x18);
    u32 e = A0;
    if (GCALL3(0x80100700u, e, 1, 3) != 0) GCALL2(FN_EN_CUE, e, 9);
    if (GCALL3(0x80100700u, e, 1, 2) == 0) return;
    if (EN_HP(e) <= 0) GCALL1(0x80101FECu, e);
    else if (children_rand() & 0x20) GCALL1(FN_C_DUCK_A, e);
    else GCALL1(FN_C_DUCK_B, e);
}

/* 80104770: action 12's second handler, nothing. */
void n_children_Act12Step(void) {
}

/* 80104840 (e): action 15, vanishing: first sinking (+0x174 by 0x40 to
 * 0x41); then (sound 0xD at frame 10) blinking every other frame, puffs
 * at frames 11-20, and at 21 gone (left behind; the work timer 0-116). */
void n_children_VanishPre(void) {
    FRAME(0x18);
    u32 e = A0;
    s32 h = GV(s32, e + 0x174);
    if (h >= 0x42) { GV(s32, e + 0x174) = h - 0x40; return; }
    if (CHILD_STEP(e) == 10) GCALL3(FN_EN_ANIM_SOUND, e, 0xD, 10);
    u32 c = (u32)CHILD_TURN(e) + 1;
    CHILD_TURN(e) = (s32)c;
    if (!(c & 1)) {
        u32 show = (c & 2) == 0;
        for (u32 i = 0; (s32)i < (s32)GV(u8, e + 0x28); i++) {
            if (i) LOOP_POLL(0x801048D0u);
            GV(u8, e + i) = (u8)show;
        }
        GV(u16, GV(u32, e + 0x8BC) + 0xD4) = (u16)(show ^ 1);
    }
    s32 s = CHILD_STEP(e) + 1;
    CHILD_STEP(e) = s;
    if (s >= 0xB) {
        if (s >= 0x15) goto gone;
        GCALL1(0x80100298u, e);
    }
    if (CHILD_STEP(e) < 0x15) return;
gone:
    GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 1;
    GCALL1(0x80102060u, e);
    {
        s32 r = children_rand() % 30;
        GV(u16, CH + 6) = (u16)(r << 2);
    }
}

/* 8010498C: action 15's second handler, nothing. */
void n_children_VanishStep(void) {
}

/* 80104E64: action 17's second handler, nothing. */
void n_children_ReturnStep(void) {
}

/* 80104A7C (e): action 17, left behind, while children are still to come
 * back (g_childrenBack): the lift in stage 2 room 16 after the work
 * timer; Lily back at her spot (work +0x18, +0x1C) after the timer; in
 * stage 5 by where the player is; child 3 when it can; others every 4th
 * frame at a random point 30-49 x 100 from the player, off their line of
 * sight, when there is no wall in between. */
void n_children_ReturnPre(void) {
    FRAME(0x38);
    u32 e = A0;
    if (GV(s16, g_childrenBack) == 0 && GV(s16, g_childrenBack + 2) == 0) return;
    if (GV(s32, 0x800A866C) == 2 && GV(s32, 0x800B3F08) == 0x10) {
        u32 w = CH;
        if (GV(s16, w + 6) == 0) {
            GCALL1(FN_C_SCRIPT_LIFT, e);
            GV(u16, g_childrenBack) -= 1;
        } else {
            GV(u16, w + 6) -= 1;
        }
        return;
    }
    s32 id = CHILD_ID(e);
    if (id == 2) {
        u32 w = CH;
        u16 t = (u16)(GV(u16, w + 6) - 1);
        GV(u16, w + 6) = t;
        if ((s16)t > 0) return;
        GCALL1(FN_C_REAPPEAR, e);
        w = CH;
        CHILD_ID(e) = id;
        EN_X(e) = GV(s32, w + 0x18);
        s32 z = GV(s32, w + 0x1C);
        EN_Z(e) = z;
        GV(u16, e + 0x1C6) = z > 0 ? 0 : 0x800;
        EN_ACTION(e) = 0x15;
        GCALL3(0x801006D0u, e, 2, 0x10);
        GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 1;
        GV(u16, g_childrenBack) -= 1;
        return;
    }
    if (GV(s32, 0x800A866C) == 5) {
        s32 pz = PLAYER_PREV_Z;
        if (pz <= 0x1481F) {
            if (GV(s16, g_childrenBack) > 0) GCALL1(FN_C_CATCH_UP, e);
            return;
        }
        if ((u32)pz + 0xFFFEB3F7u > 0xB796) return;
        u32 w = CH;
        u16 t = (u16)(GV(u16, w + 6) - 1);
        GV(u16, w + 6) = t;
        if ((s16)t >= 0) return;
        if (GV(s16, g_childrenBack + 2) > 0) GCALL1(FN_C_CATCH_UP2, e);
        return;
    }
    if (id == 3) {
        u32 w = CH;
        s16 t = GV(s16, w + 6);
        if (t > 0) {
            GV(u16, w + 6) = (u16)(t - 1);
            if ((s16)(t - 1) > 0) return;
        }
        if (GCALL2(FN_C_CAN_RETURN, e, 1) != 0) return;
        GCALL1(FN_C_REAPPEAR, e);
        GCALL1(FN_C_SCRIPT_ROOM, e);
        GV(u16, g_childrenBack) -= 1;
        return;
    }
    s32 c = CHILD_STEP(e) + 1;
    CHILD_STEP(e) = c;
    if (c & 3) return;
    u32 rot = sp + 0x10, v = sp + 0x18;
    GS(u16, rot + 4) = 0;
    GS(u16, rot + 0) = 0;
    s32 a = children_rand() % 948;
    if (a >= 0x1DB) a += 0x4E0;
    GS(u16, rot + 2) = (u16)((GV(u16, g_player + 0x46) + (u32)a + 0x3B6) & 0xFFF);
    gcall(F_ROT_STEP, rot, v, (u32)-100, 0);
    s32 k = children_rand() % 20 + 0x1E;
    u32 x = GS(u32, v + 0) * (u32)k, z = GS(u32, v + 8) * (u32)k;
    GS(u32, v + 4) = GV(u32, PLAYER_POS_ADDR + 4);
    GS(u32, v + 0) = x + GV(u32, PLAYER_POS_ADDR);
    GS(u32, v + 8) = z + GV(u32, PLAYER_POS_ADDR + 8);
    if ((s32)GCALL2(0x80100A84u, PLAYER_POS_ADDR, v) != -1) return;
    EN_X(e) = GS(s32, v + 0);
    EN_Z(e) = GS(s32, v + 8);
    u32 d = (u32)((s32)GS(s16, rot + 2) - (s32)GV(s16, g_player + 0x46)) & 0xFFF;
    u16 y = GS(u16, rot + 2);
    GV(u16, e + 0x1C6) = (u16)(((s32)d < 0x800 ? y - 0x270 : y + 0x270) & 0xFFF);
    GCALL1(FN_C_REAPPEAR, e);
    GV(u16, g_childrenBack) -= 1;
}

/* 8010599C (e): state 2: both of the action's handlers. */
void n_children_StateHit(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, CHILDREN_ACT_PRE + (u32)((s32)EN_ACTION(e) << 2)), e);
    GCALL1(GV(u32, CHILDREN_ACT_STEP + (u32)((s32)EN_ACTION(e) << 2)), e);
}

/* 80105A04 (e): state 3: the same. */
void n_children_StateDying(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, CHILDREN_ACT_PRE + (u32)((s32)EN_ACTION(e) << 2)), e);
    GCALL1(GV(u32, CHILDREN_ACT_STEP + (u32)((s32)EN_ACTION(e) << 2)), e);
}

/* 80102B78 (e): should the child set off walking? Always in stage 5;
 * else when the player runs (move speed below -0x73), +0x320 bit 6 is
 * set, the player is in the wide view cone, or a shot is fired with the
 * reticle on screen. */
void n_children_WantsGo(void) {
    FRAME(0x18);
    u32 e = A0;
    if (GV(s32, 0x800A866C) == 5 || g_playerMoveSpeed < -0x73 || (EN_FLAGS(e) & 0x40) ||
        GCALL2(0x80100FF4u, e, 1) != 0) { V0 = 1; return; }
    V0 = (g_fireRequest != 0 && g_reticleX >= 0 && g_reticleY >= 0) ? 1 : 0;
}

/* 8010468C (e): action 11 (a hit reaction): walk on at the animation's
 * event. */
void n_children_Act11Pre(void) {
    FRAME(0x18);
    u32 e = A0;
    if (GCALL3(0x80100700u, e, 1, 1) != 0) GCALL2(0x80101B0Cu, e, 0x10);
}

/* 801046C8: action 11's second handler, nothing. */
void n_children_Act11Step(void) {
}

/* 80105A6C: state 4, nothing. */
void n_children_State4(void) {
}
