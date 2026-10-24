/* Palette animation (TEST overlay; used by the stage programs): copy a
 * 16-colour CLUT row within VRAM.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "decomp.h"

void n_clut_CopyRow(void);

#define F_MOVE_IMAGE 0x800356E0u   /* (rect, x, y) */

/* 80092D44 (x, y, dy): copy the CLUT at (x, y + dy) to (x, y). */
void n_clut_CopyRow(void) {
    FRAME(0x20);
    u32 x = A0, y = A1;
    GS(u16, sp + 0x14) = 0x10;
    GS(u16, sp + 0x10) = (u16)x;
    GS(u16, sp + 0x12) = (u16)(y + A2);
    GS(u16, sp + 0x16) = 1;
    GCALL3(F_MOVE_IMAGE, sp + 0x10, (u32)(s32)(s16)x, (u32)(s32)(s16)y);
}
