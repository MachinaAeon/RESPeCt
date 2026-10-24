/* Enemies: the per-frame update of every enemy, steering, moving through
 * the walls, animation changes, auto-aim targeting, and pushing each other
 * and the player apart (TEST overlay).
 *
 * Enemy records are 0x8C4 bytes, D_800AF530 of them at *0x800AB510.
 * Fields used here:
 *   +0x029 u8   visible / in play          +0x02A u8   1 = active
 *   +0x02B u8   bit 0 far away, bit 2 has extra body spheres
 *   +0x16C      bone/coordinate (root position at +0x38/+0x3C/+0x40)
 *   +0x170/+0x180  GsCOORDINATE2 scale / coordinate
 *   +0x198 s32 x, y, z position             +0x1C4 s16 rotation (yaw +0x1C6)
 *   +0x1D0 s32 animation tracks, +0x1D4[] their sequence records (hmd_anim.c)
 *   +0x274[]   per track: animation start table (4 bytes per animation)
 *   +0x314 u8, +0x315 u8 enemy type (indexes the AI tables), +0x316 s16
 *   +0x318 s16 state, +0x31A s16 next state (-1 none), +0x31C s16 substate
 *   +0x31E s16 hit points / targetable (< 0 not targetable)
 *   +0x320 u32 flags: 1 off, 4 root motion, 8 root motion running,
 *              0x20 blocked by a wall, 0x100 no movement, 0x400 alerted,
 *              0x1000 not aimable, 0x8000/0x200000 no body push,
 *              0x20000 gives control back, 0x80000 touching the player
 *   +0x354 s32 slot in 0x800B3EA8 (12 bytes each, s16 alert state)
 *   +0x392...  body spheres, 0x5C apart, until a negative +0x392 (radius);
 *              +0x3BC the sphere's coordinate
 *   +0x84E s16 AI table select, +0x850 s16 action
 *   +0x85C s16 animation change step (1 start, 2 blend, 3 drop root motion)
 *   +0x85E s16 animation playing, +0x860 s16 next, +0x862 s16 frame offset
 *   +0x866 s16 walking speed, +0x868 s32 distance to the player
 *   +0x86C s32 x, y, z before the move, +0x87C s32 x, y, z this frame's step
 *   +0x88C     model data (+8: animation table)
 *   +0x8A0 s32 x, y, z root offset, +0x8B0 s32 blend frames
 *   +0x8B8 s16 wall between it and the player (-1 none), +0x8BA its kind
 *   +0x8C0 s32 body radius
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "collision.h"

void n_enemy_SteerToPlayer(void);
void n_enemy_MoveStep(void);
void n_enemy_Distance(void);
void n_enemy_UpdateDistance(void);
void n_enemy_RootMotionStart(void);
void n_enemy_NextState(void);
void n_enemy_AnimChangeStep(void);
void n_enemy_CheckWallToPlayer(void);
void n_enemy_Awareness(void);
void n_enemy_GrabShot(void);
void n_enemy_UpdateAll(void);
void n_enemy_UpdateAllOnScratch(void);
void n_aim_ConsiderTarget(void);
void n_aim_TargetVisible(void);
void n_aim_UpdateTarget(void);
void n_enemy_SetAnim(void);
void n_enemy_SetStateAnim(void);
void n_enemy_SetAnimBlend(void);
void n_enemy_SetStateAnimBlend(void);
void n_enemy_ReleaseControl(void);
void n_enemy_PushApart(void);
void n_body_CirclePush(void);
void n_aim_SnapReticle(void);
void n_player_PushFromEnemies(void);
void n_enemy_CheckInFront(void);
void n_enemy_AttackInfo(void);
void n_enemy_AnimParam(void);
void n_enemy_AdvanceParams(void);
void n_enemy_MainTrack(void);
void n_enemy_MarkAlertSlot(void);

#define F_ISQRT          0x8002A3D8u
#define F_ROT_TO_STEP    0x8008286Cu   /* (rot, step, speed) */
#define F_COORD_UPDATE   0x80082760u   /* (coordinate, scale) */
#define F_ANIM_TRACK     0x80082DECu   /* (type) -> main animation track */
#define F_IN_FRONT       0x800824B0u   /* (enemy): flag 0x40 */
#define F_SHOT_GRAB_OK   0x8007FCCCu
#define F_RAND           0x80011570u
#define F_AREA_TEST      0x8008F64Cu
#define F_AREA_SET       0x8008F5F8u
#define F_LOCK_ON        0x80078D38u   /* (enemy, 0, distance, 2, [2]) */

#define g_enemies        GV(u32, 0x800AB510)
#define g_enemyCount     GV(s32, 0x800AF530)
#define ENEMY_SIZE       0x8C4
#define g_aimTarget      GV(u32, 0x800BB018)   /* nearest aimable enemy (0 none) */
#define g_aimTargetNew   GV(s32, 0x800A13D8)
#define g_gameMode       GV(s32, 0x800A866C)
#define g_alertSlots     0x800B3EA8u
#define g_alertRange     GV(s32, 0x800A8848)
#define g_frameCount     GV(u32, 0x80062E10)
#define g_freezeEnemies  GV(s32, 0x800BBB58)
#define g_msgState       GV(s32, 0x800BA9F4)
#define g_aiTableA       0x800A21ACu
#define g_aiTableB       0x800A2234u
#define g_controlEnemy   GV(u32, 0x800BB198)
#define g_inputLocked    GV(s16, 0x800B3D2C)
#define g_shots          0x800BBC18u          /* 4 x 0x14 */
#define g_area           0x80062F60u
#define g_areaMisses     GV(u32, 0x80063448 + 8)
#define g_wallInfo       GV(u32, 0x800B60D4)  /* 20 bytes per wall */

/* body push */
#define g_pushHit        GV(s32, 0x800BB040)
#define g_pushRadiusA    GV(s32, 0x800BB044)
#define g_pushRadiusB    GV(s32, 0x800BB048)
#define PLAYER_POS_A     0x800A85D8u
#define g_props          0x800C09D8u          /* 0x44 bytes each */
#define g_propSlots      0x800BBC68u          /* 4 x 0x18 */

