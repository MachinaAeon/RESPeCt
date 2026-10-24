/* The alligator (enemy program ENEMPROG 56, loaded in enemy slot B at
 * 0x80100000). Identified by the area scenarios that load it (area61 and
 * area62, the alligator: stage 4's sewer): it bursts in through the wall
 * (a scripted entrance: the wall's picture patched, a flash, the music),
 * swims at the player along the channel, lunges and bites (grabbing
 * them), tires and rests, and can be hurt enough to back off.
 *
 * alligator_Update (0x80101B84) runs every frame: it picks the work block,
 * runs the state's handler (0x801020C0: 0 set up, 1 active, 2 hit, 3
 * dying) and takes the shots (alligator_OnHit). In the active state the
 * action's pair of handlers run (0x80102050 every frame, 0x80102088 while
 * the phase is negative): 1 and 2 the entrance from either end (step by
 * step), 3 swimming, 4 and 5 lunges, 7 diving away.
 *
 * Each one has a 12-byte work block (0x801020D4 + slot * 12; the current
 * one's address is kept at 0x80102128, see AW_*): +0 bit 0 tired, +4
 * frames swimming, +8 damage taken.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"

void n_alligator_AnimEvent(void);
void n_alligator_PlayerInCone(void);
void n_alligator_TryLunge(void);
void n_alligator_Tire(void);
void n_alligator_EntranceAPre(void);
void n_alligator_EntranceA(void);
void n_alligator_EntranceBPre(void);
void n_alligator_EntranceB(void);
void n_alligator_SwimPre(void);
void n_alligator_Swim(void);
void n_alligator_LungePre(void);
void n_alligator_Lunge(void);
void n_alligator_DivePre(void);
void n_alligator_Dive(void);
void n_alligator_Damage(void);
void n_alligator_OnHit(void);
void n_alligator_StateActive(void);
void n_alligator_StateDying(void);
void n_alligator_StateNone(void);
void n_alligator_Update(void);

/* ---- the work block ---- */
#define AW               GV(u32, 0x80102128)
#define AW_FLAGS         GV(u32, AW + 0x0)    /* bit 0: tired */
#define AW_SWIM          GV(s32, AW + 0x4)    /* frames swimming */
#define AW_HURT          GV(s32, AW + 0x8)    /* damage taken */
#define GATOR_WORK_BASE  0x801020D4u
#define GATOR_WORK_SIZE  12

#define GATOR_PRE        0x80102050u
#define GATOR_STEP       0x80102088u
#define GATOR_STATES     0x801020C0u
/* the overlay's tuning values */
#define K_BACK1          GV(s32, 0x8010200C)
#define K_BACK2          GV(s32, 0x80102010)
#define K_TURN           GV(u16, 0x80102014)
#define K_DROP           GV(s32, 0x80102018)
#define K_YAW            GV(u16, 0x8010201C)
#define K_SLIDE          GV(s32, 0x80102020)
#define K_DIVE           GV(s32, 0x80102048)
#define g_gatorWait      GV(s32, 0x80102044)
#define LINE_A           0x80101FECu           /* entrance A's trigger line (two points) */
#define LINE_B           0x80102024u           /* entrance B's */

#define FN_A_CAN_GRAB    0x801000DCu   /* (e): mode 1 and the player free (not decompiled) */
#define FN_EN_SET_SPEED  0x80080718u   /* (e, k) */
#define FN_EN_ANIM_SOUND 0x8008127Cu   /* (e, bank, sound) */
#define FN_EN_DIST       0x8007F4E8u   /* (e): +0x868 */
#define FN_PART_DAMAGE   0x8008E0E8u   /* (e, hit): the hit's damage */
#define FN_SEQ_REWIND    0x800925C4u
#define FN_SOUND_AT      0x800805F4u
#define FN_SCREEN_FLASH  0x800731BCu
#define FN_MUSIC_PLAY    0x8001209Cu
#define FN_MESSAGE       0x8008D390u
#define FN_MOVE_IMAGE    0x800356E0u
#define FN_DRAW_SYNC     0x80035374u
#define g_musicTrack     GV(s16, 0x80062E78)

