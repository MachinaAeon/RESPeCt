/* The room script's ops (TEST overlay): the event scripts that run the
 * game's scenes (messages, sounds, flags, characters, the camera).
 *
 * Up to 10 script threads run at 0x800BB7E0 (0x58 bytes each):
 *   +0x00 u32 pc (the script: s16 codes, an op and its operands)
 *   +0x04 s16 step (an op's progress; 0 when an op starts)
 *   +0x08 u16 flags (bit 0: running)
 *   +0x0A.. s16 work for the op (counters, steps per frame)
 *   +0x1C the op's object (an address, or not negative for none)
 *   +0x54 skippable (the scene can be skipped with the confirm button)
 * Every frame each running thread runs ops from the table at 0x800A2B8C,
 * by the code at pc, until one returns 1 (wait, run it again next frame):
 * 0 moves on to the next thread, 2 runs the next op at once (the op moves
 * pc past itself when it's done). 0x800BBB58 counts the running threads.
 *
 * "Actors" are the enemy records (0x8C4 bytes at 0x800AB510) the scripts
 * move, found by their s16 id at +0x850.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "poll.h"

void n_op_CutsceneStart(void);
void n_op_ThreadEnd(void);
void n_op_Message(void);
void n_op_Sound(void);
void n_op_SoundNext(void);
void n_op_Wait(void);
void n_op_FlagOp(void);
void n_script_BlockLength(void);
void n_script_FindActor(void);
void n_op_Nop(void);
void n_op_ActorHold(void);
void n_op_ActorRelease(void);
void n_op_ActorAnim(void);
void n_op_StartThread(void);
void n_op_ActorSetPos(void);
void n_op_ActorSetRot(void);
void n_op_ActorShow(void);
void n_op_ActorMove(void);
void n_op_ActorTurnTo(void);
void n_op_ActorMoveTurn(void);
void n_camera_AimAt(void);
void n_op_CameraDetach(void);
void n_op_CameraRestore(void);
void n_op_CameraPan(void);
void n_op_LoadImage(void);
void n_op_SceneStart(void);
void n_op_SceneEnd(void);
void n_op_LoadFontPage(void);
void n_op_DimStart(void);
void n_op_ActorRemove(void);
void n_op_SkipOn(void);
void n_op_SkipOff(void);
void n_op_Sparks(void);
void n_op_StreamPlay(void);
void n_op_IfFlag(void);
void n_op_SceneSeq(void);
void n_op_CameraPlace(void);
void n_op_CameraMoveAim(void);
void n_op_CameraMoveTurn(void);
void n_op_CameraWalk(void);
void n_op_DimHold(void);
void n_op_SoundFade(void);
void n_door_FindKey(void);
void n_op_DoorState(void);
void n_op_ActorWalk(void);

#define PC(th)          GV(u32, (th))
#define STEP(th)        GV(s16, (th) + 4)
#define STEP_U(th)      GV(u16, (th) + 4)
#define W(th, o)        GV(s16, (th) + (o))   /* op work */
#define W_U(th, o)      GV(u16, (th) + (o))
#define ARG(pc, i)      GV(s16, (pc) + (u32)(i) * 2)
#define ARG_U(pc, i)    GV(u16, (pc) + (u32)(i) * 2)
/* a 32-bit operand from two s16 halves (the low one sign-extended, as the
 * original ORs them) */
#define ARG32(pc, i)    ((u32)(s32)ARG(pc, i) | ((u32)ARG(pc, (i) + 1) << 16))

#define THREADS         0x800BB7E0u   /* 10 x 0x58 */
#define g_threads       GV(s32, 0x800BBB58)
#define g_menuInputOff  GV(u32, 0x800BBB60)
#define g_cameraMode    GV(u32, 0x800BBB70)   /* 1 script camera, 2 scene */
#define SCRIPT_TEXTS    GV(u32, 0x800BBB7C)   /* s16 count, then offsets */
#define g_hudFlags      GV(u16, 0x800C09D4)
#define g_msgState      GV(s32, 0x800BA9F4)
#define g_activeCamera  GV(u32, 0x800B3F04)
#define CAM_SCRIPT      0x800BB520u           /* the script camera (object) */
#define CAM_TARGET      0x800BB4D0u           /* its parent (at the player) */
#define CAM_AIM         0x800BB4C8u           /* s16 pitch, s16 yaw to a point */
#define SCENE_MODELS    0x800A89C0u           /* 0x318 bytes each */
#define g_imageBuf      GV(u32, 0x800B3D54)
#define OPLEN           0x800A2FC8u           /* u8 per op: its length in s16 */

#define IDENTITY_MATRIX_ADDR 0x80065B90u   /* GsIDMATRIX */
#define D_800A8610      GV(u32, 0x800A8610)   /* 3D scene on */
#define ENEMIES_A       GV(u32, 0x800AB510)
#define ENEMY_N         GV(s32, 0x800AF530)

#define F_MSG_SHOW      0x8007D6E0u
#define F_SND_CUE       0x800805F4u   /* (slot, sound, left, right) -> voice */
#define F_SND_CUE_NOTE  0x80080670u
#define F_STREAM_STOP   0x8001F6A4u
#define F_OP_SOUND      0x800863C8u
#define F_BLOCK_LEN     0x80088350u
#define F_FIND_ACTOR    0x800892D8u
#define F_SET_ANIM_BLEND 0x80080334u  /* (enemy, anim, blend) */
#define F_START_THREAD  0x8008D390u
#define F_RATAN2        0x8002BB48u
#define F_ISQRT         0x8002A3D8u
#define F_CAMERA_AIM    0x80089E6Cu
#define F_FILE_LOAD     0x800115C0u
#define F_TEXTURE_UPLOAD 0x8006F370u
#define F_SCENE_START   0x800925C4u
#define F_SCENE_END     0x80092D98u
#define F_GS_SET_PROJECTION 0x8002D818u
#define F_FONT_LOAD     0x8007CC74u
#define F_SPARKS_A      0x800A0158u   /* (x, y, z) */
#define F_SPARKS_B      0x800A0268u
#define F_STREAM_PLAY   0x800135A8u   /* (0, channel, a, b) */
#define F_FLAG_TEST_    0x8008F64Cu   /* flag_Test (flags, bit) */
#define F_SEQ_REWIND    0x800925C4u   /* (sequence player) */
#define F_SEQ_RESTART   0x80092654u   /* (sequence player): rewind, one step back */
#define F_CAMERA_MOVE_TURN 0x8008A6A4u

