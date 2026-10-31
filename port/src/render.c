/* Rasterizer (see render.h). Triangles use edge functions with 8 bits of
 * sub-pixel precision and attributes interpolated from plane equations, so
 * the same code draws at the PS1's resolution (integer vertices, its
 * fill rules) and at higher resolutions (vertices that may carry the
 * GTE's fractions). */
#ifdef _WIN32
#include <windows.h>          /* before rt.h: its R macro clashes with winnt.h */
#else
#include <pthread.h>
#include <semaphore.h>
#endif

#include "render.h"
#include "render_gpu.h"
#include "rt.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint16_t *buf;
    int stride, scale, height;
} Target;

static int S = 1;
static uint16_t *hires;                      /* (1024*S) x (512*S) */
/* Bare: the pixel still holds the game's clear of the screen (a large fill)
 * and nothing opaque has been drawn on it since. Thin runs of bare pixels
 * between drawn ones are cracks: hair-thin gaps where a room's walls,
 * floors and ceilings meet (their edges a fraction of a PS1 pixel apart,
 * which the PS1's whole pixels shut). The picture shown fills them from
 * the neighbouring pixels (Close model gaps). */
static uint8_t *bare;
/* 24-bit colour: the 3 bits of each 8-bit channel that the 15-bit picture
 * drops (red in bits 0-2, green 3-5, blue 6-8), bit 15 set while they
 * belong to the pixel there. The PS1's VRAM keeps its 15 bits; this only
 * makes the picture shown finer. */
static uint16_t *low;
static size_t hires_n;
/* Depth buffer for the high-resolution picture (1/z per pixel, 0 = far).
 * The PS1 has none: it draws a frame's polygons back to front in buckets
 * of depth, and inside a bucket in any order, so parts of a model can show
 * through its front (a head's back over its face); at the original
 * resolution that is a pixel or two, at 4x it's plain to see. 3D polygons
 * (every corner from the GTE, with its depth) are tested against it. */
int g_depth_buffer = 1;
int g_tex_edge, g_no_overlap, g_margin_any, g_margin_log;
unsigned g_margin_hist[7];
static float g_margin_depth = 64;
static double g_margin_area = 8;                /* PS1 pixels (RT_MARGIN_AREA) */               /* GTE depth units behind (RT_MARGIN_DEPTH) */
static double g_overlap = 0.75;                 /* PS1 pixels (RT_OVERLAP) */
/* A model's own surfaces keep the PS1's order this close (RT_DEPTH_TOL,
 * GTE depth units). 2 now, was 24 (or 0.4% of the distance): with the
 * models' corners and depths in full precision (gte.c) a head and the
 * shirt it pokes out of are told apart a few units apart, and with 24 the
 * shirt, drawn later, came over the top of the head in some frames and not
 * in others as the camera turned. */
static float g_depth_tol = 2;
static float g_depth_rel = 0;               /* ... or this share of the distance, if more (RT_DEPTH_REL) */
static int g_fill_mark, g_sliver_mark;                          /* testing: RT_FILL_MARK fills the screen magenta (gaps show) */
static int g_depth_any;                          /* testing: RT_DEPTH_ANY hides any model behind any */
static int g_far_page = 1;                       /* RT_FAR_PAGE=0: off */
/* Another object: hidden this far behind (RT_CROSS_TOL; 0 = never), well
 * behind only: a model reaching into another (a dead zombie's hand through
 * the floor, the lab tyrant into its tank's frame) stays drawn as on the
 * PS1. A model's own parts are one object (same_object). */
static float g_cross_tol = 128;
static double g_margin_len = 6;                  /* longest edge (PS1 pixels) of a triangle given the gap margin; RT_MARGIN_LEN */
int g_gap_fill = 1; 
int g_persp = 1;                                /* Perspective-correct textures (setting) */
int g_smooth_tex = 1;                           /* Smooth textures (setting) */
int g_true_color = 1;                           /* 24-bit colour (setting) */
                            /* Close model gaps (setting) */                                 /* testing: RT_TEX_EDGE samples the far texel edge too */
static float *zbuf;
/* What wrote each depth: the texture page and CLUT (a model part and its
 * own triangles share both). The Depth fix hides a model's own back behind
 * its front, not one thing behind another (those keep the PS1's order: the
 * game sorted them, and a model reaching into another, like the lab tyrant
 * into its tank's frame or a dart board into its wall, stays drawn as on
 * the PS1). */
static uint32_t *zid;
static Target native_t, hires_t;

/* ------------------------------------------------------------ pixels */

static const int dither4[4][4] = {{-4, 0, -3, 1}, {2, -2, 3, -1}, {-3, 1, -4, 0}, {3, -1, 2, -2}};

static inline uint16_t texel(const RPrim *p, int u, int v) {
    u &= 0xFF;
    v &= 0xFF;
    u = (u & ~(p->twmx * 8)) | ((p->twox & p->twmx) * 8);
    v = (v & ~(p->twmy * 8)) | ((p->twoy & p->twmy) * 8);
    int y = (p->ty + v) & 511;
    switch (p->depth) {
    case 0: {
        uint16_t w = g_vram[y][(p->tx + (u >> 2)) & 1023];
        return g_vram[p->clut_y][(p->clut_x + ((w >> ((u & 3) * 4)) & 0xF)) & 1023];
    }
    case 1: {
        uint16_t w = g_vram[y][(p->tx + (u >> 1)) & 1023];
        return g_vram[p->clut_y][(p->clut_x + ((w >> ((u & 1) * 8)) & 0xFF)) & 1023];
    }
    default:
        return g_vram[y][(p->tx + u) & 1023];
    }
}

static inline void put(uint16_t *d, uint16_t c, int semi, const RPrim *p) {
    if (p->mask_check && (*d & 0x8000)) return;
    if (semi) {
        int br = *d & 31, bg = (*d >> 5) & 31, bb = (*d >> 10) & 31;
        int fr = c & 31, fg = (c >> 5) & 31, fb = (c >> 10) & 31;
        int r, g, b;
        switch (p->abr) {
        case 0: r = (br + fr) >> 1; g = (bg + fg) >> 1; b = (bb + fb) >> 1; break;
        case 1: r = br + fr; g = bg + fg; b = bb + fb; break;
        case 2: r = br - fr; g = bg - fg; b = bb - fb; break;
        default: r = br + (fr >> 2); g = bg + (fg >> 2); b = bb + (fb >> 2); break;
        }
        r = r < 0 ? 0 : r > 31 ? 31 : r;
        g = g < 0 ? 0 : g > 31 ? 31 : g;
        b = b < 0 ? 0 : b > 31 ? 31 : b;
        c = (uint16_t)((c & 0x8000) | r | (g << 5) | (b << 10));
    }
    *d = c | (p->mask_set ? 0x8000 : 0);
    if (hires) {
        size_t i = (size_t)(d - hires);
        if (i < hires_n) {
            if (!semi) bare[i] = 0;
            low[i] = 0;
            if (!p->zt) zbuf[i] = -1;            /* 2D over it (see render_depth) */
        }
    }
}

/* The 24-bit colour plane's entry for a pixel of the high-resolution
 * picture with 24-bit colour on; NULL otherwise (the PS1's VRAM). */
static inline uint16_t *low_of(uint16_t *d) {
    if (!g_true_color || !hires) return NULL;
    size_t i = (size_t)(d - hires);
    return i < hires_n ? &low[i] : NULL;
}

static inline int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }

/* put for 24-bit colour: 8-bit channels, blended in 8 bits (with the pixel
 * there at its full precision), the top 5 bits to the picture and the rest
 * to the low plane. */
static inline void put8(uint16_t *d, uint16_t *lp, int r, int g, int b, int stp, int semi, const RPrim *p) {
    if (p->mask_check && (*d & 0x8000)) return;
    r = clamp8(r); g = clamp8(g); b = clamp8(b);
    if (semi) {
        int c5[3] = {*d & 31, (*d >> 5) & 31, (*d >> 10) & 31}, bk[3];
        for (int k = 0; k < 3; k++)
            bk[k] = (*lp & 0x8000) ? (c5[k] << 3) | ((*lp >> (3 * k)) & 7) : (c5[k] << 3) | (c5[k] >> 2);
        switch (p->abr) {
        case 0: r = (bk[0] + r) >> 1; g = (bk[1] + g) >> 1; b = (bk[2] + b) >> 1; break;
        case 1: r = bk[0] + r; g = bk[1] + g; b = bk[2] + b; break;
        case 2: r = bk[0] - r; g = bk[1] - g; b = bk[2] - b; break;
        default: r = bk[0] + (r >> 2); g = bk[1] + (g >> 2); b = bk[2] + (b >> 2); break;
        }
        r = clamp8(r); g = clamp8(g); b = clamp8(b);
    }
    *d = (uint16_t)((stp ? 0x8000 : 0) | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | (p->mask_set ? 0x8000 : 0));
    *lp = (uint16_t)(0x8000 | (r & 7) | ((g & 7) << 3) | ((b & 7) << 6));
    if (!semi) bare[lp - low] = 0;
    if (!p->zt) zbuf[lp - low] = -1;
}

/* 5-bit channel to 8 bits, as the picture shows it */
static inline int expand5(int c) { return (c << 3) | (c >> 2); }

/* Shade one pixel: texture and/or colour, dithering, blending. */
static inline void shade(const RPrim *p, uint16_t *d, int x, int y, int r, int g, int b, int u, int v, int dith) {
    uint16_t *lp = low_of(d);
    if (lp) {
        /* 24-bit colour: lit in full precision, no dither (the PC
         * version's Direct3D draws so). The game's colour 128 leaves a
         * texel as it is, as on the PS1. */
        if (p->textured) {
            uint16_t t = texel(p, u, v);
            if (!t) return;
            int tr = expand5(t & 31), tg = expand5((t >> 5) & 31), tb = expand5((t >> 10) & 31);
            if (!p->raw) { tr = (tr * r + 64) >> 7; tg = (tg * g + 64) >> 7; tb = (tb * b + 64) >> 7; }
            put8(d, lp, tr, tg, tb, t & 0x8000, p->semi && (t & 0x8000), p);
        } else
            put8(d, lp, r, g, b, 0, p->semi, p);
        return;
    }
    if (p->textured) {
        uint16_t t = texel(p, u, v);
        if (!t) return;
        if (!p->raw) {
            int tr = (t & 31) * r, tg = ((t >> 5) & 31) * g, tb = ((t >> 10) & 31) * b;
            if (dith) {
                int dd = dither4[y & 3][x & 3] * 16;
                tr += dd; tg += dd; tb += dd;
            }
            tr = tr < 0 ? 0 : tr >> 7;
            tg = tg < 0 ? 0 : tg >> 7;
            tb = tb < 0 ? 0 : tb >> 7;
            if (tr > 31) tr = 31;
            if (tg > 31) tg = 31;
            if (tb > 31) tb = 31;
            t = (uint16_t)((t & 0x8000) | tr | (tg << 5) | (tb << 10));
        }
        put(d, t, p->semi && (t & 0x8000), p);
    } else {
        if (dith) {
            int dd = dither4[y & 3][x & 3];
            r += dd; g += dd; b += dd;
            r = r < 0 ? 0 : r > 255 ? 255 : r;
            g = g < 0 ? 0 : g > 255 ? 255 : g;
            b = b < 0 ? 0 : b > 255 ? 255 : b;
        }
        put(d, (uint16_t)((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10)), p->semi, p);
    }
}

