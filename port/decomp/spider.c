/* The giant spider (enemy program ENEMPROG 55, loaded in enemy slot B at
 * 0x80100000). Identified by what it does and how it looks in the area
 * scenario that loads it (area12: stage 16 room 2, two of them): it
 * crawls on the floor, walls and ceiling with a swaying gait, spits and
 * strikes at the player, and its legs (body parts) can be shot off.
 *
 * spider_Update (0x8010569C) runs every frame for each one: it picks the
 * work block, runs the state's handler (0x80105C7C; state 1,
 * spider_StateActive, runs the action's pair of handlers from 0x80105BAC
 * (every frame) and 0x80105C14 (while the phase is negative), like the
 * zombie), takes the shots (spider_OnHit), keeps its shadow at its height
 * and stops it at the room's edges (spider_MoveStep).
 *
 * Decompiled here: actions 16 (crawl at the player), 18 (landing) and 22
 * (death), the hits, the movement checks.
 *
 * Each one has a 0x4C-byte work block (0x80105C90 + slot * 0x4C; the
 * current one's address is kept at 0x80105EA4, see SW_*).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"

void n_spider_EdgeCheck(void);
void n_spider_MoveCheck(void);
void n_spider_MoveStep(void);
void n_spider_Attack(void);
void n_spider_Sway(void);
void n_spider_Wake(void);
void n_spider_CrossedWall(void);
void n_spider_TakeHits(void);
void n_spider_OnHit(void);
void n_spider_PlayerInCone(void);
void n_spider_CrawlPre(void);
void n_spider_CrawlStep(void);
void n_spider_LandPre(void);
void n_spider_LandStep(void);
void n_spider_DiePre(void);
void n_spider_DieStep(void);
void n_spider_StateActive(void);
void n_spider_StateBoth(void);
void n_spider_ShadowHeight(void);
void n_spider_Update(void);

/* ---- the work block ---- */
#define SW               GV(u32, 0x80105EA4)
#define SW_FLOOR         GV(s32, SW + 0x04)   /* height limit on walls (+0x258) */
#define SW_IDLE          GV(s32, SW + 0x0C)   /* frames since it last moved on */
#define SW_TIMER         GV(s32, SW + 0x14)
#define SW_FLAGS         GV(u16, SW + 0x24)   /* 0x40 stopped, 0x200 roused, 0x400, 0x1000 wall in the way, 0x2000 strike */
#define SW_GAIT          GV(u16, SW + 0x2A)   /* sway: 0x8000 the side, low bits the count */
#define SPIDER_WORK_BASE 0x80105C90u
#define SPIDER_WORK_SIZE 0x4C

#define SPIDER_ACT_PRE   0x80105BACu
#define SPIDER_ACT_STEP  0x80105C14u
#define SPIDER_STATES    0x80105C7Cu
/* the move being checked (spider_MoveCheck) */
#define MC_FROM          0x80105EB0u   /* position (16 bytes) */
#define MC_TO            0x80105EC0u   /* position + step */
#define MC_STEP_PTR      GV(u32, 0x80105ED0)
#define MC_HITS          GV(s32, 0x80105ED4)
#define FN_S_EDGE_HIT    0x80100078u   /* (edge, from, to): stop at the edge, counts MC_HITS */
#define FN_S_STOPPED     0x8010053Cu   /* (e) */
#define FN_S_LEGS_LEFT   0x80100030u   /* (e) */
#define FN_S_AROUND_WALL 0x80101F74u   /* (e) */
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_PART_BLOW     0x8009FE14u   /* (slot, k) */
#define FN_PART_SEVER    0x8009FE24u   /* (slot, a, b): 0 when cut off */
#define FN_EN_DIST       0x8007F4E8u   /* (e): +0x868 */
#define HIT_SPARK_TEMPLATE 0x80065B90u  /* 0x20 bytes */

static u32 spider_call(void (*f)(void), u32 a0, u32 a1) {
    A0 = a0; A1 = a1;
    f();
    return V0;
}

/* ------------------------------------------------------------------ */
/* Moving */

/* 8010020C: does the move MC_FROM -> MC_TO run into one of the room's
 * area edges (0x800B60D0, those within 60000 in z)? Each one it touches
 * from the inside goes to 0x80100078, which stops the move there and
 * counts it (MC_HITS, at most two). */