/* 800861C8 (thread): the scene starts: the player can't act, no menu. */
void n_op_CutsceneStart(void) {
    u32 th = A0;
    D_80062DEC = 0;
    g_menuInputOff = 1;
    GV(s32, 0x800BBB68) = -1;
    GV(u32, 0x800A2B68) = 0;
    PC(th) += 2;
    V0 = 0;
}

/* 80086204 (thread): end this thread; the last one gives the player back
 * control. */
void n_op_ThreadEnd(void) {
    u32 th = A0;
    if (STEP(th) != 0) { V0 = 1; return; }
    GV(u32, th + 0x54) = 0;
    GV(u16, th + 8) = (u16)(GV(u16, th + 8) & 0xFFFE);
    g_threads = g_threads - 1;
    if (g_threads == 0) {
        g_hudFlags = 2;
        D_80062DEC = 1;
        g_menuInputOff = 0;
        GV(u32, 0x800A8954) = 0;
    }
    V0 = 0;
}

/* A text of the room's text table. */
static u32 script_text(s16 idx) {
    u32 tab = SCRIPT_TEXTS;
    return tab + (u32)((s32)GV(s16, tab + (u32)(s32)idx * 4 + 4) / 2 * 2);
}

/* 800862EC (thread): show text pc[2] in a message box of kind pc[1] and
 * wait until it's closed. */
void n_op_Message(void) {
    FRAME(0x18);
    u32 th = A0;
    s16 step = STEP(th);
    if (step == 0) {
        u32 pc = PC(th);
        if (GV(s16, SCRIPT_TEXTS) < ARG(pc, 2)) { PC(th) = pc + 6; V0 = 0; return; }
        GCALL2(F_MSG_SHOW, script_text(ARG(pc, 2)), (s32)ARG(pc, 1));
        STEP_U(th) = (u16)(STEP_U(th) + 1);
        V0 = 1;
        return;
    }
    if (step == 1 && g_msgState == 0) { PC(th) += 6; V0 = 0; return; }
    V0 = 1;
}

/* 800863C8 (thread): a sound: pc[1] how, pc[2] the sound. 0 a sound of
 * bank 0, 2 of bank 6 (its voice kept in 0x800BB420[sound]), 4 stop
 * stream pc[2], 5 a note of bank 6, 6 of bank 2 if loaded; with bit 15,
 * the voice of the actor of kind 1 numbered pc[1] & 0x7FFF. */
void n_op_Sound(void) {
    FRAME(0x20);
    u32 th = A0;
    s16 how = ARG(PC(th), 1);
    if (how & 0x8000) {
        s32 n = ENEMY_N;
        u32 e = ENEMIES_A;
        if (n > 0) {
            for (s32 i = 0;; i++, e += 0x8C4) {
                if (GV(u8, e + 0x2A) == 1 && GV(u8, e + 0x315) < 0x32) {
                    u32 pc = PC(th);
                    if (GV(u8, e + 0x315) == (u32)(ARG_U(pc, 1) & 0x7FFF)) {
                        LOOP_POLL(0x80086578);
                        GCALL4(F_SND_CUE, (s32)GV(s16, e + 0x84E) * 2 + 3, (s32)ARG(pc, 2), 0x7F, 0x7F);
                        break;
                    }
                }
                if (!(i + 1 < n)) break;
                LOOP_POLL(0x8008658C);
            }
        }
    } else if ((u32)(s32)how < 7) {
        LOOP_POLL(0x80086414);
        switch (how) {
        case 0:
            GCALL4(F_SND_CUE, 0, (s32)ARG(PC(th), 2), 0x7F, 0x7F);
            break;
        case 2: {
            u32 v = GCALL4(F_SND_CUE, 6, (s32)ARG(PC(th), 2), 0x7F, 0x7F);
            GV(s32, 0x800BB420u + (u32)(s32)ARG(PC(th), 2) * 4) = (s16)v;
            break;
        }
        case 4:
            GS(s32, sp + 0x10) = ARG(PC(th), 2);
            GCALL4(F_SND_CUE_NOTE, 6, 0, 0, 0);
            GCALL1(F_STREAM_STOP, (s32)ARG(PC(th), 2));
            break;
        case 5:
            GS(s32, sp + 0x10) = 0x10;
            GCALL4(F_SND_CUE_NOTE, 6, (s32)ARG(PC(th), 2), 0x7F, 0x7F);
            break;
        case 6:
            if (GV(s16, 0x800637E0) >= 0)
                GCALL4(F_SND_CUE, 2, (s32)ARG(PC(th), 2), 0x7F, 0x7F);
            break;
        }
    }
    PC(th) += 6;
    V0 = 0;
}

/* 800865B8 (thread): the same, going straight on to the next op. */
void n_op_SoundNext(void) {
    FRAME(0x18);
    GCALL1(F_OP_SOUND, A0);
    V0 = 2;
}

/* 80086BC0 (thread): wait pc[1] frames (counted in the step). */
void n_op_Wait(void) {
    u32 th = A0;
    u32 pc = PC(th);
    if (STEP(th) != ARG(pc, 1)) {
        STEP_U(th) = (u16)(STEP_U(th) + 1);
        V0 = 1;
        return;
    }
    PC(th) = pc + 4;
    V0 = 0;
}

/* 80087938 (thread): flag pc[2] of flag array pc[1] (0x800A2B6C): pc[3]
 * 0 set, 1 clear, 2 toggle. Bits count from the top of each word. */