/* HD texture pack image at texel position (U, V) (texture page texels,
 * fractions included): bilinear between the opaque and semi-transparent
 * pixels around it; the nearest one says transparent or not (alpha
 * under 1/4) and semi-transparent or not (under 3/4: the STP bit).
 * 0: transparent. */
/* lo/hi: the texels the polygon maps (hi one past the last); filtering
 * stays inside them, as a PS1 texture's tiles are cut from a sheet of
 * others (a neighbour's dark edge would show as lines along every tile). */
static inline int hd_texel(const RPrim *p, float U, float V, const float *lo, float *rgb, int *stp) {
    const HdImg *im = p->hd;
    float fx = p->hd_ox + U * p->hd_kx, fy = p->hd_oy + V * p->hd_ky;
    int w = im->w, h = im->h;
    int nx = (int)fx, ny = (int)fy;
    nx = nx < 0 ? 0 : nx >= w ? w - 1 : nx;
    ny = ny < 0 ? 0 : ny >= h ? h - 1 : ny;
    uint32_t c = im->px[(size_t)ny * w + nx];
    int a = (int)(c >> 24);
    if (a < 64) return 0;
    {
        float x_lo = p->hd_ox + lo[0] * p->hd_kx + 0.5f, x_hi = p->hd_ox + lo[1] * p->hd_kx - 0.5f;
        float y_lo = p->hd_oy + lo[2] * p->hd_ky + 0.5f, y_hi = p->hd_oy + lo[3] * p->hd_ky - 0.5f;
        if (x_hi >= x_lo) fx = fx < x_lo ? x_lo : fx > x_hi ? x_hi : fx;
        if (y_hi >= y_lo) fy = fy < y_lo ? y_lo : fy > y_hi ? y_hi : fy;
    }
    fx -= 0.5f; fy -= 0.5f;
    int x0 = (int)floorf(fx), y0 = (int)floorf(fy);
    float ax = fx - (float)x0, ay = fy - (float)y0;
    int xa = x0 < 0 ? 0 : x0 >= w ? w - 1 : x0, xb = x0 + 1 < 0 ? 0 : x0 + 1 >= w ? w - 1 : x0 + 1;
    int ya = y0 < 0 ? 0 : y0 >= h ? h - 1 : y0, yb = y0 + 1 < 0 ? 0 : y0 + 1 >= h ? h - 1 : y0 + 1;
    uint32_t q[4] = {im->px[(size_t)ya * w + xa], im->px[(size_t)ya * w + xb], im->px[(size_t)yb * w + xa], im->px[(size_t)yb * w + xb]};
    float wt[4] = {(1 - ax) * (1 - ay), ax * (1 - ay), (1 - ax) * ay, ax * ay};
    float r = 0, g = 0, b = 0, sum = 0;
    for (int i = 0; i < 4; i++) {
        if ((q[i] >> 24) < 64) continue;
        r += wt[i] * (float)(q[i] & 0xFF);
        g += wt[i] * (float)((q[i] >> 8) & 0xFF);
        b += wt[i] * (float)((q[i] >> 16) & 0xFF);
        sum += wt[i];
    }
    if (sum <= 0) { r = (float)(c & 0xFF); g = (float)((c >> 8) & 0xFF); b = (float)((c >> 16) & 0xFF); sum = 1; }
    rgb[0] = r / sum; rgb[1] = g / sum; rgb[2] = b / sum;
    *stp = a < 192;
    return 1;
}

/* 4x4 ordered dither, in 1/16ths of a 5-bit step */
static const uint8_t bayer4[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};

/* The game's own texture at texel position (U, V), fractions included,
 * filtered as the PC version's Direct3D draws it: bilinear between the
 * four texels around, leaving out the transparent ones (colour 0), so a
 * cut-out edge doesn't darken. The nearest texel says transparent or not,
 * and semi-transparent or not (its STP bit), as at 1x. lo: as hd_texel,
 * the texels the polygon maps, which filtering stays inside. 0:
 * transparent. Colours to 0..255. */
static inline int smooth_texel(const RPrim *p, float U, float V, const float *lo, float *rgb, int *stp) {
    uint16_t n = texel(p, (int)U, (int)V);
    if (!n) return 0;
    float x_lo = lo[0] + 0.5f, x_hi = lo[1] - 0.5f, y_lo = lo[2] + 0.5f, y_hi = lo[3] - 0.5f;
    if (x_hi >= x_lo) U = U < x_lo ? x_lo : U > x_hi ? x_hi : U;
    if (y_hi >= y_lo) V = V < y_lo ? y_lo : V > y_hi ? y_hi : V;
    U -= 0.5f; V -= 0.5f;
    int u0 = (int)floorf(U), v0 = (int)floorf(V);
    float au = U - (float)u0, av = V - (float)v0;
    uint16_t q[4] = {texel(p, u0, v0), texel(p, u0 + 1, v0), texel(p, u0, v0 + 1), texel(p, u0 + 1, v0 + 1)};
    float wt[4] = {(1 - au) * (1 - av), au * (1 - av), (1 - au) * av, au * av};
    float r = 0, g = 0, b = 0, sum = 0;
    for (int i = 0; i < 4; i++) {
        if (!q[i]) continue;
        r += wt[i] * (float)(q[i] & 31);
        g += wt[i] * (float)((q[i] >> 5) & 31);
        b += wt[i] * (float)((q[i] >> 10) & 31);
        sum += wt[i];
    }
    if (sum <= 0) { r = (float)(n & 31); g = (float)((n >> 5) & 31); b = (float)((n >> 10) & 31); sum = 1; }
    const float k = 255.0f / 31.0f;
    rgb[0] = r / sum * k; rgb[1] = g / sum * k; rgb[2] = b / sum * k;
    *stp = (n & 0x8000) != 0;
    return 1;
}

/* An HD texel or a smoothed game texel, lit, to the 15-bit picture. The
 * light is applied to the full 8-bit colour and the result dithered down
 * (finely, at the high resolution): rounded to the PS1's 5 bits first, a
 * smooth texture under the game's shading shows hard bands, dark contour
 * lines that crawl as the light on a surface changes. */
static inline void shade_hd(const RPrim *p, uint16_t *d, int r, int g, int b, float U, float V, const float *lo, int x, int y) {
    float c[3];
    int stp;
    if (!p->hd) {
        if (!smooth_texel(p, U, V, lo, c, &stp)) return;
    } else if (!hd_texel(p, U, V, lo, c, &stp)) return;
    /* blended where the game's own texel is (its STP bit): packs made for
     * Beetle PSX mostly leave alpha at 255, which would make a game's
     * see-through glows and glass solid */
    if (p->semi && !stp && (texel(p, (int)U, (int)V) & 0x8000)) stp = 1;
    if (!p->raw) {
        c[0] *= (float)r * (1.0f / 128); c[1] *= (float)g * (1.0f / 128); c[2] *= (float)b * (1.0f / 128);
    }
    uint16_t *lp = low_of(d);
    if (lp) {                                  /* 24-bit colour: as it is */
        put8(d, lp, (int)(c[0] + 0.5f), (int)(c[1] + 0.5f), (int)(c[2] + 0.5f), stp, p->semi && stp, p);
        return;
    }
    /* 0..255 to 0..31 with the dither's fraction added. A smoothed game
     * texel keeps the PS1's brightness on average: its shading truncates
     * (shade), so the dither is centred on 0 instead of 1/2. */
    float dt = ((float)bayer4[y & 3][x & 3] + 0.5f) * (1.0f / 16) - (p->hd ? 0.0f : 0.5f);
    int o[3];
    for (int i = 0; i < 3; i++) {
        int v = (int)(c[i] * (31.0f / 255.0f) + dt);
        o[i] = v < 0 ? 0 : v > 31 ? 31 : v;
    }
    put(d, (uint16_t)((stp ? 0x8000 : 0) | o[0] | (o[1] << 5) | (o[2] << 10)), p->semi && stp, p);
}

/* ------------------------------------------------------------ triangles */

typedef struct { double a, b, c; } Plane;      /* value = a*x + b*y + c */

static Plane plane(const double X[3], const double Y[3], double v0, double v1, double v2) {
    double d = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    Plane p;
    p.a = ((v1 - v0) * (Y[2] - Y[0]) - (v2 - v0) * (Y[1] - Y[0])) / d;
    p.b = ((v2 - v0) * (X[1] - X[0]) - (v1 - v0) * (X[2] - X[0])) / d;
    p.c = v0 - p.a * X[0] - p.b * Y[0];
    return p;
}

#define FIX 256                                    /* sub-pixel steps */

int g_zp_on, g_zp_x, g_zp_y; unsigned g_zp_f;

/* Everything about a triangle that doesn't depend on the pixel: raster_tri
 * runs it over its pixels, the GPU renderer (render_gpu.c) packs it. */
typedef struct {
    int64_t ex[3], ey[3], ec[3], bias[3], mbias[3];
    double margin;                               /* high-res pixels */
    int guarded, zuse, ztest, zwrite, gour, dith, smooth, persp, pcol;
    int umin, umax, vmin, vmax;
    uint32_t id;
    int x0, x1, y0, y1;                          /* clipped to the drawing area */
    Plane pr, pg, pb, pu, pv, pw, qw, qu, qv, qr, qg, qb;
    int32_t dr, dg, db, du, dv;
} TriSetup;

/* Two depth buffer ids (tri_setup) of one object: the same model if both
 * models are known, else the same texture page and CLUT. */
static inline int same_object(uint32_t a, uint32_t b) {
    if ((a >> 21) && (b >> 21) && a != 0xFFFFFFFFu && b != 0xFFFFFFFFu) return (a >> 21) == (b >> 21);
    return a == b;
}