void n_spider_EdgeCheck(void) {
    FRAME(0x30);
    MC_HITS = 0;
    u32 n = GV(u8, EP_EDGES_N);
    u32 w = GV(u32, EP_EDGES_N + 4);
    for (u32 i = 0; i < n; i++, w += 0x14) {
        s32 px = GV(s32, MC_FROM), pz = GV(s32, MC_FROM + 8);
        s32 x0 = GV(s32, w), z0 = GV(s32, w + 4), x1 = GV(s32, w + 8), z1 = GV(s32, w + 12);
        s32 qz = GV(s32, MC_TO + 8);
        s32 dz = (s32)((u32)qz - (u32)z0);
        s32 adz = dz < 0 ? (s32)(0u - (u32)dz) : dz;
        if (0xEA60 < adz) continue;
        s32 qx = GV(s32, MC_TO);
        u32 mz = (u32)qz - (u32)pz, mx = (u32)qx - (u32)px;
        /* the edge's ends either side of the move, the move's ends either side of the edge */
        s32 a = (s32)(mz * ((u32)x0 - (u32)px) - mx * ((u32)z0 - (u32)pz));
        s32 b = (s32)(mz * ((u32)x1 - (u32)px) - mx * ((u32)z1 - (u32)pz));
        u32 ez = (u32)z1 - (u32)z0, ex = (u32)x1 - (u32)x0;
        s32 c = (s32)(ez * ((u32)qx - (u32)x0) - ex * (u32)dz);
        if (c < 0) continue;
        s32 d = (s32)(((u32)px - (u32)x0) * ez - ex * ((u32)pz - (u32)z0));
        int straddle = (a < 0) != (b < 0) || a == 0 || b == 0;
        if (straddle && (d <= 0 || c == 0) && (u32)a + (u32)b + (u32)c + (u32)d != 0)
            GCALL3(FN_S_EDGE_HIT, w, MC_FROM, MC_TO);
        if (MC_HITS >= 2) return;
    }
}

/* 8010041C (pos, step): check the move pos -> pos + step against the
 * edges (spider_EdgeCheck); returns how many it ran into. */
void n_spider_MoveCheck(void) {
    FRAME(0x18);
    u32 pos = A0, step = A1;
    for (u32 k = 0; k < 16; k += 4) GV(u32, MC_FROM + k) = GV(u32, pos + k);
    GV(u32, MC_TO + 0) = GV(u32, pos + 0) + GV(u32, step + 0);
    GV(u32, MC_TO + 4) = GV(u32, pos + 4) + GV(u32, step + 4);
    MC_STEP_PTR = step;
    GV(u32, MC_TO + 8) = GV(u32, pos + 8) + GV(u32, step + 8);
    n_spider_EdgeCheck();
    V0 = (u32)MC_HITS;
}

/* 801004AC (e): at an edge: stop (no movement, SW_FLAGS | 0x40) and go
 * to state 4. */
void n_spider_MoveStep(void) {
    FRAME(0x18);
    u32 e = A0;
    if ((u16)spider_call(n_spider_MoveCheck, EN_POS(e), EN_VEL(e)) == 0) return;
    u32 w = SW;
    EN_VZ(e) = 0;
    EN_VY(e) = 0;
    EN_VX(e) = 0;
    GV(u16, w + 0x24) |= 0x40;
    GCALL1(FN_S_STOPPED, e);
    EN_STATE(e) = 4;
}

/* 80100684 (e): seeing the player in its cone (+-11*0x28): strike
 * (action 0x13) within 1000 if SW_FLAGS & 0x2000, else (not 0x20) spit
 * (action 0x12) within 6000. */
void n_spider_Attack(void) {
    FRAME(0x18);
    u32 e = A0;
    if (!(EN_FLAGS(e) & EF_SEES)) return;
    if (spider_call(n_spider_PlayerInCone, e, 0x28) == 0) return;
    u16 f = SW_FLAGS;
    if (f & 0x2000) {
        if ((s32)GCALL1(FN_EN_DIST, e) >= 0x3E9) return;
        GCALL3(FN_EN_SET_ACTION, e, 0x13, EN_ANIM(e) == 3 ? 4 : 6);
        return;
    }
    if (f & 0x20) return;
    if ((s32)GCALL1(FN_EN_DIST, e) >= 0x1771) return;
    if (EN_ANIM(e) == 3) GCALL3(FN_EN_SET_ACTION, e, 0x12, 3);
    else GCALL4(FN_EN_SET_ACTION_B, e, 0x12, 2, 2);
}

