/* GTE (COP2): the PS1's geometry coprocessor. Fixed-point transform,
 * perspective, lighting and colour commands, with the hardware's
 * saturation flags and its division algorithm (psx-spx "GTE"). */
#include "rt.h"
#include "savestate.h"

#include <math.h>
#include <stdlib.h>

typedef struct { int16_t x, y, z; } V3;

static struct {
    V3 v[3];
    uint32_t rgbc;
    uint16_t otz;
    int16_t ir[4];
    int16_t sx[3], sy[3];        /* screen XY FIFO */
    float fx[3], fy[3];          /* the same with the division's fractions */
    uint16_t sz[4];              /* screen Z FIFO */
    uint32_t rgb[3];             /* colour FIFO */
    uint32_t res1;
    int32_t mac[4];
    int32_t lzcs, lzcr;

    int16_t rt[3][3], llm[3][3], lcm[3][3];
    int32_t tr[3], bk[3], fc[3];
    int32_t ofx, ofy;
    uint16_t h;
    int16_t dqa;
    int32_t dqb;
    int16_t zsf3, zsf4;
    uint32_t flag;
} G;

/* ------------------------------------------------------------ registers */

static int clz32(uint32_t x) { return x ? __builtin_clz(x) : 32; }

static uint32_t pack(int16_t lo, int16_t hi) { return (uint16_t)lo | ((uint32_t)(uint16_t)hi << 16); }

static int clamp5(int16_t v) { int x = v >> 7; return x < 0 ? 0 : x > 31 ? 31 : x; }

static void moving_push(uint32_t word, float x, float y, float z);   /* precision, below */
static void moving_frac(uint32_t word, float *x, float *y);

uint32_t gte_read_data(int r) {
    switch (r) {
    case 0: return pack(G.v[0].x, G.v[0].y);
    case 1: return (uint32_t)(int32_t)G.v[0].z;
    case 2: return pack(G.v[1].x, G.v[1].y);
    case 3: return (uint32_t)(int32_t)G.v[1].z;
    case 4: return pack(G.v[2].x, G.v[2].y);
    case 5: return (uint32_t)(int32_t)G.v[2].z;
    case 6: return G.rgbc;
    case 7: return G.otz;
    case 8: case 9: case 10: case 11: return (uint32_t)(int32_t)G.ir[r - 8];
    case 12: case 13: case 14: case 15: {
        int k = r == 15 ? 2 : r - 12;
        uint32_t w = pack(G.sx[k], G.sy[k]);
        moving_push(w, G.fx[k], G.fy[k], (float)G.sz[k + 1]);
        return w;
    }
    case 16: case 17: case 18: case 19: return G.sz[r - 16];
    case 20: case 21: case 22: return G.rgb[r - 20];
    case 23: return G.res1;
    case 24: case 25: case 26: case 27: return (uint32_t)G.mac[r - 24];
    case 28: case 29:
        return clamp5(G.ir[1]) | (clamp5(G.ir[2]) << 5) | (clamp5(G.ir[3]) << 10);
    case 30: return (uint32_t)G.lzcs;
    case 31: return (uint32_t)G.lzcr;
    }
    return 0;
}

void gte_write_data(int r, uint32_t v) {
    int16_t lo = (int16_t)v, hi = (int16_t)(v >> 16);
    switch (r) {
    case 0: G.v[0].x = lo; G.v[0].y = hi; break;
    case 1: G.v[0].z = lo; break;
    case 2: G.v[1].x = lo; G.v[1].y = hi; break;
    case 3: G.v[1].z = lo; break;
    case 4: G.v[2].x = lo; G.v[2].y = hi; break;
    case 5: G.v[2].z = lo; break;
    case 6: G.rgbc = v; break;
    case 7: G.otz = (uint16_t)v; break;
    case 8: case 9: case 10: case 11: G.ir[r - 8] = lo; break;
    case 12: case 13: case 14:
        G.sx[r - 12] = lo; G.sy[r - 12] = hi; G.fx[r - 12] = lo; G.fy[r - 12] = hi;
        moving_frac(v, &G.fx[r - 12], &G.fy[r - 12]);     /* a point read back: its fractions too */
        break;
    case 15:
        G.sx[0] = G.sx[1]; G.sy[0] = G.sy[1]; G.fx[0] = G.fx[1]; G.fy[0] = G.fy[1];
        G.sx[1] = G.sx[2]; G.sy[1] = G.sy[2]; G.fx[1] = G.fx[2]; G.fy[1] = G.fy[2];
        G.sx[2] = lo; G.sy[2] = hi; G.fx[2] = lo; G.fy[2] = hi;
        moving_frac(v, &G.fx[2], &G.fy[2]);
        break;
    case 16: case 17: case 18: case 19: G.sz[r - 16] = (uint16_t)v; break;
    case 20: case 21: case 22: G.rgb[r - 20] = v; break;
    case 23: G.res1 = v; break;
    case 24: case 25: case 26: case 27: G.mac[r - 24] = (int32_t)v; break;
    case 28:
        G.ir[1] = (int16_t)((v & 0x1F) << 7);
        G.ir[2] = (int16_t)(((v >> 5) & 0x1F) << 7);
        G.ir[3] = (int16_t)(((v >> 10) & 0x1F) << 7);
        break;
    case 29: break;
    case 30:
        G.lzcs = (int32_t)v;
        G.lzcr = (int32_t)v < 0 ? clz32(~v) : clz32(v);
        break;
    case 31: break;
    }
}

