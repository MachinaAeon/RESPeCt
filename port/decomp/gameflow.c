/* Game flow (TEST overlay): the per-frame dispatcher, its handlers, and
 * the game over sequence.
 *
 * Every game frame runs one handler from the table at 0x800A28E0, chosen
 * by g_flowHandler (0x800BB19C); each keeps its progress in g_flowStep
 * (0x800A28CC):
 *   0  starting a stage: straight on to 1
 *   1  playing (counts down g_actLock, after which the player can act)
 *   2  an enemy has hold of the player (camera shake, damage, let go)
 *   3  game over: the fall, the fade, the RETRY / SAVE menu, and
 *      "continue" (restore the checkpoint) or save (the save screen)
 *   4, 6  nothing; 5  not reached in testing
 * The dispatcher returns 1 once the handler is 6 or more (leave the stage
 * loop).
 *
 * A full-screen overlay (two GPU sprites per buffer, 0x800BB080) dims the
 * screen for game over; it has its own small state machine (0x800A0CB4,
 * table 0x800A2900) run every frame after the handler.
 *
 * Player damage: hits are queued (up to five a frame) in the player record
 * at 0x800A8584 and applied together.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"

void n_flow_StageStart(void);
void n_flow_Playing(void);
void n_flow_Grabbed(void);
void n_flow_GameOver(void);
void n_flow_GrabStart(void);
void n_flow_Frame(void);
void n_player_FacingAngle(void);
void n_player_StepSound(void);
void n_player_AddHit(void);
void n_player_ApplyHits(void);
void n_player_TakeHits(void);
void n_player_HitsWouldKill(void);
void n_dim_Init(void);
void n_dim_Idle(void);
void n_dim_Hold(void);
void n_dim_Draw(void);
void n_dim_Frame(void);

/* ---- state ---- */
#define g_flowHandler   GV(s32, 0x800BB19C)
#define g_flowStep      GV(s32, 0x800A28CC)
#define g_flowEnemy     GV(u32, 0x800BB198)   /* the enemy holding the player (address) */
#define g_flowFlags     GV(u32, 0x800BB070)   /* bit 0: let go / retry chosen */
#define g_flowTimer     GV(s32, 0x800BB074)
#define g_flowSpeed     GV(s32, 0x800BB078)
#define g_flowVar       GV(s32, 0x800BB07C)
#define g_actLock       GV(s32, 0x800A28D0)   /* frames until the player can act */
#define g_canAct        GV(u8, 0x80062DEC)
#define g_menuState     GV(s16, 0x800A55A2)   /* fade / menu: 4 = done */
#define g_camPitch      GV(u16, 0x800A865C)
#define g_camRoll       GV(u16, 0x800A865E)
#define g_camBob        GV(s32, 0x800A8634)
#define g_camShake      GV(s32, 0x800B3EF8)
#define g_camDist       GV(s32, 0x800B3F00)
#define g_gameType      GV(u8, 0x80062DF4)
#define g_endKind       GV(u8, 0x80062DFC)
#define g_cameFromStage GV(s32, 0x800A866C)
#define g_difficulty    GV(s32, 0x80062DF8)   /* 1: hits do half damage */

/* the screen dimmer */
#define g_dimOn         GV(s32, 0x800A0CB0)
#define g_dimState      GV(s32, 0x800A0CB4)
#define g_dimX          GV(s16, 0x800A28D4)
#define g_dimY          GV(s16, 0x800A28D6)
#define g_dimShade      GV(s32, 0x800BB158)
#define g_dimFade       GV(s32, 0x800BB15C)
#define g_dimCode       GV(u32, 0x800BB160)
#define g_dimScale      GV(s32, 0x800BB164)
#define g_dimSpin       GV(s32, 0x800BB168)
#define DIM_SPRITES     0x800BB080u           /* [2 buffers][2] sprites, 0x24 bytes */
#define DIM_MODES       0x800BB170u           /* [2 buffers] draw-mode packets, 0x10 bytes */
#define g_buffer        GV(u8, 0x80062D1C)    /* double buffer index */
#define ORDER_TABLES    0x800AFC88u           /* [2] ordering tables, 0x14 bytes */
#define g_screenOffX    GV(u16, 0x80062E18)
#define g_screenOffY    GV(u16, 0x80062E1A)