static u32 gator_call(void (*f)(void), u32 a0, u32 a1, u32 a2) {
    A0 = a0; A1 = a1; A2 = a2;
    f();
    return V0;
}

/* 80100078 (e, part, k): the animation event bit of part `part`, k frames
 * ahead (zombie_AnimEvent). */
void n_alligator_AnimEvent(void) {
    V0 = en_anim_event(A0, A1, A2);
}

/* 80100104 (e, spread): is the player in its view cone (licker_PlayerInCone)? */
void n_alligator_PlayerInCone(void) {
    FRAME(0x50);
    V0 = en_player_in_cone(sp, A0, (s32)A1);
}

/* 8010028C (e): no event running and the player free, within 6000, seen
 * and in front (+-0x5A): lunge, one way or the other (action 4 or 5). */
void n_alligator_TryLunge(void) {
    FRAME(0x18);
    u32 e = A0;
    if (ep_grabLock != 0 || ep_grabLock2 != 0) return;
    if ((s32)GCALL1(FN_EN_DIST, e) >= 0x1771) return;
    if (!(EN_FLAGS(e) & EF_SEES)) return;
    if (gator_call(n_alligator_PlayerInCone, e, 0x5A, 0) == 0) return;
    if ((en_rand() & 0xFF) < 0x7F) GCALL4(FN_EN_SET_ACTION_B, e, 4, 3, 4);
    else GCALL4(FN_EN_SET_ACTION_B, e, 5, 4, 4);
}

/* 80100338 (e): swimming (anim 1) 150 frames without being hurt much: get
 * tired (anim 2, slower); tired and hurt by 300: swim again. */
void n_alligator_Tire(void) {
    FRAME(0x20);
    u32 e = A0;
    s16 a = EN_ANIM(e);
    if (a == 1) {
        u32 w = AW;
        s32 t = GV(s32, w + 4) + 1;
        GV(s32, w + 4) = t;
        if (t < 0x96 || GV(s32, w + 8) >= 0x12C) return;
        GCALL3(FN_EN_SET_ANIM_B, e, 2, 2);
        EN_SPEED(e) = (s16)GCALL2(FN_EN_BASE_SPEED, e, 1);
        w = AW;
        GV(s32, w + 8) = 0;
        GV(u32, w) |= 1;
    } else if (a == 2) {
        if (AW_HURT < 0x12C) return;
        GCALL3(FN_EN_SET_ANIM_B, e, 1, 2);
        EN_SPEED(e) = (s16)GCALL2(FN_EN_BASE_SPEED, e, 0);
        u32 w = AW;
        GV(s32, w + 8) = 0;
        GV(s32, w + 4) = 0;
        GV(u32, w) &= ~1u;
    }
}

/* 80100674 (e): action 1, every frame: no hits taken. */
void n_alligator_EntranceAPre(void) {
    u32 e = A0;
    EN_NHITS(e) = -1;
    EN_FLAGS(e) &= ~(u32)EF_SHOT;
}

/* 80100AB8 (e): action 2, every frame: the same. */
void n_alligator_EntranceBPre(void) {
    u32 e = A0;
    EN_NHITS(e) = -1;
    EN_FLAGS(e) &= ~(u32)EF_SHOT;
}

/* The burst through the wall, animation frames 0x14E-0x1AC: thrown
 * forward, then slowing. */
