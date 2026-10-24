/* Game threads (KERNEL, 0x80012DDC-0x800131E8): the kernel runs the game
 * modes (title, game, options...) as BIOS threads next to its main loop.
 *
 * Three slots of 0x80 bytes at 0x80063AD8: +0 (u16) state, +4 (u16)
 * flags (2: start wanted, 0x40: held), +8 entry point, +0xC thread
 * handle, +0x10 the thread's register block (from the BIOS's thread
 * table), +0x14 its stack top, +0x48 $gp. 0x80063C5C points at the slot
 * of the thread running; its +0 is 1 while it waits for a mode switch
 * (+2 the mode), 2 after it ended (+4 its result), +0x50 where it
 * switched from. A thread hands control back to the main thread
 * (0xFF000000) with ChangeThread; the main loop (0x8001107C, stays
 * translated) switches to it again next frame.
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). See kernel.h for the frame conventions: a
 * thread switched away continues from its saved $ra after a save state
 * is loaded, in the translations, so these keep the original's frames
 * and return addresses exactly. */
#include "kernel.h"

#define K_THREADS      0x80063AD8u
#define K_THREAD_SIZE  0x80u
#define k_curThread    GV(u32, 0x80063C5C)
#define K_MAIN_THREAD  0xFF000000u

/* 80012DDC: the three slots: flags cleared, register blocks (BIOS
 * thread table at 0x110, blocks of 0xC0 from the second: SR 0x404 at
 * +0x94), $gp, and the stacks (0x801FFA00, 0x801FF200, 0x801FEF00). */
void n_thread_InitSlots(void) {
    FRAME(0x28);
    SAVE_S;
    GS(u32, sp + 0x20) = in_s4;
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x1C) = in_s3;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x24) = R[31];
    R[20] = 0x100; R[19] = 0x404;
    u32 slot = K_THREADS, off = 0xC0;
    for (u32 i = 0;; i++) {
        GV(u16, slot + 4) = 0;
        u32 regs = GV(u32, 0x110) + off;
        off += 0xC0;
        GV(u32, slot + 0x10) = regs;
        GV(u32, regs + 0x94) = 0x404;
        R[16] = slot; R[17] = off; R[18] = i + 1;
        GV(u32, slot + 0x48) = JAL(0x80012E30u, K_GET_GP, A0, A1, A2, A3);
        slot += K_THREAD_SIZE;
        if (i + 1 >= 3) break;
        LOOP_POLL(0x80012E38u);
    }
    GV(u32, K_THREADS + 0x14) = 0x801FFA00u;
    GV(u32, K_THREADS + 0x94) = 0x801FF200u;
    GV(u32, K_THREADS + 0x114) = 0x801FEF00u;
    V0 = K_THREADS;
    RESTORE_S;
}

/* 8001304C (slot, entry): a thread to start. */
void n_thread_Set(void) {
    u32 slot = K_THREADS + (A0 << 7);
    GV(u32, slot + 8) = A1;
    GV(u16, slot + 4) = 2;
    A0 = slot;
    V0 = 2;
}

/* 8001306C (mode): from a game thread: switch modes. Marks the thread
 * waiting with the mode, keeps where it was called from (0x80062EB0
 * and the slot's +0x50), and switches to the main thread. */
void n_thread_ModeSwitch(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    u32 t = k_curThread;
    GV(u16, t + 2) = (u16)A0;
    GV(u16, t) = 1;
    GV(u32, 0x80062EB0) = R[31];
    GV(u32, t + 0x50) = GV(u32, 0x80062EB0);
    JAL(0x800130A4u, K_CHANGE_THREAD, K_MAIN_THREAD, 0x80062EB0u, A2, A3);
}

/* 800130B4: a game thread's end: state 0, close itself, back to the
 * main thread. */
void n_thread_Exit(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = R[31];
    u32 t = k_curThread;
    R[16] = t;
    GV(u16, t) = 0;
    JAL(0x800130D0u, K_ENTER_CRITICAL, A0, A1, A2, A3);
    JAL(0x800130DCu, K_CLOSE_THREAD, GV(u32, t + 8), A1, A2, A3);
    JAL(0x800130E4u, K_EXIT_CRITICAL, A0, A1, A2, A3);
    JAL(0x800130ECu, K_CHANGE_THREAD, K_MAIN_THREAD, A1, A2, A3);
    RESTORE_S;
}

/* 800130FC (slot): close a slot's thread if it has one. */
void n_thread_Close(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = R[31];
    u32 s = K_THREADS + 4 + (A0 << 7);
    A0 <<= 7;
    R[16] = s;
    V0 = GV(u16, s);
    if (V0 != 0) {
        GV(u16, s) = 0;
        JAL(0x80013130u, K_ENTER_CRITICAL, A0, A1, A2, A3);
        JAL(0x8001313Cu, K_CLOSE_THREAD, GV(u32, s + 8), A1, A2, A3);
        JAL(0x80013144u, K_EXIT_CRITICAL, A0, A1, A2, A3);
    }
    RESTORE_S;
}

/* 80013154 (result): a game thread ends with a result: state 2, the
 * result at +4, close itself, back to the main thread. */
void n_thread_Return(void) {
    FRAME(0x18);
    SAVE_S;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x14) = R[31];
    u32 t = k_curThread;
    R[16] = t;
    GV(u16, t) = 2;
    GV(u32, t + 4) = A0;
    JAL(0x80013178u, K_ENTER_CRITICAL, A0, A1, A2, A3);
    JAL(0x80013184u, K_CLOSE_THREAD, GV(u32, t + 8), A1, A2, A3);
    JAL(0x8001318Cu, K_EXIT_CRITICAL, A0, A1, A2, A3);
    JAL(0x80013194u, K_CHANGE_THREAD, K_MAIN_THREAD, A1, A2, A3);
    RESTORE_S;
}

/* 800131A4 / 800131C8 (slot): hold / release a slot (flag 0x40). */
void n_thread_Hold(void) {
    u32 f = K_THREADS + (A0 << 7) + 4;
    A0 = f - 4;
    V0 = GV(u16, f) | 0x40;
    GV(u16, f) = (u16)V0;
}
void n_thread_Release(void) {
    u32 f = K_THREADS + (A0 << 7) + 4;
    A0 = f - 4;
    V0 = GV(u16, f) & 0xFFBF;
    GV(u16, f) = (u16)V0;
}
