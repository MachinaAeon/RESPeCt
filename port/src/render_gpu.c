/* The high-resolution picture on the graphics card (render_gpu.h), with
 * Direct3D 11 compute shaders. d3d11.dll, dxgi.dll and d3dcompiler_47.dll
 * are loaded at run time (Windows has them); without them, or without a
 * card that does double precision (the edges are worked out exactly, as
 * render.c does in 64-bit integers), rgpu_init fails and the CPU draws. */
#ifdef _WIN32
#define COBJMACROS
#define INITGUID
#include <windows.h>
#include <d3d11.h>
#include <d3dcompiler.h>
#include <dxgi.h>

#include "render_gpu.h"
#include "rt.h"

#include <stdlib.h>
#include <string.h>

/* the shader's source (render_gpu.hlsl), built in */
__asm__(".section .rdata,\"dr\"\n"
        ".globl rgpu_hlsl\n"
        "rgpu_hlsl:\n"
        ".incbin \"src/render_gpu.hlsl\"\n"
        ".byte 0\n"
        ".text\n");
extern const char rgpu_hlsl[];

typedef HRESULT(WINAPI *PFN_CreateDXGIFactory1)(REFIID, void **);
typedef HRESULT(WINAPI *PFN_D3DCompile)(LPCVOID, SIZE_T, LPCSTR, const D3D_SHADER_MACRO *, ID3DInclude *, LPCSTR, LPCSTR,
                                         UINT, UINT, ID3DBlob **, ID3DBlob **);

static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;
static ID3D11ComputeShader *cs_draw, *cs_copy_out, *cs_copy_in, *cs_read_out;
static ID3D11ComputeShader *cs_pic_color, *cs_holes_across, *cs_holes_down, *cs_cracks_across, *cs_cracks_down, *cs_pic_scale;
static ID3D11Buffer *cb, *cb_pic, *cb_scale;

typedef struct {
    ID3D11Buffer *buf;
    ID3D11ShaderResourceView *srv;
    ID3D11UnorderedAccessView *uav;
    UINT n, stride;                              /* elements, bytes each */
} Buf;

static Buf b_hl, b_bare, b_z, b_zid, b_tmp;      /* the picture; scratch */
static Buf b_ops, b_vram, b_pool, b_tiles, b_bins;
static Buf b_gamma, b_pic, b_pmask, b_pic2;      /* the picture shown */
static ID3D11Buffer *staging, *pic_staging;
static UINT staging_n, pic_staging_n;
static int S, W, H;

typedef struct {
    uint32_t S, W, H, true_color;
    float depth_tol, cross_tol, margin_depth, pad0;
    uint32_t far_page, depth_any, margin_any, ntiles;
    int32_t cp_sx, cp_sy, cp_dx, cp_dy;
    uint32_t cp_w, cp_h, cp_mset, cp_mchk;
    uint32_t rd_x, rd_y, rd_w, rd_h;
} Consts;
static Consts K;

static void buf_free(Buf *b) {
    if (b->uav) ID3D11UnorderedAccessView_Release(b->uav);
    if (b->srv) ID3D11ShaderResourceView_Release(b->srv);
    if (b->buf) ID3D11Buffer_Release(b->buf);
    memset(b, 0, sizeof *b);
}

/* A structured buffer of n elements (stride bytes), shader readable, and
 * writable (uav) or not. */
static int buf_make(Buf *b, UINT n, UINT stride, int uav, const void *init) {
    buf_free(b);
    if (!n) n = 1;
    D3D11_BUFFER_DESC d = {0};
    d.ByteWidth = n * stride;
    d.Usage = D3D11_USAGE_DEFAULT;
    d.BindFlags = D3D11_BIND_SHADER_RESOURCE | (uav ? D3D11_BIND_UNORDERED_ACCESS : 0);
    d.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED;
    d.StructureByteStride = stride;
    D3D11_SUBRESOURCE_DATA sd = {init, 0, 0};
    if (FAILED(ID3D11Device_CreateBuffer(dev, &d, init ? &sd : NULL, &b->buf))) return 0;
    D3D11_SHADER_RESOURCE_VIEW_DESC sv = {0};
    sv.Format = DXGI_FORMAT_UNKNOWN;
    sv.ViewDimension = D3D11_SRV_DIMENSION_BUFFER;
    sv.Buffer.FirstElement = 0;
    sv.Buffer.NumElements = n;
    if (FAILED(ID3D11Device_CreateShaderResourceView(dev, (ID3D11Resource *)b->buf, &sv, &b->srv))) return 0;
    if (uav) {
        D3D11_UNORDERED_ACCESS_VIEW_DESC uv = {0};
        uv.Format = DXGI_FORMAT_UNKNOWN;
        uv.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
        uv.Buffer.FirstElement = 0;
        uv.Buffer.NumElements = n;
        if (FAILED(ID3D11Device_CreateUnorderedAccessView(dev, (ID3D11Resource *)b->buf, &uv, &b->uav))) return 0;
    }
    b->n = n;
    b->stride = stride;
    return 1;
}

