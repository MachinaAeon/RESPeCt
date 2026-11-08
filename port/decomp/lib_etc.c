/* libetc (kernel, 0x800255F8-): interrupt helpers.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "poll.h"

void n_libetc_SetIntrMask(void);
void n_libetc_IntrHandler(void);
void n_libetc_CheckCallback(void);
void n_libetc_GetVideoMode(void);
void n_libetc_VSync(void);
void n_libetc_VSyncWait(void);
void n_libetc_VBlank(void);
void n_libetc_DmaIntr(void);
void n_libetc_InterruptCallback(void);

/* 80025984 SetIntrMask (mask): set the interrupt mask register (pointer
 * at 0x8005B2C4); returns the old mask. */
void n_libetc_SetIntrMask(void) {
    u32 r = GV(u32, 0x8005B2C4u);
    V0 = MEM_R16(r);
    MEM_W16(r, A0);
}

/* 80025A74: libetc's interrupt handler (run through HookEntryInt): while
 * interrupts that are both pending (0x1F801070, pointer at 0x8005B2C0)
 * and enabled (0x8005B2C4, and libetc's own mask at 0x8005A264) remain,
 * acknowledge each and call its callback (0x8005A238 + 4n, n 0-10). If
 * an enabled interrupt is still pending after that, count it
 * (0x8005B2CC); past 2048 times in a row, give up on it (a message and
 * everything acknowledged). 0x8005A234 says libetc is set up, +2 / +2
 * reset: in the handler. Ends with ReturnFromException. */
#define ISTAT_P GV(u32, 0x8005B2C0u)
#define IMASK_P GV(u32, 0x8005B2C4u)
void n_libetc_IntrHandler(void) {
    FRAME(0x28);
    u32 s0 = R[16], s1 = R[17], s2 = R[18], s3 = R[19], s4 = R[20];
    MEM_W32(sp + 0x10, s0); MEM_W32(sp + 0x14, s1); MEM_W32(sp + 0x18, s2);
    MEM_W32(sp + 0x1C, s3); MEM_W32(sp + 0x20, s4); MEM_W32(sp + 0x24, R[31]);
    u32 st = 0x8005A234u;
    R[17] = st;
    if (MEM_R16(st) == 0) {
        gcall(0x800398C8u, 0x80010334u, MEM_R16(ISTAT_P), R[6], R[7]);
        gcall(0x800399D8u, R[4], R[5], R[6], R[7]);
    }
    u32 own = MEM_R16(st + 0x30);
    MEM_W16(st + 2, 1);
    u32 is = MEM_R16(ISTAT_P);
    u32 pend = MEM_R16(IMASK_P) & (own & is);
    if (pend != 0) {
        for (;;) {
            if (pend != 0) {
                u32 cb = st + 4;
                for (s32 i = 0; i < 11;) {
                    if (pend & 1) {
                        MEM_W16(ISTAT_P, ~(1u << (i & 31)));
                        u32 fn = MEM_R32(cb);
                        if (fn) {
                            /* the original's s0-s4 as callbacks see them
                             * (some keep a saved one in memory) */
                            R[16] = pend; R[17] = (u32)i; R[18] = cb; R[19] = 1; R[20] = st + 4;
                            gcall(fn, R[4], R[5], R[6], R[7]);
                        }
                    }
                    cb += 4;
                    pend >>= 1;
                    i++;
                    if ((pend & 0xFFFF) == 0) break;
                    LOOP_POLL(0x80025B5Cu);
                }
            }
            own = MEM_R16(0x8005A264u);
            is = MEM_R16(ISTAT_P);
            pend = MEM_R16(IMASK_P) & (own & is);
            if (pend == 0) break;
            LOOP_POLL(0x80025B8Cu);
        }
    }
    u32 a = MEM_R16(ISTAT_P), b = MEM_R16(IMASK_P);
    if (a & b) {
        u32 c = GV(u32, 0x8005B2CCu);
        GV(u32, 0x8005B2CCu) = c + 1;
        if (!((s32)c < 2049)) {
            gcall(0x800398C8u, 0x80010350u, MEM_R16(ISTAT_P), MEM_R16(IMASK_P), R[7]);
            GV(u32, 0x8005B2CCu) = 0;
            MEM_W16(ISTAT_P, 0);
        }
    } else {
        GV(u32, 0x8005B2CCu) = 0;
    }
    GV(u16, 0x8005A236u) = 0;
    gcall(0x800399D8u, R[4], R[5], R[6], R[7]);
    R[16] = s0; R[17] = s1; R[18] = s2; R[19] = s3; R[20] = s4;
}

