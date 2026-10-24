/* Hardware register map, interrupt controller, DMA, and the frame clock.
 *
 * DMA transfers complete instantly when started; the game only sees them
 * finish (and gets its interrupt) at the next poll, which is close enough
 * to the real hardware's timing for the libraries. */
#include "rt.h"
#include "savestate.h"

#include <stdio.h>
#include <stdlib.h>

uint32_t g_istat, g_imask;
static uint32_t memctrl[16];

void irq_raise(int n) { g_istat |= 1u << n; }
int irq_pending(void) { return (g_istat & g_imask & 0x7FF) != 0; }

/* ------------------------------------------------------------ DMA */

typedef struct { uint32_t madr, bcr, chcr; } DmaChan;
static DmaChan dma[7];
static uint32_t dpcr = 0x07654321, dicr;

static void dma_update_master(void) {
    int flags = (dicr >> 24) & 0x7F, en = (dicr >> 16) & 0x7F;
    int master = (dicr & 0x8000) || ((dicr & 0x800000) && (flags & en));
    uint32_t old = dicr & 0x80000000;
    dicr = (dicr & 0x7FFFFFFF) | (master ? 0x80000000 : 0);
    if (!old && master)
        irq_raise(IRQ_DMA);
}

static void dma_done(int ch) {
    dma[ch].chcr &= ~0x01000000u;
    if (dicr & (1u << (16 + ch)))
        dicr |= 1u << (24 + ch);
    dma_update_master();
}

static void dma_gpu(DmaChan *c) {
    uint32_t mode = (c->chcr >> 9) & 3, to_dev = c->chcr & 1;
    uint32_t a = c->madr & 0x1FFFFC;
    if (mode == 2) {                         /* linked list (ordering table) */
        int guard = 0;
        for (;;) {
            uint32_t hdr = MEM_R32(a);
            uint32_t n = hdr >> 24;
            for (uint32_t i = 0; i < n; i++) {
                g_gp0_src = 0x80000000u | (a + 4 + 4 * i);
                gpu_write_gp0(MEM_R32(a + 4 + 4 * i));
            }
            g_gp0_src = 0;
            if (hdr & 0x800000)
                break;
            a = hdr & 0x1FFFFC;
            if (++guard > 0x100000) { rt_log("GPU DMA: list loop"); break; }
        }
        c->madr = 0x00FFFFFF;
        return;
    }
    uint32_t words = (c->bcr & 0xFFFF) * (mode == 1 ? (c->bcr >> 16) : 1);
    if (mode == 0 && words == 0) words = 0x10000;
    for (uint32_t i = 0; i < words; i++, a += 4) {
        if (to_dev)
            gpu_write_gp0(MEM_R32(a));
        else
            MEM_W32(a, gpu_read());
    }
    c->madr = a;
}

static void dma_otc(DmaChan *c) {
    uint32_t a = c->madr & 0x1FFFFC, n = c->bcr & 0xFFFF;
    if (!n) n = 0x10000;
    for (uint32_t i = 0; i < n; i++, a -= 4)
        MEM_W32(a, i == n - 1 ? 0x00FFFFFF : ((a - 4) & 0x1FFFFC));
}

static int mdec_pending;

static void dma_start(int ch) {
    DmaChan *c = &dma[ch];
    uint32_t mode = (c->chcr >> 9) & 3;
    uint32_t words = (c->bcr & 0xFFFF) * (mode == 1 ? (c->bcr >> 16) : 1);
    uint32_t a = c->madr & 0x1FFFFC;
    if (mode != 2 && words == 0) words = 0x10000;
    switch (ch) {
    case 0:                                  /* MDEC in */
        for (uint32_t i = 0; i < words; i++, a += 4) mdec_dma_in(MEM_R32(a));
        break;
    case 1:                                  /* MDEC out: waits for decoded data */
        if ((uint32_t)mdec_out_ready() < words) {
            mdec_pending = 1;
            return;                          /* still busy; dma_mdec_kick finishes it */
        }
        for (uint32_t i = 0; i < words; i++, a += 4) MEM_W32(a, mdec_dma_out());
        break;
    case 2: dma_gpu(c); break;
    case 3:                                  /* CD-ROM data */
        if (getenv("RT_TRACE_DMA")) rt_log("DMA3 %u words to %08X (chcr %08X bcr %08X)", words, a, c->chcr, c->bcr);
        for (uint32_t i = 0; i < words; i++, a += 4) MEM_W32(a, cd_dma_read());
        break;
    case 4:                                  /* SPU */
        if (getenv("RT_TRACE_DMA")) rt_log("DMA4 %u words %s %08X (chcr %08X) dicr %08X", words, (c->chcr & 1) ? "from" : "to", a, c->chcr, dicr);
        for (uint32_t i = 0; i < words; i++, a += 4) {
            if (c->chcr & 1) spu_dma_write(MEM_R32(a));
            else MEM_W32(a, spu_dma_read());
        }
        break;
    case 6: dma_otc(c); break;
    default: rt_log("DMA%d not handled", ch); break;
    }
    dma_done(ch);
}

/* The MDEC produced output: finish a DMA1 transfer that was waiting for it. */
void dma_mdec_kick(void) {
    if (!mdec_pending || !(dma[1].chcr & 0x01000000)) { mdec_pending = 0; return; }
    mdec_pending = 0;
    dma_start(1);
}