/* the player record */
#define PLR_HP          GV(s16, 0x800A8584)
#define PLR_POISONED    GV(u8, 0x800A8587)
#define PLR_HIT_LAST    GV(s16, 0x800A8588)   /* index of the last queued hit, -1: none */
#define PLR_HIT_A       GA(u8, 0x800A858A)
#define PLR_HIT_SIDE    GA(u8, 0x800A858F)
#define PLR_HIT_DAMAGE  GA(s16, 0x800A8594)
#define PLR_HIT_FLAGS   GA(u16, 0x800A859E)   /* 0x4000: can poison */
#define PLR_ROT_ADDR    0x800A8604u
#define PLR_POS_ADDR    0x800A85D8u
#define PLR_X           GV(s32, 0x800A85D8)
#define PLR_Z           GV(s32, 0x800A85E0)

/* ---- game functions ---- */
#define F_DIM_FRAME        0x80085C94u
#define F_FLOW_TAIL        0x80082608u
#define F_CAMERA_SHAKE     0x800825E4u
#define F_SCREEN_FLASH     0x800731BCu   /* (r, g, b, ?) */
#define F_ENEMY_ATTACK     0x800829D8u   /* (enemy, attack): its flags / damage */
#define F_ENEMY_REACT      0x80078D38u   /* (enemy, 0, distance, 2, 0) */
#define F_MOVE_DIR         0x8008286Cu   /* (rot, out vector, speed) */
#define F_MUSIC_FADE       0x80011EC4u
#define F_SOUND_STOP       0x8001215Cu
#define F_FADE_OUT         0x80090BF0u
#define F_FADE_IN          0x80090B90u
#define F_GAMEOVER_TEXT    0x8007A874u
#define F_MENU_OPEN        0x8007ABF0u
#define F_MENU_BUILD       0x8007AC80u
#define F_MENU_CHOICE      0x8007B2A8u   /* 1: save, 2: continue */
#define F_SET_DISP_MASK    0x800352DCu
#define F_MODE_SWITCH      0x8001306Cu
#define F_RESTORE_CHECKPOINT 0x80071CF4u
#define F_ADD_PRIM         0x8002BF28u   /* (packet, ot, n) */
#define F_ADD_PRIM2        0x8002BDA8u
#define F_RATAN2           0x8002BB48u
#define F_SOUND_LOAD       0x80011CB4u   /* (slot, bank file, buffer) */
#define F_SOUND_AT         0x80080524u   /* (bank, sound, pitch, sound#, left, right) */
#define F_MAKE_CHECKPOINT  0x800717E8u
#define F_HUD_RESET        0x80092DD0u
#define F_RAND             0x80011570u

/* 80083000, handler 0: on to the next handler (1, playing). */
void n_flow_StageStart(void) {
    FRAME(0x8);
    g_flowStep = 0;
    g_flowFlags = 0;
    g_flowHandler++;
}

/* 80083048, handler 1: playing. Counts g_actLock down; the player can act
 * when it reaches 0 (a negative count is cleared at once). */
void n_flow_Playing(void) {
    FRAME(0x8);
    s32 t = g_actLock;
    if (t > 0) {
        g_actLock = --t;
        if (t == 0) g_canAct = 1;
    } else if (t < 0) {
        g_actLock = 0;
        g_canAct = 1;
    }
}

/* 800830F8, handler 2: an enemy (g_flowEnemy) has hold of the player. The
 * screen flashes and the camera shakes and tilts (g_camRoll down to 100
 * and up to 300, three times), then the enemy is told how the attack went,
 * and when it lets go the queued damage is applied and play resumes. */
