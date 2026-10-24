/* The crows (enemy program ENEMPROG 52, loaded in enemy slot B at
 * 0x80100000). Identified by the area scenario that loads it (area56, the
 * bird: stage 9 room 2, several of them): they perch, take off, circle and
 * wander about the room high up, swoop down at the player and peck
 * (grabbing them), and drop dead in a burst of feathers (fx_Hit kind 11,
 * fx_Feathers) when shot.
 *
 * crow_Update (0x80103C38) runs every frame for each one: it picks the
 * work block, checks the room's event (crow_RoomEvent), runs the state's
 * handler (0x80104408; state 1, crow_StateActive, runs the action's
 * handler from 0x801043D0), takes the shots (crow_TakeHits) and keeps
 * its last position. The actions decompiled here: 3 falling, 4 flying,
 * 5 pecking, 6 swooping, 7 taking off, 8 perched, 9 landing, 10 perched
 * (looking about), 11 dying, 13 falling dead. Each action keeps the
 * animation's frame count (crow_FrameTick) and its event bit (CW_EVENT).
 *
 * Flying (CW_FLIGHT): 0 wandering, 1 heading for the player, 2 turning
 * back, 3 circling; the wander changes every CW_TIMER frames
 * (crow_PickFlight).
 *
 * Each one has a 0x2C-byte work block (0x8010444C + slot * 0x2C; the
 * current one's address is kept at 0x80104580, see CW_*).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"

void n_crow_TakeHits(void);
void n_crow_WallBetween(void);
void n_crow_WallAhead(void);
void n_crow_Steer(void);
void n_crow_TurnTo(void);
void n_crow_FallStep(void);
void n_crow_PickFlight(void);
void n_crow_Approach(void);
void n_crow_Circle(void);
void n_crow_FlyStep(void);
void n_crow_PeckStep(void);
void n_crow_SwoopStep(void);
void n_crow_TakeoffStep(void);
void n_crow_Hop(void);
void n_crow_PerchStep(void);
void n_crow_LandStep(void);
void n_crow_WatchStep(void);
void n_crow_DieStep(void);
void n_crow_DeadFallStep(void);
void n_crow_RoomEvent(void);
void n_crow_Update(void);
void n_crow_StateActive(void);

/* ---- the work block ---- */
#define CW               GV(u32, 0x80104580)
#define CW_SIDE          GV(u8, CW + 0x00)    /* steering round a wall: 0 no, 1 left, 2 right */
#define CW_EVENT         GV(u8, CW + 0x01)    /* the animation's event bit (part 1) */
#define CW_FAR           GV(u8, CW + 0x02)
#define CW_FLYING        GV(u8, CW + 0x03)
#define CW_FLIGHT        GV(u8, CW + 0x04)
#define CW_KILLED        GV(u8, CW + 0x05)
#define CW_PECKED        GV(u8, CW + 0x07)
#define CW_INIT          GV(u8, CW + 0x08)
#define CW_ROOM          GV(u8, CW + 0x09)    /* the room's event: 0, 1 started, 2 */
#define CW_TIMER2        GV(s16, CW + 0x0A)
#define CW_RANGE         GV(s16, CW + 0x0C)
#define CW_FRAME         GV(s16, CW + 0x10)   /* frames into the animation */
#define CW_FRAME_PREV    GV(s16, CW + 0x12)
#define CW_LEFT          GV(u16, CW + 0x14)   /* frames left */
#define CW_COUNT         GV(u16, CW + 0x16)
#define CW_TIMER         GV(u16, CW + 0x18)   /* frames to the next change of flight */
#define CW_LAST_X        GV(s32, CW + 0x1C)
#define CW_LAST_Z        GV(s32, CW + 0x24)
#define CROW_WORK_BASE   0x8010444Cu
#define CROW_WORK_SIZE   0x2C
#define g_crowCount      GV(s32, 0x80104588)
#define g_crowStarted    GV(s32, 0x80104584)
#define g_crowPlaced     GV(s32, 0x8010458C)
#define CROW_PLACES      0x801043B0u          /* x, z per slot */

#define CROW_ACTIONS     0x801043D0u
#define CROW_STATES      0x80104408u
#define FN_C_PERCH       0x80100C7Cu   /* (e): on the ground (not decompiled) */
#define FN_C_TAKEOFF     0x801009DCu   /* (e): pick how to take off (not decompiled) */
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_EN_ANIM_SOUND 0x8008127Cu   /* (e, bank, sound) */
#define FN_DISTANCE      0x8007BDDCu   /* (from, to) */
#define g_stage          GV(s32, 0x800A866C)
#define g_room           GV(s32, 0x800B3F08)
#define g_crowsHold      GV(s32, 0x800AB524)
#define WALLS            GV(u32, EP_WALLS_N + 4)

static u32 crow_call(void (*f)(void), u32 a0, u32 a1, u32 a2) {
    A0 = a0; A1 = a1; A2 = a2;
    f();
    return V0;
}

