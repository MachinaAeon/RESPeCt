/* SPU: the PS1 sound chip (psx-spx "Sound Processing Unit").
 *
 * 24 ADPCM voices with pitch, ADSR envelopes, volume, noise and loop
 * flags; hardware reverb; CD audio (XA-ADPCM streamed from the disc) mixed
 * in. Runs on the emulated clock: one stereo sample every 768 CPU cycles
 * (44.1 kHz), written to a ring buffer the platform layer plays. */
#include "rt.h"
#include "savestate.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define CYCLES_PER_SAMPLE 768

static uint16_t regs[0x200];
static uint8_t ram[0x80000];
static uint32_t xfer_addr;
static uint32_t cycle_acc;

#define REG(off) regs[(off) >> 1]          /* off: 0x000-0x3FF from 0x1F801C00 */

/* ------------------------------------------------------------ voices */

enum { ENV_OFF, ENV_ATTACK, ENV_DECAY, ENV_SUSTAIN, ENV_RELEASE };

typedef struct {
    uint32_t addr;                         /* current block (bytes) */
    uint32_t repeat;                       /* loop address (bytes) */
    uint32_t counter;                      /* pitch counter, 12-bit fraction */
    int16_t buf[28];                       /* decoded block */
    int16_t prev[3];                       /* last samples of the previous block (interpolation) */
    int32_t s1, s2;                        /* ADPCM filter history */
    int flags;                             /* current block's loop flags */
    int env_phase;
    int32_t env;                           /* 0..0x7FFF */
    int32_t env_counter;
    int repeat_set;                        /* software wrote the repeat address */
} Voice;

static Voice voices[24];
static uint32_t endx;
static int32_t noise_level = 1, noise_timer;

/* The SPU's 4-point Gaussian interpolation table (psx-spx "SPU
 * Interpolation"), generated with nocash's formula and normalised so each
 * set of four weights sums to 0x7F80 (within 1 of the real table). */
static int16_t gauss[512];
static void gauss_init(void) {
    static int done;
    if (done) return;
    done = 1;
    double t[512], sum = 0;
    for (int i = 0; i < 512; i++) {
        double k = 0.5 + i;
        double sn = sin(M_PI * k * 2.048 / 1024);
        double c1 = (cos(M_PI * k * 2.000 / 1023) - 1) * 0.50;
        double c2 = (cos(M_PI * k * 4.000 / 1023) - 1) * 0.08;
        t[511 - i] = sn * (c1 + c2 + 1.0) / k;
        sum += t[511 - i];
    }
    for (int i = 0; i < 512; i++) gauss[i] = (int16_t)lround(t[i] * (128.0 * 0x7F80) / sum);
    for (int i = 0; i < 128; i++) {                  /* each set of four: exactly 0x7F80 */
        int idx[4] = {i, 255 - i, 256 + i, 511 - i};
        int d = 0x7F80 - (gauss[idx[0]] + gauss[idx[1]] + gauss[idx[2]] + gauss[idx[3]]);
        gauss[511 - i] = (int16_t)(gauss[511 - i] + d);   /* the largest weight takes it */
    }
}

static const int pos_f[5] = {0, 60, 115, 98, 122};
static const int neg_f[5] = {0, 0, -52, -55, -60};

static void decode_block(Voice *v) {
    const uint8_t *b = ram + (v->addr & 0x7FFF0);
    int shift = b[0] & 0x0F, filter = (b[0] >> 4) & 7;
    if (shift > 12) shift = 9;
    if (filter > 4) filter = 4;
    v->flags = b[1];
    v->prev[0] = v->buf[25];
    v->prev[1] = v->buf[26];
    v->prev[2] = v->buf[27];
    for (int i = 0; i < 28; i++) {
        int nib = (b[2 + i / 2] >> ((i & 1) * 4)) & 0xF;
        int32_t s = (int16_t)(nib << 12) >> shift;
        s += (v->s1 * pos_f[filter] + v->s2 * neg_f[filter] + 32) >> 6;
        if (s > 32767) s = 32767;
        if (s < -32768) s = -32768;
        v->s2 = v->s1;
        v->s1 = s;
        v->buf[i] = (int16_t)s;
    }
}