/* 8002595C CheckCallback: 1 while the interrupt handler runs callbacks. */
void n_libetc_CheckCallback(void) {
    V0 = GV(u16, 0x8005A236u);
}

/* 8002629C GetVideoMode: 0 NTSC, 1 PAL. */
void n_libetc_GetVideoMode(void) {
    V0 = GV(u32, 0x8005B33Cu);
}

/* ---- VSync ----
 *   0x8005A1C4  -> GPU status (0x1F801814)
 *   0x8005A1C8  -> root counter 1 (horizontal blanks)
 *   0x8005A1CC  the counter at the last VSync
 *   0x8005A1D0  the vertical blank count at the last VSync
 *   0x8005B2FC  vertical blanks so far (the interrupt counts them) */
#define VS_GPUSTAT   GV(u32, 0x8005A1C4u)
#define VS_HCOUNT    GV(u32, 0x8005A1C8u)
#define VS_LAST_H    GV(u32, 0x8005A1CCu)
#define VS_LAST_V    GV(u32, 0x8005A1D0u)
#define VS_COUNT     GV(u32, 0x8005B2FCu)

/* 800255F8 VSync(mode): mode < 0 returns the vertical blank count; 1
 * the horizontal blanks since the last VSync; 0 waits for the next
 * vertical blank, n > 1 for n blanks after the last VSync (80025770
 * VSyncWait), then on an interlaced screen for the field to change; it
 * returns the horizontal blanks counted before the wait.
 *
 * The game's main loop waits for the frame here, so this is where save
 * states are made: VSyncWait is called as a plain C call (not gcall, so
 * no native is counted below it) with the original's frame, return
 * addresses and s0 (GPU status) / s1 (the result) in their registers,
 * and its wait polls where a save can be made; the save continues in
 * the translations of both. */
void n_libetc_VSyncWait(void);
void n_libetc_VSync(void) {
    FRAME(0x28);
    const u32 in_s0 = R[16], in_s1 = R[17], in_ra = R[31];
    MEM_W32(sp + 0x18, R[16]);
    MEM_W32(sp + 0x1C, R[17]);
    MEM_W32(sp + 0x20, R[31]);
    s32 mode = (s32)A0;
    u32 st = MEM_R32(VS_GPUSTAT);
    for (;;) {
        MEM_W32(sp + 0x10, MEM_R32(VS_HCOUNT));
        if (MEM_R32(sp + 0x10) == MEM_R32(VS_HCOUNT)) break;
        LOOP_POLL(0x80025634u);
    }
    u32 h = (MEM_R32(sp + 0x10) - VS_LAST_H) & 0xFFFF;
    if (mode < 0) { V0 = VS_COUNT; return; }
    if (mode == 1) { V0 = h; return; }
    u32 target, timeout;
    if (mode > 0) { target = VS_LAST_V - 1 + (u32)mode; timeout = (u32)mode - 1; }
    else { target = VS_LAST_V; timeout = 0; }
    R[16] = st; R[17] = h;
    R[4] = target; R[5] = timeout; R[31] = 0x800256B0u;
    n_libetc_VSyncWait();
    st = MEM_R32(VS_GPUSTAT);
    R[16] = st;
    R[4] = VS_COUNT + 1; R[5] = 1; R[31] = 0x800256D4u;
    n_libetc_VSyncWait();
    R[16] = in_s0; R[17] = in_s1; R[31] = in_ra;
    if (st & 0x400000) {
        u32 p = VS_GPUSTAT;
        if ((s32)(st ^ MEM_R32(p)) >= 0) {
            for (;;) {
                if ((st ^ MEM_R32(p)) & 0x80000000u) break;
                LOOP_POLL(0x80025714u);
            }
        }
    }
    VS_LAST_V = VS_COUNT;
    u32 hc = VS_HCOUNT;
    for (;;) {
        VS_LAST_H = MEM_R32(hc);
        if (VS_LAST_H == MEM_R32(hc)) break;
        LOOP_POLL(0x80025750u);
    }
    V0 = h;
}

