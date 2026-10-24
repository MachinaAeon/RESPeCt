/* The Hunter (enemy program ENEMPROG 60, loaded in enemy slot B at
 * 0x80100000). Identified by how it looks in the area scenarios that load
 * it (area27: stage 4 room 1, area30: stage 16 room 7: the green, clawed
 * reptile) and by what the code does: it prowls round walls, circles to
 * the player's side, rushes in and leaps at them (through its model's
 * height, +0x8BC +0xB0), slashes (grabs the player) and backs off when
 * hurt.
 *
 * hunter_Update (0x8010537C) runs every frame for each one: it picks the
 * work block, counts animation frames, runs the per-room set-up once
 * (hunter_StageCheck), runs the state's handler (0x80105B34; state 1,
 * hunter_StateActive, runs the action's handler from 0x80105AE8), takes
 * the hits (hunter_OnHit) and keeps a copy of its position.
 * The action handlers act by what it is doing (HW_MODE): 1 and 2 prowling
 * and rushing, 3 and 4 leaping (at the player, or far), 5 waiting, 6
 * sidestepping, 9 dying, 18 the room 1 retreat... (switches through jump
 * tables in the overlay's first words).
 *
 * Each one has a 0x34-byte work block (0x80105B48 + slot * 0x34; the
 * current one's address is kept at 0x80105CB4, see HW_*). Animations are
 * started like the Cerberus' (en_start_anim).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"
#include "poll.h"

void n_hunter_Near(void);
void n_hunter_Advance(void);
void n_hunter_GrabStep(void);
void n_hunter_NotFacing(void);
void n_hunter_CrossedWall(void);
void n_hunter_WallAhead(void);
void n_hunter_PlayerWalls(void);
void n_hunter_Wander(void);
void n_hunter_InPlayerCone(void);
void n_hunter_PlayerInCone(void);
void n_hunter_TurnTo(void);
void n_hunter_WallToPlayer(void);
void n_hunter_PlayerHasRoom(void);
void n_hunter_Status(void);
void n_hunter_Lunge(void);
void n_hunter_AnimEvent(void);
void n_hunter_StepSound(void);
void n_hunter_Anim(void);
void n_hunter_Cooldown(void);
void n_hunter_TryPounce(void);
void n_hunter_PickSide(void);
void n_hunter_Lift(void);
void n_hunter_Land(void);
void n_hunter_ProwlStep(void);
void n_hunter_RushStep(void);
void n_hunter_LeapStep(void);
void n_hunter_LeapFarStep(void);
void n_hunter_WaitStep(void);
void n_hunter_SidestepStep(void);
void n_hunter_LeapStart(void);
void n_hunter_IdleAct(void);
void n_hunter_WalkAct(void);
void n_hunter_RunAct(void);
void n_hunter_SlashAct(void);
void n_hunter_LeapAct(void);
void n_hunter_RiseAct(void);
void n_hunter_OnHit(void);
void n_hunter_Room1Retreat(void);
void n_hunter_Stage4Check(void);
void n_hunter_StageCheck(void);
void n_hunter_StateActive(void);
void n_hunter_Update(void);

/* ---- the work block ---- */
#define HW               GV(u32, 0x80105CB4)
#define HW_STARTED       GV(s8, HW + 0x00)
#define HW_TURN          GV(u8, HW + 0x01)    /* steering round a wall: 0 no, 1 left, 2 right */
#define HW_MODE          GV(u8, HW + 0x02)    /* what it is doing (see the top) */
#define HW_SIDE          GV(u8, HW + 0x05)    /* the side it sidesteps to: 1, 2 */
#define HW_WAIT          GV(u8, HW + 0x07)    /* frames before it may leap again */
#define HW_ANIM          GV(u8, HW + 0x08)    /* last frame's animation */
#define HW_HITS          GV(u8, HW + 0x09)    /* hits towards backing off */
#define HW_ROOM_DONE     GV(u8, HW + 0x0A)    /* the per-room set-up ran */
#define HW_RETREAT       GV(u8, HW + 0x0B)
#define HW_HIT_TIME      GV(u8, HW + 0x0C)    /* frames since it was last hit (from 100) */
#define HW_AIRBORNE      GV(u8, HW + 0x0D)
#define HW_TOUGH         GV(u8, HW + 0x0E)    /* hits don't count while 0? */
#define HW_HALF_HP       GV(s16, HW + 0x10)
#define HW_DIST          GV(s16, HW + 0x12)   /* leap length, or a timer */
#define HW_TIME          GV(s16, HW + 0x14)   /* timer, or vertical speed */
#define HW_UP            GV(s16, HW + 0x16)   /* leap: frames rising */
#define HW_FRAME         GV(s16, HW + 0x18)   /* animation frame counter */
#define HW_PREV_FRAME    GV(s16, HW + 0x1A)
#define HW_ANIM_FRAME    GV(s16, HW + 0x1C)   /* last frame's animation frame */
#define HW_KILL_WEAPON   GV(u16, HW + 0x1E)
#define HW_SEED          GV(u16, HW + 0x20)   /* counts frames: a random factor */
#define HW_POS           (HW + 0x24)          /* last position (16 bytes) */
#define HUNTER_WORK_BASE 0x80105B48u
#define HUNTER_WORK_SIZE 0x34