#define E8(e, o)   GV(u8, (e) + (o))
#define ES16(e, o) GV(s16, (e) + (o))
#define EU16(e, o) GV(u16, (e) + (o))
#define E32(e, o)  GV(s32, (e) + (o))
#define EU32(e, o) GV(u32, (e) + (o))

#define F_OFF          0x1
#define F_ROOT_MOTION  0x4
#define F_ROOT_RUNNING 0x8
#define F_BLOCKED      0x20
#define F_NO_MOVE      0x100
#define F_ALERTED      0x400
#define F_NO_AIM       0x1000
#define F_NO_PUSH      0x8000
#define F_RELEASE      0x20000
#define F_TOUCH_PLAYER 0x80000
#define F_NO_PUSH2     0x200000

static inline s32 abs32(s32 a) { return a < 0 ? (s32)(0u - (u32)a) : a; }
static inline s32 mul32(s32 a, s32 b) { return (s32)((u32)a * (u32)b); }

/* ------------------------------------------------------------ movement */

/* 8007F210 (enemy): turn towards the player: the further off the facing
 * is (cosine of the angle between the step and the player), the faster
 * it turns, 6-0x28 units a frame; no turn when already facing the
 * player exactly. */
static const u8 turn_by_cos[10] = {0x28, 0x24, 0x20, 0x1C, 0x18, 0x14, 0x10, 0x0C, 0x08, 0x06};
void n_enemy_SteerToPlayer(void) {
    u32 e = A0;
    FRAME(0x38);
    GCALL3(F_ROT_TO_STEP, e + 0x1C4, e + 0x87C, (s32)ES16(e, 0x866));
    s32 sx = E32(e, 0x87C), sz = E32(e, 0x884);
    s32 dx = (s16)(u16)(GV(u16, g_player + 0x18) - EU16(e, 0x198));
    s32 dz = (s16)(u16)(GV(u16, g_player + 0x20) - EU16(e, 0x1A0));
    s32 len_step = (s32)GCALL1(F_ISQRT, (u32)mul32(sx, sx) + (u32)mul32(sz, sz));
    s32 len_dir = (s32)GCALL1(F_ISQRT, (u32)(dx * dx) + (u32)(dz * dz));
    s32 dot = (s32)((u32)mul32(E32(e, 0x87C), dx) + (u32)mul32(E32(e, 0x884), dz));
    s32 den = mul32(len_step, len_dir);
    if (den == 0) den = 1;
    s32 cosv = mips_div((s32)((u32)dot << 12), den);
    if (cosv <= 0x1000) {
        u32 k = (u32)((s32)((u32)cosv * 10) >> 12);
        s32 turn = k < 10 ? turn_by_cos[k] : 0x28;
        /* which side of its heading the player is: the point one step
         * ahead goes on the stack for geom_SideOfLine */
        GS(s32, sp + 0x14) = 0;
        GS(s32, sp + 0x10) = (s32)((u32)E32(e, 0x198) + (u32)E32(e, 0x87C));
        GS(s32, sp + 0x18) = (s32)((u32)E32(e, 0x1A0) + (u32)E32(e, 0x884));
        A0 = e + 0x198; A1 = sp + 0x10;
        A2 = GV(u32, g_player + 0x18); A3 = GV(u32, g_player + 0x20);
        n_geom_SideOfLine();
        if ((s32)V0 <= 0) turn = -turn;
        u16 yaw = (u16)((EU16(e, 0x1C6) + turn) & 0xFFF);
        EU16(e, 0x1C6) = yaw;
        V0 = yaw;
    } else {
        V0 = 0;
    }
}

/* 8007F404 (enemy): apply this frame's step through the walls (with the
 * extra lines), then rebuild its coordinate and clear the step. */
void n_enemy_MoveStep(void) {
    u32 e = A0;
    FRAME(0x18);
    if (!(EU32(e, 0x320) & F_NO_MOVE)) {
        E32(e, 0x874) = E32(e, 0x1A0);
        E32(e, 0x86C) = E32(e, 0x198);
        E32(e, 0x870) = E32(e, 0x19C);
        A0 = e + 0x198; A1 = e + 0x87C; A2 = 1;
        n_move_Collide();
        if ((V0 & 0xFFFF) == 0) {
            E32(e, 0x198) = (s32)((u32)E32(e, 0x198) + (u32)E32(e, 0x87C));
            E32(e, 0x1A0) = (s32)((u32)E32(e, 0x1A0) + (u32)E32(e, 0x884));
            EU32(e, 0x320) &= ~(u32)F_BLOCKED;
        } else {                              /* step holds the pushed-out position */
            E32(e, 0x198) = E32(e, 0x87C);
            E32(e, 0x1A0) = E32(e, 0x884);
            EU32(e, 0x320) |= F_BLOCKED;
        }
        E32(e, 0x19C) = (s32)((u32)E32(e, 0x19C) + (u32)E32(e, 0x880));
    }
    GCALL2(F_COORD_UPDATE, e + 0x180, e + 0x170);
    E32(e, 0x884) = 0;
    E32(e, 0x880) = 0;
    E32(e, 0x87C) = 0;
}

/* 8007F4E8 (enemy): its distance to the player. */
void n_enemy_Distance(void) { V0 = (u32)E32(A0, 0x868); }

/* 8007F4F4 (enemy): measure the distance from the player to the enemy's
 * body centre (position + the offset in its +0x388 record). */
void n_enemy_UpdateDistance(void) {
    u32 e = A0;
    FRAME(0x18);
    u32 c = EU32(e, 0x388);
    s32 dx = (s32)((u32)GV(s32, g_player + 0x18) - (u32)E32(e, 0x198) + (u32)GV(s32, c + 0x18));
    s32 dz = (s32)((u32)GV(s32, g_player + 0x20) - (u32)E32(e, 0x1A0) + (u32)GV(s32, c + 0x20));
    s32 d = (s32)GCALL1(F_ISQRT, (u32)mul32(dx, dx) + (u32)mul32(dz, dz));
    E32(e, 0x868) = d;
    V0 = (u32)d;
}

/* 8007F564 (enemy): when the main track reaches the end of an animation
 * that moves the body (its table entry negative), early on and with no
 * change pending: remember how far the root has moved (to take it back
 * off when the animation changes) and, for root-motion enemies, stop the
 * tracks. */
