// The high-resolution picture on the graphics card (render_gpu.h): one
// thread per pixel runs the operations binned to its 16x16 tile, in the
// queue's order, as render.c's raster_tri / raster_rect / run_ops do for
// that pixel. Word offsets and flags: render_gpu.h.

#define OPW 128
#define GK_TRI 0
#define GK_RECT 1
#define GK_FILL 2
#define GK_ZCLEAR 3
#define GK_UPLOAD 4

#define GO_KIND 0
#define GO_FLAGS 1
#define GO_TEX 2
#define GO_CLUT 3
#define GO_TWIN 4
#define GO_X0 5
#define GO_X1 6
#define GO_Y0 7
#define GO_Y1 8
#define GO_UV 9
#define GO_ID 10
#define GO_COLOR 11
#define GO_DU 12
#define GO_DV 13
#define GO_DR 14
#define GO_DG 15
#define GO_DB 16
#define GO_RX 17
#define GO_RY 18
#define GO_RW 19
#define GO_RH 20
#define GO_U0V0 21
#define GO_POOL 22
#define GO_FILLC 23
#define GO_BIAS 24
#define GO_EX 28
#define GO_EY 34
#define GO_EC 40
#define GO_MB 46
#define GO_PL 52
#define GO_SQ0 124
#define GO_SQK 126

#define GF_TEX (1u << 0)
#define GF_RAW (1u << 1)
#define GF_SEMI (1u << 2)
#define GF_MSET (1u << 7)
#define GF_MCHK (1u << 8)
#define GF_GOUR (1u << 9)
#define GF_SMOOTH (1u << 11)
#define GF_PERSP (1u << 12)
#define GF_PCOL (1u << 13)
#define GF_ZUSE (1u << 14)
#define GF_ZTEST (1u << 15)
#define GF_ZWRITE (1u << 16)
#define GF_GUARD (1u << 17)
#define GF_MARGIN (1u << 18)
#define GF_FLIPX (1u << 19)
#define GF_FLIPY (1u << 20)
#define GF_SQ (1u << 21)
#define GF_ZT (1u << 22)
#define GF_BIGFILL (1u << 23)

cbuffer Params : register(b0) {
    uint S, W, H, true_color;
    float depth_tol, cross_tol, margin_depth, depth_rel;
    uint far_page, depth_any, margin_any, ntiles;
    int cp_sx, cp_sy, cp_dx, cp_dy;              // copies (high-res pixels)
    uint cp_w, cp_h, cp_mset, cp_mchk;
    uint rd_x, rd_y, rd_w, rd_h;                 // reads (high-res pixels)
};

StructuredBuffer<uint> ops : register(t0);
StructuredBuffer<uint> vram : register(t1);      // the PS1's VRAM, two pixels a word
StructuredBuffer<uint> pool : register(t2);      // uploads' pixels, one a word
StructuredBuffer<uint4> tiles : register(t3);    // tile x, y, first bin, bins
StructuredBuffer<uint> bins : register(t4);      // op indices
RWStructuredBuffer<uint> hl : register(u0);      // high-res pixel | 24-bit colour low bits << 16
RWStructuredBuffer<uint> bare : register(u1);
RWStructuredBuffer<float> zb : register(u2);
RWStructuredBuffer<uint> zid : register(u3);
RWStructuredBuffer<uint> tmp : register(u4);     // copies, reads

// this pixel, while the ops run
static uint hv, lw, br, idv;
static float zv;
static uint ob, fl;                              // the op: base word, flags

uint V(int x, int y) {
    uint i = (uint)((y & 511) * 1024 + (x & 1023));
    return (vram[i >> 1] >> ((i & 1) * 16)) & 0xFFFF;
}
uint O(uint k) { return ops[ob + k]; }
int OI(uint k) { return asint(ops[ob + k]); }
double OD(uint k) { return asdouble(ops[ob + k], ops[ob + k + 1]); }
double PLV(uint k, double x, double y) {         // plane k at (x, y)
    uint o = GO_PL + k * 6;
    return OD(o) * x + OD(o + 2) * y + OD(o + 4);
}
int floordiv(int a, int b) { return a >= 0 ? a / b : -((-a + b - 1) / b); }
int imod(int a, int m) { int r = a % m; return r < 0 ? r + m : r; }