static void burst_move(u32 e) {
    u32 f = GV(u16, EN_ANIMSTATE(e) + 0x16);
    if (f - 0x14E < 0x14) {
        gcall(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (u32)-0x64, 0);
        EN_VX(e) = (s32)((u32)EN_VX(e) << 1);
        EN_VZ(e) = (s32)((u32)EN_VZ(e) << 1);
        EN_VY(e) = (s32)((u32)EN_VY(e) << 1);
    }
    if ((u32)GV(u16, EN_ANIMSTATE(e) + 0x16) - 0x18B < 0x18)
        gcall(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (u32)K_BACK1, 0);
    if (GV(u16, EN_ANIMSTATE(e) + 0x16) >= 0x1AC)
        gcall(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (u32)K_BACK2, 0);
    if (gator_call(n_alligator_AnimEvent, e, 1, 2)) {
        GCALL2(FN_EN_SET_ANIM, e, 0);
        EN_STEP(e)++;
    }
}

/* Turn while the animation runs on (frame +0x10 not negative); at its end
 * animation 1 and the next step. */
static void turn_step(u32 e) {
    u32 p = EN_ANIMSTATE(e);
    if (GV(u16, p + 0x16) < GV(u16, p + 0x14)) {
        if (GV(s16, p + 0x10) < 0) return;
        GV(u16, e + 0x1C6) = (u16)(GV(u16, e + 0x1C6) + K_TURN);
        gcall(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (u32)(s32)EN_SPEED(e), 0);
        return;
    }
    GCALL2(FN_EN_SET_ANIM, e, 1);
    EN_STEP(e)++;
}

/* Done with the entrance: swim (action 3), not tired. */
static void entrance_end(u32 e) {
    GCALL3(FN_EN_SET_ACTION, e, 3, (u32)(s32)EN_ANIM(e));
    EN_FLAGS(e) &= 0xFFDFFFFFu;
    u32 w = AW;
    GV(s32, w + 4) = 0;
    GV(s32, w + 8) = 0;
}

/* Body part settings once it's in the channel. */
static void parts_in(u32 e) {
    GV(u16, e + 0x392) = 0x9C4;
    GV(u16, e + 0x616) = 0x7D0;
    GV(u16, e + 0x672) = 0x7D0;
}

/* 80100690 (e): action 1, the entrance from the -x end: wait hidden until
 * the player crosses LINE_A (event flag 0x3F, message 8), drop in, burst
 * out (seq_Rewind, sounds, a flash, the music), turn into the channel and
 * slide to x 0, then swim. */
void n_alligator_EntranceA(void) {
    FRAME(0x18);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if ((u16)step >= 0xB) return;
    RT_POLL_NOSAVE(0x801006C8);
    switch (step) {
    case 0:
        EN_STEP(e)++;
        /* fall through */
    case 1:
        GCALL3(FN_EN_SET_ANIM_B, e, 5, 2);
        EN_X(e) = -0x1D4C;
        EN_Y(e) = 0x3E8;
        EN_Z(e) = 0x300C;
        GV(u16, e + 0x1C6) = 0xC00;
        GV(u8, 0x800A8D02) = 2;
        GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 1;
        EN_STEP(e)++;
        EN_FLAGS(e) |= 0x200000;
        break;
    case 2:
    case 3:
        EN_STEP(e)++;
        break;
    case 4:
        EN_ALIVE(e) = 0;
        EN_FLAGS(e) |= 0x100000;
        EN_STEP(e)++;
        break;
    case 5:
        if ((s32)gcall(FN_LINE_SIDE, LINE_A, LINE_A + 0x10, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) < 0) break;
        EN_STEP(e)++;
        GCALL2(FN_FLAG_SET, ep_eventFlags, 0x3F);
        GCALL2(FN_MESSAGE, 0, 8);
        break;
    case 6: {
        s32 y = (s32)((u32)EN_Y(e) - (u32)K_DROP);
        EN_Y(e) = y;
        if (y > 0) break;
        EN_ALIVE(e) = 1;
        EN_Y(e) = 0;
        EN_FLAGS(e) &= 0xFFEFFFFFu;
        EN_STEP(e)++;
        break;
    }
    case 7:
        if (GV(u16, EN_ANIMSTATE(e) + 0x16) == 0x154) {
            GCALL1(FN_SEQ_REWIND, 0x800A8FF0u);
            GCALL4(FN_SOUND_AT, 6, 3, 0x7F, 0x7F);
            GCALL4(FN_SOUND_AT, 6, 4, 0x7F, 0x7F);
            GCALL4(FN_SCREEN_FLASH, 0xFF, 0xFF, 0x16, 9);
            g_musicTrack = 0;
            GCALL1(FN_MUSIC_PLAY, 0);
        }
        burst_move(e);
        break;
    case 8:
        turn_step(e);
        break;
    case 9:
        if (EN_YAW(e) != 0) {
            u16 y = (u16)(GV(u16, e + 0x1C6) + K_YAW);
            GV(u16, e + 0x1C6) = y;
            if ((s16)y >= 0x1000) GV(u16, e + 0x1C6) = 0;
        }
        if (EN_X(e) < 0) {
            s32 x = (s32)((u32)EN_X(e) + (u32)K_SLIDE);
            EN_X(e) = x;
            if (x >= 0) EN_X(e) = 0;
        }
        if (EN_YAW(e) != 0 || EN_X(e) != 0) break;
        parts_in(e);
        EN_STEP(e)++;
        break;
    case 10:
        entrance_end(e);
        break;
    }
}

