/* The kernel's main program (KERNEL, 0x80010B00-0x800116A0, 0x80011734):
 * the game-mode threads' bodies, the vertical-blank callback, the main
 * loop's mode switch, the random number, a whole-file read.
 *
 * The main loop (0x8001107C, kernel_MainLoop: it never returns) calls
 * kernel_ModeSwitch, runs the game threads (0x80012E8C) and waits for the
 * vertical blank, where save states are made (see the main thread's
 * natives below). The game mode wanted is 0x80062DFC (1-5, 0: none);
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
#define K_THREAD_WAIT   0x80012FF4u          /* (mode): load and run its overlay */
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
void n_kernel_ModeThread6(void) { mode_thread(0x80010EA0u, 6); }
void n_kernel_ModeThread7(void) { mode_thread(0x80010ED0u, 7); }
void n_kernel_ModeThread8(void) { mode_thread(0x80010F00u, 8); }

/* 80012FF4 (mode): the mode's overlay (the table at 0x80059618, 12
 * bytes a mode: sector, size, entry) read to the overlay area
 * (0x80010040's address), then the thread ends with its entry point. */
void n_kernel_ModeLoad(void) {
    { extern void sg_hook_mode_switch(void); sg_hook_mode_switch(); }   /* hooks.txt */
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[16];
    GS(u32, sp + 0x14) = R[31];
    const u32 in_s0 = R[16];
    const u32 e = 0x80059618u + A0 * 12;
    R[16] = e;
    JAL(0x80013030u, 0x80013208u, GV(u32, e), GV(u32, e + 4), GV(u32, 0x80010040u), A3);
    JAL(0x8001303Cu, 0x80013154u, GV(u32, e + 8), A1, A2, A3);
    R[16] = in_s0;
}

/* 80013208 (sector, size, dest): read sectors through the CD queue and
 * wait for them (a vertical blank at a time), then stop the queue. */
void n_kernel_ReadWait(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    JAL(0x80013220u, 0x80015D44u, A2, A1, A0, A3);
    for (;;) {
        if (JAL(0x80013228u, 0x80014B14u, 0x40, A1, A2, A3) == 0) break;
        JAL(0x80013238u, 0x800255F8u, 0, A1, A2, A3);
        LOOP_POLL(0x80013238u);
    }
    JAL(0x80013248u, 0x80015AE8u, 0, A1, A2, A3);
}

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

/* ---- the main thread ----
 * The main thread runs these for the whole game, and its frame wait
 * (VSync) is where save states are made: each calls on with gcall_ss and
 * the registers as the original has them (gp, and the s registers of
 * 0x80012E8C), so a save continues in their translations. */

/* 80010B08: the program's entry: clear the BSS (0x80062D90-0x8006C118),
 * the stack at the top of RAM (the size at 0x80010BB8), gp, the heap
 * from 0x8006C11C (InitHeap) up to the stack's reserve (0x8005A1B4),
 * then the main loop (it doesn't return). */
void n_kernel_Entry(void) {
    for (u32 p = 0x80062D90u;;) {
        GV(u32, p) = 0;
        p += 4;
        if (!(p < 0x8006C118u)) break;
        LOOP_POLL(0x80010B24u);
    }
    const u32 top = GV(u32, 0x80010BB8u);
    R[8] = 0x80000000u;
    R[29] = top | 0x80000000u;
    const u32 bss_end = 0x0006C118u;
    R[3] = GV(u32, 0x8005A1B4u);
    R[5] = top - R[3] - bss_end;
    R[4] = bss_end | 0x80000000u;
    GV(u32, 0x80062D90u) = R[31];
    R[28] = 0x80062D1Cu;
    R[30] = R[29];
    gcall_ss(0x80010B9Cu, 0x800398D8u, R[4] + 4, R[5], A2, A3);
    R[31] = GV(u32, 0x80062D90u);
    gcall_ss(0x80010BB0u, 0x8001107Cu, A0, A1, A2, A3);
    rt_break(0x80010BB0u, 1);
}

/* 80010BC4: start-up: interrupts, the threads, the GTE, the pads, the
 * display (reset, cleared), the settings' defaults (volumes 100, offsets
 * 0), the scratchpad cleared, the graphics system (320x240),
 * the CD check (anti-modchip, 0x800170B8 / 0x80017124), the CD queue
 * (double speed, volume, filter), the sound, the vertical-blank callback
 * and the three threads' bodies (mode 2's, then the idle body twice). */