static int tri_setup(const Target *t, const RPrim *p, TriSetup *q) {
    const RVtx *v = p->v;
    int s = t->scale;
    memset(q, 0, sizeof *q);
    /* hardware limit: polygons this large aren't drawn. Not for 3D drawn
     * in perspective at a higher resolution: there the game no longer
     * splits close polygons (rt.c), and a wall right in front of you is
     * one polygon far wider than the screen. */
    float minxf = fminf(v[0].x, fminf(v[1].x, v[2].x)), maxxf = fmaxf(v[0].x, fmaxf(v[1].x, v[2].x));
    float minyf = fminf(v[0].y, fminf(v[1].y, v[2].y)), maxyf = fmaxf(v[0].y, fmaxf(v[1].y, v[2].y));
    int whole = g_persp && p->zt == 2 && t->buf == hires && t->scale > 1;
    if ((maxxf - minxf >= 1024 || maxyf - minyf >= 512) && !whole) return 0;
    int64_t X[3], Y[3];
    double Xd[3], Yd[3];
    for (int i = 0; i < 3; i++) {
        Xd[i] = (double)v[i].x * s;
        Yd[i] = (double)v[i].y * s;
        X[i] = (int64_t)llround(Xd[i] * FIX);
        Y[i] = (int64_t)llround(Yd[i] * FIX);
    }
    int64_t area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (area == 0) return 0;
    int o[3] = {0, 1, 2};
    if (area < 0) { o[1] = 2; o[2] = 1; }
    /* edges: e opposite vertex o[e], from o[e+1] to o[e+2] */
    /* Opaque 3D at a higher resolution gets a hairline margin (half a
     * pixel) against the cracks between a model's own triangles. The gaps
     * between its parts, which the PS1's whole pixels close and a sharper
     * picture shows, are filled when the picture is shown (Close model
     * gaps, display.c), so outlines keep their size. */
    int zuse = p->zt && zbuf && t->buf == hires && s > 1;
    double margin = 0;                           /* high-res pixels */
    int guarded = 0;                             /* only over what lies well behind */
    /* Slivers (edge-on faces, the game's thin strips along edges) cover
     * next to nothing, at 1x or here: no margin, which would draw them as
     * lines along walls' and models' edges. */
    /* A sliver is thin for its length (under 1/4 PS1 pixel across its
     * longest edge); tiny triangles (distant models' faces) are not. */
    double cross2 = fabs((double)(v[1].x - v[0].x) * (v[2].y - v[0].y) - (double)(v[2].x - v[0].x) * (v[1].y - v[0].y));
    double len2 = 0;
    for (int i = 0; i < 3; i++) {
        double dx = v[(i + 1) % 3].x - v[i].x, dy = v[(i + 1) % 3].y - v[i].y;
        if (dx * dx + dy * dy > len2) len2 = dx * dx + dy * dy;
    }
    int sliver = cross2 * cross2 < 0.0625 * len2;     /* height = cross / length < 1/4 */
    if (zuse && !p->semi && !g_no_overlap && !sliver) {
        margin = 0.5;                            /* hairlines between a model's triangles */
        static int grow = -1;                    /* RT_GAP_GROW: the old Close model gaps, for comparisons */
        if (grow < 0) grow = getenv("RT_GAP_GROW") != NULL;
        if (g_gap_fill && grow) {                /* small triangles (distant models) drawn larger */
            double a = fabs((double)(v[1].x - v[0].x) * (v[2].y - v[0].y) - (double)(v[2].x - v[0].x) * (v[1].y - v[0].y)) / 2;
            double m = g_overlap * s;
            /* small in every direction: long thin slivers (the game's
             * highlight strips along walls' edges) stay hairlines, a margin
             * would draw them as lines across walls, floors and ceilings */
            double l = 0;
            for (int i = 0; i < 3; i++) {
                double dx = v[(i + 1) % 3].x - v[i].x, dy = v[(i + 1) % 3].y - v[i].y;
                if (dx * dx + dy * dy > l) l = dx * dx + dy * dy;
            }
            if (l > g_margin_len * g_margin_len) a = g_margin_area;
            if (a < g_margin_area / 2) margin = m;
            else if (a < g_margin_area) margin = 0.5 + (m - 0.5) * 2 * (1 - a / g_margin_area);   /* fading out */
            guarded = margin > 0.5;
        }
    }
    for (int e = 0; e < 3; e++) {
        int a = o[(e + 1) % 3], b = o[(e + 2) % 3];
        q->ex[e] = -(Y[b] - Y[a]);                /* d/dx */
        q->ey[e] = (X[b] - X[a]);                 /* d/dy */
        q->ec[e] = -(q->ex[e] * X[a] + q->ey[e] * Y[a]);
        int dy = (int)(Y[b] - Y[a]), dx = (int)(X[b] - X[a]);
        int top_left = (dy < 0) || (dy == 0 && dx > 0);
        q->bias[e] = top_left ? 0 : -1;
        q->mbias[e] = q->bias[e] + (int64_t)(margin * FIX * sqrt((double)q->ex[e] * q->ex[e] + (double)q->ey[e] * q->ey[e]));
    }
    int x0 = (int)floor(minxf * s), x1 = (int)ceil(maxxf * s);
    int y0 = (int)floor(minyf * s), y1 = (int)ceil(maxyf * s);
    if (margin > 0) { int m = (int)ceil(margin); x0 -= m; y0 -= m; x1 += m; y1 += m; }
    int cx0 = p->dx1 * s, cx1 = (p->dx2 + 1) * s - 1, cy0 = p->dy1 * s, cy1 = (p->dy2 + 1) * s - 1;
    if (x0 < cx0) x0 = cx0;
    if (x1 > cx1) x1 = cx1;
    if (y0 < cy0) y0 = cy0;
    if (y1 > cy1) y1 = cy1;
    if (x0 > x1 || y0 > y1) return 0;
    q->x0 = x0; q->x1 = x1; q->y0 = y0; q->y1 = y1;
    q->margin = margin;
    q->guarded = guarded;
    q->zuse = zuse;

    int gour = p->gouraud;
    if (gour) {
        q->pr = plane(Xd, Yd, v[0].r, v[1].r, v[2].r);
        q->pg = plane(Xd, Yd, v[0].g, v[1].g, v[2].g);
        q->pb = plane(Xd, Yd, v[0].b, v[1].b, v[2].b);
    }
    if (p->textured) {
        q->pu = plane(Xd, Yd, v[0].u, v[1].u, v[2].u);
        q->pv = plane(Xd, Yd, v[0].v, v[1].v, v[2].v);
    }
    q->gour = gour;
    q->dith = p->dither && s == 1 && (gour || (p->textured && !p->raw));
    /* Smooth textures: the game's own textures on 3D surfaces (every corner
     * from the GTE) in the high-resolution picture; 2D (menus, the HUD,
     * text) keeps its sharp texels. */
    q->smooth = g_smooth_tex && p->textured && p->zt && s > 1 && t->buf == hires;
    /* Texture coordinates stay off the far edge of the triangle's texture
     * area (its largest u and v), as on the PS1: it never draws a
     * triangle's right and bottom edge, so at 1x the texel column and row
     * there are hardly ever sampled, and models' textures often have
     * transparent ones there (holes, at a higher resolution). */
    int umin = 0, umax = 255, vmin = 0, vmax = 255;
    if (p->textured && s > 1 && !g_tex_edge) {
        umin = umax = v[0].u; vmin = vmax = v[0].v;
        for (int i = 1; i < 3; i++) {
            umin = v[i].u < umin ? v[i].u : umin; umax = v[i].u > umax ? v[i].u : umax;
            vmin = v[i].v < vmin ? v[i].v : vmin; vmax = v[i].v > vmax ? v[i].v : vmax;
        }
        if (umax > umin) umax--;
        if (vmax > vmin) vmax--;
    }
    q->umin = umin; q->umax = umax; q->vmin = vmin; q->vmax = vmax;
    /* depth: 1/z, linear on the screen */
    q->ztest = zuse && g_depth_buffer;               /* the Depth fix setting */
    /* Slivers (the game's thin highlight strips, edge on surfaces) don't
     * hide anything: what the game draws over them still covers them. */
    q->zwrite = zuse && !sliver;
    /* texture page and CLUT: a model part's own triangles share both;
     * decals (a dart board on a wall) and other objects drawn with the
     * room's page have their own CLUT */
    q->id = p->textured ? 1u + (uint32_t)(p->tx >> 6) + ((uint32_t)(p->ty >> 8) << 4) +
                          ((uint32_t)(p->clut_x >> 4) << 5) + ((uint32_t)p->clut_y << 11)
                        : 0xFFFFFFFFu;
    /* and the model it belongs to, when known (the root of its matrices'
     * chain, gte.c), in the top bits: a model's parts often have pages of
     * their own (a zombie's head and torso), and they are one object */
    if (p->textured && p->obj) q->id |= (((p->obj * 0x9E3779B1u) >> 21) | 1u) << 21;
    if (zuse) q->pw = plane(Xd, Yd, 1.0 / v[0].z, 1.0 / v[1].z, 1.0 / v[2].z);
    /* Perspective-correct textures (a 3D polygon, every corner's depth
     * known): the PS1 interpolates u and v straight across the screen,
     * which bends a texture's straight lines across big polygons and makes
     * them swim as the view moves; sharp HD textures show it most. */
    int pz = g_persp && p->zt == 2 && v[0].z > 0 && v[1].z > 0 && v[2].z > 0;
    int persp = pz && p->textured;
    /* The same for the colours at the corners (the game's lighting and
     * depth fog): spread straight across the screen, the light and shadow
     * on a wall slide over it as you walk, most where the game splits
     * close polygons into smaller ones. Direct3D (the PC version) spreads
     * them in perspective. */
    int pcol = pz && gour;
    if (persp || pcol) q->qw = plane(Xd, Yd, 1.0 / v[0].z, 1.0 / v[1].z, 1.0 / v[2].z);
    if (pcol) {
        q->qr = plane(Xd, Yd, v[0].r / v[0].z, v[1].r / v[1].z, v[2].r / v[2].z);
        q->qg = plane(Xd, Yd, v[0].g / v[0].z, v[1].g / v[1].z, v[2].g / v[2].z);
        q->qb = plane(Xd, Yd, v[0].b / v[0].z, v[1].b / v[1].z, v[2].b / v[2].z);
    }
    if (persp) {
        q->qu = plane(Xd, Yd, v[0].u / v[0].z, v[1].u / v[1].z, v[2].u / v[2].z);
        q->qv = plane(Xd, Yd, v[0].v / v[0].z, v[1].v / v[1].z, v[2].v / v[2].z);
        static int pl = -1;
        if (pl < 0) pl = getenv("RT_PERSP_LOG") != NULL;
        if (pl) {
            double zmin = fmin(v[0].z, fmin(v[1].z, v[2].z)), zmax = fmax(v[0].z, fmax(v[1].z, v[2].z));
            if (zmax > zmin * 1.5)
                rt_log("persp: tp %d,%d z %.0f %.0f %.0f xy (%.1f,%.1f) (%.1f,%.1f) (%.1f,%.1f) uv (%d,%d) (%d,%d) (%d,%d)", p->tx, p->ty, v[0].z, v[1].z, v[2].z,
                       v[0].x, v[0].y, v[1].x, v[1].y, v[2].x, v[2].y, v[0].u, v[0].v, v[1].u, v[1].v, v[2].u, v[2].v);
        }
    }
    q->persp = persp;
    q->pcol = pcol;
    const int32_t one = 65536;
    q->dr = (int32_t)(q->pr.a * one); q->dg = (int32_t)(q->pg.a * one); q->db = (int32_t)(q->pb.a * one);
    q->du = (int32_t)(q->pu.a * one); q->dv = (int32_t)(q->pv.a * one);
    return 1;
}