/* The sway's sideways step (the result is unused, only the call is
 * kept): rot (8 bytes) at sp+0x10, out at sp+0x18. */
static void spider_SwayStep(u32 e, u32 sp, s32 dyaw, s32 drz, s32 speed) {
    u32 rot = sp + 0x10;
    MEM_W16(rot + 0, GV(u16, e + 0x1C4));
    MEM_W16(rot + 2, (u32)GV(u16, e + 0x1C6) + (u32)dyaw);
    MEM_W16(rot + 4, (u32)GV(u16, e + 0x1C8) + (u32)drz);
    gcall(FN_ROT_VELOCITY, rot, sp + 0x18, (u32)speed, 0);
}

/* 80100798 (e): the swaying gait: a step to one side then the other
 * (SW_GAIT, 0x10 frames on the floor, 0x40 on walls); on a wall keep it
 * between -0x258 and SW_FLOOR + 0x258 high. */
void n_spider_Sway(void) {
    FRAME(0x38);
    u32 e = A0;
    u32 w = SW;
    s32 side = (GV(s16, w + 0x2A) & 0x8000) ? 0x400 : -0x400;
    s32 lean = 0x40 - (s32)(GV(u16, w + 0x2A) & 0x7FFF);
    if (EN_Y(e) == 0 || (EN_FLAGS(e) & 0x200)) {
        if (EN_FLAGS(e) & 0x200) spider_SwayStep(e, sp, side, 0, lean);
        else spider_SwayStep(e, sp, side, 0, 0x40);
    } else {
        spider_SwayStep(e, sp, 0, side, lean);
        s32 y = (s32)((u32)EN_Y(e) + GV(u32, e + 0x880));
        if (!(y < -0x257)) {
            EN_Y(e) = -0x258;
            GV(u32, e + 0x880) = 0;
        } else {
            s32 lim = (s32)((u32)GV(s32, SW + 4) + 0x258);
            if (y < lim) {
                EN_Y(e) = lim;
                GV(u32, e + 0x880) = 0;
            }
        }
    }
    u32 w2 = SW;
    u32 g = (u32)GV(u16, w2 + 0x2A) + 1;
    int more = (s32)(g & 0x7FFF) < (EN_Y(e) == 0 ? 0x11 : 0x41);
    GV(u16, w2 + 0x2A) = (u16)g;
    if (!more) GV(u16, w2 + 0x2A) = (u16)((g ^ 0x8000) & 0x8000);
}

/* 80100C80 (e): after 600 frames of the same, with the player 3000 away
 * or more: rouse (SW_FLAGS | 0x200) at the type's speed. */
void n_spider_Wake(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 w = SW;
    s32 n = GV(s32, w + 0xC) + 1;
    if (n < 0x258) {
        GV(s32, w + 0xC) = n;
        return;
    }
    if ((s32)GCALL1(FN_EN_DIST, e) < 0xBB9) return;
    w = SW;
    GV(u32, w + 0xC) = 0;
    GV(u16, w + 0x24) |= 0x200;
    EN_SPEED(e) = (s16)GCALL2(FN_EN_BASE_SPEED, e, 0);
}

/* 80100FFC (from, to): the first wall the path crosses from its front, or
 * -1 (zombie_CrossedWall). */
void n_spider_CrossedWall(void) {
    FRAME(0x40);
    V0 = (u32)en_crossed_wall(A0, A1);
}

/* 80101918 (e): take this frame's hits: shots at a leg (body parts of
 * kind 2) blow it off on a big hit or at random when weak, legs of kinds
 * 3 and 4 are cut off (part kind -1); returns 0 if it died (action 0x17
 * on the floor, 0x18 elsewhere, state 3). */
