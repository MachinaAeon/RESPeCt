/* Flag sets and the high-memory allocator (TEST overlay).
 *
 * Flag sets are bit arrays, most significant bit first in each word: the
 * game's progress flags (0x80062FC0), the room's (0x80062FA0), and
 * others. High memory is handed out downwards from 0x801F8300, for a
 * room's files (events, sounds) that live until the next room.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "decomp.h"

void n_flag_Set(void);
void n_flag_Test(void);
void n_himem_Reset(void);
void n_himem_Alloc(void);

#define g_himemTop GV(u32, 0x800BBCD0)

/* 8008F5F8 (set, flag) */
void n_flag_Set(void) {
    u32 w = A0 + (A1 >> 5) * 4;
    u32 bit = 0x80000000u >> (A1 & 0x1F);
    GS(u32, w) |= bit;
    V0 = bit;
}

/* 8008F64C (set, flag): nonzero when set. */
void n_flag_Test(void) {
    u32 w = A0 + (A1 >> 5) * 4;
    V0 = GS(u32, w) & (0x80000000u >> (A1 & 0x1F));
}

/* 8008F670: free all of high memory. */
void n_himem_Reset(void) {
    g_himemTop = 0x801F8300u;
    V0 = 0x800C0000u;
}

/* 8008F684 (size): take size bytes; their address. */
void n_himem_Alloc(void) {
    g_himemTop -= A0;
    V0 = g_himemTop;
}