/* An input buffer holding at least n elements (grown by doubling). */
static int buf_fit(Buf *b, UINT n, UINT stride, int uav) {
    if (b->buf && b->n >= n) return 1;
    UINT m = b->n ? b->n : 1024;
    while (m < n) m *= 2;
    return buf_make(b, m, stride, uav, NULL);
}

static void buf_put(Buf *b, const void *data, UINT n) {
    if (!n) return;
    D3D11_BOX box = {0, 0, 0, n * b->stride, 1, 1};
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)b->buf, 0, &box, data, 0, 0);
}

static void consts_put(void) {
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)cb, 0, NULL, &K, 0, 0);
}

static ID3D11ComputeShader *compile(PFN_D3DCompile comp, const char *entry) {
    ID3DBlob *code = NULL, *err = NULL;
    HRESULT hr = comp(rgpu_hlsl, strlen(rgpu_hlsl), "render_gpu.hlsl", NULL, NULL, entry, "cs_5_0",
                      D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &code, &err);
    if (FAILED(hr)) {
        rt_log("gpu: shader %s: %s", entry, err ? (const char *)ID3D10Blob_GetBufferPointer(err) : "failed");
        if (err) ID3D10Blob_Release(err);
        return NULL;
    }
    if (err) ID3D10Blob_Release(err);
    ID3D11ComputeShader *cs = NULL;
    ID3D11Device_CreateComputeShader(dev, ID3D10Blob_GetBufferPointer(code), ID3D10Blob_GetBufferSize(code), NULL, &cs);
    ID3D10Blob_Release(code);
    return cs;
}

static int make_picture(void) {
    UINT n = (UINT)W * H;
    uint32_t *zero = calloc(n, 4);
    if (!zero) return 0;
    int ok = buf_make(&b_hl, n, 4, 1, zero) && buf_make(&b_bare, n, 4, 1, zero) && buf_make(&b_z, n, 4, 1, zero) &&
             buf_make(&b_zid, n, 4, 1, zero);
    free(zero);
    return ok;
}

