/* Stage program 25 (STAGPROG overlay 25: stage 10): the per-frame room
 * hook, which does nothing.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"

void n_stage25_RoomFrame(void);

/* 800F2E68: every frame: nothing. Returns 0. */
void n_stage25_RoomFrame(void) {
    V0 = 0;
}