void n_op_FlagOp(void) {
    u32 th = A0;
    u32 pc = PC(th);
    u16 bit = ARG_U(pc, 2);
    u32 w = GV(u32, 0x800A2B6Cu + (u32)(s32)ARG(pc, 1) * 4) + (u32)(((s32)(s16)bit >> 5) * 4);
    u32 m = 1u << (~bit & 0x1F);
    switch (ARG(pc, 3)) {
    case 0: GV(u32, w) |= m; break;
    case 1: GV(u32, w) &= ~m; break;
    case 2: GV(u32, w) ^= m; break;
    }
    PC(th) = pc + 8;
    V0 = 0;
}

/* 80088510 (thread): if flag pc[2] of flag array pc[1] (0x800A2B6C) is
 * set, go on (the block that follows runs); if not, skip to after the
 * else / end. */
void n_op_IfFlag(void) {
    FRAME(0x18);
    u32 th = A0;
    u32 pc = PC(th);
    if (GCALL2(F_FLAG_TEST_, GV(u32, 0x800A2B6Cu + (u32)(s32)ARG(pc, 1) * 4), (u32)(s32)ARG(pc, 2))) {
        PC(th) += 6;
    } else {
        u32 n = GCALL2(F_BLOCK_LEN, PC(th), 0x18);
        PC(th) += n * 2;
    }
    V0 = 0;
}

/* 80088350 (pc, end): length (in s16) of the ops from pc up to and
 * including the first op end. For end 0x18 / 0x19 (else / end of an if)
 * the op at pc is skipped first and nested ifs (0x17) are skipped whole. */
void n_script_BlockLength(void) {
    FRAME(0x28);
    u32 p = A0;
    s32 end = (s32)A1;
    u32 n = 0;
    if (end == 0x18 || end == 0x19) {
        u32 pcl = end == 0x18 ? 0x80088408 : 0x80088488;
        n = GV(u8, OPLEN + (u32)(s32)GV(s16, p));
        p += n * 2;
        if (GV(s16, p) != end) {
            for (;;) {
                s16 op = GV(s16, p);
                u32 add;
                if (op == 0x17) add = GCALL2(F_BLOCK_LEN, p, 0x19);
                else add = GV(u8, OPLEN + (u32)(s32)op);
                n += add;
                p += add * 2;
                if (GV(s16, p) == end) break;
                LOOP_POLL(pcl);
            }
        }
    } else if (GV(s16, p) != end) {
        for (;;) {
            u32 add = GV(u8, OPLEN + (u32)(s32)GV(s16, p));
            n += add;
            p += add * 2;
            if (GV(s16, p) == end) break;
            LOOP_POLL(0x800884D4);
        }
    }
    V0 = n + GV(u8, OPLEN + (u32)end);
}

/* 800892D8 (id): the index of the actor with this id, or -1. */
void n_script_FindActor(void) {
    s32 id = (s32)A0;
    u32 e = ENEMIES_A;
    s32 n = ENEMY_N, i = 0;
    if (n > 0) {
        for (;;) {
            if (GV(s16, e + 0x850) == id) break;
            i++;
            e += 0x8C4;
            if (!(i < n)) break;
            LOOP_POLL(0x8008930C);
        }
    }
    V0 = i == ENEMY_N ? (u32)-1 : (u32)i;
}

/* 80089334 (thread): nothing. */
void n_op_Nop(void) {
    PC(A0) += 2;
    V0 = 0;
}

/* The actor of id pc[1] (address), or 0 if there's none. */
static u32 actor(u32 th) {
    u32 base = ENEMIES_A;
    s32 i = (s32)GCALL1(F_FIND_ACTOR, (s32)ARG(PC(th), 1));
    return i < 0 ? 0 : base + (u32)i * 0x8C4;
}

/* 8008934C (thread): hold actor pc[1]: its state (+0x316) saved in
 * +0x858 and set to 4. */
void n_op_ActorHold(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) {
        u16 s = GV(u16, e + 0x316);
        GV(u16, e + 0x316) = 4;
        GV(u16, e + 0x858) = s;
    }
    PC(th) += 4;
    V0 = 2;
}

/* 800893D0 (thread): give actor pc[1] its state back. */
void n_op_ActorRelease(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) GV(u16, e + 0x316) = GV(u16, e + 0x858);
    PC(th) += 4;
    V0 = 2;
}

/* 80089450 (thread): actor pc[1] on, animation pc[2] blended over pc[4];
 * pc[3] 0 sets bit 2 of its flags (+0x320). */
void n_op_ActorAnim(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) {
        GV(u8, e) = 1;
        u32 pc = PC(th);
        GCALL3(F_SET_ANIM_BLEND, e, (s32)ARG(pc, 2), (s32)ARG(pc, 4));
        if (ARG(PC(th), 3) == 0) GV(u32, e + 0x320) |= 4;
    }
    PC(th) += 0xA;
    V0 = 2;
}

/* 8008959C (thread): start script pc[1] in thread pc[2]. */
void n_op_StartThread(void) {
    FRAME(0x18);
    u32 th = A0, pc = PC(th);
    GCALL2(F_START_THREAD, (s32)ARG(pc, 1), (s32)ARG(pc, 2));
    PC(th) += 6;
    V0 = 2;
}

/* 80089608 (thread): actor pc[1]'s position (x, y, z: 32 bits each). */
void n_op_ActorSetPos(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) {
        u32 pc = PC(th);
        GV(u32, e + 0x198) = ARG32(pc, 2);
        GV(u32, e + 0x19C) = ARG32(pc, 4);
        GV(u32, e + 0x1A0) = ARG32(pc, 6);
    }
    PC(th) += 0x10;
    V0 = 2;
}

/* 800896DC (thread): actor pc[1]'s rotation. */
void n_op_ActorSetRot(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) {
        GV(u16, e + 0x1C4) = ARG_U(PC(th), 2);
        GV(u16, e + 0x1C6) = ARG_U(PC(th), 3);
        GV(u16, e + 0x1C8) = ARG_U(PC(th), 4);
    }
    PC(th) += 0xA;
    V0 = 2;
}