void n_spider_TakeHits(void) {
    FRAME(0x28);
    u32 e = A0;
    for (s32 i = 0; i <= EN_NHITS(e); i++) {
        u32 part = e + (u32)EN_HIT_PART(e, i) * 0x5C;
        s16 kind = GV(s16, part + 0x39A);
        s16 dmg = EN_HIT_DMG(e, i);
        switch (kind) {
        case 2: {
            s16 hp = EN_HP(e);
            int blow = 1;
            if ((s32)hp - (s32)dmg > 0 && dmg < 0x64) {
                if (hp >= 0xC8) blow = 0;
                else if ((en_rand() & 0xFF) < 0xB2) blow = 0;
            }
            if (blow) {
                GCALL2(FN_PART_BLOW, EN_SLOT(e), 5);
                EN_HP(e) = -1;
            }
            break;
        }
        case 3:
        case 4: {
            int cut = 1;
            if (dmg < 0x64) {
                if ((s32)GCALL1(FN_S_LEGS_LEFT, e) < 2) cut = 0;
                else if ((en_rand() & 0xFF) < 0x99) cut = 0;
            }
            if (cut) {
                u32 r = kind == 3 ? GCALL3(FN_PART_SEVER, EN_SLOT(e), 6, 0xE)
                                  : GCALL3(FN_PART_SEVER, EN_SLOT(e), 0xA, 0x12);
                if (r == 0) GV(s16, part + 0x39A) = -1;
            }
            break;
        }
        }
        EN_HP(e) = (s16)(GV(u16, e + 0x31E) - GV(u16, e + 0x330 + (u32)i * 2));
        if (EN_HP(e) <= 0) {
            EN_STATE(e) = 3;
            if (EN_Y(e) == 0) GCALL4(FN_EN_SET_ACTION_B, e, 0x17, 8, 2);
            else GCALL4(FN_EN_SET_ACTION_B, e, 0x18, 0, 2);
            V0 = 0;
            return;
        }
    }
    V0 = 1;
}

/* 80101B48 (e): shot: take the hits (alive: SW_FLAGS | 0x40), a spark at
 * the first part hit (template 0x80065B90 at the part's position). */
void n_spider_OnHit(void) {
    FRAME(0x70);
    u32 e = A0;
    EN_FLAGS(e) &= ~0x10u;
    if (spider_call(n_spider_TakeHits, e, 0) != 0) SW_FLAGS |= 0x40;
    u32 p = EN_HIT_PART(e, 0);
    u32 s = sp + 0x18;
    for (u32 k = 0; k < 0x20; k += 4) GS(u32, s + 4 + k) = GV(u32, HIT_SPARK_TEMPLATE + k);
    u32 obj = EN_BODY_OBJ(e, p);
    GS(u32, s + 0x18) = GV(u32, obj + 0x38);
    GS(u32, s + 0x1C) = GV(u32, EN_BODY_OBJ(e, p) + 0x3C);
    GS(u32, s + 0x20) = GV(u32, EN_BODY_OBJ(e, p) + 0x40);
    gcall5(sp, FN_EN_DRAW, s, 0, 0, 0, 4);
    EN_NHITS(e) = -1;
    GCALL1(FN_EN_HIT_PRE, e);
}

/* 80101D3C (e, spread): is the player in its view cone (turned round
 * when it is upside down)? (licker_PlayerInCone) */
void n_spider_PlayerInCone(void) {
    FRAME(0x50);
    V0 = en_player_in_cone(sp, A0, (s32)A1);
}

/* ------------------------------------------------------------------ */
/* Actions */

/* 80104328 (e): action 16 every frame: its sound now and then, the wake
 * timer, and once roused (SW_FLAGS 0x200) in steps 1-3 rear up (action
 * 0xD). */
void n_spider_CrawlPre(void) {
    FRAME(0x18);
    u32 e = A0;
    if ((ep_frameCount & 0xF) == 0) GCALL2(FN_EN_SET_SPEED, e, 2);
    A0 = e;
    n_spider_Wake();
    s16 step = EN_STEP(e);
    if (step >= 4 || step <= 0) return;
    u16 f = SW_FLAGS;
    if (!(f & 0x200)) return;
    SW_FLAGS = f & 0xFBFF;
    GCALL3(FN_EN_SET_ACTION, e, 0xD, (s32)EN_ANIM(e));
}

/* 801043B4 (e): action 16, crawling at the player: pick strike or not
 * (SW_FLAGS 0x2000) and set off (straight, or after anim 2); then turn to
 * the player (or round the wall between) with the swaying gait, and
 * attack when it can. */