int rgpu_init(int scale) {
    HMODULE md3d = LoadLibraryA("d3d11.dll"), mdxgi = LoadLibraryA("dxgi.dll"), mcomp = LoadLibraryA("d3dcompiler_47.dll");
    if (!md3d || !mdxgi || !mcomp) { rt_log("gpu: Direct3D 11 not found"); return 0; }
    PFN_D3D11_CREATE_DEVICE create = (PFN_D3D11_CREATE_DEVICE)(void *)GetProcAddress(md3d, "D3D11CreateDevice");
    PFN_CreateDXGIFactory1 factory = (PFN_CreateDXGIFactory1)(void *)GetProcAddress(mdxgi, "CreateDXGIFactory1");
    PFN_D3DCompile comp = (PFN_D3DCompile)(void *)GetProcAddress(mcomp, "D3DCompile");
    if (!create || !factory || !comp) return 0;
    /* the card with the most memory of its own (not a processor's graphics) */
    IDXGIFactory1 *f = NULL;
    IDXGIAdapter1 *best = NULL;
    SIZE_T best_mem = 0;
    char name[128] = "?";
    if (SUCCEEDED(factory(&IID_IDXGIFactory1, (void **)&f))) {
        IDXGIAdapter1 *a;
        for (UINT i = 0; IDXGIFactory1_EnumAdapters1(f, i, &a) != DXGI_ERROR_NOT_FOUND; i++) {
            DXGI_ADAPTER_DESC1 d;
            IDXGIAdapter1_GetDesc1(a, &d);
            if (!(d.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) && d.DedicatedVideoMemory > best_mem) {
                if (best) IDXGIAdapter1_Release(best);
                best = a;
                best_mem = d.DedicatedVideoMemory;
                WideCharToMultiByte(CP_UTF8, 0, d.Description, -1, name, sizeof name, NULL, NULL);
            } else
                IDXGIAdapter1_Release(a);
        }
        IDXGIFactory1_Release(f);
    }
    D3D_FEATURE_LEVEL want = D3D_FEATURE_LEVEL_11_0, got;
    HRESULT hr = create((IDXGIAdapter *)best, best ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, NULL, 0, &want, 1,
                        D3D11_SDK_VERSION, &dev, &got, &ctx);
    if (best) IDXGIAdapter1_Release(best);
    if (FAILED(hr)) { rt_log("gpu: no Direct3D 11 device (%08lX)", (unsigned long)hr); return 0; }
    D3D11_FEATURE_DATA_DOUBLES dbl = {0};
    D3D11_FEATURE_DATA_D3D11_OPTIONS opt = {0};
    ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_DOUBLES, &dbl, sizeof dbl);
    ID3D11Device_CheckFeatureSupport(dev, D3D11_FEATURE_D3D11_OPTIONS, &opt, sizeof opt);
    if (!dbl.DoublePrecisionFloatShaderOps || !opt.ExtendedDoublesShaderInstructions) {
        rt_log("gpu: %s has no double precision in shaders", name);
        ID3D11DeviceContext_Release(ctx);
        ID3D11Device_Release(dev);
        dev = NULL;
        return 0;
    }
    cs_draw = compile(comp, "draw");
    cs_copy_out = compile(comp, "copy_out");
    cs_copy_in = compile(comp, "copy_in");
    cs_read_out = compile(comp, "read_out");
    cs_pic_color = compile(comp, "pic_color");
    cs_holes_across = compile(comp, "holes_across");
    cs_holes_down = compile(comp, "holes_down");
    cs_cracks_across = compile(comp, "cracks_across");
    cs_cracks_down = compile(comp, "cracks_down");
    cs_pic_scale = compile(comp, "pic_scale");
    if (!cs_draw || !cs_copy_out || !cs_copy_in || !cs_read_out || !cs_pic_color || !cs_holes_across || !cs_holes_down ||
        !cs_cracks_across || !cs_cracks_down || !cs_pic_scale)
        return 0;
    D3D11_BUFFER_DESC cd = {0};
    cd.ByteWidth = (sizeof K + 15) & ~15u;
    cd.Usage = D3D11_USAGE_DEFAULT;
    cd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &cd, NULL, &cb))) return 0;
    cd.ByteWidth = 32;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &cd, NULL, &cb_pic))) return 0;
    cd.ByteWidth = 16;
    if (FAILED(ID3D11Device_CreateBuffer(dev, &cd, NULL, &cb_scale))) return 0;
    if (!buf_make(&b_gamma, 256, 4, 0, NULL)) return 0;
    if (!buf_make(&b_vram, 1024 * 512 / 2, 4, 0, NULL)) return 0;
    S = scale;
    W = 1024 * S;
    H = 512 * S;
    if (!make_picture()) return 0;
    rt_log("gpu: drawing on %s (%u MB), %dx", name, (unsigned)(best_mem >> 20), S);
    return 1;
}

void rgpu_set_scale(int scale) {
    if (!dev || scale == S) return;
    S = scale;
    W = 1024 * S;
    H = 512 * S;
    buf_free(&b_tmp);
    make_picture();
}

static void bind_picture(void) {
    ID3D11UnorderedAccessView *u[5] = {b_hl.uav, b_bare.uav, b_z.uav, b_zid.uav, b_tmp.uav};
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 5, u, NULL);
    ID3D11DeviceContext_CSSetConstantBuffers(ctx, 0, 1, &cb);
}

/* ------------------------------------------------------------ binning */

static uint32_t *tile_n, *tile_at;              /* per tile: ops binned, next free slot */
static int tile_cap;
static uint32_t *touched, *tlist, *binv;
static size_t touched_cap, binv_cap;