static void get_mat(int16_t m[3][3], int base, int r, uint32_t *out) {
    (void)base;
    int16_t *f = &m[0][0];
    if (r < 4) *out = pack(f[r * 2], f[r * 2 + 1]);
    else *out = (uint32_t)(int32_t)f[8];
}

static void set_mat(int16_t m[3][3], int r, uint32_t v) {
    int16_t *f = &m[0][0];
    if (r < 4) { f[r * 2] = (int16_t)v; f[r * 2 + 1] = (int16_t)(v >> 16); }
    else f[8] = (int16_t)v;
}

uint32_t gte_read_ctrl(int r) {
    uint32_t v = 0;
    if (r < 5) { get_mat(G.rt, 0, r, &v); return v; }
    if (r < 8) return (uint32_t)G.tr[r - 5];
    if (r < 13) { get_mat(G.llm, 0, r - 8, &v); return v; }
    if (r < 16) return (uint32_t)G.bk[r - 13];
    if (r < 21) { get_mat(G.lcm, 0, r - 16, &v); return v; }
    if (r < 24) return (uint32_t)G.fc[r - 21];
    switch (r) {
    case 24: return (uint32_t)G.ofx;
    case 25: return (uint32_t)G.ofy;
    case 26: return (uint32_t)(int32_t)(int16_t)G.h;   /* reads back sign-extended */
    case 27: return (uint32_t)(int32_t)G.dqa;
    case 28: return (uint32_t)G.dqb;
    case 29: return (uint32_t)(int32_t)G.zsf3;
    case 30: return (uint32_t)(int32_t)G.zsf4;
    case 31: return G.flag;
    }
    return 0;
}

void gte_write_ctrl(int r, uint32_t v) {
    if (r < 5) { set_mat(G.rt, r, v); return; }
    if (r < 8) { G.tr[r - 5] = (int32_t)v; return; }
    if (r < 13) { set_mat(G.llm, r - 8, v); return; }
    if (r < 16) { G.bk[r - 13] = (int32_t)v; return; }
    if (r < 21) { set_mat(G.lcm, r - 16, v); return; }
    if (r < 24) { G.fc[r - 21] = (int32_t)v; return; }
    switch (r) {
    case 24: G.ofx = (int32_t)v; break;
    case 25: G.ofy = (int32_t)v; break;
    case 26: G.h = (uint16_t)v; break;
    case 27: G.dqa = (int16_t)v; break;
    case 28: G.dqb = (int32_t)v; break;
    case 29: G.zsf3 = (int16_t)v; break;
    case 30: G.zsf4 = (int16_t)v; break;
    case 31:
        G.flag = v & 0x7FFFF000;
        if (G.flag & 0x7F87E000) G.flag |= 0x80000000;
        break;
    }
}

/* ------------------------------------------------------------ math */

static int sf_shift;     /* 12 when sf=1 */
static int lm_flag;

static int64_t chk_mac(int i, int64_t v) {
    static const uint32_t pos[4] = {1u << 16, 1u << 30, 1u << 29, 1u << 28};
    static const uint32_t neg[4] = {1u << 15, 1u << 27, 1u << 26, 1u << 25};
    if (i == 0) {
        if (v > 0x7FFFFFFFLL) G.flag |= pos[0];
        else if (v < -0x80000000LL) G.flag |= neg[0];
    } else {
        if (v > 0x7FFFFFFFFFFLL) G.flag |= pos[i];
        else if (v < -0x80000000000LL) G.flag |= neg[i];
        /* wrap to 44 bits like the hardware */
        v = (v << 20) >> 20;
    }
    return v;
}

static int32_t set_mac(int i, int64_t v, int shift) {
    v = chk_mac(i, v);
    G.mac[i] = (int32_t)(v >> shift);
    return G.mac[i];
}

static int16_t sat_ir(int i, int64_t v, int lm) {
    static const uint32_t fl[4] = {1u << 12, 1u << 24, 1u << 23, 1u << 22};
    int64_t lo = lm ? 0 : -0x8000;
    if (v < lo) { G.flag |= fl[i]; return (int16_t)lo; }
    if (v > 0x7FFF) { G.flag |= fl[i]; return 0x7FFF; }
    return (int16_t)v;
}

static void set_ir(int i, int64_t v, int lm) { G.ir[i] = sat_ir(i, v, lm); }

static void mac_to_ir(int lm) {
    for (int i = 1; i < 4; i++) set_ir(i, G.mac[i], lm);
}

static uint8_t sat_color(int i, int32_t v) {
    static const uint32_t fl[3] = {1u << 21, 1u << 20, 1u << 19};
    if (v < 0) { G.flag |= fl[i]; return 0; }
    if (v > 255) { G.flag |= fl[i]; return 255; }
    return (uint8_t)v;
}

static void push_color(void) {
    G.rgb[0] = G.rgb[1];
    G.rgb[1] = G.rgb[2];
    G.rgb[2] = sat_color(0, G.mac[1] >> 4) | ((uint32_t)sat_color(1, G.mac[2] >> 4) << 8) |
               ((uint32_t)sat_color(2, G.mac[3] >> 4) << 16) | (G.rgbc & 0xFF000000);
}

