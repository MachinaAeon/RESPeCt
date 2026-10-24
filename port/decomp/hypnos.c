/* The final boss's second form (enemy program ENEMPROG 48, loaded in
 * enemy slot A at 0x800F8000; enemy type 0x21, 900 hit points). It comes
 * in when the first form (type 0x20, in slot B) is beaten: the tyrant
 * Hypnos mutated.
 *
 * Only its set-up is decompiled.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "enemprog.h"

void n_hypnos_Setup(void);

#define HYPNOS_HIT_FACTORS  0x800FC620u
#define HYPNOS_PARAMS       0x800FC818u
#define HYPNOS_MODEL        0x800FC714u   /* its hit-part table */
#define HYPNOS_WORK         0x800FC89Cu   /* 0x1C bytes per enemy slot */

/* 800FC160 (spawn, e): set it up: its tables, walking speed, hit-part
 * table for its program slot, and its work bytes 2-11 cleared. */
void n_hypnos_Setup(void) {
    FRAME(0x20);
    u32 spawn = A0, e = A1;
    GV(u32, e + 0x840) = HYPNOS_HIT_FACTORS;
    u8 slot = EN_SLOT(e);
    GV(u32, e + 0x844) = HYPNOS_PARAMS;
    EN_SPEED(e) = (s16)gcall(FN_EN_BASE_SPEED, e, 0, 0, 0);
    GV(u32, D_800BBB80_ADDR + GS(u32, spawn) * 4) = HYPNOS_MODEL;
    u32 w = HYPNOS_WORK + (u32)slot * 0x1C;
    GV(u8, w + 3) = 0;
    GV(u8, w + 2) = 0;
    for (u32 k = 4; k < 0xC; k++) GV(u8, w + k) = 0;
    V0 = w;
}
