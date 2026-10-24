/* The giant moth (enemy program ENEMPROG 59, loaded in enemy slot B at
 * 0x80100000). Identified by the area scenario that loads it (area60, the
 * moth): it flutters up off the floor, hovers turning to the player,
 * swoops down at them and grabs them when it reaches them from the front,
 * and drops when shot enough.
 *
 * moth_Update (0x80101D64) runs every frame for each one: it picks the
 * work block, counts animation frames, runs the state's handler (0x80102698;
 * state 1, moth_StateActive, runs the action's handler from 0x80102674),
 * takes the shots (moth_OnHit) and pushes the player back after a grab.
 *
 * The flight is a mode in the work block (MW_MODE): 0 resting, 2 turning
 * to the player on the floor, 3 rising / swooping, 4 gliding at the player
 * (a jump), 5 dropping (shot or past a wall), 6 hit hard: falling, 7 hit
 * hard again; the actions run the mode's step (1 flying, 2 on the floor,
 * 3 swooping, 5 dying, 7 grabbing).
 *
 * Each one has a 0x16-byte work block (0x801026AC + slot * 0x16; the
 * current one's address is kept at 0x80102748, see MW_*).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"

void n_moth_CrossedWall(void);
void n_moth_TurnTo(void);
void n_moth_Swoop(void);
void n_moth_Grab(void);
void n_moth_Glide(void);
void n_moth_Drop(void);
void n_moth_FlyStep(void);
void n_moth_FloorStep(void);
void n_moth_DieStep(void);
void n_moth_OnHit(void);
void n_moth_Update(void);
void n_moth_SwoopStep(void);
void n_moth_GrabStep(void);
void n_moth_StateActive(void);

/* ---- the work block ---- */
#define MW               GV(u32, 0x80102748)
#define MW_INIT          GV(u8, MW + 0x00)
#define MW_SIDE          GV(u8, MW + 0x01)    /* turning: 0 left, 1 right */
#define MW_MODE          GV(u8, MW + 0x02)
#define MW_HURT          GV(s16, MW + 0x04)   /* damage taken */
#define MW_HURT2         GV(s16, MW + 0x06)   /* damage since it last dropped */
#define MW_LIMIT         GV(s16, MW + 0x08)   /* hit points / 5 */
#define MW_TIMER         GV(s16, MW + 0x0A)
#define MW_VY            GV(s16, MW + 0x0C)   /* rise (negative) / fall speed */
#define MW_SPEED         GV(s16, MW + 0x0E)
#define MW_FRAME         GV(s16, MW + 0x10)   /* frames into the animation */
#define MW_FRAME_PREV    GV(s16, MW + 0x12)
#define MW_COUNT         GV(u16, MW + 0x14)
#define MOTH_WORK_BASE   0x801026ACu
#define MOTH_WORK_SIZE   0x16
#define g_mothPush       GV(s16, 0x8010274C)  /* the player pushed back after a grab */

#define MOTH_ACTIONS     0x80102674u
#define MOTH_STATES      0x80102698u
#define FN_M_REST        0x80100510u   /* mode 0 (not decompiled) */
#define FN_M_HIT_HARD    0x80101278u   /* mode 7 (not decompiled) */
#define FN_M_SPIN        0x80101150u   /* mode 7 on the floor (not decompiled) */
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_EN_ANIM_SOUND 0x8008127Cu   /* (e, bank, sound) */
#define PLAYER_HP        GV(s16, 0x800A8584)

static u32 moth_call(void (*f)(void), u32 a0, u32 a1) {
    A0 = a0; A1 = a1;
    f();
    return V0;
}

/* Has the animation stopped (its frame at the last frame)? */
static int anim_at_end(u32 e) {
    u32 p = EN_ANIMSTATE(e);
    return GV(u16, p + 0x14) == GV(u16, p + 0xE);
}

/* Climb by the work block's vertical speed (the model's height too). */
static void rise(u32 e, s32 dy) {
    u32 m = GV(u32, e + 0x8BC);
    GV(u32, m + 0xB0) += (u32)dy;
    GV(u32, e + 0x19C) += (u32)dy;
}

/* Back on the floor (y 0) once y isn't negative. */
static int landed(u32 e) {
    if (EN_Y(e) < 0) return 0;
    u32 m = GV(u32, e + 0x8BC);
    EN_Y(e) = 0;
    GV(u32, m + 0xB0) = 0;
    return 1;
}

