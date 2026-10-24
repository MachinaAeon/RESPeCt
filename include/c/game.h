/* Declarations of game code and data used by C replacements in src/mods/.
 * Names come from symbol_addrs.txt; address-named ones (D_xxxxxxxx,
 * func_xxxxxxxx) resolve to the address in their name at link time.
 * See tools/build_mod.py. */
#ifndef GAME_H
#define GAME_H

typedef signed char s8;
typedef unsigned char u8;
typedef short s16;
typedef unsigned short u16;
typedef int s32;
typedef unsigned int u32;

/* ---- input (TEST.BIN) ---- */
extern s32 g_buttons;          /* mapped buttons this frame */
extern s32 g_buttonsPrev;      /* last frame's g_buttons */
extern u8 g_inputAxes[4];      /* pad_GetAxes result; [0], [1] = stick bytes;
                                  PS Mouse: [3] = X motion, [2] = Y motion (s8) */
extern s32 g_inputAxesPrev;
extern u32 g_padTypeBit;       /* 0x10000 PS Mouse, 0x80000 digital, 0x200000 GunCon,
                                  0x400000 DualShock (port 1) */
#define PADTYPE_MOUSE 0x10000

/* input_SampleFrame state */
extern s32 D_800B3CE4;         /* 0 live, 1 attract-demo playback, else no sampling */
extern s32 D_800B3CE0;         /* last frame's g_buttonsHi */
extern s16 g_inputLocked;      /* demo playback only: zero the buttons (not cutscenes) */
extern s32 *g_demoInput;       /* 2 words per frame: buttons, axes */
extern s32 g_demoFrame;
extern s32 g_demoLength;
extern s32 g_demoRealButtons;  /* real pad during the demo; Start (0x800) ends it */
extern s32 D_800AF578;         /* last frame's g_demoRealButtons */
extern u8 D_80062DFC;          /* why the demo ended: 5 ran out, 1 Start pressed */
u32 pad_GetButtons(s32 unused);   /* port 1 low 16, port 2 high 16, 1 = pressed */
u32 input_MapButtons(u32 raw);
u32 func_80013B90(s32 port);      /* controller-type bits for a port */
void DrawSync(s32 mode);
void SetDispMask(s32 mask);
void func_80072F74(void);
void func_8001306C(s32 a0);

/* ---- pad module (KERNEL) ---- */
extern u8 *g_padBuf1Ptr;       /* receive buffers: [0] status (0 = ok), [1] type, [2..3] buttons */
extern u8 *g_padBuf2Ptr;
extern u32 g_padState1;        /* connection bits + controller-type bit (type + 15) */
extern u32 g_padState2;
#define PADSTATE_TYPE_MASK 0x7F8000

/* PS Mouse receive buffer: [0] status (0 = ok), [1] type, [2] 0xFF,
 * [3] buttons (0 = pressed), [4] X motion, [5] Y motion (signed).
 * pad_GetAxes takes port 0 or 0x10 (not 1). */
#define PAD_PORT2 0x10
#define MOUSE_LEFT  0x0008
#define MOUSE_RIGHT 0x0004
/* Raw pad buffer [3] (0 = pressed): L2 01, R2 02, L1 04, R1 08,
 * Triangle 10, Circle 20, Cross 40, Square 80. */
#define PAD_TRIANGLE 0x10
#define PAD_CROSS 0x40

/* Logical button bits after input_MapButtons. Names from the in-game key
 * config screen (A-type, the default): Up "forward/run", Down "back",
 * Left/Right "turn", R1 "target", L1 "quick search", Triangle and Cross
 * "OK/attack", Square "cancel". */
#define BTN_UP     0x1000
#define BTN_RIGHT  0x2000
#define BTN_DOWN   0x4000
#define BTN_LEFT   0x8000
#define BTN_AIM    0x0008      /* "target" */
#define BTN_FIRE   0x0020      /* "OK/attack" */

/* ---- player ---- */
extern u32 g_playerMoveFlags;  /* 1 fwd, 2 back, 4 turn R, 8 turn L, 0x40 walk, 0x80 aim */
#define MOVE_FWD        0x1
#define MOVE_BACK       0x2
#define MOVE_TURN_RIGHT 0x4
#define MOVE_TURN_LEFT  0x8
#define MOVE_WALK       0x40
#define MOVE_AIM        0x80
extern s32 g_playerStick;      /* analog snapshot; byte 0 fwd/back, byte 1 turn */
extern u8 D_800B4E01;          /* = byte 1 of g_playerStick (0xFF right, 0 left) */
extern s32 g_playerMoveSpeed;  /* <0 forward (-0x50 walk, -0x96 run), 0x3C back */
extern s16 g_playerYawVel;
extern s16 g_playerTurnRamp;
extern s16 D_800B4DFC;         /* last frame's speed, to spot run <-> walk changes */
extern s16 D_800B4DFE;         /* last frame's yaw velocity */
extern s16 *D_800B4DE4;        /* current movement animation table (0 = pick again) */
extern s32 D_800B4DE8;         /* index into it */
extern s16 D_800B4DF8;         /* forward-step counter (caps at 0x64) */
extern s16 D_800A128C[];       /* animation tables: walk */
extern s16 D_800A12B4[];       /*                   run */
extern s16 D_800A12CC[];       /*                   backstep */
extern s32 D_800A8634;         /* table value - 0x640 (vertical bob) */
extern s32 D_800B6084;

