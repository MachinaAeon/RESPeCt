/* PS1 GPU: command processing (GP0/GP1) and a software rasterizer into a
 * 1024x512 16-bit VRAM, following the hardware's rules (psx-spx):
 * flat/Gouraud/textured triangles and quads, lines, rectangles, VRAM
 * transfers, semi-transparency, mask bit, texture windows, draw area. */
#include "rt.h"
#include "savestate.h"
#include "render.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

uint16_t g_vram[512][1024];

typedef struct {
    /* drawing environment */
    uint32_t texpage;            /* E1 bits */
    int tex_x, tex_y, tex_depth, abr, dither, draw_display, tex_disable;
    int rect_flip_x, rect_flip_y;
    int tw_mask_x, tw_mask_y, tw_off_x, tw_off_y;
    int da_x1, da_y1, da_x2, da_y2;
    int off_x, off_y;
    int mask_set, mask_check;
    /* display */
    int disp_x, disp_y, disp_x1, disp_x2, disp_y1, disp_y2;
    uint32_t disp_mode;
    int disp_enabled;
    int dma_dir;
    int irq;
    /* command buffer */
    uint32_t cmd[16];
    int ncmd, need;
    int poly_line;               /* polyline in progress */
    /* VRAM transfers */
    int xfer_in, xfer_out;
    int xf_x, xf_y, xf_w, xf_h, xf_i, xf_n;
    uint32_t gpuread;
    int field;
} Gpu;

static Gpu g;

void gpu_init(void) {
    memset(&g, 0, sizeof g);
    g.da_x2 = 1023;
    g.da_y2 = 511;
    g.disp_x1 = 0x200;
    g.disp_x2 = 0xC00;
    g.disp_y1 = 0x10;
    g.disp_y2 = 0x100;
}

/* ------------------------------------------------------------ primitives */

static inline uint16_t rgb15(uint32_t c) {
    return (uint16_t)(((c >> 3) & 0x1F) | (((c >> 11) & 0x1F) << 5) | (((c >> 19) & 0x1F) << 10));
}

static int sx(uint32_t w) { return ((int32_t)(w << 21) >> 21); }   /* 11-bit signed */
static int sy(uint32_t w) { return ((int32_t)(w << 5) >> 21); }

/* RAM address each command word came from (0 = written to the port), so
 * vertices the GTE produced can be looked up with their fractions. */
uint32_t g_gp0_src;
static uint32_t cmd_src[16];

static void set_texpage(uint32_t v) {
    g.texpage = v;
    g.tex_x = (v & 0xF) * 64;
    g.tex_y = ((v >> 4) & 1) * 256;
    g.abr = (v >> 5) & 3;
    g.tex_depth = (v >> 7) & 3;
    g.dither = (v >> 9) & 1;
    g.draw_display = (v >> 10) & 1;
    g.tex_disable = (v >> 11) & 1;
    g.rect_flip_x = (v >> 12) & 1;
    g.rect_flip_y = (v >> 13) & 1;
}

static int poly_words(uint32_t c) {
    int verts = (c & 0x08000000) ? 4 : 3;
    int gour = (c >> 28) & 1, tex = (c >> 26) & 1;
    return 1 + verts * (1 + tex) + (gour ? verts - 1 : 0);
}

static int rect_words(uint32_t c) { return 2 + (((c >> 26) & 1) ? 1 : 0) + ((((c >> 27) & 3) == 0) ? 1 : 0); }

static void base_prim(RPrim *p, uint32_t c) {
    memset(p, 0, sizeof *p);
    p->semi = (c >> 25) & 1;
    p->raw = (c >> 24) & 1;
    p->abr = g.abr;
    p->dither = g.dither;
    p->tx = g.tex_x;
    p->ty = g.tex_y;
    p->depth = g.tex_depth;
    p->twmx = g.tw_mask_x; p->twmy = g.tw_mask_y; p->twox = g.tw_off_x; p->twoy = g.tw_off_y;
    p->dx1 = g.da_x1; p->dy1 = g.da_y1; p->dx2 = g.da_x2; p->dy2 = g.da_y2;
    p->mask_set = g.mask_set;
    p->mask_check = g.mask_check;
}