static uint16_t adsr_lo(int n) { return REG(n * 16 + 8); }
static uint16_t adsr_hi(int n) { return REG(n * 16 + 0xA); }

/* One envelope step (psx-spx "ADSR"). */
static void env_step(Voice *v, int rate, int decreasing, int exponential) {
    int shift = rate >> 2, step = rate & 3;
    int32_t cycles = 1 << (shift > 11 ? shift - 11 : 0);
    int32_t delta = (decreasing ? -8 + step : 7 - step) << (shift < 11 ? 11 - shift : 0);
    if (exponential && !decreasing && v->env > 0x6000) cycles *= 4;
    if (exponential && decreasing) delta = delta * v->env >> 15;
    if (++v->env_counter < cycles) return;
    v->env_counter = 0;
    v->env += delta;
    if (v->env < 0) v->env = 0;
    if (v->env > 0x7FFF) v->env = 0x7FFF;
}

static uint8_t fast_release[24];          /* port-side: set by limit_copies */
static void env_tick(Voice *v, int n) {
    uint16_t lo = adsr_lo(n), hi = adsr_hi(n);
    switch (v->env_phase) {
    case ENV_ATTACK:
        env_step(v, (lo >> 8) & 0x7F, 0, lo >> 15);
        if (v->env >= 0x7FFF) { v->env = 0x7FFF; v->env_phase = ENV_DECAY; v->env_counter = 0; }
        break;
    case ENV_DECAY: {
        int32_t sl = ((lo & 0xF) + 1) * 0x800;
        env_step(v, ((lo >> 4) & 0xF) * 4, 1, 1);
        if (v->env <= sl) { v->env_phase = ENV_SUSTAIN; v->env_counter = 0; }
        break;
    }
    case ENV_SUSTAIN:
        env_step(v, (hi >> 6) & 0x7F, (hi >> 14) & 1, hi >> 15);
        break;
    case ENV_RELEASE:
        if (fast_release[n]) v->env -= v->env < 0x80 ? v->env : 0x80;    /* the limiter's cut: ~6 ms */
        else env_step(v, (hi & 0x1F) * 4, 1, (hi >> 5) & 1);
        if (v->env == 0) v->env_phase = ENV_OFF;
        break;
    }
    REG(n * 16 + 0xC) = (uint16_t)v->env;
}

/* Repeated sound effects: the game can start the same one-shot sound on
 * many voices at once (a swarm of bugs, each restarting its sound every
 * half second), and a dozen copies of one sample just out of step with
 * each other turn into a roar. At most g_audio_same_sample voices play one
 * sample at a time; a new start replaces the oldest copy (0: no limit).
 * (Port-side, not saved in save states.) */
int g_audio_same_sample = 0;              /* the platform sets it (Windows: 3) */
static uint32_t start_addr[24], start_seq[24], seq;

static void limit_copies(int n, uint32_t addr) {
    if (g_audio_same_sample <= 0) return;
    int count = 0, oldest = -1;
    for (int i = 0; i < 24; i++) {
        if (i == n || voices[i].env_phase == ENV_OFF || voices[i].env_phase == ENV_RELEASE ||
            start_addr[i] != addr) continue;
        count++;
        if (oldest < 0 || start_seq[i] < start_seq[oldest]) oldest = i;
    }
    /* Copies already being let go (fading out at their own, often slow,
     * release rate) still sound: they count too, and are cut quickly, or a
     * swarm restarting its sound many times a second piles up anyway. */
    if (count >= g_audio_same_sample && oldest >= 0) {
        voices[oldest].env_phase = ENV_RELEASE;
        voices[oldest].env_counter = 0;
        fast_release[oldest] = 1;
    }
    for (int i = 0; i < 24; i++)
        if (i != n && start_addr[i] == addr && voices[i].env_phase == ENV_RELEASE) fast_release[i] = 1;
}