/* Animation `anim` with blend frames, without making it visible. */
static void anim_set(u32 e, u32 anim, u32 blend) {
    GV(u16, e + 0x31A) = (u16)anim;
    GV(u16, e + 0x860) = (u16)anim;
    GV(u16, e + 0x85C) = 1;
    GV(u32, e + 0x8B0) = blend;
    EN_FLAGS(e) &= 0xFFFF7FF3u;
}

/* Move up or down by dy (the model's height too). */
static void rise(u32 e, u32 dy) {
    u32 m = GV(u32, e + 0x8BC);
    GV(u32, m + 0xB0) += dy;
    GV(u32, e + 0x19C) += dy;
}

/* Count the frames of animated part `part` (CW_FRAME). */
static void frame_tick(u32 e, u32 part) {
    u32 p = EN_ANIMPART(e, part);
    if (GV(u16, p + 0x16) < GV(u16, p + 0x14)) {
        CW_FRAME = 0;
    } else {
        CW_FRAME_PREV = CW_FRAME;
        if ((s32)CW_FRAME != (s32)GV(u16, EN_ANIMPART(e, part) + 0x14)) CW_FRAME = (s16)(CW_FRAME + 1);
    }
}

/* The frames left in part `part`'s animation (CW_LEFT). */
static void frames_left(u32 e, u32 part) {
    u32 p = EN_ANIMPART(e, part);
    u16 end = GV(u16, p + 0x14 + (u32)GV(u8, p + 0xC) * 4);
    CW_LEFT = (u16)(GV(u16, p + 0x16) - 1 - end);
}

/* Count down to the next change of flight (CW_TIMER, kept under 300). */
static void flight_tick(u32 e) {
    if (CW_TIMER >= 0x1F4) {
        s32 r = (s32)en_rand();
        CW_TIMER = (u16)(r % 300);
    }
    CW_TIMER = (u16)(CW_TIMER - 1);
    if (CW_TIMER == 0) {
        A0 = e;
        n_crow_PickFlight();
    }
}

/* The sign of x as the game works it out (x / |x|: 0 counts as -1). */
static s32 sign_div(s32 x) {
    return mips_div(x, x < 0 ? (s32)(0u - (u32)x) : x);
}

/* Does the segment a-b (stack vectors) separate p and q, and the
 * segment p-q a and b? (The walls loops.) */
static int crosses(u32 a, u32 b, u32 p, u32 q) {
    s32 s1 = sign_div((s32)gcall(FN_LINE_SIDE, a, b, MEM_R32(p), MEM_R32(p + 8)));
    s32 s2 = sign_div((s32)gcall(FN_LINE_SIDE, a, b, MEM_R32(q), MEM_R32(q + 8)));
    s32 s3 = (s32)gcall(FN_LINE_SIDE, p, q, MEM_R32(a), MEM_R32(a + 8));
    s32 s4 = (s32)gcall(FN_LINE_SIDE, p, q, MEM_R32(b), MEM_R32(b + 8));
    s3 = sign_div(s3);
    s4 = sign_div(s4);
    if ((s32)((u32)s1 * (u32)s2) >= 0) return 0;
    return (s32)((u32)s3 * (u32)s4) < 0;
}

/* Copy wall i's ends into the stack vectors a and b (x at +0, z at +8). */
static void wall_ends(u32 w, u32 a, u32 b) {
    MEM_W32(a + 0, GV(u32, w + 0));
    MEM_W32(a + 8, GV(u32, w + 4));
    MEM_W32(b + 0, GV(u32, w + 8));
    MEM_W32(b + 8, GV(u32, w + 12));
}

/* Steer away from a wall within 200 x 20 ahead (crow_WallAhead): turn
 * 0x30 towards the player's side, banking (+0x1C8); none: level out. */
static void avoid_wall(u32 sp, u32 e) {
    if ((s32)crow_call(n_crow_WallAhead, e, 0xC8, 0) == -1) {
        CW_SIDE = 0;
        GV(u16, e + 0x1C8) = 0;
        return;
    }
    u32 v = sp + 0x10;
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v, MEM_R32(v) + (u32)EN_X(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), v, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) >= 0) {
        GV(u16, e + 0x1C8) = 0x38;
        GV(u16, e + 0x1C6) += 0x30;
    } else {
        GV(u16, e + 0x1C8) = (u16)-0x38;
        GV(u16, e + 0x1C6) -= 0x30;
    }
    GV(u16, e + 0x1C6) &= 0xFFF;
}

/* 8010009C (e): take this frame's hits; killed: enemy_Killed, mark it
 * (+0x320 | 2, CW_KILLED). Unless it's in another animation, a burst of
 * feathers (fx_Hit kind 11) and animation 9. */