void n_enemy_RootMotionStart(void) {
    u32 e = A0;
    FRAME(0x20);
    u32 anims = GV(u32, EU32(e, 0x88C) + 8);
    u32 trk_i = GCALL1(F_ANIM_TRACK, E8(e, 0x315)) & 0xFF;
    u32 trk = EU32(e, 0x1D4 + trk_i * 4);
    if (GV(s32, anims + (u32)((s32)GV(s16, trk + 0x16) * 4) + 8) < 0 &&
        GV(s16, trk + 0x10) < 0x15 && ES16(e, 0x85C) == 0) {
        EU32(e, 0x320) |= F_ROOT_RUNNING;
        E32(e, 0x8A0) = (s32)((u32)E32(e, 0x198) - (u32)GV(s32, EU32(e, 0x16C) + 0x38));
        E32(e, 0x8A4) = (s32)((u32)E32(e, 0x19C) - (u32)GV(s32, EU32(e, 0x16C) + 0x3C));
        E32(e, 0x8A8) = (s32)((u32)E32(e, 0x1A0) - (u32)GV(s32, EU32(e, 0x16C) + 0x40));
        if (EU32(e, 0x320) & F_ROOT_MOTION)
            for (s32 i = 0; i < E32(e, 0x1D0); i++) GV(u8, EU32(e, 0x1D4 + i * 4) + 0xD) = 0;
    }
}

/* 8007F688 (enemy): switch to the queued state, if any. */
void n_enemy_NextState(void) {
    u32 e = A0;
    if (ES16(e, 0x31A) >= 0) {
        EU16(e, 0x318) = EU16(e, 0x31A);
        ES16(e, 0x31C) = 0;
        ES16(e, 0x31A) = -1;
    }
    V0 = 0xFFFFFFFFu;
}

/* 8007F6AC (enemy): the steps of an animation change (+0x85C):
 *   1  every track starts fading out (speed 0x10, from its key at +0xE)
 *   2  once the main track has finished: every track starts the new
 *      animation (+0x860) over the blend frames (+0x8B0); 3 next if the
 *      blend was under 2 frames, else done
 *   3  take the root offset back off the position */
void n_enemy_AnimChangeStep(void) {
    u32 e = A0;
    switch (ES16(e, 0x85C)) {
    case 1:
        EU16(e, 0x85C) = 2;
        GV(u8, EU32(e, 0x1D4) + 0x1B) = 1;
        for (s32 i = 0; i < E32(e, 0x1D0); i++) {
            u32 t = EU32(e, 0x1D4 + i * 4);
            GV(u16, t + 0x8) = GV(u16, t + 0xE);
            GV(u8, t + 0xD) = 0x10;
        }
        break;
    case 2: {
        if (GV(u8, EU32(e, 0x1D4) + 0x1B) != 0) break;
        /* track 0 even with no tracks counted, then the others */
        for (s32 i = 0; i == 0 || i < E32(e, 0x1D0); i++) {
            u32 t = EU32(e, 0x1D4 + i * 4);
            GV(u16, t + 0x10) = EU16(e, 0x8B0);
            GV(u16, t + 0x12) = EU16(e, 0x8B0);
            GV(u16, t + 0x8) = 0xFFFF;
            GV(u16, t + 0x14) = GV(u16, t + 0xE);
            u32 start = EU32(e, 0x274 + i * 4) + (u32)((s32)ES16(e, 0x860) * 4);
            u16 key = GV(u16, start);
            if (i > 0) key = (u16)(key + EU16(e, 0x862));
            GV(u16, t + 0x16) = key;
            GV(u8, t + 0xC) = GV(u8, start + 2);
        }
        u16 next = EU16(e, 0x860);
        ES16(e, 0x862) = 0;
        EU16(e, 0x85E) = next;
        if (E32(e, 0x8B0) < 2) EU16(e, 0x85C) = (u16)(EU16(e, 0x85C) + 1);
        else ES16(e, 0x85C) = 0;
        break;
    }
    case 3:
        ES16(e, 0x85C) = 0;
        E32(e, 0x198) = (s32)((u32)E32(e, 0x198) - (u32)E32(e, 0x8A0));
        E32(e, 0x19C) = (s32)((u32)E32(e, 0x19C) - (u32)E32(e, 0x8A4));
        E32(e, 0x1A0) = (s32)((u32)E32(e, 0x1A0) - (u32)E32(e, 0x8A8));
        break;
    }
}

/* 8007FA98 (enemy): is there a wall between it and the player (+0x8B8,
 * with that wall's kind at +0x8BA). */
void n_enemy_CheckWallToPlayer(void) {
    FRAME(0x18);
    u32 e = A0;
    A0 = e + 0x198; A1 = PLAYER_POS_A;
    n_wall_FirstCrossing();
    s16 w = (s16)V0;
    ES16(e, 0x8B8) = w;
    if (w == -1) { EU16(e, 0x8BA) = 0; V0 = 0xFFFFFFFFu; }
    else { u16 k = GV(u16, g_wallInfo + (u32)((s32)w * 20) + 0x10); EU16(e, 0x8BA) = k; V0 = k; }
}

static inline u32 alert_slot(u32 e) { return g_alertSlots + (u32)E32(e, 0x354) * 12; }

/* 8007FB08 (enemy): in normal play, track whether the enemy is in range of
 * the player (its alert slot: -1 out of range, 0 in range, 1 alerted) and
 * on alternate frames re-arm an idle slot; then whether it is in front of the player (flag 0x40) and the wall
 * check. */
void n_enemy_Awareness(void) {
    u32 e = A0;
    FRAME(0x18);
    if (g_gameMode == 5 && D_800B3F08 < 3) {
        u32 f = EU32(e, 0x320);
        if (!(f & F_OFF)) {
            if (GV(s16, alert_slot(e)) == 1) f |= F_ALERTED;
            else f &= ~(u32)F_ALERTED;
            EU32(e, 0x320) = f;
            if (g_alertRange < E32(e, 0x868)) {
                GV(s16, alert_slot(e)) = -1;
                E8(e, 0x2B) |= 1;
            } else {
                GV(s16, alert_slot(e)) = 0;
                E8(e, 0x2B) &= 0xFE;
            }
        } else if (E32(e, 0x868) >= 0x2301) {
            GV(s16, alert_slot(e)) = -1;
            E8(e, 0x29) = 0;
            EU32(e, 0x320) &= ~(u32)F_ALERTED;
            E8(e, 0x2B) |= 1;
        }
        if (((u32)E32(e, 0x354) & 1) != (g_frameCount & 1)) {
            u32 s = alert_slot(e);
            if (GV(s16, s) == 0) GV(s16, s) = 1;
        }
    }
    GCALL1(F_IN_FRONT, e);
    A0 = e;
    n_enemy_CheckWallToPlayer();
}