uint texel(int u, int v) {
    uint tw = O(GO_TWIN);
    int twmx = tw & 0xFF, twmy = (tw >> 8) & 0xFF, twox = (tw >> 16) & 0xFF, twoy = tw >> 24;
    u &= 0xFF;
    v &= 0xFF;
    u = (u & ~(twmx * 8)) | ((twox & twmx) * 8);
    v = (v & ~(twmy * 8)) | ((twoy & twmy) * 8);
    uint t = O(GO_TEX), c = O(GO_CLUT);
    int tx = t & 0xFFFF, ty = t >> 16, cx = c & 0xFFFF, cy = c >> 16;
    int y = (ty + v) & 511;
    uint depth = (fl >> 5) & 3;
    if (depth == 0) {
        uint w = V(tx + (u >> 2), y);
        return V(cx + ((w >> ((u & 3) * 4)) & 0xF), cy);
    }
    if (depth == 1) {
        uint w = V(tx + (u >> 1), y);
        return V(cx + ((w >> ((u & 1) * 8)) & 0xFF), cy);
    }
    return V(tx + u, y);
}

int clamp8(int v) { return v < 0 ? 0 : v > 255 ? 255 : v; }
int expand5(int c) { return (c << 3) | (c >> 2); }

void put(uint c, bool semi) {
    if ((fl & GF_MCHK) && (hv & 0x8000)) return;
    if (semi) {
        int br5 = hv & 31, bg = (hv >> 5) & 31, bb = (hv >> 10) & 31;
        int fr = c & 31, fg = (c >> 5) & 31, fb = (c >> 10) & 31;
        int r, g, b;
        uint abr = (fl >> 3) & 3;
        if (abr == 0) { r = (br5 + fr) >> 1; g = (bg + fg) >> 1; b = (bb + fb) >> 1; }
        else if (abr == 1) { r = br5 + fr; g = bg + fg; b = bb + fb; }
        else if (abr == 2) { r = br5 - fr; g = bg - fg; b = bb - fb; }
        else { r = br5 + (fr >> 2); g = bg + (fg >> 2); b = bb + (fb >> 2); }
        r = clamp(r, 0, 31); g = clamp(g, 0, 31); b = clamp(b, 0, 31);
        c = (c & 0x8000) | r | (g << 5) | (b << 10);
    }
    hv = c | ((fl & GF_MSET) ? 0x8000 : 0);
    if (!semi) br = 0;
    lw = 0;
    if (!(fl & GF_ZT)) zv = -1;
}

void put8(int r, int g, int b, bool stp, bool semi) {
    if ((fl & GF_MCHK) && (hv & 0x8000)) return;
    r = clamp8(r); g = clamp8(g); b = clamp8(b);
    if (semi) {
        int c5[3] = {(int)(hv & 31), (int)((hv >> 5) & 31), (int)((hv >> 10) & 31)};
        int bk[3];
        [unroll] for (int k = 0; k < 3; k++)
            bk[k] = (lw & 0x8000) ? (c5[k] << 3) | ((lw >> (3 * k)) & 7) : (c5[k] << 3) | (c5[k] >> 2);
        uint abr = (fl >> 3) & 3;
        if (abr == 0) { r = (bk[0] + r) >> 1; g = (bk[1] + g) >> 1; b = (bk[2] + b) >> 1; }
        else if (abr == 1) { r = bk[0] + r; g = bk[1] + g; b = bk[2] + b; }
        else if (abr == 2) { r = bk[0] - r; g = bk[1] - g; b = bk[2] - b; }
        else { r = bk[0] + (r >> 2); g = bk[1] + (g >> 2); b = bk[2] + (b >> 2); }
        r = clamp8(r); g = clamp8(g); b = clamp8(b);
    }
    hv = (stp ? 0x8000 : 0) | (r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10) | ((fl & GF_MSET) ? 0x8000 : 0);
    lw = 0x8000 | (r & 7) | ((g & 7) << 3) | ((b & 7) << 6);
    if (!semi) br = 0;
    if (!(fl & GF_ZT)) zv = -1;
}