/* Widescreen: 2D drawn into the visible buffer is squeezed toward the
 * middle so it keeps its shape when the picture is stretched to 16:9 (the
 * GTE already squeezes the 3D scene, see gte.c). */
static int squeeze_2d(void) {
    /* a frame buffer: as wide as the screen and where the picture shown
     * is (the frame buffers sit side by side in x with the display). Not
     * images the game composes off screen, like the HUD's weapon picture */
    return g_widescreen && (g.da_x2 - g.da_x1) >= 255 && g.da_x1 == g.disp_x;
}
/* 2D spanning the whole width (letterbox bars, fades, backdrops) stays
 * full width: stretched to 16:9 it still covers the screen. */
static int full_width(float x0, float x1) {
    return x0 <= (float)g.da_x1 + 4 && x1 >= (float)g.da_x2 - 3;
}
/* 2D textured from the frame buffers (motion blur, screen transitions)
 * copies the picture onto itself: it must stay 1:1, not squeezed. */
static int overlaps(int ax0, int ay0, int ax1, int ay1, int bx0, int by0, int bx1, int by1) {
    return ax0 <= bx1 && bx0 <= ax1 && ay0 <= by1 && by0 <= ay1;
}
static int samples_screen(const RPrim *p, int umin, int umax, int vmin, int vmax) {
    if (!p->textured) return 0;
    int sh = p->depth == 0 ? 2 : p->depth == 1 ? 1 : 0;
    int x0 = p->tx + (umin >> sh), x1 = p->tx + (umax >> sh), y0 = p->ty + vmin, y1 = p->ty + vmax;
    int w = g.da_x2 - g.da_x1, h = g.da_y2 - g.da_y1;
    return overlaps(x0, y0, x1, y1, g.da_x1, g.da_y1, g.da_x2, g.da_y2) ||
           overlaps(x0, y0, x1, y1, g.disp_x, g.disp_y, g.disp_x + w, g.disp_y + h);
}
/* Widescreen needs 3D (already squeezed by the GTE) told apart from 2D
 * (HUD, text: squeezed here). A primitive is 3D if one of its corners is
 * the very word the GTE stored at that address, or if every corner is a
 * point the GTE projected (packets the game copied from elsewhere). */
int gte_stored_sxy(uint32_t addr, uint32_t word);
static int corner_stored(int k) { return gte_stored_sxy(cmd_src[k], g.cmd[k]); }
static float squeeze_x(float x) {
    /* toward the middle of the draw area (not off_x + 160: some 2D, like
     * the reticle, is drawn with the offset at the screen centre) */
    float cx = (float)(g.da_x1 + g.da_x2 + 1) * 0.5f;
    return cx + (x - cx) * 0.75f;
}
/* Squeezed 2D keeps the draw area's clipping, squeezed the same way: the
 * game draws some HUD work just past the right edge of the screen (the
 * heartbeat graph), where in 4:3 it is clipped away. */
static void squeeze_clip(RPrim *p) {
    p->dx1 = (int)ceilf(squeeze_x((float)g.da_x1));
    p->dx2 = (int)floorf(squeeze_x((float)(g.da_x2 + 1))) - 1;
}

/* Vertex k of the command: position (with the GTE's fractions if this word
 * is one the GTE stored), plus drawing offset. Returns 1 if it came from the GTE. */
static int vertex_exact;                         /* the last vertex_pos found its corner by address */
static int vertex_pos(int k, float *x, float *y, float *z) {
    uint32_t w = g.cmd[k];
    float fx, fy, fz = 0;
    int got = gte_precise(cmd_src[k], w, &fx, &fy, &fz);
    vertex_exact = got == 2;
    *z = got ? fz : 0;
    if (g_subpixel && got) {
        *x = fx + (float)g.off_x;
        *y = fy + (float)g.off_y;
        return 1;
    }
    *x = (float)(sx(w) + g.off_x);
    *y = (float)(sy(w) + g.off_y);
    return got != 0;
}

