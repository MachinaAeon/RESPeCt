/* Captions: words a mod puts over parts of the game's pictures (a menu
 * word drawn as an image, say), drawn by the platform over the picture
 * where and while the game draws that part, as bright as the game draws
 * it. A caption is keyed by the picture it covers: its colour depth and a
 * rectangle of its texels, counted from VRAM's corner (x in texels: 4 to a
 * VRAM halfword at 4 bits, 2 at 8, 1 at 16). Any primitive drawing from
 * there places it, even one that draws only part of it (a big picture cut
 * into pages): the caption goes where that primitive's mapping puts it.
 *
 * gpu.c reports each 2D textured primitive (caption_prim); the captions it
 * covers are kept with the frame buffer they were drawn into, and the
 * buffer on display is the one shown (caption_shown). A buffer's list is
 * emptied when the display moves off it (it's then drawn again) or when
 * it's cleared whole. */
#include "captions.h"
#include "rt.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct { int depth, x0, y0, x1, y1; uint32_t hash; char *text; uint32_t seen_frame; int seen_ok; } Source;
extern uint16_t g_vram[512][1024];

/* FNV-1a of the VRAM halfwords under a texel rectangle: which picture is
 * there (several share a place in VRAM, a word's caption only goes with its own). */
uint32_t caption_hash(int depth, int x0, int y0, int x1, int y1) {
    int per = depth == 0 ? 4 : depth == 1 ? 2 : 1;
    uint32_t h = 2166136261u;
    for (int y = y0; y < y1; y++)
        for (int x = x0 / per; x < (x1 + per - 1) / per; x++) {
            uint16_t v = g_vram[y & 511][x & 1023];
            h = (h ^ (v & 0xFF)) * 16777619u;
            h = (h ^ (v >> 8)) * 16777619u;
        }
    return h;
}
static Source *sources;
static int nsources;

#define MAX_SHOWN 32
typedef struct { int used, ox, oy, n; Caption c[MAX_SHOWN]; } Buffer;   /* by its draw area's origin */
static Buffer buffers[2];

void caption_add(int depth, int x0, int y0, int x1, int y1, uint32_t hash, const char *text) {
    Source *s = realloc(sources, (size_t)(nsources + 1) * sizeof *s);
    if (!s) return;
    sources = s;
    s += nsources++;
    s->depth = depth;
    s->x0 = x0; s->y0 = y0; s->x1 = x1; s->y1 = y1;
    s->hash = hash;
    s->seen_frame = 0xFFFFFFFFu;
    s->seen_ok = 0;
    s->text = strdup(text);
}
int caption_sources(void) { return nsources; }

static Buffer *buffer_at(int ox, int oy, int make) {
    for (int i = 0; i < 2; i++)
        if (buffers[i].used && buffers[i].ox == ox && buffers[i].oy == oy) return &buffers[i];
    if (!make) return NULL;
    Buffer *b = !buffers[0].used ? &buffers[0] : !buffers[1].used ? &buffers[1] : !buffers[0].n ? &buffers[0] : &buffers[1];
    b->used = 1; b->ox = ox; b->oy = oy; b->n = 0;
    return b;
}

/* The commonest colour of a picture's texel rectangle, through its CLUT:
 * the box drawn under a caption takes it (a highlighted button stays red). */
static void back_colour(int depth, int clut_x, int clut_y, const Source *s, Caption *c) {
    enum { N = 64 };
    uint16_t col[N];
    int cnt[N] = {0}, n = 0;
    for (int y = s->y0; y < s->y1; y++)
        for (int x = s->x0; x < s->x1; x++) {
            uint16_t px;
            if (depth == 2) px = g_vram[y & 511][x & 1023];
            else {
                uint16_t w = g_vram[y & 511][(depth == 0 ? x >> 2 : x >> 1) & 1023];
                int idx = depth == 0 ? (w >> ((x & 3) * 4)) & 0xF : (w >> ((x & 1) * 8)) & 0xFF;
                px = g_vram[clut_y & 511][(clut_x + idx) & 1023];
            }
            if (!px) continue;                       /* (see-through) */
            int k = 0;
            for (; k < n; k++) if (col[k] == px) break;
            if (k == n) { if (n == N) continue; col[n++] = px; }
            cnt[k]++;
        }
    int best = -1;
    for (int k = 0; k < n; k++) if (best < 0 || cnt[k] > cnt[best]) best = k;
    uint16_t px = best < 0 ? 0 : col[best];
    c->r = (px & 31) << 3; c->g = ((px >> 5) & 31) << 3; c->b = ((px >> 10) & 31) << 3;
    int text = -1;                                   /* the words: the commonest colour far from it */
    for (int k = 0; k < n; k++) {
        int r = (col[k] & 31) << 3, g = ((col[k] >> 5) & 31) << 3, b = ((col[k] >> 10) & 31) << 3;
        int d = abs(r - c->r) + abs(g - c->g) + abs(b - c->b);
        if (d >= 160 && (text < 0 || cnt[k] > cnt[text])) text = k;
    }
    if (text < 0) {                                  /* none: white or black, whichever shows */
        int l = (c->r * 3 + c->g * 6 + c->b) / 10 > 128 ? 20 : 235;
        c->tr = c->tg = c->tb = l;
    } else {
        c->tr = (col[text] & 31) << 3; c->tg = ((col[text] >> 5) & 31) << 3; c->tb = ((col[text] >> 10) & 31) << 3;
    }
}