/* The high-res rectangles an op covers (fills and uploads wrap round VRAM). */
static int op_rects(const uint32_t *o, int r[4][4]) {
    int kind = (int)o[GO_KIND];
    if (kind == GK_TRI || kind == GK_RECT) {
        int x0 = (int)o[GO_X0], x1 = (int)o[GO_X1], y0 = (int)o[GO_Y0], y1 = (int)o[GO_Y1];
        if (x0 < 0) x0 = 0;
        if (y0 < 0) y0 = 0;
        if (x1 >= W) x1 = W - 1;
        if (y1 >= H) y1 = H - 1;
        if (x0 > x1 || y0 > y1) return 0;
        r[0][0] = x0; r[0][1] = x1; r[0][2] = y0; r[0][3] = y1;
        return 1;
    }
    int xs[2][2], ys[2][2], nx = 1, ny = 1;
    int sx = (((int)o[GO_RX] * S) % W + W) % W, lx = (int)o[GO_RW] * S;
    int sy = (((int)o[GO_RY] * S) % H + H) % H, ly = (int)o[GO_RH] * S;
    if (lx <= 0 || ly <= 0) return 0;
    if (lx > W) lx = W;
    if (ly > H) ly = H;
    xs[0][0] = sx; xs[0][1] = sx + lx - 1;
    if (xs[0][1] >= W) { xs[1][0] = 0; xs[1][1] = xs[0][1] - W; xs[0][1] = W - 1; nx = 2; }
    ys[0][0] = sy; ys[0][1] = sy + ly - 1;
    if (ys[0][1] >= H) { ys[1][0] = 0; ys[1][1] = ys[0][1] - H; ys[0][1] = H - 1; ny = 2; }
    int n = 0;
    for (int j = 0; j < ny; j++)
        for (int i = 0; i < nx; i++) {
            r[n][0] = xs[i][0]; r[n][1] = xs[i][1]; r[n][2] = ys[j][0]; r[n][3] = ys[j][1];
            n++;
        }
    return n;
}

void rgpu_draw(const uint32_t *ops, int n, const uint16_t *pool, size_t npool, const uint16_t *vram, const GpuParams *gp) {
    if (!dev || n <= 0) return;
    int TX = W / 16, TY = H / 16, nt = TX * TY;
    if (tile_cap < nt) {
        free(tile_n); free(tile_at);
        tile_n = calloc((size_t)nt, 4);
        tile_at = calloc((size_t)nt, 4);
        tile_cap = nt;
    }
    size_t nt_touched = 0, nbins = 0;
    for (int k = 0; k < n; k++) {
        int r[4][4], nr = op_rects(ops + (size_t)k * GOP_WORDS, r);
        for (int q = 0; q < nr; q++)
            for (int ty = r[q][2] >> 4; ty <= r[q][3] >> 4; ty++)
                for (int tx = r[q][0] >> 4; tx <= r[q][1] >> 4; tx++) {
                    uint32_t t = (uint32_t)(ty * TX + tx);
                    if (!tile_n[t]) {
                        if (nt_touched == touched_cap) {
                            touched_cap = touched_cap ? touched_cap * 2 : 4096;
                            touched = realloc(touched, touched_cap * 4);
                        }
                        touched[nt_touched++] = t;
                    }
                    tile_n[t]++;
                    nbins++;
                }
    }
    if (!nt_touched) return;
    if (nbins > binv_cap) { binv_cap = nbins * 2; binv = realloc(binv, binv_cap * 4); }
    tlist = realloc(tlist, nt_touched * 16);
    uint32_t off = 0;
    for (size_t i = 0; i < nt_touched; i++) {
        uint32_t t = touched[i];
        tlist[i * 4] = t % (uint32_t)TX;
        tlist[i * 4 + 1] = t / (uint32_t)TX;
        tlist[i * 4 + 2] = off;
        tlist[i * 4 + 3] = tile_n[t];
        tile_at[t] = off;
        off += tile_n[t];
    }
    for (int k = 0; k < n; k++) {                /* in the queue's order: each tile's list is too */
        int r[4][4], nr = op_rects(ops + (size_t)k * GOP_WORDS, r);
        for (int q = 0; q < nr; q++)
            for (int ty = r[q][2] >> 4; ty <= r[q][3] >> 4; ty++)
                for (int tx = r[q][0] >> 4; tx <= r[q][1] >> 4; tx++) binv[tile_at[ty * TX + tx]++] = (uint32_t)k;
    }
    for (size_t i = 0; i < nt_touched; i++) tile_n[touched[i]] = 0;

    buf_fit(&b_ops, (UINT)n * GOP_WORDS, 4, 0);
    buf_put(&b_ops, ops, (UINT)n * GOP_WORDS);
    buf_put(&b_vram, vram, 1024 * 512 / 2);
    if (npool) {
        uint32_t *p32 = malloc(npool * 4);
        for (size_t i = 0; i < npool; i++) p32[i] = pool[i];
        buf_fit(&b_pool, (UINT)npool, 4, 0);
        buf_put(&b_pool, p32, (UINT)npool);
        free(p32);
    } else
        buf_fit(&b_pool, 1, 4, 0);
    buf_fit(&b_tiles, (UINT)nt_touched, 16, 0);
    buf_put(&b_tiles, tlist, (UINT)nt_touched);
    buf_fit(&b_bins, (UINT)nbins, 4, 0);
    buf_put(&b_bins, binv, (UINT)nbins);
    if (!b_tmp.buf) buf_make(&b_tmp, 16, 4, 1, NULL);

    K.S = (uint32_t)S; K.W = (uint32_t)W; K.H = (uint32_t)H;
    K.true_color = (uint32_t)gp->true_color;
    K.depth_tol = gp->depth_tol; K.cross_tol = gp->cross_tol; K.margin_depth = gp->margin_depth;
    K.far_page = (uint32_t)gp->far_page; K.depth_any = (uint32_t)gp->depth_any; K.margin_any = (uint32_t)gp->margin_any;
    K.ntiles = (uint32_t)nt_touched;
    consts_put();
    ID3D11ShaderResourceView *srv[5] = {b_ops.srv, b_vram.srv, b_pool.srv, b_tiles.srv, b_bins.srv};
    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 5, srv);
    bind_picture();
    ID3D11DeviceContext_CSSetShader(ctx, cs_draw, NULL, 0);
    UINT gx = nt_touched < 32768 ? (UINT)nt_touched : 32768, gy = (UINT)((nt_touched + 32767) / 32768);
    ID3D11DeviceContext_Dispatch(ctx, gx, gy, 1);
}