/* 8007FD44 (enemy, area): enemy types 0x16/0x17 catch a flagged shot
 * (sometimes): the shot's owner record takes the enemy's position and
 * facing. Then the area test for the given area. */
void n_enemy_GrabShot(void) {
    u32 e = A0, area = A1;
    FRAME(0x28);
    if ((u8)(E8(e, 0x315) - 0x16) < 2) {
        for (u32 i = 0; i < 4; i++) {
            u32 s = g_shots + i * 0x14;
            if (GV(u32, s + 4) == 0 || !(GV(u32, s + 0x10) & 0x10)) continue;
            if (GCALL0(F_SHOT_GRAB_OK) != 0 && (GCALL0(F_RAND) & 0xFF) >= 0x4C) continue;
            u32 o = GV(u32, s + 4);
            if (GV(s16, o + 0x31C) == 0) {
                GV(s16, o + 0x31C) = 1;
                GV(s32, GV(u32, s + 4) + 0x198) = E32(e, 0x198);
                GV(s32, GV(u32, s + 4) + 0x19C) = E32(e, 0x19C);
                GV(s32, GV(u32, s + 4) + 0x1A0) = E32(e, 0x1A0);
                GV(s32, GV(u32, s + 4) + 0x350) = ES16(e, 0x1C6);
                GV(s32, GV(u32, s + 4) + 0x35C) = E8(e, 0x314);
            }
            break;
        }
    }
    if (GCALL2(F_AREA_TEST, g_area, area) == 0) {
        u32 n = g_areaMisses;
        if (n < 0xFFFFFFFFu) g_areaMisses = n + 1;
    }
    if (area != 0) GCALL2(F_AREA_SET, g_area, area);
}

/* 8007FEBC: update every active enemy: animation change, distance, root
 * motion, its AI (unless a message box is up or a change is under way),
 * pushing, moving, awareness; then the aim target and the 3D sounds. */
void n_enemy_UpdateAll(void) {
    FRAME(0x28);
    u32 e = g_enemies;
    for (s32 i = 0; i < g_enemyCount; i++, e += ENEMY_SIZE) {
        if (E8(e, 0x2A) != 1 || E8(e, 0x29) == 0) continue;
        if (ES16(e, 0x85C) != 0) { A0 = e; n_enemy_AnimChangeStep(); }
        A0 = e; n_enemy_UpdateDistance();
        A0 = e; n_enemy_RootMotionStart();
        if (!(EU32(e, 0x320) & F_OFF)) {
            if (E8(e, 0x315) < 0x32) {
                if (g_msgState == 0) {
                    if (ES16(e, 0x85C) == 0) {
                        u32 table = ES16(e, 0x84E) == 0 ? g_aiTableA : g_aiTableB;
                        GCALL1(GV(u32, table + (u32)E8(e, 0x315) * 4), e);
                        A0 = e; n_enemy_NextState();
                    }
                    if (g_freezeEnemies != 0 && ES16(e, 0x316) != 4) {
                        s16 act = ES16(e, 0x850);
                        if (act != 0xE8 && act != 0x63 && act != 0x64 && act != 0xED) {
                            E32(e, 0x884) = 0;
                            E32(e, 0x880) = 0;
                            E32(e, 0x87C) = 0;
                        }
                    }
                }
                A0 = e; n_enemy_PushApart();
            }
            A0 = e; n_enemy_MoveStep();
        }
        A0 = e; n_enemy_Awareness();
    }
    n_aim_UpdateTarget();
    GCALL0(0x80081464u);
}

/* 800800A0: n_enemy_UpdateAll on a stack at the top of the scratchpad. */
void n_enemy_UpdateAllOnScratch(void) {
    FRAME(0x18);
    R[29] = 0x1F800400u;
    n_enemy_UpdateAll();
    R[29] = sp;
    V0 = 0x1F800400u;                         /* what the stack switch back returned */
}

/* ------------------------------------------------------------ aiming */

/* 800800E0 (enemy): keep the nearest aimable enemy as the aim target
 * (0x800A13D8 set when a target is picked from none). */
void n_aim_ConsiderTarget(void) {
    FRAME(0x20);
    u32 e = A0;
    if (ES16(e, 0x31E) < 0) { V0 = (u32)(s32)ES16(e, 0x31E); return; }
    if (EU32(e, 0x320) & F_NO_AIM) { V0 = F_NO_AIM; return; }
    u32 t = g_aimTarget;
    if ((s32)t >= 0) {
        g_aimTarget = e;
        g_aimTargetNew = 1;
        V0 = 1;
    } else if (E32(e, 0x868) < GV(s32, t + 0x868)) {
        g_aimTarget = e;
        V0 = 1;
    } else {
        V0 = 0;
    }
}

/* 80080178 (enemy): 1 if it can stay the target: alive, the player
 * standing still, and no wall in between. */
void n_aim_TargetVisible(void) {
    u32 e = A0;
    if (ES16(e, 0x31E) <= 0 || (g_playerMoveFlags & 0x3F)) { V0 = 0; return; }
    V0 = ES16(e, 0x8B8) == -1;
}

/* 800801FC: while locked on, keep the lock on the target; drop the target
 * when it can't stay one. */
void n_aim_UpdateTarget(void) {
    FRAME(0x20);
    u32 t = g_aimTarget;
    if ((s32)t < 0 && D_800BB19C == 1) {
        GS(s32, sp + 0x10) = 2;                 /* argument 5 */
        GCALL4(F_LOCK_ON, t, 0, (u32)GV(s32, t + 0x868), 2);
        A0 = g_aimTarget;
        n_aim_TargetVisible();
        if (V0 == 0) g_aimTarget = 0;
    }
}

/* ------------------------------------------------------------ animation requests */

#define ANIM_CHANGE_CLEAR 0xFFFF7FF3u          /* clears flags 0x4, 0x8 and 0x8000 */