static void key_on(int n) {
    Voice *v = &voices[n];
    static int trace = -1;
    if (trace < 0) trace = getenv("RT_TRACE_SPU") != NULL;
    if (trace)
        rt_log("SPU key on %d: addr %05X pitch %04X vol %04X/%04X adsr %04X %04X cnt %04X main %04X/%04X",
               n, REG(n * 16 + 6) * 8, REG(n * 16 + 4), REG(n * 16), REG(n * 16 + 2), REG(n * 16 + 8),
               REG(n * 16 + 0xA), REG(0x1AA), REG(0x180), REG(0x182));
    v->addr = (uint32_t)REG(n * 16 + 6) * 8;
    limit_copies(n, v->addr);
    fast_release[n] = 0;
    start_addr[n] = v->addr;
    start_seq[n] = ++seq;
    if (trace) {
        uint32_t a = v->addr; int k = 0;
        while (k < 20000 && !(ram[(a + 1) & 0x7FFFF] & 1)) { a += 16; k++; }
        rt_log("SPU   sample %05X: %d blocks, last flags %02X, repeat reg %05X", v->addr, k + 1, ram[(a + 1) & 0x7FFFF], REG(n * 16 + 0xE) * 8);
    }
    if (!v->repeat_set) v->repeat = v->addr;
    v->repeat_set = 0;
    v->counter = 0;
    v->s1 = v->s2 = 0;
    memset(v->buf, 0, sizeof v->buf);
    v->env = 0;
    v->env_counter = 0;
    v->env_phase = ENV_ATTACK;
    endx &= ~(1u << n);
    decode_block(v);
    if (v->flags & 4) v->repeat = v->addr;
}

static void key_off(int n) {
    { static int tr = -1; if (tr < 0) tr = getenv("RT_TRACE_SPU") != NULL; if (tr) rt_log("SPU key off %d", n); }
    if (voices[n].env_phase != ENV_OFF) { voices[n].env_phase = ENV_RELEASE; voices[n].env_counter = 0; }
}

static int irq_check_addr(uint32_t addr, uint32_t len) {
    if (!(REG(0x1AA) & 0x40)) return 0;
    uint32_t ia = (uint32_t)REG(0x1A4) * 8;
    if (ia >= addr && ia < addr + len) {
        REG(0x1AE) |= 0x40;
        irq_raise(IRQ_SPU);
        return 1;
    }
    return 0;
}

/* Next sample of a voice at the current pitch position. */
static int32_t voice_sample(Voice *v, int n, int32_t pitch_mod) {
    int i = (int)(v->counter >> 12);
    int g = (int)(v->counter >> 4) & 0xFF;
    int32_t s4[4];                                   /* oldest, older, old, new */
    for (int k = 0; k < 4; k++) {
        int j = i - 3 + k;
        s4[k] = j >= 0 ? v->buf[j] : v->prev[3 + j];
    }
    int32_t s = (gauss[0xFF - g] * s4[0] >> 15) + (gauss[0x1FF - g] * s4[1] >> 15) +
                (gauss[0x100 + g] * s4[2] >> 15) + (gauss[g] * s4[3] >> 15);
    if ((REG(0x194) | (uint32_t)REG(0x196) << 16) & (1u << n)) s = noise_level;
    uint32_t pitch = REG(n * 16 + 4);
    if (n > 0 && ((REG(0x190) | (uint32_t)REG(0x192) << 16) & (1u << n))) {
        int32_t f = (int16_t)pitch_mod + 0x8000;
        pitch = (uint32_t)((int32_t)pitch * f >> 15) & 0xFFFF;
    }
    if (pitch > 0x3FFF) pitch = 0x4000;
    v->counter += pitch;
    while ((v->counter >> 12) >= 28) {
        v->counter -= 28 << 12;
        /* end of block: loop flags */
        if (v->flags & 1) {
            endx |= 1u << n;
            if (v->flags & 2) {
                v->addr = v->repeat;
            } else {
                static int tr = -1;
                if (tr < 0) tr = getenv("RT_TRACE_SPU") != NULL;
                if (tr && v->env_phase != ENV_OFF) rt_log("SPU voice %d ended", n);
                v->addr = v->repeat;
                v->env_phase = ENV_OFF;
                v->env = 0;
            }
        } else {
            v->addr = (v->addr + 16) & 0x7FFFF;
        }
        irq_check_addr(v->addr, 16);
        decode_block(v);
        if (v->flags & 4 && !v->repeat_set) v->repeat = v->addr;
    }
    return s;
}

/* Volume registers (voices 0x00/0x02 per voice, main 0x180/0x182): a fixed
 * level, or sweep mode (bit 15), where the level moves on its own. Sweeps
 * aren't modelled: they hold the last fixed level. */