#define HUNTER_ACTIONS   0x80105AE8u
#define HUNTER_STATES    0x80105B34u
#define HUNTER_SIDE_YAWS 0x80105AD4u          /* s16 x 4: escape directions */
#define g_hunterSeed0    GV(u16, 0x80105B68)  /* the first one's HW_SEED */
#define g_hunterFrame0   GV(s16, 0x80105B60)  /* the first one's HW_FRAME */
#define g_hunterAlarm    GV(u8, 0x80105CB8)   /* the stage 16 alarm was raised */
#define g_hunterPounceWait GV(u8, 0x80105CB9) /* frames before any one pounces */
#define g_hunterLastBuf  GV(u32, 0x80105CBC)  /* the frame buffer counted last */
#define g_gameMode       GV(s32, 0x800BB19C)
#define g_playerHp       GV(s16, 0x800A8584)
#define g_currentWeapon  GV(u32, 0x800A8580)   /* pointer: *it is the weapon ID */
#define g_stage          GV(s32, 0x800A866C)
#define g_room           GV(s32, 0x800B3F08)
#define g_fireReq        GV(u8, 0x80062E40)
#define g_aimX           GV(s32, 0x80062DDC)
#define g_aimY           GV(s32, 0x80062DE0)
#define FN_EN_ANIM_SOUND 0x8008127Cu   /* (e, bank, sound) */
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_DISTANCE      0x8007BDDCu   /* (from, to) */
#define FN_BIOS_RAND     0x800398A8u
#define FN_ALARM         0x800731BCu   /* (r, g, b, n): the screen flash */
#define F_ROT_MATRIX2    0x8002AA68u   /* (rotation, matrix) */
#define F_SET_ROT_MATRIX 0x8002A778u
#define F_SET_TRANS_MATRIX 0x8002A7A8u
#define F_ROT_TRANS      0x8002A958u   /* (short vector, long vector out, flag out) */
#define g_bufIndex       GV(u8, 0x80062D1C) /* double buffer: 0 or 1 */
/* translated (not reached by the scenarios) */
#define FN_HU_PLAYER_SIDE 0x80100440u  /* (e): 0 ahead, 1 / 2 the sides */
#define FN_HU_WALL_DIST  0x80100518u   /* (e, wall) */
#define FN_HU_CROSSED_EDGE 0x8010071Cu /* (from, to) */
#define FN_HU_NOP        0x801017D0u
#define FN_HU_IDLE0      0x80101A94u
#define FN_HU_IDLE1      0x80101C14u
#define FN_HU_WALK0      0x80101B08u
#define FN_HU_MODE7      0x801026F4u
#define FN_HU_SETUP      0x801051A0u

static u32 hu_call(void (*f)(void), u32 a0, u32 a1, u32 a2) {
    A0 = a0; A1 = a1; A2 = a2;
    f();
    return V0;
}
static void hu_anim(u32 e, u32 anim, u32 blend) { hu_call(n_hunter_Anim, e, anim, blend); }

/* Has the animation reached its end (frame +0x16 not behind +0x14)? */
static int hu_anim_done(u32 e) {
    u32 an = EN_ANIMSTATE(e);
    return GV(u16, an + 0x16) >= GV(u16, an + 0x14);
}

/* 80100128 (e, r): within r of its position last frame? */
void n_hunter_Near(void) {
    FRAME(0x38);
    u32 e = A0;
    s32 r = (s32)A1;
    for (u32 k = 0; k < 16; k += 4) GS(u32, sp + 0x10 + k) = GV(u32, HW_POS + k);
    for (u32 k = 0; k < 16; k += 4) GS(u32, sp + 0x20 + k) = GV(u32, e + 0x198 + k);
    s32 d = (s32)gcall(FN_DISTANCE, sp + 0x10, sp + 0x20, 0, 0);
    V0 = !(r < d);
}

/* 80100240 (rot, out, dist): the step dist ahead along rot (the GTE's
 * rotation and translation from template 0x80065B90), rounded to 1/32. */
void n_hunter_Advance(void) {
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
    for (u32 k = 0; k < 3; k++) GS(u16, v + k * 2) = (u16)(GS(u8, out + k * 4) >> 4);
    MEM_W32(out + 0, (u32)((s32)MEM_R32(out + 0) >> 5));
    MEM_W32(out + 8, (u32)((s32)MEM_R32(out + 8) >> 5));
    MEM_W32(out + 4, (u32)((s32)MEM_R32(out + 4) >> 5));
    for (u32 k = 0; k < 3; k++)
        if (GS(s16, v + k * 2) >= 8) MEM_W32(out + k * 4, MEM_R32(out + k * 4) + 1);
}

/* 801003AC (e): grab the player if the game allows it, closing in (up to
 * 1000) when nearer than 2300. */
void n_hunter_GrabStep(void) {
    FRAME(0x18);
    u32 e = A0;
    if (g_gameMode == 1 && ep_grabLock == 0 && ep_grabLock2 == 0) {
        gcall(FN_EN_GRAB_PLAYER, e, 7, 0, 0);
        GCALL1(FN_EN_GRAB_START, e);
    }
    u32 d = (u32)EN_DIST(e);
    if (!(d < 0x8FC)) return;
    s32 step = (s32)(0x8FC - d);
    if (!(step < 0x3E9)) step = 0x3E8;
    hu_call(n_hunter_Advance, EN_ROT(e), EN_VEL(e), (u32)step);
}

/* 801004E4 (e): 1 unless it and the player face each other (yaws
 * 0x47D..0xB83 apart). */
void n_hunter_NotFacing(void) {
    V0 = en_not_facing(A0, 0x47D, 0x707);
}

/* 80100574 (from, to): the first wall the path crosses from its front,
 * or -1. */
void n_hunter_CrossedWall(void) {
    FRAME(0x40);
    V0 = (u32)en_crossed_segment_poll(A0, A1, GV(u32, EP_WALLS_N + 4), GV(u8, EP_WALLS_N), 16, 1, 0x801006EC);
}

/* 801008C0 (e, k): the wall within k x 1000 ahead, or -1. */
void n_hunter_WallAhead(void) {
    FRAME(0x30);
    V0 = en_wall_ahead(sp + 0x10, A0, A1, 0x80100574u);
}