void n_spider_CrawlStep(void) {
    FRAME(0x18);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0: {
        s16 step;
        if ((en_rand() & 0xFF) < 0x7F) {
            if ((en_rand() & 0xFF) < 0x4C) SW_FLAGS |= 0x2000;
            else SW_FLAGS &= 0xDFFF;
            step = 1;
        } else {
            if ((en_rand() & 0xFF) < 0x4C) SW_FLAGS |= 0x2000;
            else SW_FLAGS &= 0xDFFF;
            GCALL2(FN_EN_SET_ANIM, e, 2);
            step = 2;
        }
        EN_STEP(e) = step;
        EN_SPEED(e) = (s16)GCALL2(FN_EN_BASE_SPEED, e, 1);
        return;
    }
    case 1:
        if ((s32)spider_call(n_spider_CrossedWall, EN_POS(e), PLAYER_POS_ADDR) >= 0) {
            SW_FLAGS |= 0x1000;
            GCALL1(FN_S_AROUND_WALL, e);
        } else {
            u32 w = SW;
            if (GV(u16, w + 0x24) & 0x1000) GV(u16, w + 0x2A) = 0;
            SW_FLAGS &= 0xEFFF;
            GCALL1(FN_EN_TURN_TO_PLAYER, e);
            A0 = e;
            n_spider_Sway();
        }
        break;
    case 2:
        if (!(EN_FLAGS(e) & 8)) return;
        GCALL3(FN_EN_SET_ANIM_B, e, 3, 4);
        EN_STEP(e) = 3;
        return;
    case 3:
        if ((s32)spider_call(n_spider_CrossedWall, EN_POS(e), PLAYER_POS_ADDR) >= 0)
            GCALL1(FN_S_AROUND_WALL, e);
        else
            GCALL1(FN_EN_TURN_TO_PLAYER, e);
        break;
    default:
        return;
    }
    A0 = e;
    n_spider_Attack();
}

/* 8010463C (e): action 18 every frame: the wake timer. */
void n_spider_LandPre(void) {
    FRAME(0x18);
    n_spider_Wake();
}

/* 8010465C (e): action 18, landing: the landing animation (3), the
 * landing thud (spark mode 9, sound 3), a pause of 15 frames, then rear
 * up (action 0xD, if roused) or move on: action 0x10 on the floor, 0x14
 * upside down, else 0x15 (4 if roused). */
void n_spider_LandStep(void) {
    FRAME(0x28);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        if (EN_ANIM(e) != 2) { EN_STEP(e) = 1; return; }
        if (!(EN_FLAGS(e) & 8)) return;
        GCALL3(FN_EN_SET_ANIM_B, e, 3, 2);
        EN_STEP(e) = 1;
        return;
    case 1:
        gcall5(sp, FN_EN_DRAW, e, 0, 0, 0, 9);
        GCALL2(FN_EN_SET_SPEED, e, 3);
        SW_TIMER = 0;
        GCALL2(FN_EN_SET_ANIM, e, 1);
        EN_STEP(e) = 2;
        return;
    case 2: {
        u32 w = SW;
        s32 t = GV(s32, w + 0x14) + 1;
        GV(s32, w + 0x14) = t;
        if (t < 0xF) return;
        u16 f = GV(u16, w + 0x24);
        u32 act, anim;
        if (EN_Y(e) >= 0) {
            if (f & 0x200) { GV(u16, w + 0x24) = f & 0xFBFF; act = 0xD; anim = (u32)(s32)EN_ANIM(e); }
            else { act = 0x10; anim = 1; }
        } else if (EN_FLAGS(e) & 0x200) {
            if (f & 0x200) { GV(u16, w + 0x24) = f & 0xFBFF; act = 0xD; anim = (u32)(s32)EN_ANIM(e); }
            else { act = 0x14; anim = 1; }
        } else {
            if (f & 0x200) { GV(u16, w + 0x24) = f & 0xFBFF; act = 4; }
            else act = 0x15;
            anim = 1;
        }
        GCALL3(FN_EN_SET_ACTION, e, act, anim);
        return;
    }
    }
}

/* 80104CD4: action 22 (death), nothing every frame. */
void n_spider_DiePre(void) {
}