/* Player record. Position x/y/z (s32) at +0x18, rotation SVECTOR at +0x44
 * (yaw at +0x46, 4096 = 360 degrees). */
extern u8 g_player[];
#define PLAYER_POS      ((s32 *)(g_player + 0x18))
#define PLAYER_ROT      ((s16 *)(g_player + 0x44))
#define PLAYER_YAW      (*(s16 *)(g_player + 0x46))
extern u8 g_playerFrozen;

/* player_Update helpers */
extern s32 D_800A12E8;
extern s16 D_800A865C;         /* camera pitch (eye coordinate rot.x, 0 = level) */
extern s32 D_800B4DF0;
extern u8 *D_800B4DEC;         /* +0x31E: s16 */
extern u8 D_800A8618[];
extern u8 D_800BB790[];
extern u8 D_800B3EE8[];
void func_800789F8(void);
void func_80079064(s32 a0);
void func_80078460(void);
void func_80078208(void);
void func_80077F68(void);
void func_800780E4(void);
void func_80079A28(void);
void func_80084B04(void);
void func_80081C80(s32 *adjust, s32 *step);
void func_8006F69C(void *obj, s32 a1);
void func_80082760(void *obj, s32 a1);
void func_80033FD8(void *a0);
void player_Control(void);
/* step = rotate (0, 0, speed) by the rotation angles (libgte RotMatrix +
 * RotTrans), rounded; step needs room for 4 words */
void func_8006F7A8(s16 *rot, s32 *step, s32 speed);
/* 0 = clear: add step to pos; else step x/z hold the corrected position */
s32 move_Collide(s32 *pos, s32 *step, s32 flag);
/* Move the player by step through collision (the original's pattern). */
static inline void player_ApplyStep(s32 *step)
{
    if (move_Collide(PLAYER_POS, step, 0) == 0) {
        PLAYER_POS[0] += step[0];
        PLAYER_POS[2] += step[2];
    } else {
        PLAYER_POS[0] = step[0];
        PLAYER_POS[2] = step[2];
    }
}
/* in KERNEL's unused GunCon routine (src/mods/kernel/func_800110D8.c) */
void player_MoveStep(s16 *rot);   /* rot: direction to step in */
/* defined in player_Control.c */
s32 player_FpsActive(void);
s32 player_MovementLockedArea(void);
extern s32 g_buttonsHi;        /* port 2 buttons (high half of the mapped value) */
#define BTN_RUN    0x0040
#define BTN_QUICKSEARCH 0x0004 /* "quick search": fresh press sets 0x100 in D_800A85B8 */
extern s32 D_800A85B8;
extern s32 D_800A8670;

/* FPS settings block in RAM, written every frame by the PC port
 * (port/src/rt.c) and by the DuckStation patch (tools/make_patches.py):
 * heading and pitch units per mouse count, pitch limit (4096 = 360). */
typedef struct { s32 magic; s16 yaw_scale, pitch_scale, pitch_limit, flags; } PortConfig;
#define PORT_CONFIG ((volatile PortConfig *)0x80011288)   /* spare bytes after KERNEL's func_800110D8 helper */
#define PORT_MAGIC 0x52535043

/* ---- state checks used by player_Control ---- */
extern u8 D_80062DEC;          /* 0 = player can't be controlled right now */
extern u8 D_80062E2C;
extern s32 D_80062DC4;
extern s32 D_800BB19C;
extern s32 D_800A866C;
extern s32 D_800B3F08;
extern u8 D_80062F60[];
s32 func_8008F64C(void *a0, s32 a1);
void func_8007BC7C(void);
s32 pad_GetAxes(s32 port);
void player_AimMode(void);

/* ---- aiming and firing ---- */
extern s32 g_reticleX;         /* screen X, centred at 0xA0 */
extern s32 g_reticleY;         /* screen Y, centred at 0x78 */
extern u16 g_fireCooldown;     /* low 15 bits count down by 2 per frame */
extern s16 *g_currentWeapon;   /* *g_currentWeapon = weapon ID */
extern u8 g_fireRequest;
extern s16 g_weaponState;
extern u16 D_800AFCB0;         /* bit 3 lets a shot through during cooldown flag 0x8000 */
extern s32 D_800A0CB8;         /* set to 1 every frame while aiming */
extern u8 D_80062DF4;          /* 3 = analog stick ignored for the reticle */
extern s16 D_800B3E28[];       /* reticle quad: x,y pairs at [4..11] */
extern s16 D_800BBB98[];       /* reticle sprite: x at [2], y at [3] */

/* fire_AtScreen is called with the reticle-derived values the original
 * left in a0-a3; their meaning isn't confirmed yet. */
void fire_AtScreen(s32 a0, s32 a1, s32 a2, s32 a3);
void func_800805F4(s32 a0, s32 a1, s32 a2, s32 a3);   /* probably a sound cue */

#endif