/* 8010099C (k): walls beside the player (see en_player_walls). */
void n_hunter_PlayerWalls(void) {
    FRAME(0x38);
    V0 = en_player_walls(sp + 0x10, A0, 0x80100574u);
}

/* 80100B70 (e, k, turn): steer round the wall within k x 1000 ahead (by
 * turn a frame; 0: 51..150 at random): pick the way (left when there is
 * a wall both sides or on the right, else right; else at random or
 * straight on when the wall is far) and keep turning that way. */
void n_hunter_Wander(void) {
    FRAME(0x30);
    en_wander(A0, A1, A2, HW, 0x801008C0u, FN_HU_WALL_DIST);
}

/* 80100D90 (e): is e within the player's rear cone (yaw + 0x800 +0x226
 * -0x226)? */
void n_hunter_InPlayerCone(void) {
    FRAME(0x50);
    u32 e = A0;
    u32 rot = sp + 0x30, left = sp + 0x20, right = sp + 0x10;
    GS(u16, rot + 0) = GV(u16, g_player + 0x44);
    u16 y = GV(u16, g_player + 0x46);
    GS(u16, rot + 2) = (u16)(y + 0x800);
    GS(u16, rot + 4) = GV(u16, g_player + 0x48);
    GS(u16, rot + 2) = (u16)((y + 0xA26) & 0xFFF);
    gcall(F_ROT_STEP, rot, left, 100, 0);
    MEM_W32(left, MEM_R32(left) + GV(u32, PLAYER_POS_ADDR));
    MEM_W32(left + 8, MEM_R32(left + 8) + GV(u32, PLAYER_POS_ADDR + 8));
    GS(u16, rot + 2) = (u16)(GS(u16, rot + 2) - 0x44C);
    gcall(F_ROT_STEP, rot, right, 100, 0);
    MEM_W32(right, MEM_R32(right) + GV(u32, PLAYER_POS_ADDR));
    MEM_W32(right + 8, MEM_R32(right + 8) + GV(u32, PLAYER_POS_ADDR + 8));
    if ((s32)gcall(FN_LINE_SIDE, PLAYER_POS_ADDR, left, (u32)EN_X(e), (u32)EN_Z(e)) <= 0) { V0 = 0; return; }
    V0 = (s32)gcall(FN_LINE_SIDE, PLAYER_POS_ADDR, right, (u32)EN_X(e), (u32)EN_Z(e)) < 0;
}

/* 80100EAC (e, half): is the player within yaw +-half of e? */
void n_hunter_PlayerInCone(void) {
    FRAME(0x50);
    V0 = en_player_in_cone_r(sp, A0, A1);
}

/* 80100FC0 (e, dist, step): turn towards the player (see en_turn_to). */
void n_hunter_TurnTo(void) {
    FRAME(0x48);
    V0 = en_turn_to(sp, A0, A1, A2, 0x80100EACu);
}

/* 80101118 (e): is there a wall (in stage 16 room 9 an area edge) between
 * it and the player? */
void n_hunter_WallToPlayer(void) {
    FRAME(0x18);
    u32 e = A0;
    s32 w;
    if (g_stage == 0x10 && g_room == 9) w = (s32)gcall(FN_HU_CROSSED_EDGE, EN_POS(e), PLAYER_POS_ADDR, 0, 0);
    else w = (s32)hu_call(n_hunter_CrossedWall, EN_POS(e), PLAYER_POS_ADDR, 0);
    V0 = w != -1;
}

/* The point 3000 from the player along their rotation rot into v; is
 * the way from e there clear? */
static int hu_clear_to(u32 e, u32 rot, u32 v) {
    gcall(F_ROT_STEP, rot, v, (u32)-100, 0);
    u32 x = MEM_R32(v + 0) * 30, y = MEM_R32(v + 4) * 30, z = MEM_R32(v + 8) * 30;
    MEM_W32(v + 0, x + GV(u32, PLAYER_POS_ADDR));
    MEM_W32(v + 4, y + GV(u32, PLAYER_POS_ADDR + 4));
    MEM_W32(v + 8, z + GV(u32, PLAYER_POS_ADDR + 8));
    return (s32)hu_call(n_hunter_CrossedWall, EN_POS(e), v, 0) == -1;
}

/* 80101180 (e): is the way clear from e to a point 3000 from the player,
 * in front of them or in one of four directions round them? */
void n_hunter_PlayerHasRoom(void) {
    FRAME(0x40);
    u32 e = A0, v = sp + 0x10, rot = sp + 0x20;
    if (hu_clear_to(e, PLAYER_ROT_ADDR, v)) { V0 = 1; return; }
    for (u32 i = 0;; ) {
        GS(u16, rot + 4) = 0;
        GS(u16, rot + 0) = 0;
        GS(u16, rot + 2) = (u16)(GV(u16, g_player + 0x46) + GV(u16, HUNTER_SIDE_YAWS + i * 2));
        if (hu_clear_to(e, rot, v)) { V0 = 1; return; }
        i++;
        if (!(i < 4)) break;
        LOOP_POLL(0x801012E8);
    }
    V0 = 0;
}

/* 80101310 (e): why it can't leap: bit 0 near (2000), 2 not behind the
 * player, 3 player not in its cone, 4 no room round the player (stage 2
 * room 12: never near). */
void n_hunter_Status(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 s = (u32)EN_DIST(e) < 0x7D0;
    if (!hu_call(n_hunter_InPlayerCone, e, 0, 0)) s |= 4;
    if (!hu_call(n_hunter_PlayerInCone, e, 0x71, 0)) s |= 8;
    if (!hu_call(n_hunter_PlayerHasRoom, e, 0, 0)) s |= 0x10;
    if (g_stage == 2 && g_room == 0xC) s &= ~1u;
    V0 = s;
}