void n_crow_TakeHits(void) {
    FRAME(0x20);
    u32 e = A0;
    s32 n = EN_NHITS(e);
    if (n >= 0) {
        for (s32 i = 0;; i++) {
            GV(u16, e + 0x31E) = (u16)(GV(u16, e + 0x31E) - GV(u16, e + 0x330 + (u32)i * 2));
            if (n < i + 1) break;
            RT_POLL_NOSAVE(0x801000D8);
        }
    }
    if (EN_HP(e) <= 0) GCALL2(FN_EN_KILLED, e, (u32)(s32)EN_ROOMID(e));
    s16 a = EN_ANIM(e);
    if (a == 0xA || a == 0xC || (a >= 0 && a < 9)) {
        GCALL2(FN_EN_SET_SPEED, e, 1);
        gcall5(sp, FN_EN_DRAW, EN_OBJ(e), 0, 0, 0, 0xB);
        anim_set(e, 9, 0x10);
    }
    if (EN_HP(e) <= 0) {
        EN_FLAGS(e) |= 2;
        CW_KILLED = 1;
    }
}

/* 801001D0 (e): is there a wall between it and the player? */
void n_crow_WallBetween(void) {
    FRAME(0x58);
    u32 e = A0;
    u32 n = GV(u8, EP_WALLS_N);
    u32 w = WALLS;
    for (u32 i = 0; i < n; i++, w += 16) {
        if (i != 0) RT_POLL_NOSAVE(0x8010033C);
        wall_ends(w, sp + 0x10, sp + 0x20);
        if (crosses(sp + 0x10, sp + 0x20, EN_POS(e), PLAYER_POS_ADDR)) {
            V0 = 1;
            return;
        }
    }
    V0 = 0;
}

/* 80100380 (e, dist): the first wall between it and the point dist x 20
 * ahead, or -1. */
void n_crow_WallAhead(void) {
    FRAME(0x68);
    u32 e = A0, dist = A1;
    u32 t = sp + 0x10;
    gcall(F_ROT_STEP, EN_ROT(e), t, 0u - dist, 0);
    MEM_W32(t + 0, MEM_R32(t + 0) * 20 + (u32)EN_X(e));
    MEM_W32(t + 4, MEM_R32(t + 4) * 20 + (u32)EN_Y(e));
    MEM_W32(t + 8, MEM_R32(t + 8) * 20 + (u32)EN_Z(e));
    u32 n = GV(u8, EP_WALLS_N);
    u32 w = WALLS;
    for (u32 i = 0; i < n; i++, w += 16) {
        if (i != 0) RT_POLL_NOSAVE(0x80100558);
        wall_ends(w, sp + 0x20, sp + 0x30);
        if (crosses(sp + 0x20, sp + 0x30, EN_POS(e), t)) {
            V0 = i;
            return;
        }
    }
    V0 = (u32)-1;
}

/* 80100594 (e, dist, turn): steer round the wall within dist x 20 ahead
 * by turn a frame (0: 40..139 at random), banking: pick the way (CW_SIDE:
 * 1 left when there's a wall both sides or on the right, 2 right; at
 * random, or straight on when the wall is far) and keep turning that way.
 * No wall: stop steering. */
void n_crow_Steer(void) {
    FRAME(0x50);
    u32 e = A0;
    s32 dist = (s32)A1;
    u32 turn = A2;
    if ((turn & 0xFFFF) == 0) {
        s32 r = (s32)GCALL0(0x800398A8u);
        s32 v = (s32)((u32)r * CW_COUNT);
        turn = (u32)(v % 100 + 0x28);
    }
    s32 ahead = (s32)crow_call(n_crow_WallAhead, e, (u32)dist, 0);
    if (ahead == -1) {
        u32 w = CW;
        GV(u8, w) = 0;
        GV(u16, e + 0x1C8) = 0;
        V0 = w;
        return;
    }
    if (CW_SIDE == 0) {
        s32 half = dist >> 1;
        u16 yaw = GV(u16, e + 0x1C6);
        GV(u16, e + 0x1C6) = (yaw + 0x400) & 0xFFF;
        s32 l = (s32)crow_call(n_crow_WallAhead, e, (u32)half, 0);
        GV(u16, e + 0x1C6) = (GV(u16, e + 0x1C6) + 0x800) & 0xFFF;
        s32 r = (s32)crow_call(n_crow_WallAhead, e, (u32)half, 0);
        int pick = 0;    /* 1: at random */
        if (ahead == l) CW_SIDE = 1;
        else if (ahead == r) CW_SIDE = 2;
        else if (l == -1) {
            if (r == l) pick = 1;
            else CW_SIDE = 2;
        } else if (r != -1) {
            u32 w = WALLS + (u32)ahead * 16;
            wall_ends(w, sp + 0x10, sp + 0x20);
            if ((s32)gcall(FN_LINE_DIST, sp + 0x10, sp + 0x20, EN_POS(e), 0) < 0x3E8) pick = 1;
            else CW_SIDE = 0;
        } else CW_SIDE = 1;
        if (pick) CW_SIDE = (u8)((GV(u8, CW + 0x16) & 1) + 1);
        GV(u16, e + 0x1C6) = yaw;
    }
    if (CW_SIDE == 1) {
        GV(u16, e + 0x1C8) = (u16)-0x38;
        GV(u16, e + 0x1C6) = (u16)(GV(u16, e + 0x1C6) - turn);
    } else if (CW_SIDE == 2) {
        GV(u16, e + 0x1C8) = 0x38;
        GV(u16, e + 0x1C6) = (u16)(GV(u16, e + 0x1C6) + turn);
    }
    u16 y = GV(u16, e + 0x1C6) & 0xFFF;
    GV(u16, e + 0x1C6) = y;
    V0 = y;
}

