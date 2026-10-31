/* The picture the PS1 is showing, for the platform layer: the display
 * area of VRAM, from the high-resolution copy when rendering above the
 * original resolution (24-bit movie frames always come from the PS1's own
 * VRAM, since they're uploaded rather than drawn). */
#include "render.h"
#include "render_gpu.h"
#include "rt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int g_opt_widescreen;
/* The size the picture is shown at (the platform sets it; 0: as made) and
 * whether to smooth it there: drawing on the graphics card, the picture is
 * made at that size (render_gpu.c). */
int g_display_w, g_display_h, g_display_smooth = 1;

/* Once per frame: decide whether widescreen applies (in game, not movies). */
void display_frame_update(void) {
    GpuDisplay d;
    gpu_display(&d);
    g_widescreen = g_opt_widescreen && MEM_R32(0x8006C118) == 6 && !d.rgb24;
}

static void px_native(const GpuDisplay *d, int x, int y, uint32_t *r, uint32_t *g, uint32_t *b) {
    if (d->rgb24) {
        const uint8_t *row = (const uint8_t *)g_vram[(d->y + y) & 511];
        int bx = d->x * 2 + x * 3;
        *r = row[bx % 2048]; *g = row[(bx + 1) % 2048]; *b = row[(bx + 2) % 2048];
    } else {
        uint16_t p = g_vram[(d->y + y) & 511][(d->x + x) & 1023];
        *r = (p & 31) << 3; *g = ((p >> 5) & 31) << 3; *b = ((p >> 10) & 31) << 3;
        *r |= *r >> 5; *g |= *g >> 5; *b |= *b >> 5;
    }
}

void display_grab_rgb(uint8_t *rgb, int *w, int *h) {
    GpuDisplay d;
    gpu_display(&d);
    *w = d.w;
    *h = d.h;
    for (int y = 0; y < d.h; y++)
        for (int x = 0; x < d.w; x++) {
            uint32_t r, g, b;
            px_native(&d, x, y, &r, &g, &b);
            uint8_t *o = rgb + ((size_t)y * d.w + x) * 3;
            o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)b;
        }
}

/* Movie frames (24-bit, decoded by the MDEC at 320x240 or so) are only
 * there at the PS1's resolution. When rendering higher, scale them up here:
 * soften the 8x8 block edges the compression leaves (only where the step
 * across the edge is small, so real edges stay), then Catmull-Rom
 * (bicubic) to the rendering scale. */
int g_opt_movie_upscale = 1;

static uint8_t mv_src[640 * 512 * 3];
static float mv_tmp[2560 * 512 * 3];

static void deblock(uint8_t *p, int w, int h) {
    const int T = 10;
    for (int y = 0; y < h; y++)                       /* vertical block edges */
        for (int x = 8; x < w - 1; x += 8)
            for (int c = 0; c < 3; c++) {
                uint8_t *r = p + ((size_t)y * w) * 3 + c;
                int a = r[(x - 2) * 3], b = r[(x - 1) * 3], d = r[x * 3], e = r[(x + 1) * 3];
                if (abs(b - d) >= T) continue;
                r[(x - 1) * 3] = (uint8_t)((a + 2 * b + d + 2) / 4);
                r[x * 3] = (uint8_t)((b + 2 * d + e + 2) / 4);
            }
    for (int y = 8; y < h - 1; y += 8)                /* horizontal block edges */
        for (int x = 0; x < w; x++)
            for (int c = 0; c < 3; c++) {
                uint8_t *r = p + (size_t)x * 3 + c;
                size_t row = (size_t)w * 3;
                int a = r[(y - 2) * row], b = r[(y - 1) * row], d = r[y * row], e = r[(y + 1) * row];
                if (abs(b - d) >= T) continue;
                r[(y - 1) * row] = (uint8_t)((a + 2 * b + d + 2) / 4);
                r[y * row] = (uint8_t)((b + 2 * d + e + 2) / 4);
            }
}

static void cr_weights(float t, float wt[4]) {
    float t2 = t * t, t3 = t2 * t;
    wt[0] = (-t3 + 2 * t2 - t) * 0.5f;
    wt[1] = (3 * t3 - 5 * t2 + 2) * 0.5f;
    wt[2] = (-3 * t3 + 4 * t2 + t) * 0.5f;
    wt[3] = (t3 - t2) * 0.5f;
}