static void push_sz(int64_t v) {
    G.sz[0] = G.sz[1]; G.sz[1] = G.sz[2]; G.sz[2] = G.sz[3];
    if (v < 0) { G.flag |= 1u << 18; v = 0; }
    else if (v > 0xFFFF) { G.flag |= 1u << 18; v = 0xFFFF; }
    G.sz[3] = (uint16_t)v;
}

static void precise_record(int16_t sx, int16_t sy, float fx, float fy, float fz);

static void push_sxy(int32_t x, int32_t y, float fx, float fy) {
    if (x < -0x400) { G.flag |= 1u << 14; x = -0x400; fx = -1024.0f; }
    else if (x > 0x3FF) { G.flag |= 1u << 14; x = 0x3FF; fx = 1023.0f; }
    if (y < -0x400) { G.flag |= 1u << 13; y = -0x400; fy = -1024.0f; }
    else if (y > 0x3FF) { G.flag |= 1u << 13; y = 0x3FF; fy = 1023.0f; }
    G.sx[0] = G.sx[1]; G.sy[0] = G.sy[1]; G.fx[0] = G.fx[1]; G.fy[0] = G.fy[1];
    G.sx[1] = G.sx[2]; G.sy[1] = G.sy[2]; G.fx[1] = G.fx[2]; G.fy[1] = G.fy[2];
    G.sx[2] = (int16_t)x; G.sy[2] = (int16_t)y; G.fx[2] = fx; G.fy[2] = fy;
    precise_record((int16_t)x, (int16_t)y, fx, fy, (float)G.sz[3]);
}

static uint8_t unr_table[0x101];

static void init_unr(void) {
    for (int i = 0; i < 0x101; i++) {
        int v = (0x40000 / (i + 0x100) + 1) / 2 - 0x101;
        unr_table[i] = (uint8_t)(v < 0 ? 0 : v);
    }
}

static uint32_t gte_divide(uint16_t h, uint16_t sz) {
    if (h >= sz * 2) {
        G.flag |= 1u << 17;
        return 0x1FFFF;
    }
    int z = __builtin_clz((uint32_t)sz) - 16;
    uint64_t n = (uint64_t)h << z;
    uint32_t d = (uint32_t)sz << z;
    uint32_t u = unr_table[(d - 0x7FC0) >> 7] + 0x101;
    d = (0x2000080 - d * u) >> 8;
    d = (0x0000080 + d * u) >> 8;
    uint64_t r = (n * d + 0x8000) >> 16;
    return r > 0x1FFFF ? 0x1FFFF : (uint32_t)r;
}

static void set_ir0(int64_t v) {
    if (v < 0) { G.flag |= 1u << 12; v = 0; }
    else if (v > 0x1000) { G.flag |= 1u << 12; v = 0x1000; }
    G.ir[0] = (int16_t)v;
}

/* Field of view setting (0 = the game's own). In play the game projects
 * with H = 270, about 61 degrees across a 4:3 screen; a wider view is a
 * smaller H, scaled the same way for any H it sets while TEST is loaded. */
int g_opt_fov;
int g_fov_hold;                          /* game code that needs the game's own H */
static uint16_t projection_h(void) {
    if (!g_opt_fov || g_fov_hold || MEM_R32(0x8006C118) != 6) return G.h;
    double want = 160.0 / tan(g_opt_fov * 3.14159265358979 / 360.0);
    double h = G.h * (want / 270.0);
    return (uint16_t)(h < 16 ? 16 : h > 65535 ? 65535 : h);
}

static void rtp(int i, int last) {
    V3 v = G.v[i];
    int64_t m[3];
    for (int r = 0; r < 3; r++) {
        m[r] = chk_mac(r + 1, ((int64_t)G.tr[r] << 12) + (int64_t)G.rt[r][0] * v.x +
                                  (int64_t)G.rt[r][1] * v.y + (int64_t)G.rt[r][2] * v.z);
        G.mac[r + 1] = (int32_t)(m[r] >> sf_shift);
    }
    set_ir(1, G.mac[1], lm_flag);
    set_ir(2, G.mac[2], lm_flag);
    /* IR3's flag is computed from MAC3 >> 12 regardless of sf. */
    {
        int64_t t = m[2] >> 12;
        if (t < -0x8000 || t > 0x7FFF) G.flag |= 1u << 22;
        int64_t v3 = G.mac[3], lo = lm_flag ? 0 : -0x8000;
        G.ir[3] = (int16_t)(v3 < lo ? lo : v3 > 0x7FFF ? 0x7FFF : v3);
    }
    push_sz(m[2] >> 12);
    uint32_t n = gte_divide(projection_h(), G.sz[3]);
    int64_t px = (int64_t)n * G.ir[1];
    if (g_widescreen) {                  /* widescreen: squeeze toward the centre */
        static int dbg = -1;             /* RT_WS_DEBUG: squeeze to 1/2 (to spot culling) */
        if (dbg < 0) dbg = getenv("RT_WS_DEBUG") != NULL;
        px = dbg ? px / 2 : px * 3 / 4;
    }
    int64_t x = px + G.ofx;
    int64_t y = (int64_t)n * G.ir[2] + G.ofy;
    chk_mac(0, x);
    chk_mac(0, y);
    push_sxy((int32_t)(x >> 16), (int32_t)(y >> 16), (float)((double)x / 65536.0), (float)((double)y / 65536.0));
    if (last) {
        int64_t d = (int64_t)n * G.dqa + G.dqb;
        chk_mac(0, d);
        G.mac[0] = (int32_t)d;
        set_ir0(d >> 12);
    }
}