/* 801007FC (e, test): unless the player is within +-0x2C of its facing
 * (then level out, 0), turn towards them banking: 0x30 a frame seeing
 * them (returns +-0x30), else 8 (returns +-0x10). With test set, only
 * which side they're on (1, -1). */
void n_crow_TurnTo(void) {
    FRAME(0x60);
    u32 e = A0, test = A1;
    if (en_point_in_cone(sp + 0x40, sp + 0x30, sp + 0x20, e, 0x2C, 100, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z)) {
        GV(u16, e + 0x1C8) = 0;
        V0 = 0;
        return;
    }
    u32 v = sp + 0x10;
    gcall(F_ROT_STEP, EN_ROT(e), v, (u32)-100, 0);
    MEM_W32(v, MEM_R32(v) + (u32)EN_X(e));
    MEM_W32(v + 8, MEM_R32(v + 8) + (u32)EN_Z(e));
    u32 ret, d;
    if ((s32)gcall(FN_LINE_SIDE, EN_POS(e), v, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) >= 0) {
        if (test) { V0 = 1; return; }
        GV(u16, e + 0x1C8) = 0x38;
        if (EN_FLAGS(e) & EF_SEES) { d = 0x30; ret = 0x30; }
        else { d = 8; ret = 0x10; }
    } else {
        if (test) { V0 = (u32)-1; return; }
        GV(u16, e + 0x1C8) = (u16)-0x38;
        if (EN_FLAGS(e) & EF_SEES) { d = (u32)-0x30; ret = (u32)-0x30; }
        else { d = (u32)-8; ret = (u32)-0x10; }
    }
    GV(u16, e + 0x1C6) = (u16)((GV(u16, e + 0x1C6) + d) & 0xFFF);
    V0 = ret;
}

/* 80101488 (e): action 3: fall (40 a frame) to the floor, then land
 * (animation 10). */
void n_crow_FallStep(void) {
    FRAME(0x18);
    u32 e = A0;
    frame_tick(e, 1);
    frames_left(e, 1);
    if (CW_FRAME == 4) GCALL3(FN_EN_ANIM_SOUND, e, 0xB, 0xB);
    if (D_800BB19C != 3) gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0x55, 0);
    u32 m = GV(u32, e + 0x8BC);
    GV(u32, m + 0xB0) += 0x28;
    s32 y = (s32)((u32)EN_Y(e) + 0x28);
    EN_Y(e) = y;
    if (y >= 0) {
        if (y > 0) {
            EN_Y(e) = 0;
            GV(u32, GV(u32, e + 0x8BC) + 0xB0) = 0;
        }
        anim_set(e, 0xA, 0x80);
    }
    CW_EVENT = (u8)en_anim_event1(e);
}

/* 80101650 (e): pick the next flight at random (fewer crows left: more
 * often): land (animation 3), wander (CW_FLIGHT 0) or head for the
 * player (1), and for how long (CW_TIMER). */
void n_crow_PickFlight(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 r = en_rand();
    s32 x = (s32)((u32)CW_COUNT + r);
    s32 roll = x % 10;
    s32 n = g_crowCount;
    s32 land, wander;
    u32 range;
    if (n > 0 && n < 3) { land = 0; wander = 0; range = 0xA; }
    else if (n > 0 && n < 5) { land = 1; wander = 2; range = 0x32; }
    else { land = 1; wander = 4; range = 0x12C; }
    if (roll < land) {
        anim_set(e, 3, 0x80);
    } else if (roll < wander) {
        CW_FLIGHT = 0;
        s32 v = (s32)en_rand();
        CW_TIMER = (u16)(v % (s32)range);
    } else {
        CW_FLIGHT = 1;
        s32 v = (s32)en_rand();
        CW_TIMER = (u16)(v % 300 + 0x190);
    }
}

/* 801017BC (e): flight 1, heading for the player: round walls, banking;
 * once facing them within CW_RANGE, swoop (animation 6); close, wander
 * again. */
void n_crow_Approach(void) {
    FRAME(0x30);
    u32 e = A0;
    flight_tick(e);
    if (crow_call(n_crow_WallBetween, e, 0, 0)) {
        crow_call(n_crow_Steer, e, 0x14, 0);
        return;
    }
    avoid_wall(sp, e);
    s16 step = EN_STEP(e);
    if (step == 0) {
        u32 w = CW;
        u16 c = GV(u16, w + 0x16);
        u32 k;
        if (!(c & 1)) {
            GV(u8, w + 2) = 0;
            k = (u32)(c % 6) + 8;
        } else {
            GV(u8, w + 2) = 1;
            k = (u32)(c % 3) + 6;
        }
        GV(u16, w + 0xC) = (u16)(k * 1000);
        EN_STEP(e)++;
    } else if (step == 1) {
        if (crow_call(n_crow_TurnTo, e, 0, 0) == 0 && (u32)EN_DIST(e) < (u32)(s32)CW_RANGE) {
            anim_set(e, 6, 8);
            return;
        }
        if ((u32)EN_DIST(e) < 0x3E8) {
            CW_FLIGHT = 0;
            s32 v = (s32)en_rand();
            CW_TIMER = (u16)(v % 100);
        }
    }
}