static void movie_upscale(const GpuDisplay *d, int f, uint32_t *out) {
    int w = d->w, h = d->h, W = w * f, H = h * f;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            uint32_t r, g, b;
            px_native(d, x, y, &r, &g, &b);
            uint8_t *o = mv_src + ((size_t)y * w + x) * 3;
            o[0] = (uint8_t)r; o[1] = (uint8_t)g; o[2] = (uint8_t)b;
        }
    deblock(mv_src, w, h);
    for (int y = 0; y < h; y++)                       /* across */
        for (int X = 0; X < W; X++) {
            float sx = (X + 0.5f) / f - 0.5f;
            int x0 = (int)floorf(sx);
            float wt[4];
            cr_weights(sx - x0, wt);
            float acc[3] = {0, 0, 0};
            for (int k = 0; k < 4; k++) {
                int xx = x0 - 1 + k;
                xx = xx < 0 ? 0 : xx >= w ? w - 1 : xx;
                const uint8_t *p = mv_src + ((size_t)y * w + xx) * 3;
                for (int c = 0; c < 3; c++) acc[c] += wt[k] * p[c];
            }
            float *o = mv_tmp + ((size_t)y * W + X) * 3;
            o[0] = acc[0]; o[1] = acc[1]; o[2] = acc[2];
        }
    for (int Y = 0; Y < H; Y++) {                     /* down */
        float sy = (Y + 0.5f) / f - 0.5f;
        int y0 = (int)floorf(sy);
        float wt[4];
        cr_weights(sy - y0, wt);
        for (int X = 0; X < W; X++) {
            float acc[3] = {0, 0, 0};
            for (int k = 0; k < 4; k++) {
                int yy = y0 - 1 + k;
                yy = yy < 0 ? 0 : yy >= h ? h - 1 : yy;
                const float *p = mv_tmp + ((size_t)yy * W + X) * 3;
                for (int c = 0; c < 3; c++) acc[c] += wt[k] * p[c];
            }
            uint32_t v[3];
            for (int c = 0; c < 3; c++) v[c] = acc[c] < 0 ? 0 : acc[c] > 255 ? 255 : (uint32_t)(acc[c] + 0.5f);
            out[(size_t)Y * W + X] = v[2] | (v[1] << 8) | (v[0] << 16);
        }
    }
}

/* 32-bit BGRX at the rendering resolution; out must hold 2560 x 1920. */



/* Close model gaps: hair-thin cracks where a room's surfaces meet. A run of
 * bare pixels (still the screen clear, nothing drawn) at most one PS1 pixel
 * long between drawn ones, across or down, shows the drawn pixel before it.
 * Runs are found row by row, then column by column, so it costs little
 * even when much of the screen is bare (dark scenes, transitions). */
static void close_cracks(uint32_t *out, const uint8_t *bare, int stride, int s, int X, int Y, int W, int H) {
    static uint8_t *mask;
    static int *start;
    static size_t mask_n, start_n;
    if ((size_t)W * H > mask_n) { free(mask); mask_n = (size_t)W * H; mask = malloc(mask_n); }
    if ((size_t)W > start_n) { free(start); start_n = (size_t)W; start = malloc(start_n * sizeof *start); }
    if (!mask || !start) return;
    int mw = 1024 * s, mh = 512 * s;
    for (int x = 0; x < W; x++) start[x] = -1;
    for (int y = 0; y <= H; y++) {
        uint8_t *m = mask + (size_t)(y < H ? y : 0) * W;
        uint32_t *o = out + (size_t)(y < H ? y : 0) * W;
        if (y < H) {
            const uint8_t *br = bare + (size_t)((Y + y) % mh) * stride;
            if (X % mw + W <= mw) memcpy(m, br + X % mw, (size_t)W);
            else for (int x = 0; x < W; x++) m[x] = br[(X + x) % mw];
            /* across */
            for (int x = 1; x < W; ) {
                if (!m[x]) { x++; continue; }
                int e = x;
                while (e < W && m[e]) e++;
                if (e < W && !m[x - 1] && e - x <= s)
                    for (int k = x; k < e; k++) { o[k] = o[x - 1]; m[k] = 0; }
                x = e;
            }
        }
        /* down: runs still bare in each column, closed as they end */
        for (int x = 0; x < W; x++) {
            int b = y < H && m[x];
            if (b) { if (start[x] < 0) start[x] = y; continue; }
            int st = start[x];
            if (st < 0) continue;
            start[x] = -1;
            if (st > 0 && y < H && y - st <= s) {
                uint32_t c = out[(size_t)(st - 1) * W + x];
                for (int k = st; k < y; k++) out[(size_t)k * W + x] = c;
            }
        }
    }
}

