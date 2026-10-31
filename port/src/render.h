/* Rasterizer for the PS1 GPU's primitives.
 *
 * Two copies of video memory are kept: the PS1's own 1024x512 VRAM
 * (g_vram), drawn immediately and used for textures and for anything the
 * game reads back, and a high-resolution copy at resolution_scale x, which
 * is what gets displayed. High-resolution drawing is queued and done by
 * worker threads in horizontal bands at the end of each frame (or earlier
 * when the game is about to use something it drew as a texture). */
#ifndef RENDER_H
#define RENDER_H

#include <stdint.h>

#include "texpack.h"

typedef struct {
    float x, y;                /* VRAM coordinates (drawing offset applied) */
    int r, g, b, u, v;
    float z;                   /* GTE screen depth (SZ), 0 = unknown */
} RVtx;

enum { RP_TRI, RP_RECT, RP_LINE };

typedef struct RPrim {
    int kind;
    RVtx v[3];
    int gouraud, textured, raw, semi, abr, dither;
    int tx, ty, depth, clut_x, clut_y;
    int twmx, twmy, twox, twoy;              /* texture window */
    int dx1, dy1, dx2, dy2;                  /* drawing area, inclusive */
    int mask_set, mask_check;
    int w, h, flipx, flipy;                  /* rectangles */
    int zt;                                  /* 3D with depth: depth-test it */
    float sqx0, sqx1;                        /* widescreen: a rectangle squeezed to x sqx0..sqx1 (if sqx1 > sqx0) */
    const HdImg *hd;                         /* HD texture pack image to draw from (texpack.c), high-res only: */
    float hd_ox, hd_oy, hd_kx, hd_ky;        /*   its pixel = (hd_ox + u * hd_kx, hd_oy + v * hd_ky) */
} RPrim;
void texpack_attach(RPrim *p, int ax, int ay, int aw, int ah);   /* texpack.c */

void render_init(int scale, int threads);
int render_scale(void);
void render_set_scale(int scale);                    /* change it while running */
void render_prim(const RPrim *p);
void render_fill(int x, int y, int w, int h, uint16_t color);
void render_copy(int sx, int sy, int dx, int dy, int w, int h, int mask_set, int mask_check);
void render_before_write(int x, int y, int w, int h); /* CPU is about to write this g_vram area */
void render_uploaded(int x, int y, int w, int h);  /* g_vram area written by the CPU */
void render_sync(void);
void render_want(int x, int y, int w, int h);      /* the high-res area the picture shown needs next */
int render_on_gpu(void);                           /* drawing with the graphics card */
void render_zclear(int x, int y, int w, int h);    /* a new frame starts drawing here */
extern int g_depth_buffer;                         /* setting */                            /* finish queued high-res drawing */
const uint16_t *render_hires(int *stride);
const uint16_t *render_low(void);                 /* 24-bit colour: the low bits of the high-res VRAM (render.c) */
const uint8_t *render_bare(void);                 /* pixels still bare from the screen clear (render.c) */         /* high-res VRAM (scale x 1024 wide) */
const float *render_depth(void);                  /* 1/z per high-res pixel, -1 under 2D (render.c) */

extern int g_gap_fill;                     /* Close model gaps (margins on distant models' triangles) */

#endif