static int16_t fixed_vol[0x200];
static int32_t vol_reg(uint32_t off) {
    uint16_t r = REG(off);
    if (r & 0x8000) return fixed_vol[off >> 1];
    return (int16_t)(r << 1);
}

/* ------------------------------------------------------------ reverb */

#define RV(o) ((int32_t)(int16_t)REG(0x1C0 + (o) * 2))
static uint32_t rv_cur;
static int rv_phase;
static int32_t rv_out_l, rv_out_r, rv_prev_l, rv_prev_r, rv_in_l, rv_in_r;

/* Reverb buffer access; off in bytes from the current position. */
static int16_t rv_read(int32_t off) {
    uint32_t base = (uint32_t)REG(0x1A2) * 8;
    uint32_t a = rv_cur + (uint32_t)off;
    uint32_t size = 0x80000 - base;
    if (!size) return 0;
    a = base + ((a - base) % size);
    int16_t v;
    memcpy(&v, ram + (a & 0x7FFFE), 2);
    return v;
}

static void rv_write(int32_t off, int32_t val) {
    if (!(REG(0x1AA) & 0x80)) return;
    uint32_t base = (uint32_t)REG(0x1A2) * 8;
    uint32_t a = rv_cur + (uint32_t)off;
    uint32_t size = 0x80000 - base;
    if (!size) return;
    a = base + ((a - base) % size);
    if (val > 32767) val = 32767;
    if (val < -32768) val = -32768;
    int16_t v = (int16_t)val;
    memcpy(ram + (a & 0x7FFFE), &v, 2);
}

/* psx-spx "SPU Reverb Formula", run at 22.05 kHz. */
static void reverb(int32_t in_l, int32_t in_r) {
    enum { dAPF1, dAPF2, vIIR, vCOMB1, vCOMB2, vCOMB3, vCOMB4, vWALL, vAPF1, vAPF2, mLSAME, mRSAME,
           mLCOMB1, mRCOMB1, mLCOMB2, mRCOMB2, dLSAME, dRSAME, mLDIFF, mRDIFF, mLCOMB3, mRCOMB3,
           mLCOMB4, mRCOMB4, dLDIFF, dRDIFF, mLAPF1, mRAPF1, mLAPF2, mRAPF2, vLIN, vRIN };
#define M(r) ((int32_t)(uint16_t)REG(0x1C0 + (r) * 2) * 8)
    int32_t lin = in_l * RV(vLIN) >> 15, rin = in_r * RV(vRIN) >> 15;
    int32_t iir = RV(vIIR), wall = RV(vWALL);
    int32_t ls = (lin + (rv_read(M(dLSAME)) * wall >> 15) - rv_read(M(mLSAME) - 2)) * iir >> 15;
    ls += rv_read(M(mLSAME) - 2);
    int32_t rs = (rin + (rv_read(M(dRSAME)) * wall >> 15) - rv_read(M(mRSAME) - 2)) * iir >> 15;
    rs += rv_read(M(mRSAME) - 2);
    int32_t ld = (lin + (rv_read(M(dRDIFF)) * wall >> 15) - rv_read(M(mLDIFF) - 2)) * iir >> 15;
    ld += rv_read(M(mLDIFF) - 2);
    int32_t rd = (rin + (rv_read(M(dLDIFF)) * wall >> 15) - rv_read(M(mRDIFF) - 2)) * iir >> 15;
    rd += rv_read(M(mRDIFF) - 2);
    rv_write(M(mLSAME), ls);
    rv_write(M(mRSAME), rs);
    rv_write(M(mLDIFF), ld);
    rv_write(M(mRDIFF), rd);
    int32_t lout = (RV(vCOMB1) * rv_read(M(mLCOMB1)) + RV(vCOMB2) * rv_read(M(mLCOMB2)) +
                    RV(vCOMB3) * rv_read(M(mLCOMB3)) + RV(vCOMB4) * rv_read(M(mLCOMB4))) >> 15;
    int32_t rout = (RV(vCOMB1) * rv_read(M(mRCOMB1)) + RV(vCOMB2) * rv_read(M(mRCOMB2)) +
                    RV(vCOMB3) * rv_read(M(mRCOMB3)) + RV(vCOMB4) * rv_read(M(mRCOMB4))) >> 15;
    lout -= RV(vAPF1) * rv_read(M(mLAPF1) - M(dAPF1)) >> 15;
    rv_write(M(mLAPF1), lout);
    lout = (lout * RV(vAPF1) >> 15) + rv_read(M(mLAPF1) - M(dAPF1));
    rout -= RV(vAPF1) * rv_read(M(mRAPF1) - M(dAPF1)) >> 15;
    rv_write(M(mRAPF1), rout);
    rout = (rout * RV(vAPF1) >> 15) + rv_read(M(mRAPF1) - M(dAPF1));
    lout -= RV(vAPF2) * rv_read(M(mLAPF2) - M(dAPF2)) >> 15;
    rv_write(M(mLAPF2), lout);
    lout = (lout * RV(vAPF2) >> 15) + rv_read(M(mLAPF2) - M(dAPF2));
    rout -= RV(vAPF2) * rv_read(M(mRAPF2) - M(dAPF2)) >> 15;
    rv_write(M(mRAPF2), rout);
    rout = (rout * RV(vAPF2) >> 15) + rv_read(M(mRAPF2) - M(dAPF2));
    rv_out_l = lout * (int16_t)REG(0x184) >> 15;
    rv_out_r = rout * (int16_t)REG(0x186) >> 15;
    uint32_t base = (uint32_t)REG(0x1A2) * 8;
    rv_cur += 2;
    if (rv_cur >= 0x80000 || rv_cur < base) rv_cur = base;
#undef M
}