static void raster_tri(const Target *t, int band0, int band1, const RPrim *p) {
    TriSetup q;
    if (!tri_setup(t, p, &q)) return;
    const RVtx *v = p->v;
    int s = t->scale;
    int x0 = q.x0, x1 = q.x1, y0 = q.y0, y1 = q.y1;
    if (y0 < band0) y0 = band0;
    if (y1 >= band1) y1 = band1 - 1;
    if (y0 > y1) return;
    const int64_t *ex = q.ex, *ey = q.ey, *ec = q.ec, *bias = q.bias, *mbias = q.mbias;
    double margin = q.margin;
    int guarded = q.guarded, zuse = q.zuse, ztest = q.ztest, zwrite = q.zwrite, gour = q.gour, dith = q.dith;
    int smooth = q.smooth, persp = q.persp, pcol = q.pcol;
    int umin = q.umin, umax = q.umax, vmin = q.vmin, vmax = q.vmax;
    uint32_t id = q.id;
    Plane pr = q.pr, pg = q.pg, pb = q.pb, pu = q.pu, pv = q.pv, pw = q.pw;
    Plane qw = q.qw, qu = q.qu, qv = q.qv, qr = q.qr, qg = q.qg, qb = q.qb;
    const int32_t one = 65536;
    int32_t dr = q.dr, dg = q.dg, db = q.db, du = q.du, dv = q.dv;
    for (int y = y0; y <= y1; y++) {
        int64_t py = (int64_t)y * FIX;
        int64_t w[3];
        int64_t px0 = (int64_t)x0 * FIX;
        for (int e = 0; e < 3; e++) w[e] = ex[e] * px0 + ey[e] * py + ec[e];
        int64_t sx[3] = {ex[0] * FIX, ex[1] * FIX, ex[2] * FIX};
        /* attributes at (x0, y), +0.5 rounding bias for colours */
        int32_t r = 0, g = 0, b = 0, u = 0, vv = 0;
        if (gour) {
            r = (int32_t)((pr.a * x0 + pr.b * y + pr.c) * one) + one / 2;
            g = (int32_t)((pg.a * x0 + pg.b * y + pg.c) * one) + one / 2;
            b = (int32_t)((pb.a * x0 + pb.b * y + pb.c) * one) + one / 2;
        }
        if (p->textured) {
            u = (int32_t)((pu.a * x0 + pu.b * y + pu.c) * one) + one / 1024;
            vv = (int32_t)((pv.a * x0 + pv.b * y + pv.c) * one) + one / 1024;
        }
        uint16_t *row = t->buf + (size_t)y * t->stride;
        float *zrow = zuse ? zbuf + (size_t)y * t->stride : NULL;
        uint32_t *idrow = zuse ? zid + (size_t)y * t->stride : NULL;
        double wz = zuse ? pw.a * x0 + pw.b * y + pw.c : 0;
        double qwv = 0, quv = 0, qvv = 0, qrv = 0, qgv = 0, qbv = 0;
        /* sampled at the pixel's centre, as the affine values are */
        if (persp || pcol) qwv = qw.a * x0 + qw.b * y + qw.c;
        if (persp) { quv = qu.a * x0 + qu.b * y + qu.c; qvv = qv.a * x0 + qv.b * y + qv.c; }
        if (pcol) { qrv = qr.a * x0 + qr.b * y + qr.c; qgv = qg.a * x0 + qg.b * y + qg.c; qbv = qb.a * x0 + qb.b * y + qb.c; }
        for (int x = x0; x <= x1; x++, wz += pw.a, qwv += qw.a, quv += qu.a, qvv += qv.a, qrv += qr.a, qgv += qg.a, qbv += qb.a) {
            double iz = (persp || pcol) && qwv > 0 ? 1.0 / qwv : 0;
            if (persp && qwv > 0) {
                u = (int32_t)(quv * iz * one) + one / 1024;
                vv = (int32_t)(qvv * iz * one) + one / 1024;
            }
            int core = w[0] + bias[0] >= 0 && w[1] + bias[1] >= 0 && w[2] + bias[2] >= 0;
            if (g_margin_log && !core && margin > 0 && w[0] + mbias[0] >= 0 && w[1] + mbias[1] >= 0 && w[2] + mbias[2] >= 0) {
                float r = zrow[x] / (float)wz;
                int bin = zrow[x] == 0 ? 0 : r < 0.5f ? 1 : r < 0.9f ? 2 : r < 0.98f ? 3 : r < 1.02f ? 4 : r < 1.5f ? 5 : 6;
                __atomic_add_fetch(&g_margin_hist[bin], 1, __ATOMIC_RELAXED);
            }
            if (!core && margin > 0 && w[0] + mbias[0] >= 0 && w[1] + mbias[1] >= 0 && w[2] + mbias[2] >= 0 &&
                (!guarded || g_margin_any || zrow[x] == 0 || 1.0f / zrow[x] - 1.0f / (float)wz > g_margin_depth))
                core = 2;                                 /* margin, over something well behind */
            if (g_zp_on && x == g_zp_x && (y == g_zp_y || y == g_zp_y + 256 * s) && g_frame >= g_zp_f)
                rt_log("zprobe: [pzt %d zb %d hr %d s %d] tp %d,%d clut %d,%d semi %d core %d zt %d z %.1f zs %.1f zwrite %d v (%.1f,%.1f %.0f) (%.1f,%.1f %.0f) (%.1f,%.1f %.0f)", p->zt, zbuf != NULL, t->buf == hires, s, p->tx, p->ty, p->clut_x, p->clut_y, p->semi, core, ztest,
                       zuse ? 1.0 / wz : 0.0, zuse && zrow[x] ? 1.0 / zrow[x] : 0.0, zwrite, v[0].x, v[0].y, v[0].z, v[1].x, v[1].y, v[1].z, v[2].x, v[2].y, v[2].z);
            if (core) {
                int tu = u >> 16, tv = vv >> 16;
                tu = tu < umin ? umin : tu > umax ? umax : tu;
                tv = tv < vmin ? vmin : tv > vmax ? vmax : tv;
                if (zwrite && !ztest && core == 1 && !p->semi && (!p->textured || texel(p, tu, tv))) { zrow[x] = (float)wz; idrow[x] = id; }
                if (ztest) {
                    /* a little behind what's there: hidden. Only a little:
                     * that's a model's own back showing through its front.
                     * Touching faces and decals (a sliver behind) keep the
                     * PS1's order, and so does anything far behind, which
                     * the game drew on top on purpose (reflections in
                     * water are models under the floor). */
                    float zs = zrow[x];
                    if ((float)wz > zs * 0.88f && (float)wz < zs) {
                        float behind = 1.0f / (float)wz - 1.0f / zs;   /* in GTE depth units */
                        float tol = g_depth_rel / zs;
                        if (tol < g_depth_tol) tol = g_depth_tol;
                        if (behind > tol && (same_object(idrow[x], id) || g_depth_any)) goto next;
                        /* Another object's surface well in front (the
                         * PS1 sorts whole polygons, so a stair rail can
                         * come after the ceiling it's behind): hidden
                         * too. Not translucent things (shadows, glows)
                         * or decals a little into their wall. */
                        if (g_cross_tol > 0 && !p->semi && behind > g_cross_tol && behind > tol) goto next;
                    } else if (g_far_page && (float)wz <= zs * 0.88f && !p->semi && id != 0xFFFFFFFFu &&
                               idrow[x] != 0xFFFFFFFFu && ((idrow[x] ^ id) & 0x1F) == 0) {
                        /* Far behind a surface on the same texture page:
                         * the room's own walls and ceiling, with more of
                         * the room behind them (a stair rail on the floor
                         * above). Models under the floor (reflections)
                         * have their own pages and are still drawn. */
                        goto next;
                    }
                    if (zwrite && core == 1 && !p->semi && (!p->textured || texel(p, tu, tv))) { zrow[x] = (float)wz; idrow[x] = id; }
                }
                int cr = v[0].r, cg = v[0].g, cb = v[0].b;
                if (pcol && qwv > 0) {
                    cr = (int)(qrv * iz + 0.5); cg = (int)(qgv * iz + 0.5); cb = (int)(qbv * iz + 0.5);
                    cr = cr < 0 ? 0 : cr > 255 ? 255 : cr;
                    cg = cg < 0 ? 0 : cg > 255 ? 255 : cg;
                    cb = cb < 0 ? 0 : cb > 255 ? 255 : cb;
                } else if (gour) {
                    cr = r >> 16; cg = g >> 16; cb = b >> 16;
                    cr = cr < 0 ? 0 : cr > 255 ? 255 : cr;
                    cg = cg < 0 ? 0 : cg > 255 ? 255 : cg;
                    cb = cb < 0 ? 0 : cb > 255 ? 255 : cb;
                }
                if (g_sliver_mark && zuse && !zwrite) { row[x] = core == 2 ? 0x03E0 : 0x001F; goto next; }
                if (p->hd || smooth) {
                    float U = (float)u * (1.0f / 65536), V = (float)vv * (1.0f / 65536);
                    float ulo = (float)umin, uhi = (float)umax + 0.999f, vlo = (float)vmin, vhi = (float)vmax + 0.999f;
                    U = U < ulo ? ulo : U > uhi ? uhi : U;
                    V = V < vlo ? vlo : V > vhi ? vhi : V;
                    const float lo[4] = {ulo, (float)umax + 1, vlo, (float)vmax + 1};
                    shade_hd(p, row + x, cr, cg, cb, U, V, lo, x, y);
                } else
                    shade(p, row + x, x, y, cr, cg, cb, tu, tv, dith);
            }
        next:
            w[0] += sx[0]; w[1] += sx[1]; w[2] += sx[2];
            r += dr; g += dg; b += db; u += du; vv += dv;
        }
    }
}

/* ------------------------------------------------------------ rects, lines */

static int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }

