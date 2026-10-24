/* Keeping the original's time (for the natives in room_loader.c,
 * room_triggers.c, quick_search.c and death_continue.c).
 *
 * Translated code spends a little of the poll budget at every backward
 * branch it takes (RT_POLL_AT, recomp.h); when the budget runs out the
 * hardware clock advances and pending interrupts run. A native spends the
 * budget at the same points, so the interrupts (vertical blank, CD, sound)
 * come at the same moments as under the original.
 *
 * LOOP_POLL(pc) goes where the original takes a backward branch (or a jump
 * table, which the translation polls too), pc the branch's address. It
 * is RT_POLL_NOSAVE: a save state can't be made inside a native. */
#ifndef DECOMP_POLL_H
#define DECOMP_POLL_H

#include "decomp.h"

#define LOOP_POLL(pc) RT_POLL_NOSAVE(pc)

/* Copy a 0x8C4-byte object record the way the game's inlined memcpy does:
 * 16 bytes per loop pass (the loop branch at pc), then the last word. */
static inline void copy_record_polled(u32 dst, u32 src, u32 pc) {
    for (u32 k = 0;; k += 0x10) {
        for (u32 j = 0; j < 0x10; j += 4) GV(u32, dst + k + j) = GV(u32, src + k + j);
        if (k + 0x10 == 0x8C0) break;
        LOOP_POLL(pc);
    }
    GV(u32, dst + 0x8C0) = GV(u32, src + 0x8C0);
}

#endif