// render.c shade(), without the dither (only at 1x)
void shade(int r, int g, int b, int u, int v) {
    if (true_color) {
        if (fl & GF_TEX) {
            uint t = texel(u, v);
            if (!t) return;
            int tr = expand5(t & 31), tg = expand5((t >> 5) & 31), tb = expand5((t >> 10) & 31);
            if (!(fl & GF_RAW)) { tr = (tr * r + 64) >> 7; tg = (tg * g + 64) >> 7; tb = (tb * b + 64) >> 7; }
            put8(tr, tg, tb, (t & 0x8000) != 0, (fl & GF_SEMI) && (t & 0x8000));
        } else
            put8(r, g, b, false, (fl & GF_SEMI) != 0);
        return;
    }
    if (fl & GF_TEX) {
        uint t = texel(u, v);
        if (!t) return;
        if (!(fl & GF_RAW)) {
            int tr = (t & 31) * r, tg = ((t >> 5) & 31) * g, tb = ((t >> 10) & 31) * b;
            tr = tr < 0 ? 0 : tr >> 7; tg = tg < 0 ? 0 : tg >> 7; tb = tb < 0 ? 0 : tb >> 7;
            tr = min(tr, 31); tg = min(tg, 31); tb = min(tb, 31);
            t = (t & 0x8000) | tr | (tg << 5) | (tb << 10);
        }
        put(t, (fl & GF_SEMI) && (t & 0x8000));
    } else
        put((r >> 3) | ((g >> 3) << 5) | ((b >> 3) << 10), (fl & GF_SEMI) != 0);
}

static const uint bayer4[16] = {0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5};

bool smooth_texel(float U, float V2, float4 lo, out float3 rgb, out bool stp) {
    rgb = 0;
    stp = false;
    uint n = texel((int)U, (int)V2);
    if (!n) return false;
    float x_lo = lo.x + 0.5f, x_hi = lo.y - 0.5f, y_lo = lo.z + 0.5f, y_hi = lo.w - 0.5f;
    if (x_hi >= x_lo) U = U < x_lo ? x_lo : U > x_hi ? x_hi : U;
    if (y_hi >= y_lo) V2 = V2 < y_lo ? y_lo : V2 > y_hi ? y_hi : V2;
    U -= 0.5f; V2 -= 0.5f;
    int u0 = (int)floor(U), v0 = (int)floor(V2);
    float au = U - (float)u0, av = V2 - (float)v0;
    uint q[4] = {texel(u0, v0), texel(u0 + 1, v0), texel(u0, v0 + 1), texel(u0 + 1, v0 + 1)};
    float wt[4] = {(1 - au) * (1 - av), au * (1 - av), (1 - au) * av, au * av};
    float r = 0, g = 0, b = 0, sum = 0;
    [unroll] for (int i = 0; i < 4; i++) {
        if (!q[i]) continue;
        r += wt[i] * (float)(q[i] & 31);
        g += wt[i] * (float)((q[i] >> 5) & 31);
        b += wt[i] * (float)((q[i] >> 10) & 31);
        sum += wt[i];
    }
    if (sum <= 0) { r = (float)(n & 31); g = (float)((n >> 5) & 31); b = (float)((n >> 10) & 31); sum = 1; }
    const float k = 255.0f / 31.0f;
    rgb = float3(r / sum * k, g / sum * k, b / sum * k);
    stp = (n & 0x8000) != 0;
    return true;
}