void rgpu_copy(int sx, int sy, int dx, int dy, int w, int h, int mask_set, int mask_check) {
    if (!dev || w <= 0 || h <= 0) return;
    UINT n = (UINT)w * S * h * S;
    if (b_tmp.n < n) buf_make(&b_tmp, n, 4, 1, NULL);
    K.S = (uint32_t)S; K.W = (uint32_t)W; K.H = (uint32_t)H;
    K.cp_sx = sx * S; K.cp_sy = sy * S; K.cp_dx = dx * S; K.cp_dy = dy * S;
    K.cp_w = (uint32_t)(w * S); K.cp_h = (uint32_t)(h * S);
    K.cp_mset = (uint32_t)mask_set; K.cp_mchk = (uint32_t)mask_check;
    consts_put();
    bind_picture();
    UINT gx = (K.cp_w + 15) / 16, gy = (K.cp_h + 15) / 16;
    ID3D11DeviceContext_CSSetShader(ctx, cs_copy_out, NULL, 0);
    ID3D11DeviceContext_Dispatch(ctx, gx, gy, 1);
    ID3D11DeviceContext_CSSetShader(ctx, cs_copy_in, NULL, 0);
    ID3D11DeviceContext_Dispatch(ctx, gx, gy, 1);
}

void rgpu_read(int x0, int y0, int w, int h, uint16_t *hires, uint16_t *low, uint8_t *bare, float *z) {
    if (!dev || w <= 0 || h <= 0) return;
    UINT n = (UINT)w * h * 3;
    if (b_tmp.n < n) buf_make(&b_tmp, n, 4, 1, NULL);
    if (staging_n < n) {
        if (staging) ID3D11Buffer_Release(staging);
        D3D11_BUFFER_DESC d = {0};
        d.ByteWidth = n * 4;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        staging = NULL;
        if (FAILED(ID3D11Device_CreateBuffer(dev, &d, NULL, &staging))) { staging_n = 0; return; }
        staging_n = n;
    }
    K.S = (uint32_t)S; K.W = (uint32_t)W; K.H = (uint32_t)H;
    K.rd_x = (uint32_t)x0; K.rd_y = (uint32_t)y0; K.rd_w = (uint32_t)w; K.rd_h = (uint32_t)h;
    consts_put();
    bind_picture();
    ID3D11DeviceContext_CSSetShader(ctx, cs_read_out, NULL, 0);
    ID3D11DeviceContext_Dispatch(ctx, ((UINT)w + 15) / 16, ((UINT)h + 15) / 16, 1);
    D3D11_BOX box = {0, 0, 0, n * 4, 1, 1};
    ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)staging, 0, 0, 0, 0, (ID3D11Resource *)b_tmp.buf, 0, &box);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)staging, 0, D3D11_MAP_READ, 0, &m))) return;
    const uint32_t *src = m.pData;
    for (int y = 0; y < h; y++) {
        size_t row = (size_t)((y0 + y) % H) * W;
        for (int x = 0; x < w; x++) {
            size_t i = row + (size_t)((x0 + x) % W);
            const uint32_t *s = src + ((size_t)y * w + x) * 3;
            hires[i] = (uint16_t)s[0];
            low[i] = (uint16_t)(s[0] >> 16);
            bare[i] = (uint8_t)s[1];
            memcpy(&z[i], &s[2], 4);
        }
    }
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)staging, 0);
}

