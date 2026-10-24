/* Stage program 29 (STAGPROG overlay 29: stage 14, the arcade and the
 * casino, area57 / area58): its per-frame update (the third entry of its
 * function table, 0x800F2FE0) does nothing.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"

void n_stage29_Update(void);

/* 800F2E24: nothing; returns 0. */
void n_stage29_Update(void) {
    V0 = 0;
}