/* 80104CDC (e): action 22, dying: two bursts at its body (+0x16C's
 * 0x50 bytes from +0x50, at its position and 0x100 off), counted as
 * killed, its death animation; at the end lie still (+0x320 | 1). */
void n_spider_DieStep(void) {
    FRAME(0x70);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0: {
        u32 s = sp + 0x18;
        u32 src = GV(u32, e + 0x16C) + 0x50;
        for (u32 k = 0; k < 0x50; k += 4) GS(u32, s + k) = GV(u32, src + k);
        GS(u32, s + 0x18) += (u32)EN_X(e);
        GS(u32, s + 0x1C) += (u32)EN_Y(e);
        GS(u32, s + 0x20) += (u32)EN_Z(e);
        gcall5(sp, FN_EN_DRAW, s, 0, 0, 0, 5);
        GS(u32, s + 0x18) += 0x100;
        GS(u32, s + 0x20) += 0x100;
        GS(u32, s + 0x1C) += 0x100;
        gcall5(sp, FN_EN_DRAW, s, 0, 0, 0, 4);
        GCALL2(FN_EN_KILLED, e, (s32)EN_ROOMID(e));
        EN_FLAGS(e) |= 0x10006;
        EN_STEP(e) += 1;
        gcall5(sp, FN_EN_DRAW, e, 3, 0, 0, 2);
        return;
    }
    case 1:
        if (!(EN_FLAGS(e) & 8)) return;
        EN_FLAGS(e) |= 1;
        EN_STEP(e) += 1;
        return;
    }
}

/* ------------------------------------------------------------------ */
/* Per frame */

/* 80105300 (e): state 1: the action's handlers; its footsteps (spark
 * mode 2) while it moves or turns, not upside down; no movement once the
 * animation ends or in anim 0. */
void n_spider_StateActive(void) {
    FRAME(0x28);
    u32 e = A0;
    u16 yaw = GV(u16, e + 0x1C6);
    GCALL1(GV(u32, SPIDER_ACT_PRE + (u32)((s32)EN_ACTION(e) << 2)), e);
    if (EN_PHASE(e) < 0)
        GCALL1(GV(u32, SPIDER_ACT_STEP + (u32)((s32)EN_ACTION(e) << 2)), e);
    if ((EN_VX(e) != 0 || EN_VY(e) != 0 || EN_VZ(e) != 0 || EN_YAW(e) != (s16)yaw) &&
        !(EN_FLAGS(e) & 0x200))
        gcall5(sp, FN_EN_DRAW, e, 2, 0, 0, 2);
    u32 an = EN_ANIMSTATE(e);
    if (GV(u16, an + 0x14) == GV(u16, an + 0xE)) {
        EN_VZ(e) = 0;
        EN_VX(e) = 0;
    }
    if (EN_ANIM(e) == 0) {
        EN_VZ(e) = 0;
        EN_VX(e) = 0;
    }
}

/* 801054B0 (e): states 2/3: both of the action's handlers. */
void n_spider_StateBoth(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, SPIDER_ACT_PRE + (u32)((s32)EN_ACTION(e) << 2)), e);
    GCALL1(GV(u32, SPIDER_ACT_STEP + (u32)((s32)EN_ACTION(e) << 2)), e);
}

/* 80105520 (e): its shadow object (+0x8BC) at its height. */
void n_spider_ShadowHeight(void) {
    u32 e = A0;
    V0 = (u32)EN_Y(e);
    GV(u32, GV(u32, e + 0x8BC) + 0xB0) = V0;
}

/* 8010569C (e): a spider's frame (see the top of the file). */
void n_spider_Update(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 handler = GV(u32, SPIDER_STATES + (u32)((s32)EN_STATE(e) << 2));
    SW = SPIDER_WORK_BASE + (u32)EN_SLOT(e) * SPIDER_WORK_SIZE;
    GCALL1(handler, e);
    if (EN_FLAGS(e) & EF_SHOT) {
        GCALL2(FN_EN_SET_SPEED, e, 1);
        A0 = e;
        n_spider_OnHit();
        GCALL1(FN_EN_HIT_POST, e);
    }
    A0 = e;
    n_spider_ShadowHeight();
    A0 = e;
    n_spider_MoveStep();
}