/* 801013A8 (e): in the air near the floor: grab the player when close
 * (2000), facing and seen. */
void n_hunter_Lunge(void) {
    FRAME(0x18);
    u32 e = A0;
    if (EN_Y(e) < -0x6A4) return;
    if (!((u32)EN_DIST(e) < 0x7D1)) return;
    if (hu_call(n_hunter_NotFacing, e, 0, 0)) return;
    if (!(EN_FLAGS(e) & EF_SEES)) return;
    hu_call(n_hunter_GrabStep, e, 1, 1);
}

/* 80101464 (e, part, k): the animation event bit (en_anim_event). */
void n_hunter_AnimEvent(void) {
    V0 = en_anim_event(A0, A1, A2);
}

/* 801014C8 (e, f1, f2): a footstep on reaching animation frame f1 or f2
 * (in stage 16 room 1, on the floor, a sound by distance). */
void n_hunter_StepSound(void) {
    FRAME(0x18);
    u32 e = A0;
    s32 f1 = (s32)A1, f2 = (s32)A2;
    s32 f = HW_FRAME;
    if (!((f == f1 && HW_PREV_FRAME != f) || (f == f2 && HW_PREV_FRAME != f2))) return;
    if (g_stage == 0x10 && g_room == 1 && EN_Y(e) == 0) {
        s32 v = (s32)((u32)EN_DIST(e) / 80);
        if (!(v < 0x7F)) v = 0x7F;
        u32 vol = (u32)(0x7F - v);
        gcall(FN_SOUND_CUE2, 6, 0, vol, vol);
        return;
    }
    gcall(FN_EN_ANIM_SOUND, e, 8, 2, 0);
}

/* 801015A0 (e, anim, blend): start an action / animation. */
void n_hunter_Anim(void) {
    en_start_anim(A0, A1, A2);
    V0 = 1;
}

/* 801015D4 (e): 1 when the leap wait has run out (and restart it: 30). */
void n_hunter_Cooldown(void) {
    FRAME(0x18);
    u8 t = HW_WAIT;
    if (t != 0) { HW_WAIT = t - 1; V0 = 0; return; }
    HW_WAIT = 0x1E;
    en_rand();
    V0 = 1;
}

/* 80101620 (e): pounce on the player (seen, within 2500, facing, in its
 * cone, the game allowing it, no other pounce lately): 1 when it does. */
void n_hunter_TryPounce(void) {
    FRAME(0x20);
    u32 e = A0;
    V0 = 0;
    if (!(EN_FLAGS(e) & EF_SEES)) return;
    if (!((u32)EN_DIST(e) < 0x9C4)) return;
    if (hu_call(n_hunter_NotFacing, e, 0, 0)) { V0 = 0; return; }
    if (!hu_call(n_hunter_PlayerInCone, e, 0x71, 0)) { V0 = 0; return; }
    if (g_gameMode == 2) { V0 = 0; return; }
    if (ep_grabLock2 != 0) { V0 = 0; return; }
    if (g_hunterPounceWait != 0) { V0 = 0; return; }
    HW_MODE = 5;
    if (!(HW_HALF_HP < EN_HP(e)) && g_playerHp < 0x65) {
        hu_anim(e, 4, 0x40);
        g_hunterPounceWait = 0x18;
        V0 = 1;
        return;
    }
    if (GV(u16, g_currentWeapon) < 2) {
        hu_anim(e, 5, 0x40);
        g_hunterPounceWait = 0x1D;
    } else {
        s32 r = (s32)(en_rand() * (u32)HW_SEED);
        hu_anim(e, 5, 0x40);
        g_hunterPounceWait = r % 9 == 0 ? 0x1D : 0x15;
    }
    V0 = 1;
}

/* 801017E0 (e): pick the side to sidestep to (away from walls round the
 * player, else by where it stands and faces) and start (mode 6): 0; 1
 * when walled in both sides. */
void n_hunter_PickSide(void) {
    FRAME(0x20);
    u32 e = A0;
    s32 w = (s32)hu_call(n_hunter_PlayerWalls, 1, 0, 0);
    if (w == 1) {
        HW_SIDE = 2;
    } else if (w == 2) {
        HW_SIDE = 1;
    } else if (w == 0) {
        s32 side = (s32)gcall(FN_HU_PLAYER_SIDE, e, 0, 0, 0);
        if (side == 1) {
            HW_SIDE = hu_call(n_hunter_NotFacing, e, 0, 0) ? 2 : 1;
        } else if (side == 2) {
            HW_SIDE = hu_call(n_hunter_NotFacing, e, 0, 0) ? 1 : 2;
        } else if (side == 0) {
            HW_SIDE = en_rand() + (u32)HW_SEED == 0 ? 2 : 1;
        }
    } else {
        V0 = 1;
        return;
    }
    HW_MODE = 6;
    hu_anim(e, 2, 2);
    V0 = 0;
}

/* 80101940 (e, dy): rise by dy (en_lift). */
void n_hunter_Lift(void) {
    en_lift(A0, A1);
}

/* 8010196C (e): 1 while still in the air; else land (height 0) with a
 * thud, 0. */
void n_hunter_Land(void) {
    FRAME(0x18);
    u32 e = A0;
    if (GV(s32, GV(u32, e + 0x8BC) + 0xB0) < 0) { V0 = 1; return; }
    GV(u32, e + 0x19C) = 0;
    GV(u32, GV(u32, e + 0x8BC) + 0xB0) = 0;
    EN_VY(e) = 0;
    if (g_stage == 0x10 && g_room == 1) gcall(FN_SOUND_CUE2, 6, 1, 0x7F, 0x7F);
    else gcall(FN_EN_ANIM_SOUND, e, 8, 2, 0);
    V0 = 0;
}