/* Fly forward at speed (vertical movement by the work block). */
static void fly(u32 e, s32 speed, s32 accel) {
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)speed, 0);
    EN_VY(e) = 0;
    rise(e, MW_VY);
    MW_VY = (s16)(MW_VY + accel);
}

/* 8010003C (from, to): the first wall the path crosses from its front, or
 * -1 (zombie_CrossedWall). */
void n_moth_CrossedWall(void) {
    FRAME(0x40);
    V0 = (u32)en_crossed_segment_poll(A0, A1, GV(u32, EP_WALLS_N + 4), GV(u8, EP_WALLS_N), 16, 1, 0x801001B4);
}

/* 801001E4 (e, dist): unless the player (dist 0), or the point dist x 1000
 * ahead of them, is within +-0x22 of its facing, turn by 0x20 towards
 * it. Returns the turn made (0: none). */
void n_moth_TurnTo(void) {
    FRAME(0x68);
    u32 e = A0, dist = A1;
    u32 t = sp + 0x10;
    if (dist == 0) {
        MEM_W32(t + 0, GV(u32, PLAYER_POS_ADDR));
        MEM_W32(t + 4, GV(u32, PLAYER_POS_ADDR + 4));
        MEM_W32(t + 8, GV(u32, PLAYER_POS_ADDR + 8));
    } else {
        gcall(F_ROT_STEP, PLAYER_ROT_ADDR, t, (u32)-100, 0);
        u32 x = MEM_R32(t + 0) * 10 * dist, z = MEM_R32(t + 8) * 10 * dist;
        MEM_W32(t + 0, x + GV(u32, PLAYER_POS_ADDR));
        MEM_W32(t + 8, z + GV(u32, PLAYER_POS_ADDR + 8));
    }
    if (en_point_in_cone(sp + 0x50, sp + 0x40, sp + 0x30, e, 0x22, 100, MEM_R32(t), MEM_R32(t + 8))) {
        V0 = 0;
        return;
    }
    u32 v = sp + 0x20;
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v, MEM_R32(v) + (u32)EN_X(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    u32 turn = 0x20;
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), v, MEM_R32(t), MEM_R32(t + 8)) < 0) turn = (u32)-0x20;
    GV(u16, e + 0x1C6) = (u16)((GV(u16, e + 0x1C6) + turn) & 0xFFF);
    V0 = (u32)(s32)(s16)turn;
}

/* 801006B0 (e): mode 3: rise (30 frames, turning to the player), then
 * swoop down onto the floor (on to mode 2). Close in front of the player
 * while swooping: grab them (mode 5), or from high up just drop (anim 7). */
void n_moth_Swoop(void) {
    FRAME(0x18);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        MW_TIMER = 0x1E;
        MW_VY = -0x78;
        EN_STEP(e)++;
        /* fall through */
    case 1:
        gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x14, 0);
        EN_VY(e) = 0;
        rise(e, -0x14);
        if (moth_call(n_moth_TurnTo, e, 0) == 0) EN_STEP(e)++;
        MW_TIMER = (s16)(MW_TIMER - 1);
        if (MW_TIMER == -1) EN_STEP(e)++;
        break;
    case 2:
        fly(e, -0xDC, 5);
        if (MW_VY < 0) break;
        EN_STEP(e)++;
        /* fall through */
    case 3:
        fly(e, -0xDC, 0xA);
        if (landed(e)) {
            GCALL2(FN_EN_SET_SPEED, e, 7);
            MW_MODE = 2;
            en_start_anim(e, 2, 0x40);
        }
        break;
    }
    if (MW_MODE != 3 || PLAYER_HP <= 0 || EN_STEP(e) < 2) return;
    if ((u32)EN_DIST(e) >= 0x708 || !(EN_FLAGS(e) & EF_SEES)) return;
    s32 mode = D_800BB19C;
    if (mode == 3) return;
    s32 d = (s32)PLAYER_YAW - (s32)EN_YAW(e);
    if (d < 0) d = -d;
    if (!((((u32)d - 0x47D) & 0xFFFF) < 0x707)) return;
    if (EN_Y(e) < -0x577) {
        en_start_anim(e, 7, 0x40);
        return;
    }
    g_mothPush = 0x64;
    if (mode == 1 && ep_grabLock == 0 && ep_grabLock2 == 0) {
        GCALL3(FN_EN_GRAB_PLAYER, e, 7, 0);
        GCALL1(FN_EN_GRAB_START, e);
    }
    MW_MODE = 5;
    en_start_anim(e, 1, 0x40);
    GCALL2(FN_EN_SET_SPEED, e, 6);
}

