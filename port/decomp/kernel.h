/* Shared by the KERNEL game-code natives (kernel_*.c): the kernel's own
 * code (0x80010B00-0x800170B4: start-up, threads, input, sound, movies,
 * the CD queue), see KERNEL_MAP.md.
 *
 * Much of this runs from interrupt callbacks (the vertical-blank callback
 * 0x80010F20 and everything it calls, the CD and MDEC callbacks), on
 * libetc's interrupt stack in main RAM (0x8005Bxxx), which the game can
 * see. So these natives keep the original's stack frames exactly
 * (FRAME), store the registers the original saves where it saves them,
 * call with $ra as the original's jal leaves it (JAL), and keep the
 * callee-saved registers ($s0-$s7, $fp) as the original sets them at
 * every call (callees save them in their own frames), restoring them on
 * return. The same is done everywhere here: it costs little, and the
 * thread code (0x8001306C...) depends on it too, since a thread switched
 * away continues from its saved $ra after a save state is loaded. */
#ifndef KERNEL_H
#define KERNEL_H

#include "libgs.h"
#include "poll.h"

/* a jal: $ra as the original's jal leaves it (the address after the
 * delay slot), the argument registers as given */
#define JAL(ra, f, a, b, c, d) gcall_ra((ra), (f), (u32)(a), (u32)(b), (u32)(c), (u32)(d))

/* MIPS div's remainder (mfhi), with the R3000's results for x % 0. */
static inline s32 mips_rem(s32 x, s32 y) {
    if (y == 0) return x;
    if (x == INT32_MIN && y == -1) return 0;
    return x % y;
}

/* x / 100 the way GCC emits it (multiply by 0x51EB851F, take the high
 * word, shift, correct for the sign). */
static inline s32 div100(s32 x) {
    s32 hi = (s32)(((s64)x * 0x51EB851F) >> 32);
    return (hi >> 5) - (x >> 31);
}

/* 32-bit products as mult/mflo leaves them. */
static inline s32 mul32(s32 a, s32 b) { return (s32)((u32)a * (u32)b); }

/* The callee-saved registers: SAVE_S keeps the caller's, RESTORE_S puts
 * them back (as the original's epilogue does). */
#define SAVE_S u32 in_s0 = R[16], in_s1 = R[17], in_s2 = R[18], in_s3 = R[19], \
                   in_s4 = R[20], in_s5 = R[21], in_s6 = R[22], in_s7 = R[23], in_fp = R[30]; \
               (void)in_s0; (void)in_s1; (void)in_s2; (void)in_s3; (void)in_s4; \
               (void)in_s5; (void)in_s6; (void)in_s7; (void)in_fp
#define RESTORE_S do { R[16] = in_s0; R[17] = in_s1; R[18] = in_s2; R[19] = in_s3; R[20] = in_s4; \
                       R[21] = in_s5; R[22] = in_s6; R[23] = in_s7; R[30] = in_fp; } while (0)

/* Kernel functions called from here (all SDK unless noted). */
#define K_VSYNC            0x800255F8u
#define K_DRAW_SYNC        0x80035374u
#define K_CLEAR_IMAGE      0x800354F8u
#define K_LOAD_IMAGE       0x80035620u
#define K_PUT_DISP_ENV     0x80035BA4u
#define K_PUT_DRAW_ENV     0x800359D8u
#define K_RESET_GRAPH      0x80034FF8u
#define K_RAND             0x800398A8u   /* BIOS rand() */
#define K_ENTER_CRITICAL   0x80039A08u
#define K_EXIT_CRITICAL    0x80039A18u
#define K_CLOSE_THREAD     0x800399B8u
#define K_CHANGE_THREAD    0x800399C8u
#define K_GET_GP           0x80039AB8u
#define K_INIT_GEOM        0x8002A350u   /* keeps $ra in 0x8005C05C */

#endif