static void exec_poly(void) {
    uint32_t c = g.cmd[0];
    int quad = (c >> 27) & 1, gour = (c >> 28) & 1, tex = (c >> 26) & 1;
    int nv = quad ? 4 : 3;
    RPrim p;
    base_prim(&p, c);
    p.kind = RP_TRI;
    p.gouraud = gour;
    p.textured = tex;
    RVtx v[4];
    int k = 0, ngte = 0, stored = 0, nexact = 0;
    uint32_t color = c & 0xFFFFFF;
    for (int i = 0; i < nv; i++) {
        if (i > 0 && gour) color = g.cmd[k] & 0xFFFFFF;
        if (i == 0 || gour) k++;
        stored |= corner_stored(k);
        ngte += vertex_pos(k++, &v[i].x, &v[i].y, &v[i].z);
        nexact += vertex_exact;
        v[i].r = color & 0xFF;
        v[i].g = (color >> 8) & 0xFF;
        v[i].b = (color >> 16) & 0xFF;
        v[i].u = v[i].v = 0;
        if (tex) {
            uint32_t uv = g.cmd[k++];
            v[i].u = uv & 0xFF;
            v[i].v = (uv >> 8) & 0xFF;
            if (i == 0) { p.clut_x = ((uv >> 16) & 0x3F) * 16; p.clut_y = (uv >> 22) & 0x1FF; }
            if (i == 1) {
                set_texpage((g.texpage & ~0x9FFu) | ((uv >> 16) & 0x9FF));
                p.tx = g.tex_x; p.ty = g.tex_y; p.depth = g.tex_depth; p.abr = g.abr; p.dither = g.dither;
            }
        }
    }
    if (squeeze_2d() && !stored && ngte < nv) {
        float lo = v[0].x, hi = v[0].x;
        for (int i = 1; i < nv; i++) { lo = fminf(lo, v[i].x); hi = fmaxf(hi, v[i].x); }
        int umin = 255, umax = 0, vmin = 255, vmax = 0;
        for (int i = 0; i < nv; i++) {
            umin = v[i].u < umin ? v[i].u : umin; umax = v[i].u > umax ? v[i].u : umax;
            vmin = v[i].v < vmin ? v[i].v : vmin; vmax = v[i].v > vmax ? v[i].v : vmax;
        }
        if (!full_width(lo, hi) && !samples_screen(&p, umin, umax, vmin, vmax)) {
            for (int i = 0; i < nv; i++) v[i].x = squeeze_x(v[i].x);
            squeeze_clip(&p);
            static int mark = -1;            /* RT_WS_MARK: tint what the 2D squeeze touched */
            if (mark < 0) mark = getenv("RT_WS_MARK") != NULL;
            if (mark) { p.textured = 0; p.gouraud = 0; for (int i = 0; i < nv; i++) { v[i].r = 255; v[i].g = 0; v[i].b = 255; } }
        }
    }
    {
        static int ml = -1;
        if (ml < 0) ml = getenv("RT_MIXLOG") != NULL;
        if (ml && ngte > 0 && ngte < nv) {
            char b[256]; int n = 0;
            for (int i = 0; i < nv; i++) n += snprintf(b + n, sizeof b - n, " (%.2f,%.2f z%.0f)", v[i].x, v[i].y, v[i].z);
            rt_log("mix: nv %d ngte %d tex %d tp %d,%d%s", nv, ngte, tex, p.tx, p.ty, b);
        }
    }
    /* every corner projected by the GTE, with a depth: 3D, depth-tested */
    p.zt = ngte == nv;
    for (int i = 0; i < nv; i++) if (v[i].z <= 0) p.zt = 0;
    if (p.zt && nexact == nv) p.zt = 2;              /* every corner surely its own: depths fit for perspective */
    p.v[0] = v[0]; p.v[1] = v[1]; p.v[2] = v[2];
    render_prim(&p);
    if (quad) {
        p.v[0] = v[1]; p.v[1] = v[2]; p.v[2] = v[3];
        render_prim(&p);
    }
}