/* 80089784 (thread): show (pc[2] = 1) or hide actor pc[1]. */
void n_op_ActorShow(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) {
        GV(u8, e + 0x29) = GV(u8, PC(th) + 4);
        GV(u16, GV(u32, e + 0x8BC) + 0xD4) = (u16)(ARG_U(PC(th), 2) ^ 1);
    }
    PC(th) += 6;
    V0 = 2;
}

/* Spread the rest of a move (thread +o, over +0xA frames) by a step. */
static s32 part(u32 th, u32 o) { return mips_div(W(th, o), W(th, 0xA)); }
static int count_down(u32 th) {
    u16 n = (u16)(W_U(th, 0xA) - 1);
    W_U(th, 0xA) = n;
    return (s16)n > 0;
}

/* 8008981C (thread): move actor pc[1] by (pc[2], pc[3], pc[4]) over
 * pc[5] frames. */
void n_op_ActorMove(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) {
        s16 step = STEP(th);
        if (step == 0) {
            u32 pc = PC(th);
            W_U(th, 0xC) = ARG_U(pc, 2);
            W_U(th, 0xE) = ARG_U(pc, 3);
            W_U(th, 0x10) = ARG_U(pc, 4);
            STEP_U(th) = (u16)(STEP_U(th) + 1);
            W_U(th, 0xA) = ARG_U(pc, 5);
        }
        if (step == 0 || step == 1) {
            GV(s32, e + 0x198) += part(th, 0xC);
            GV(s32, e + 0x19C) += part(th, 0xE);
            GV(s32, e + 0x1A0) += part(th, 0x10);
            s32 dx = part(th, 0xC), dy = part(th, 0xE), dz = part(th, 0x10);
            int more = count_down(th);
            W_U(th, 0xC) = (u16)(W_U(th, 0xC) - dx);
            W_U(th, 0xE) = (u16)(W_U(th, 0xE) - dy);
            W_U(th, 0x10) = (u16)(W_U(th, 0x10) - dz);
            if (more) { V0 = 1; return; }
        }
    }
    PC(th) += 0xC;
    V0 = 2;
}

/* 800899D8 (thread): turn actor pc[1] to face direction (pc[2], pc[3])
 * over pc[4] frames, pc[5] 0 turning one way, else the other. */
void n_op_ActorTurnTo(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) {
        s16 step = STEP(th);
        if (step == 0) {
            u32 pc = PC(th);
            u16 a = (u16)((GCALL2(F_RATAN2, (s32)ARG(pc, 2), (s32)ARG(pc, 3)) + 0x800) & 0xFFF);
            W_U(th, 0xC) = a;
            u16 d = (u16)(a - (GV(u16, e + 0x1C6) & 0xFFF));
            W_U(th, 0xC) = d;
            if (ARG(PC(th), 5) == 0) {
                if ((s16)d < 0) W_U(th, 0xC) = (u16)(d + 0x1000);
            } else {
                if ((s16)d > 0) W_U(th, 0xC) = (u16)(d - 0x1000);
            }
            STEP_U(th) = (u16)(STEP_U(th) + 1);
            W_U(th, 0xA) = ARG_U(PC(th), 4);
        }
        if (step == 0 || step == 1) {
            GV(u16, e + 0x1C6) = (u16)((GV(u16, e + 0x1C6) + part(th, 0xC)) & 0xFFF);
            s32 d = part(th, 0xC);
            int more = count_down(th);
            W_U(th, 0xC) = (u16)(W_U(th, 0xC) - d);
            if (more) { V0 = 1; return; }
        }
    }
    PC(th) += 0xC;
    V0 = 2;
}

/* 80089C98 (thread): move actor pc[1] by (pc[3], pc[4], pc[5]) over
 * pc[6] frames, turning by pc[2] a frame. */
void n_op_ActorMoveTurn(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) {
        s16 step = STEP(th);
        if (step == 0) {
            u32 pc = PC(th);
            W_U(th, 0xA) = ARG_U(pc, 6);
            W_U(th, 0xC) = ARG_U(pc, 3);
            W_U(th, 0xE) = ARG_U(pc, 4);
            STEP_U(th) = (u16)(STEP_U(th) + 1);
            W_U(th, 0x10) = ARG_U(pc, 5);
        }
        if (step == 0 || step == 1) {
            GV(u16, e + 0x1C6) = (u16)(GV(u16, e + 0x1C6) + ARG_U(PC(th), 2));
            GV(s32, e + 0x198) += part(th, 0xC);
            GV(s32, e + 0x19C) += part(th, 0xE);
            GV(s32, e + 0x1A0) += part(th, 0x10);
            s32 dx = part(th, 0xC), dy = part(th, 0xE), dz = part(th, 0x10);
            int more = count_down(th);
            W_U(th, 0xC) = (u16)(W_U(th, 0xC) - dx);
            W_U(th, 0xE) = (u16)(W_U(th, 0xE) - dy);
            W_U(th, 0x10) = (u16)(W_U(th, 0x10) - dz);
            if (more) { V0 = 1; return; }
        }
    }
    PC(th) += 0xE;
    V0 = 2;
}

/* 80089E6C (x, y, z): the yaw and pitch from the script camera's parent
 * (at the player) to a point, into 0x800BB4CA / 0x800BB4C8; returns the
 * pitch. */
void n_camera_AimAt(void) {
    FRAME(0x20);
    s32 dx = (s32)(A0 - GV(u32, CAM_TARGET + 0x18));
    s32 dy = (s32)(A1 - GV(u32, CAM_TARGET + 0x1C));
    s32 dz = (s32)(A2 - GV(u32, CAM_TARGET + 0x20));
    u32 yaw = GCALL2(F_RATAN2, dx, dz);
    u32 sq = (u32)dx * (u32)dx + (u32)dz * (u32)dz;
    GV(u16, CAM_AIM + 2) = (u16)((yaw + 0x800) & 0xFFF);
    u32 r = GCALL1(F_ISQRT, sq);
    u32 pitch = GCALL2(F_RATAN2, dy, r) & 0xFFF;
    GV(u16, CAM_AIM) = (u16)pitch;
    V0 = pitch;
}