void caption_prim(int tx, int ty, int depth, int clut_x, int clut_y, int umin, int vmin, int umax, int vmax,
                  float x0, float y0, float x1, float y1, int bright, int da_x, int da_y, int disp_w, int disp_h) {
    if (!nsources || umax <= umin || vmax <= vmin) return;
    int per = depth == 0 ? 4 : depth == 1 ? 2 : 1;
    int ax0 = tx * per + umin, ax1 = tx * per + umax, ay0 = ty + vmin, ay1 = ty + vmax;   /* its texels, from VRAM's corner */
    for (int i = 0; i < nsources; i++) {
        Source *s = &sources[i];
        if (s->depth != depth || s->x1 <= ax0 || s->x0 >= ax1 || s->y1 <= ay0 || s->y0 >= ay1) continue;
        if (s->hash) {                                 /* its own picture there? (once a frame) */
            if (s->seen_frame != g_frame) { s->seen_frame = g_frame; s->seen_ok = caption_hash(s->depth, s->x0, s->y0, s->x1, s->y1) == s->hash; }
            if (!s->seen_ok) continue;
        }
        float kx = (x1 - x0) / (float)(ax1 - ax0), ky = (y1 - y0) / (float)(ay1 - ay0);
        Caption c;
        c.x = (x0 + (float)(s->x0 - ax0) * kx - (float)da_x) / (float)disp_w;
        c.y = (y0 + (float)(s->y0 - ay0) * ky - (float)da_y) / (float)disp_h;
        c.w = (float)(s->x1 - s->x0) * kx / (float)disp_w;
        c.h = (float)(s->y1 - s->y0) * ky / (float)disp_h;
        c.bright = bright > 255 ? 255 : bright;
        c.text = s->text;
        back_colour(depth, clut_x, clut_y, s, &c);
        Buffer *b = buffer_at(da_x, da_y, 1);
        int dup = 0;
        for (int k = 0; k < b->n; k++)
            if (b->c[k].text == c.text && b->c[k].x - c.x < 0.01f && c.x - b->c[k].x < 0.01f && b->c[k].y - c.y < 0.01f && c.y - b->c[k].y < 0.01f) dup = 1;
        if (!dup && b->n < MAX_SHOWN) b->c[b->n++] = c;
        static int log = -1;
        if (log < 0) log = getenv("RT_CAPLOG") != NULL;
        if (log && !dup) rt_log("caption: \"%s\" %.3f %.3f %.3f %.3f bright %d rgb %d,%d,%d text %d,%d,%d", c.text, c.x, c.y, c.w, c.h, c.bright, c.r, c.g, c.b, c.tr, c.tg, c.tb);
    }
}

/* The display moved to (x, y): the other buffer is drawn next, from nothing. */
void caption_display(int x, int y) {
    for (int i = 0; i < 2; i++)
        if (buffers[i].n && (buffers[i].ox != x || buffers[i].oy != y)) buffers[i].n = 0;
}

/* A fill of a whole buffer: what was drawn in it is gone. */
void caption_fill(int x, int y, int w, int h, int disp_w, int disp_h) {
    if (w < disp_w * 9 / 10 || h < disp_h * 9 / 10) return;
    for (int i = 0; i < 2; i++)
        if (buffers[i].n && buffers[i].ox >= x && buffers[i].oy >= y && buffers[i].ox < x + w && buffers[i].oy < y + h) buffers[i].n = 0;
}

int caption_shown(int disp_x, int disp_y, const Caption **out) {
    Buffer *b = buffer_at(disp_x, disp_y, 0);
    if (!b || !b->n) { *out = NULL; return 0; }
    *out = b->c;
    return b->n;
}