/* 80025770 (count, frames): wait for the vertical blank count to reach
 * count; after about frames * 32768 polls give up: print "VSync:
 * timeout", ChangeClearPAD(0), ChangeClearRCnt(3, 0). The wait polls
 * where a save state can be made (v1 = -1, a0 = count, as the
 * translation has them there). */
void n_libetc_VSyncWait(void) {
    FRAME(0x20);
    s32 until = (s32)A0;
    MEM_W32(sp + 0x10, A1 << 15);
    MEM_W32(sp + 0x18, R[31]);
    V0 = 0;
    if (!((s32)VS_COUNT < until)) return;
    for (;;) {
        MEM_W32(sp + 0x10, MEM_R32(sp + 0x10) - 1);
        if (MEM_R32(sp + 0x10) == 0xFFFFFFFFu) {
            gcall(0x80039800u, 0x800102F0u, R[5], R[6], R[7]);
            gcall(0x80039AA8u, 0, R[5], R[6], R[7]);
            gcall(0x80039AD8u, 3, 0, R[6], R[7]);
            return;
        }
        if (!((s32)VS_COUNT < until)) { V0 = 0; return; }
        R[2] = 1; R[3] = 0xFFFFFFFFu; R[4] = (u32)until;
        SAVE_POLL(0x800257F0u);
    }
}

/* 80025F20: the vertical blank interrupt: count it, then run the eight
 * VSync callbacks (0x8005B2DC, those set). The callbacks see the
 * original's s0 (the slot) and s1 (its number). */
void n_libetc_VBlank(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
    VS_COUNT = VS_COUNT + 1;
    u32 slot = 0x8005B2DCu;
    for (u32 i = 0;;) {
        R[16] = slot; R[17] = i;
        u32 f = GV(u32, slot);
        if (f) gcall(f, R[4], R[5], R[6], R[7]);
        i++;
        slot += 4;
        if (!((s32)i < 8)) break;
        R[16] = slot; R[17] = i;
        LOOP_POLL(0x80025F70u);
    }
    V0 = 0;
    R[16] = s0; R[17] = s1;
}

/* 80026034: the DMA interrupt: while DICR (*0x8005B30C) has channel
 * flags set (bits 24-30), acknowledge each (bit, keeping the low 24)
 * and call its callback (0x8005B310 + n * 4). A bus error or a flagged
 * master bit then prints DICR and the seven channels' records
 * (0x8005B330, 16 bytes each). s-registers as the original's. */
void n_libetc_DmaIntr(void) {
    FRAME(0x30);
    u32 sv[6];
    for (int i = 0; i < 6; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x10 + (u32)i * 4, sv[i]); }
    MEM_W32(sp + 0x28, R[31]);