// render.c shade_hd() for the game's own textures (no texture packs here)
void shade_smooth(int r, int g, int b, float U, float V2, float4 lo, int x, int y) {
    float3 c;
    bool stp;
    if (!smooth_texel(U, V2, lo, c, stp)) return;
    if ((fl & GF_SEMI) && !stp && (texel((int)U, (int)V2) & 0x8000)) stp = true;
    if (!(fl & GF_RAW)) c *= float3((float)r, (float)g, (float)b) * (1.0f / 128);
    if (true_color) {
        put8((int)(c.x + 0.5f), (int)(c.y + 0.5f), (int)(c.z + 0.5f), stp, (fl & GF_SEMI) && stp);
        return;
    }
    float dt = ((float)bayer4[(y & 3) * 4 + (x & 3)] + 0.5f) * (1.0f / 16) - 0.5f;
    int o0 = clamp((int)(c.x * (31.0f / 255.0f) + dt), 0, 31);
    int o1 = clamp((int)(c.y * (31.0f / 255.0f) + dt), 0, 31);
    int o2 = clamp((int)(c.z * (31.0f / 255.0f) + dt), 0, 31);
    put((stp ? 0x8000 : 0) | o0 | (o1 << 5) | (o2 << 10), (fl & GF_SEMI) && stp);
}

void tri(int x, int y) {
    int x0 = OI(GO_X0);
    if (x < x0 || x > OI(GO_X1) || y < OI(GO_Y0) || y > OI(GO_Y1)) return;
    double X = (double)x * 256.0, Y = (double)y * 256.0;
    double w0 = OD(GO_EX) * X + OD(GO_EY) * Y + OD(GO_EC);
    double w1 = OD(GO_EX + 2) * X + OD(GO_EY + 2) * Y + OD(GO_EC + 2);
    double w2 = OD(GO_EX + 4) * X + OD(GO_EY + 4) * Y + OD(GO_EC + 4);
    int core = (w0 + OI(GO_BIAS) >= 0 && w1 + OI(GO_BIAS + 1) >= 0 && w2 + OI(GO_BIAS + 2) >= 0) ? 1 : 0;
    double wz = (fl & GF_ZUSE) ? PLV(5, x, y) : 0;
    if (!core && (fl & GF_MARGIN) && w0 + OD(GO_MB) >= 0 && w1 + OD(GO_MB + 2) >= 0 && w2 + OD(GO_MB + 4) >= 0 &&
        (!(fl & GF_GUARD) || margin_any || zv == 0 || 1.0f / zv - 1.0f / (float)wz > margin_depth))
        core = 2;
    if (!core) return;
    bool persp = (fl & GF_PERSP) != 0, pcol = (fl & GF_PCOL) != 0, tex = (fl & GF_TEX) != 0;
    double qwv = (persp || pcol) ? PLV(6, x, y) : 0;
    double iz = (persp || pcol) && qwv > 0 ? 1.0 / qwv : 0;
    int u = 0, vv = 0;
    if (tex) {
        if (persp && qwv > 0) {
            u = (int)(PLV(7, x, y) * iz * 65536.0) + 64;
            vv = (int)(PLV(8, x, y) * iz * 65536.0) + 64;
        } else {
            u = (int)(PLV(3, x0, y) * 65536.0) + 64 + (x - x0) * OI(GO_DU);
            vv = (int)(PLV(4, x0, y) * 65536.0) + 64 + (x - x0) * OI(GO_DV);
        }
    }
    uint uvb = O(GO_UV);
    int umin = uvb & 0xFF, umax = (uvb >> 8) & 0xFF, vmin = (uvb >> 16) & 0xFF, vmax = uvb >> 24;
    int tu = clamp(u >> 16, umin, umax), tv = clamp(vv >> 16, vmin, vmax);
    bool semi = (fl & GF_SEMI) != 0, zwrite = (fl & GF_ZWRITE) != 0, ztest = (fl & GF_ZTEST) != 0;
    uint id = O(GO_ID);
    if (zwrite && !ztest && core == 1 && !semi && (!tex || texel(tu, tv))) { zv = (float)wz; idv = id; }
    if (ztest) {
        float zs = zv;
        if ((float)wz > zs * 0.88f && (float)wz < zs) {
            float behind = 1.0f / (float)wz - 1.0f / zs;
            float tol = depth_rel / zs;
            if (tol < depth_tol) tol = depth_tol;
            if (behind > tol && (idv == id || depth_any)) return;
            if (cross_tol > 0 && !semi && behind > cross_tol && behind > tol) return;
        } else if (far_page && (float)wz <= zs * 0.88f && !semi && id != 0xFFFFFFFFu &&
                   idv != 0xFFFFFFFFu && ((idv ^ id) & 0x1F) == 0)
            return;
        if (zwrite && core == 1 && !semi && (!tex || texel(tu, tv))) { zv = (float)wz; idv = id; }
    }
    uint col = O(GO_COLOR);
    int cr = col & 0xFF, cg = (col >> 8) & 0xFF, cb = (col >> 16) & 0xFF;
    if (pcol && qwv > 0) {
        cr = clamp8((int)(PLV(9, x, y) * iz + 0.5));
        cg = clamp8((int)(PLV(10, x, y) * iz + 0.5));
        cb = clamp8((int)(PLV(11, x, y) * iz + 0.5));
    } else if (fl & GF_GOUR) {
        cr = clamp8(((int)(PLV(0, x0, y) * 65536.0) + 32768 + (x - x0) * OI(GO_DR)) >> 16);
        cg = clamp8(((int)(PLV(1, x0, y) * 65536.0) + 32768 + (x - x0) * OI(GO_DG)) >> 16);
        cb = clamp8(((int)(PLV(2, x0, y) * 65536.0) + 32768 + (x - x0) * OI(GO_DB)) >> 16);
    }
    if (fl & GF_SMOOTH) {
        float U = (float)u * (1.0f / 65536), V2 = (float)vv * (1.0f / 65536);
        float ulo = (float)umin, uhi = (float)umax + 0.999f, vlo = (float)vmin, vhi = (float)vmax + 0.999f;
        U = U < ulo ? ulo : U > uhi ? uhi : U;
        V2 = V2 < vlo ? vlo : V2 > vhi ? vhi : V2;
        shade_smooth(cr, cg, cb, U, V2, float4(ulo, (float)umax + 1, vlo, (float)vmax + 1), x, y);
    } else
        shade(cr, cg, cb, tu, tv);
}

