/* Images TEST sets up when it starts (TEST overlay): the aiming reticle's
 * sprite, and the text strip of the opening lines (shown by 0x8008F3BC).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "libgs.h"

void n_reticle_LoadImage(void);
void n_opening_SetupText(void);

#define ENEMY_FILES  0x800A3EDCu
#define F_TIM_LOAD   0x8006F370u
#define F_VRAM_CLEAR 0x8006F40Cu   /* (x, y, w) */
#define OFF_X        GV(u16, 0x80062E18)   /* screen offset */
#define OFF_Y        GV(u16, 0x80062E1A)

/* 8008F0E0: load the reticle's image (file 0x50) and set up its GsSPRITE
 * (0x800BBB98): 16x16 at texture 0xA0 in page (512, 416), clut (0x150,
 * 0x1E8). */
void n_reticle_LoadImage(void) {
    FRAME(0x20);
    const u32 buf = 0x800C3E50u;
    GS(u32, sp + 0x10) = buf;
    GCALL4(F_FILE_LOAD, 0x8006C6A8u, 0x8005855Cu, ENEMY_FILES, 0x50);
    GCALL1(F_TIM_LOAD, buf);
    const u32 s = 0x800BBB98u;
    GV(u32, s) = 0;
    GV(u16, s + 0xA) = 0x10;
    GV(u16, s + 0x8) = 0x10;
    GV(u16, s + 0xC) = (u16)GCALL4(F_GET_TPAGE, 0, 0, 0x200, 0x1A0);
    GV(u8, s + 0xF) = 0xA0;
    GV(u16, s + 0x10) = 0x150;
    GV(u16, s + 0x12) = 0x1E8;
    GV(u8, s + 0xE) = 0;
    GV(u8, s + 0x16) = 0x80;
    GV(u8, s + 0x15) = 0x80;
    GV(u8, s + 0x14) = 0x80;
    GV(u16, s + 0x1A) = 0;
    GV(u16, s + 0x18) = 0;
    GV(u16, s + 0x1E) = 0;
    GV(u16, s + 0x1C) = 0;
    GV(u32, s + 0x20) = 0;
    V0 = 0x80;
}

/* 8008F1F0: clear VRAM, load the opening lines' text image (file 0x51)
 * and set up a textured quad per buffer (0x800BBBC0, POLY_FT4) at
 * (80..336, 180..220) on screen. */
void n_opening_SetupText(void) {
    FRAME(0x30);
    GCALL4(F_VRAM_CLEAR, 0, 0, 0x140, 0x200);
    GS(u32, sp + 0x10) = 0x80150000u;
    GCALL4(F_FILE_LOAD, 0x8006C6A8u, 0x8005855Cu, ENEMY_FILES, 0x51);
    GCALL1(F_TIM_LOAD, 0x80150000u);
    for (u32 q = 0x800BBBC0u; q < 0x800BBBC0u + 0x50; q += 0x28) {
        GCALL1(F_SET_POLY_FT4, q);
        GV(u8, q + 4) = 0x80;
        GV(u8, q + 5) = 0x80;
        GV(u8, q + 6) = 0x80;
        GV(u16, q + 0x08) = (u16)(OFF_X + 0x50);
        GV(u16, q + 0x0A) = (u16)(OFF_Y + 0xB4);
        GV(u16, q + 0x10) = (u16)(OFF_X + 0x150);
        GV(u16, q + 0x12) = (u16)(OFF_Y + 0xB4);
        GV(u16, q + 0x18) = (u16)(OFF_X + 0x50);
        GV(u16, q + 0x1A) = (u16)(OFF_Y + 0xDC);
        GV(u16, q + 0x20) = (u16)(OFF_X + 0x150);
        GV(u8, q + 0x1D) = 0x28;
        GV(u8, q + 0x25) = 0x28;
        GV(u8, q + 0x0C) = 0;
        GV(u8, q + 0x0D) = 0;
        GV(u8, q + 0x14) = 0xFF;
        GV(u8, q + 0x15) = 0;
        GV(u8, q + 0x1C) = 0;
        GV(u8, q + 0x24) = 0xFF;
        GV(u16, q + 0x22) = (u16)(OFF_Y + 0xDC);
        if (BUF_INDEX) GCALL4(F_GS_SET_DRAW_OFFSETS, OFF_X, OFF_Y, 0, 0x100);
        else GCALL4(F_GS_SET_DRAW_OFFSETS, 0, 0, OFF_X, (u16)(OFF_Y + 0x100));
        GV(u16, q + 0xE) = (u16)GCALL2(F_GET_CLUT, 0x140, 0x1F1);
        GV(u16, q + 0x16) = (u16)GCALL4(F_GET_TPAGE, 0, 0, 0x200, 0);
    }
}