/* Close model gaps, in models: hair-thin holes where the background shows
 * between a model's parts (their seam's corners a fraction of a PS1 pixel
 * apart, which the PS1's whole pixels shut; at a higher resolution they
 * open and close as the model moves). A run under one PS1 pixel long of
 * pixels well behind, or with no 3D, between two pixels of 3D at about the
 * same depth, across or down, shows the pixel before it. A model's outline
 * has 3D on one side only, so it's never widened; 2D drawn over the 3D
 * (depth -1) is never touched. */
#define HOLE_BEHIND 64.0f                       /* GTE depth units */
static void close_holes(uint32_t *out, const float *depth, int stride, int s, int X, int Y, int W, int H) {
    static float *last_z;
    static int *last_y, *run_y;
    static size_t col_n;
    if ((size_t)W > col_n) {
        free(last_z); free(last_y); free(run_y);
        col_n = (size_t)W;
        last_z = malloc(col_n * sizeof *last_z); last_y = malloc(col_n * sizeof *last_y); run_y = malloc(col_n * sizeof *run_y);
    }
    if (!last_z || !last_y || !run_y) return;
    int mw = 1024 * s, mh = 512 * s, maxrun = s - 1;
    for (int x = 0; x < W; x++) last_y[x] = run_y[x] = -1;
    for (int y = 0; y < H; y++) {
        const float *zr = depth + (size_t)((Y + y) % mh) * stride;
        uint32_t *o = out + (size_t)y * W;
        /* across */
        for (int x = 0; x < W - 1; ) {
            float z = zr[(X + x) % mw];
            if (z <= 0) { x++; continue; }
            float iz = 1.0f / z, far = 1.0f / (iz + HOLE_BEHIND);    /* 1/z below this: well behind */
            int j = x + 1;
            while (j < W && j - x - 1 < maxrun) {
                float zj = zr[(X + j) % mw];
                if (zj < 0 || zj >= far) break;
                j++;
            }
            if (j > x + 1 && j < W) {
                float ze = zr[(X + j) % mw];
                if (ze > 0 && fabsf(1.0f / ze - iz) < HOLE_BEHIND) {
                    for (int k = x + 1; k < j; k++) o[k] = o[x];
                    x = j;
                    continue;
                }
            }
            x++;
        }
        /* down: per column, the last 3D pixel and the run behind it */
        for (int x = 0; x < W; x++) {
            float z = zr[(X + x) % mw];
            if (z < 0) { last_y[x] = run_y[x] = -1; continue; }   /* 2D over the 3D */
            if (last_y[x] >= 0 && (z == 0 || 1.0f / z - 1.0f / last_z[x] > HOLE_BEHIND)) {
                if (run_y[x] < 0) run_y[x] = y;                     /* behind the last 3D pixel */
                if (y - run_y[x] + 1 > maxrun) last_y[x] = run_y[x] = -1;
                continue;
            }
            if (z == 0) continue;
            int r = run_y[x];
            if (r >= 0 && fabsf(1.0f / z - 1.0f / last_z[x]) < HOLE_BEHIND) {
                uint32_t c = out[(size_t)(r - 1) * W + x];
                for (int k = r; k < y; k++) out[(size_t)k * W + x] = c;
            }
            last_z[x] = z; last_y[x] = y; run_y[x] = -1;
        }
    }
}

/* Brightness (%): a gamma curve, 100 = as the PlayStation shows it. Above
 * 100 lifts the dark parts most, as a TV's brightness does. */
int g_brightness = 100;
static int lut_for = -1;
static uint8_t gamma8[256];
static uint32_t lut15[32768];

static void build_luts(void) {
    if (lut_for == g_brightness) return;
    lut_for = g_brightness;
    double e = 100.0 / (g_brightness > 10 ? g_brightness : 10);
    for (int i = 0; i < 256; i++) {
        int v = (int)(pow(i / 255.0, e) * 255.0 + 0.5);
        gamma8[i] = (uint8_t)(v > 255 ? 255 : v);
    }
    for (uint32_t p = 0; p < 32768; p++) {
        uint32_t r = (p & 31) << 3, g = ((p >> 5) & 31) << 3, b = ((p >> 10) & 31) << 3;
        r |= r >> 5; g |= g >> 5; b |= b >> 5;
        lut15[p] = gamma8[b] | ((uint32_t)gamma8[g] << 8) | ((uint32_t)gamma8[r] << 16);
    }
}