void rect(int x, int y) {
    if (x < OI(GO_X0) || x > OI(GO_X1) || y < OI(GO_Y0) || y > OI(GO_Y1)) return;
    int s = (int)S;
    int x0 = OI(GO_RX) * s, y0 = OI(GO_RY) * s;
    int j = floordiv(y - y0, s);
    int i;
    if (fl & GF_SQ) {
        i = (int)floor(((double)x + 0.5 - OD(GO_SQ0)) * OD(GO_SQK));
        if (i < 0 || i >= OI(GO_RW)) return;
    } else
        i = floordiv(x - x0, s);
    uint uv0 = O(GO_U0V0);
    int u0 = uv0 & 0xFFFF, v0 = uv0 >> 16;
    int u = (fl & GF_FLIPX) ? u0 - i : u0 + i;
    int v = (fl & GF_FLIPY) ? v0 - j : v0 + j;
    uint col = O(GO_COLOR);
    shade(col & 0xFF, (col >> 8) & 0xFF, (col >> 16) & 0xFF, u, v);
}

// fills, depth clears, uploads: is (x, y) in the area (wrapping)? Its texel there.
bool in_area(int x, int y, out int col, out int row) {
    int s = (int)S;
    int rx = imod(x - OI(GO_RX) * s, (int)W), ry = imod(y - OI(GO_RY) * s, (int)H);
    col = rx / s;
    row = ry / s;
    return rx < OI(GO_RW) * s && ry < OI(GO_RH) * s;
}

