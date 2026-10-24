/* Small helpers shared by the room, fade, HUD and effect natives (stack
 * frames and stack variables are FRAME / GS in decomp.h). */
#ifndef GAME_STACK_H
#define GAME_STACK_H

#include "decomp.h"

/* jalr: call game code through a pointer, argument registers as they are. */
static inline u32 jalr_call(u32 fn) {
    u32 ra = R[31];
    g_native_depth++;
    rt_call(fn);
    g_native_depth--;
    R[31] = ra;
    return R[2];
}

/* MIPS div remainder (mfhi), with the R3000's results for x % 0 and
 * MIN % -1. */
static inline s32 mips_rem(s32 x, s32 y) {
    if (y == 0) return x;
    if (y == -1) return 0;
    return x % y;
}

/* x / 2^k rounded toward zero, the way GCC divides a signed int by a
 * power of two (add 2^k - 1 when negative, then shift). */
static inline s32 div_pow2(s32 x, int k) {
    if (x < 0) x += (1 << k) - 1;
    return x >> k;
}

#endif