void n_flow_Grabbed(void) {
    FRAME(0x20);
    switch ((u32)g_flowStep) {
    case 0:
        g_flowTimer = 0;
        g_flowSpeed = -0x32;
        g_flowStep++;
        GCALL4(F_SOUND_CUE, 0, 8, 0x7F, 0x7F);
        g_canAct = 0;
        GCALL4(F_SCREEN_FLASH, 0xFF, 0, 0x20, 0);
        /* fall through */
    case 1:
        g_camShake -= 0xFA;
        if (g_camShake < -1000) g_camShake = -1000;
        /* fall through */
    case 2:
        if (g_flowTimer >= 3) {
            g_flowStep = 5;
        } else if ((g_flowTimer & 1) == 0) {
            u16 r = (u16)(g_camRoll + (u16)g_flowSpeed);
            g_camRoll = r;
            if ((s16)r < 100) {
                g_camRoll = 100;
                g_flowTimer++;
            }
            GCALL0(F_CAMERA_SHAKE);
        } else {
            u16 r = (u16)(g_camRoll - (u16)g_flowSpeed);
            g_camRoll = r;
            if ((s16)r >= 0x12D) {
                g_camRoll = 0x12C;
                g_flowTimer++;
            }
        }
        break;
    case 3:
        if ((g_flowTimer & 1) == 0) {
            u16 r = (u16)(g_camRoll - 0x32);
            g_camRoll = r;
            if ((s16)r < 0) {
                g_camRoll = 0;
                g_flowStep++;
            }
            GCALL0(F_CAMERA_SHAKE);
        } else {
            u16 r = (u16)(g_camRoll + 10);
            g_camRoll = r;
            if ((s16)r > 0) {
                g_camRoll = 0;
                g_flowStep++;
            }
        }
        g_camShake += 0xFA;
        if (g_camShake >= 0x1F5) g_camShake = 0x1F4;
        break;
    case 4:
        g_flowStep++;
        break;
    case 5: {
        g_camPitch = 0;
        g_camRoll = 0;
        g_camShake = 0x1F4;
        u32 r = GCALL2(F_ENEMY_ATTACK, g_flowEnemy, 9);
        if ((r & 0x1000) == 0) {
            GS(u32, sp + 0x10) = 0;                         /* argument 5 */
            GCALL4(F_ENEMY_REACT, g_flowEnemy, 0, GV(u32, g_flowEnemy + 0x868), 2);
        } else {
            D_800A8670 = 0;
        }
        g_flowStep++;
        break;
    }
    case 6:
        if (D_800A8670 == 0) {
            if (g_flowFlags & 1) {
                g_flowHandler = 1;
                g_flowStep = 0;
            } else {
                g_flowStep++;
            }
        }
        break;
    case 7: {
        u32 r = GCALL2(F_ENEMY_ATTACK, g_flowEnemy, 9);
        if (r & 0x8000) {
            u32 e = g_flowEnemy;
            GV(s16, e + 0x324) = 0;          /* its hits this frame */
            GV(u8, e + 0x326) = 1;
            GV(u8, e + 0x32B) = 0;
            GV(s16, e + 0x330) = 0;
            GV(s16, e + 0x33A) = 0;
            GV(u32, e + 0x320) |= 0x10;
            GV(u32, e + 0x320) |= 0x1000;
        }
        g_flowStep++;
    }
        /* fall through */
    case 8:
        g_flowStep = 0;
        g_flowHandler = 1;
        g_actLock = 0;
        g_canAct = 1;
        GCALL0(0x80085100u);                 /* apply the damage */
        g_flowEnemy = 0;
        break;
    default:
        break;
    }
}

/* Stage totals (shots, hits, kills) take in the stage's counts. */
static void add_stage_stats(void) {
    GV(u32, 0x80063460) += GV(u32, 0x80063448);
    GV(u32, 0x80063464) += GV(u32, 0x8006344C);
    GV(u32, 0x80063468) += GV(u32, 0x80063450);
}

/* 8008369C, handler 3: game over. The player falls (camera pitches down and
 * drops to the floor, sliding back), the screen dims and spins away, then
 * the GAME OVER text and the menu: save (step 15: make the checkpoint and
 * switch to TITLE's save screen; the payphones open it here too) or
 * continue (16: restore the checkpoint). 17 is like 15 for another ending. */