/* ------------------------------------------------------------ CD audio */

#define CDBUF 16384
static int16_t cd_buf[CDBUF][2];
static uint32_t cd_rd, cd_wr;                 /* in 44.1 kHz frames */

/* CD-ROM audio volumes: left->left, left->right, right->right,
 * right->left (0x80 = 100%), and ADPCM mute. */
static int32_t cd_vol[4] = {0x80, 0, 0x80, 0};
static int cd_mute;
void spu_cd_volume(int ll, int lr, int rr, int rl, int mute) {
    cd_vol[0] = ll; cd_vol[1] = lr; cd_vol[2] = rr; cd_vol[3] = rl;
    cd_mute = mute;
}

static void cd_push(int16_t l, int16_t r) {
    if (cd_wr - cd_rd >= CDBUF) return;       /* full: drop */
    cd_buf[cd_wr % CDBUF][0] = l;
    cd_buf[cd_wr % CDBUF][1] = r;
    cd_wr++;
}

/* XA-ADPCM (psx-spx "CDROM XA Audio ADPCM Compression"). */
static int32_t xa_hist[2][2];
static uint32_t xa_resample_acc;
static int16_t xa_last[2];

static void xa_out(int16_t l, int16_t r, int rate) {
    /* simple resample to 44.1 kHz: linear between the last two samples */
    uint32_t step = (uint32_t)((uint64_t)rate * 65536 / 44100);
    while (xa_resample_acc < 65536) {
        int32_t f = (int32_t)xa_resample_acc;
        int16_t ol = (int16_t)(xa_last[0] + ((l - xa_last[0]) * f >> 16));
        int16_t or_ = (int16_t)(xa_last[1] + ((r - xa_last[1]) * f >> 16));
        cd_push(ol, or_);
        xa_resample_acc += step;
    }
    xa_resample_acc -= 65536;
    xa_last[0] = l;
    xa_last[1] = r;
}