/* Multiply a matrix by a vector plus translation << 12, into MAC/IR. */
static void mat_vec(int16_t m[3][3], const int32_t *t, int16_t vx, int16_t vy, int16_t vz) {
    for (int r = 0; r < 3; r++) {
        int64_t acc = t ? ((int64_t)t[r] << 12) : 0;
        acc = chk_mac(r + 1, acc + (int64_t)m[r][0] * vx);
        acc = chk_mac(r + 1, acc + (int64_t)m[r][1] * vy);
        acc = chk_mac(r + 1, acc + (int64_t)m[r][2] * vz);
        G.mac[r + 1] = (int32_t)(acc >> sf_shift);
    }
    mac_to_ir(lm_flag);
}

/* Depth cue toward the far colour: [MAC] holds the colour << 12 scaled. */
static void interpolate(void) {
    int32_t m[3] = {G.mac[1], G.mac[2], G.mac[3]};
    for (int i = 0; i < 3; i++)
        set_ir(i + 1, chk_mac(i + 1, ((int64_t)G.fc[i] << 12) - ((int64_t)m[i] << sf_shift)) >> sf_shift, 0);
    for (int i = 0; i < 3; i++)
        set_mac(i + 1, (int64_t)G.ir[i + 1] * G.ir[0] + ((int64_t)m[i] << sf_shift), sf_shift);
    mac_to_ir(lm_flag);
}

static void color_light(int16_t vx, int16_t vy, int16_t vz, int stage) {
    /* stage 0: NCS (light only), 1: NCC (times RGB), 2: NCD (depth cue) */
    mat_vec(G.llm, NULL, vx, vy, vz);
    mat_vec(G.lcm, G.bk, G.ir[1], G.ir[2], G.ir[3]);
    if (stage == 0) {
        push_color();
        return;
    }
    int r = G.rgbc & 0xFF, g = (G.rgbc >> 8) & 0xFF, b = (G.rgbc >> 16) & 0xFF;
    if (stage == 1) {
        set_mac(1, ((int64_t)r * G.ir[1]) << 4, sf_shift);
        set_mac(2, ((int64_t)g * G.ir[2]) << 4, sf_shift);
        set_mac(3, ((int64_t)b * G.ir[3]) << 4, sf_shift);
        mac_to_ir(lm_flag);
    } else {
        G.mac[1] = (int32_t)(((int64_t)r * G.ir[1]) << 4 >> sf_shift);
        G.mac[2] = (int32_t)(((int64_t)g * G.ir[2]) << 4 >> sf_shift);
        G.mac[3] = (int32_t)(((int64_t)b * G.ir[3]) << 4 >> sf_shift);
        /* interpolate expects MAC = colour product before the shift */
        {
            int32_t m[3] = {(int32_t)(((int64_t)r * G.ir[1]) << 4), (int32_t)(((int64_t)g * G.ir[2]) << 4),
                            (int32_t)(((int64_t)b * G.ir[3]) << 4)};
            for (int i = 0; i < 3; i++)
                set_ir(i + 1, chk_mac(i + 1, ((int64_t)G.fc[i] << 12) - m[i]) >> sf_shift, 0);
            for (int i = 0; i < 3; i++)
                set_mac(i + 1, (int64_t)G.ir[i + 1] * G.ir[0] + m[i], sf_shift);
            mac_to_ir(lm_flag);
        }
    }
    push_color();
}

