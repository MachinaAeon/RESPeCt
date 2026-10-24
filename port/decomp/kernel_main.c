/* The kernel's main program (KERNEL, 0x80010B00-0x800116A0, 0x80011734):
 * the game-mode threads' bodies, the vertical-blank callback, the main
 * loop's mode switch, the random number, a whole-file read.
 *
 * The main loop (0x8001107C, stays translated: it never returns) calls
 * kernel_ModeSwitch, runs the game threads (0x80012E8C) and waits for the
 * vertical blank. The game mode wanted is 0x80062DFC (1-5, 0: none);
 * slot 0 runs the mode's thread, slots 1 and 2 an idle body.
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). See kernel.h for the frame conventions. */
#include "kernel.h"

#define k_mode         GV(u8, 0x80062DFC)   /* game mode wanted (0: none) */
#define k_frameCb      GV(u32, 0x80062E28)  /* per-frame callback (render_Flip) */
#define k_frameCb2     GV(u32, 0x80062E14)
#define k_attract      GV(u8, 0x80062DF4)   /* 3: attract demo (fixed input) */
#define k_padTypeBit   GV(u32, 0x80062E38)
#define k_padTypeBit2  GV(u32, 0x80062E3C)
#define k_frames       GV(u32, 0x80062E10)  /* the main loop's frame count */

#define K_THREAD_SET    0x8001304Cu
#define K_MODE_SWITCH   0x8001306Cu
#define K_THREAD_WAIT   0x80012FF4u          /* (mode): stays translated */
#define K_THREAD_EXIT   0x800130B4u
#define K_THREAD_CLOSE  0x800130FCu
#define K_THREADS_INIT  0x80012DDCu
#define K_IDLE_BODY     0x80010DD8u

/* 80010B00: does nothing. */
void n_kernel_Nothing(void) {}

/* 80010DD8: the idle threads' body: the GTE set-up, then end. */
void n_kernel_IdleThread(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    JAL(0x80010DE8u, K_INIT_GEOM, A0, A1, A2, A3);
    JAL(0x80010DF0u, K_THREAD_EXIT, A0, A1, A2, A3);
}

/* 80010E00, 80010E60: mode threads (game modes 2 and 4): the GTE
 * set-up, switch to mode 1, then wait for the mode. */
static void mode_thread(u32 ra0, u32 mode) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    JAL(ra0, K_INIT_GEOM, A0, A1, A2, A3);
    JAL(ra0 + 8, K_MODE_SWITCH, 1, A1, A2, A3);
    JAL(ra0 + 0x10, K_THREAD_WAIT, mode, A1, A2, A3);
}
void n_kernel_ModeThread2(void) { mode_thread(0x80010E10u, 2); }
void n_kernel_ModeThread4(void) { mode_thread(0x80010E70u, 4); }

/* 80010F20: the vertical-blank callback: controller types (fixed to a
 * digital pad in the attract demo; a GunCon on port 1 counts as 0x8000),
 * the pads, the sound updates (when 0x80062D35), the sound library's
 * tick, the CD queue, the GunCon, then the frame callback. */
void n_kernel_VSyncCallback(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    if (k_attract == 3) {
        k_padTypeBit = 0x80000;
        k_padTypeBit2 = 0x80000;
    } else {
        k_padTypeBit = JAL(0x80010F50u, 0x80013B90u, 0, A1, A2, A3);
        k_padTypeBit2 = JAL(0x80010F5Cu, 0x80013B90u, 1, A1, A2, A3);
        if (k_padTypeBit == 0x200000) k_padTypeBit = 0x8000;
    }
    JAL(0x80010F7Cu, 0x800136B0u, A0, A1, A2, A3);
    if (GV(u8, 0x80062D35)) {
        JAL(0x80010F94u, 0x80012204u, A0, A1, A2, A3);
        GV(u16, 0x80062E0E) = (u16)JAL(0x80010F9Cu, 0x80012DD0u, A0, A1, A2, A3);
    }
    JAL(0x80010FA8u, 0x80019868u, A0, A1, A2, A3);
    GV(u32, 0x80062E48) = JAL(0x80010FB0u, 0x800163E8u, A0, A1, A2, A3);
    if (k_padTypeBit == 0x200000) JAL(0x80010FCCu, 0x800110D8u, A0, A1, A2, A3);
    if (k_frameCb) JAL(0x80010FE4u, k_frameCb, A0, A1, A2, A3);
}