void n_kernel_Start(void) {
    FRAME(0x20);
    GS(u32, sp + 0x1C) = R[31];
    GS(u32, sp + 0x18) = R[16];
    const u32 in_s0 = R[16];
#define K(ra, f, a, b, c, d) gcall_ss((ra), (f), (u32)(a), (u32)(b), (u32)(c), (u32)(d))
    K(0x80010BD4u, 0x80025808u, A0, A1, A2, A3);
    K(0x80010BDCu, 0x80012DDCu, A0, A1, A2, A3);
    K(0x80010BE4u, 0x80039928u, 2, A1, A2, A3);
    K(0x80010BECu, 0x8002A350u, A0, A1, A2, A3);
    K(0x80010C08u, 0x80013260u, 0x800633F8u, 0x80063420u, R[28] + 0x128, R[28] + 0x130);
    K(0x80010C10u, 0x80013698u, 0, A1, A2, A3);
    K(0x80010C18u, 0x80013698u, 0x10, A1, A2, A3);
    K(0x80010C20u, 0x8003D4CCu, A0, A1, A2, A3);
    GV(u16, 0x80062DBCu) = 0;          /* g_fireCooldown */
    GV(u8, 0x80062E40u) = 0;           /* g_fireRequest */
    GV(u8, 0x80062DECu) = 0;
    GV(u8, 0x80062E2Cu) = 0;
    GV(u32, 0x80062DE0u) = 0;          /* g_reticleY */
    GV(u32, 0x80062DDCu) = 0;          /* g_reticleX */
    GV(u32, 0x80062DA0u) = 0;
    GV(u32, 0x80062DD8u) = 0;
    GV(u32, 0x80062DC4u) = 0;
    GV(u8, 0x80062DFCu) = 0;
    K(0x80010C50u, 0x800398B8u, 0x214, A1, A2, A3);
    K(0x80010C58u, 0x80034FF8u, 0, A1, A2, A3);
    K(0x80010C60u, 0x800352DCu, 0, A1, A2, A3);
    GV(u16, 0x80062E04u) = 0x400;
    GV(u16, 0x80062E00u) = 0;
    GV(u16, 0x80062E02u) = 0;
    GV(u16, 0x80062E06u) = 0x200;
    K(0x80010C8Cu, 0x800354F8u, 0x80062E00u, 0, 0, 0);
    K(0x80010C94u, 0x80035374u, 0, A1, A2, A3);
    GV(u16, 0x80062DB6u) = 0x64;
    GV(u16, 0x80062DB4u) = 0x64;
    GV(u16, 0x80062E1Au) = 0;
    GV(u16, 0x80062E18u) = 0;
    GV(u16, 0x80062DD6u) = 0;
    GV(u16, 0x80062DD4u) = 0;
    GV(u16, 0x80062DAEu) = 0;
    GV(u16, 0x80062DACu) = 0;
    GV(u32, 0x80062E20u) = 0;
    GV(u32, 0x80062DA4u) = 1;
    K(0x80010CCCu, 0x8001367Cu, 0, A1, A2, A3);
    for (s32 i = 0xFF;;) {
        MEM_W32(0x1F800000u + (0xFFu - (u32)i) * 4, 0);   /* the scratchpad (GV is main RAM only) */
        i--;
        if (i < 0) break;
        LOOP_POLL(0x80010CDCu);
    }
    GS(u32, sp + 0x10) = 0;
    K(0x80010CFCu, 0x8002C638u, 0x140, 0xF0, 4, 0);
    K(0x80010D10u, 0x8002D6F8u, 0, 0, 0, 0xF0);
    K(0x80010D1Cu, 0x8002CE58u, 0, 0, A2, A3);
    K(0x80010D24u, 0x80039AA8u, 0, A1, A2, A3);
    R[16] = 0x80010000u;
    K(0x80010D2Cu, 0x800170B8u, A0, A1, A2, A3);
    K(0x80010D34u, 0x80017124u, 0, A1, A2, A3);
    K(0x80010D44u, 0x80014958u, 0x40, 0x80062FF8u, A2, A3);
    K(0x80010D4Cu, 0x80014FF4u, 1, A1, A2, A3);
    K(0x80010D60u, 0x80015484u, 0xFF, 0, 0xFF, 0);
    K(0x80010D6Cu, 0x80015840u, 1, 0, A2, A3);
    GV(u8, 0x80062E1Cu) = 0;
    GV(u32, 0x80062E28u) = 0;
    GV(u32, 0x80062E14u) = 0;
    K(0x80010D80u, 0x80011994u, 0, A1, A2, A3);
    K(0x80010D90u, 0x8001D898u, 0, 0x7F, 0x7F, A3);
    K(0x80010D9Cu, 0x80025898u, 0x80010F20u, A1, A2, A3);
    K(0x80010DACu, 0x8001304Cu, 0, 0x80010E00u, A2, A3);
    R[16] = 0x80010DD8u;
    K(0x80010DBCu, 0x8001304Cu, 1, 0x80010DD8u, A2, A3);
    K(0x80010DC8u, 0x8001304Cu, 2, 0x80010DD8u, A2, A3);
#undef K
    R[16] = in_s0;
}