/* 80101AD0 (e): flight 3, circling: turn round 0x800, then fly on until
 * a wall ahead or the player is far (flight 1). */
void n_crow_Circle(void) {
    FRAME(0x20);
    u32 e = A0;
    flight_tick(e);
    crow_call(n_crow_Steer, e, 0x14, 0);
    switch (EN_STEP(e)) {
    case 0:
        CW_RANGE = 0x800;
        EN_STEP(e)++;
        /* fall through */
    case 1:
        GV(u16, e + 0x1C6) += 0x100;
        CW_RANGE = (s16)(CW_RANGE - 0x100);
        if (CW_RANGE > 0) return;
        EN_STEP(e)++;
        /* fall through */
    case 2:
        if (crow_call(n_crow_WallAhead, e, 0xC8, 0) == 0 && (u32)EN_DIST(e) < 0x1771) return;
        EN_STEP(e) = 0;
        CW_FLIGHT = 1;
        return;
    }
}

/* 80101C58 (e): action 4, flying: wing beats, forward (faster seeing the
 * player), keep between heights -0x7D0 and -0x79D, and the flight. */
void n_crow_FlyStep(void) {
    FRAME(0x20);
    u32 e = A0;
    frame_tick(e, 1);
    frames_left(e, 1);
    if (CW_FRAME == 3) GCALL3(FN_EN_ANIM_SOUND, e, 9, 2);
    if (CW_LAST_X == EN_X(e) && CW_LAST_Z == EN_Z(e)) GV(u16, e + 0x1C6) += 0x20;
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (EN_FLAGS(e) & EF_SEES) ? (u32)-0xAA : (u32)-0xF0, 0);
    if (EN_Y(e) >= -0x79D) rise(e, (u32)-0x2A);
    if (EN_Y(e) < -0x7D0) rise(e, 0x2A);
    switch (CW_FLIGHT) {
    case 0:
        crow_call(n_crow_Steer, e, 0x14, 0);
        flight_tick(e);
        break;
    case 1:
        A0 = e;
        n_crow_Approach();
        break;
    case 2:
        flight_tick(e);
        crow_call(n_crow_Steer, e, 0x14, 0);
        EN_STEP(e) = 0;
        CW_FLIGHT = 1;
        break;
    case 3:
        A0 = e;
        n_crow_Circle();
        break;
    }
    CW_EVENT = (u8)en_anim_event1(e);
}

/* 80102028 (e): action 5, pecking the player: feathers, grab them (when
 * free to), peck through the animation's events, then fly off (anim 4,
 * wandering) or, in a scene (mode 3), perch (anim 3). */
void n_crow_PeckStep(void) {
    FRAME(0x28);
    u32 e = A0;
    frame_tick(e, 1);
    frames_left(e, 1);
    s16 step = EN_STEP(e);
    if ((u16)step >= 5) goto done;
    RT_POLL_NOSAVE(0x801020FC);
    switch (step) {
    case 0:
        GCALL2(FN_EN_SET_SPEED, e, 4);
        gcall5(sp, FN_EN_DRAW, EN_OBJ(e), 0, 0, 0, 0xB);
        EN_STEP(e)++;
        if (D_800BB19C == 1 && ep_grabLock == 0 && ep_grabLock2 == 0) {
            GCALL3(FN_EN_GRAB_PLAYER, e, 6, 0);
            GCALL1(FN_EN_GRAB_START, e);
        }
        /* fall through */
    case 1:
    case 2:
    case 3:
        if (en_anim_event1(e)) EN_STEP(e)++;
        break;
    case 4:
        if (!en_anim_event1(e)) break;
        if (D_800BB19C == 3) {
            anim_set(e, 3, 0x20);
        } else {
            CW_FLIGHT = 0;
            s32 v = (s32)en_rand();
            CW_TIMER = (u16)(v % 80);
            anim_set(e, 4, 0x20);
        }
        break;
    }
done:
    CW_EVENT = (u8)en_anim_event1(e);
}

/* 801023E4 (e): action 6, swooping at the player: round walls, turn to
 * them; from far (step 0: by the distance, CW_RANGE) drop at them (step
 * 10, 11), or glide in (20, 21); close and low: grab and peck (or, facing
 * them, peck: anim 5), else fly off (flight 2, anim 4). Past them and
 * behind them: circle (flight 3). */