[numthreads(16, 16, 1)]
void draw(uint3 g : SV_GroupID, uint3 l : SV_GroupThreadID) {
    uint ti = g.y * 32768 + g.x;                 // groups: 32768 across, as many rows as needed
    if (ti >= ntiles) return;
    uint4 T = tiles[ti];
    int x = (int)(T.x * 16 + l.x), y = (int)(T.y * 16 + l.y);
    uint i = (uint)y * W + (uint)x;
    uint h = hl[i];
    hv = h & 0xFFFF;
    lw = h >> 16;
    br = bare[i];
    zv = zb[i];
    idv = zid[i];
    for (uint k = 0; k < T.w; k++) {
        ob = bins[T.z + k] * OPW;
        fl = O(GO_FLAGS);
        uint kind = O(GO_KIND);
        if (kind == GK_TRI) tri(x, y);
        else if (kind == GK_RECT) rect(x, y);
        else {
            int c, r;
            if (!in_area(x, y, c, r)) continue;
            if (kind == GK_FILL) { hv = O(GO_FILLC); lw = 0; zv = 0; br = (fl & GF_BIGFILL) ? 1 : 0; }
            else if (kind == GK_ZCLEAR) zv = 0;
            else { hv = pool[O(GO_POOL) + (uint)(r * OI(GO_RW) + c)] & 0xFFFF; lw = 0; br = 0; zv = -1; }
        }
    }
    hl[i] = hv | (lw << 16);
    bare[i] = br;
    zb[i] = zv;
    zid[i] = idv;
}

// render_copy: the source area into tmp, then tmp to the destination
[numthreads(16, 16, 1)]
void copy_out(uint3 t : SV_DispatchThreadID) {
    if (t.x >= cp_w || t.y >= cp_h) return;
    uint sx = (uint)imod(cp_sx + (int)t.x, (int)W), sy = (uint)imod(cp_sy + (int)t.y, (int)H);
    tmp[t.y * cp_w + t.x] = hl[sy * W + sx];
}
[numthreads(16, 16, 1)]
void copy_in(uint3 t : SV_DispatchThreadID) {
    if (t.x >= cp_w || t.y >= cp_h) return;
    uint dx = (uint)imod(cp_dx + (int)t.x, (int)W), dy = (uint)imod(cp_dy + (int)t.y, (int)H);
    uint i = dy * W + dx;
    if (cp_mchk && (hl[i] & 0x8000)) return;
    uint s = tmp[t.y * cp_w + t.x];
    hl[i] = s | (cp_mset ? 0x8000 : 0);
    bare[i] = 0;
    zb[i] = -1;
}

// a read: the area's pixels packed (pixel | low << 16, bare, depth) into tmp
[numthreads(16, 16, 1)]
void read_out(uint3 t : SV_DispatchThreadID) {
    if (t.x >= rd_w || t.y >= rd_h) return;
    uint x = (rd_x + t.x) % W, y = (rd_y + t.y) % H;
    uint i = y * W + x, o = (t.y * rd_w + t.x) * 3;
    tmp[o] = hl[i];
    tmp[o + 1] = bare[i];
    tmp[o + 2] = asuint(zb[i]);
}

// ------------------------------------------------------------ the picture shown
// display.c's display_grab_bgrx for the high-res picture: each pixel to
// BGRX (24-bit colour, brightness), then its Close model gaps passes, as
// close_holes and close_cracks do them: across each row, then down each
// column (the order they come to the same thing in), rows and columns in
// parallel. pic_x, pic_y: the display area in high-res pixels.
cbuffer Pic : register(b1) {
    uint pic_x, pic_y, pic_w, pic_h;
    uint pic_s, pic_gaps, pad2, pad3;
};
StructuredBuffer<uint> gamma8 : register(t5);
RWStructuredBuffer<uint> pic : register(u5);
RWStructuredBuffer<uint> pmask : register(u6);

