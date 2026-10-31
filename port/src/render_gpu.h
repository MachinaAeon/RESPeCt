/* The high-resolution picture drawn by the graphics card (Direct3D 11): the
 * same queue render.c's worker threads run, run by a compute shader, one
 * thread per pixel, each pixel's operations in the queue's order. The PS1's
 * own VRAM stays on the CPU (render.c draws it and the game reads it); the
 * card gets a copy of it with every batch, for textures.
 *
 * render.c packs each queued operation into GOP_WORDS words (below); the
 * shader (render_gpu.hlsl) reads them by the same offsets. */
#ifndef RENDER_GPU_H
#define RENDER_GPU_H

#include <stddef.h>
#include <stdint.h>

#define GOP_WORDS 128

/* kinds */
enum { GK_TRI, GK_RECT, GK_FILL, GK_ZCLEAR, GK_UPLOAD };

/* word offsets */
enum {
    GO_KIND = 0, GO_FLAGS, GO_TEX, GO_CLUT, GO_TWIN,
    GO_X0, GO_X1, GO_Y0, GO_Y1,                  /* triangles, rectangles: high-res pixels, inclusive */
    GO_UV, GO_ID, GO_COLOR, GO_DU, GO_DV, GO_DR, GO_DG, GO_DB,
    GO_RX, GO_RY, GO_RW, GO_RH,                  /* rectangles: origin and size; fills, uploads: area (PS1 pixels) */
    GO_U0V0, GO_POOL, GO_FILLC,
    GO_BIAS = 24,                                /* 3 ints */
    GO_EX = 28, GO_EY = 34, GO_EC = 40, GO_MB = 46,   /* 3 doubles each */
    GO_PL = 52,                                  /* 12 planes of 3 doubles: PR PG PB PU PV PW QW QU QV QR QG QB */
    GO_SQ0 = 124, GO_SQK = 126                   /* rectangles squeezed (widescreen): doubles */
};

/* flags */
#define GF_TEX      (1u << 0)
#define GF_RAW      (1u << 1)
#define GF_SEMI     (1u << 2)
#define GF_ABR_SH   3
#define GF_DEPTH_SH 5
#define GF_MSET     (1u << 7)
#define GF_MCHK     (1u << 8)
#define GF_GOUR     (1u << 9)
#define GF_SMOOTH   (1u << 11)
#define GF_PERSP    (1u << 12)
#define GF_PCOL     (1u << 13)
#define GF_ZUSE     (1u << 14)
#define GF_ZTEST    (1u << 15)
#define GF_ZWRITE   (1u << 16)
#define GF_GUARD    (1u << 17)
#define GF_MARGIN   (1u << 18)
#define GF_FLIPX    (1u << 19)
#define GF_FLIPY    (1u << 20)
#define GF_SQ       (1u << 21)
#define GF_ZT       (1u << 22)
#define GF_BIGFILL  (1u << 23)

typedef struct {
    float depth_tol, cross_tol, margin_depth;
    int true_color, far_page, depth_any, margin_any;
} GpuParams;

int rgpu_init(int scale);                        /* 0: no usable card (the CPU draws) */
void rgpu_set_scale(int scale);
/* Run a batch: ops (n of them, GOP_WORDS each), the pixels uploads take
 * (pool), the PS1's VRAM as it is now (textures). */
void rgpu_draw(const uint32_t *ops, int n, const uint16_t *pool, size_t npool, const uint16_t *vram, const GpuParams *gp);
void rgpu_copy(int sx, int sy, int dx, int dy, int w, int h, int mask_set, int mask_check);   /* PS1 pixels */
/* Read high-res rows y0..y0+h (wrapping), columns x0..x0+w, into arrays laid
 * out as render.c's (stride 1024 * scale). */
void rgpu_read(int x0, int y0, int w, int h, uint16_t *hires, uint16_t *low, uint8_t *bare, float *z);
/* The picture shown (display.c): the display area x, y, w x h (high-res
 * pixels) as 32-bit BGRX through gamma8, with Close model gaps if gaps;
 * made ow x oh (the window's size: smooth, by area) if ow > 0. */
void rgpu_picture(int x, int y, int w, int h, int gaps, const uint8_t gamma8[256], uint32_t *out, int ow, int oh, int smooth);
/* Replace the card's copy with these (after a load or a change of scale). */
void rgpu_write(const uint16_t *hires, const uint16_t *low, const uint8_t *bare, const float *z, const uint32_t *zid);

#endif