void rgpu_picture(int x, int y, int w, int h, int gaps, const uint8_t gamma8[256], uint32_t *out, int ow, int oh, int smooth) {
    if (!dev || w <= 0 || h <= 0) return;
    UINT n = (UINT)w * h;
    if (b_pic.n < n && (!buf_make(&b_pic, n, 4, 1, NULL) || !buf_make(&b_pmask, n, 4, 1, NULL))) return;
    int scaled = ow > 0 && oh > 0 && (ow != w || oh != h);
    if (scaled) {
        UINT n2 = (UINT)ow * oh;
        if (b_pic2.n < n2 && !buf_make(&b_pic2, n2, 4, 1, NULL)) return;
        n = n2;
    } else if (!b_pic2.buf && !buf_make(&b_pic2, 16, 4, 1, NULL))
        return;
    if (pic_staging_n < n) {
        if (pic_staging) ID3D11Buffer_Release(pic_staging);
        D3D11_BUFFER_DESC d = {0};
        d.ByteWidth = n * 4;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        pic_staging = NULL;
        if (FAILED(ID3D11Device_CreateBuffer(dev, &d, NULL, &pic_staging))) { pic_staging_n = 0; return; }
        pic_staging_n = n;
    }
    static uint8_t last_gamma[256];
    static int gamma_set;
    if (!gamma_set || memcmp(last_gamma, gamma8, 256)) {
        uint32_t g[256];
        for (int i = 0; i < 256; i++) g[i] = gamma8[i];
        buf_put(&b_gamma, g, 256);
        memcpy(last_gamma, gamma8, 256);
        gamma_set = 1;
    }
    uint32_t pc[8] = {(uint32_t)x, (uint32_t)y, (uint32_t)w, (uint32_t)h, (uint32_t)S, (uint32_t)gaps, 0, 0};
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)cb_pic, 0, NULL, pc, 0, 0);
    K.S = (uint32_t)S; K.W = (uint32_t)W; K.H = (uint32_t)H;
    consts_put();
    uint32_t sc[4] = {(uint32_t)ow, (uint32_t)oh, (uint32_t)smooth, 0};
    ID3D11DeviceContext_UpdateSubresource(ctx, (ID3D11Resource *)cb_scale, 0, NULL, sc, 0, 0);
    if (!b_tmp.buf) buf_make(&b_tmp, 16, 4, 1, NULL);
    ID3D11UnorderedAccessView *u[8] = {b_hl.uav, b_bare.uav, b_z.uav, b_zid.uav, b_tmp.uav, b_pic.uav, b_pmask.uav, b_pic2.uav};
    ID3D11DeviceContext_CSSetUnorderedAccessViews(ctx, 0, 8, u, NULL);
    ID3D11Buffer *cbs[3] = {cb, cb_pic, cb_scale};
    ID3D11DeviceContext_CSSetConstantBuffers(ctx, 0, 3, cbs);
    ID3D11ShaderResourceView *srv[6] = {b_ops.srv, b_vram.srv, b_pool.srv, b_tiles.srv, b_bins.srv, b_gamma.srv};
    ID3D11DeviceContext_CSSetShaderResources(ctx, 0, 6, srv);
    ID3D11DeviceContext_CSSetShader(ctx, cs_pic_color, NULL, 0);
    ID3D11DeviceContext_Dispatch(ctx, ((UINT)w + 15) / 16, ((UINT)h + 15) / 16, 1);
    if (gaps) {
        ID3D11DeviceContext_CSSetShader(ctx, cs_holes_across, NULL, 0);
        ID3D11DeviceContext_Dispatch(ctx, ((UINT)h + 63) / 64, 1, 1);
        ID3D11DeviceContext_CSSetShader(ctx, cs_holes_down, NULL, 0);
        ID3D11DeviceContext_Dispatch(ctx, ((UINT)w + 63) / 64, 1, 1);
        ID3D11DeviceContext_CSSetShader(ctx, cs_cracks_across, NULL, 0);
        ID3D11DeviceContext_Dispatch(ctx, ((UINT)h + 63) / 64, 1, 1);
        ID3D11DeviceContext_CSSetShader(ctx, cs_cracks_down, NULL, 0);
        ID3D11DeviceContext_Dispatch(ctx, ((UINT)w + 63) / 64, 1, 1);
    }
    if (scaled) {
        ID3D11DeviceContext_CSSetShader(ctx, cs_pic_scale, NULL, 0);
        ID3D11DeviceContext_Dispatch(ctx, ((UINT)ow + 15) / 16, ((UINT)oh + 15) / 16, 1);
    }
    if (pic_staging_n < n) {
        if (pic_staging) ID3D11Buffer_Release(pic_staging);
        D3D11_BUFFER_DESC d = {0};
        d.ByteWidth = n * 4;
        d.Usage = D3D11_USAGE_STAGING;
        d.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        pic_staging = NULL;
        if (FAILED(ID3D11Device_CreateBuffer(dev, &d, NULL, &pic_staging))) { pic_staging_n = 0; return; }
        pic_staging_n = n;
    }
    D3D11_BOX box = {0, 0, 0, n * 4, 1, 1};
    ID3D11DeviceContext_CopySubresourceRegion(ctx, (ID3D11Resource *)pic_staging, 0, 0, 0, 0,
                                              (ID3D11Resource *)(scaled ? b_pic2.buf : b_pic.buf), 0, &box);
    D3D11_MAPPED_SUBRESOURCE m;
    if (FAILED(ID3D11DeviceContext_Map(ctx, (ID3D11Resource *)pic_staging, 0, D3D11_MAP_READ, 0, &m))) return;
    memcpy(out, m.pData, (size_t)n * 4);
    ID3D11DeviceContext_Unmap(ctx, (ID3D11Resource *)pic_staging, 0);
}