static void raster_rect(const Target *t, int band0, int band1, const RPrim *p) {
    int s = t->scale;
    int nx = (int)p->v[0].x, ny = (int)p->v[0].y;
    int x0 = nx * s, y0 = ny * s, x1 = (nx + p->w) * s - 1, y1 = (ny + p->h) * s - 1;
    int cx0 = p->dx1 * s, cx1 = (p->dx2 + 1) * s - 1, cy0 = p->dy1 * s, cy1 = (p->dy2 + 1) * s - 1;
    int ys = y0 > cy0 ? y0 : cy0, ye = y1 < cy1 ? y1 : cy1;
    int xs = x0 > cx0 ? x0 : cx0, xe = x1 < cx1 ? x1 : cx1;
    if (ys < band0) ys = band0;
    if (ye >= band1) ye = band1 - 1;
    int r = p->v[0].r, g = p->v[0].g, b = p->v[0].b;
    /* squeezed (widescreen 2D): texel columns spread evenly over the
     * squeezed width, every one of them shown at a higher resolution */
    double sq0 = 0, sqk = 0;
    if (p->sqx1 > p->sqx0) {
        sq0 = p->sqx0 * s;
        sqk = p->w / ((p->sqx1 - p->sqx0) * s);
        xs = (int)ceil(sq0 - 0.5);
        xe = (int)ceil(p->sqx1 * s - 0.5) - 1;
        if (xs < cx0) xs = cx0;
        if (xe > cx1) xe = cx1;
    }
    for (int y = ys; y <= ye; y++) {
        int j = floordiv(y - y0, s);
        uint16_t *row = t->buf + (size_t)y * t->stride;
        for (int x = xs; x <= xe; x++) {
            int i = sqk > 0 ? (int)floor((x + 0.5 - sq0) * sqk) : floordiv(x - x0, s);
            if (sqk > 0 && (i < 0 || i >= p->w)) continue;
            int u = p->flipx ? p->v[0].u - i : p->v[0].u + i;
            int v = p->flipy ? p->v[0].v - j : p->v[0].v + j;
            if (p->hd) {
                float fi = sqk > 0 ? (float)((x + 0.5 - sq0) * sqk) : (float)(x - x0 + 0.5f) / (float)s;
                float fj = (float)(y - y0 + 0.5f) / (float)s;
                fi -= (float)i; fj -= (float)j;                          /* within the texel */
                int u0 = p->v[0].u, v0 = p->v[0].v;
                const float lo[4] = {(float)(p->flipx ? u0 - p->w + 1 : u0), (float)(p->flipx ? u0 + 1 : u0 + p->w),
                                     (float)(p->flipy ? v0 - p->h + 1 : v0), (float)(p->flipy ? v0 + 1 : v0 + p->h)};
                shade_hd(p, row + x, r, g, b, (float)u + (p->flipx ? 1 - fi : fi), (float)v + (p->flipy ? 1 - fj : fj), lo, x, y);
            } else
                shade(p, row + x, x, y, r, g, b, u, v, 0);
        }
    }
}

static void raster_line(const Target *t, int band0, int band1, const RPrim *p) {
    int s = t->scale;
    int ax = (int)lroundf(p->v[0].x), ay = (int)lroundf(p->v[0].y);
    int bx = (int)lroundf(p->v[1].x), by = (int)lroundf(p->v[1].y);
    int dx = bx - ax, dy = by - ay;
    if (abs(dx) >= 1024 || abs(dy) >= 512) return;
    int n = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
    for (int i = 0; i <= n; i++) {
        int x = n ? ax + (dx * i + (dx >= 0 ? n / 2 : -n / 2)) / n : ax;
        int y = n ? ay + (dy * i + (dy >= 0 ? n / 2 : -n / 2)) / n : ay;
        if (x < p->dx1 || x > p->dx2 || y < p->dy1 || y > p->dy2) continue;
        int r = p->v[0].r, g = p->v[0].g, b = p->v[0].b;
        if (p->gouraud && n) {
            r += (p->v[1].r - r) * i / n;
            g += (p->v[1].g - g) * i / n;
            b += (p->v[1].b - b) * i / n;
        }
        for (int yy = y * s; yy < (y + 1) * s; yy++) {
            if (yy < band0 || yy >= band1) continue;
            for (int xx = x * s; xx < (x + 1) * s; xx++)
                shade(p, t->buf + (size_t)yy * t->stride + xx, xx, yy, r, g, b, 0, 0, p->dither && s == 1 && p->gouraud);
        }
    }
}

static void raster(const Target *t, int band0, int band1, const RPrim *p) {
    switch (p->kind) {
    case RP_TRI: raster_tri(t, band0, band1, p); break;
    case RP_RECT: raster_rect(t, band0, band1, p); break;
    case RP_LINE: raster_line(t, band0, band1, p); break;
    }
}

/* ------------------------------------------------------------ queue */

enum { OP_PRIM, OP_FILL, OP_UPLOAD, OP_ZCLEAR };
typedef struct {
    int op;
    RPrim prim;
    int x, y, w, h;
    uint16_t color;
    uint16_t *pixels;          /* upload snapshot */
} Op;

static Op *queue;
static int nq, capq;
static uint8_t tex_tiles[8][16];                /* 64x64 VRAM tiles read by queued textures */
static uint8_t drawn_tiles[8][16];              /* 64x64 VRAM tiles written by queued ops */

static void tiles_mark(int x, int y, int w, int h) {
    for (int ty = y >> 6; ty <= ((y + h - 1) >> 6); ty++)
        for (int tx = x >> 6; tx <= ((x + w - 1) >> 6); tx++)
            tex_tiles[ty & 7][tx & 15] = 1;
}

static int tiles_hit(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return 0;
    for (int ty = y >> 6; ty <= ((y + h - 1) >> 6); ty++)
        for (int tx = x >> 6; tx <= ((x + w - 1) >> 6); tx++)
            if (tex_tiles[ty & 7][tx & 15]) return 1;
    return 0;
}

/* Queued ops run in horizontal bands, one thread each, so a texture that a
 * queued op draws into may be read by another band's thread before it's
 * drawn: a textured primitive reading such tiles waits for the queue. */
static void drawn_mark(int x, int y, int w, int h) {
    if (w <= 0 || h <= 0) return;
    for (int ty = y >> 6; ty <= ((y + h - 1) >> 6); ty++)
        for (int tx = x >> 6; tx <= ((x + w - 1) >> 6); tx++)
            drawn_tiles[ty & 7][tx & 15] = 1;
}

static int drawn_hit(int x, int y, int w, int h) {
    for (int ty = y >> 6; ty <= ((y + h - 1) >> 6); ty++)
        for (int tx = x >> 6; tx <= ((x + w - 1) >> 6); tx++)
            if (drawn_tiles[ty & 7][tx & 15]) return 1;
    return 0;
}

static Op *push_op(void) {
    if (nq == capq) {
        capq = capq ? capq * 2 : 4096;
        queue = realloc(queue, (size_t)capq * sizeof *queue);
    }
    return &queue[nq++];
}

static void run_ops(int band0, int band1) {
    for (int i = 0; i < nq; i++) {
        Op *o = &queue[i];
        switch (o->op) {
        case OP_PRIM: raster(&hires_t, band0, band1, &o->prim); break;
        case OP_FILL:
            for (int y = o->y * S; y < (o->y + o->h) * S; y++) {
                int yy = y % (512 * S);
                if (yy < band0 || yy >= band1) continue;
                for (int x = o->x * S; x < (o->x + o->w) * S; x++) {
                    size_t i = (size_t)yy * hires_t.stride + (x % (1024 * S));
                    hires[i] = g_fill_mark && o->w >= 256 ? 0x7C1F : o->color;
                    low[i] = 0;
                    zbuf[i] = 0;
                    bare[i] = o->w >= 256 && o->h >= 128;
                }
            }
            break;
        case OP_ZCLEAR:
            for (int y = o->y * S; y < (o->y + o->h) * S; y++) {
                int yy = y % (512 * S);
                if (yy < band0 || yy >= band1) continue;
                for (int x = o->x * S; x < (o->x + o->w) * S; x++) zbuf[(size_t)yy * hires_t.stride + (x % (1024 * S))] = 0;
            }
            break;
        case OP_UPLOAD:
            for (int y = o->y * S; y < (o->y + o->h) * S; y++) {
                int yy = y % (512 * S);
                if (yy < band0 || yy >= band1) continue;
                const uint16_t *src = o->pixels + (size_t)((y - o->y * S) / S) * o->w;
                for (int x = 0; x < o->w * S; x++) {
                    size_t i = (size_t)yy * hires_t.stride + ((o->x * S + x) % (1024 * S));
                    hires[i] = src[x / S];
                    low[i] = 0;
                    bare[i] = 0;
                    zbuf[i] = -1;
                }
            }
            break;
        }
    }
}

/* ------------------------------------------------------------ the graphics card */

/* Draw with the graphics card (setting): the queue runs there instead of
 * on the worker threads (render_gpu.c). The arrays above then hold what was
 * last read back from it, for the picture shown. */
int g_gpu = 0;
static int gpu_on, gpu_dirty;
static uint32_t *gops;
static size_t gops_cap;
static uint16_t *gpool;
static size_t gpool_n, gpool_cap;
static int want_x, want_y, want_w, want_h;     /* the area the picture shown needs (high-res), or none */

static void put_d(uint32_t *w, int at, double d) { memcpy(&w[at], &d, 8); }

static uint32_t *gop_new(int *n) {
    if ((size_t)(*n + 1) * GOP_WORDS > gops_cap) {
        gops_cap = gops_cap ? gops_cap * 2 : (size_t)4096 * GOP_WORDS;
        gops = realloc(gops, gops_cap * 4);
    }
    uint32_t *w = gops + (size_t)(*n)++ * GOP_WORDS;
    memset(w, 0, GOP_WORDS * 4);
    return w;
}

static void gop_prim_common(uint32_t *w, const RPrim *p) {
    w[GO_FLAGS] = (p->textured ? GF_TEX : 0) | (p->raw ? GF_RAW : 0) | (p->semi ? GF_SEMI : 0) |
                  ((uint32_t)(p->abr & 3) << GF_ABR_SH) | ((uint32_t)(p->depth & 3) << GF_DEPTH_SH) |
                  (p->mask_set ? GF_MSET : 0) | (p->mask_check ? GF_MCHK : 0) | (p->zt ? GF_ZT : 0);
    w[GO_TEX] = (uint32_t)(p->tx & 0xFFFF) | ((uint32_t)p->ty << 16);
    w[GO_CLUT] = (uint32_t)(p->clut_x & 0xFFFF) | ((uint32_t)p->clut_y << 16);
    w[GO_TWIN] = (uint32_t)(p->twmx & 0xFF) | ((uint32_t)(p->twmy & 0xFF) << 8) | ((uint32_t)(p->twox & 0xFF) << 16) |
                 ((uint32_t)(p->twoy & 0xFF) << 24);
    w[GO_COLOR] = (uint32_t)(p->v[0].r & 0xFF) | ((uint32_t)(p->v[0].g & 0xFF) << 8) | ((uint32_t)(p->v[0].b & 0xFF) << 16);
}

/* A rectangle (the area raster_rect draws) as an op. */
static void gop_rect(int *n, const RPrim *p, int nx, int ny, int pw, int ph, int r, int g, int b) {
    int s = S;
    int x0 = nx * s, y0 = ny * s, x1 = (nx + pw) * s - 1, y1 = (ny + ph) * s - 1;
    int cx0 = p->dx1 * s, cx1 = (p->dx2 + 1) * s - 1, cy0 = p->dy1 * s, cy1 = (p->dy2 + 1) * s - 1;
    int ys = y0 > cy0 ? y0 : cy0, ye = y1 < cy1 ? y1 : cy1;
    int xs = x0 > cx0 ? x0 : cx0, xe = x1 < cx1 ? x1 : cx1;
    double sq0 = 0, sqk = 0;
    if (p->sqx1 > p->sqx0) {
        sq0 = p->sqx0 * s;
        sqk = pw / ((p->sqx1 - p->sqx0) * s);
        xs = (int)ceil(sq0 - 0.5);
        xe = (int)ceil(p->sqx1 * s - 0.5) - 1;
        if (xs < cx0) xs = cx0;
        if (xe > cx1) xe = cx1;
    }
    if (xs > xe || ys > ye) return;
    uint32_t *w = gop_new(n);
    w[GO_KIND] = GK_RECT;
    gop_prim_common(w, p);
    w[GO_COLOR] = (uint32_t)(r & 0xFF) | ((uint32_t)(g & 0xFF) << 8) | ((uint32_t)(b & 0xFF) << 16);
    w[GO_FLAGS] |= (p->flipx ? GF_FLIPX : 0) | (p->flipy ? GF_FLIPY : 0) | (sqk > 0 ? GF_SQ : 0);
    w[GO_X0] = (uint32_t)xs; w[GO_X1] = (uint32_t)xe; w[GO_Y0] = (uint32_t)ys; w[GO_Y1] = (uint32_t)ye;
    w[GO_RX] = (uint32_t)nx; w[GO_RY] = (uint32_t)ny; w[GO_RW] = (uint32_t)pw; w[GO_RH] = (uint32_t)ph;
    w[GO_U0V0] = (uint32_t)(p->v[0].u & 0xFFFF) | ((uint32_t)(p->v[0].v & 0xFFFF) << 16);
    put_d(w, GO_SQ0, sq0);
    put_d(w, GO_SQK, sqk);
}

