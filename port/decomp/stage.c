/* Stage control (TEST overlay): starting a stage and running its own
 * code, the screen flash, and the pause while the controller is out.
 *
 * Each stage has a table of three functions (0x800B4BAC points to it):
 * +0 its start, +4 run once a frame (draw), +8 its update (while the
 * player is alive and the input isn't locked). The stage's loader is
 * picked from 0x800A2698 by stage number (0x800B4BA0).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"

void n_pad_CheckRemoved(void);
void n_flash_Request(void);
void n_flash_Draw(void);
void n_stage_Start(void);
void n_stage_Run(void);
void n_stage_Update(void);

#define g_stage          GV(s32, 0x800B4BA0)
#define g_stageVariant   GV(s32, 0x800B3E20)
#define g_stageFuncs     GV(u32, 0x800B4BAC)   /* {start, run, update} (address) */
#define g_curStage       GV(s32, 0x800A866C)
#define g_prevStage      GV(s32, 0x800B3D4C)
#define STAGE_LOADERS    0x800A2698u
#define TRIGGERS         0x800C3A20u           /* 5 x 0x14, word 0 = -1: free */
#define HP               GV(s16, 0x800A8584)
#define g_inputLocked    GV(s16, 0x800B3D2C)
#define g_flash          GV(u32, 0x800BB060)   /* bit 0: draw the flash this frame */
#define FLASH_TILE       0x800BB050u           /* a TILE primitive */
#define FLASH_OTS        0x800B4BB8u           /* per buffer, 0x14 bytes */
#define g_padRemoved     GV(s16, 0x800BAAA2)   /* paused: no controller */
#define g_padRemovedLast GV(s16, 0x800BAAA4)

#define F_PAD_TYPE       0x80013B90u   /* (port): pad type bits */
#define F_PAUSE_RESET    0x8007EF44u
#define F_PAUSE_SCREEN   0x8007EBDCu
#define F_ADD_PRIM2      0x8002BDA8u
#define F_FILE_LOAD      0x800115C0u
#define F_LOAD_STAGE_BANK 0x80080FCCu
#define F_HUD_STAGE      0x80085E14u
#define F_AREA_RESET     0x8008F014u
#define F_STAGE_EVENT    0x800908DCu

/* 8007EF50: in play, when the controller has been pulled out (pad type
 * 0x8000) pause, and while paused run the pause screen (reset on the
 * first frame). */
void n_pad_CheckRemoved(void) {
    FRAME(0x18);
    u32 type = GCALL1(F_PAD_TYPE, 0);
    if (D_80062DF4 == 3) return;
    if (D_80062DEC == 0 && D_800BB19C == 1) return;
    if (HP <= 0) return;
    if (GV(u16, 0x800A55A2) & 4) return;
    if (type == 0x8000) g_padRemoved = 1;
    if (g_padRemoved != 0) {
        if (g_padRemovedLast == 0) GCALL1(F_PAUSE_RESET, type);
        GCALL1(F_PAUSE_SCREEN, type);
    }
    g_padRemovedLast = (s16)GV(u16, 0x800BAAA2);
}

/* 800825E4: flash the screen red this frame (see flash_Draw). */
void n_flash_Request(void) {
    u32 f = g_flash;
    if (!(f & 1)) g_flash = f | 1;
    V0 = f | 1;
}

/* 80082608: draw the requested flash: a full-screen red tile. */
void n_flash_Draw(void) {
    FRAME(0x18);
    if (!(g_flash & 1)) { V0 = 0; return; }
    GV(u32, FLASH_TILE) = 0x70000000u;
    GV(s16, FLASH_TILE + 4) = -0xA0;
    GV(s16, FLASH_TILE + 6) = -0x78;
    GV(s16, FLASH_TILE + 8) = 0x13F;
    GV(s16, FLASH_TILE + 0xA) = 0xEF;
    GV(u8, FLASH_TILE + 0xC) = 0xFF;
    GV(u8, FLASH_TILE + 0xD) = 0;
    GV(u8, FLASH_TILE + 0xE) = 0;
    GCALL3(F_ADD_PRIM2, FLASH_TILE, FLASH_OTS + (u32)GV(u8, 0x80062D1C) * 0x14, 1);
    u32 f = g_flash & ~1u;
    g_flash = f;
    V0 = f;
}

static void clear_triggers(void) {
    for (s32 i = 4; i >= 0; i--) GV(s32, TRIGGERS + (u32)i * 0x14) = -1;
}

/* 80082E04: start the stage: its sound bank; if it's a different stage,
 * load its code and run its loader; then its start function, the HUD and
 * areas, the triggers cleared, and the stage's draw distances. */
void n_stage_Start(void) {
    FRAME(0x28);
    GCALL0(F_LOAD_STAGE_BANK);
    s32 cur = g_curStage;
    g_prevStage = cur;
    if (cur != g_stage) {
        GS(u32, sp + 0x10) = GV(u32, 0x80010044);         /* argument 5 */
        GCALL4(F_FILE_LOAD, 0x8006C3F4, 0x80058558, 0x800A261C, (u32)g_stage);
        s32 st = g_stage;
        g_stageFuncs = 0;
        GV(u32, 0x800BBB5C) = 0;
        g_curStage = st;
        gcall(GV(u32, STAGE_LOADERS + (u32)st * 4), (u32)st, R[5], R[6], R[7]);
    }
    GV(s16, 0x800AF568) = 0;
    gcall(GV(u32, g_stageFuncs), R[4], R[5], R[6], R[7]);
    GCALL0(F_HUD_STAGE);
    GCALL0(F_AREA_RESET);
    clear_triggers();
    s16 d = (g_stage == 8 && g_stageVariant == 3) ? 0xE0 : 0x58;
    GV(s16, 0x80062D3A) = d;
    GV(s16, 0x80062D38) = d;
    GV(s16, 0x80062D36) = d;
}

/* 80082F3C: once a frame: the stage's run function, then its event (if
 * one is set in 0x800B3D34). */
void n_stage_Run(void) {
    FRAME(0x18);
    gcall(GV(u32, g_stageFuncs + 4), R[4], R[5], R[6], R[7]);
    if (GV(s16, 0x800B3D34) != -1) GCALL0(F_STAGE_EVENT);
}

/* 80082F88: the stage's update (when the player is alive and in control),
 * then the triggers are cleared. */
void n_stage_Update(void) {
    FRAME(0x18);
    if (g_inputLocked != 0) return;
    if (HP <= 0) return;
    gcall(GV(u32, g_stageFuncs + 8), R[4], R[5], R[6], R[7]);
    clear_triggers();
}