void n_flow_GameOver(void) {
    FRAME(0x30);
    switch ((u32)g_flowStep) {
    case 0:
        g_dimOn = 1;
        g_dimState = 3;
        g_dimCode = 0x52000000;
        g_dimX = 0;
        g_dimY = 0;
        g_dimShade = 0x28;
        g_dimFade = 0;
        g_dimScale = 0x1140;
        g_dimSpin = 0;
        GV(s16, 0x800C09D4) = 0x400;
        GCALL2(F_MUSIC_FADE, (s32)GV(s16, 0x80062E78), 0x800);
        g_canAct = 0;
        g_camPitch = g_camPitch & 0xFFF;
        g_flowTimer = 0x18;
        g_flowSpeed = 0x50;
        g_flowVar = 0xB4;
        g_flowStep++;
        GCALL4(F_SOUND_CUE, 0, 10, 0x7F, 0x7F);
        break;
    case 1: {
        if (g_flowTimer != 0) {
            g_flowTimer--;
            g_camPitch = (u16)(g_camPitch - 0x20);
        }
        g_camBob += g_flowSpeed;
        if (g_camBob >= -0xC7) g_flowStep++;
        /* slide back */
        u32 v = sp + 0x10;                           /* move_Collide keeps its address */
        GCALL3(F_MOVE_DIR, PLR_ROT_ADDR, v, g_flowVar);
        if (GCALL3(F_MOVE_COLLIDE, PLR_POS_ADDR, v, 0) == 0) {
            PLR_X += GS(s32, v);
            PLR_Z += GS(s32, v + 8);
        } else {
            PLR_X = GS(s32, v);
            PLR_Z = GS(s32, v + 8);
        }
        g_flowVar = g_flowVar - 1 - g_flowTimer;
        break;
    }
    case 2:
        g_flowTimer = 0x5A;
        g_flowStep++;
        break;
    case 3:
        if (g_flowTimer != 0) {
            g_flowTimer--;
        } else {
            g_flowTimer = 0x3C;
            g_flowStep++;
        }
        break;
    case 4:
        /* the dim grows and darkens, then spins away */
        g_dimShade += 4;
        g_dimScale += 0xE;
        if (g_dimShade >= 0x61) {
            g_camDist += g_dimSpin;
            g_dimSpin += 0x400;
            if (g_dimSpin > 0x2D000) g_dimSpin = 0x2D000;
        }
        if (g_dimShade >= 0x80) g_dimShade = 0x7F;
        if (g_flowTimer < 0x20) {
            g_dimFade = (0x20 - g_flowTimer) << 3;
            if (g_dimFade >= 0x100) g_dimFade = 0xFF;
        }
        if (g_flowTimer != 0) g_flowTimer--;
        else g_flowStep++;
        break;
    case 5:
        GCALL2(F_FADE_OUT, 0, 0x10);
        g_flowStep++;
        /* fall through */
    case 6:
        if (g_menuState == 4) {
            g_dimOn = 0;
            g_dimState = 1;
            GV(u32, 0x800AB524) = 0;
            g_flowStep++;
        }
        break;
    case 7:
        /* clear the field: sounds, HUD, shots; load the game over screen */
        g_canAct = 0;
        GCALL1(F_SOUND_STOP, 0);
        GCALL1(F_SOUND_STOP, 1);
        GCALL0(0x8006D180u);
        GCALL0(0x8009453Cu);                 /* hud_FreePaused */
        GCALL1(0x8001E118u, 0);
        GCALL0(0x800809B4u);
        GCALL0(0x8008E4A0u);                 /* shots_Clear */
        GV(u32, 0x800AF530) = 0;
        GV(s16, 0x800B3CC2) = 0;
        GV(u8, 0x800BBD10) = 0;
        GCALL0(0x8007A4F8u);
        GCALL0(0x8007A540u);
        GCALL1(0x8002D818u, 0x10E);
        GCALL1(0x80011DCCu, 6);
        GCALL3(F_SOUND_LOAD, 6, 0x10, 0x800D28B0u);
        GCALL2(F_FADE_IN, 0, 8);
        g_flowTimer = 0;
        g_flowStep++;
        /* fall through */
    case 8:
        if (g_menuState == 0) g_flowStep++;
        /* fall through */
    case 9:
        /* GAME OVER: on, after 40 frames, OK fades out */
        GCALL0(F_GAMEOVER_TEXT);
        if ((g_buttons & BTN_FIRE) && !(g_buttonsPrev & BTN_FIRE) && g_flowTimer >= 0x28) {
            GCALL2(F_FADE_OUT, 0, 8);
            g_flowStep++;
        } else if (g_flowTimer < 0x28) {
            g_flowTimer++;
        }
        break;
    case 10:
        GCALL0(F_GAMEOVER_TEXT);
        if (g_menuState == 4) g_flowStep++;
        break;
    case 11:
        GCALL0(F_MENU_OPEN);
        GCALL0(F_MENU_BUILD);
        GCALL2(F_FADE_IN, 0, 8);
        g_flowStep++;
        /* fall through */
    case 12:
        if (g_menuState == 0) g_flowStep++;
        /* fall through */
    case 13: {
        s32 choice = (s32)GCALL0(F_MENU_CHOICE);
        if (choice == 1 || choice == 2) {
            GCALL2(F_FADE_OUT, 0, 8);
            g_flowStep++;
        }
        break;
    }
    case 14: {
        s32 choice = (s32)GCALL0(F_MENU_CHOICE);
        if (g_menuState == 4) {
            if (choice == 1) g_flowStep++;          /* 15: save */
            else g_flowStep += 2;                   /* 16: continue */
        }
        break;
    }
    case 15:
        GCALL1(F_SET_DISP_MASK, 0);
        g_endKind = 2;
        add_stage_stats();
        GCALL0(F_MAKE_CHECKPOINT);
        GCALL1(F_MODE_SWITCH, 1);
        break;
    case 16:
        GCALL1(F_SET_DISP_MASK, 0);
        g_gameType = 1;
        GCALL0(F_RESTORE_CHECKPOINT);
        g_cameFromStage = -1;
        GV(s32, 0x800B3F08) = 0;
        GCALL0(0x800809B4u);
        GCALL1(0x80011DCCu, 1);
        GV(s16, 0x80062E7C) = -1;
        GV(s16, 0x80062E78) = -1;
        GCALL0(0x8006DD5Cu);
        GCALL0(F_HUD_RESET);
        GCALL0(0x8009493Cu);
        GCALL0(0x80098AECu);
        g_flowHandler = 6;
        break;
    case 17:
        GCALL1(F_SET_DISP_MASK, 0);
        g_endKind = 4;
        add_stage_stats();
        GCALL0(F_MAKE_CHECKPOINT);
        GCALL1(F_MODE_SWITCH, 1);
        break;
    default:
        break;
    }
    if (g_flowStep < 7) GCALL0(F_CAMERA_SHAKE);
}