void n_crow_SwoopStep(void) {
    FRAME(0x30);
    u32 e = A0;
    frame_tick(e, 1);
    frames_left(e, 1);
    avoid_wall(sp, e);
    gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0xAA, 0);
    s32 turn = (s32)crow_call(n_crow_TurnTo, e, 0, 0);
    s32 d = (s32)PLAYER_YAW - (s32)EN_YAW(e);
    if (d < 0) d = -d;
    int behind = !((((u32)d - 0x401) & 0xFFFF) < 0x7FF);
    if (EN_STEP(e) >= 0xA && (u32)EN_DIST(e) >= 0xBB9 && behind) {
        s32 v = (s32)en_rand();
        CW_FLIGHT = 3;
        CW_TIMER = (u16)(v % 100);
        anim_set(e, 4, 0x20);
        return;
    }
    s16 step = EN_STEP(e);
    if ((u16)step >= 0x16) goto done;
    RT_POLL_NOSAVE(0x80102660);
    switch (step) {
    case 0:
        if (CW_FAR == 0 && (u32)EN_DIST(e) >= 0x1770) {
            u32 dist = GCALL2(FN_DISTANCE, EN_POS(e), PLAYER_POS_ADDR);
            u32 q = (dist - 0xBB8) / 170;
            u32 v = q == 0 ? 0xFFFFFFFFu : 0x5DCu / q;
            EN_DIST(e) = (s32)dist;
            CW_RANGE = (s16)(v << 1);
            EN_STEP(e) = 0xA;
        } else {
            EN_STEP(e) = 0x14;
        }
        break;
    case 0xA:
        if (EN_Y(e) < -0x1F4) {
            u32 dy = 0u - (u32)(s32)CW_RANGE;
            u32 m = GV(u32, e + 0x8BC);
            GV(u32, m + 0xB0) -= dy;
            GV(u32, e + 0x19C) -= dy;
        }
        if ((u32)EN_DIST(e) < 0xBB8) EN_STEP(e)++;
        /* fall through */
    case 0xB:
        if (EN_STEP(e) == 0xB) rise(e, (u32)-0x50);
        if ((u32)EN_DIST(e) < 0x320) {
            s32 y = EN_Y(e);
            if (y < -0x514 && !(y < -0x577)) {
                if (D_800BB19C == 1 && ep_grabLock == 0 && ep_grabLock2 == 0) {
                    GCALL3(FN_EN_GRAB_PLAYER, e, 6, 0);
                    GCALL1(FN_EN_GRAB_START, e);
                }
                A0 = e;
                n_crow_TakeHits();
            } else if (turn == 0 && !behind && (EN_FLAGS(e) & EF_SEES) && D_800BB19C != 3) {
                anim_set(e, 5, 8);
            } else {
                CW_FLIGHT = 2;
                s32 v = (s32)en_rand();
                CW_TIMER = (u16)(v % 300);
                anim_set(e, 4, 0x20);
            }
        } else if (EN_Y(e) < -0x672 && EN_STEP(e) == 0xB) {
            anim_set(e, 4, 0x20);
        }
        break;
    case 0x14: {
        u32 m = GV(u32, e + 0x8BC);
        GV(u32, m + 0xB0) += 0x28;
        s32 y = (s32)((u32)EN_Y(e) + 0x28);
        EN_Y(e) = y;
        if (!(y < -0x577)) EN_STEP(e)++;
    }
        /* fall through */
    case 0x15:
        if ((u32)EN_DIST(e) >= 0x320 || EN_Y(e) < -0x68F) break;
        if (!behind && (EN_FLAGS(e) & EF_SEES) && D_800BB19C != 3) {
            anim_set(e, 5, 0x20);
        } else {
            s32 v = (s32)en_rand();
            CW_FLIGHT = 2;
            CW_TIMER = (u16)(v % 300);
            anim_set(e, 4, 0x20);
        }
        break;
    }
done:
    CW_EVENT = (u8)en_anim_event1(e);
}

/* 80102AFC (e): action 7, taking off: wing beats; once the animation
 * (part 4) runs, rise (0x41 a frame), and at its event fly (anim 4). */
void n_crow_TakeoffStep(void) {
    FRAME(0x20);
    u32 e = A0;
    frame_tick(e, 4);
    CW_FLYING = 1;
    frames_left(e, 4);
    if (CW_FRAME == 0xC || CW_FRAME == 0x17) GCALL3(FN_EN_ANIM_SOUND, e, 9, 2);
    switch (EN_STEP(e)) {
    case 0: {
        s32 v = (s32)en_rand();
        CW_TIMER = (u16)(v % 100);
        EN_STEP(e)++;
    }
        /* fall through */
    case 1: {
        u32 p = EN_ANIMPART(e, 4);
        if (GV(u16, p + 0x14) < GV(u16, p + 0x16)) EN_STEP(e)++;
        break;
    }
    case 2:
        rise(e, (u32)-0x41);
        gcall(F_ROT_STEP, EN_ROT(e), EN_VEL(e), (u32)-0xAA, 0);
        if (CW_EVENT) anim_set(e, 4, 0x10);
        break;
    }
    if (CW_TIMER >= 0x1F4) {
        s32 r = (s32)en_rand();
        CW_TIMER = (u16)(r % 300);
    }
    CW_TIMER = (u16)(CW_TIMER - 1);
    CW_EVENT = (u8)en_anim_event(e, 4, 1);
}