void spu_xa_sector(const uint8_t *s) {
    uint8_t coding = s[19];
    int stereo = (coding & 3) == 1;
    int rate = (coding >> 2) & 1 ? 18900 : 37800;
    int bits8 = ((coding >> 4) & 3) == 1;
    const uint8_t *data = s + 24;
    static int16_t out[2][28 * 8 * 18];
    int n[2] = {0, 0};
    for (int g = 0; g < 18; g++) {
        const uint8_t *grp = data + g * 128;
        int units = bits8 ? 4 : 8;
        for (int u = 0; u < units; u++) {
            uint8_t hdr = grp[4 + u];
            int shift = hdr & 0xF, filter = (hdr >> 4) & 3;
            if (shift > 12) shift = 9;
            int ch = stereo ? (u & 1) : 0;
            for (int i = 0; i < 28; i++) {
                int32_t t;
                if (bits8) t = (int8_t)grp[16 + i * 4 + u] << 8;
                else t = (int16_t)(((grp[16 + i * 4 + (u >> 1)] >> ((u & 1) * 4)) & 0xF) << 12);
                int32_t smp = (t >> shift) + ((xa_hist[ch][0] * pos_f[filter] + xa_hist[ch][1] * neg_f[filter] + 32) >> 6);
                if (smp > 32767) smp = 32767;
                if (smp < -32768) smp = -32768;
                xa_hist[ch][1] = xa_hist[ch][0];
                xa_hist[ch][0] = smp;
                out[ch][n[ch]++] = (int16_t)smp;
            }
        }
    }
    if (stereo) {
        for (int i = 0; i < n[0] && i < n[1]; i++) xa_out(out[0][i], out[1][i], rate);
    } else {
        for (int i = 0; i < n[0]; i++) xa_out(out[0][i], out[0][i], rate);
    }
}

/* ------------------------------------------------------------ output */

#define OUTBUF 32768
static int16_t out_buf[OUTBUF][2];
static volatile uint32_t out_rd, out_wr;

/* Take up to n stereo frames for the platform layer; returns the count. */
int spu_take_samples(int16_t *dst, int n) {
    int got = 0;
    while (got < n && out_rd != out_wr) {
        dst[got * 2] = out_buf[out_rd % OUTBUF][0];
        dst[got * 2 + 1] = out_buf[out_rd % OUTBUF][1];
        out_rd++;
        got++;
    }
    return got;
}

int spu_samples_available(void) { return (int)(out_wr - out_rd); }

/* Player's sound settings (not the game's): volumes in percent, and a
 * limiter that turns the output down smoothly when it would clip. They
 * only change what's heard, never the game. */
int g_audio_master = 100, g_audio_music = 100, g_audio_sfx = 100, g_audio_limiter = 0;
static int32_t lim_gain = 1 << 16;            /* 16.16 */

static void output(int32_t l, int32_t r) {
    if (g_audio_master != 100) {
        l = l * g_audio_master / 100;
        r = r * g_audio_master / 100;
    }
    if (g_audio_limiter) {
        const int32_t ceiling = 30000;
        int32_t peak = abs(l) > abs(r) ? abs(l) : abs(r);
        if ((int64_t)peak * lim_gain >> 16 > ceiling) lim_gain = (int32_t)(((int64_t)ceiling << 16) / peak);
        l = (int32_t)((int64_t)l * lim_gain >> 16);
        r = (int32_t)((int64_t)r * lim_gain >> 16);
        if (lim_gain < 1 << 16) {                 /* recover over about 0.2 s */
            lim_gain += ((1 << 16) - lim_gain) / 8192 + 1;
            if (lim_gain > 1 << 16) lim_gain = 1 << 16;
        }
    }
    if (l > 32767) l = 32767;
    if (l < -32768) l = -32768;
    if (r > 32767) r = 32767;
    if (r < -32768) r = -32768;
    if (out_wr - out_rd < OUTBUF) {
        out_buf[out_wr % OUTBUF][0] = (int16_t)l;
        out_buf[out_wr % OUTBUF][1] = (int16_t)r;
        out_wr++;
    }
}