/* 80101C94 (e): prowl (26 a frame): rush when the player is in front of
 * it or aims at it; turn to them when seen and no wall between, else
 * steer round the walls (or turn when stuck). */
void n_hunter_ProwlStep(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 walled = hu_call(n_hunter_WallToPlayer, e, 0, 0);
    if (walled == 0) {
        if (hu_call(n_hunter_PlayerInCone, e, 0x400, 0) || (g_fireReq != 0 && g_aimX >= 0 && g_aimY >= 0)) {
            HW_MODE = 2;
            hu_anim(e, 2, 2);
        }
    }
    if (walled == 0 && (EN_FLAGS(e) & EF_SEES)) {
        hu_call(n_hunter_TurnTo, e, 0, 0x80);
    } else if (!hu_call(n_hunter_Near, e, 0x50, 0)) {
        GV(u16, e + 0x1C6) += 0x100;
    } else {
        hu_call(n_hunter_Wander, e, 1, 0x38);
    }
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x1A, 0);
    EN_STEP(e) = 1;
}

/* 80101DA0 (e): rush (156 a frame): steer round walls; when it sees the
 * player turn to them and leap in from far (6000, now and then) or mid
 * range (4500..6000, in its cone), or pounce; unseen, go round to where
 * it can. A grab nearby (2500) makes it slash. */
void n_hunter_RushStep(void) {
    FRAME(0x18);
    u32 e = A0;
    if ((u32)EN_DIST(e) < 0x1F41 && HW_MODE == 2) HW_MODE = 1;
    if (hu_call(n_hunter_WallToPlayer, e, 0, 0)) {
        hu_call(n_hunter_Wander, e, 1, 0xA1);
        gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x9C, 0);
    } else if (EN_FLAGS(e) & EF_SEES) {
        hu_call(n_hunter_TurnTo, e, 0, 0x80);
        if ((u32)EN_DIST(e) >= 0x9C5) {
            gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x9C, 0);
            u32 d = (u32)EN_DIST(e);
            if (d >= 0x1770) {
                if (hu_call(n_hunter_Cooldown, e, 0, 0)) {
                    hu_anim(e, 6, 0x10);
                    HW_MODE = 3;
                    return;
                }
            } else if (d >= 0x1194 && hu_call(n_hunter_PlayerInCone, e, 0xA, 0)) {
                HW_MODE = 3;
                hu_anim(e, 6, 0x40);
                return;
            }
        }
        if (hu_call(n_hunter_TryPounce, e, 0, 0)) return;
    } else {
        u32 st = hu_call(n_hunter_Status, e, 0, 0);
        if (st == 0) {
            HW_MODE = 4;
            hu_anim(e, 6, 0x40);
        } else if ((st & 5) == 1) {
            if (!hu_call(n_hunter_TurnTo, e, 0, 0x80)) gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), 0x64, 0);
        } else {
            hu_call(n_hunter_TurnTo, e, (st & 4) ? 3 : 0, 0x80);
            hu_call(n_hunter_Advance, EN_ROT(e), EN_VEL(e), (u32)-0x190);
        }
    }
    if ((u32)EN_DIST(e) < 0x9C4 && ep_grabLock2 != 0) {
        HW_MODE = 6;
        hu_anim(e, 6, 0x80);
    }
}

/* Rise for 800 / frames up, until under 101 a frame (step 1). */
static void hu_rise(u32 e) {
    s32 q = mips_div(0x320, HW_UP);
    HW_TIME = (s16)q;
    hu_call(n_hunter_Lift, e, (u32)q, 0);
    if (HW_TIME < 0x65) EN_STEP(e) += 1;
    else HW_UP += 1;
}

/* Fall (step 2): 800 / frames up a frame, grabbing the player near the
 * floor; then back to prowling. 1 when it has landed. */
static int hu_fall(u32 e, int lunge) {
    if (lunge) hu_call(n_hunter_Lunge, e, 0, 0);
    s32 q = mips_div(0x320, HW_UP);
    HW_TIME = (s16)q;
    hu_call(n_hunter_Lift, e, 0u - (u32)q, 0);
    if (HW_UP >= 2) HW_UP -= 1;
    return hu_call(n_hunter_Land, e, 0, 0) == 0;
}

/* 80101FCC (e): the leap at the player (300 a frame): a cry, up, down
 * grabbing the player, then prowl. */
void n_hunter_LeapStep(void) {
    FRAME(0x20);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if (step == 0) {
        GV(u16, e + 0x1C6) = (GV(u16, g_player + 0x46) + 0x800) & 0xFFF;
        gcall(FN_EN_ANIM_SOUND, e, 0xA, 0, 0);
        HW_UP = 1;
        HW_TOUGH = 0;
        EN_STEP(e) += 1;
        step = 1;
    }
    if (step == 1) {
        hu_call(n_hunter_Advance, EN_ROT(e), EN_VEL(e), (u32)-0x12C);
        hu_rise(e);
    } else if (step == 2) {
        hu_call(n_hunter_Advance, EN_ROT(e), EN_VEL(e), (u32)-0x12C);
        if (hu_fall(e, 1)) {
            HW_MODE = 2;
            hu_anim(e, 2, 0x80);
        }
    }
}

/* 8010217C (e): the long leap (500..580 a frame) over the player; in
 * stage 16 room 9 the first landing raises the alarm (event 0x45). */