/* 80100AAC (e): action 7: grab the player (when free to), hold on until
 * the animation's events, then let go (mode 5, the player can act). */
void n_moth_Grab(void) {
    FRAME(0x18);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        if (D_800BB19C == 1 && ep_grabLock == 0 && ep_grabLock2 == 0) {
            GCALL3(FN_EN_GRAB_PLAYER, e, 6, 0);
            GCALL1(FN_EN_GRAB_START, e);
        }
        GCALL2(FN_EN_SET_SPEED, e, 0);
        EN_STEP(e)++;
        /* fall through */
    case 1:
        if (en_anim_event1(e)) EN_STEP(e) += 3;
        break;
    case 3:
        if (en_anim_event1(e)) EN_STEP(e)++;
        break;
    case 4:
        if (PLAYER_HP > 0) D_80062DEC = 1;
        MW_MODE = 5;
        en_start_anim(e, 1, 0x40);
        break;
    }
}

/* 80100CA0 (e): mode 4: turn to the point 8000 ahead of the player, then
 * glide at them with a hop (faster the closer they are), stopping at a
 * wall (mode 5); back on the floor, mode 2. Seeing the player while
 * turning: mode 3 instead. */
void n_moth_Glide(void) {
    FRAME(0x30);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        if (moth_call(n_moth_TurnTo, e, 8) == 0) EN_STEP(e)++;
        if (EN_FLAGS(e) & EF_SEES) {
            MW_MODE = 3;
            EN_STEP(e) = 0;
        }
        return;
    case 1: {
        MW_VY = -0x96;
        u16 s = (u16)(0xFF9Cu - (u32)EN_DIST(e) / 100);
        MW_SPEED = (s16)s;
        if ((s16)s < 0xC9) MW_SPEED = -0xC8;
        EN_STEP(e)++;
    }
        /* fall through */
    case 2: {
        u32 v = sp + 0x10;
        gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-0x19, 0);
        MEM_W32(v + 0, MEM_R32(v + 0) * 20 + (u32)EN_X(e));
        MEM_W32(v + 4, MEM_R32(v + 4) * 20 + (u32)EN_Y(e));
        MEM_W32(v + 8, MEM_R32(v + 8) * 20 + (u32)EN_Z(e));
        if ((s32)moth_call(n_moth_CrossedWall, EN_POS(e), v) != -1) {
            MW_MODE = 5;
            en_start_anim(e, 1, 0x40);
        }
        fly(e, MW_SPEED, 5);
        if (MW_VY < 0) return;
        EN_STEP(e)++;
    }
        /* fall through */
    case 3:
        fly(e, MW_SPEED, 5);
        if (landed(e)) {
            GCALL2(FN_EN_SET_SPEED, e, 7);
            MW_MODE = 2;
            en_start_anim(e, 2, 0x40);
        }
        return;
    }
}

/* 80100FD0 (e): mode 5: drop to the floor, levelling out; landed: dead
 * (y -200, anim 5), or after a big hit mode 7 (else 6) on the floor. */
void n_moth_Drop(void) {
    FRAME(0x18);
    u32 e = A0;
    if (EN_STEP(e) != 0) return;
    rise(e, 0x64);
    if (EN_ROTX(e) != 0) {
        u16 rx = (u16)((GV(u16, e + 0x1C4) + 0x28) & 0xFFF);
        GV(u16, e + 0x1C4) = rx;
        if ((s32)rx < 0x30) GV(u16, e + 0x1C4) = 0;
    }
    if (!landed(e)) return;
    if (EN_ROTX(e) != 0) return;
    GCALL2(FN_EN_SET_SPEED, e, 7);
    if (EN_HP(e) <= 0) {
        if (EN_Y(e) < -0xC7) return;
        EN_Y(e) = -0xC8;
        if (EN_ROTX(e) != 0) return;
        en_start_anim(e, 5, 0x40);
        return;
    }
    if (MW_HURT >= 0x78) {
        MW_MODE = 7;
        MW_HURT = 0;
    } else {
        MW_MODE = 6;
    }
    en_start_anim(e, 2, 0x40);
}

/* 80101670 (e): action 1, flying: the mode's step (mode 3: hover 45
 * frames, then anim 3; mode 4: turn to the point ahead of the player). */