uint src_at(uint x, uint y) { return ((pic_y + y) % H) * W + (pic_x + x) % W; }

[numthreads(16, 16, 1)]
void pic_color(uint3 t : SV_DispatchThreadID) {
    if (t.x >= pic_w || t.y >= pic_h) return;
    uint i = src_at(t.x, t.y);
    uint h = hl[i], c = h & 0xFFFF, e = h >> 16;
    uint r, g, b;
    if (e & 0x8000) {
        r = ((c & 31) << 3) | (e & 7); g = (((c >> 5) & 31) << 3) | ((e >> 3) & 7); b = (((c >> 10) & 31) << 3) | ((e >> 6) & 7);
    } else {
        r = (c & 31) << 3; g = ((c >> 5) & 31) << 3; b = ((c >> 10) & 31) << 3;
        r |= r >> 5; g |= g >> 5; b |= b >> 5;
    }
    pic[t.y * pic_w + t.x] = gamma8[b] | (gamma8[g] << 8) | (gamma8[r] << 16);
    pmask[t.y * pic_w + t.x] = bare[i];
}

#define HOLE_BEHIND 64.0f

// close_holes, across: one thread a row
[numthreads(64, 1, 1)]
void holes_across(uint3 t : SV_DispatchThreadID) {
    uint y = t.x;
    if (y >= pic_h) return;
    int Wp = (int)pic_w, maxrun = (int)pic_s - 1;
    uint o = y * pic_w;
    for (int x = 0; x < Wp - 1; ) {
        float z = zb[src_at(x, y)];
        if (z <= 0) { x++; continue; }
        float iz = 1.0f / z, far = 1.0f / (iz + HOLE_BEHIND);
        int j = x + 1;
        while (j < Wp && j - x - 1 < maxrun) {
            float zj = zb[src_at(j, y)];
            if (zj < 0 || zj >= far) break;
            j++;
        }
        if (j > x + 1 && j < Wp) {
            float ze = zb[src_at(j, y)];
            if (ze > 0 && abs(1.0f / ze - iz) < HOLE_BEHIND) {
                for (int k = x + 1; k < j; k++) pic[o + k] = pic[o + x];
                x = j;
                continue;
            }
        }
        x++;
    }
}

// close_holes, down: one thread a column
[numthreads(64, 1, 1)]
void holes_down(uint3 t : SV_DispatchThreadID) {
    uint x = t.x;
    if (x >= pic_w) return;
    int maxrun = (int)pic_s - 1, last_y = -1, run_y = -1;
    float last_z = 0;
    for (int y = 0; y < (int)pic_h; y++) {
        float z = zb[src_at(x, y)];
        if (z < 0) { last_y = run_y = -1; continue; }
        if (last_y >= 0 && (z == 0 || 1.0f / z - 1.0f / last_z > HOLE_BEHIND)) {
            if (run_y < 0) run_y = y;
            if (y - run_y + 1 > maxrun) last_y = run_y = -1;
            continue;
        }
        if (z == 0) continue;
        int r = run_y;
        if (r >= 0 && abs(1.0f / z - 1.0f / last_z) < HOLE_BEHIND) {
            uint c = pic[(r - 1) * pic_w + x];
            for (int k = r; k < y; k++) pic[k * pic_w + x] = c;
        }
        last_z = z; last_y = y; run_y = -1;
    }
}

// close_cracks, across: one thread a row (the mask: bare pixels)
[numthreads(64, 1, 1)]
void cracks_across(uint3 t : SV_DispatchThreadID) {
    uint y = t.x;
    if (y >= pic_h) return;
    int Wp = (int)pic_w, s = (int)pic_s;
    uint o = y * pic_w;
    for (int x = 1; x < Wp; ) {
        if (!pmask[o + x]) { x++; continue; }
        int e = x;
        while (e < Wp && pmask[o + e]) e++;
        if (e < Wp && !pmask[o + x - 1] && e - x <= s)
            for (int k = x; k < e; k++) { pic[o + k] = pic[o + x - 1]; pmask[o + k] = 0; }
        x = e;
    }
}