/* 80011558 / 80011564 (fn): set the per-frame callbacks. */
void n_kernel_SetFrameCallback(void) { k_frameCb = A0; }
void n_kernel_SetFrameCallback2(void) { k_frameCb2 = A0; }

/* 80011570: a random number: BIOS rand() times the frame count (0x80 in
 * the attract demo). */
void n_kernel_Random(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    if (k_attract == 3) { V0 = 0x80; return; }
    u32 r = JAL(0x80011590u, K_RAND, A0, A1, A2, A3);
    s64 p = (s64)(s32)r * (s32)k_frames;
    LO = (u32)p;
    HI = (u32)((uint64_t)p >> 32);
    V0 = LO;
}

/* 800116A4 (unused, CD position, sectors, dest): read sectors whole:
 * wait for room in the CD queue, queue the read, wait again, queue a
 * pause. Returns the size read (sectors * 2048). */
void n_kernel_ReadSectors(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x14) = in_s1;
    GS(u32, sp + 0x18) = in_s2;
    GS(u32, sp + 0x10) = in_s0;
    GS(u32, sp + 0x1C) = R[31];
    u32 pos = A1, sectors = A2, dest = A3;
    R[17] = pos; R[18] = sectors; R[16] = dest;
    while (JAL(0x800116CCu, 0x80014B14u, 0x40, A1, A2, A3)) {
        JAL(0x800116DCu, K_VSYNC, 0, A1, A2, A3);
        LOOP_POLL(0x800116DCu);
    }
    JAL(0x800116F0u, 0x80015D44u, dest, sectors, pos, A3);
    while (JAL(0x800116FCu, 0x80014B14u, 0x40, A1, A2, A3)) {
        JAL(0x80011708u, K_VSYNC, 0, A1, A2, A3);
        LOOP_POLL(0x80011708u);
    }
    JAL(0x80011718u, 0x80015AE8u, 0, A1, A2, A3);
    V0 = sectors << 11;
    RESTORE_S;
}

/* 80011734: the main loop's mode switch: when a mode is wanted, stop the
 * sound, close the threads, clear VRAM, drop the frame callbacks, set the
 * thread slots up again and start the mode's thread in slot 0 (modes
 * 1-5, entry points 0x80010E30 + 0x30 * (mode - 1)) and the idle body in
 * slots 1 and 2. */
void n_kernel_ModeSwitch(void) {
    FRAME(0x20);
    SAVE_S;
    GS(u32, sp + 0x1C) = R[31];
    GS(u32, sp + 0x18) = in_s0;
    for (int i = 0; i < 8; i++) GS(u8, sp + 0x10 + i) = GV(u8, 0x80062D40 + i);
    R[16] = sp + 0x10;
    if (k_mode == 0) { V0 = 0; RESTORE_S; return; }
    JAL(0x80011784u, 0x80011D48u, 1, A1, A2, A3);
    JAL(0x8001178Cu, K_THREAD_CLOSE, 0, A1, A2, A3);
    JAL(0x80011794u, K_THREAD_CLOSE, 1, A1, A2, A3);
    JAL(0x8001179Cu, K_THREAD_CLOSE, 2, A1, A2, A3);
    GV(u32, 0x80062DC4) = 0;
    JAL(0x800117ACu, K_DRAW_SYNC, 0, A1, A2, A3);
    JAL(0x800117B4u, K_VSYNC, 0, A1, A2, A3);
    JAL(0x800117C8u, K_CLEAR_IMAGE, sp + 0x10, 0, 0, 0);
    k_frameCb = 0;
    k_frameCb2 = 0;
    JAL(0x800117D8u, K_THREADS_INIT, A0, A1, A2, A3);
    u32 m = (u32)k_mode - 1;
    if (m < 5) {
        LOOP_POLL(0x80011808u);
        JAL(0x80011864u, K_THREAD_SET, 0, 0x80010E30u + m * 0x30, A2, A3);
    }
    R[16] = K_IDLE_BODY;
    JAL(0x80011878u, K_THREAD_SET, 1, K_IDLE_BODY, A2, A3);
    JAL(0x80011884u, K_THREAD_SET, 2, K_IDLE_BODY, A2, A3);
    k_mode = 0;
    RESTORE_S;
}