void n_hunter_LeapFarStep(void) {
    FRAME(0x20);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if (step == 0) {
        gcall(FN_EN_ANIM_SOUND, e, 0xA, 0, 0);
        GV(u16, e + 0x1C6) = (GV(u16, g_player + 0x46) + 0x800) & 0xFFF;
        HW_UP = 1;
        s32 r = (s32)(en_rand() * (u32)g_hunterSeed0);
        HW_DIST = (s16)((r % 5 + 0x19) * 20);
        EN_STEP(e) += 1;
        step = 1;
    }
    if (step == 1) {
        hu_call(n_hunter_Advance, EN_ROT(e), EN_VEL(e), (u32)(s32)HW_DIST);
        hu_rise(e);
    } else if (step == 2) {
        hu_call(n_hunter_Advance, EN_ROT(e), EN_VEL(e), (u32)(s32)HW_DIST);
        if (!hu_fall(e, 0)) return;
        if (!GCALL2(FN_FLAG_TEST, ep_eventFlags, 0x45) && g_hunterAlarm == 0 && g_stage == 0x10 && g_room == 9) {
            GCALL2(FN_FLAG_SET, ep_eventFlags, 0x45);
            gcall(FN_ALARM, 0xFF, 0xFF, 0x16, 9);
        }
        g_hunterAlarm = 1;
        HW_MODE = 2;
        hu_anim(e, 2, 0x20);
    }
}

/* 80102404 (e): wait (20 frames; none in game mode 1), then sidestep, or
 * when walled in leap (mode 3), else wait 30..59 more. */
void n_hunter_WaitStep(void) {
    FRAME(0x20);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if (step == 0) {
        HW_DIST = g_gameMode == 1 ? 0 : 0x14;
        EN_STEP(e) += 1;
    } else if (step != 1) {
        return;
    }
    HW_DIST -= 1;
    if (HW_DIST > 0) return;
    if (hu_call(n_hunter_PickSide, e, 0, 0)) {
        HW_MODE = 3;
        hu_anim(e, 6, 0x80);
        return;
    }
    HW_TIME = (s16)((s32)en_rand() % 30 + 0x1E);
}

/* 80102528 (e): sidestep (200 a frame), turned 0x200 to the side, until
 * a wall is near, the player is lost from sight or the time is up. */
void n_hunter_SidestepStep(void) {
    FRAME(0x18);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if (step == 0) {
        if (HW_SIDE == 1) GV(u16, e + 0x1C6) += 0x200;
        else GV(u16, e + 0x1C6) -= 0x200;
        EN_STEP(e) += 1;
        step = 1;
    }
    if (step == 1) {
        int stop;
        if ((s32)hu_call(n_hunter_WallAhead, e, 1, 0) >= 0) stop = 1;
        else if (!(EN_FLAGS(e) & EF_SEES)) stop = 1;
        else { HW_TIME -= 1; stop = HW_TIME == -1; }
        if (stop) {
            HW_SIDE = 0;
            HW_TIME = 0;
            HW_MODE = 1;
        }
    }
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0xC8, 0);
}

/* 80102634 (e): leap from a sidestep (leap length 500..580). */
void n_hunter_LeapStart(void) {
    FRAME(0x20);
    u32 e = A0;
    if (EN_STEP(e) == 0) {
        gcall(FN_EN_ANIM_SOUND, e, 0xA, 0, 0);
        HW_UP = 1;
        s32 r = (s32)(en_rand() * (u32)g_hunterSeed0);
        HW_DIST = (s16)((r % 5 + 0x19) * 20);
        EN_STEP(e) += 1;
    }
    A0 = e;
    n_hunter_LeapStep();
}

/* 801033E0 (e): action 0, idle, by mode. */
void n_hunter_IdleAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!hu_anim_done(e)) return;
    u32 m = HW_MODE;
    if (!(m < 0x11)) return;
    LOOP_POLL(0x80103438);
    switch (m) {
    case 0: GCALL1(FN_HU_IDLE0, e); return;
    case 1: case 2: GCALL1(FN_HU_IDLE1, e); return;
    case 5: A0 = e; n_hunter_WaitStep(); return;
    case 7: GCALL1(FN_HU_MODE7, e); return;
    case 10: GCALL1(0x801027ECu, e); return;
    case 12: GCALL1(0x80102AC4u, e); return;
    case 13: GCALL1(0x80102C54u, e); return;
    case 15: GCALL1(0x80102FB0u, e); return;
    }
}

/* 801034C8 (e): action 1, walk: footsteps, by mode. */
void n_hunter_WalkAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!hu_anim_done(e)) return;
    if (EN_STEP(e) == 0) HW_UP = 0;
    hu_call(n_hunter_StepSound, e, 1, 8);
    u32 m = HW_MODE;
    if (!(m < 0x10)) return;
    LOOP_POLL(0x80103558);
    switch (m) {
    case 0: GCALL1(FN_HU_WALK0, e); return;
    case 1: case 2: A0 = e; n_hunter_ProwlStep(); return;
    case 11: GCALL1(0x801028E8u, e); return;
    case 15: GCALL1(0x80102FD4u, e); return;
    }
}

/* 801035A8 (e): action 2, run: footsteps, by mode. */
void n_hunter_RunAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!hu_anim_done(e)) return;
    hu_call(n_hunter_StepSound, e, 2, 9);
    u32 m = (u32)HW_MODE - 1;
    if (!(m < 0x12)) return;
    LOOP_POLL(0x80103618);
    switch (m + 1) {
    case 1: case 2: A0 = e; n_hunter_RushStep(); return;
    case 6: A0 = e; n_hunter_SidestepStep(); return;
    case 11: GCALL1(0x801029ECu, e); return;
    case 18: GCALL1(0x80103148u, e); return;
    }
}

/* 8010381C (e): action 5, the slash: speed 1 at frame 8, a grab within
 * 2600 from then on; at the animation's event back to idle (mode 7 when
 * the game is over). */