static void exec_rect(void) {
    uint32_t c = g.cmd[0];
    int tex = (c >> 26) & 1;
    int size = (c >> 27) & 3;
    int k = 1;
    RPrim p;
    base_prim(&p, c);
    p.kind = RP_RECT;
    p.textured = tex;
    p.dither = 0;
    uint32_t pos = g.cmd[k++];
    p.v[0].x = (float)(sx(pos) + g.off_x);
    p.v[0].y = (float)(sy(pos) + g.off_y);
    p.v[0].r = c & 0xFF;
    p.v[0].g = (c >> 8) & 0xFF;
    p.v[0].b = (c >> 16) & 0xFF;
    if (tex) {
        uint32_t uv = g.cmd[k++];
        p.v[0].u = uv & 0xFF;
        p.v[0].v = (uv >> 8) & 0xFF;
        p.clut_x = ((uv >> 16) & 0x3F) * 16;
        p.clut_y = (uv >> 22) & 0x1FF;
    }
    switch (size) {
    case 0: { uint32_t s = g.cmd[k++]; p.w = s & 0x3FF; p.h = (s >> 16) & 0x1FF; break; }
    case 1: p.w = p.h = 1; break;
    case 2: p.w = p.h = 8; break;
    default: p.w = p.h = 16; break;
    }
    p.flipx = g.rect_flip_x;
    p.flipy = g.rect_flip_y;
    if (!squeeze_2d() || corner_stored(1) || full_width(p.v[0].x, p.v[0].x + (float)p.w - 1) ||
        samples_screen(&p, p.v[0].u, p.v[0].u + p.w - 1, p.v[0].v, p.v[0].v + p.h - 1)) {
        render_prim(&p);
        return;
    }
    /* widescreen: draw the sprite squeezed (as one rectangle: as two
     * triangles their texel columns met unevenly along the diagonal, and
     * the HUD pictures showed broken columns and stray lines) */
    squeeze_clip(&p);
    p.sqx0 = squeeze_x(p.v[0].x);
    p.sqx1 = squeeze_x(p.v[0].x + (float)p.w);
    render_prim(&p);
}

static void draw_line(uint32_t c, uint32_t ca, uint32_t pa, uint32_t cb, uint32_t pb, int gour) {
    RPrim p;
    base_prim(&p, c);
    p.kind = RP_LINE;
    p.gouraud = gour;
    p.raw = 1;
    p.v[0] = (RVtx){(float)(sx(pa) + g.off_x), (float)(sy(pa) + g.off_y), (int)(ca & 0xFF), (int)((ca >> 8) & 0xFF), (int)(ca >> 16), 0, 0};
    p.v[1] = (RVtx){(float)(sx(pb) + g.off_x), (float)(sy(pb) + g.off_y), (int)(cb & 0xFF), (int)((cb >> 8) & 0xFF), (int)(cb >> 16), 0, 0};
    if (squeeze_2d() && !corner_stored(1) && !corner_stored(gour ? 3 : 2)) { p.v[0].x = squeeze_x(p.v[0].x); p.v[1].x = squeeze_x(p.v[1].x); squeeze_clip(&p); }
    render_prim(&p);
}

static void fill(void) {
    uint32_t c = g.cmd[0], p = g.cmd[1], s = g.cmd[2];
    int x = p & 0x3F0, y = (p >> 16) & 0x1FF;
    int w = ((s & 0x3FF) + 15) & ~15, h = (s >> 16) & 0x1FF;
    render_fill(x, y, w, h, rgb15(c));
}

static void vram_copy(void) {
    uint32_t s = g.cmd[1], d = g.cmd[2], sz = g.cmd[3];
    int sx0 = s & 0x3FF, sy0 = (s >> 16) & 0x1FF, dx = d & 0x3FF, dy = (d >> 16) & 0x1FF;
    int w = ((sz & 0x3FF) - 1) & 0x3FF, h = (((sz >> 16) & 0x1FF) - 1) & 0x1FF;
    render_copy(sx0, sy0, dx, dy, w + 1, h + 1, g.mask_set, g.mask_check);
}