// close_cracks, down: one thread a column
[numthreads(64, 1, 1)]
void cracks_down(uint3 t : SV_DispatchThreadID) {
    uint x = t.x;
    if (x >= pic_w) return;
    int s = (int)pic_s, start = -1;
    for (int y = 0; y <= (int)pic_h; y++) {
        bool b = y < (int)pic_h && pmask[y * pic_w + x];
        if (b) { if (start < 0) start = y; continue; }
        int st = start;
        if (st < 0) continue;
        start = -1;
        if (st > 0 && y < (int)pic_h && y - st <= s) {
            uint c = pic[(st - 1) * pic_w + x];
            for (int k = st; k < y; k++) pic[k * pic_w + x] = c;
        }
    }
}

// The picture to the window's size (pic_ow x pic_oh): each axis shrunk by
// the area each pixel covers (a larger picture made into a sharper smaller
// one: supersampling) or grown linearly; or the nearest pixel (pic_smooth 0).
cbuffer Scale : register(b2) {
    uint pic_ow, pic_oh, pic_smooth, pad4;
};
RWStructuredBuffer<uint> pic2 : register(u7);

// taps along one axis for output pixel o: source pixels first.., weights w
int taps(uint o, uint n_in, uint n_out, out int first, out float w[10]) {
    float k = (float)n_in / (float)n_out;
    int i;
    [unroll] for (i = 0; i < 10; i++) w[i] = 0;
    if (!pic_smooth) {
        first = min((int)(((float)o + 0.5f) * k), (int)n_in - 1);
        w[0] = 1;
        return 1;
    }
    if (k >= 1) {                                // the source pixels under this one, by how much of each
        float a = (float)o * k, b = a + k;
        first = (int)floor(a);
        int last = min((int)ceil(b) - 1, (int)n_in - 1), n = min(last - first + 1, 10);
        float sum = 0;
        for (i = 0; i < n; i++) {
            float lo = max(a, (float)(first + i)), hi = min(b, (float)(first + i + 1));
            w[i] = max(hi - lo, 0.0f);
            sum += w[i];
        }
        for (i = 0; i < n; i++) w[i] /= sum;
        return n;
    }
    float c = ((float)o + 0.5f) * k - 0.5f;      // linear between the two nearest
    first = (int)floor(c);
    float f = c - (float)first;
    w[0] = 1 - f;
    w[1] = f;
    if (first < 0) { first = 0; w[0] = 1; w[1] = 0; }
    if (first >= (int)n_in - 1) { first = (int)n_in - 1; w[0] = 1; w[1] = 0; }
    return 2;
}

[numthreads(16, 16, 1)]
void pic_scale(uint3 t : SV_DispatchThreadID) {
    if (t.x >= pic_ow || t.y >= pic_oh) return;
    int fx, fy;
    float wx[10], wy[10];
    int nx = taps(t.x, pic_w, pic_ow, fx, wx), ny = taps(t.y, pic_h, pic_oh, fy, wy);
    float3 acc = 0;
    for (int j = 0; j < ny; j++) {
        if (wy[j] <= 0) continue;
        uint row = (uint)min(fy + j, (int)pic_h - 1) * pic_w;
        for (int i = 0; i < nx; i++) {
            if (wx[i] <= 0) continue;
            uint c = pic[row + (uint)min(fx + i, (int)pic_w - 1)];
            acc += wx[i] * wy[j] * float3((float)((c >> 16) & 255), (float)((c >> 8) & 255), (float)(c & 255));
        }
    }
    uint3 v = (uint3)clamp(acc + 0.5f, 0, 255);
    pic2[t.y * pic_ow + t.x] = v.z | (v.y << 8) | (v.x << 16);
}