void dma_write(uint32_t a, uint32_t v) {
    uint32_t off = a - 0x1F801080;
    if (a == 0x1F8010F0) { dpcr = v; return; }
    if (a == 0x1F8010F4) {
        if (getenv("RT_TRACE_DMA")) rt_log("DICR <- %08X", v);
        dicr = (dicr & 0xFF000000 & ~(v & 0x7F000000)) | (v & 0x00FF803F);
        dma_update_master();
        return;
    }
    int ch = off >> 4;
    if (ch > 6) return;
    switch (off & 0xF) {
    case 0: dma[ch].madr = v & 0xFFFFFF; break;
    case 4: dma[ch].bcr = v; break;
    case 8:
        dma[ch].chcr = v;
        if ((v & 0x01000000) && (dpcr & (8u << (ch * 4))))
            dma_start(ch);
        break;
    }
}

uint32_t dma_read(uint32_t a) {
    if (a == 0x1F8010F0) return dpcr;
    if (a == 0x1F8010F4) return dicr;
    uint32_t off = a - 0x1F801080;
    int ch = off >> 4;
    if (ch > 6) return 0;
    switch (off & 0xF) {
    case 0: return dma[ch].madr;
    case 4: return dma[ch].bcr;
    case 8: return dma[ch].chcr;
    }
    return 0;
}

/* ------------------------------------------------------------ registers */

uint32_t hw_read(uint32_t p, int size) {
    if (p >= 0x1F801000 && p < 0x1F801040) return memctrl[(p >> 2) & 15];
    if (p >= 0x1F801040 && p < 0x1F801060) return sio_read(p, size);
    if (p == 0x1F801060) return 0x00000B88;
    if (p == 0x1F801070) return g_istat;
    if (p == 0x1F801074) return g_imask;
    if (p >= 0x1F801080 && p < 0x1F801100) return dma_read(p & ~3u) >> (8 * (p & 3));
    if (p >= 0x1F801100 && p < 0x1F801130) return timers_read(p);
    if (p >= 0x1F801800 && p < 0x1F801804) return cd_read_reg(p & 3);
    if (p == 0x1F801810) return gpu_read();
    if (p == 0x1F801814) return gpu_status();
    if (p == 0x1F801820 || p == 0x1F801824) return mdec_read(p);
    if (p >= 0x1F801C00 && p < 0x1F802000) {
        if (size == 4) return spu_read(p) | ((uint32_t)spu_read(p + 2) << 16);
        return spu_read(p & ~1u);
    }
    return 0;
}

void hw_write(uint32_t p, uint32_t v, int size) {
    if (p >= 0x1F801000 && p < 0x1F801040) { memctrl[(p >> 2) & 15] = v; return; }
    if (p >= 0x1F801040 && p < 0x1F801060) { sio_write(p, v, size); return; }
    if (p == 0x1F801070) { g_istat &= v; return; }
    if (p == 0x1F801074) { g_imask = v & 0x7FF; return; }
    if (p >= 0x1F801080 && p < 0x1F801100) {
        /* 8/16-bit writes (libetc sets DICR's upper half with sh): merge
         * into the 32-bit register. */
        if (size < 4) {
            uint32_t a = p & ~3u, sh = 8 * (p & 3), m = (size == 2 ? 0xFFFFu : 0xFFu) << sh;
            uint32_t cur = dma_read(a);
            if (a == 0x1F8010F4) cur &= 0x00FFFFFF;   /* don't re-acknowledge flags */
            v = (cur & ~m) | ((v << sh) & m);
            p = a;
        }
        dma_write(p & ~3u, v);
        return;
    }
    if (p >= 0x1F801100 && p < 0x1F801130) { timers_write(p, v); return; }
    if (p >= 0x1F801800 && p < 0x1F801804) { cd_write_reg(p & 3, (uint8_t)v); return; }
    if (p == 0x1F801810) { gpu_write_gp0(v); return; }
    if (p == 0x1F801814) { gpu_write_gp1(v); return; }
    if (p == 0x1F801820 || p == 0x1F801824) { mdec_write(p, v); return; }
    if (p >= 0x1F801C00 && p < 0x1F802000) {
        spu_write(p & ~1u, (uint16_t)v);
        if (size == 4) spu_write(p + 2, (uint16_t)(v >> 16));
        return;
    }
}

/* ------------------------------------------------------------ clock */

/* NTSC: 263 lines of 2152 cycles (about 59.8 frames a second). */
#define LINE_CYCLES 2152
#define LINES 263
#define VBLANK_LINE 240

static uint32_t line_acc;
static int line;

void hw_advance(uint32_t cycles) {
    g_cycles += cycles;
    timers_advance(cycles);
    cd_advance(cycles);
    spu_advance(cycles);
    sio_advance(cycles);
    line_acc += cycles;
    while (line_acc >= LINE_CYCLES) {
        line_acc -= LINE_CYCLES;
        timers_hblank(1);
        if (++line == LINES)
            line = 0;
        if (line == VBLANK_LINE) {
            g_frame++;
            irq_raise(IRQ_VBLANK);
            gpu_vblank();
            bios_vblank();
            display_frame_update();
            gte_precise_frame();
            { void gte_precise_stats(void); if (g_frame % 10 == 0) gte_precise_stats(); }
            plat_frame();
        }
    }
}

int hw_in_vblank(void) { return line >= VBLANK_LINE; }

void hw_init(void) {
    gpu_init();
    cd_init();
}

void hw_state(StateIO *io) {
    ss_module(io, "hw");
    SS(io, g_istat);
    SS(io, g_imask);
    SS(io, memctrl);
    SS(io, dma);
    SS(io, dpcr);
    SS(io, dicr);
    SS(io, mdec_pending);
    SS(io, line_acc);
    SS(io, line);
}