void rgpu_write(const uint16_t *hires, const uint16_t *low, const uint8_t *bare, const float *z, const uint32_t *zid) {
    if (!dev) return;
    size_t n = (size_t)W * H;
    uint32_t *t = malloc(n * 4);
    if (!t) return;
    for (size_t i = 0; i < n; i++) t[i] = hires[i] | ((uint32_t)low[i] << 16);
    buf_put(&b_hl, t, (UINT)n);
    for (size_t i = 0; i < n; i++) t[i] = bare[i];
    buf_put(&b_bare, t, (UINT)n);
    buf_put(&b_z, z, (UINT)n);
    buf_put(&b_zid, zid, (UINT)n);
    free(t);
}

#else
#include "render_gpu.h"
int rgpu_init(int scale) { (void)scale; return 0; }
void rgpu_set_scale(int scale) { (void)scale; }
void rgpu_draw(const uint32_t *ops, int n, const uint16_t *pool, size_t npool, const uint16_t *vram, const GpuParams *gp) {
    (void)ops; (void)n; (void)pool; (void)npool; (void)vram; (void)gp;
}
void rgpu_copy(int sx, int sy, int dx, int dy, int w, int h, int mask_set, int mask_check) {
    (void)sx; (void)sy; (void)dx; (void)dy; (void)w; (void)h; (void)mask_set; (void)mask_check;
}
void rgpu_read(int x0, int y0, int w, int h, uint16_t *hires, uint16_t *low, uint8_t *bare, float *z) {
    (void)x0; (void)y0; (void)w; (void)h; (void)hires; (void)low; (void)bare; (void)z;
}
void rgpu_picture(int x, int y, int w, int h, int gaps, const uint8_t gamma8[256], uint32_t *out, int ow, int oh, int smooth) {
    (void)x; (void)y; (void)w; (void)h; (void)gaps; (void)gamma8; (void)out; (void)ow; (void)oh; (void)smooth;
}
void rgpu_write(const uint16_t *hires, const uint16_t *low, const uint8_t *bare, const float *z, const uint32_t *zid) {
    (void)hires; (void)low; (void)bare; (void)z; (void)zid;
}
#endif