/* The queue as the card's ops. */
static int gpu_pack(void) {
    int n = 0;
    gpool_n = 0;
    for (int i = 0; i < nq; i++) {
        Op *o = &queue[i];
        if (o->op == OP_PRIM && o->prim.kind == RP_TRI) {
            const RPrim *p = &o->prim;
            TriSetup q;
            if (!tri_setup(&hires_t, p, &q)) continue;
            uint32_t *w = gop_new(&n);
            w[GO_KIND] = GK_TRI;
            gop_prim_common(w, p);
            w[GO_FLAGS] |= (q.gour ? GF_GOUR : 0) | (q.smooth ? GF_SMOOTH : 0) | (q.persp ? GF_PERSP : 0) |
                           (q.pcol ? GF_PCOL : 0) | (q.zuse ? GF_ZUSE : 0) | (q.ztest ? GF_ZTEST : 0) |
                           (q.zwrite ? GF_ZWRITE : 0) | (q.guarded ? GF_GUARD : 0) | (q.margin > 0 ? GF_MARGIN : 0);
            w[GO_X0] = (uint32_t)q.x0; w[GO_X1] = (uint32_t)q.x1; w[GO_Y0] = (uint32_t)q.y0; w[GO_Y1] = (uint32_t)q.y1;
            w[GO_UV] = (uint32_t)(q.umin & 0xFF) | ((uint32_t)(q.umax & 0xFF) << 8) | ((uint32_t)(q.vmin & 0xFF) << 16) |
                       ((uint32_t)(q.vmax & 0xFF) << 24);
            w[GO_ID] = q.id;
            w[GO_DU] = (uint32_t)q.du; w[GO_DV] = (uint32_t)q.dv;
            w[GO_DR] = (uint32_t)q.dr; w[GO_DG] = (uint32_t)q.dg; w[GO_DB] = (uint32_t)q.db;
            for (int e = 0; e < 3; e++) {
                w[GO_BIAS + e] = (uint32_t)(int32_t)q.bias[e];
                put_d(w, GO_EX + 2 * e, (double)q.ex[e]);
                put_d(w, GO_EY + 2 * e, (double)q.ey[e]);
                put_d(w, GO_EC + 2 * e, (double)q.ec[e]);
                put_d(w, GO_MB + 2 * e, (double)q.mbias[e]);
            }
            const Plane *pl[12] = {&q.pr, &q.pg, &q.pb, &q.pu, &q.pv, &q.pw, &q.qw, &q.qu, &q.qv, &q.qr, &q.qg, &q.qb};
            for (int k = 0; k < 12; k++) {
                put_d(w, GO_PL + k * 6, pl[k]->a);
                put_d(w, GO_PL + k * 6 + 2, pl[k]->b);
                put_d(w, GO_PL + k * 6 + 4, pl[k]->c);
            }
        } else if (o->op == OP_PRIM && o->prim.kind == RP_RECT) {
            const RPrim *p = &o->prim;
            gop_rect(&n, p, (int)p->v[0].x, (int)p->v[0].y, p->w, p->h, p->v[0].r, p->v[0].g, p->v[0].b);
        } else if (o->op == OP_PRIM) {               /* a line: its PS1 pixels, as raster_line steps them */
            const RPrim *p = &o->prim;
            RPrim q = *p;
            q.sqx0 = q.sqx1 = 0;
            q.flipx = q.flipy = 0;
            q.v[0].u = q.v[0].v = 0;
            int ax = (int)lroundf(p->v[0].x), ay = (int)lroundf(p->v[0].y);
            int bx = (int)lroundf(p->v[1].x), by = (int)lroundf(p->v[1].y);
            int dx = bx - ax, dy = by - ay;
            if (abs(dx) >= 1024 || abs(dy) >= 512) continue;
            int m = abs(dx) > abs(dy) ? abs(dx) : abs(dy);
            for (int k = 0; k <= m; k++) {
                int x = m ? ax + (dx * k + (dx >= 0 ? m / 2 : -m / 2)) / m : ax;
                int y = m ? ay + (dy * k + (dy >= 0 ? m / 2 : -m / 2)) / m : ay;
                if (x < p->dx1 || x > p->dx2 || y < p->dy1 || y > p->dy2) continue;
                int r = p->v[0].r, g = p->v[0].g, b = p->v[0].b;
                if (p->gouraud && m) {
                    r += (p->v[1].r - r) * k / m;
                    g += (p->v[1].g - g) * k / m;
                    b += (p->v[1].b - b) * k / m;
                }
                gop_rect(&n, &q, x, y, 1, 1, r, g, b);
            }
        } else {
            uint32_t *w = gop_new(&n);
            w[GO_RX] = (uint32_t)o->x; w[GO_RY] = (uint32_t)o->y; w[GO_RW] = (uint32_t)o->w; w[GO_RH] = (uint32_t)o->h;
            if (o->op == OP_FILL) {
                w[GO_KIND] = GK_FILL;
                w[GO_FILLC] = o->color;
                w[GO_FLAGS] = o->w >= 256 && o->h >= 128 ? GF_BIGFILL : 0;
            } else if (o->op == OP_ZCLEAR)
                w[GO_KIND] = GK_ZCLEAR;
            else {
                w[GO_KIND] = GK_UPLOAD;
                size_t k = (size_t)o->w * o->h;
                if (gpool_n + k > gpool_cap) {
                    gpool_cap = (gpool_n + k) * 2;
                    gpool = realloc(gpool, gpool_cap * sizeof *gpool);
                }
                memcpy(gpool + gpool_n, o->pixels, k * sizeof *gpool);
                w[GO_POOL] = (uint32_t)gpool_n;
                gpool_n += k;
            }
        }
    }
    return n;
}

static void gpu_run(void) {
    int n = gpu_pack();
    GpuParams gp = {g_depth_tol, g_cross_tol, g_margin_depth, g_depth_rel, g_true_color, g_far_page, g_depth_any, g_margin_any};
    rgpu_draw(gops, n, gpool, gpool_n, &g_vram[0][0], &gp);
    gpu_dirty = 1;
}

/* The arrays up to date with the card, where the picture shown needs them. */
static void gpu_fetch(void) {
    if (!gpu_on || !gpu_dirty) return;
    if (want_w > 0) rgpu_read(want_x, want_y, want_w, want_h, hires, low, bare, zbuf);
    else rgpu_read(0, 0, 1024 * S, 512 * S, hires, low, bare, zbuf);
    gpu_dirty = 0;
    want_w = 0;
}

void render_want(int x, int y, int w, int h) { want_x = x; want_y = y; want_w = w; want_h = h; }

int render_on_gpu(void) { return gpu_on; }

/* The graphics card setting changed while running: the high-resolution
 * picture moves over to the card or back. 0: drawing stays as it was (no
 * usable card). */
int render_set_gpu(int on) {
    g_gpu = on;
    if (!!on == gpu_on || S == 1) return 1;
    render_sync();
    if (on) {
        if (!rgpu_init(S)) return 0;
        if (zid) memset(zid, 0, hires_n * sizeof *zid);
        rgpu_write(hires, low, bare, zbuf, zid);
        gpu_on = 1;
        gpu_dirty = 0;
    } else {
        gpu_dirty = 1;
        want_w = 0;
        gpu_fetch();                             /* the whole picture back, for the CPU to go on from */
        gpu_on = 0;
        if (S > 4) render_set_scale(4);
    }
    rt_log("renderer: now drawing with the %s", gpu_on ? "graphics card" : "processor");
    return 1;
}

/* ------------------------------------------------------------ workers */

#define MAX_WORKERS 16
static int nworkers, workers_started;
static int band_of[MAX_WORKERS + 1][2];

#ifdef _WIN32
static HANDLE w_go[MAX_WORKERS], w_done[MAX_WORKERS];
static DWORD WINAPI worker(LPVOID arg) {
    int i = (int)(intptr_t)arg;
    for (;;) {
        WaitForSingleObject(w_go[i], INFINITE);
        run_ops(band_of[i + 1][0], band_of[i + 1][1]);
        ReleaseSemaphore(w_done[i], 1, NULL);
    }
    return 0;
}
static void start_workers(void) {
    for (int i = 0; i < nworkers; i++) {
        w_go[i] = CreateSemaphore(NULL, 0, 1, NULL);
        w_done[i] = CreateSemaphore(NULL, 0, 1, NULL);
        CreateThread(NULL, 0, worker, (LPVOID)(intptr_t)i, 0, NULL);
    }
}
static void go_workers(void) { for (int i = 0; i < nworkers; i++) ReleaseSemaphore(w_go[i], 1, NULL); }
static void wait_workers(void) { for (int i = 0; i < nworkers; i++) WaitForSingleObject(w_done[i], INFINITE); }
#else
static sem_t w_go[MAX_WORKERS], w_done[MAX_WORKERS];
static void *worker(void *arg) {
    int i = (int)(intptr_t)arg;
    for (;;) {
        sem_wait(&w_go[i]);
        run_ops(band_of[i + 1][0], band_of[i + 1][1]);
        sem_post(&w_done[i]);
    }
    return NULL;
}
static void start_workers(void) {
    for (int i = 0; i < nworkers; i++) {
        sem_init(&w_go[i], 0, 0);
        sem_init(&w_done[i], 0, 0);
        pthread_t th;
        pthread_create(&th, NULL, worker, (void *)(intptr_t)i);
        pthread_detach(th);
    }
}
static void go_workers(void) { for (int i = 0; i < nworkers; i++) sem_post(&w_go[i]); }
static void wait_workers(void) { for (int i = 0; i < nworkers; i++) sem_wait(&w_done[i]); }
#endif

