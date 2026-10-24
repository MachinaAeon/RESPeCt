/* Game variables and functions used by the decompiled code, by address
 * (names from symbol_addrs.txt; see include/c/game.h for what they are). */
#ifndef GAME_VARS_H
#define GAME_VARS_H

#include "decomp.h"

/* ---- input ---- */
#define g_buttons        GV(s32, 0x800B4B94)   /* mapped buttons this frame */
#define g_buttonsPrev    GV(s32, 0x800B3CC4)
#define g_inputAxes      GA(u8, 0x800A8840)    /* analog stick bytes */
#define g_padTypeBit     GV(u32, 0x80062E38)   /* 0x80000 digital, 0x400000 DualShock */
#define g_padBuf1Addr    GV(u32, 0x80062EBC)   /* receive buffer pointers (game addresses) */
#define g_padBuf2Addr    GV(u32, 0x80062EC0)
#define g_padState2      GV(u32, 0x80062ED4)
#define PADSTATE_TYPE_MASK 0x7F8000
#define PADTYPE_MOUSE      0x10000
#define MOUSE_LEFT         0x0008

#define BTN_UP     0x1000
#define BTN_RIGHT  0x2000
#define BTN_DOWN   0x4000
#define BTN_LEFT   0x8000
#define BTN_AIM    0x0008      /* "target" */
#define BTN_FIRE   0x0020      /* "OK/attack" */
#define BTN_RUN    0x0040
#define BTN_QUICKSEARCH 0x0004

/* ---- player ---- */
#define g_playerMoveFlags GV(u32, 0x800B6080)
#define MOVE_FWD        0x1
#define MOVE_BACK       0x2
#define MOVE_TURN_RIGHT 0x4
#define MOVE_TURN_LEFT  0x8
#define MOVE_WALK       0x40
#define MOVE_AIM        0x80
#define g_playerStick     GV(s32, 0x800B4E00)
#define D_800B4E01        GV(u8, 0x800B4E01)   /* byte 1 of g_playerStick: turn */
#define g_playerMoveSpeed GV(s32, 0x800B3D48)
#define g_playerYawVel    GV(s16, 0x800B4DFA)
#define g_playerTurnRamp  GV(s16, 0x800A12EC)
#define D_800B4DFC        GV(s16, 0x800B4DFC)   /* last frame's speed */
#define D_800B4DFE        GV(s16, 0x800B4DFE)   /* last frame's yaw velocity */
#define D_800B4DE4        GV(u32, 0x800B4DE4)   /* movement animation table (game address, 0 = pick) */
#define D_800B4DE8        GV(s32, 0x800B4DE8)   /* index into it */
#define D_800B4DF8        GV(s16, 0x800B4DF8)   /* forward-step counter */
#define ANIM_WALK         0x800A128Cu
#define ANIM_RUN          0x800A12B4u
#define ANIM_BACK         0x800A12CCu
#define D_800A8634        GV(s32, 0x800A8634)   /* vertical bob */
#define D_800B6084        GV(s32, 0x800B6084)
#define g_player          0x800A85C0u           /* player record (address) */
#define PLAYER_POS_ADDR   (g_player + 0x18)
#define PLAYER_ROT_ADDR   (g_player + 0x44)
#define PLAYER_POS        GA(s32, PLAYER_POS_ADDR)
#define PLAYER_ROT        GA(s16, PLAYER_ROT_ADDR)
#define PLAYER_YAW        GV(s16, g_player + 0x46)
#define g_playerFrozen    GV(u8, 0x800B6088)
#define D_800A12E8        GV(s32, 0x800A12E8)   /* camera pitch levelled */
#define D_800A865C        GV(s16, 0x800A865C)   /* camera pitch (eye rot.x) */
#define D_800B4DF0        GV(s32, 0x800B4DF0)
#define D_800B4DEC        GV(u32, 0x800B4DEC)   /* quick search target (address) */
#define D_800A85B8        GV(s32, 0x800A85B8)   /* quick search state */
#define D_800A8670        GV(s32, 0x800A8670)
#define CAMERA_OBJ        0x800A8618u
#define D_800BB790_ADDR   0x800BB790u
#define D_800B3EE8_ADDR   0x800B3EE8u

/* ---- state checks ---- */
#define D_80062DEC        GV(u8, 0x80062DEC)    /* player can act */
#define D_80062DF4        GV(u8, 0x80062DF4)
#define D_800BB19C        GV(s32, 0x800BB19C)
#define D_800A866C        GV(s32, 0x800A866C)
#define D_800B3F08        GV(s32, 0x800B3F08)
#define D_80062F60_ADDR   0x80062F60u

/* ---- aiming and firing ---- */
#define g_reticleX        GV(s32, 0x80062DDC)
#define g_reticleY        GV(s32, 0x80062DE0)
#define g_fireCooldown    GV(u16, 0x80062DBC)
#define g_currentWeaponAddr GV(u32, 0x800A8580)
#define g_fireRequest     GV(u8, 0x80062E40)
#define g_weaponState     GV(s16, 0x80062E2E)
#define D_800AFCB0        GV(u16, 0x800AFCB0)
#define D_800A0CB8        GV(s32, 0x800A0CB8)
#define D_800B3E28        GA(s16, 0x800B3E28)   /* reticle quad */
#define D_800BBB98        GA(s16, 0x800BBB98)   /* reticle sprite */

/* ---- game functions ---- */
#define F_PAD_GET_AXES      0x80013D18u
#define F_ROT_STEP          0x8006F7A8u   /* (rot, step, speed): step = rotate (0,0,speed) */
#define F_OBJ_MATRIX        0x8006F69Cu   /* (obj, scale): coordinate matrix from rot/pos */
#define F_MOVE_COLLIDE      0x8007CB9Cu   /* (pos, step, flag): 0 = clear */
#define F_FIRE_AT_SCREEN    0x8008DF08u
#define F_SOUND_CUE         0x800805F4u
#define F_AREA_CHECK        0x8008F64Cu
#define F_CONTROL_PRE       0x8007BC7Cu
#define F_PITCH_LEVEL       0x800789F8u   /* ease the camera pitch back to level */
#define F_QUICK_SEARCH      0x80079064u
#define F_UPDATE_PRE        0x80078460u
#define F_MOVE_ANIM_END     0x80078208u
#define F_MOVE_PRE          0x80077F68u
#define F_MOVE_POST         0x800780E4u
#define F_STEP_SOUND        0x80084B04u
#define F_ANIM_OFFSET       0x80081C80u   /* (adjust, step) */
#define F_TRIGGERS          0x80079A28u   /* walk-into trigger zones */
#define F_UPDATE_TAIL1      0x80082760u
#define F_UPDATE_TAIL2      0x80033FD8u

#endif