/* 80089F10 (thread): switch to the script camera: it starts where the
 * game's camera is, its parent at the player. */
void n_op_CameraDetach(void) {
    u32 th = A0;
    GV(u32, CAM_SCRIPT + 0x4C) = CAM_TARGET;
    g_activeCamera = CAM_SCRIPT;
    for (u32 i = 0; i < 5; i++) GV(u32, CAM_TARGET + 4 + i * 4) = GV(u32, IDENTITY_MATRIX_ADDR + i * 4);
    GV(u32, CAM_TARGET + 0x4C) = 0;
    for (u32 i = 0; i < 3; i++) {
        GV(u32, CAM_SCRIPT + 0x18 + i * 4) = GV(u32, CAMERA_OBJ + 0x18 + i * 4);
        GV(u32, CAM_TARGET + 0x18 + i * 4) = GV(u32, g_player + 0x18 + i * 4);
    }
    for (u32 i = 0; i < 3; i++) {
        GV(u16, CAM_SCRIPT + 0x44 + i * 2) = GV(u16, CAMERA_OBJ + 0x44 + i * 2);
        GV(u16, CAM_TARGET + 0x44 + i * 2) = GV(u16, g_player + 0x44 + i * 2);
    }
    g_cameraMode = 1;
    PC(th) += 2;
    V0 = 2;
}

/* 8008A040 (thread): back to the game's camera, which (and the player)
 * takes the script camera's place. */
void n_op_CameraRestore(void) {
    u32 th = A0;
    g_activeCamera = CAMERA_OBJ;
    for (u32 i = 0; i < 3; i++) {
        GV(u32, CAMERA_OBJ + 0x18 + i * 4) = GV(u32, CAM_SCRIPT + 0x18 + i * 4);
        GV(u32, g_player + 0x18 + i * 4) = GV(u32, CAM_TARGET + 0x18 + i * 4);
    }
    for (u32 i = 0; i < 3; i++) {
        GV(u16, CAMERA_OBJ + 0x44 + i * 2) = GV(u16, CAM_SCRIPT + 0x44 + i * 2);
        GV(u16, g_player + 0x44 + i * 2) = GV(u16, CAM_TARGET + 0x44 + i * 2);
    }
    g_cameraMode = 0;
    PC(th) += 2;
    V0 = 2;
}

/* A turn by d (0-0xFFF) the short way round. */
static u16 short_turn(u16 d) { return d < 0x800 ? d : (u16)(d - 0x1000); }

/* 8008A4E4 (thread): turn the script camera to look at point (pc[1..6],
 * 32-bit x, y, z) over pc[7] frames. */
void n_op_CameraPan(void) {
    FRAME(0x18);
    u32 th = A0;
    s16 step = STEP(th);
    if (step == 0) {
        u32 pc = PC(th);
        GCALL3(F_CAMERA_AIM, ARG32(pc, 1), ARG32(pc, 3), ARG32(pc, 5));
        W_U(th, 0xC) = short_turn((u16)((GV(u16, CAM_AIM) - GV(u16, CAM_SCRIPT + 0x44)) & 0xFFF));
        W_U(th, 0xE) = short_turn((u16)((GV(u16, CAM_AIM + 2) - GV(u16, CAM_TARGET + 0x46)) & 0xFFF));
        STEP_U(th) = (u16)(STEP_U(th) + 1);
        W_U(th, 0xA) = ARG_U(PC(th), 7);
    }
    if (step == 0 || step == 1) {
        GV(u16, CAM_SCRIPT + 0x44) = (u16)((GV(u16, CAM_SCRIPT + 0x44) + part(th, 0xC)) & 0xFFF);
        GV(u16, CAM_TARGET + 0x46) = (u16)((GV(u16, CAM_TARGET + 0x46) + part(th, 0xE)) & 0xFFF);
        s32 dp = part(th, 0xC), dy = part(th, 0xE);
        int more = count_down(th);
        W_U(th, 0xC) = (u16)(W_U(th, 0xC) - dp);
        W_U(th, 0xE) = (u16)(W_U(th, 0xE) - dy);
        if (more) { V0 = 1; return; }
    }
    PC(th) += 0x10;
    V0 = 2;
}

/* The camera's place (CAM_TARGET) and its aim at a point: pc[7..12]
 * (32-bit x, y, z) through camera_AimAt, given to the script camera's
 * pitch and the target's yaw. */
static void camera_aim_pc(u32 pc) {
    GCALL3(F_CAMERA_AIM, ARG32(pc, 7), ARG32(pc, 9), ARG32(pc, 11));
    GV(u16, CAM_SCRIPT + 0x44) = GV(u16, CAM_AIM);
    GV(u16, CAM_TARGET + 0x46) = GV(u16, CAM_AIM + 2);
}

/* Move the camera target a frame's share (the rest over the frames left,
 * +0xA) of what is left of the move (+0xC/+0xE/+0x10). */
static void camera_move_step(u32 th) {
    GV(u32, CAM_TARGET + 0x18) += (u32)part(th, 0xC);
    GV(u32, CAM_TARGET + 0x1C) += (u32)part(th, 0xE);
    GV(u32, CAM_TARGET + 0x20) += (u32)part(th, 0x10);
    s32 dx = part(th, 0xC), dy = part(th, 0xE), dz = part(th, 0x10);
    W_U(th, 0xC) = (u16)(W_U(th, 0xC) - dx);
    W_U(th, 0xE) = (u16)(W_U(th, 0xE) - dy);
    W_U(th, 0x10) = (u16)(W_U(th, 0x10) - dz);
}