unsigned g_sync_count, g_sync_ops;
void render_sync(void) {
    if (!nq) return;
    g_sync_count++;
    g_sync_ops += (unsigned)nq;
    static int opstat = -1;
    if (opstat < 0) opstat = getenv("RT_OPSTAT") != NULL;
    if (opstat) {
        unsigned k[8] = {0}; double area[8] = {0};
        for (int i = 0; i < nq; i++) {
            Op *o = &queue[i];
            int t = o->op == OP_PRIM ? (o->prim.kind == RP_TRI ? (o->prim.semi ? 1 : 0) : o->prim.kind == RP_RECT ? 2 : 3) : o->op == OP_FILL ? 4 : o->op == OP_UPLOAD ? 5 : 6;
            k[t]++;
            if (o->op == OP_PRIM && o->prim.kind == RP_TRI) {
                const RVtx *v = o->prim.v;
                area[t] += fabs((v[1].x - v[0].x) * (v[2].y - v[0].y) - (v[2].x - v[0].x) * (v[1].y - v[0].y)) / 2;
            } else if (o->op == OP_PRIM) area[t] += (double)o->prim.w * o->prim.h;
            else area[t] += (double)o->w * o->h;
        }
        rt_log("ops: tri %u (%.0f px) semi-tri %u (%.0f) rect %u (%.0f) line %u fill %u (%.0f) upload %u (%.0f) other %u",
               k[0], area[0], k[1], area[1], k[2], area[2], k[3], k[4], area[4], k[5], area[5], k[6]);
    }
    if (gpu_on) gpu_run();
    else {
        go_workers();
        run_ops(band_of[0][0], band_of[0][1]);
        wait_workers();
    }
    for (int i = 0; i < nq; i++)
        if (queue[i].op == OP_UPLOAD) free(queue[i].pixels);
    nq = 0;
    memset(tex_tiles, 0, sizeof tex_tiles);
    memset(drawn_tiles, 0, sizeof drawn_tiles);
    texpack_frame();
}

/* ------------------------------------------------------------ API */

static void set_bands(void) {
    int bands = nworkers + 1, h = 512 * S;
    for (int i = 0; i < bands; i++) {
        band_of[i][0] = h * i / bands;
        band_of[i][1] = h * (i + 1) / bands;
    }
}

void render_init(int scale, int threads) {
    S = scale < 1 ? 1 : scale > 8 ? 8 : scale;
    if (getenv("RT_GPU")) g_gpu = atoi(getenv("RT_GPU"));
    if (S > 1 && g_gpu) gpu_on = rgpu_init(S);
    if (!gpu_on && S > 4) S = 4;                 /* above 4x only on the card */
    g_tex_edge = getenv("RT_TEX_EDGE") != NULL;      /* for comparing */
    g_depth_any = getenv("RT_DEPTH_ANY") != NULL;
    g_fill_mark = getenv("RT_FILL_MARK") != NULL;
    g_sliver_mark = getenv("RT_SLIVER_MARK") != NULL;
    if (getenv("RT_MARGIN_LEN")) g_margin_len = atof(getenv("RT_MARGIN_LEN"));
    { const char *e = getenv("RT_ZPROBE"); if (e) g_zp_on = sscanf(e, "%d,%d,%u", &g_zp_x, &g_zp_y, &g_zp_f) == 3; }
    g_no_overlap = getenv("RT_NO_OVERLAP") != NULL;
    if (getenv("RT_OVERLAP")) g_overlap = atof(getenv("RT_OVERLAP"));
    g_margin_any = getenv("RT_MARGIN_ANY") != NULL;
    if (getenv("RT_NO_GAP_FILL")) g_gap_fill = 0;
    if (getenv("RT_DEPTH_TOL")) g_depth_tol = (float)atof(getenv("RT_DEPTH_TOL"));
    if (getenv("RT_DEPTH_REL")) g_depth_rel = (float)atof(getenv("RT_DEPTH_REL"));
    if (getenv("RT_FAR_PAGE")) g_far_page = atoi(getenv("RT_FAR_PAGE"));
    if (getenv("RT_CROSS_TOL")) g_cross_tol = (float)atof(getenv("RT_CROSS_TOL"));
    g_margin_log = getenv("RT_MARGIN_LOG") != NULL;
    if (getenv("RT_MARGIN_DEPTH")) g_margin_depth = (float)atof(getenv("RT_MARGIN_DEPTH"));
    if (getenv("RT_MARGIN_AREA")) g_margin_area = atof(getenv("RT_MARGIN_AREA"));
    native_t = (Target){&g_vram[0][0], 1024, 1, 512};
    if (S > 1) {
        hires = calloc((size_t)1024 * S * 512 * S, sizeof *hires);
        zbuf = calloc((size_t)1024 * S * 512 * S, sizeof *zbuf);
        zid = calloc((size_t)1024 * S * 512 * S, sizeof *zid);
        bare = calloc((size_t)1024 * S * 512 * S, 1);
        low = calloc((size_t)1024 * S * 512 * S, sizeof *low);
        hires_n = (size_t)1024 * S * 512 * S;
        hires_t = (Target){hires, 1024 * S, S, 512 * S};
    }
    nworkers = threads - 1;
    if (nworkers < 0) nworkers = 0;
    if (nworkers > MAX_WORKERS) nworkers = MAX_WORKERS;
    set_bands();
    if (S > 1) { start_workers(); workers_started = 1; }
    rt_log("renderer: %dx internal resolution, %s", S, gpu_on ? "graphics card" : "processor");
    if (!gpu_on) rt_log("renderer: %d threads", nworkers + 1);
}

/* Change the internal resolution while running: the new high-res VRAM
 * starts as a blown-up copy of the PS1's VRAM (textures stay intact; the
 * picture sharpens from the next frame on). */
static void upscale_from_native(void) {
    int W = 1024 * S;
    for (int y = 0; y < 512 * S; y++)
        for (int x = 0; x < W; x++) hires[(size_t)y * W + x] = g_vram[y / S][x / S];
}

/* After loading a save: the high-res VRAM starts again from the PS1's. */
void render_reload(void) {
    render_sync();
    texpack_reload();
    if (S > 1) upscale_from_native();
    if (zbuf) memset(zbuf, 0, (size_t)1024 * S * 512 * S * sizeof *zbuf);
    if (bare) memset(bare, 0, hires_n);
    if (low) memset(low, 0, hires_n * sizeof *low);
    if (gpu_on && S > 1) {
        if (zid) memset(zid, 0, hires_n * sizeof *zid);
        rgpu_write(hires, low, bare, zbuf, zid);
        gpu_dirty = 0;
    }
}

void render_set_scale(int scale) {
    int top = gpu_on ? 8 : 4;
    scale = scale < 1 ? 1 : scale > top ? top : scale;
    if (scale == S) return;
    render_sync();
    free(hires);
    free(zbuf);
    free(zid);
    free(bare);
    free(low);
    bare = NULL;
    low = NULL;
    hires_n = 0;
    hires = NULL;
    zbuf = NULL;
    zid = NULL;
    S = scale;
    if (S > 1) {
        int W = 1024 * S;
        hires = malloc((size_t)W * 512 * S * sizeof *hires);
        zbuf = calloc((size_t)W * 512 * S, sizeof *zbuf);
        zid = calloc((size_t)W * 512 * S, sizeof *zid);
        bare = calloc((size_t)W * 512 * S, 1);
        low = calloc((size_t)W * 512 * S, sizeof *low);
        hires_n = (size_t)W * 512 * S;
        for (int y = 0; y < 512 * S; y++)
            for (int x = 0; x < W; x++) hires[(size_t)y * W + x] = g_vram[y / S][x / S];
        hires_t = (Target){hires, W, S, 512 * S};
        if (!workers_started) { start_workers(); workers_started = 1; }
        if (!gpu_on && g_gpu) gpu_on = rgpu_init(S);   /* the setting on, from 1x */
        if (gpu_on) { rgpu_set_scale(S); rgpu_write(hires, low, bare, zbuf, zid); gpu_dirty = 0; }
    }
    set_bands();
    rt_log("renderer: now %dx internal resolution", S);
}

int render_scale(void) { return S; }

/* The high-res VRAM's bare-pixel map (NULL at 1x or with Close model
 * gaps off), same layout as render_hires. */
const uint8_t *render_bare(void) { gpu_fetch(); return S > 1 && g_gap_fill ? bare : NULL; }

/* The high-res depth (1/z per pixel; 0: no 3D there, -1: 2D drawn over the
 * 3D), NULL at 1x or with Close model gaps off; same layout as render_hires. */
const float *render_depth(void) { gpu_fetch(); return S > 1 && g_gap_fill ? zbuf : NULL; }

const uint16_t *render_hires(int *stride) {
    render_sync();
    gpu_fetch();
    *stride = S > 1 ? 1024 * S : 1024;
    return S > 1 ? hires : &g_vram[0][0];
}

/* The high-res VRAM's 24-bit colour plane (NULL at 1x), same layout as
 * render_hires. */
const uint16_t *render_low(void) { gpu_fetch(); return S > 1 ? low : NULL; }


/* The VRAM a textured primitive reads: its texture coordinates' bounds in
 * its page (the whole page with a texture window, or when they wrap), and
 * its CLUT row. Narrower than the page, so drawing near a page doesn't
 * make every textured primitive wait for the queue. */
static void tex_area(const RPrim *p, int *x, int *y, int *w, int *h) {
    int sh = p->depth == 0 ? 2 : p->depth == 1 ? 1 : 0;
    int umin, umax, vmin, vmax;
    if (p->kind == RP_RECT) {
        int u0 = p->v[0].u, v0 = p->v[0].v;
        umin = p->flipx ? u0 - p->w + 1 : u0; umax = p->flipx ? u0 : u0 + p->w - 1;
        vmin = p->flipy ? v0 - p->h + 1 : v0; vmax = p->flipy ? v0 : v0 + p->h - 1;
    } else {
        int n = p->kind == RP_LINE ? 2 : 3;
        umin = umax = p->v[0].u; vmin = vmax = p->v[0].v;
        for (int i = 1; i < n; i++) {
            if (p->v[i].u < umin) umin = p->v[i].u;
            if (p->v[i].u > umax) umax = p->v[i].u;
            if (p->v[i].v < vmin) vmin = p->v[i].v;
            if (p->v[i].v > vmax) vmax = p->v[i].v;
        }
    }
    if (p->twmx || p->twmy || umin < 0 || vmin < 0 || umax > 255 || vmax > 255) { umin = 0; umax = 255; vmin = 0; vmax = 255; }
    *x = p->tx + (umin >> sh);
    *w = (umax >> sh) - (umin >> sh) + 1;
    *y = (p->ty + vmin) & 511;
    *h = vmax - vmin + 1;
    if (*y + *h > 512) { *y = 0; *h = 512; }
    if (*x + *w > 1024) { *x = 0; *w = 1024; }
}