void n_moth_FlyStep(void) {
    FRAME(0x18);
    u32 e = A0;
    if (anim_at_end(e)) return;
    u8 mode = MW_MODE;
    if (mode >= 8) return;
    RT_POLL_NOSAVE(0x801016CC);
    switch (mode) {
    case 0:
        GCALL1(FN_M_REST, e);
        break;
    case 3:
        if (EN_STEP(e) == 0) {
            MW_TIMER = 0x2D;
            EN_STEP(e)++;
        } else if (EN_STEP(e) != 1) {
            break;
        }
        MW_TIMER = (s16)(MW_TIMER - 1);
        if (MW_TIMER != -1) break;
        en_start_anim(e, 3, 0x40);
        break;
    case 4:
        if (moth_call(n_moth_TurnTo, e, 8) != 0) break;
        en_start_anim(e, 3, 0x10);
        break;
    case 5:
        A0 = e;
        n_moth_Drop();
        break;
    case 6:
        if (EN_STEP(e) == 0) {
            MW_VY = -0x64;
            EN_STEP(e) = 2;
        }
        A0 = e;
        n_moth_Swoop();
        break;
    case 7:
        GCALL1(FN_M_HIT_HARD, e);
        break;
    }
}

/* 80101828 (e): action 2, on the floor: flapping sounds; mode 2 turn to
 * the player, then rise at them (mode 3) if it sees them, else jump
 * (mode 4); mode 6: spin round 0x400 either way. */
void n_moth_FloorStep(void) {
    FRAME(0x20);
    u32 e = A0;
    if (anim_at_end(e)) return;
    if (MW_FRAME == 8 || MW_FRAME == 0x10) GCALL3(FN_EN_ANIM_SOUND, e, 0xA, 5);
    switch (MW_MODE) {
    case 2:
        if (moth_call(n_moth_TurnTo, e, 0) != 0) return;
        if (!(EN_FLAGS(e) & EF_SEES)) {
            MW_MODE = 4;
            en_start_anim(e, 3, 0x40);
        } else {
            MW_MODE = 3;
            en_start_anim(e, 1, 0x40);
        }
        return;
    case 3:
        if (EN_STEP(e) == 0) MW_MODE = 2;
        return;
    case 6:
        if (EN_STEP(e) == 0) {
            u32 r = en_rand();
            u32 w = MW;
            GV(u8, w + 1) = (u8)((r * GV(u16, w + 0x14)) & 1);
            MW_TIMER = 0x400;
            EN_STEP(e)++;
        } else if (EN_STEP(e) != 1) {
            return;
        }
        if (MW_SIDE == 0) GV(u16, e + 0x1C6) += 0x40;
        else GV(u16, e + 0x1C6) -= 0x40;
        MW_TIMER = (s16)(MW_TIMER - 0x40);
        if (MW_TIMER >= 0) return;
        en_start_anim(e, 1, 0x40);
        return;
    case 7:
        GCALL1(FN_M_SPIN, e);
        return;
    }
}

/* 80101A78 (e): action 5, dying: sinks; at the animation's event every
 * part stops (+0xD, +0x10) and it's switched off (+0x320 | 0x10007). */
void n_moth_DieStep(void) {
    FRAME(0x18);
    u32 e = A0;
    if (anim_at_end(e)) return;
    switch (EN_STEP(e)) {
    case 0:
        GCALL2(FN_EN_SET_SPEED, e, 4);
        EN_STEP(e)++;
        /* fall through */
    case 1:
        GV(s32, e + 0x174) -= 0x10;
        if (!en_anim_event1(e)) return;
        {
            s32 n = EN_NPARTS(e);
            for (s32 i = 0; i < n; i++) {
                if (i != 0) RT_POLL_NOSAVE(0x80101B84);
                u32 p = EN_ANIMPART(e, i);
                GV(u8, p + 0xD) = 0;
                GV(u16, p + 0x10) = 0;
            }
        }
        EN_FLAGS(e) |= 0x10007;
        return;
    }
}

/* 80101BB0 (e): the shots this frame: take the damage (also counted in
 * MW_HURT, MW_HURT2); killed: enemy_Killed. On the floor and not hurt
 * much: rise (mode 3); hurt by more than a fifth of its hit points since
 * it last dropped (or killed): drop (mode 5). */