static void mix_sample(void) {
    int32_t l = 0, r = 0, rl = 0, rr = 0;
    uint16_t cnt = REG(0x1AA);
    uint32_t eon = REG(0x198) | (uint32_t)REG(0x19A) << 16;
    /* noise generator */
    {
        int shift = (cnt >> 10) & 0xF, step = ((cnt >> 8) & 3) + 4;
        noise_timer -= step;
        int32_t period = 0x20000 >> shift;
        while (noise_timer < 0) {
            noise_timer += period;
            int parity = ((noise_level >> 15) ^ (noise_level >> 12) ^ (noise_level >> 11) ^ (noise_level >> 10) ^ 1) & 1;
            noise_level = (int16_t)((noise_level << 1) | parity);
        }
    }
    int32_t prev_out = 0;
    for (int n = 0; n < 24; n++) {
        Voice *v = &voices[n];
        if (v->env_phase == ENV_OFF && v->env == 0) { prev_out = 0; continue; }
        int32_t s = voice_sample(v, n, prev_out);
        env_tick(v, n);
        int32_t o = s * v->env >> 15;
        prev_out = o;
        int32_t vl = o * vol_reg((uint32_t)n * 16) >> 15, vr = o * vol_reg((uint32_t)n * 16 + 2) >> 15;
        l += vl;
        r += vr;
        if (eon & (1u << n)) { rl += vl; rr += vr; }
    }
    if (!(cnt & 0x8000)) { l = r = 0; rl = rr = 0; }       /* SPU disabled */
    if (cnt & 0x4000) {                                       /* unmuted */
        l = l * vol_reg(0x180) >> 15;
        r = r * vol_reg(0x182) >> 15;
    } else {
        l = r = 0;
    }
    /* CD audio */
    int32_t cl = 0, cr = 0, vcl = 0, vcr = 0;
    if (cd_rd != cd_wr) {
        cl = cd_buf[cd_rd % CDBUF][0];
        cr = cd_buf[cd_rd % CDBUF][1];
        cd_rd++;
    }
    {                                                         /* the CD-ROM's own mixer */
        int32_t ml = (cl * cd_vol[0] + cr * cd_vol[3]) >> 7;
        int32_t mr = (cr * cd_vol[2] + cl * cd_vol[1]) >> 7;
        cl = ml > 32767 ? 32767 : ml < -32768 ? -32768 : ml;
        cr = mr > 32767 ? 32767 : mr < -32768 ? -32768 : mr;
        if (cd_mute) cl = cr = 0;
    }
    if (cnt & 1) {
        cl = cl * (int16_t)REG(0x1B0) >> 15;
        cr = cr * (int16_t)REG(0x1B2) >> 15;
        l += cl;
        r += cr;
        vcl = cl;
        vcr = cr;
        if (cnt & 4) { rl += cl; rr += cr; }
    }
    /* reverb at half rate (22.05 kHz): the input is the average of two
     * samples and the output goes linearly from one result to the next,
     * instead of skipping / repeating samples (which adds a harsh hiss). */
    rv_in_l += rl;
    rv_in_r += rr;
    rv_phase ^= 1;
    int32_t ol, or_;
    if (rv_phase) {
        rv_prev_l = rv_out_l;
        rv_prev_r = rv_out_r;
        reverb(rv_in_l >> 1, rv_in_r >> 1);
        rv_in_l = rv_in_r = 0;
        ol = (rv_prev_l + rv_out_l) >> 1;
        or_ = (rv_prev_r + rv_out_r) >> 1;
    } else {
        ol = rv_out_l;
        or_ = rv_out_r;
    }
    static int norv = -1;
    if (norv < 0) norv = getenv("RT_NO_REVERB") != NULL;
    if ((cnt & 0x80) && !norv) { l += ol; r += or_; }
    if (g_audio_sfx != 100 || g_audio_music != 100) {
        /* the CD audio is in l/r already: take it out, rescale, put back */
        l -= vcl; r -= vcr;
        l = l * g_audio_sfx / 100 + vcl * g_audio_music / 100;
        r = r * g_audio_sfx / 100 + vcr * g_audio_music / 100;
    }
    output(l, r);
}

void spu_advance(uint32_t cycles) {
    gauss_init();
    cycle_acc += cycles;
    while (cycle_acc >= CYCLES_PER_SAMPLE) {
        cycle_acc -= CYCLES_PER_SAMPLE;
        mix_sample();
    }
}

/* ------------------------------------------------------------ registers */

uint16_t spu_read(uint32_t a) {
    uint32_t off = (a - 0x1F801C00) & 0x3FE;
    switch (off) {
    case 0x19C: return (uint16_t)endx;
    case 0x19E: return (uint16_t)(endx >> 16);
    case 0x1A6: return (uint16_t)(xfer_addr >> 3);
    case 0x1AE: {
        uint16_t cnt = REG(0x1AA);
        uint16_t s = (uint16_t)((cnt & 0x3F) | (REG(0x1AE) & 0x40));
        uint32_t mode = (cnt >> 4) & 3;
        if (mode == 2) s |= 0x0100 | 0x0080;
        if (mode == 3) s |= 0x0200 | 0x0080;
        return s;
    }
    }
    if (off < 0x180 && (off & 0xF) == 0xC) return (uint16_t)voices[off >> 4].env;
    if (off < 0x180 && (off & 0xF) == 0xE) return (uint16_t)(voices[off >> 4].repeat >> 3);
    return REG(off);
}