/* The move still to make to pc[1], pc[3], pc[5] (low halves). */
static void camera_move_start(u32 th) {
    u32 pc = PC(th);
    W_U(th, 0xC) = (u16)(ARG_U(pc, 1) - GV(u16, CAM_TARGET + 0x18));
    W_U(th, 0xE) = (u16)(ARG_U(pc, 3) - GV(u16, CAM_TARGET + 0x1C));
    W_U(th, 0x10) = (u16)(ARG_U(pc, 5) - GV(u16, CAM_TARGET + 0x20));
}

/* 8008A194 (thread): put the script camera's target at pc[1..6] (32-bit
 * x, y, z), aimed at pc[7..12]. */
void n_op_CameraPlace(void) {
    FRAME(0x20);
    u32 th = A0;
    u32 pc = PC(th);
    GV(u32, CAM_TARGET + 0x18) = ARG32(pc, 1);
    GV(u32, CAM_TARGET + 0x1C) = ARG32(pc, 3);
    GV(u32, CAM_TARGET + 0x20) = ARG32(pc, 5);
    camera_aim_pc(PC(th));
    PC(th) += 0x1A;
    V0 = 2;
}

/* 8008A2E4 (thread): move the script camera's target to pc[1], pc[3],
 * pc[5] over pc[13] frames, aimed at pc[7..12] all the way. */
void n_op_CameraMoveAim(void) {
    FRAME(0x20);
    u32 th = A0;
    s16 step = STEP(th);
    if (step == 0) {
        camera_move_start(th);
        STEP_U(th) = (u16)(STEP_U(th) + 1);
        W_U(th, 0xA) = GV(u16, PC(th) + 0x1A);
    }
    if (step == 0 || step == 1) {
        camera_move_step(th);
        camera_aim_pc(PC(th));
        if (count_down(th)) { V0 = 1; return; }
    }
    PC(th) += 0x1C;
    V0 = 2;
}

/* A turn (0-0xFFF) the short way round, signed. */
static s32 turn_short(s32 d) {
    d &= 0xFFF;
    return d < 0x800 ? d : d - 0x1000;
}

/* 8008A6A4 (thread): move the script camera's target to pc[1], pc[3],
 * pc[5] over pc[13] frames, turning it a frame's share towards pc[7..12]
 * each frame. The whole move stays in +0x12/+0x14 (x, z; for
 * op_CameraWalk) and +0x16 counts its steps. */
void n_op_CameraMoveTurn(void) {
    FRAME(0x20);
    u32 th = A0;
    s16 step = STEP(th);
    if (step == 0) {
        camera_move_start(th);
        W_U(th, 0x12) = W_U(th, 0xC);
        W_U(th, 0x14) = W_U(th, 0x10);
        W_U(th, 0x16) = 0;
        STEP_U(th) = (u16)(STEP_U(th) + 1);
        W_U(th, 0xA) = GV(u16, PC(th) + 0x1A);
    }
    if (step == 0 || step == 1) {
        camera_move_step(th);
        u32 pc = PC(th);
        GCALL3(F_CAMERA_AIM, ARG32(pc, 7), ARG32(pc, 9), ARG32(pc, 11));
        u16 pitch = GV(u16, CAM_SCRIPT + 0x44), yaw = GV(u16, CAM_TARGET + 0x46);
        s32 dp = turn_short((s16)GV(u16, CAM_AIM) - (s16)pitch);
        s32 dy = turn_short((s16)GV(u16, CAM_AIM + 2) - (s16)yaw);
        GV(u16, CAM_SCRIPT + 0x44) = (u16)((pitch + mips_div(dp, W(th, 0xA))) & 0xFFF);
        GV(u16, CAM_TARGET + 0x46) = (u16)((yaw + mips_div(dy, W(th, 0xA))) & 0xFFF);
        if (count_down(th)) { V0 = 1; return; }
    }
    PC(th) += 0x1C;
    V0 = 2;
}

/* 8008BFB8 (thread): op_CameraMoveTurn as a walk: a footstep sound every
 * 1000 units of the way and the camera bobbing with each step (its
 * height -0x640 less up to 50); at the end it settles and a last step
 * sounds. */
void n_op_CameraWalk(void) {
    FRAME(0x28);
    u32 th = A0;
    s32 r = (s32)GCALL1(F_CAMERA_MOVE_TURN, th);
    if (r == 1) {
        s32 dx = W(th, 0x12) - W(th, 0xC), dz = W(th, 0x14) - W(th, 0x10);
        s32 d = (s32)GCALL1(F_ISQRT, (u32)dx * (u32)dx + (u32)dz * (u32)dz);
        s32 steps = d / 1000;
        if (W(th, 0x16) < steps) {
            GCALL4(F_SND_CUE, 6, 0, 0x7F, 0x7F);
            W_U(th, 0x16) = (u16)(W_U(th, 0x16) + 1);
        }
        s32 rem = (s32)((u32)d - (u32)steps * 1000);
        s32 bob = rem < 600 ? rem / 12 : (1000 - rem) / 8;
        GV(s32, CAM_SCRIPT + 0x1C) = -0x640 - bob;
    } else {
        GV(s32, CAM_SCRIPT + 0x1C) = -0x640;
        GCALL4(F_SND_CUE, 6, 0, 0x7F, 0x7F);
    }
    V0 = (u32)r;
}

/* 8008B078 (thread): load image file pc[1] to 0x800B3D54's buffer and
 * upload it. */
void n_op_LoadImage(void) {
    FRAME(0x28);
    u32 th = A0;
    GS(u32, sp + 0x10) = g_imageBuf;
    GCALL4(F_FILE_LOAD, 0x8006C664u, 0x80058538u, 0x800A2B3Cu, (s32)ARG(PC(th), 1));
    GCALL1(F_TEXTURE_UPLOAD, g_imageBuf);
    PC(th) += 4;
    V0 = 0;
}

/* 8008B0F0 (thread): start scene model pc[1] (0x800A89C0, 0x318 bytes
 * each) with the 3D scene off. */