static void start_xfer(int in) {
    uint32_t p = g.cmd[1], s = g.cmd[2];
    g.xf_x = p & 0x3FF;
    g.xf_y = (p >> 16) & 0x1FF;
    g.xf_w = ((((s & 0xFFFF) - 1) & 0x3FF) + 1);
    g.xf_h = (((((s >> 16) & 0xFFFF) - 1) & 0x1FF) + 1);
    g.xf_i = 0;
    g.xf_n = g.xf_w * g.xf_h;
    if (in) {
        render_before_write(g.xf_x, g.xf_y, g.xf_w, g.xf_h);
        g.xfer_in = 1;
    } else {
        g.xfer_out = 1;
    }
}

static void xfer_pixel_in(uint16_t px) {
    int x = g.xf_x + g.xf_i % g.xf_w, y = g.xf_y + g.xf_i / g.xf_w;
    uint16_t *d = &g_vram[y & 511][x & 1023];
    if (!(g.mask_check && (*d & 0x8000)))
        *d = px | (g.mask_set ? 0x8000 : 0);
    g.xf_i++;
    if (g.xf_i >= g.xf_n) render_uploaded(g.xf_x, g.xf_y, g.xf_w, g.xf_h);
}

static void env(uint32_t v) {
    switch (v >> 24) {
    case 0xE1: set_texpage(v); break;
    case 0xE2:
        g.tw_mask_x = v & 0x1F; g.tw_mask_y = (v >> 5) & 0x1F;
        g.tw_off_x = (v >> 10) & 0x1F; g.tw_off_y = (v >> 15) & 0x1F;
        break;
    case 0xE3: g.da_x1 = v & 0x3FF; g.da_y1 = (v >> 10) & 0x1FF;
        { static int dl = -1; if (dl < 0) dl = getenv("RT_DISPLOG") != NULL; if (dl) rt_log("draw y %d", g.da_y1); }
        break;
    case 0xE4: {
        g.da_x2 = v & 0x3FF; g.da_y2 = (v >> 10) & 0x1FF;
        /* drawing moved to another area (the other buffer): a new frame,
         * so the depths there are stale */
        static int lx1 = -1, ly1, lx2, ly2;
        if (g.da_x1 != lx1 || g.da_y1 != ly1 || g.da_x2 != lx2 || g.da_y2 != ly2) {
            lx1 = g.da_x1; ly1 = g.da_y1; lx2 = g.da_x2; ly2 = g.da_y2;
            render_zclear(g.da_x1, g.da_y1, g.da_x2 - g.da_x1 + 1, g.da_y2 - g.da_y1 + 1);
        }
        break;
    }
    case 0xE5: g.off_x = sx(v); g.off_y = (int32_t)(v << 10) >> 21; break;
    case 0xE6: g.mask_set = v & 1; g.mask_check = (v >> 1) & 1; break;
    }
}

uint32_t g_gp0_count;

