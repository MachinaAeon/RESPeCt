/* MDEC: the PS1's video decoder (psx-spx "Macroblock Decoder").
 *
 * The game's movie player decodes the bitstream's variable-length codes
 * itself and hands the MDEC run-length coded blocks over DMA0; the MDEC
 * dequantizes, runs the inverse DCT, converts YUV to RGB and returns
 * pixels over DMA1. Here decoding happens as soon as the input arrives. */
#include "rt.h"
#include "savestate.h"

#include <stdlib.h>

static uint8_t qt_y[64], qt_uv[64];
static int16_t scale[64];

/* input */
static uint32_t cmd;
static uint32_t words_left;          /* parameter words still expected */
static int mode;                     /* 1 decode, 2 quant table, 3 scale table */
static uint32_t *in_buf;
static uint32_t in_n, in_cap;

/* output FIFO */
static uint32_t *out_buf;
static uint32_t out_n, out_cap, out_pos;

static int dma_in_en, dma_out_en;

static const uint8_t zigzag[64] = {
    0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34, 27, 20, 13, 6, 7, 14, 21, 28,
    35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37, 44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63};

static void out_push(uint32_t w) {
    if (out_n == out_cap) {
        out_cap = out_cap ? out_cap * 2 : 4096;
        out_buf = realloc(out_buf, out_cap * 4);
    }
    out_buf[out_n++] = w;
}

static int sign10(int v) { return (v & 0x200) ? v - 0x400 : v; }

/* Run-length decode one 8x8 block from halfwords at *p; returns 0 at end of data. */
static int rl_block(const uint16_t *h, uint32_t nh, uint32_t *p, int16_t blk[64], const uint8_t *qt) {
    memset(blk, 0, 64 * sizeof blk[0]);
    while (*p < nh && h[*p] == 0xFE00) (*p)++;              /* padding */
    if (*p >= nh) return 0;
    uint16_t n = h[(*p)++];
    int q = n >> 10;
    int k = 0;
    int val = q == 0 ? sign10(n & 0x3FF) * 2 : sign10(n & 0x3FF) * qt[0];
    for (;;) {
        if (val < -0x400) val = -0x400;
        if (val > 0x3FF) val = 0x3FF;
        if (q == 0) blk[k] = (int16_t)val;
        else blk[zigzag[k]] = (int16_t)val;
        if (*p >= nh) break;
        n = h[(*p)++];
        if (n == 0xFE00) break;
        k += (n >> 10) + 1;
        if (k > 63) break;
        val = q == 0 ? sign10(n & 0x3FF) * 2 : (sign10(n & 0x3FF) * qt[k] * q + 4) / 8;
    }
    return 1;
}

/* psx-spx real_idct_core: two passes of a matrix product with the scale table. */
static void idct(int16_t blk[64]) {
    int32_t tmp[64], src[64];
    for (int i = 0; i < 64; i++) src[i] = blk[i];
    for (int pass = 0; pass < 2; pass++) {
        for (int x = 0; x < 8; x++) {
            for (int y = 0; y < 8; y++) {
                int64_t sum = 0;
                for (int z = 0; z < 8; z++) sum += (int64_t)src[y + z * 8] * (scale[x + z * 8] / 8);
                tmp[x + y * 8] = (int32_t)((sum + 0xFFF) / 0x2000);
            }
        }
        memcpy(src, tmp, sizeof src);
    }
    for (int i = 0; i < 64; i++) {
        int32_t v = src[i];
        blk[i] = (int16_t)(v < -128 ? -128 : v > 127 ? 127 : v);
    }
}

static void emit_rgb(int16_t cr[64], int16_t cb[64], int16_t ys[4][64], int depth, int is_signed, int bit15) {
    uint8_t px[16][16][3];
    static const int ox[4] = {0, 8, 0, 8}, oy[4] = {0, 0, 8, 8};
    for (int b = 0; b < 4; b++) {
        for (int y = 0; y < 8; y++) {
            for (int x = 0; x < 8; x++) {
                int cx = (x + ox[b]) / 2, cy = (y + oy[b]) / 2;
                int vr = cr[cx + cy * 8], vb = cb[cx + cy * 8];
                int dg = (-88 * vb - 183 * vr) >> 8;
                int dr = (359 * vr) >> 8;
                int db = (454 * vb) >> 8;
                int luma = ys[b][x + y * 8];
                int r = luma + dr, g = luma + dg, bb = luma + db;
                r = r < -128 ? -128 : r > 127 ? 127 : r;
                g = g < -128 ? -128 : g > 127 ? 127 : g;
                bb = bb < -128 ? -128 : bb > 127 ? 127 : bb;
                if (!is_signed) { r += 128; g += 128; bb += 128; }
                px[y + oy[b]][x + ox[b]][0] = (uint8_t)r;
                px[y + oy[b]][x + ox[b]][1] = (uint8_t)g;
                px[y + oy[b]][x + ox[b]][2] = (uint8_t)bb;
            }
        }
    }
    if (depth == 2) {                                          /* 24-bit */
        uint8_t bytes[16 * 16 * 3];
        int k = 0;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                for (int c = 0; c < 3; c++) bytes[k++] = px[y][x][c];
        for (int i = 0; i < k; i += 4)
            out_push(bytes[i] | bytes[i + 1] << 8 | bytes[i + 2] << 16 | (uint32_t)bytes[i + 3] << 24);
    } else {                                                   /* 15-bit */
        uint16_t pix[256];
        int k = 0;
        for (int y = 0; y < 16; y++)
            for (int x = 0; x < 16; x++)
                pix[k++] = (uint16_t)((px[y][x][0] >> 3) | ((px[y][x][1] >> 3) << 5) | ((px[y][x][2] >> 3) << 10) |
                                      (bit15 ? 0x8000 : 0));
        for (int i = 0; i < 256; i += 2) out_push(pix[i] | (uint32_t)pix[i + 1] << 16);
    }
}