void n_hunter_SlashAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!hu_anim_done(e)) return;
    s16 step = EN_STEP(e);
    if (step == 0) {
        if (HW_FRAME == 8 && HW_PREV_FRAME != 8) gcall(FN_EN_SET_SPEED, e, 1, 0, 0);
        if (HW_FRAME >= 8 && (u32)EN_DIST(e) < 0xA28 && (EN_FLAGS(e) & EF_SEES)) {
            hu_call(n_hunter_GrabStep, e, 2, 0);
            EN_STEP(e) += 1;
        }
    } else if (step != 1) {
        return;
    }
    if (!hu_call(n_hunter_AnimEvent, e, 1, 1)) return;
    if (g_gameMode == 3) HW_MODE = 7;
    hu_anim(e, 0, 0x80);
}

/* 80103954 (e): action 6, leaping, by mode. */
void n_hunter_LeapAct(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!hu_anim_done(e)) return;
    u32 m = (u32)HW_MODE - 3;
    if (!(m < 0x10)) return;
    LOOP_POLL(0x801039B0);
    switch (m + 3) {
    case 3: A0 = e; n_hunter_LeapStep(); return;
    case 4: A0 = e; n_hunter_LeapFarStep(); return;
    case 6: A0 = e; n_hunter_LeapStart(); return;
    case 13: GCALL1(0x80102D24u, e); return;
    case 15: GCALL1(0x801030D8u, e); return;
    case 17: GCALL1(0x80102DC4u, e); return;
    case 18: GCALL1(0x80103228u, e); return;
    }
}

/* 80103B24 (e): action 9, getting up (sliding back 20 a frame along the
 * player's facing): then run (mode 1). */
void n_hunter_RiseAct(void) {
    FRAME(0x18);
    u32 e = A0;
    gcall(F_ROT_STEP, PLAYER_ROT_ADDR, EN_VEL(e), (u32)-0x14, 0);
    s16 step = EN_STEP(e);
    if (step == 0) {
        u32 an = EN_ANIMSTATE(e);
        if (GV(u16, an + 0x14) < GV(u16, an + 0x16)) EN_STEP(e) = (s16)(step + 1);
    } else if (step == 1) {
        if (hu_call(n_hunter_AnimEvent, e, 1, 1)) EN_STEP(e) += 1;
    } else if (step == 2) {
        HW_MODE = 1;
        hu_anim(e, 2, 0x80);
    }
}

/* 801044C8 (e): the shots it took: lose their damage (none while leaping
 * from a sidestep, dying, retreating or before it is hurt); count hits
 * towards backing off (handguns 1, others 2: at 3 leap away or
 * sidestep); die at 0 HP; else flinch by where it was hit, its height
 * and which way it faces. */
void n_hunter_OnHit(void) {
    FRAME(0x28);
    u32 e = A0;
    EN_FLAGS(e) &= ~(u32)EF_SHOT;
    u8 m = HW_MODE;
    u32 dmg = 0;
    u32 part_hit = 0;
    if (m == 9 || m == 0x12 || (m != 6 && HW_TOUGH != 0)) {
        EN_NHITS(e) = -1;
        return;
    }
    if (m != 6) {
        s16 n = EN_NHITS(e);
        if (n >= 0) {
            for (s32 i = 0;; ) {
                dmg += GV(u16, e + 0x330 + (u32)i * 2);
                i++;
                if (n < i) break;
                LOOP_POLL(0x80104560);
            }
        }
        if ((s32)EN_HP(e) - (s32)(s16)dmg > 0) {
            if (GV(u32, GV(u32, e + 0x8BC) + 0xB0) != 0) {
                HW_HITS = 0;
            } else {
                if (GV(u16, g_currentWeapon) < 4) HW_HITS += 1;
                else HW_HITS += 2;
                if (HW_HIT_TIME != 0 && HW_HITS >= 3) {
                    HW_HITS = 0;
                    if ((s32)hu_call(n_hunter_WallAhead, e, 6, 0) == -1) {
                        HW_MODE = 6;
                        hu_anim(e, 6, 0x40);
                        EN_NHITS(e) = -1;
                        HW_HIT_TIME = 0;
                        return;
                    }
                    if (!hu_call(n_hunter_PickSide, e, 0, 0)) {
                        HW_TIME = (s16)((s32)en_rand() % 30 + 0x1E);
                        EN_NHITS(e) = -1;
                        HW_HIT_TIME = 0;
                        return;
                    }
                }
            }
        }
    }
    HW_HIT_TIME = 100;
    GV(u16, e + 0x31E) = (u16)(GV(u16, e + 0x31E) - dmg);
    if (EN_NHITS(e) > 0) {
        /* the loop's count is reset inside it: one pass */
        u8 p = GV(u8, e + 0x326);
        if (p == 0 || p == 4) part_hit = 1;
        EN_NHITS(e) = -1;
    }
    if (EN_HP(e) <= 0) {
        HW_KILL_WEAPON = GV(u16, g_currentWeapon);
        gcall(FN_EN_KILLED, e, (u32)(s32)EN_ROOMID(e), 0, 0);
    }
    EN_NHITS(e) = -1;
    GCALL1(FN_EN_HIT_PRE, e);
    m = HW_MODE;
    if (m == 0xF) {
        if (EN_HP(e) <= 0) {
            GV(u16, GV(u32, e + 0x8BC) + 0xD6) = 0;
            HW_MODE = 9;
            hu_anim(e, 0xE, 0x40);
        } else {
            HW_MODE = 3;
            HW_TOUGH = 1;
            hu_anim(e, 6, 0x40);
        }
        return;
    }
    if (EN_HP(e) <= 0) {
        HW_MODE = 9;
    } else {
        if (m == 6) return;
        HW_MODE = 1;
        u8 a = HW_ANIM;
        if (a == 9 && GV(u16, EN_ANIMSTATE(e) + 0x16) == 0xE2) return;
        if (a == 0xA && GV(u16, EN_ANIMSTATE(e) + 0x16) == 0xE8) return;
    }
    if (GV(u32, GV(u32, e + 0x8BC) + 0xB0) != 0) {
        HW_AIRBORNE = 1;
        if ((u16)(GV(u16, e + 0x85E) - 0xE) < 2) return;
        u32 nf = hu_call(n_hunter_NotFacing, e, 0, 0);
        gcall(FN_EN_SET_SPEED, e, 4, 0, 0);
        hu_anim(e, nf ? 0xE : 0xF, 8);
        return;
    }
    HW_AIRBORNE = 0;
    s16 anim = EN_ANIM(e);
    if (anim == 0xF || (anim == 0xD && g_hunterFrame0 < 7)) {
        gcall(FN_EN_SET_SPEED, e, 4, 0, 0);
        hu_anim(e, 0x10, 0x40);
        return;
    }
    if (hu_call(n_hunter_NotFacing, e, 0, 0)) {
        if (EN_HP(e) <= 0) {
            gcall(FN_EN_ANIM_SOUND, e, 6, 5, 0);
            hu_anim(e, 0xE, 0x40);
        } else {
            gcall(FN_EN_SET_SPEED, e, 4, 0, 0);
            hu_anim(e, 0xA, 0x40);
        }
        return;
    }
    if (!((u32)(s32)GV(s16, g_currentWeapon) < 4)) {
        if (EN_HP(e) <= 0) {
            gcall(FN_EN_ANIM_SOUND, e, 6, 5, 0);
            hu_anim(e, 0xE, 0x40);
            return;
        }
        if (part_hit) {
            gcall(FN_EN_SET_SPEED, e, 4, 0, 0);
            hu_anim(e, 0xF, 0x40);
            return;
        }
    }
    if (EN_HP(e) <= 0) {
        gcall(FN_EN_ANIM_SOUND, e, 6, 5, 0);
        hu_anim(e, 0xE, 0x40);
        return;
    }
    gcall(FN_EN_SET_SPEED, e, 4, 0, 0);
    hu_anim(e, 9, 0x40);
    if (HW_ANIM == 9 && HW_FRAME < 8) GV(u16, e + 0x862) = 1;
    if (HW_ANIM == 0xA && HW_FRAME < 6) GV(u16, e + 0x862) = 1;
}