/* Start a change to animation id (bit 15: change at once, else blend
 * over 0x180 frames). */
static u32 start_anim(u32 e, u32 id) {
    if (id & 0x8000) {
        EU16(e, 0x860) = (u16)(id & 0x7FFF);
        ES16(e, 0x85C) = 1;
        E32(e, 0x8B0) = 1;
    } else {
        ES16(e, 0x85C) = 1;
        E32(e, 0x8B0) = 0x180;
        EU16(e, 0x860) = (u16)id;
    }
    u32 f = EU32(e, 0x320) & ANIM_CHANGE_CLEAR;
    EU32(e, 0x320) = f;
    return f;
}

/* 80080268 (enemy, anim) */
void n_enemy_SetAnim(void) {
    u32 e = A0;
    V0 = start_anim(e, A1);
    ES16(e, 0x862) = 0;
}

/* 800802C8 (enemy, state, anim): queue a state; change the animation
 * unless it's the one playing. */
void n_enemy_SetStateAnim(void) {
    u32 e = A0;
    s32 cur = ES16(e, 0x85E);
    EU16(e, 0x31A) = (u16)A1;
    if (cur == (s32)A2) V0 = (u32)cur;
    else V0 = start_anim(e, A2);
    ES16(e, 0x862) = 0;
}

/* 80080334 (enemy, anim, blend frames) */
void n_enemy_SetAnimBlend(void) {
    u32 e = A0;
    ES16(e, 0x85C) = 1;
    EU16(e, 0x860) = (u16)A1;
    E32(e, 0x8B0) = (s32)A2;
    ES16(e, 0x862) = 0;
    u32 f = EU32(e, 0x320) & ~0xCu;
    EU32(e, 0x320) = f;
    V0 = f;
}

/* 8008035C (enemy, state, anim, blend frames) */
void n_enemy_SetStateAnimBlend(void) {
    u32 e = A0;
    EU16(e, 0x31A) = (u16)A1;
    EU16(e, 0x860) = (u16)A2;
    ES16(e, 0x85C) = 1;
    E32(e, 0x8B0) = (s32)A3;
    ES16(e, 0x862) = 0;
    EU32(e, 0x320) &= ANIM_CHANGE_CLEAR;
    V0 = 1;
}

/* 8008159C (enemy): give the player control back when the enemy says so
 * (flag 0x20000, cleared), or when it's the enemy holding the player and
 * nothing else locks the input. */
void n_enemy_ReleaseControl(void) {
    u32 e = A0;
    if (EU32(e, 0x320) & F_RELEASE) {
        D_80062DEC = 1;
        u32 f = EU32(e, 0x320) & ~(u32)F_RELEASE;
        EU32(e, 0x320) = f;
        V0 = f;
        return;
    }
    if (g_controlEnemy != e) { V0 = g_controlEnemy; return; }
    if (D_800BB19C == 3) { V0 = 3; return; }
    if (g_inputLocked != 0) { V0 = (u32)(s32)g_inputLocked; return; }
    D_80062DEC = 1;
    V0 = 1;
}

/* ------------------------------------------------------------ pushing apart */

/* 80081FF0's work. Two upright cylinders, radii 0x800BB044 + 0x800BB048,
 * centres a and b, within 800 in height: how far to push b out (x, z) and
 * whether they overlap (also in 0x800BB040). It has 80081FF0's stack frame
 * (the square root runs at the same stack depth as under the original). */
static int circle_push(s32 ax, s32 ay, s32 az, s32 bx, s32 by, s32 bz, s32 *ox, s32 *oz) {
    FRAME(0x30);
    s32 r = (s32)((u32)g_pushRadiusA + (u32)g_pushRadiusB);
    *ox = 0; *oz = 0;
    g_pushHit = 0;
    u32 r2 = (u32)r << 1;
    s32 dx = (s32)((u32)bx - (u32)ax);
    if (r2 < (u32)dx + (u32)r) return g_pushHit;    /* |dx| >= r */
    s32 dz = (s32)((u32)bz - (u32)az);
    if (r2 < (u32)dz + (u32)r) return g_pushHit;
    s32 d = (s32)GCALL1(F_ISQRT, (u32)mul32(dx, dx) + (u32)mul32(dz, dz));
    s32 depth = (s32)((u32)r - (u32)d);
    if (depth <= 0) return g_pushHit;
    s32 dy = (s32)((u32)by - (u32)ay);
    if (!(-800 < dy && dy < 800)) return g_pushHit;
    *ox = mips_div(mul32(dx, depth), d + 1);
    *oz = mips_div(mul32(dz, depth), d + 1);
    g_pushHit = 1;
    return 1;
}

/* 80081FF0 (a, b, out): circle_push on game vectors. */
void n_body_CirclePush(void) {
    u32 a = A0, b = A1, out = A2;           /* the frame is circle_push's */
    s32 ox, oz;
    MEM_W32(out + 8, 0);
    MEM_W32(out, 0);
    int hit = circle_push((s32)MEM_R32(a), (s32)MEM_R32(a + 4), (s32)MEM_R32(a + 8),
                          (s32)MEM_R32(b), (s32)MEM_R32(b + 4), (s32)MEM_R32(b + 8), &ox, &oz);
    if (hit) { MEM_W32(out, (u32)ox); MEM_W32(out + 8, (u32)oz); }
    V0 = (u32)g_pushHit;
}

/* An enemy pushed by another (or one of its spheres), axis by axis: take
 * the push into the step if that shortens it; otherwise, if the step heads
 * towards the other one, slow it to an eighth. */
static void push_from(u32 e, u32 o, s32 px, s32 pz) {
    /* x */
    s32 st = E32(e, 0x87C);
    s32 moved = (s32)((u32)st + (u32)px);
    if (abs32(moved) < abs32(st)) E32(e, 0x87C) = moved;
    else {
        s32 gap = (s32)((u32)E32(o, 0x198) - (u32)E32(e, 0x198));
        if (abs32(gap) < abs32((s32)((u32)gap + (u32)st))) {
            s32 v = abs32(st) >> 3;
            E32(e, 0x87C) = st >= 0 ? v : -v;
        }
    }
    /* z */
    st = E32(e, 0x884);
    moved = (s32)((u32)st + (u32)pz);
    if (abs32(moved) < abs32(st)) E32(e, 0x884) = moved;
    else {
        s32 gap = (s32)((u32)E32(o, 0x1A0) - (u32)E32(e, 0x1A0));
        if (abs32(gap) < abs32((s32)((u32)gap + (u32)st))) {
            s32 v = abs32(st) >> 3;
            E32(e, 0x884) = st >= 0 ? v : -v;
        }
    }
}