/* 8008445C (enemy): an enemy grabs the player: handler 2 with it. If the
 * hits queued so far would kill, it's game over at once (returns 1). */
void n_flow_GrabStart(void) {
    FRAME(0x18);
    g_flowHandler = 2;
    g_flowEnemy = A0;
    g_flowFlags &= ~1u;
    n_player_HitsWouldKill();
    if (V0 == 0) { V0 = 0; return; }
    PLR_HP = 0;
    g_flowHandler = 3;
    g_flowStep = 0;
    V0 = 1;
}

/* 8008480C: the game frame's flow: the handler, then the common tail and
 * the dimmer. Returns 1 once the handler is past 5 (end of the stage). */
void n_flow_Frame(void) {
    FRAME(0x28);
    u32 handler = GV(u32, 0x800A28E0u + (u32)g_flowHandler * 4);
    gcall(handler, 0, 0, 0, 0);
    GCALL0(F_FLOW_TAIL);
    GCALL0(F_DIM_FRAME);
    V0 = g_flowHandler < 6 ? 0 : 1;
}

/* 800849D0 (obj, pos): the angle of pos from obj's facing direction
 * (0x1000 a turn; negative to the right). */
void n_player_FacingAngle(void) {
    FRAME(0x40);
    u32 obj = A0, pos = A1;
    u32 dir = sp + 0x10;
    GCALL3(F_ROT_STEP, obj + 0x44, dir, -0x50);
    s16 dx = (s16)(GS(u16, pos) - GS(u16, obj + 0x18));
    s16 dz = (s16)(GS(u16, pos + 8) - GS(u16, obj + 0x20));
    s32 vx = GS(s32, dir), vz = GS(s32, dir + 8);
    u32 along = (u32)((s32)dx * vx) + (u32)((s32)dz * vz);
    u32 across = (u32)((s32)dx * vz) - (u32)((s32)dz * vx);
    u32 a = GCALL2(F_RATAN2, across, along);
    V0 = (u32)(s32)(s16)(-(u16)a);
}

/* 80084B04: a footstep: a random sound from the player's bank (0x800BB194),
 * pitched a little at random, alternating feet (0x800A28FC), louder and
 * different running backwards. */