/* 80102E04 (e): hopping round on the ground to face the player (0x93 a
 * frame between frames 5 and 11); faced, or after two hops, animation 7. */
void n_crow_Hop(void) {
    FRAME(0x20);
    u32 e = A0;
    u16 f = (u16)CW_FRAME;
    s32 side = (s32)crow_call(n_crow_TurnTo, e, 1, 0);
    if (side == 0) {
        anim_set(e, 7, 0x80);
        return;
    }
    if ((u16)(f - 5) < 7) {
        if (side > 0) GV(u16, e + 0x1C6) += 0x93;
        else GV(u16, e + 0x1C6) -= 0x93;
    }
    s16 step = EN_STEP(e);
    if (step == 0) {
        CW_TIMER2 = 0;
        EN_STEP(e)++;
    } else if (step == 1) {
        if ((s16)f != 0xF) return;
        CW_TIMER2 = (s16)(CW_TIMER2 + 1);
        if (CW_TIMER2 != 2) return;
        anim_set(e, 7, 0x80);
    }
}

/* 80102F50 (e): action 8, on the ground: the perch behaviour (not
 * decompiled), or once it has flown, hop round to the player. */
void n_crow_PerchStep(void) {
    FRAME(0x18);
    u32 e = A0;
    frame_tick(e, 1);
    u8 flying = CW_FLYING;
    frames_left(e, 1);
    if (flying == 0) GCALL1(FN_C_PERCH, e);
    else { A0 = e; n_crow_Hop(); }
    CW_EVENT = (u8)en_anim_event1(e);
}

/* 8010308C (e): action 9, landing: at the animation's event, animation
 * 13. */
void n_crow_LandStep(void) {
    u32 e = A0;
    frame_tick(e, 1);
    frames_left(e, 1);
    if (en_anim_event1(e)) anim_set(e, 0xD, 0x10);
    CW_EVENT = (u8)en_anim_event1(e);
}

/* 80103218 (e): action 10, perched: once the animation runs, at its event
 * (two frames ahead) take off (when it has flown before, the player is
 * near and there's no scene) or keep watching (animation 8). */
void n_crow_WatchStep(void) {
    FRAME(0x18);
    u32 e = A0;
    frame_tick(e, 1);
    frames_left(e, 1);
    s16 step = EN_STEP(e);
    if (step == 0) {
        u32 p = EN_ANIMPART(e, 1);
        if (GV(u16, p + 0x14) < GV(u16, p + 0x16)) EN_STEP(e) = (s16)(step + 1);
    } else if (step == 1 && en_anim_event(e, 1, 2)) {
        u32 w = CW;
        if (GV(u8, w + 3) == 0) {
            GCALL1(FN_C_TAKEOFF, e);
        } else if ((u32)EN_DIST(e) < 0x4A39 && D_800BB19C != 3) {
            anim_set(e, 8, 0x10);
        } else {
            GV(u16, w + 0xE) = 0;
            GCALL1(FN_C_TAKEOFF, e);
        }
    }
    CW_EVENT = (u8)en_anim_event1(e);
}

/* 80103458 (e): action 11, dying: still alive, back to animation 8 at
 * the event; dead: one crow fewer (+0x320 | 0x4000), twitch a few times
 * (animation 11), then every part stops and it's switched off. */
void n_crow_DieStep(void) {
    u32 e = A0;
    frame_tick(e, 1);
    frames_left(e, 1);
    if (EN_HP(e) > 0) {
        if (en_anim_event1(e)) anim_set(e, 8, 0x10);
        goto done;
    }
    switch (EN_STEP(e)) {
    case 0: {
        u32 w = CW;
        if (GV(u8, w + 5) != 0) {
            EN_FLAGS(e) |= 0x4000;
            GV(u8, w + 5) = 0;
            u32 w2 = CW;
            g_crowCount--;
            GV(u16, w2 + 0xA) = (u16)(GV(u16, w2 + 0x16) % 10);
        }
        EN_STEP(e)++;
    }
        /* fall through */
    case 1: {
        u32 w = CW;
        if (GV(s16, w + 0xA) != 0) {
            if (GV(s16, w + 0x10) != 2) goto done;
            GV(u16, w + 0xA) = (u16)(GV(u16, w + 0xA) - 1);
            anim_set(e, 0xB, 0xA0);
            goto done;
        }
        EN_STEP(e)++;
    }
        /* fall through */
    case 2: {
        u32 p = EN_ANIMPART(e, 1);
        if (!(GV(u16, p + 0x14) < GV(u16, p + 0x16))) goto done;
        EN_STEP(e)++;
    }
        /* fall through */
    case 3: {
        s32 n = EN_NPARTS(e);
        for (s32 i = 0; i < n; i++) {
            if (i != 0) RT_POLL_NOSAVE(0x80103718);
            GV(u8, EN_ANIMPART(e, i) + 0xD) = 0;
        }
        EN_FLAGS(e) |= 5;
        break;
    }
    }
done:
    CW_EVENT = (u8)en_anim_event1(e);
}