/* Each body sphere of e (when it has them) against point a. */
static int spheres_hit(u32 e, s32 ax, s32 ay, s32 az, s32 *px, s32 *pz, u32 part) {
    u32 c = GV(u32, part + 0x3BC);
    g_pushRadiusA = ES16(part, 0x392);
    return circle_push(ax, ay, az, GV(s32, c + 0x38), GV(s32, c + 0x3C), GV(s32, c + 0x40), px, pz);
}

/* 80081618 (enemy): keep a moving enemy out of the other enemies, the
 * player (flag 0x80000 while touching) and up to four props. */
void n_enemy_PushApart(void) {
    u32 e = A0;
    FRAME(0x60);
    u32 f = EU32(e, 0x320);
    if ((f & F_NO_PUSH2) || (f & F_NO_PUSH)) return;
    if (E32(e, 0x87C) == 0 && E32(e, 0x880) == 0 && E32(e, 0x884) == 0) return;

    u32 o = g_enemies;
    g_pushRadiusA = E32(e, 0x8C0);
    for (s32 i = 0; i < g_enemyCount; i++, o += ENEMY_SIZE) {
        if (e == o || E8(o, 0x2A) != 1 || E8(o, 0x29) == 0) continue;
        u32 of = EU32(o, 0x320);
        if ((of & F_NO_PUSH) || (of & F_OFF)) continue;
        s32 px, pz;
        g_pushRadiusB = E32(o, 0x8C0);
        if (circle_push(E32(o, 0x198), E32(o, 0x19C), E32(o, 0x1A0),
                        E32(e, 0x198), E32(e, 0x19C), E32(e, 0x1A0), &px, &pz))
            push_from(e, o, px, pz);
        if ((E8(e, 0x2B) & 4) && ES16(e, 0x392) >= 0) {
            for (u32 part = e; ES16(part, 0x392) >= 0; part += 0x5C) {
                if (ES16(part, 0x392) == 0) continue;
                if (spheres_hit(e, E32(o, 0x198), E32(o, 0x19C), E32(o, 0x1A0), &px, &pz, part))
                    push_from(e, o, px, pz);
            }
        }
    }

    /* the player */
    s32 px, pz;
    s32 plx = GV(s32, PLAYER_POS_A), ply = GV(s32, PLAYER_POS_A + 4), plz = GV(s32, PLAYER_POS_A + 8);
    g_pushRadiusB = 600;
    if (circle_push(plx, ply, plz, E32(e, 0x198), E32(e, 0x19C), E32(e, 0x1A0), &px, &pz)) {
        EU32(e, 0x320) |= F_TOUCH_PLAYER;
        E32(e, 0x87C) = (s32)((u32)E32(e, 0x87C) + (u32)px);
        E32(e, 0x884) = (s32)((u32)E32(e, 0x884) + (u32)pz);
    } else {
        EU32(e, 0x320) &= ~(u32)F_TOUCH_PLAYER;
    }
    if ((E8(e, 0x2B) & 4) && ES16(e, 0x392) >= 0) {
        for (u32 part = e; ES16(part, 0x392) >= 0; part += 0x5C) {
            if (ES16(part, 0x392) == 0) continue;
            if (spheres_hit(e, GV(s32, PLAYER_POS_A), GV(s32, PLAYER_POS_A + 4), GV(s32, PLAYER_POS_A + 8), &px, &pz, part)) {
                EU32(e, 0x320) |= F_TOUCH_PLAYER;
                E32(e, 0x87C) = (s32)((u32)E32(e, 0x87C) + (u32)px);
                E32(e, 0x884) = (s32)((u32)E32(e, 0x884) + (u32)pz);
                break;
            }
            EU32(e, 0x320) &= ~(u32)F_TOUCH_PLAYER;
        }
    }

    /* props */
    for (u32 i = 0; i < 4; i++) {
        u32 s = g_propSlots + i * 0x18;
        s32 k = GV(s32, s);
        if (k < 0) break;
        if (GV(s16, s + 0x16) < 0) continue;
        u32 p = g_props + (u32)k * 0x44;
        g_pushRadiusB = GV(s16, s + 0x16);
        if (circle_push(GV(s16, p + 0x10) << 2, 0, GV(s16, p + 0x18) << 4,
                        E32(e, 0x198), E32(e, 0x19C), E32(e, 0x1A0), &px, &pz)) {
            E32(e, 0x87C) = (s32)((u32)E32(e, 0x87C) + (u32)px);
            E32(e, 0x884) = (s32)((u32)E32(e, 0x884) + (u32)pz);
        }
    }
}

/* ------------------------------------------------------------ more aiming */

#define g_lockedEnemy    GV(u32, 0x800A8670)   /* the enemy locked on to (address) */
#define AIM_PARTS        0x800A1350u           /* per enemy type: s16 body part to aim at, s16 height */
#define F_PART_MATRIX    0x800339E8u   /* (coordinate, matrix out): its world matrix */
#define F_SET_MATRIX     0x8002CF14u   /* (matrix): GTE rotation and translation */
#define F_ROT_TRANS_PERS4 0x8002A9E8u  /* (v0, v1, v2, v3, xy0..xy3, [p], [flag]) */

/* 8007BC7C: when a target has just been picked, put the reticle on it:
 * the screen position of a point above the body part its type aims at.
 * (Not with 0x200000 pads; the request is dropped when there's nothing to
 * aim at.) */