void n_op_SceneStart(void) {
    FRAME(0x20);
    u32 th = A0;
    u32 m = SCENE_MODELS + (u32)(s32)ARG(PC(th), 1) * 0x318;
    D_800A8610 = 0;
    GCALL1(F_SCENE_START, m);
    GV(u32, 0x800BB2D0) = GV(u32, m + 0x16C) + GV(u32, m + 0x1D0) * 0x50 - 0x50;
    g_cameraMode = 2;
    PC(th) += 4;
    V0 = 0;
}

/* 8008B194 (thread): end scene pc[1]: the 3D scene back on. */
void n_op_SceneEnd(void) {
    FRAME(0x18);
    u32 th = A0;
    D_800A8610 = 1;
    GCALL1(F_SCENE_END, (s32)ARG(PC(th), 1));
    g_cameraMode = 0;
    GCALL1(F_GS_SET_PROJECTION, 0x10E);
    PC(th) += 4;
    V0 = 0;
}

/* 8008B1F0 (thread): load font page 2 into 0x800B3D54's buffer. */
void n_op_LoadFontPage(void) {
    FRAME(0x18);
    u32 th = A0;
    GCALL2(F_FONT_LOAD, g_imageBuf, 2);
    PC(th) += 2;
    V0 = 0;
}

/* 8008B234 (thread): the screen dimmer holds the picture (state 2). */
void n_op_DimHold(void) {
    u32 th = A0;
    GV(s32, 0x800A0CB0) = 1;
    GV(s32, 0x800A0CB4) = 2;
    PC(th) += 2;
    V0 = 2;
}

/* 8008B260 (thread): start the screen dimmer (gameflow.c). */
void n_op_DimStart(void) {
    u32 th = A0;
    GV(s32, 0x800A0CB0) = 0;
    GV(s32, 0x800A0CB4) = 1;
    PC(th) += 2;
    V0 = 2;
}

/* 800891C0 (thread): scene model pc[1]'s sequence player: pc[2] 0
 * rewind, 1 restart. */
void n_op_SceneSeq(void) {
    FRAME(0x18);
    u32 th = A0;
    u32 pc = PC(th);
    u32 m = SCENE_MODELS + (u32)(s32)ARG(pc, 1) * 0x318;
    if (ARG(pc, 2) == 0) GCALL1(F_SEQ_REWIND, m);
    else if (ARG(pc, 2) == 1) GCALL1(F_SEQ_RESTART, m);
    PC(th) += 6;
    V0 = 0;
}

/* 8008B934 (thread): actor pc[1] off. */
void n_op_ActorRemove(void) {
    FRAME(0x20);
    u32 th = A0, e = actor(th);
    if (e) GV(u8, e) = 0;
    PC(th) += 4;
    V0 = 0;
}

/* 8008BC14 (thread): every running thread can be skipped from here. */
void n_op_SkipOn(void) {
    u32 th = A0;
    u32 t = THREADS;
    for (s32 i = 0;; i++, t += 0x58) {
        if (GV(u16, t + 8) & 1) GV(u32, t + 0x54) = 1;
        if (!(i + 1 < 10)) break;
        LOOP_POLL(0x8008BC44);
    }
    PC(th) += 2;
    V0 = 2;
}

/* 8008BC64 (thread): this thread can't be skipped. */
void n_op_SkipOff(void) {
    u32 th = A0;
    GV(u32, th + 0x54) = 0;
    PC(th) += 2;
    V0 = 2;
}

/* 8008BC7C (thread): for pc[7] frames, every 10th frame an effect at
 * (pc[1..6]: 32-bit x, y, z). */
void n_op_Sparks(void) {
    FRAME(0x28);
    u32 th = A0;
    s16 step = STEP(th);
    if (step == 0) {
        STEP_U(th) = (u16)(STEP_U(th) + 1);
        W_U(th, 0xA) = ARG_U(PC(th), 7);
    }
    if (step == 0 || step == 1) {
        s32 k = (s32)W(th, 0xA) - 1;
        if (k % 10 == 0) {
            u32 pc = PC(th);
            u32 x = ARG32(pc, 1), y = ARG32(pc, 3), z = ARG32(pc, 5);
            GCALL3(F_SPARKS_A, x, y, z);
            GCALL3(F_SPARKS_B, x, y, z);
        }
        if (count_down(th)) { V0 = 1; return; }
    }
    PC(th) += 0x10;
    V0 = 0;
}

/* 8008BDA8 (thread): streamed audio on channels 0 (pc[1] >= 0: pc[3],
 * pc[1]'s low byte) and 1 (pc[2]: pc[4], pc[2]'s low byte). */
void n_op_StreamPlay(void) {
    FRAME(0x18);
    u32 th = A0;
    u32 pc = PC(th);
    if (ARG(pc, 1) >= 0) {
        GCALL4(F_STREAM_PLAY, 0, 0, ARG_U(pc, 3), GV(u8, pc + 2));
        pc = PC(th);
    }
    if (ARG(pc, 2) >= 0) GCALL4(F_STREAM_PLAY, 0, 1, ARG_U(pc, 4), GV(u8, pc + 4));
    PC(th) += 0xA;
    V0 = 2;
}

#define SOUND_VOICES    0x800BB420u   /* s32 per sound slot: its voice, -1 none */
#define F_SOUND_VOLUME  0x800206B4u   /* (voice, left, right) */
#define g_masterVolume  GV(s16, 0x80062DB6)

/* 800865D8 (thread): fade sound slot pc[1] (playing) over pc[3] frames:
 * in (pc[2] 0: volume 127 - level) or out (then stopped); the level
 * (work +0xC, from 127) loses level / frames left each frame (work +0xA).
 * 1 while fading, then on (2: pc + 8). */