void n_player_StepSound(void) {
    FRAME(0x38);
    u32 bank = GV(u32, 0x800BB194);
    s32 sound = (s32)(GCALL0(F_RAND) & 0x3F);
    s32 r = (s32)(GCALL0(F_RAND) & 7);
    s32 wobble = (r & 4) ? -(r & 3) : (r & 3);
    s32 foot = GV(s32, 0x800A28FC);
    s32 vol = GV(s16, 0x80062DB6);
    s32 pitch, loud;
    if (g_playerMoveSpeed < -0x50) {
        pitch = 0x3D - foot;
        loud = (s32)((u32)(wobble + 0x7A) * (u32)vol) / 100;
    } else {
        pitch = 0x3C - foot * 2;
        loud = (s32)((u32)(wobble + 0x60) * (u32)vol) / 100;
    }
    GS(s32, sp + 0x10) = loud;                       /* arguments 5 and 6: left, right */
    GS(s32, sp + 0x14) = loud;
    GCALL4(F_SOUND_AT, 6, bank, pitch, sound);
    GV(s32, 0x800A28FC) = (GV(s32, 0x800A28FC) + 1) & 1;
}

/* 80084D84 (enemy, attack, flags): queue a hit on the player: which side it
 * came from, the attack's damage (half on the easy setting). Returns 0 if
 * five are queued already. A 0x4000 (poisoning) attack from enemy type
 * 0x13 doesn't poison. */
void n_player_AddHit(void) {
    FRAME(0x20);
    u32 enemy = A0, attack = A1, flags = A2;
    if (PLR_HIT_LAST >= 5) { V0 = 0; return; }
    s16 i = (s16)(PLR_HIT_LAST + 1);
    PLR_HIT_LAST = i;
    PLR_HIT_A[i] = 0;
    GCALL1(0x8008216Cu, enemy);              /* enemy_HitSide */
    PLR_HIT_SIDE[i] = (u8)V0;
    if ((flags & 0x4000) && GV(u8, enemy + 0x315) == 0x13) flags &= ~0x4000u;
    PLR_HIT_FLAGS[i] = (u16)flags;
    PLR_HIT_DAMAGE[i] = (s16)GCALL2(F_ENEMY_ATTACK, enemy, attack);
    if (g_difficulty == 1) PLR_HIT_DAMAGE[i] = (s16)(PLR_HIT_DAMAGE[i] / 2);
    V0 = 1;
}

/* 80084F24 (apply): the queued hits. Applying, the damage comes off the
 * player's health (a poisoning hit poisons 76 times in 256) and the queue
 * empties; returns 1 if that killed (health then 0). Not applying, returns
 * whether it would. */
void n_player_ApplyHits(void) {
    FRAME(0x20);
    s32 apply = (s32)A0;
    s32 hp = PLR_HP;
    for (s32 i = 0; i <= PLR_HIT_LAST; i++) {
        if (apply) {
            PLR_HP = (s16)(GV(u16, 0x800A8584) - (u16)PLR_HIT_DAMAGE[i]);
            if ((s16)(PLR_HIT_FLAGS[i] & 0x4000)) {
                if ((GCALL0(F_RAND) & 0xFF) < 0x4C) PLR_POISONED = 1;
            }
        } else {
            hp -= PLR_HIT_DAMAGE[i];
        }
    }
    if (apply) {
        PLR_HIT_LAST = -1;
        if (PLR_HP > 0) { V0 = 0; return; }
        PLR_HP = 0;
        V0 = 1;
        return;
    }
    V0 = hp > 0 ? 0 : 1;
}

/* 80085100: apply the queued hits. */
void n_player_TakeHits(void) {
    FRAME(0x18);
    A0 = 1;
    n_player_ApplyHits();
}

/* 80085144: would the queued hits kill? */
void n_player_HitsWouldKill(void) {
    FRAME(0x18);
    A0 = 0;
    n_player_ApplyHits();
}

/* 80085188, dimmer state 0: reset the sprites (code 0x42000000 = a
 * semi-transparent rectangle, grey 0x80, scale 0x1000) and draw-mode
 * packets, centre the dimmer, then state 1. */
void n_dim_Init(void) {
    FRAME(0x10);
    u32 p = DIM_SPRITES;
    for (int i = 0; i < 6; i++, p += 0x24) {
        GV(u32, p) = 0x42000000;
        GV(s16, p + 0x12) = 0;
        GV(s16, p + 0x10) = 0;
        GV(u8, p + 0x16) = 0x80;
        GV(u8, p + 0x15) = 0x80;
        GV(u8, p + 0x14) = 0x80;
        GV(s16, p + 0x1A) = 0;
        GV(s16, p + 0x18) = 0;
        GV(s16, p + 0x1E) = 0x1000;
        GV(s16, p + 0x1C) = 0x1000;
        GV(u32, p + 0x20) = 0;
    }
    g_dimX = -0xA0;
    g_dimY = -0x78;
    g_dimCode = 0x42000000;
    g_dimShade = 0x80;
    g_dimScale = 0x1000;
    for (int i = 0; i < 2; i++) {
        u32 m = DIM_MODES + (u32)i * 16;
        GV(u32, m) = 0x60000000;
        GV(s16, m + 8) = 0x140;
        GV(s16, m + 10) = 0xF0;
        GV(s16, m + 4) = -0xA0;
        GV(s16, m + 6) = -0x78;
        GV(u8, m + 12) = 0;
        GV(u8, m + 13) = 0;
        GV(u8, m + 14) = 0;
    }
    g_dimState++;
}