void spu_write(uint32_t a, uint16_t v) {
    uint32_t off = (a - 0x1F801C00) & 0x3FE;
    REG(off) = v;
    if (((off < 0x180 && (off & 0xF) < 4) || off == 0x180 || off == 0x182) && !(v & 0x8000))
        fixed_vol[off >> 1] = (int16_t)(v << 1);
    if (off < 0x180) {
        if ((off & 0xF) == 0xE) { voices[off >> 4].repeat = (uint32_t)v * 8; voices[off >> 4].repeat_set = 1; }
        return;
    }
    switch (off) {
    case 0x188: for (int i = 0; i < 16; i++) if (v & (1u << i)) key_on(i); break;
    case 0x18A: for (int i = 0; i < 8; i++) if (v & (1u << i)) key_on(16 + i); break;
    case 0x18C: for (int i = 0; i < 16; i++) if (v & (1u << i)) key_off(i); break;
    case 0x18E: for (int i = 0; i < 8; i++) if (v & (1u << i)) key_off(16 + i); break;
    case 0x19C: case 0x19E: break;           /* ENDX is read-only */
    case 0x1A2: rv_cur = (uint32_t)v * 8; break;
    case 0x1A6: xfer_addr = (uint32_t)v * 8; break;
    case 0x1A8:
        irq_check_addr(xfer_addr, 2);
        ram[xfer_addr & 0x7FFFF] = (uint8_t)v;
        ram[(xfer_addr + 1) & 0x7FFFF] = (uint8_t)(v >> 8);
        xfer_addr = (xfer_addr + 2) & 0x7FFFF;
        break;
    case 0x1AA:
        if (!(v & 0x40)) REG(0x1AE) &= ~0x40;  /* IRQ acknowledge */
        break;
    }
}

void spu_dma_write(uint32_t v) {
    irq_check_addr(xfer_addr, 4);
    for (int i = 0; i < 4; i++) ram[(xfer_addr + i) & 0x7FFFF] = (uint8_t)(v >> (8 * i));
    xfer_addr = (xfer_addr + 4) & 0x7FFFF;
}

uint32_t spu_dma_read(void) {
    uint32_t v = 0;
    for (int i = 0; i < 4; i++) v |= (uint32_t)ram[(xfer_addr + i) & 0x7FFFF] << (8 * i);
    xfer_addr = (xfer_addr + 4) & 0x7FFFF;
    return v;
}

void spu_state(StateIO *io) {
    ss_module(io, "spu");
    SS(io, regs);
    SS(io, ram);
    SS(io, xfer_addr);
    SS(io, cycle_acc);
    SS(io, voices);
    SS(io, endx);
    SS(io, noise_level);
    SS(io, noise_timer);
    SS(io, rv_cur);
    SS(io, rv_phase);
    SS(io, rv_out_l);
    SS(io, rv_out_r);
    SS(io, cd_buf);
    SS(io, cd_rd);
    SS(io, cd_wr);
    SS(io, xa_hist);
    SS(io, xa_resample_acc);
    SS(io, xa_last);
    SS(io, rv_prev_l);
    SS(io, rv_prev_r);
    SS(io, rv_in_l);
    SS(io, rv_in_r);
    SS(io, fixed_vol);
}

/* After loading: drop sound mixed before it. */
void spu_reset_output(void) {
    out_rd = out_wr;
    /* saves from before fixed_vol was kept: take it from the registers */
    for (uint32_t off = 0; off < 0x184; off += 2) {
        int is_vol = (off < 0x180 && (off & 0xF) < 4) || off == 0x180 || off == 0x182;
        if (is_vol && !(REG(off) & 0x8000) && !fixed_vol[off >> 1]) fixed_vol[off >> 1] = (int16_t)(REG(off) << 1);
    }
}

/* Write sound data straight into SPU RAM from the PC side (as a finished
 * DMA transfer would leave it). */
void spu_write_ram(uint32_t addr, const uint8_t *data, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) ram[(addr + i) & 0x7FFFF] = data[i];
}