/* 80100AD4 (e): action 2, the entrance from the +x end: wait until event
 * flag 0x3F (and not 0x40), in normal play, the player crosses LINE_B (or
 * the first enemy is down to 300 hit points): message 6, then after 10
 * frames drop in, burst through the wall (its picture patched in VRAM,
 * sounds, event flag 0x40), turn and slide to x 0, then swim. */
void n_alligator_EntranceB(void) {
    FRAME(0x28);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if ((u16)step >= 0xB) return;
    RT_POLL_NOSAVE(0x80100B10);
    switch (step) {
    case 0:
        EN_STEP(e)++;
        /* fall through */
    case 1:
        GCALL3(FN_EN_SET_ANIM_B, e, 5, 2);
        EN_X(e) = 0x1D4C;
        EN_Y(e) = 0x3E8;
        EN_Z(e) = -0x300C;
        GV(u16, e + 0x1C6) = 0x400;
        GV(u16, GV(u32, e + 0x8BC) + 0xD4) = 1;
        EN_STEP(e)++;
        EN_FLAGS(e) |= 0x208000;
        break;
    case 2:
    case 3:
        EN_STEP(e)++;
        break;
    case 4:
        EN_ALIVE(e) = 0;
        EN_FLAGS(e) |= 0x100000;
        EN_STEP(e)++;
        break;
    case 5:
        if (!GCALL2(FN_FLAG_TEST, ep_eventFlags, 0x3F)) break;
        if (GCALL2(FN_FLAG_TEST, ep_eventFlags, 0x40)) break;
        if (D_800BB19C != 1) break;
        if ((s32)gcall(FN_LINE_SIDE, LINE_B, LINE_B + 0x10, (u32)EP_PLAYER_X, (u32)EP_PLAYER_Z) < 0 &&
            GV(s16, ep_enemies + 0x31E) >= 0x12D)
            break;
        g_gatorWait = 0xA;
        EN_STEP(e)++;
        GCALL2(FN_MESSAGE, 0, 6);
        GV(u32, 0x800BB018) = 0;
        EN_FLAGS(e) &= 0xFFFF7FFFu;
        break;
    case 6: {
        s32 t = g_gatorWait;
        if (t > 0) {
            g_gatorWait = t - 1;
            break;
        }
        s32 y = (s32)((u32)EN_Y(e) - (u32)K_DROP);
        EN_Y(e) = y;
        if (y > 0) break;
        EN_ALIVE(e) = 1;
        EN_Y(e) = 0;
        EN_FLAGS(e) &= 0xFFEFFFFFu;
        EN_STEP(e)++;
        break;
    }
    case 7:
        if (GV(u16, EN_ANIMSTATE(e) + 0x16) == 0x154) {
            u32 r = sp + 0x10;
            GS(u16, r + 0) = 0x340;
            GS(u16, r + 4) = 0x30;
            GS(u16, r + 2) = 0;
            GS(u16, r + 6) = 0x80;
            GCALL3(FN_MOVE_IMAGE, r, 0x300, 0);
            GS(u16, r + 0) = 0x1B0;
            GS(u16, r + 2) = 0x1E2;
            GS(u16, r + 4) = 0x10;
            GS(u16, r + 6) = 1;
            GCALL3(FN_MOVE_IMAGE, r, 0x1B0, 0x1E1);
            GCALL1(FN_DRAW_SYNC, 0);
            GCALL4(FN_SOUND_AT, 6, 5, 0x7F, 0x7F);
            GCALL4(FN_SOUND_AT, 6, 6, 0x7F, 0x7F);
            GCALL2(FN_FLAG_SET, ep_eventFlags, 0x40);
        }
        burst_move(e);
        break;
    case 8:
        turn_step(e);
        break;
    case 9:
        if (EN_YAW(e) != 0x800) {
            u16 y = (u16)(GV(u16, e + 0x1C6) + K_YAW);
            GV(u16, e + 0x1C6) = y;
            if ((s16)y >= 0x800) GV(u16, e + 0x1C6) = 0x800;
        }
        if (EN_X(e) > 0) {
            s32 x = (s32)((u32)EN_X(e) - (u32)K_SLIDE);
            EN_X(e) = x;
            if (x <= 0) EN_X(e) = 0;
        }
        if (EN_YAW(e) != 0x800 || EN_X(e) != 0) break;
        GV(u8, 0x800A8D02) = 0;
        parts_in(e);
        EN_STEP(e)++;
        break;
    case 10:
        entrance_end(e);
        break;
    }
}

