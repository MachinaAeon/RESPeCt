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
static float g_depth_tol = 24;                  /* touching surfaces keep the PS1's order (RT_DEPTH_TOL) */
static int g_fill_mark, g_sliver_mark;                          /* testing: RT_FILL_MARK fills the screen magenta (gaps show) */
static int g_depth_any;                          /* testing: RT_DEPTH_ANY hides any model behind any */
static double g_margin_len = 6;                  /* longest edge (PS1 pixels) of a triangle given the gap margin; RT_MARGIN_LEN */
int g_gap_fill = 1;                             /* Close model gaps (setting) */                                 /* testing: RT_TEX_EDGE samples the far texel edge too */
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
    if (bare && !semi) { size_t i = (size_t)(d - hires); if (i < hires_n) bare[i] = 0; }
}

/* Shade one pixel: texture and/or colour, dithering, blending. */
static inline void shade(const RPrim *p, uint16_t *d, int x, int y, int r, int g, int b, int u, int v, int dith) {
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
static void raster_tri(const Target *t, int band0, int band1, const RPrim *p) {
    const RVtx *v = p->v;
    int s = t->scale;
    /* hardware limit: polygons this large aren't drawn */
    float minxf = fminf(v[0].x, fminf(v[1].x, v[2].x)), maxxf = fmaxf(v[0].x, fmaxf(v[1].x, v[2].x));
    float minyf = fminf(v[0].y, fminf(v[1].y, v[2].y)), maxyf = fmaxf(v[0].y, fmaxf(v[1].y, v[2].y));
    if (maxxf - minxf >= 1024 || maxyf - minyf >= 512) return;
    int64_t X[3], Y[3];
    double Xd[3], Yd[3];
    for (int i = 0; i < 3; i++) {
        Xd[i] = (double)v[i].x * s;
        Yd[i] = (double)v[i].y * s;
        X[i] = (int64_t)llround(Xd[i] * FIX);
        Y[i] = (int64_t)llround(Yd[i] * FIX);
    }
    int64_t area = (X[1] - X[0]) * (Y[2] - Y[0]) - (X[2] - X[0]) * (Y[1] - Y[0]);
    if (area == 0) return;
    int o[3] = {0, 1, 2};
    if (area < 0) { o[1] = 2; o[2] = 1; }
    /* edges: e opposite vertex o[e], from o[e+1] to o[e+2] */
    int64_t ex[3], ey[3], ec[3];
    int64_t bias[3], mbias[3];
    /* Opaque 3D at a higher resolution gets a margin, up to a PS1 pixel
     * wide, drawn only over what lies well behind it (the depth buffer
     * says): the PS1's whole pixels close the gaps a model's tiny
     * triangles and its parts leave between them, gaps that a sharper
     * picture shows (and that open and shut as models move: flicker). The
     * model's own triangles, at its own depth, are never drawn over. */
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
        if (g_gap_fill) {                        /* Close model gaps: small triangles (distant models) */
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
        ex[e] = -(Y[b] - Y[a]);                   /* d/dx */
        ey[e] = (X[b] - X[a]);                    /* d/dy */
        ec[e] = -(ex[e] * X[a] + ey[e] * Y[a]);
        int dy = (int)(Y[b] - Y[a]), dx = (int)(X[b] - X[a]);
        int top_left = (dy < 0) || (dy == 0 && dx > 0);
        bias[e] = top_left ? 0 : -1;
        mbias[e] = bias[e] + (int64_t)(margin * FIX * sqrt((double)ex[e] * ex[e] + (double)ey[e] * ey[e]));
    }
    int x0 = (int)floor(minxf * s), x1 = (int)ceil(maxxf * s);
    int y0 = (int)floor(minyf * s), y1 = (int)ceil(maxyf * s);
    if (margin > 0) { int m = (int)ceil(margin); x0 -= m; y0 -= m; x1 += m; y1 += m; }
    int cx0 = p->dx1 * s, cx1 = (p->dx2 + 1) * s - 1, cy0 = p->dy1 * s, cy1 = (p->dy2 + 1) * s - 1;
    if (x0 < cx0) x0 = cx0;
    if (x1 > cx1) x1 = cx1;
    if (y0 < cy0) y0 = cy0;
    if (y1 > cy1) y1 = cy1;
    if (y0 < band0) y0 = band0;
    if (y1 >= band1) y1 = band1 - 1;
    if (x0 > x1 || y0 > y1) return;

    Plane pr = {0}, pg = {0}, pb = {0}, pu = {0}, pv = {0};
    int gour = p->gouraud;
    if (gour) {
        pr = plane(Xd, Yd, v[0].r, v[1].r, v[2].r);
        pg = plane(Xd, Yd, v[0].g, v[1].g, v[2].g);
        pb = plane(Xd, Yd, v[0].b, v[1].b, v[2].b);
    }
    if (p->textured) {
        pu = plane(Xd, Yd, v[0].u, v[1].u, v[2].u);
        pv = plane(Xd, Yd, v[0].v, v[1].v, v[2].v);
    }
    int dith = p->dither && s == 1 && (gour || (p->textured && !p->raw));
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
    /* depth: 1/z, linear on the screen */
    int ztest = zuse && g_depth_buffer;              /* the Depth fix setting */
    /* Slivers (the game's thin highlight strips, edge on surfaces) don't
     * hide anything: what the game draws over them still covers them. */
    int zwrite = zuse && !sliver;
    /* texture page and CLUT: a model part's own triangles share both;
     * decals (a dart board on a wall) and other objects drawn with the
     * room's page have their own CLUT */
    uint32_t id = p->textured ? 1u + (uint32_t)(p->tx >> 6) + ((uint32_t)(p->ty >> 8) << 4) +
                                ((uint32_t)(p->clut_x >> 4) << 5) + ((uint32_t)p->clut_y << 11)
                              : 0xFFFFFFFFu;
    Plane pw = {0};
    if (zuse) pw = plane(Xd, Yd, 1.0 / v[0].z, 1.0 / v[1].z, 1.0 / v[2].z);
    const int32_t one = 65536;
    int32_t dr = (int32_t)(pr.a * one), dg = (int32_t)(pg.a * one), db = (int32_t)(pb.a * one);
    int32_t du = (int32_t)(pu.a * one), dv = (int32_t)(pv.a * one);
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
        for (int x = x0; x <= x1; x++, wz += pw.a) {
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
                rt_log("zprobe: tp %d,%d clut %d,%d semi %d core %d zt %d z %.1f zs %.1f zwrite %d v (%.1f,%.1f %.0f) (%.1f,%.1f %.0f) (%.1f,%.1f %.0f)", p->tx, p->ty, p->clut_x, p->clut_y, p->semi, core, ztest,
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
                    if ((float)wz > zs * 0.88f && (float)wz < zs && (idrow[x] == id || g_depth_any)) {
                        float behind = 1.0f / (float)wz - 1.0f / zs;   /* in GTE depth units */
                        float tol = 0.004f / zs;
                        if (tol < g_depth_tol) tol = g_depth_tol;
                        if (behind > tol) goto next;
                    }
                    if (zwrite && core == 1 && !p->semi && (!p->textured || texel(p, tu, tv))) { zrow[x] = (float)wz; idrow[x] = id; }
                }
                int cr = v[0].r, cg = v[0].g, cb = v[0].b;
                if (gour) {
                    cr = r >> 16; cg = g >> 16; cb = b >> 16;
                    cr = cr < 0 ? 0 : cr > 255 ? 255 : cr;
                    cg = cg < 0 ? 0 : cg > 255 ? 255 : cg;
                    cb = cb < 0 ? 0 : cb > 255 ? 255 : cb;
                }
                if (g_sliver_mark && zuse && !zwrite) { row[x] = core == 2 ? 0x03E0 : 0x001F; goto next; }
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
                    bare[i] = 0;
                }
            }
            break;
        }
    }
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
    go_workers();
    run_ops(band_of[0][0], band_of[0][1]);
    wait_workers();
    for (int i = 0; i < nq; i++)
        if (queue[i].op == OP_UPLOAD) free(queue[i].pixels);
    nq = 0;
    memset(tex_tiles, 0, sizeof tex_tiles);
    memset(drawn_tiles, 0, sizeof drawn_tiles);
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
    S = scale < 1 ? 1 : scale > 4 ? 4 : scale;
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
    g_margin_log = getenv("RT_MARGIN_LOG") != NULL;
    if (getenv("RT_MARGIN_DEPTH")) g_margin_depth = (float)atof(getenv("RT_MARGIN_DEPTH"));
    if (getenv("RT_MARGIN_AREA")) g_margin_area = atof(getenv("RT_MARGIN_AREA"));
    native_t = (Target){&g_vram[0][0], 1024, 1, 512};
    if (S > 1) {
        hires = calloc((size_t)1024 * S * 512 * S, sizeof *hires);
        zbuf = calloc((size_t)1024 * S * 512 * S, sizeof *zbuf);
        zid = calloc((size_t)1024 * S * 512 * S, sizeof *zid);
        bare = calloc((size_t)1024 * S * 512 * S, 1);
        hires_n = (size_t)1024 * S * 512 * S;
        hires_t = (Target){hires, 1024 * S, S, 512 * S};
    }
    nworkers = threads - 1;
    if (nworkers < 0) nworkers = 0;
    if (nworkers > MAX_WORKERS) nworkers = MAX_WORKERS;
    set_bands();
    if (S > 1) { start_workers(); workers_started = 1; }
    rt_log("renderer: %dx internal resolution, %d threads", S, nworkers + 1);
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
    if (S > 1) upscale_from_native();
    if (zbuf) memset(zbuf, 0, (size_t)1024 * S * 512 * S * sizeof *zbuf);
    if (bare) memset(bare, 0, hires_n);
}