void n_op_SoundFade(void) {
    FRAME(0x20);
    u32 th = A0;
    u32 pc = PC(th);
    if (GV(s32, SOUND_VOICES + (u32)(s32)ARG(pc, 1) * 4) >= 0) {
        s16 step = STEP(th);
        if (step == 0) {
            W_U(th, 0xC) = 0x7F;
            W_U(th, 0xA) = ARG_U(pc, 3);
            STEP_U(th) = (u16)(step + 1);
        }
        if (step == 0 || step == 1) {
            s32 q = mips_div(W(th, 0xC), W(th, 0xA));
            W_U(th, 0xC) = (u16)(W_U(th, 0xC) - (u32)q);
            s32 lv = W(th, 0xC);
            if (ARG(PC(th), 2) == 0) lv = 0x7F - lv;
            s16 vol = (s16)((s32)((u32)lv * (u32)(s32)g_masterVolume) / 100);
            u32 voice = (u32)(s32)GV(s16, SOUND_VOICES + (u32)(s32)ARG(PC(th), 1) * 4);
            gcall(F_SOUND_VOLUME, voice, (u32)(s32)vol, (u32)(s32)vol, 0);
            W_U(th, 0xA) = (u16)(W_U(th, 0xA) - 1);
            if (W(th, 0xA) > 0) { V0 = 1; return; }
            if (ARG(PC(th), 2) != 0)
                GCALL1(F_STREAM_STOP, (s32)GV(s16, SOUND_VOICES + (u32)(s32)ARG(PC(th), 1) * 4));
        }
    }
    PC(th) += 8;
    V0 = 2;
}

/* 80087A20 (door flags): the carried key (0x800B4BE8: u8 slots, s16
 * count at +0x50) for a door: an item of kind 4 (0x800A09C8, 8 bytes an
 * item) whose value matches the door's bits 24-29; used up (0x8008D7F0)
 * unless kind bit 2 says it's kept. Returns the item, or -1. */
void n_door_FindKey(void) {
    FRAME(0x18);
    u32 key = A0 & 0x3F000000u;
    u32 base = 0x800B4BE8u;
    for (s32 i = 0; i < GV(s16, base + 0x50); i++) {
        if (i) LOOP_POLL(0x80087AD4u);
        u32 slot = 0x800AFA00u + (u32)GV(u8, base + (u32)i) * 8;
        s16 item = GV(s16, slot);
        u8 kind = GV(u8, 0x800A09C8u + (u32)(s32)item * 8 + 6);
        if (!(kind & 4)) continue;
        if (GV(u32, slot + 4) != key) continue;
        if (!(kind & 2)) gcall(0x8008D7F0u, (u32)(s32)(s16)i, slot, base, key);
        V0 = (u32)(s32)item;
        return;
    }
    V0 = (u32)-1;
}

/* 80088030 (thread): door pc[1]'s state (doors at 0x800B3D30, 24 bytes)
 * into flags 0x800BBB54: bit 2 open (not locked, or its lock flag set),
 * bit 1 locked but the key is carried, none locked. */
void n_op_DoorState(void) {
    FRAME(0x20);
    u32 th = A0;
    u32 door = GV(u32, 0x800B3D30) + (u32)(s32)ARG(PC(th), 1) * 24;
    if (STEP(th) != 0) { V0 = 0; return; }
    GV(u32, 0x800BBB54) = 0;
    s32 lock = GV(s32, door + 0x10);
    u32 bit;
    if (lock < 0) {
        if (GCALL2(F_FLAG_TEST_, 0x80062FE0u, (u32)lock & 0xFFFFFF) != 0) bit = 2;
        else bit = (s32)GCALL1(0x80087A20u, GV(u32, door + 0x10)) >= 0 ? 1 : 0;
    } else {
        GCALL2(F_FLAG_TEST_, 0x80062FE0u, (u32)lock & 0xFFFFFF);
        bit = 2;
    }
    GCALL2(0x8008F5F8u, 0x800BBB54u, bit);
    PC(th) += 4;
    V0 = 0;
}

/* 80089B68 (thread): actor pc[1] walks: turning by pc[2] and moving
 * pc[3] along its facing each frame for pc[4] frames. 1 while walking,
 * then on (2: pc + 0xA). */
void n_op_ActorWalk(void) {
    FRAME(0x30);
    u32 th = A0;
    u32 e = ENEMIES_A;
    s32 k = (s32)GCALL1(F_FIND_ACTOR, (s32)ARG(PC(th), 1));
    if (k >= 0) {
        e += (u32)k * 0x8C4;
        s16 step = STEP(th);
        if (step == 0) {
            STEP_U(th) = (u16)(step + 1);
            W_U(th, 0xA) = ARG_U(PC(th), 4);
        }
        if (step == 0 || step == 1) {
            GV(u16, e + 0x1C6) += ARG_U(PC(th), 2);
            gcall(0x8006F7A8u, e + 0x1C4, sp + 0x10, (u32)(s32)ARG(PC(th), 3), 0);
            GV(u32, e + 0x198) += GS(u32, sp + 0x10);
            GV(u32, e + 0x19C) += GS(u32, sp + 0x14);
            GV(u32, e + 0x1A0) += GS(u32, sp + 0x18);
            W_U(th, 0xA) = (u16)(W_U(th, 0xA) - 1);
            if (W(th, 0xA) > 0) { V0 = 1; return; }
        }
    }
    PC(th) += 0xA;
    V0 = 2;
}

void n_op_Nop0(void);
void n_op_Music(void);

/* 80086054 (thread): op 0, nothing. */
void n_op_Nop0(void) {
    PC(A0) += 2;
    V0 = 0;
}

/* 80088828 (thread): op 0x22, the music: stop track pc[1], then play
 * track pc[2] (the one playing, 0x80062E78); either skipped if negative. */
void n_op_Music(void) {
    FRAME(0x18);
    u32 th = A0;
    s16 stop = ARG(PC(th), 1);
    if (stop >= 0) GCALL1(0x8001215Cu, (u32)(s32)stop);
    s16 play = ARG(PC(th), 2);
    if (play >= 0) {
        GV(u16, 0x80062E78) = (u16)play;
        GCALL1(0x8001209Cu, (u32)(s32)play);
    }
    PC(th) += 6;
    V0 = 0;
}