void gte_command(uint32_t cmd) {
    static int inited;
    if (!inited) { init_unr(); inited = 1; }
    G.flag = 0;
    sf_shift = (cmd & (1u << 19)) ? 12 : 0;
    lm_flag = (cmd >> 10) & 1;
    int op = cmd & 0x3F;
    switch (op) {
    case 0x01: rtp(0, 1); break;                                     /* RTPS */
    case 0x30: rtp(0, 0); rtp(1, 0); rtp(2, 1); break;               /* RTPT */
    case 0x06: {                                                     /* NCLIP */
        int64_t v = (int64_t)G.sx[0] * G.sy[1] + (int64_t)G.sx[1] * G.sy[2] + (int64_t)G.sx[2] * G.sy[0] -
                    (int64_t)G.sx[0] * G.sy[2] - (int64_t)G.sx[1] * G.sy[0] - (int64_t)G.sx[2] * G.sy[1];
        set_mac(0, v, 0);
        break;
    }
    case 0x0C: {                                                     /* OP */
        int64_t d1 = G.rt[0][0], d2 = G.rt[1][1], d3 = G.rt[2][2];
        set_mac(1, (int64_t)G.ir[3] * d2 - (int64_t)G.ir[2] * d3, sf_shift);
        set_mac(2, (int64_t)G.ir[1] * d3 - (int64_t)G.ir[3] * d1, sf_shift);
        set_mac(3, (int64_t)G.ir[2] * d1 - (int64_t)G.ir[1] * d2, sf_shift);
        mac_to_ir(lm_flag);
        break;
    }
    case 0x10: {                                                     /* DPCS */
        int r = G.rgbc & 0xFF, g = (G.rgbc >> 8) & 0xFF, b = (G.rgbc >> 16) & 0xFF;
        int32_t m[3] = {r << 16, g << 16, b << 16};
        for (int i = 0; i < 3; i++)
            set_ir(i + 1, chk_mac(i + 1, ((int64_t)G.fc[i] << 12) - m[i]) >> sf_shift, 0);
        for (int i = 0; i < 3; i++)
            set_mac(i + 1, (int64_t)G.ir[i + 1] * G.ir[0] + m[i], sf_shift);
        mac_to_ir(lm_flag);
        push_color();
        break;
    }
    case 0x2A: {                                                     /* DPCT */
        for (int k = 0; k < 3; k++) {
            uint32_t c = G.rgb[0];
            int32_t m[3] = {(int32_t)(c & 0xFF) << 16, (int32_t)((c >> 8) & 0xFF) << 16, (int32_t)((c >> 16) & 0xFF) << 16};
            for (int i = 0; i < 3; i++)
                set_ir(i + 1, chk_mac(i + 1, ((int64_t)G.fc[i] << 12) - m[i]) >> sf_shift, 0);
            for (int i = 0; i < 3; i++)
                set_mac(i + 1, (int64_t)G.ir[i + 1] * G.ir[0] + m[i], sf_shift);
            mac_to_ir(lm_flag);
            push_color();
        }
        break;
    }
    case 0x11: {                                                     /* INTPL */
        int32_t m[3] = {(int32_t)G.ir[1] << 12, (int32_t)G.ir[2] << 12, (int32_t)G.ir[3] << 12};
        for (int i = 0; i < 3; i++)
            set_ir(i + 1, chk_mac(i + 1, ((int64_t)G.fc[i] << 12) - m[i]) >> sf_shift, 0);
        for (int i = 0; i < 3; i++)
            set_mac(i + 1, (int64_t)G.ir[i + 1] * G.ir[0] + m[i], sf_shift);
        mac_to_ir(lm_flag);
        push_color();
        break;
    }
    case 0x29: {                                                     /* DCPL */
        int r = G.rgbc & 0xFF, g = (G.rgbc >> 8) & 0xFF, b = (G.rgbc >> 16) & 0xFF;
        int32_t m[3] = {(r * G.ir[1]) << 4, (g * G.ir[2]) << 4, (b * G.ir[3]) << 4};
        for (int i = 0; i < 3; i++)
            set_ir(i + 1, chk_mac(i + 1, ((int64_t)G.fc[i] << 12) - m[i]) >> sf_shift, 0);
        for (int i = 0; i < 3; i++)
            set_mac(i + 1, (int64_t)G.ir[i + 1] * G.ir[0] + m[i], sf_shift);
        mac_to_ir(lm_flag);
        push_color();
        break;
    }
    case 0x12: {                                                     /* MVMVA */
        int mx = (cmd >> 17) & 3, vv = (cmd >> 15) & 3, cv = (cmd >> 13) & 3;
        int16_t vx, vy, vz;
        if (vv < 3) { vx = G.v[vv].x; vy = G.v[vv].y; vz = G.v[vv].z; }
        else { vx = G.ir[1]; vy = G.ir[2]; vz = G.ir[3]; }
        int16_t garbage[3][3] = {{(int16_t)-(int16_t)((G.rgbc & 0xFF) << 4), (int16_t)((G.rgbc & 0xFF) << 4), G.ir[0]},
                                 {G.rt[0][2], G.rt[0][2], G.rt[0][2]},
                                 {G.rt[1][1], G.rt[1][1], G.rt[1][1]}};
        int16_t (*m)[3] = mx == 0 ? G.rt : mx == 1 ? G.llm : mx == 2 ? G.lcm : garbage;
        const int32_t *t = cv == 0 ? G.tr : cv == 1 ? G.bk : NULL;
        if (cv == 2) {
            /* Far-colour translation is broken on hardware: the flags see
             * FC + first column, the result only the other two columns. */
            for (int r = 0; r < 3; r++) {
                int64_t first = ((int64_t)G.fc[r] << 12) + (int64_t)m[r][0] * vx;
                sat_ir(r + 1, chk_mac(r + 1, first) >> sf_shift, 0);
                int64_t acc = chk_mac(r + 1, (int64_t)m[r][1] * vy);
                acc = chk_mac(r + 1, acc + (int64_t)m[r][2] * vz);
                G.mac[r + 1] = (int32_t)(acc >> sf_shift);
            }
            mac_to_ir(lm_flag);
        } else {
            mat_vec(m, t, vx, vy, vz);
        }
        break;
    }
    case 0x13: color_light(G.v[0].x, G.v[0].y, G.v[0].z, 2); break;  /* NCDS */
    case 0x16:                                                       /* NCDT */
        for (int k = 0; k < 3; k++) color_light(G.v[k].x, G.v[k].y, G.v[k].z, 2);
        break;
    case 0x1B: color_light(G.v[0].x, G.v[0].y, G.v[0].z, 1); break;  /* NCCS */
    case 0x3F:                                                       /* NCCT */
        for (int k = 0; k < 3; k++) color_light(G.v[k].x, G.v[k].y, G.v[k].z, 1);
        break;
    case 0x1E: color_light(G.v[0].x, G.v[0].y, G.v[0].z, 0); break;  /* NCS */
    case 0x20:                                                       /* NCT */
        for (int k = 0; k < 3; k++) color_light(G.v[k].x, G.v[k].y, G.v[k].z, 0);
        break;
    case 0x1C: {                                                     /* CC */
        mat_vec(G.lcm, G.bk, G.ir[1], G.ir[2], G.ir[3]);
        int r = G.rgbc & 0xFF, g = (G.rgbc >> 8) & 0xFF, b = (G.rgbc >> 16) & 0xFF;
        set_mac(1, ((int64_t)r * G.ir[1]) << 4, sf_shift);
        set_mac(2, ((int64_t)g * G.ir[2]) << 4, sf_shift);
        set_mac(3, ((int64_t)b * G.ir[3]) << 4, sf_shift);
        mac_to_ir(lm_flag);
        push_color();
        break;
    }
    case 0x14: {                                                     /* CDP */
        mat_vec(G.lcm, G.bk, G.ir[1], G.ir[2], G.ir[3]);
        int r = G.rgbc & 0xFF, g = (G.rgbc >> 8) & 0xFF, b = (G.rgbc >> 16) & 0xFF;
        int32_t m[3] = {(int32_t)(((int64_t)r * G.ir[1]) << 4), (int32_t)(((int64_t)g * G.ir[2]) << 4),
                        (int32_t)(((int64_t)b * G.ir[3]) << 4)};
        for (int i = 0; i < 3; i++)
            set_ir(i + 1, chk_mac(i + 1, ((int64_t)G.fc[i] << 12) - m[i]) >> sf_shift, 0);
        for (int i = 0; i < 3; i++)
            set_mac(i + 1, (int64_t)G.ir[i + 1] * G.ir[0] + m[i], sf_shift);
        mac_to_ir(lm_flag);
        push_color();
        break;
    }
    case 0x28:                                                       /* SQR */
        for (int i = 1; i < 4; i++) set_mac(i, (int64_t)G.ir[i] * G.ir[i], sf_shift);
        mac_to_ir(lm_flag);
        break;
    case 0x2D: {                                                     /* AVSZ3 */
        int64_t v = (int64_t)G.zsf3 * (G.sz[1] + G.sz[2] + G.sz[3]);
        set_mac(0, v, 0);
        int64_t o = v >> 12;
        if (o < 0) { G.flag |= 1u << 18; o = 0; } else if (o > 0xFFFF) { G.flag |= 1u << 18; o = 0xFFFF; }
        G.otz = (uint16_t)o;
        break;
    }
    case 0x2E: {                                                     /* AVSZ4 */
        int64_t v = (int64_t)G.zsf4 * (G.sz[0] + G.sz[1] + G.sz[2] + G.sz[3]);
        set_mac(0, v, 0);
        int64_t o = v >> 12;
        if (o < 0) { G.flag |= 1u << 18; o = 0; } else if (o > 0xFFFF) { G.flag |= 1u << 18; o = 0xFFFF; }
        G.otz = (uint16_t)o;
        break;
    }
    case 0x3D:                                                       /* GPF */
        for (int i = 1; i < 4; i++) set_mac(i, (int64_t)G.ir[0] * G.ir[i], sf_shift);
        mac_to_ir(lm_flag);
        push_color();
        break;
    case 0x3E:                                                       /* GPL */
        for (int i = 1; i < 4; i++)
            set_mac(i, ((int64_t)G.mac[i] << sf_shift) + (int64_t)G.ir[0] * G.ir[i], sf_shift);
        mac_to_ir(lm_flag);
        push_color();
        break;
    default:
        rt_log("GTE: unknown command %07X", cmd);
        break;
    }
    if (G.flag & 0x7F87E000) G.flag |= 0x80000000;
}