/* 80100FD0 (e): action 3, every frame: lunge when it can, tire. */
void n_alligator_SwimPre(void) {
    FRAME(0x18);
    u32 e = A0;
    A0 = e;
    n_alligator_TryLunge();
    A0 = e;
    n_alligator_Tire();
}

/* 80100FFC (e): action 3, swimming along the channel (x 0): towards the
 * player when far, kept within z +-3000 by the room flags 0x63 / 0x64
 * (the gates at either end, by which end it came in: +0x850). */
void n_alligator_Swim(void) {
    FRAME(0x18);
    u32 e = A0;
    if (EN_STEP(e) != 0) return;
    if ((s32)GCALL1(FN_EN_DIST, e) >= 0x1771) {
        gcall(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (u32)(s32)EN_SPEED(e), 0);
        EN_X(e) = 0;
    } else {
        EN_X(e) = 0;
        EN_VZ(e) = 0;
    }
    EN_VX(e) = 0;
    s16 id = EN_ROOMID(e);
    if (id == 0x63) {
        if (GCALL2(FN_FLAG_TEST, ep_roomFlags, 0x64)) return;
        if (EN_Z(e) >= 0xBB8) return;
        EN_Z(e) = 0xBB8;
        EN_VZ(e) = 0;
    } else if (id == 0x64) {
        if (GCALL2(FN_FLAG_TEST, ep_roomFlags, 0x63)) return;
        if (EN_Z(e) < -0xBB7) return;
        EN_Z(e) = -0xBB8;
        EN_VZ(e) = 0;
    }
}

/* 801011E4 (e): action 5, every frame: stay in the channel (x 0). */
void n_alligator_LungePre(void) {
    u32 e = A0;
    EN_X(e) = 0;
    EN_VX(e) = 0;
}

/* 801011F0 (e): action 5, a lunge: from animation frame 0x12A grab the
 * player if close and free; at the animation's event swim on (tired:
 * animation 2). */