static void emit_mono(int16_t y[64], int depth, int is_signed) {
    uint8_t v[64];
    for (int i = 0; i < 64; i++) v[i] = (uint8_t)(is_signed ? y[i] : y[i] + 128);
    if (depth == 1) {
        for (int i = 0; i < 64; i += 4) out_push(v[i] | v[i + 1] << 8 | v[i + 2] << 16 | (uint32_t)v[i + 3] << 24);
    } else {
        for (int i = 0; i < 64; i += 8) {
            uint32_t w = 0;
            for (int j = 0; j < 8; j++) w |= (uint32_t)(v[i + j] >> 4) << (4 * j);
            out_push(w);
        }
    }
}

static void decode(void) {
    const uint16_t *h = (const uint16_t *)in_buf;
    uint32_t nh = in_n * 2, p = 0;
    int depth = (cmd >> 27) & 3, is_signed = (cmd >> 26) & 1, bit15 = (cmd >> 25) & 1;
    int16_t cr[64], cb[64], ys[4][64];
    if (depth >= 2) {
        for (;;) {
            if (!rl_block(h, nh, &p, cr, qt_uv)) break;
            idct(cr);
            if (!rl_block(h, nh, &p, cb, qt_uv)) break;
            idct(cb);
            int ok = 1;
            for (int b = 0; b < 4 && ok; b++) {
                ok = rl_block(h, nh, &p, ys[b], qt_y);
                if (ok) idct(ys[b]);
            }
            if (!ok) break;
            emit_rgb(cr, cb, ys, depth, is_signed, bit15);
        }
    } else {
        int16_t y[64];
        while (rl_block(h, nh, &p, y, qt_y)) {
            idct(y);
            emit_mono(y, depth, is_signed);
        }
    }
}

static void command_done(void) {
    if (mode == 1) decode();
    else if (mode == 2) {
        const uint8_t *b = (const uint8_t *)in_buf;
        memcpy(qt_y, b, 64);
        if (cmd & 1) memcpy(qt_uv, b + 64, 64);
    } else if (mode == 3) {
        memcpy(scale, in_buf, 128);
    }
    mode = 0;
    in_n = 0;
    dma_mdec_kick();
}

void mdec_dma_in(uint32_t v) { mdec_write(0x1F801820, v); }

uint32_t g_mdec_frame = 0xFFFF0000u;          /* last frame a picture was decoded */

void mdec_write(uint32_t a, uint32_t v) {
    if (a == 0x1F801824) {
        if (v & 0x80000000) {                       /* reset */
            mode = 0;
            words_left = 0;
            in_n = 0;
            out_n = out_pos = 0;
        }
        dma_in_en = (v >> 30) & 1;
        dma_out_en = (v >> 29) & 1;
        return;
    }
    if (mode == 0) {
        cmd = v;
        if ((v >> 29) == 1) g_mdec_frame = g_frame;   /* decoding: a movie is playing */
        switch (v >> 29) {
        case 1:
            mode = 1;
            words_left = v & 0xFFFF;
            /* a new decode starts a fresh output stream */
            if (out_pos >= out_n) out_n = out_pos = 0;
            break;
        case 2: mode = 2; words_left = (v & 1) ? 32 : 16; break;
        case 3: mode = 3; words_left = 32; break;
        default: return;
        }
        in_n = 0;
        if (!words_left) command_done();
        return;
    }
    if (in_n == in_cap) {
        in_cap = in_cap ? in_cap * 2 : 8192;
        in_buf = realloc(in_buf, in_cap * 4);
    }
    in_buf[in_n++] = v;
    if (--words_left == 0) command_done();
}

uint32_t mdec_dma_out(void) {
    if (out_pos < out_n) return out_buf[out_pos++];
    return 0;
}

int mdec_out_ready(void) { return (int)(out_n - out_pos); }

uint32_t mdec_read(uint32_t a) {
    if (a == 0x1F801820) return mdec_dma_out();
    uint32_t s = 0;
    if (out_pos >= out_n) s |= 1u << 31;
    if (mode) s |= 1u << 29;
    if (dma_in_en && mode) s |= 1u << 28;
    if (dma_out_en && out_pos < out_n) s |= 1u << 27;
    s |= ((cmd >> 25) & 0xF) << 23;
    s |= (words_left - 1) & 0xFFFF;
    return s;
}

void mdec_state(StateIO *io) {
    ss_module(io, "mdec");
    SS(io, qt_y);
    SS(io, qt_uv);
    SS(io, scale);
    SS(io, cmd);
    SS(io, words_left);
    SS(io, mode);
    ss_buffer(io, "in_buf", (void **)&in_buf, &in_n, &in_cap, sizeof *in_buf);
    ss_buffer(io, "out_buf", (void **)&out_buf, &out_n, &out_cap, sizeof *out_buf);
    SS(io, out_pos);
    SS(io, dma_in_en);
    SS(io, dma_out_en);
}