/* Native-resolution bounding box of a primitive (for conflict checks). */
static void prim_box(const RPrim *p, int *x, int *y, int *w, int *h) {
    float x0, y0, x1, y1;
    if (p->kind == RP_RECT) {
        x0 = p->v[0].x; y0 = p->v[0].y; x1 = x0 + p->w; y1 = y0 + p->h;
        if (p->sqx1 > p->sqx0) { x0 = p->sqx0; x1 = p->sqx1; }
    } else {
        int n = p->kind == RP_LINE ? 2 : 3;
        x0 = x1 = p->v[0].x; y0 = y1 = p->v[0].y;
        for (int i = 1; i < n; i++) {
            x0 = fminf(x0, p->v[i].x); x1 = fmaxf(x1, p->v[i].x);
            y0 = fminf(y0, p->v[i].y); y1 = fmaxf(y1, p->v[i].y);
        }
        x1 += 1; y1 += 1;
    }
    if (x0 < p->dx1) x0 = (float)p->dx1;
    if (y0 < p->dy1) y0 = (float)p->dy1;
    if (x1 > p->dx2 + 1) x1 = (float)(p->dx2 + 1);
    if (y1 > p->dy2 + 1) y1 = (float)(p->dy2 + 1);
    *x = (int)floorf(x0); *y = (int)floorf(y0);
    *w = (int)ceilf(x1) - *x; *h = (int)ceilf(y1) - *y;
}

void render_prim(const RPrim *p) {
    {
        static int probe = -2, px, py;
        static unsigned pf0, pf1;
        if (probe == -2) { const char *e = getenv("RT_PROBE"); probe = e && sscanf(e, "%d,%d,%u,%u", &px, &py, &pf0, &pf1) == 4; }
        if (probe && g_frame >= pf0 && g_frame <= pf1) {
            int bx, by, bw, bh;
            prim_box(p, &bx, &by, &bw, &bh);
            if ((px >= bx && px < bx + bw && py >= by && py < by + bh) || (px >= bx && px < bx + bw && py + 240 >= by && py + 240 < by + bh) || (px >= bx && px < bx + bw && py + 256 >= by && py + 256 < by + bh) || (px + 320 >= bx && px + 320 < bx + bw && py >= by && py < by + bh))
                rt_log("probe: kind %d tex %d depth %d tp %d,%d clut %d,%d semi %d raw %d abr %d da %d,%d-%d,%d v (%.1f,%.1f %d,%d) (%.1f,%.1f %d,%d) (%.1f,%.1f %d,%d) wh %d,%d zt %d z %.0f %.0f %.0f",
                       p->kind, p->textured, p->depth, p->tx, p->ty, p->clut_x, p->clut_y, p->semi, p->raw, p->abr,
                       p->dx1, p->dy1, p->dx2, p->dy2, p->v[0].x, p->v[0].y, p->v[0].u, p->v[0].v, p->v[1].x, p->v[1].y,
                       p->v[1].u, p->v[1].v, p->v[2].x, p->v[2].y, p->v[2].u, p->v[2].v, p->w, p->h, p->zt, p->v[0].z, p->v[1].z, p->v[2].z);
        }
    }
    {
        static int sus = -1;                       /* RT_SUSPECT: CLUT textures with CLUT 0,0 */
        if (sus < 0) sus = getenv("RT_SUSPECT") != NULL;
        if (sus == 1 && getenv("RT_SUSPECT")[0] == '2') sus = 2;
        if (sus == 2 && p->textured && p->depth == 2)
            rt_log("d2: tp %d,%d semi %d v (%.0f,%.0f %d,%d) (%.0f,%.0f %d,%d) (%.0f,%.0f %d,%d)", p->tx, p->ty, p->semi,
                   p->v[0].x, p->v[0].y, p->v[0].u, p->v[0].v, p->v[1].x, p->v[1].y, p->v[1].u, p->v[1].v,
                   p->v[2].x, p->v[2].y, p->v[2].u, p->v[2].v);
        if (sus == 1 && p->textured && p->depth < 2 && p->clut_x == 0 && p->clut_y == 0)
            rt_log("suspect: depth %d tp %d,%d v (%.0f,%.0f %d,%d) (%.0f,%.0f %d,%d) (%.0f,%.0f %d,%d)", p->depth, p->tx, p->ty,
                   p->v[0].x, p->v[0].y, p->v[0].u, p->v[0].v, p->v[1].x, p->v[1].y, p->v[1].u, p->v[1].v,
                   p->v[2].x, p->v[2].y, p->v[2].u, p->v[2].v);
    }
    if (S > 1) {
        int x, y, w, h;
        prim_box(p, &x, &y, &w, &h);
        if (tiles_hit(x, y, w, h)) render_sync();    /* drawing into a texture still in use */
        if (p->textured) {                           /* reading a texture still being drawn */
            int ax, ay, aw, ah;
            tex_area(p, &ax, &ay, &aw, &ah);
            if (drawn_hit(ax, ay, aw, ah) ||
                (p->depth < 2 && drawn_hit(p->clut_x, p->clut_y, p->depth == 0 ? 16 : 256, 1)))
                render_sync();
        }
        drawn_mark(x, y, w, h);
    }
    /* native: integer vertex positions, as the PS1 draws them */
    RPrim n = *p;
    for (int i = 0; i < 3; i++) { n.v[i].x = floorf(n.v[i].x + 0.0001f); n.v[i].y = floorf(n.v[i].y + 0.0001f); }
    raster(&native_t, 0, 512, &n);
    if (S > 1) {
        Op *o = push_op();
        o->op = OP_PRIM;
        o->prim = *p;
        if (p->textured) {
            /* the texels drawn: a triangle's largest u and v aren't (see
             * raster_tri), so they don't count when finding the upload */
            RPrim q = *p;
            if (q.kind == RP_TRI) {
                int um = 0, vm = 0, un = 255, vn = 255;
                for (int i = 0; i < 3; i++) {
                    um = q.v[i].u > um ? q.v[i].u : um; un = q.v[i].u < un ? q.v[i].u : un;
                    vm = q.v[i].v > vm ? q.v[i].v : vm; vn = q.v[i].v < vn ? q.v[i].v : vn;
                }
                for (int i = 0; i < 3; i++) {
                    if (um > un && q.v[i].u == um) q.v[i].u--;
                    if (vm > vn && q.v[i].v == vm) q.v[i].v--;
                }
            }
            int ax, ay, aw, ah;
            tex_area(&q, &ax, &ay, &aw, &ah);
            if (aw == 1024 || ah == 512) {           /* past the edge of VRAM: what's left of it */
                int sh = q.depth == 0 ? 2 : q.depth == 1 ? 1 : 0, un = 255, um = 0, vn = 255, vm = 0;
                int n = q.kind == RP_RECT ? 1 : 3;
                for (int i = 0; i < n; i++) {
                    un = q.v[i].u < un ? q.v[i].u : un; um = q.v[i].u > um ? q.v[i].u : um;
                    vn = q.v[i].v < vn ? q.v[i].v : vn; vm = q.v[i].v > vm ? q.v[i].v : vm;
                }
                if (q.kind == RP_RECT) { um = un + q.w - 1; vm = vn + q.h - 1; }
                if (!q.twmx && !q.twmy && un >= 0 && vn >= 0 && um <= 255 && vm <= 255) {
                    ax = q.tx + (un >> sh); aw = (q.tx + (um >> sh) > 1023 ? 1023 : q.tx + (um >> sh)) - ax + 1;
                    ay = q.ty + vn; ah = (q.ty + vm > 511 ? 511 : q.ty + vm) - ay + 1;
                }
            }
            texpack_attach(&o->prim, ax, ay, aw, ah);
        }
        static int sync_each = -1;
        if (sync_each < 0) sync_each = getenv("RT_SYNC_EACH") != NULL;
        if (sync_each) { render_sync(); return; }
        if (p->textured) {
            int ax, ay, aw, ah;
            tex_area(p, &ax, &ay, &aw, &ah);
            tiles_mark(ax, ay, aw, ah);
            if (p->depth < 2) tiles_mark(p->clut_x, p->clut_y, p->depth == 0 ? 16 : 256, 1);
        }
    }
}

void render_fill(int x, int y, int w, int h, uint16_t color) {
    texpack_fill(x, y, w, h);
    if (S > 1 && tiles_hit(x, y, w, h)) render_sync();
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) g_vram[(y + j) & 511][(x + i) & 1023] = g_fill_mark && S == 1 && w >= 256 ? 0x7C1F : color;
    if (S > 1) {
        drawn_mark(x & 1023, y & 511, w, h);
        Op *o = push_op();
        o->op = OP_FILL;
        o->x = x; o->y = y; o->w = w; o->h = h; o->color = color;
    }
}

void render_copy(int sx, int sy, int dx, int dy, int w, int h, int mask_set, int mask_check) {
    static uint16_t tmp[1024];
    { static int cl = -1; if (cl < 0) cl = getenv("RT_COPYLOG") != NULL; if (cl) rt_log("copy %d,%d -> %d,%d %dx%d", sx, sy, dx, dy, w, h); }
    render_sync();
    texpack_copy(sx, sy, dx, dy, w, h);
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) tmp[i] = g_vram[(sy + j) & 511][(sx + i) & 1023];
        for (int i = 0; i < w; i++) {
            uint16_t *d = &g_vram[(dy + j) & 511][(dx + i) & 1023];
            if (mask_check && (*d & 0x8000)) continue;
            *d = tmp[i] | (mask_set ? 0x8000 : 0);
        }
    }
    if (S > 1 && gpu_on) {
        rgpu_copy(sx, sy, dx, dy, w, h, mask_set, mask_check);
        gpu_dirty = 1;
    } else if (S > 1) {
        int W = 1024 * S, H = 512 * S;
        uint16_t *row = malloc((size_t)w * S * 2 * sizeof *row), *lrow = row + (size_t)w * S;
        for (int j = 0; j < h * S; j++) {
            int ys = (sy * S + j) % H, yd = (dy * S + j) % H;
            for (int i = 0; i < w * S; i++) {
                size_t k = (size_t)ys * W + (sx * S + i) % W;
                row[i] = hires[k];
                lrow[i] = low[k];
            }
            for (int i = 0; i < w * S; i++) {
                uint16_t *d = &hires[(size_t)yd * W + (dx * S + i) % W];
                if (mask_check && (*d & 0x8000)) continue;
                *d = row[i] | (mask_set ? 0x8000 : 0);
                bare[(size_t)(d - hires)] = 0;
                low[(size_t)(d - hires)] = lrow[i];
                zbuf[(size_t)(d - hires)] = -1;
            }
        }
        free(row);
    }
}

/* The game started drawing a frame into another area: forget the depths
 * left there by the frame drawn into it before. */
void render_zclear(int x, int y, int w, int h) {
    if (S == 1 || w <= 0 || h <= 0) return;
    Op *o = push_op();
    o->op = OP_ZCLEAR;
    o->x = x; o->y = y; o->w = w; o->h = h;
}

void render_before_write(int x, int y, int w, int h) {
    if (S > 1 && tiles_hit(x, y, w, h)) render_sync();
}

void render_uploaded(int x, int y, int w, int h) {
    texpack_upload(x, y, w, h);
    if (S == 1) return;
    drawn_mark(x & 1023, y & 511, w, h);
    Op *o = push_op();
    o->op = OP_UPLOAD;
    o->x = x; o->y = y; o->w = w; o->h = h;
    o->pixels = malloc((size_t)w * h * sizeof *o->pixels);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) o->pixels[j * w + i] = g_vram[(y + j) & 511][(x + i) & 1023];
}