void n_alligator_Lunge(void) {
    FRAME(0x18);
    u32 e = A0;
    s16 step = EN_STEP(e);
    if (step == 0) {
        if (GV(u16, EN_ANIMSTATE(e) + 0x16) < 0x12A) return;
        if ((s32)GCALL1(FN_EN_DIST, e) < 0x1771 && GCALL1(FN_A_CAN_GRAB, e)) {
            GCALL3(FN_EN_GRAB_PLAYER, e, 6, 0);
            GCALL1(FN_EN_GRAB_START, e);
        }
        EN_STEP(e)++;
    } else if (step == 1) {
        if (!gator_call(n_alligator_AnimEvent, e, 1, 2)) return;
        GCALL3(FN_EN_SET_ACTION, e, 3, (AW_FLAGS & 1) ? 2 : 1);
    }
}

/* 8010135C (e): action 7, every frame: nothing. */
void n_alligator_DivePre(void) {
}

/* The dive's sideways push: forward at K_DIVE with x and z swapped, no
 * vertical movement. */
static void dive_push(u32 e) {
    gcall(FN_ROT_VELOCITY, EN_ROT(e), EN_VEL(e), (u32)K_DIVE, 0);
    EN_VY(e) = EN_VX(e);
    s32 z = EN_VZ(e), x = EN_VY(e);
    EN_VY(e) = 0;
    EN_VX(e) = z;
    EN_VZ(e) = x;
}

/* 80101364 (e): action 7, diving away (+0x320 | 2): pushed aside at
 * animation frames 0x1C5-0x1CD and 0x1E0-0x1ED; at the event, gone
 * (hidden, +0x320 | 0x4001). */
void n_alligator_Dive(void) {
    FRAME(0x18);
    u32 e = A0;
    switch (EN_STEP(e)) {
    case 0:
        EN_FLAGS(e) |= 2;
        EN_STEP(e)++;
        /* fall through */
    case 1:
        if ((u32)GV(u16, EN_ANIMSTATE(e) + 0x16) - 0x1C5 < 9) dive_push(e);
        if ((u32)GV(u16, EN_ANIMSTATE(e) + 0x16) - 0x1E0 < 0xE) dive_push(e);
        if (gator_call(n_alligator_AnimEvent, e, 1, 1)) EN_STEP(e)++;
        break;
    case 2:
        EN_ALIVE(e) = 0;
        EN_FLAGS(e) |= 0x4001;
        EN_STEP(e)++;
        break;
    }
}

/* 80101638 (e): take this frame's hits (an unarmoured part's damage by
 * 0x8008E0E8): killed, state 3 (action 9) and 0; else 2 for a big hit (or
 * by chance), 3 once hurt by 300 in all, otherwise 1. */
void n_alligator_Damage(void) {
    FRAME(0x28);
    u32 e = A0;
    u32 res = 1;
    if (EN_NHITS(e) >= 0) {
        for (s32 i = 0;; i++) {
            u32 part = GV(u8, e + 0x326 + (u32)i);
            if (GV(s16, e + part * 0x5C + 0x39A) == 0) {
                GV(u8, e + 0x326 + (u32)i) = 1;
                u32 d = GCALL2(FN_PART_DAMAGE, e, (u32)i);
                GV(u16, e + 0x330 + (u32)i * 2) = (u16)d;
            }
            u16 hp = (u16)(GV(u16, e + 0x31E) - GV(u16, e + 0x330 + (u32)i * 2));
            GV(u16, e + 0x31E) = hp;
            if ((s16)hp <= 0) {
                GCALL2(FN_EN_SET_SPEED, e, 3);
                EN_STATE(e) = 3;
                GCALL4(FN_EN_SET_ACTION_B, e, 9, 6, 2);
                V0 = 0;
                return;
            }
            u32 w = AW;
            s16 d = GV(s16, e + 0x330 + (u32)i * 2);
            GV(s32, w + 8) += d;
            d = GV(s16, e + 0x330 + (u32)i * 2);
            if (d >= 0x6E) res = 2;
            else if (d >= 0x50) { if ((en_rand() & 0xFF) < 0x7F) res = 2; }
            else { if ((en_rand() & 0xFF) < 0xC) res = 2; }
            if (EN_NHITS(e) < i + 1) break;
            RT_POLL_NOSAVE(0x80101774);
        }
    }
    if (AW_HURT >= 0x12C) res = 3;
    V0 = res;
}