void gpu_write_gp0(uint32_t v) {
    g_gp0_count++;
    if (g.xfer_in) {
        xfer_pixel_in(v & 0xFFFF);
        if (g.xf_i < g.xf_n) xfer_pixel_in(v >> 16);
        if (g.xf_i >= g.xf_n) g.xfer_in = 0;
        return;
    }
    if (g.poly_line) {
        /* Polyline: vertices (and colours) until the terminator word. */
        if ((v & 0xF000F000) == 0x50005000 && g.ncmd >= 3) { g.poly_line = 0; g.ncmd = 0; return; }
        g.cmd[g.ncmd++] = v;
        uint32_t c = g.cmd[0];
        int gour = (c >> 28) & 1;
        int per = gour ? 2 : 1;
        if (g.ncmd == 1 + 2 * per) {
            uint32_t pa = gour ? g.cmd[1] : g.cmd[1], pb = gour ? g.cmd[3] : g.cmd[2];
            uint32_t ca = c & 0xFFFFFF, cb = gour ? g.cmd[2] & 0xFFFFFF : ca;
            draw_line(c, ca, pa, cb, pb, gour);
            /* last vertex becomes the first of the next segment */
            if (gour) { g.cmd[0] = (g.cmd[0] & 0xFF000000) | cb; g.cmd[1] = pb; g.ncmd = 2; }
            else { g.cmd[1] = pb; g.ncmd = 2; }
        }
        return;
    }
    if (g.ncmd == 0) {
        uint32_t op = v >> 24;
        g.cmd[0] = v;
        cmd_src[0] = g_gp0_src;
        g.ncmd = 1;
        if (op >= 0x20 && op < 0x40) g.need = poly_words(v);
        else if (op >= 0x40 && op < 0x60) {
            int gour = (v >> 28) & 1;
            if (v & 0x08000000) {           /* polyline */
                g.poly_line = 1;
                return;
            }
            g.need = gour ? 4 : 3;
        } else if (op >= 0x60 && op < 0x80) g.need = rect_words(v);
        else if (op == 0x02) g.need = 3;
        else if (op >= 0x80 && op < 0xA0) g.need = 4;
        else if (op >= 0xA0 && op < 0xC0) g.need = 3;
        else if (op >= 0xC0 && op < 0xE0) g.need = 3;
        else if (op >= 0xE1 && op <= 0xE6) { env(v); g.ncmd = 0; return; }
        else if (op == 0x1F) { g.irq = 1; irq_raise(IRQ_GPU); g.ncmd = 0; return; }
        else { g.ncmd = 0; return; }        /* nop, cache clear, unknown */
    } else {
        cmd_src[g.ncmd] = g_gp0_src;
        g.cmd[g.ncmd++] = v;
    }
    if (g.ncmd < g.need) return;
    uint32_t op = g.cmd[0] >> 24;
    g.ncmd = 0;
    {
        static int on = -1;
        if (on < 0) on = getenv("RT_OP_LOG") != NULL;
        if (on && g_frame >= 2980 && g_frame < 3010) rt_log("op %02X src %08X xy %d,%d", op, cmd_src[0], sx(g.cmd[1]), sy(g.cmd[1]));
    }
    if (op >= 0x20 && op < 0x40) exec_poly();
    else if (op >= 0x40 && op < 0x60) {
        int gour = (g.cmd[0] >> 28) & 1;
        uint32_t ca = g.cmd[0] & 0xFFFFFF, pa = g.cmd[1];
        uint32_t cb = gour ? g.cmd[2] & 0xFFFFFF : ca, pb = gour ? g.cmd[3] : g.cmd[2];
        draw_line(g.cmd[0], ca, pa, cb, pb, gour);
    } else if (op >= 0x60 && op < 0x80) exec_rect();
    else if (op == 0x02) fill();
    else if (op >= 0x80 && op < 0xA0) vram_copy();
    else if (op >= 0xA0 && op < 0xC0) start_xfer(1);
    else if (op >= 0xC0 && op < 0xE0) start_xfer(0);
}

uint32_t gpu_read(void) {
    if (g.xfer_out) {
        uint32_t v = 0;
        for (int k = 0; k < 2 && g.xf_i < g.xf_n; k++) {
            int x = g.xf_x + g.xf_i % g.xf_w, y = g.xf_y + g.xf_i / g.xf_w;
            v |= (uint32_t)g_vram[y & 511][x & 1023] << (16 * k);
            g.xf_i++;
        }
        if (g.xf_i >= g.xf_n) g.xfer_out = 0;
        return v;
    }
    return g.gpuread;
}

/* ------------------------------------------------------------ GP1 */