/* ------------------------------------------------------------ precision
 * Screen positions with their fractions, so the renderer can place
 * vertices between pixels at higher resolutions. They're kept by value:
 * the packed integer position (what the game stores into its drawing
 * packets, by whatever route) maps to the precise position the GTE last
 * produced for it. Every polygon sharing a corner then gets the same
 * precise point, so no cracks open between them. Entries from this frame
 * and the previous one are used (drawing lists are built a frame ahead). */

int g_widescreen, g_subpixel;

/* Precise positions by packed SXY word, for the GPU (see gpu.c). 4-way
 * buckets: a word keeps its slot until four newer words land in the same
 * bucket, so the frame the game is building doesn't push out the one the
 * GPU is about to draw. */
#define PBUCKETS 65536
#define PWAYS 8
typedef struct { uint32_t word; uint32_t gen; float x, y, z; } Precise;
static Precise pcache[PBUCKETS][PWAYS];
static uint32_t pgen = 1;

static inline uint32_t phash(uint32_t w) {
    w ^= w >> 16; w *= 0x7FEB352Du; w ^= w >> 15; w *= 0x846CA68Bu; w ^= w >> 16;
    return w & (PBUCKETS - 1);
}

void gte_precise_frame(void) { pgen++; }

/* Every distinct corner projected recently is kept (several can share a
 * pixel: distant models, the seams between a model's parts). */
static void precise_record(int16_t sx, int16_t sy, float fx, float fy, float fz) {
    uint32_t w = (uint16_t)sx | ((uint32_t)(uint16_t)sy << 16);
    Precise *b = pcache[phash(w)], *p = &b[0];
    for (int i = 0; i < PWAYS; i++) {
        if (b[i].word == w && b[i].gen && fabsf(b[i].x - fx) < 1.0f / 256 && fabsf(b[i].y - fy) < 1.0f / 256) {
            p = &b[i];                            /* the same corner again */
            break;
        }
        if (b[i].gen < p->gen) p = &b[i];         /* else the oldest */
    }
    p->word = w;
    p->gen = pgen;
    p->x = fx;
    p->y = fy;
    p->z = fz;
}