/* 8001107C: the main loop: start-up, then for ever: the mode switch,
 * the game threads (0x80012E8C), the drawing finished, the vertical
 * blank (VSync(0x80062E1C)), the frame count. */
void n_kernel_MainLoop(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    gcall_ss(0x8001108Cu, 0x80010B00u, A0, A1, A2, A3);
    gcall_ss(0x80011094u, 0x80010BC4u, A0, A1, A2, A3);
    GV(u32, R[28] + 0xF4) = 0;
    GV(u8, R[28] + 0xC9) = 0;
    for (;;) {
        gcall_ss(0x800110A4u, 0x80011734u, A0, A1, A2, A3);
        gcall_ss(0x800110ACu, 0x80012E8Cu, A0, A1, A2, A3);
        gcall_ss(0x800110B4u, 0x80035374u, 0, A1, A2, A3);
        gcall_ss(0x800110C0u, 0x800255F8u, GV(u8, R[28] + 0x100), A1, A2, A3);
        GV(u32, R[28] + 0xF4) += 1;
        SAVE_POLL(0x800110D0u);
    }
}

/* 80012E8C: the game threads' turn: for each of the three slots (0x80
 * bytes from 0x80063ADC; the one running at 0x80063C5C): state 2 opens
 * its thread (in a critical section) and runs it, 4 runs it, 1 counts a
 * delay down and runs it when it ends (each then state 0x7F); the
 * thread runs until it switches back (ChangeThread, gcall_ss with the
 * s registers as the original has them, since the main thread waits
 * there while a game thread can make a save state). */
void n_thread_Run(void) {
    FRAME(0x28);
    u32 in_s[5];
    for (int i = 0; i < 5; i++) { in_s[i] = R[16 + i]; GS(u32, sp + 0x10 + 4 * i) = R[16 + i]; }
    GS(u32, sp + 0x24) = R[31];
    R[17] = 0x80063ADCu;
    R[18] = 0x7F;
    R[19] = 0x80063AD8u;
    R[20] = 0x80063C5Cu;
    R[16] = 0x80063AE4u;
    for (;;) {
        GV(u32, 0x80063C5Cu) = R[17];
        const u32 st = GV(u16, R[17]);
        int run = 0;
        if (st == 2) {
            gcall_ra(0x80012F08u, 0x80039A08u, A0, A1, A2, A3);
            const u32 h = gcall_ra(0x80012F1Cu, 0x800399A8u, GV(u32, R[16] - 4), GV(u32, R[16] + 8), GV(u32, R[16] + 0x3C), A3);
            GV(u32, R[16]) = h;
            gcall_ra(0x80012F24u, 0x80039A18u, A0, A1, A2, A3);
            GV(u16, R[17]) = 0x7F;
            run = 1;
        } else if (st == 1) {
            const u16 c = (u16)(GV(u16, R[16] - 6) - 1);
            GV(u16, R[16] - 6) = c;
            if ((s16)c <= 0) { GV(u16, R[17]) = 0x7F; run = 1; }
        } else if (st == 4) {
            GV(u16, R[17]) = 0x7F;
            run = 1;
        }
        if (run) gcall_ss(0x80012F58u, 0x800399C8u, GV(u32, R[16]), A1, A2, A3);
        R[16] += 0x80;
        R[17] += 0x80;
        if (!(R[17] < R[20])) break;
        LOOP_POLL(0x80012F64u);
    }
    for (int i = 0; i < 5; i++) R[16 + i] = in_s[i];
}