#define DICR GV(u32, 0x8005B30Cu)
    R[17] = (MEM_R32(DICR) >> 24) & 0x7F;
    if (R[17] != 0) {
        R[20] = 1; R[19] = 0xFFFFFF; R[21] = 0x8005B310u;
        for (;;) {
            R[16] = 0;
            if (R[17] != 0) {
                R[18] = R[21];
                for (;;) {
                    if (!((s32)R[16] < 7)) break;
                    if (R[17] & 1) {
                        u32 d = DICR;
                        MEM_W32(d, MEM_R32(d) & ((R[20] << ((R[16] + 24) & 31)) | R[19]));
                        u32 f = GV(u32, R[18]);
                        if (f) gcall(f, d, R[5], R[6], R[7]);
                    }
                    R[18] += 4;
                    R[17] >>= 1;
                    R[16]++;
                    if (R[17] == 0) break;
                    LOOP_POLL(0x800260E4u);
                }
            }
            R[17] = (MEM_R32(DICR) >> 24) & 0x7F;
            if (R[17] == 0) break;
            LOOP_POLL(0x80026108u);
        }
    }
    u32 d = DICR;
    V0 = MEM_R32(d) & 0xFF000000u;
    if (V0 == 0x80000000u || (MEM_R32(d) & 0x8000)) {
        gcall(0x800398C8u, 0x80010370u, MEM_R32(d), R[6], R[7]);
        R[16] = 0;
        for (;;) {
            u32 rec = GV(u32, 0x8005B330u) + (R[16] << 4);
            u32 i = R[16];
            R[16]++;
            gcall(0x800398C8u, 0x8001038Cu, i, MEM_R32(rec), R[7]);
            if (!((s32)R[16] < 7)) break;
            LOOP_POLL(0x80026188u);
        }
    }
    V0 = 0;
#undef DICR
    for (int i = 0; i < 6; i++) R[16 + i] = sv[i];
}

/* 80025C44 (irq, fn): InterruptCallback: the handler for an interrupt
 * (table 0x8005A238) set or cleared, its bit in the enabled mask
 * (0x8005A264) and in I_MASK (*0x8005B2C4, zeroed meanwhile) to match;
 * the vertical blank (0) also (un)hooks the root counter 3 handler
 * (80039AA8, 80039AD8 3), and the DMA/timer interrupts 4-6 their root
 * counters 0-2 (80039AD8). Nothing unless the handler changes and the
 * system is up (0x8005A234). Returns the previous handler. */
void n_libetc_InterruptCallback(void) {
    FRAME(0x28);
    u32 sv[5];
    for (int i = 0; i < 5; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x24, R[31]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x10, sv[0]);
    const u32 irq = A0, fn = A1;
    const u32 slot = 0x8005A238u + (irq << 2);
    const u32 old = MEM_R32(slot);
    R[17] = irq; R[18] = fn; R[20] = old;
    if (fn != old && GV(u16, 0x8005A234u) != 0) {
        const u32 im = GV(u32, 0x8005B2C4u);
        u32 mask = MEM_R16(im);
        MEM_W16(im, 0);
        const u32 bit = 1u << (irq & 31);
        if (fn != 0) {
            MEM_W32(slot, fn);
            GV(u16, 0x8005A264u) |= (u16)bit;
            mask |= bit;
        } else {
            MEM_W32(slot, 0);
            GV(u16, 0x8005A264u) &= (u16)~bit;
            mask &= ~bit;
        }
        R[19] = mask;
        const u32 off = fn == 0;
        if (irq == 0) {
            R[16] = off;
            gcall(0x80039AA8u, off, R[5], R[6], R[7]);
            gcall(0x80039AD8u, 3, off, R[6], R[7]);
        }
        if (irq == 4) gcall(0x80039AD8u, 0, off, R[6], R[7]);
        if (irq == 5) gcall(0x80039AD8u, 1, off, R[6], R[7]);
        if (irq == 6) gcall(0x80039AD8u, 2, off, R[6], R[7]);
        MEM_W16(GV(u32, 0x8005B2C4u), (u16)mask);
    }
    V0 = old;
    for (int i = 0; i < 5; i++) R[16 + i] = sv[i];
}