/* Where the game stored projected points (SWC2 of an SXY register) and the
 * value stored, per RAM word: a GPU packet word still holding that value is
 * a corner the GTE projected (gpu.c uses this to tell 3D from 2D in
 * widescreen). Packet words the game's own code writes never match, even
 * where 3D sat in an earlier frame. */
static uint32_t sxy_word[0x80000], sxy_gen[0x80000];
static float sxy_px[0x80000], sxy_py[0x80000], sxy_pz[0x80000];   /* that corner's exact position, depth */

uint8_t g_sxy_mark[0x80000];
uint32_t g_sxy_filter[8];
static int g_sxy_moving;

/* The game's code also moves projected points itself: read from the GTE
 * (MFC2) or from where it stored them, then written into a drawing packet
 * or a buffer of its own. Found by value alone, a corner that stays on the
 * same pixel can take another frame's or another corner's position, and
 * polygons jump as the view moves. Points just read are kept here, and a
 * store of the same value soon after takes the point along to its new
 * address, which the GPU then finds by address (as PGXP follows copies). */
#define MOVING 16
static struct { uint32_t word, gen; float x, y, z; } moving[MOVING];
static int moving_at;

static void moving_push(uint32_t word, float x, float y, float z) {
    moving_at = (moving_at + 1) % MOVING;
    moving[moving_at].word = word;
    moving[moving_at].gen = pgen;
    moving[moving_at].x = x;
    moving[moving_at].y = y;
    moving[moving_at].z = z;
    g_sxy_moving = 1;
    memset(g_sxy_filter, 0, sizeof g_sxy_filter);
    for (int k = 0; k < MOVING; k++) {
        uint32_t h = (moving[k].word * 0x9E3779B1u) >> 24;
        if (moving[k].gen) g_sxy_filter[h >> 5] |= 1u << (h & 31);
    }
}

/* The newest point just read with this value, or -1. */
static int moving_find(uint32_t word) {
    for (int n = 0; n < MOVING; n++) {
        int k = (moving_at - n + MOVING) % MOVING;
        if (moving[k].word == word && moving[k].gen && moving[k].gen + 2 >= pgen) return k;
    }
    return -1;
}

static void moving_frac(uint32_t word, float *x, float *y) {
    int k = g_sxy_moving ? moving_find(word) : -1;
    if (k >= 0) { *x = moving[k].x; *y = moving[k].y; }
}

static void sxy_set(uint32_t i, uint32_t word, float x, float y, float z) {
    sxy_word[i] = word;
    sxy_gen[i] = pgen;
    sxy_px[i] = x;
    sxy_py[i] = y;
    sxy_pz[i] = z;
    g_sxy_mark[i] = 1;
}

void sxy_load(uint32_t p, uint32_t v) {
    uint32_t i = p >> 2;
    if (sxy_word[i] == v && sxy_gen[i] + 6 >= pgen) moving_push(v, sxy_px[i], sxy_py[i], sxy_pz[i]);
    else g_sxy_mark[i] = 0;
}

void sxy_store(uint32_t p, uint32_t v) {
    uint32_t i = p >> 2;
    int k = g_sxy_moving ? moving_find(v) : -1;
    if (k >= 0) sxy_set(i, v, moving[k].x, moving[k].y, moving[k].z);
    else g_sxy_mark[i] = 0;
}

/* The scratchpad, where the game's drawing code keeps points too. */
static struct { uint32_t word, gen; float x, y, z; } sc_sxy[256];

void sxy_scratch_load(uint32_t p, uint32_t v) {
    uint32_t i = (p & 0x3FC) >> 2;
    if (sc_sxy[i].word == v && sc_sxy[i].gen && sc_sxy[i].gen + 6 >= pgen) moving_push(v, sc_sxy[i].x, sc_sxy[i].y, sc_sxy[i].z);
}

void sxy_scratch_store(uint32_t p, uint32_t v) {
    uint32_t i = (p & 0x3FC) >> 2;
    int k = g_sxy_moving ? moving_find(v) : -1;
    if (k < 0) { sc_sxy[i].gen = 0; return; }
    sc_sxy[i].word = v;
    sc_sxy[i].gen = pgen;
    sc_sxy[i].x = moving[k].x;
    sc_sxy[i].y = moving[k].y;
    sc_sxy[i].z = moving[k].z;
}

void gte_swc2(uint32_t addr, int reg) {
    uint32_t v = gte_read_data(reg);         /* an SXY: now among the points just read */
    MEM_W32(addr, v);                        /* (RAM or scratchpad: takes the point along) */
}

int gte_stored_sxy(uint32_t addr, uint32_t word) {
    if (!addr || (addr & 0x1FFFFFFF) >= 0x00800000) return 0;
    uint32_t i = (addr & 0x1FFFFF) >> 2;
    return sxy_gen[i] && sxy_gen[i] + 8 >= pgen && sxy_word[i] == word;
}