/* 801017C0 (e): the shots: killed, enemy_Killed; a big hit, flinch (state
 * 2, action 8); hurt by 300, back off (the same, rested). */
void n_alligator_OnHit(void) {
    FRAME(0x18);
    u32 e = A0;
    EN_FLAGS(e) &= ~(u32)EF_SHOT;
    A0 = e;
    n_alligator_Damage();
    s32 r = (s32)V0;
    if (r == 0) {
        GCALL2(FN_EN_KILLED, e, (u32)(s32)EN_ROOMID(e));
    } else if (r == 2) {
        EN_STATE(e) = 2;
        GCALL3(FN_EN_SET_ACTION, e, 8, 3);
    } else if (r == 3) {
        EN_STATE(e) = 2;
        GCALL3(FN_EN_SET_ACTION, e, 8, 8);
        u32 w = AW;
        GV(s32, w + 8) = 0;
        GV(s32, w + 4) = 0;
        GV(u32, w) &= ~1u;
    }
    EN_NHITS(e) = -1;
    GCALL1(FN_EN_HIT_PRE, e);
}

/* 801018B8 (e): state 1: splashing sounds (anim 2 every 64 frames, 3
 * every 32), then the action's handlers. */
void n_alligator_StateActive(void) {
    FRAME(0x18);
    u32 e = A0;
    s16 a = EN_ANIM(e);
    if (a == 2) {
        if (!(ep_frameCount & 0x3F)) GCALL3(FN_EN_ANIM_SOUND, e, 8, 5);
    } else if (a == 3) {
        if (!(ep_frameCount & 0x1F)) GCALL3(FN_EN_ANIM_SOUND, e, 8, 5);
    }
    GCALL1(GV(u32, GATOR_PRE + (u32)((s32)EN_ACTION(e) << 2)), e);
    if (EN_PHASE(e) < 0) GCALL1(GV(u32, GATOR_STEP + (u32)((s32)EN_ACTION(e) << 2)), e);
}

/* 80101A04 (e): state 3, dying: no more hits; both handlers. */
void n_alligator_StateDying(void) {
    FRAME(0x18);
    u32 e = A0;
    EN_NHITS(e) = -1;
    EN_FLAGS(e) &= ~(u32)EF_SHOT;
    GCALL1(GV(u32, GATOR_PRE + (u32)((s32)EN_ACTION(e) << 2)), e);
    GCALL1(GV(u32, GATOR_STEP + (u32)((s32)EN_ACTION(e) << 2)), e);
}

/* 80101A84: state 4: nothing. */
void n_alligator_StateNone(void) {
}

/* 80101B84 (e): every frame: the work block, the state's handler, the
 * shots. */
void n_alligator_Update(void) {
    FRAME(0x18);
    u32 e = A0;
    AW = GATOR_WORK_BASE + (u32)EN_SLOT(e) * GATOR_WORK_SIZE;
    GCALL1(GV(u32, GATOR_STATES + (u32)((s32)EN_STATE(e) << 2)), e);
    if (!(EN_FLAGS(e) & EF_SHOT)) return;
    GCALL2(FN_EN_SET_SPEED, e, 0xA);
    A0 = e;
    n_alligator_OnHit();
    GCALL1(FN_EN_HIT_POST, e);
}