void gpu_write_gp1(uint32_t v) {
    uint32_t p = v & 0xFFFFFF;
    switch ((v >> 24) & 0x3F) {
    case 0x00:
        gpu_init();
        g.disp_enabled = 0;
        break;
    case 0x01: g.ncmd = 0; g.xfer_in = g.xfer_out = 0; g.poly_line = 0; break;
    case 0x02: g.irq = 0; break;
    case 0x03: g.disp_enabled = !(p & 1); break;
    case 0x04: g.dma_dir = p & 3; break;
    case 0x05: g.disp_x = p & 0x3FE; g.disp_y = (p >> 10) & 0x1FF;
        { static int dl = -1; if (dl < 0) dl = getenv("RT_DISPLOG") != NULL; if (dl) rt_log("disp y %d", g.disp_y); }
        break;
    case 0x06: g.disp_x1 = p & 0xFFF; g.disp_x2 = (p >> 12) & 0xFFF; break;
    case 0x07: g.disp_y1 = p & 0x3FF; g.disp_y2 = (p >> 10) & 0x3FF; break;
    case 0x08: g.disp_mode = p; break;
    case 0x10:
        switch (p & 7) {
        case 2: g.gpuread = (g.tw_mask_x) | (g.tw_mask_y << 5) | (g.tw_off_x << 10) | (g.tw_off_y << 15); break;
        case 3: g.gpuread = g.da_x1 | (g.da_y1 << 10); break;
        case 4: g.gpuread = g.da_x2 | (g.da_y2 << 10); break;
        case 5: g.gpuread = (g.off_x & 0x7FF) | ((g.off_y & 0x7FF) << 11); break;
        case 7: g.gpuread = 2; break;
        }
        break;
    }
}

uint32_t gpu_status(void) {
    uint32_t s = g.texpage & 0x7FF;
    s |= (uint32_t)g.mask_set << 11 | (uint32_t)g.mask_check << 12;
    s |= (uint32_t)g.tex_disable << 15;
    uint32_t m = g.disp_mode;
    s |= ((m >> 6) & 1) << 16 | (m & 3) << 17 | ((m >> 2) & 1) << 19 | ((m >> 3) & 1) << 20;
    s |= ((m >> 4) & 1) << 21 | ((m >> 5) & 1) << 22;
    s |= (uint32_t)(!g.disp_enabled) << 23;
    s |= (uint32_t)g.irq << 24;
    s |= 1u << 26 | 1u << 28;                       /* ready for commands / DMA */
    if (g.xfer_out) s |= 1u << 27;
    s |= (uint32_t)g.dma_dir << 29;
    switch (g.dma_dir) {
    case 1: case 2: s |= 1u << 25; break;
    case 3: if (g.xfer_out) s |= 1u << 25; break;
    }
    if (g.field) s |= 1u << 31;
    return s;
}

void gpu_vblank(void) {
    static int trace = -1;
    if (trace < 0) trace = getenv("RT_TRACE_GPU") != NULL;
    if (trace && g_gp0_count) rt_log("GPU: %u GP0 words this frame, display %s", g_gp0_count, g.disp_enabled ? "on" : "off");
    g_gp0_count = 0;
    if ((g.disp_mode >> 5) & 1) g.field ^= 1;
    else g.field = !g.field;
}

void gpu_display(GpuDisplay *d) {
    uint32_t m = g.disp_mode;
    static const int widths[4] = {256, 320, 512, 640};
    int w = (m & 0x40) ? 368 : widths[m & 3];
    int h = ((m >> 2) & 1) && ((m >> 5) & 1) ? 480 : 240;
    int lines = g.disp_y2 - g.disp_y1;
    if (lines > 0 && lines < 240 && h == 240) h = lines;
    d->x = g.disp_x;
    d->y = g.disp_y;
    d->w = w;
    d->h = h;
    d->rgb24 = (m >> 4) & 1;
    d->enabled = g.disp_enabled;
}

void gpu_state(StateIO *io) {
    ss_module(io, "gpu");
    SS(io, g_vram);
    SS(io, g);
    SS(io, cmd_src);
}

/* Write an image into VRAM from the PC side (as a CPU upload would). */
void gpu_load_image(int x, int y, int w, int h, const uint16_t *px) {
    render_before_write(x, y, w, h);
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) g_vram[(y + j) & 511][(x + i) & 1023] = px[j * w + i];
    render_uploaded(x, y, w, h);
}
