/* The picture the PS1 is showing, for the platform layer: the display
 * area of VRAM, from the high-resolution copy when rendering above the
 * original resolution (24-bit movie frames always come from the PS1's own
 * VRAM, since they're uploaded rather than drawn). */
#include "render.h"
#include "rt.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

int g_opt_widescreen;

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

void display_grab_bgrx(uint32_t *out, int *w, int *h) {
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
        return;
    }
    int stride;
    const uint16_t *hv = render_hires(&stride);
    const uint8_t *bare = render_bare();
    int W = d.w * s, H = d.h * s, X = d.x * s, Y = d.y * s;
    *w = W;
    *h = H;
    for (int y = 0; y < H; y++) {
        size_t ro = (size_t)((Y + y) % (512 * s)) * stride;
        const uint16_t *row = hv + ro;
        uint32_t *o = out + (size_t)y * W;
        for (int x = 0; x < W; x++) {
            int sx = (X + x) % (1024 * s);
            uint16_t p = row[sx];
            uint32_t r = (p & 31) << 3, g = ((p >> 5) & 31) << 3, b = ((p >> 10) & 31) << 3;
            r |= r >> 5; g |= g >> 5; b |= b >> 5;
            o[x] = b | (g << 8) | (r << 16);
        }
    }
    if (bare) close_cracks(out, bare, stride, s, X, Y, W, H);
}