void n_aim_SnapReticle(void) {
    FRAME(0x68);
    if (g_aimTargetNew == 0) return;
    if (g_padTypeBit == 0x200000) { g_aimTargetNew = 0; return; }
    u32 t = g_lockedEnemy;
    if (t == 0 || EU32(t, 0x16C) == 0) { g_aimTargetNew = 0; return; }
    u32 coord = 0;
    if (ES16(t, 0x392) >= 0) {
        s32 part = GV(s16, AIM_PARTS + (u32)E8(t, 0x315) * 4);
        u32 p = t;
        do {
            if (ES16(p, 0x39A) == part) coord = EU32(p, 0x3BC);
            p += 0x5C;
            if (ES16(p, 0x392) < 0) break;
        } while (coord == 0);
    }
    const u32 m = sp + 0x38, v = sp + 0x28, xy = sp + 0x30;
    GCALL2(F_PART_MATRIX, coord, m);
    GCALL1(F_SET_MATRIX, m);
    GS(s16, v + 0) = 0;
    GS(s16, v + 4) = 0;
    GS(u16, v + 2) = GV(u16, AIM_PARTS + (((u32)E8(g_lockedEnemy, 0x315) << 2) | 2));
    GS(u32, sp + 0x10) = xy;                    /* arguments 5-10 */
    GS(u32, sp + 0x14) = xy;
    GS(u32, sp + 0x18) = xy;
    GS(u32, sp + 0x1C) = xy;
    GS(u32, sp + 0x20) = sp + 0x58;
    GS(u32, sp + 0x24) = sp + 0x5C;
    GCALL4(F_ROT_TRANS_PERS4, v, v, v, v);
    g_reticleX = GS(s16, xy) + 0xA0;
    s32 y = GS(s16, xy + 2) + 0x78;
    g_reticleY = y;
    V0 = (u32)y;
}

/* ------------------------------------------------------------ more pushing */

/* 80081C80 (out): how far the enemies (and their body spheres) and the
 * props push the player: the sum of the pushes goes to out. Enemies that
 * aren't off get flag 0x40000 while they touch the player. */
void n_player_PushFromEnemies(void) {
    FRAME(0x70);
    u32 out = A0;
    const u32 pl = sp + 0x10, push = sp + 0x20, sph = sp + 0x30, prop = sp + 0x40;
    MEM_W32(out + 8, 0);
    MEM_W32(out + 4, 0);
    MEM_W32(out + 0, 0);
    GS(s32, pl) = GV(s32, PLAYER_POS_A);
    GS(s32, pl + 4) = GV(s32, PLAYER_POS_A + 4);
    GS(s32, pl + 8) = GV(s32, PLAYER_POS_A + 8);
    g_pushRadiusA = 600;
    u32 e = g_enemies;
    for (s32 i = 0; i < g_enemyCount; i++, e += ENEMY_SIZE) {
        if (E8(e, 0x2A) != 1 || E8(e, 0x29) == 0 || (EU32(e, 0x320) & F_NO_PUSH)) continue;
        GS(s32, push + 8) = 0; GS(s32, push + 4) = 0; GS(s32, push) = 0;
        g_pushRadiusB = E32(e, 0x8C0);
        if (!(EU32(e, 0x320) & F_OFF)) {
            A0 = e + 0x198; A1 = pl; A2 = push;
            n_body_CirclePush();
            if (V0 != 0) EU32(e, 0x320) |= 0x40000;
            else EU32(e, 0x320) &= ~0x40000u;
        }
        for (u32 k = 0; k < 3; k++) MEM_W32(out + k * 4, MEM_R32(out + k * 4) + GS(u32, push + k * 4));
        if (!(E8(e, 0x2B) & 4) || ES16(e, 0x392) < 0) continue;
        for (u32 part = e; ES16(part, 0x392) >= 0; part += 0x5C) {
            if (ES16(part, 0x392) == 0) continue;
            u32 c = EU32(part, 0x3BC);
            GS(s32, sph) = GV(s32, c + 0x38);
            GS(s32, sph + 4) = GV(s32, EU32(part, 0x3BC) + 0x3C);
            GS(s32, sph + 8) = GV(s32, EU32(part, 0x3BC) + 0x40);
            GS(s32, push + 8) = 0; GS(s32, push + 4) = 0; GS(s32, push) = 0;
            g_pushRadiusB = ES16(part, 0x392);
            if (!(EU32(e, 0x320) & F_OFF)) {
                A0 = sph; A1 = pl; A2 = push;
                n_body_CirclePush();
                if (V0 != 0) EU32(e, 0x320) |= 0x40000;
                else EU32(e, 0x320) &= ~0x40000u;
            }
            for (u32 k = 0; k < 3; k++) MEM_W32(out + k * 4, MEM_R32(out + k * 4) + GS(u32, push + k * 4));
        }
    }
    for (u32 i = 0; i < 4; i++) {
        u32 s = g_propSlots + i * 0x18;
        s32 k = GV(s32, s);
        if (k < 0) return;
        if (GV(s16, s + 0x16) <= 0) continue;
        u32 p = g_props + (u32)k * 0x44;
        GS(s32, prop + 4) = 0;
        GS(s32, prop) = (s32)((u32)(s32)GV(s16, p + 0x10) << 2);
        GS(s32, prop + 8) = (s32)((u32)(s32)GV(s16, p + 0x18) << 4);
        GS(s32, push + 8) = 0; GS(s32, push + 4) = 0; GS(s32, push) = 0;
        g_pushRadiusB = GV(s16, s + 0x16);
        A0 = prop; A1 = pl; A2 = push;
        n_body_CirclePush();
        for (u32 j = 0; j < 3; j++) MEM_W32(out + j * 4, MEM_R32(out + j * 4) + GS(u32, push + j * 4));
    }
}

/* ------------------------------------------------------------ more about enemies */

#define F_ROT_STEP_PL    0x8006F7A8u   /* (rotation, out, speed) */

/* 800824B0 (enemy): flag 0x40 while the enemy is in front of the player:
 * between the lines from the player towards yaw + 0x226 and yaw - 0x226
 * (about 48 degrees each side). */