/* 800853F8, dimmer state 1: off. */
void n_dim_Idle(void) {
    FRAME(0x8);
}

/* One of the dimmer's two sprites for this frame (left half at u 0, right
 * at u -0x100 offset), scaled about the screen centre. */
static void dim_sprite(u32 p, s16 w, s16 u0, s16 tpage_add) {
    GV(u32, p) = g_dimCode;
    s32 sx = g_dimScale * 0xA0;
    s32 sy = g_dimScale * 0x78;
    GV(s16, p + 4) = (s16)((u16)(g_dimX - g_screenOffX) - (u16)(sx / 4096));
    GV(s16, p + 6) = (s16)((u16)(g_dimY - g_screenOffY) - (u16)(sy / 4096));
    GV(s16, p + 8) = w;
    GV(s16, p + 10) = 0xF0;
    GV(s16, p + 12) = (s16)(g_buffer * 16 + tpage_add);
    GV(u8, p + 14) = 0;
    GV(u8, p + 15) = 0;
    u8 shade = (u8)g_dimShade;
    GV(u8, p + 0x16) = shade;
    GV(u8, p + 0x15) = shade;
    GV(u8, p + 0x14) = shade;
    GV(s16, p + 0x18) = u0;
    GV(s16, p + 0x1A) = 0;
    u16 sc = (u16)g_dimScale;
    GV(u16, p + 0x1E) = sc;
    GV(u16, p + 0x1C) = sc;
    GCALL3(F_ADD_PRIM, p, ORDER_TABLES + (u32)g_buffer * 0x14, 1);
}

/* 80085418, dimmer state 2: the captured picture held as it is: this
 * buffer's two sprites unscaled at the dimmer's place, u/v from
 * 0x800A28D8 / 0x800A28DA. */
void n_dim_Hold(void) {
    FRAME(0x20);                                     /* unoptimised: p kept at sp+0x10 */
    for (u32 half = 0; half < 2; half++) {
        u32 p = DIM_SPRITES + (u32)g_buffer * 0x48 + half * 0x24;
        GS(u32, sp + 0x10) = p;
        GV(u16, p + 4) = (u16)(g_dimX - (half ? (u16)(g_screenOffX - 0x100) : g_screenOffX));
        GV(u16, p + 6) = (u16)(g_dimY - g_screenOffY);
        GV(u16, p + 8) = half ? 0x40 : 0x100;
        GV(u16, p + 10) = 0xF0;
        GV(u16, p + 12) = (u16)(g_buffer * 16 + (half ? 4 : 0));
        GV(u8, p + 14) = GV(u8, 0x800A28D8);
        GV(u8, p + 15) = GV(u8, 0x800A28DA);
        GCALL3(F_ADD_PRIM, p, ORDER_TABLES + (u32)g_buffer * 0x14, 0);
    }
}

/* 80085814, dimmer state 3: draw this buffer's two sprites and the fade
 * rectangle. */
void n_dim_Draw(void) {
    FRAME(0x20);
    u32 p = DIM_SPRITES + (u32)g_buffer * 0x48;
    dim_sprite(p, 0x100, 0, 0);
    dim_sprite(p + 0x24, 0x40, -0x100, 4);
    u32 m = DIM_MODES + (u32)g_buffer * 16;
    u8 fade = (u8)g_dimFade;
    GV(u8, m + 14) = fade;
    GV(u8, m + 13) = fade;
    GV(u8, m + 12) = fade;
    GCALL3(F_ADD_PRIM2, m, ORDER_TABLES + (u32)g_buffer * 0x14, 1);
}

/* 80085C94: run the dimmer's state. */
void n_dim_Frame(void) {
    FRAME(0x20);
    gcall(GV(u32, 0x800A2900u + (u32)g_dimState * 4), 0, 0, 0, 0);
}