static void apply_gamma(uint32_t *out, size_t n) {
    if (g_brightness == 100) return;
    for (size_t i = 0; i < n; i++) {
        uint32_t c = out[i];
        out[i] = gamma8[c & 0xFF] | ((uint32_t)gamma8[(c >> 8) & 0xFF] << 8) | ((uint32_t)gamma8[(c >> 16) & 0xFF] << 16);
    }
}

void display_grab_bgrx(uint32_t *out, int *w, int *h) {
    build_luts();
    GpuDisplay d;
    gpu_display(&d);
    int s = render_scale();
    if (d.rgb24 && d.enabled && s > 1 && g_opt_movie_upscale && d.w <= 640 && d.h <= 512) {
        int f = s;
        while (f > 1 && (d.w * f > 2560 || d.h * f > 1920 || d.w * f > 2560 || (size_t)d.w * f * d.h * 3 > sizeof mv_tmp / sizeof mv_tmp[0])) f--;
        if (f > 1) {
            movie_upscale(&d, f, out);
            *w = d.w * f;
            *h = d.h * f;
            apply_gamma(out, (size_t)*w * *h);
            return;
        }
    }
    if (d.rgb24 || s == 1 || !d.enabled) {
        *w = d.w;
        *h = d.h;
        for (int y = 0; y < d.h; y++)
            for (int x = 0; x < d.w; x++) {
                uint32_t r = 0, g = 0, b = 0;
                if (d.enabled) px_native(&d, x, y, &r, &g, &b);
                out[y * d.w + x] = b | (g << 8) | (r << 16);
            }
        apply_gamma(out, (size_t)d.w * d.h);
        return;
    }
    int stride;
    int W = d.w * s, H = d.h * s, X = d.x * s, Y = d.y * s;
    if (render_on_gpu()) {                       /* made on the graphics card, to the window's size */
        int ow = g_display_w, oh = g_display_h;
        if (ow > 0 && oh > 0) {
            while (ow > 2560 || oh > 1920) { ow = ow * 3 / 4; oh = oh * 3 / 4; }
        } else if (W <= 2560 && H <= 1920) {
            ow = W;
            oh = H;
        } else
            ow = 0;
        if (ow > 0) {
            render_sync();
            rgpu_picture(X, Y, W, H, g_gap_fill, gamma8, out, ow, oh, g_display_smooth);
            *w = ow;
            *h = oh;
            return;
        }
    }
    render_want(X, Y, W, H);
    const uint16_t *hv = render_hires(&stride);
    const uint8_t *bare = render_bare();
    const uint16_t *low = render_low();
    /* a wide screen mode at a high scale: every k-th pixel (the picture
     * shown holds 2560 x 1920 at most) */
    int k = 1;
    while (W / k > 2560 || H / k > 1920) k++;
    *w = W / k;
    *h = H / k;
    for (int y = 0; y < *h; y++) {
        size_t ro = (size_t)((Y + y * k) % (512 * s)) * stride;
        const uint16_t *row = hv + ro, *lrow = low ? low + ro : NULL;
        uint32_t *o = out + (size_t)y * *w;
        for (int x = 0; x < *w; x++) {
            int sx = (X + x * k) % (1024 * s);
            uint16_t c = row[sx], e = lrow ? lrow[sx] : 0;
            if (e & 0x8000) {                /* drawn in 24-bit colour */
                uint32_t r = ((c & 31) << 3) | (e & 7), g = (((c >> 5) & 31) << 3) | ((e >> 3) & 7), b = (((c >> 10) & 31) << 3) | ((e >> 6) & 7);
                o[x] = gamma8[b] | ((uint32_t)gamma8[g] << 8) | ((uint32_t)gamma8[r] << 16);
            } else
                o[x] = lut15[c & 0x7FFF];
        }
    }
    if (k > 1) return;
    const float *depth = render_depth();
    if (depth) close_holes(out, depth, stride, s, X, Y, W, H);
    if (bare) close_cracks(out, bare, stride, s, X, Y, W, H);
}