/* 80103898 (e): action 13, falling dead (+0x320 | 0x8000), faster and
 * faster, to the floor; then animation 11. */
void n_crow_DeadFallStep(void) {
    u32 e = A0;
    frame_tick(e, 1);
    frames_left(e, 1);
    switch (EN_STEP(e)) {
    case 0:
        EN_FLAGS(e) |= 0x8000;
        CW_RANGE = 0xA;
        EN_STEP(e)++;
        /* fall through */
    case 1: {
        u32 dy = 0u - (u32)(s32)CW_RANGE;
        u32 m = GV(u32, e + 0x8BC);
        GV(u32, m + 0xB0) -= dy;
        GV(u32, e + 0x19C) -= dy;
        CW_RANGE = (s16)(CW_RANGE + 2);
        if (EN_Y(e) < 0) break;
        if (EN_Y(e) > 0) {
            EN_Y(e) = 0;
            GV(u32, GV(u32, e + 0x8BC) + 0xB0) = 0;
        }
        EN_STEP(e)++;
    }
        /* fall through */
    case 3:
        anim_set(e, 0xB, 0x10);
        break;
    }
    CW_EVENT = (u8)en_anim_event1(e);
}

/* 80103A9C (e): stage 9 room 2's event: until event flag 6 the crows are
 * held still (0x800AB524); once the player moves, each counts as started
 * (g_crowStarted), and with four started the hold ends (event flag 0x3D).
 * After it, the first one is put at its place (CROW_PLACES). */
void n_crow_RoomEvent(void) {
    FRAME(0x20);
    u32 e = A0;
    if (g_stage != 9) return;
    s32 room = g_room;
    if (room != 2) return;
    if (!GCALL2(FN_FLAG_TEST, ep_eventFlags, 6)) {
        u32 w = CW;
        GV(u32, e + 0x170) = 0;
        GV(u32, e + 0x174) = 0;
        GV(u32, e + 0x178) = 0;
        GV(u8, w + 9) = 0;
        g_crowsHold = 1;
        return;
    }
    if (!GCALL2(FN_FLAG_TEST, ep_eventFlags, 0x3D)) {
        u32 w = CW;
        u8 st = GV(u8, w + 9);
        if (st == 0) {
            if (g_playerMoveFlags & 3) {
                GV(u8, w + 9) = 1;
                g_crowStarted++;
            } else {
                g_crowsHold = 1;
                GV(u32, e + 0x170) = 0;
                GV(u32, e + 0x174) = 0;
                GV(u32, e + 0x178) = 0;
                GV(u8, w + 9) = 0;
            }
        } else if (st == (u32)room) {
            if (g_crowStarted == 4) {
                g_crowsHold = 0;
                GCALL2(FN_FLAG_SET, ep_eventFlags, 0x3D);
            }
        }
        return;
    }
    if (g_crowPlaced != 0) return;
    u32 t = CROW_PLACES + (u32)EN_SLOT(e) * 8;
    u32 m = GV(u32, e + 0x8BC);
    EN_X(e) = GV(s32, t);
    g_crowPlaced = 1;
    EN_Y(e) = 0;
    EN_Z(e) = GV(s32, t + 4);
    GV(u32, m + 0xB0) = 0;
}

/* 80103C38 (e): every frame: the work block (set up the first time:
 * counted in g_crowCount, the model at its height), the room's event, the
 * state's handler, the shots, and its position kept for next frame. */
void n_crow_Update(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 w = CROW_WORK_BASE + (u32)EN_SLOT(e) * CROW_WORK_SIZE;
    CW = w;
    GV(u16, w + 0x16) = (u16)(GV(u16, w + 0x16) + 1);
    A0 = e;
    n_crow_RoomEvent();
    w = CW;
    if (GV(u8, w + 8) == 0) {
        GV(u8, w + 8) = 1;
        u32 m = GV(u32, e + 0x8BC);
        GV(u32, m + 0xB0) = (u32)EN_Y(e);
        g_crowCount++;
    }
    GCALL1(GV(u32, CROW_STATES + (u32)((s32)EN_STATE(e) << 2)), e);
    u32 f = EN_FLAGS(e);
    if (f & EF_SHOT) {
        EN_FLAGS(e) = f & ~(u32)EF_SHOT;
        A0 = e;
        n_crow_TakeHits();
        EN_NHITS(e) = -1;
        GCALL1(FN_EN_HIT_PRE, e);
        GCALL1(FN_EN_HIT_POST, e);
    }
    w = CW;
    GV(u32, w + 0x1C) = GV(u32, e + 0x198);
    GV(u32, w + 0x20) = GV(u32, e + 0x19C);
    GV(u32, w + 0x24) = GV(u32, e + 0x1A0);
    GV(u32, w + 0x28) = GV(u32, e + 0x1A4);
}

/* 80103E30 (e): state 1: run the action's handler. */
void n_crow_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    GCALL1(GV(u32, CROW_ACTIONS + (u32)((s32)EN_ACTION(e) << 2)), e);
}