void render_set_scale(int scale) {
    scale = scale < 1 ? 1 : scale > 4 ? 4 : scale;
    if (scale == S) return;
    render_sync();
    free(hires);
    free(zbuf);
    free(zid);
    free(bare);
    bare = NULL;
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
        hires_n = (size_t)W * 512 * S;
        for (int y = 0; y < 512 * S; y++)
            for (int x = 0; x < W; x++) hires[(size_t)y * W + x] = g_vram[y / S][x / S];
        hires_t = (Target){hires, W, S, 512 * S};
        if (!workers_started) { start_workers(); workers_started = 1; }
    }
    set_bands();
    rt_log("renderer: now %dx internal resolution", S);
}

int render_scale(void) { return S; }

/* The high-res VRAM's bare-pixel map (NULL at 1x or with Close model
 * gaps off), same layout as render_hires. */
const uint8_t *render_bare(void) { return S > 1 && g_gap_fill ? bare : NULL; }

const uint16_t *render_hires(int *stride) {
    render_sync();
    *stride = S > 1 ? 1024 * S : 1024;
    return S > 1 ? hires : &g_vram[0][0];
}


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
    for (int j = 0; j < h; j++) {
        for (int i = 0; i < w; i++) tmp[i] = g_vram[(sy + j) & 511][(sx + i) & 1023];
        for (int i = 0; i < w; i++) {
            uint16_t *d = &g_vram[(dy + j) & 511][(dx + i) & 1023];
            if (mask_check && (*d & 0x8000)) continue;
            *d = tmp[i] | (mask_set ? 0x8000 : 0);
        }
    }
    if (S > 1) {
        int W = 1024 * S, H = 512 * S;
        uint16_t *row = malloc((size_t)w * S * sizeof *row);
        for (int j = 0; j < h * S; j++) {
            int ys = (sy * S + j) % H, yd = (dy * S + j) % H;
            for (int i = 0; i < w * S; i++) row[i] = hires[(size_t)ys * W + (sx * S + i) % W];
            for (int i = 0; i < w * S; i++) {
                uint16_t *d = &hires[(size_t)yd * W + (dx * S + i) % W];
                if (mask_check && (*d & 0x8000)) continue;
                *d = row[i] | (mask_set ? 0x8000 : 0);
                bare[(size_t)(d - hires)] = 0;
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
    if (S == 1) return;
    drawn_mark(x & 1023, y & 511, w, h);
    Op *o = push_op();
    o->op = OP_UPLOAD;
    o->x = x; o->y = y; o->w = w; o->h = h;
    o->pixels = malloc((size_t)w * h * sizeof *o->pixels);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) o->pixels[j * w + i] = g_vram[(y + j) & 511][(x + i) & 1023];
}