/* The word's cache entry, if live. */
static Precise *precise_find(uint32_t word) {
    Precise *b = pcache[phash(word)];
    for (int i = 0; i < PWAYS; i++)
        if (b[i].word == word && b[i].gen && b[i].gen + 6 >= pgen) return &b[i];
    return NULL;
}

static float g_weld = -1;                       /* weld radius, pixels (RT_WELD) */
#define WELD (g_weld >= 0 ? g_weld : (g_weld = getenv("RT_WELD") ? (float)atof(getenv("RT_WELD")) : 0.35f))


static unsigned st_addr, st_addr_amb, st_hash, st_hash_amb, st_miss;
void gte_precise_stats(void) {
    if (!getenv("RT_PSTAT")) return;
    rt_log("precise: address %u (welded %u), by value %u (-) %u, none %u", st_addr, st_addr_amb, st_hash, st_hash_amb, st_miss);
    st_addr = st_addr_amb = st_hash = st_hash_amb = st_miss = 0;
}

int gte_precise(uint32_t addr, uint32_t word, float *x, float *y, float *z) {
    /* The corner the GTE stored at this very address: its own position.
     * (Looking the word up alone mixes up corners that land on the same
     * pixel, which small, distant models are full of.) */
    static int old = -1;
    if (old < 0) old = getenv("RT_PRECISE_OLD") != NULL;
    if (!old && addr && (addr & 0x1FFFFFFF) < 0x00800000) {
        uint32_t i = (addr & 0x1FFFFF) >> 2;
        if (sxy_gen[i] && sxy_gen[i] + 6 >= pgen && sxy_word[i] == word) {
            static int stat = -1;
            if (stat < 0) stat = getenv("RT_PRECISE_STAT") != NULL;
            if (stat) {
                static unsigned n, diff;
                Precise *b = pcache[phash(word)];
                for (int j = 0; j < PWAYS; j++)
                    if (b[j].word == word && b[j].gen && b[j].gen + 6 >= pgen) {
                        n++;
                        if (fabsf(b[j].x - sxy_px[i]) > 0.25f || fabsf(b[j].y - sxy_py[i]) > 0.25f) diff++;
                        break;
                    }
                if ((n & 0x3FFFF) == 0) rt_log("precise: %u lookups, %u differ by more than 1/4 pixel", n, diff);
            }
            if (z) *z = sxy_pz[i];
            st_addr++;
            /* Corners a hair apart (where a model's parts meet: each part has
             * its own copy of the seam's corners) are welded to the one the
             * pixel's entry has, as the PS1's whole pixels weld them; apart
             * by their fractions alone they'd open cracks along the seams. */
            /* Corners a hair apart, around this one or across a pixel edge
             * (where a model's parts meet, each part has its own copy of the
             * seam's corners), are welded to one point, as the PS1's whole
             * pixels weld them: apart by their fractions alone they'd open
             * cracks along the seams. Every corner of such a group picks the
             * same one (the leftmost, then topmost). */
            float ex = sxy_px[i], ey = sxy_py[i], bx = ex, by = ey;
            int16_t px = (int16_t)(word & 0xFFFF), py = (int16_t)(word >> 16);
            for (int dy = -1; dy <= 1; dy++)
                for (int dx = -1; dx <= 1; dx++) {
                    uint32_t w = (uint16_t)(px + dx) | ((uint32_t)(uint16_t)(py + dy) << 16);
                    const Precise *b = pcache[phash(w)];
                    for (int j = 0; j < PWAYS; j++) {
                        const Precise *e = &b[j];
                        if (e->word != w || !e->gen || e->gen + 6 < pgen) continue;
                        if (fabsf(e->x - ex) >= WELD || fabsf(e->y - ey) >= WELD) continue;
                        if (e->x < bx || (e->x == bx && e->y < by)) { bx = e->x; by = e->y; }
                    }
                }
            if (bx != ex || by != ey) st_addr_amb++;
            *x = bx;
            *y = by;
            return 2;
            *x = sxy_px[i];
            *y = sxy_py[i];
            return 2;
        }
    }
    Precise *b = pcache[phash(word)];
    /* The game may take several vblanks to build a frame (20 or 15 fps
     * in busy scenes) before the GPU draws it, so entries stay valid for a
     * few vblanks. The newest projection of the word wins; if another one
     * at a different depth is about too, the corner isn't certain (its
     * depth is left out of perspective texturing). */
    int best = -1;
    for (int i = 0; i < PWAYS; i++) {
        if (b[i].word != word || !b[i].gen || b[i].gen + 6 < pgen) continue;
        if (best < 0 || b[i].gen > b[best].gen) best = i;
    }
    if (best >= 0) {
        if (z) *z = b[best].z;
        st_hash++;
        int amb = 0;
        for (int j = 0; j < PWAYS; j++)
            if (j != best && b[j].word == word && b[j].gen && b[j].gen + 6 >= pgen && fabsf(b[j].z - b[best].z) > 16) amb = 1;
        st_hash_amb += amb;
        *x = b[best].x;
        *y = b[best].y;
        return amb ? 1 : 2;
    }
    st_miss++;
    return 0;
}

void gte_state(StateIO *io) {
    ss_module(io, "gte");
    SS(io, G);
    SS(io, sf_shift);
    SS(io, lm_flag);
}

/* After loading: positions remembered from before belong to another game. */
void gte_reset_caches(void) {
    memset(pcache, 0, sizeof pcache);
    memset(sxy_gen, 0, sizeof sxy_gen);
}