void n_moth_OnHit(void) {
    FRAME(0x18);
    u32 e = A0;
    EN_FLAGS(e) &= ~(u32)EF_SHOT;
    GCALL2(FN_EN_SET_SPEED, e, 3);
    if (EN_HP(e) > 0) {
        if (EN_NHITS(e) >= 0) {
            u32 w = MW;
            for (s32 i = 0;; i++) {
                u16 d = GV(u16, e + 0x330 + (u32)i * 2);
                GV(u16, e + 0x31E) = (u16)(GV(u16, e + 0x31E) - d);
                GV(u16, w + 4) = (u16)(GV(u16, w + 4) + d);
                GV(u16, w + 6) = (u16)(GV(u16, w + 6) + d);
                if (EN_NHITS(e) < i + 1) break;
                RT_POLL_NOSAVE(0x80101C4C);
            }
        }
        if (EN_HP(e) <= 0) GCALL2(FN_EN_KILLED, e, (u32)(s32)EN_ROOMID(e));
        if (EN_Y(e) == 0 && MW_HURT < 0x78 && EN_HP(e) > 0) {
            MW_MODE = 3;
            en_start_anim(e, 3, 0x40);
        } else if (!(MW_HURT2 < MW_LIMIT && EN_HP(e) > 0)) {
            MW_MODE = 5;
            MW_HURT2 = 0;
            en_start_anim(e, 1, 0x40);
        }
    }
    EN_NHITS(e) = -1;
    GCALL1(FN_EN_HIT_PRE, e);
}

/* 80101D64 (e): every frame: the work block (set up the first time: the
 * drop threshold), the animation frame count, the state's handler, the
 * shots, and pushing the player back after a grab (g_mothPush, slowing
 * by 4 a frame). */
void n_moth_Update(void) {
    FRAME(0x28);
    u32 e = A0;
    u32 w = MOTH_WORK_BASE + (u32)EN_SLOT(e) * MOTH_WORK_SIZE;
    MW = w;
    GV(u16, w + 0x14) = (u16)(GV(u16, w + 0x14) + 1);
    if (GV(u8, w) == 0) {
        GV(u8, w) = 1;
        MW_LIMIT = (s16)((s32)EN_HP(e) / 5);
    }
    u32 p = EN_ANIMSTATE(e);
    if (GV(u16, p + 0x16) < GV(u16, p + 0x14)) {
        MW_FRAME = 0;
    } else {
        MW_FRAME_PREV = MW_FRAME;
        if ((s32)MW_FRAME != (s32)GV(u16, EN_ANIMSTATE(e) + 0x14)) MW_FRAME = (s16)(MW_FRAME + 1);
    }
    GCALL1(GV(u32, MOTH_STATES + (u32)((s32)EN_STATE(e) << 2)), e);
    if (EN_FLAGS(e) & EF_SHOT) {
        A0 = e;
        n_moth_OnHit();
        GCALL1(FN_EN_HIT_POST, e);
    }
    s16 push = g_mothPush;
    if (push == 0) return;
    u32 v = sp + 0x10;
    gcall(F_ROT_STEP, PLAYER_ROT_ADDR, v, (u32)(s32)push, 0);
    if (gcall(F_MOVE_COLLIDE, PLAYER_POS_ADDR, v, 0, 0) == 0) {
        GV(u32, PLAYER_POS_ADDR) += GS(u32, v);
        GV(u32, PLAYER_POS_ADDR + 8) += GS(u32, v + 8);
    } else {
        GV(u32, PLAYER_POS_ADDR) = GS(u32, v);
        GV(u32, PLAYER_POS_ADDR + 8) = GS(u32, v + 8);
    }
    g_mothPush = (s16)(g_mothPush - 4);
    if (g_mothPush < 0) g_mothPush = 0;
}

/* 80101FF4 (e): action 3, swooping: a wing beat sound at its start, then
 * mode 3's swoop or mode 4's glide. */
void n_moth_SwoopStep(void) {
    FRAME(0x20);
    u32 e = A0;
    if (anim_at_end(e)) return;
    if (MW_FRAME == 0) GCALL3(FN_EN_ANIM_SOUND, e, 8, 1);
    u8 mode = MW_MODE;
    A0 = e;
    if (mode == 3) n_moth_Swoop();
    else if (mode == 4) n_moth_Glide();
}

/* 801020F4 (e): action 7: the grab (moth_Grab) while the animation runs. */
void n_moth_GrabStep(void) {
    FRAME(0x18);
    if (anim_at_end(A0)) return;
    n_moth_Grab();
}

/* 80102144 (e): state 1: run the action's handler. */
void n_moth_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, MOTH_ACTIONS + (u32)((s32)EN_ACTION(e) << 2)), e);
}