/* 80104CB8 (e): stage 4 room 1: running (animation 2) to the other side
 * of the line z = 0 from the player: retreat (mode 18). */
void n_hunter_Room1Retreat(void) {
    u32 e = A0;
    if ((s32)((u32)EN_Z(e) * GV(u32, PLAYER_POS_ADDR + 8)) >= 0) return;
    if (EN_ANIM(e) != 2) return;
    if (HW_MODE == 0x12) return;
    HW_MODE = 0x12;
    EN_STEP(e) = 0;
    HW_RETREAT = 0;
}

/* 80104F94 (e): stage 4: room 1's check. */
void n_hunter_Stage4Check(void) {
    FRAME(0x18);
    if (g_room == 1) n_hunter_Room1Retreat();
}

/* 80105044 (e): the per-stage checks (stages 1, 4, 16); else none
 * (done). */
void n_hunter_StageCheck(void) {
    FRAME(0x18);
    u32 e = A0;
    switch (g_stage) {
    case 1: GCALL1(0x80104F64u, e); return;
    case 4: A0 = e; n_hunter_Stage4Check(); return;
    case 0x10: GCALL1(0x80104FC4u, e); return;
    default: HW_ROOM_DONE = 1; return;
    }
}

/* 801050E4 (e): state 1, active: the action's handler. */
void n_hunter_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, HUNTER_ACTIONS + (u32)((s32)EN_ACTION(e) << 2)), e);
}

/* 8010537C (e): every frame. */
void n_hunter_Update(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 w = HUNTER_WORK_BASE + (u32)EN_SLOT(e) * HUNTER_WORK_SIZE;
    HW = w;
    GV(u16, w + 0x20) += 1;
    if (GV(s8, w + 0) == 0) {
        GV(u8, w + 0) = 1;
        GV(s16, w + 0x10) = (s16)(EN_HP(e) >> 1);
        GCALL1(FN_HU_SETUP, e);
    }
    u32 an = EN_ANIMSTATE(e);
    if (GV(u16, an + 0x16) < GV(u16, an + 0x14)) {
        HW_FRAME = 0;
    } else {
        HW_PREV_FRAME = HW_FRAME;
        if ((s32)HW_ANIM_FRAME != (s32)GV(u16, EN_ANIMSTATE(e) + 0x14)) HW_FRAME += 1;
        HW_ANIM_FRAME = (s16)GV(u16, EN_ANIMSTATE(e) + 0x14);
    }
    if (HW_ROOM_DONE == 0) { A0 = e; n_hunter_StageCheck(); }
    if (g_gameMode == 3) GCALL1(FN_HU_NOP, e);
    GCALL1(GV(u32, HUNTER_STATES + (u32)((s32)EN_STATE(e) << 2)), e);
    if (EN_FLAGS(e) & EF_SHOT) {
        gcall(FN_EN_SET_SPEED, e, 0xC, 0, 0);
        A0 = e;
        n_hunter_OnHit();
        GCALL1(FN_EN_HIT_POST, e);
    }
    HW_ANIM = GV(u8, e + 0x85E);
    if (HW_HIT_TIME != 0) HW_HIT_TIME -= 1;
    for (u32 k = 0; k < 16; k += 4) GV(u32, HW_POS + k) = GV(u32, e + 0x198 + k);
    u32 buf = g_bufIndex;
    if (g_hunterLastBuf != buf) {
        g_hunterLastBuf = buf;
        if (g_hunterPounceWait != 0) g_hunterPounceWait -= 1;
    }
}