void n_enemy_CheckInFront(void) {
    FRAME(0x50);
    u32 e = A0;
    const u32 left = sp + 0x10, right = sp + 0x20, rot = sp + 0x30;
    GS(u16, rot + 2) = GV(u16, g_player + 0x46);
    GS(u16, rot + 0) = GV(u16, g_player + 0x44);
    GS(u16, rot + 4) = GV(u16, g_player + 0x48);
    GS(u16, rot + 2) = (u16)(GV(u16, g_player + 0x46) + 0x226);
    GCALL3(F_ROT_STEP_PL, rot, right, 100);
    GS(s32, right) = (s32)(GS(u32, right) + GV(u32, g_player + 0x18));
    GS(s32, right + 8) = (s32)(GS(u32, right + 8) + GV(u32, g_player + 0x20));
    GS(u16, rot + 2) = (u16)(GS(u16, rot + 2) - 0x44C);
    GCALL3(F_ROT_STEP_PL, rot, left, 100);
    GS(s32, left) = (s32)(GS(u32, left) + GV(u32, g_player + 0x18));
    GS(s32, left + 8) = (s32)(GS(u32, left + 8) + GV(u32, g_player + 0x20));
    if (E8(e, 0x2A) != 1) { V0 = 1; return; }
    A0 = PLAYER_POS_A; A1 = right; A2 = EU32(e, 0x198); A3 = EU32(e, 0x1A0);
    n_geom_SideOfLine();
    int in_front = 0;
    if ((s32)V0 > 0) {
        A0 = PLAYER_POS_A; A1 = left; A2 = EU32(e, 0x198); A3 = EU32(e, 0x1A0);
        n_geom_SideOfLine();
        in_front = (s32)V0 < 0;
    }
    u32 f = in_front ? EU32(e, 0x320) | 0x40 : EU32(e, 0x320) & ~0x40u;
    EU32(e, 0x320) = f;
    V0 = f;
}

#define HIT_TABLE        0x800A23ACu   /* s16 per hit points (small enemies) */
#define HIT_CHANCES      0x800A23CCu   /* 15 words: chances (%) */

/* 800829D8 (enemy, which): a value from the enemy's attack table
 * (+0x840): entry which; for which 2 (the hit reaction) a value by its
 * hit points (small types from a shared table, at random once the hit
 * points are 15 or more). Bosses (type 0x32 and up) only answer 1 for 2. */
void n_enemy_AttackInfo(void) {
    FRAME(0x18);
    u32 e = A0;
    s32 which = (s32)A1;
    u32 type = E8(e, 0x315);
    if (type >= 0x32) { V0 = which == 2; return; }
    if (which != 2) { V0 = (u32)(s32)GV(s16, EU32(e, 0x840) + (u32)which * 2); return; }
    if (type < 0xB) {
        s32 hp = ES16(e, 0x31E);
        u32 idx;
        if (hp < 0xF) {
            idx = (u32)hp;
        } else {
            u32 r = GCALL0(F_RAND) & 0xFF;
            idx = 0;
            for (; idx < 15; idx++) {
                s32 w = GV(s32, HIT_CHANCES + idx * 4);
                if (!((s32)((u32)w * 51) / 10 < (s32)r)) break;
            }
        }
        V0 = (u32)(s32)GV(s16, HIT_TABLE + idx * 2);
    } else {
        s32 hp = ES16(e, 0x31E);
        u32 k = hp < 4 ? (u32)hp * 2 : (GCALL0(F_RAND) & 3) * 2;
        V0 = (u32)(s32)GV(s16, k + EU32(e, 0x840) + 4);
    }
}

/* 80082C38 (enemy, i): entry i of the enemy's parameter table (+0x844);
 * entries 5 and 6 get a random 0-15 added. */
void n_enemy_AnimParam(void) {
    FRAME(0x18);
    u32 e = A0;
    s32 i = (s32)A1;
    u32 a = (u32)i * 2 + EU32(e, 0x844);
    if (i < 7 && i >= 5) {
        u16 v = GV(u16, a);
        u32 r = GCALL0(F_RAND);
        V0 = (u32)(s32)(s16)(u16)(v + (r & 0xF));
    } else {
        V0 = (u32)(s32)GV(s16, a);
    }
}

/* 80082CA4 (enemy, n): move the enemy's parameter table on by n tables
 * (16 bytes each; not for bosses). */
void n_enemy_AdvanceParams(void) {
    u32 e = A0;
    if (E8(e, 0x315) < 0x32) {
        u32 v = EU32(e, 0x844) + (A1 << 4);
        EU32(e, 0x844) = v;
        V0 = v;
    } else {
        V0 = 0;
    }
}

/* 80082DEC (type): which animation track is an enemy type's main one. */
void n_enemy_MainTrack(void) { V0 = GV(u8, A0 + 0x800A2408u); }

/* 80082708 (enemy): reset the enemy's alert slot (byte +0x314, 0-4) to
 * state 0 at its position (x, z low halves); returns the slot, or -1. */
void n_enemy_MarkAlertSlot(void) {
    u32 e = A0;
    u8 i = GV(u8, e + 0x314);
    if (i >= 5) { V0 = (u32)-1; return; }
    u32 s = g_alertSlots + (u32)i * 12;
    GV(u16, s) = 0;
    GV(u16, s + 2) = GV(u16, e + 0x198);
    GV(u16, s + 6) = GV(u16, e + 0x1A0);
    V0 = GV(u8, e + 0x314);
}

void n_enemy_CountAlive(void);
void n_enemy_ShowActive(void);

/* 80085CF0: how many enemies (not bosses: type < 0x32) are still alive:
 * hit points above 0 and their room flag (+0x850) not set. (Compiled
 * without optimisation; the locals' stack slots aren't kept.) */
void n_enemy_CountAlive(void) {
    FRAME(0x28);
    u32 e = GV(u32, 0x800AB510);
    s32 n = 0;
    for (s32 i = 0; i < GV(s32, 0x800AF530); i++, e += 0x8C4) {
        if (GV(u8, e + 0x315) < 0x32 && !GCALL2(0x8008F64Cu, 0x80062F60u, (u32)(s32)GV(s16, e + 0x850)) &&
            GV(s16, e + 0x31E) > 0)
            n++;
        RT_POLL_NOSAVE(0x80085DE0);
    }
    V0 = (u32)n;
}

/* 8007CF08: show again (+0 = 1) every active enemy (+0x2A == 1) not
 * switched off (+0x320 bits 0 and 0x100000). After picking up an item. */
void n_enemy_ShowActive(void) {
    u32 e = GV(u32, 0x800AB510);
    for (s32 i = 0; i < GV(s32, 0x800AF530); i++, e += 0x8C4) {
        if (i != 0) RT_POLL_NOSAVE(0x8007CF74);
        if (GV(u8, e + 0x2A) == 1 && !(GV(u32, e + 0x320) & 0x100001)) GV(u8, e) = 1;
    }
}
