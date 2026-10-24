/* Native C replacing recompiled game functions (the decompilation).
 *
 * A function listed in tools/recomp/native.txt isn't translated from the
 * game's machine code; the recompiler calls n_<name>() from port/decomp
 * instead. These functions work on the game's own data in its RAM (so
 * the rest of the game, still recompiled, sees every change) and call
 * back into game code with gcall().
 *
 * Conventions (MIPS o32, like the code they replace):
 *   arguments arrive in A0..A3, a result goes in V0;
 *   game pointers are 32-bit game addresses: gp() turns one into a host
 *   pointer, ga() turns a host pointer into RAM back into an address;
 *   a local the game must see goes on the game's stack (GameStack).
 */
#ifndef DECOMP_H
#define DECOMP_H

#include "recomp.h"

typedef int8_t s8;
typedef uint8_t u8;
typedef int16_t s16;
typedef uint16_t u16;
typedef int32_t s32;
typedef uint32_t u32;
typedef int64_t s64;

#define A0 R[4]
#define A1 R[5]
#define A2 R[6]
#define A3 R[7]
#define V0 R[2]

/* The game variable of this type at this address (an lvalue). */
#define GV(type, addr) (*(type *)(g_ram + ((addr) & 0x1FFFFF)))
/* An array in game RAM. */
#define GA(type, addr) ((type *)(g_ram + ((addr) & 0x1FFFFF)))

/* Game address -> host pointer: main RAM, or the scratchpad (1 KB of fast
 * RAM at 0x1F800000, where the game sometimes keeps its stack). */
static inline void *gp(u32 addr) {
    u32 p = addr & 0x1FFFFFFF;
    if (p >= 0x1F800000 && p < 0x1F800400) return g_scratch + (p & 0x3FF);
    return g_ram + (addr & 0x1FFFFF);
}
static inline u32 ga(const void *p) {
    const u8 *b = p;
    if (b >= g_scratch && b < g_scratch + SCRATCH_SIZE) return 0x1F800000u | (u32)(b - g_scratch);
    return 0x80000000u | (u32)(b - g_ram);
}

/* Call game code at addr with up to four arguments; returns V0. */
extern int g_native_depth;                 /* savestate.c: no saves in here */
static inline u32 gcall(u32 addr, u32 a0, u32 a1, u32 a2, u32 a3) {
    u32 ra = R[31];
    R[4] = a0; R[5] = a1; R[6] = a2; R[7] = a3;
    g_native_depth++;
    rt_call(addr);
    g_native_depth--;
    R[31] = ra;
    return R[2];
}
#define GCALL0(f) gcall((f), 0, 0, 0, 0)
#define GCALL1(f, a) gcall((f), (u32)(a), 0, 0, 0)
#define GCALL2(f, a, b) gcall((f), (u32)(a), (u32)(b), 0, 0)
#define GCALL3(f, a, b, c) gcall((f), (u32)(a), (u32)(b), (u32)(c), 0)
#define GCALL4(f, a, b, c, d) gcall((f), (u32)(a), (u32)(b), (u32)(c), (u32)(d))

/* MIPS div: quotient, with the R3000's results for x / 0 and MIN / -1. */
static inline s32 mips_div(s32 x, s32 y) {
    if (y == 0) return x >= 0 ? -1 : 1;
    if (x == INT32_MIN && y == -1) return INT32_MIN;
    return x / y;
}

/* A variable on the game's stack: that can be in the scratchpad (sp
 * 0x1F80xxxx), which GV/GA (main RAM only) would miss. */
#define GS(type, addr) (*(type *)gp(addr))

/* Space on the game's stack for locals that game functions read or write
 * (access them with GS).
 * The bottom 16 bytes of a MIPS frame belong to the callee (o32 argument
 * home area: functions may store a0-a3 there), so locals go above them. */
typedef struct { u32 size; } GameStack;
static inline u32 gstack_alloc(GameStack *s, u32 n) {
    s->size = ((n + 7) & ~7u) + 16;
    R[29] -= s->size;
    return R[29] + 16;
}
static inline void gstack_free(GameStack *s) { R[29] += s->size; }
/* The original's stack frame: FRAME(size) as a native's first statement,
 * size from the original's "addiu sp, sp, -size", kept until it returns.
 * Game code called from the native then runs at the same stack addresses
 * as under the original, and the native's own stack locals (`sp + offset`,
 * the offsets in the disassembly; access with GS) and arguments 5 and up
 * (sp + 0x10, 0x14...) are where the original had them. That matters when
 * a stack address ends up in memory: a callee that keeps a pointer it was
 * given (move_Collide does), or a frame pointer the game saves. */
static inline u32 frame_enter_(u32 size) { R[29] -= size; return R[29]; }
static inline void frame_leave_(u32 *size) { R[29] += *size; }
#define FRAME(size) \
    __attribute__((cleanup(frame_leave_))) u32 frame_size_ = (size); \
    __attribute__((unused)) const u32 sp = frame_enter_(frame_size_)

#endif
