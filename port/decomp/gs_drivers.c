/* libgs primitive drivers (kernel): the functions that turn a model's
 * polygon list into GPU packets, run for every visible model part every
 * frame (GsSortObject's polygon drivers, chosen per primitive type by
 * models.c). The busiest code in the game.
 *
 * A driver gets an argument block (a0):
 *   +0x00  the primitive header (u16 count at +2; the list's offset, in
 *          words from +0x14's base, at +4): advanced past the header
 *   +0x04  the ordering table (its tag array at +4)
 *   +0x08  ordering table shift   +0x0C  depth offset
 *   +0x10  the packet area         +0x14  the polygon list base
 *   +0x18  vertices (8 bytes each) +0x1C  normals (8 bytes each)
 * and returns the header pointer +8; the packet area pointer is left in
 * 0x80065C08.
 *
 * The GTE work is done as the original does it, register for register
 * (the high-resolution renderer keeps fractional screen positions per
 * address written by swc2, so stores go through gte_swc2).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "libgs.h"
#include "poll.h"

void n_gs_DriverGT4Lit(void);
void n_gs_SortFastSprite(void);
void n_gs_DivMidpoint2(void);
void n_gs_TriOffscreen(void);
void n_gs_TriOffscreen8(void);
void n_gs_DivMidpoint3(void);
void n_gs_DivMidpointF2(void);
void n_gs_DivMidpointF3(void);
void n_gs_DivMidpoint4(void);
void n_gs_DivMidpointF4(void);
void n_gs_DivMidpointF5(void);
void n_gs_DivTriGT3b(void);
void n_gs_DivTriFT3b(void);
void n_gs_TriGT3Packet(void);
void n_gs_TriFT3Packet(void);
void n_gs_ProjectVerts(void);
void n_gs_TriFT3Packet_8004EC24(void);
void n_gs_FogGT4Lit(void);
void n_gs_FogGT3Lit(void);
void n_gs_DriverF3(void);
void n_gs_DriverG3Lit(void);
void n_gs_DriverF4Lit(void);
void n_gs_DriverFT4Lit(void);
void n_gs_DriverG4Lit(void);
void n_gs_DivTriGT3R(void);
void n_gs_DivTriFT3R(void);
u32 bit_width(s32 n, u32 poll);   /* gte_math.c */
void n_gs_BitWidth_800302C4(void);
void n_gs_BitWidth_8003092C(void);
void n_gs_BitWidth_80030FB0(void);
void n_gs_TriOffscreen8_80048A04(void);
void n_gs_TriOffscreen8_8004EAD4(void);
void n_gs_TriOffscreen8_80055CE4(void);
void n_gs_TriOffscreen_8004942C(void);
void n_gs_TriOffscreen_80050374(void);
void n_gs_TriOffscreen_800568D8(void);
void n_gs_DivMidpointF_8004ED58(void);
void n_gs_DivMidpointF_80055E34(void);
void n_gs_DivMidpointF_800575DC(void);
void n_gs_DivMidpoint_8004957C(void);
void n_gs_DivMidpoint_80050610(void);
void n_gs_DivMidpoint_80056A28(void);
void n_gs_DivMidpoint_8005841C(void);
void n_gs_TriGT3Packet_800504C4(void);
void n_gs_DivOffscreenF_800464FC(void);
void n_gs_DivOffscreenF_80049F84(void);
void n_gs_DivOffscreenF_80052700(void);
void n_gs_DivOffscreenF_800535F4(void);
void n_gs_DivOffscreenF_8005741C(void);
void n_gs_DivOffscreen_8004ABBC(void);
void n_gs_DivOffscreen_80055168(void);
void n_gs_DivOffscreen_8005825C(void);
void n_gs_DriverGT3Lit(void);
void n_gs_DivMidpoint(void);
void n_gs_DivTriGT3(void);
void n_gs_DivOffscreen(void);
void n_gs_DivMidpointF(void);
void n_gs_SortUnit(void);
void n_gs_DivTriFT3(void);
void n_gs_DivOffscreenF(void);
void n_gs_DivQuadGT4(void);
void n_gs_DivQuadFT4(void);
void n_gs_DivQuadGT4c(void);
void n_gs_DivQuadGT4f(void);
void n_gs_DivQuadFT4f(void);
void n_gs_DriverFT4Div(void);
void n_gs_DivTriF3R(void);
void n_gs_DriverGT4Div(void);
void n_gs_DriverGT3Div(void);
void n_gs_DivTriGT3S(void);
void n_gs_DriverFT3Div(void);
void n_gs_SortSprite(void);
void n_gs_DivOffscreenF4(void);
void n_gs_SharedGT3(void);
void n_gs_SharedGT4(void);
void n_gs_DriverG4Div(void);
void n_gs_DivQuadG4(void);
void n_gs_WaterFT3(void);
void n_gs_WaterGT3(void);
void n_gs_WaterFT4(void);
void n_gs_WaterGT4(void);
void n_gs_WaterFT4Fog(void);
void n_gs_WaterGT4Fog(void);
void n_gs_DivQuadFT4b(void);
void n_gs_WaterFT3Fog(void);
void n_gs_WaterGT3Fog(void);
void n_gs_DriverFT3Lit(void);
void n_gs_DriverF4Div(void);
void n_gs_TriG3Packet(void);
void n_gs_Init3D(void);
void n_gs_InitGraph(void);
void n_gs_GetTimInfo(void);

#define g_packetArea    GV(u32, 0x80065C08)

#define GTE_RTPS  0x0180001u
#define GTE_RTPT  0x0280030u
#define GTE_NCLIP 0x1400006u
#define GTE_AVSZ3 0x158002Du
#define GTE_AVSZ4 0x168002Eu
#define GTE_NCCS  0x108041Bu
#define GTE_NCCT  0x118043Fu

/* Load GTE vector register pair v (0, 1 or 2) from an 8-byte vertex. */
static inline void gte_vec(int v, u32 p) {
    gte_write_data(v * 2, MEM_R32(p));
    gte_write_data(v * 2 + 1, MEM_R32(p + 4));
}

/* The original's register saves, on its stack (which can be in the
 * scratchpad): s0 up at sp+0. */
static inline void save_s_regs(u32 sp, int n) {
    for (int i = 0; i < n; i++) MEM_W32(sp + (u32)i * 4, R[16 + i]);
}

/* 8003EDE8: lit, gouraud-shaded, textured quads (POLY_GT4, 13 words),
 * a normal per corner. Each 0x1C-byte polygon: words 0-2 copied to the
 * packet (CLUT/UV words), then four (vertex << 16 | normal) words (the
 * fourth corner's UV half from the first). Back faces (and ones the GTE
 * flags) are skipped; the rest sorted by average depth. */
void n_gs_DriverGT4Lit(void) {
    FRAME(0x20);
    save_s_regs(sp, 8);
    u32 args = A0;
    u32 hdr = MEM_R32(args);
    u32 pk = MEM_R32(args + 0x10);
    u32 verts = MEM_R32(args + 0x18);
    u32 norms = MEM_R32(args + 0x1C);
    u32 zoff = MEM_R32(args + 0xC);
    u32 n = MEM_R16(hdr + 2);
    MEM_W32(args, hdr + 4);
    u32 poly = MEM_R32(args + 0x14) + ((MEM_R32(hdr + 4) & 0xFFFFFFu) << 2);
    u32 shift = MEM_R32(args + 8);
    u32 ot = MEM_R32(MEM_R32(args + 4) + 4);
    gte_write_data(6, 0x3C808080u);
    u32 w0 = MEM_R32(poly + 0xC), w1 = MEM_R32(poly + 0x10), w2 = MEM_R32(poly + 0x14), w3 = MEM_R32(poly + 0x18);
    for (u32 i = 0; i < n;) {
        gte_vec(0, verts + ((u32)((s32)w0 >> 16) << 3));
        gte_vec(1, verts + ((u32)((s32)w1 >> 16) << 3));
        gte_vec(2, verts + ((u32)((s32)w2 >> 16) << 3));
        gte_command(GTE_RTPT);
        u32 n0 = w0 & 0xFFFF, n1 = w1 & 0xFFFF, n2 = w2 & 0xFFFF;
        u32 v3 = verts + ((u32)((s32)w3 >> 16) << 3), n3 = w3 & 0xFFFF;
        w0 = MEM_R32(poly + 0x28);                 /* the next polygon's */
        w1 = MEM_R32(poly + 0x2C);
        w2 = MEM_R32(poly + 0x30);
        w3 = MEM_R32(poly + 0x34);
        if ((s32)gte_read_ctrl(31) >= 0) {
            gte_command(GTE_NCLIP);
            if ((s32)gte_read_data(24) > 0) {
                gte_swc2(pk + 0x8, 12);
                gte_swc2(pk + 0x14, 13);
                gte_swc2(pk + 0x20, 14);
                gte_vec(0, v3);
                gte_command(GTE_RTPS);
                gte_vec(1, norms + (n1 << 3));
                gte_command(GTE_AVSZ4);
                gte_vec(2, norms + (n2 << 3));
                gte_vec(0, norms + (n3 << 3));
                u32 z = gte_read_data(7) - zoff;
                gte_command(GTE_NCCS);
                u32 c2 = MEM_R32(poly + 8), c0 = MEM_R32(poly), c1 = MEM_R32(poly + 4);
                gte_vec(0, norms + ((u32)((s32)c2 >> 16) << 3));
                gte_swc2(pk + 0x28, 22);
                z = (u32)((s32)z >> (shift & 31));
                gte_command(GTE_NCCT);
                MEM_W32(pk + 0xC, c0);
                MEM_W32(pk + 0x18, c1);
                MEM_W32(pk + 0x24, c2);
                MEM_W32(pk + 0x30, n0);
                gte_swc2(pk + 0x2C, 14);
                u32 tag = ot + (z << 2);
                MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFFu) | 0x0C000000u);
                gte_swc2(pk + 4, 20);
                gte_swc2(pk + 0x10, 21);
                gte_swc2(pk + 0x1C, 22);
                MEM_W32(tag, pk & 0xFFFFFFu);
                pk += 0x34;
            }
        }
        i++;
        poly += 0x1C;
        if (!(i < n)) break;
        LOOP_POLL(0x8003F044u);
    }
    g_packetArea = pk;
    V0 = MEM_R32(args) + 4;
}

/* 8003E530: lit, gouraud-shaded, textured triangles (POLY_GT3, 10 words),
 * a normal per corner. Each 0x18-byte polygon: words 0-2 copied to the
 * packet (CLUT/UV words), then three (vertex << 16 | normal) words. Back
 * faces (and ones the GTE flags) are skipped; the rest sorted by average
 * depth. */
void n_gs_DriverGT3Lit(void) {
    FRAME(0x20);
    save_s_regs(sp, 7);
    u32 args = A0;
    u32 hdr = MEM_R32(args);
    u32 pk = MEM_R32(args + 0x10);
    u32 verts = MEM_R32(args + 0x18);
    u32 norms = MEM_R32(args + 0x1C);
    u32 n = MEM_R16(hdr + 2);
    MEM_W32(args, hdr + 4);
    u32 poly = MEM_R32(args + 0x14) + ((MEM_R32(hdr + 4) & 0xFFFFFFu) << 2);
    gte_write_data(6, 0x34808080u);
    u32 w0 = MEM_R32(poly + 0xC), w1 = MEM_R32(poly + 0x10);
    u32 ot = MEM_R32(MEM_R32(args + 4) + 4);
    u32 w2 = MEM_R32(poly + 0x14);
    u32 zoff = MEM_R32(args + 0xC), shift = MEM_R32(args + 8);
    for (u32 i = 0; i < n;) {
        gte_vec(0, verts + ((u32)((s32)w0 >> 16) << 3));
        gte_vec(1, verts + ((u32)((s32)w1 >> 16) << 3));
        gte_vec(2, verts + ((u32)((s32)w2 >> 16) << 3));
        gte_command(GTE_RTPT);
        u32 n0 = w0 & 0xFFFF, n1 = w1 & 0xFFFF, n2 = w2 & 0xFFFF;
        w0 = MEM_R32(poly + 0x24);                 /* the next polygon's */
        w1 = MEM_R32(poly + 0x28);
        s32 flag = (s32)gte_read_ctrl(31);
        gte_command(GTE_NCLIP);
        w2 = MEM_R32(poly + 0x2C);
        if ((s32)gte_read_data(24) > 0 && flag >= 0) {
            MEM_W32(pk + 0xC, MEM_R32(poly));
            gte_command(GTE_AVSZ3);
            gte_vec(0, norms + (n0 << 3));
            gte_vec(1, norms + (n1 << 3));
            gte_vec(2, norms + (n2 << 3));
            u32 z = gte_read_data(7);
            gte_command(GTE_NCCT);
            z = (u32)((s32)(z - zoff) >> (shift & 31));
            u32 tag = ot + (z << 2);
            MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFFu) | 0x09000000u);
            MEM_W32(tag, pk & 0xFFFFFFu);
            gte_swc2(pk + 0x8, 12);
            gte_swc2(pk + 0x14, 13);
            gte_swc2(pk + 0x20, 14);
            gte_swc2(pk + 0x4, 20);
            gte_swc2(pk + 0x10, 21);
            gte_swc2(pk + 0x1C, 22);
            MEM_W32(pk + 0x18, MEM_R32(poly + 4));
            MEM_W32(pk + 0x24, MEM_R32(poly + 8));
            pk += 0x28;
        }
        i++;
        poly += 0x18;
        if (!(i < n)) break;
        LOOP_POLL(0x8003E728u);
    }
    g_packetArea = pk;
    V0 = MEM_R32(args) + 4;
}

/* 80048158 (out, a, b): the midpoint of two subdivision vertices (for
 * polygons split into smaller ones near the camera): position (s16 x,
 * y, z, halves rounded toward zero), colour bytes 6-7, UV and CLUT
 * bytes 8-A averaged; byte B taken from a. */
static inline u32 avg_s16(u32 a, u32 b) {
    s32 s = (s32)(s16)MEM_R16(a) + (s32)(s16)MEM_R16(b);
    return (u32)((s + (s32)((u32)s >> 31)) >> 1);
}
static inline u32 avg_u8(u32 a, u32 b) { return (u32)((s32)(MEM_R8(a) + MEM_R8(b)) >> 1); }
void n_gs_DivMidpoint(void) {
    u32 o = A0, a = A1, b = A2;
    MEM_W16(o, avg_s16(a, b));
    MEM_W16(o + 2, avg_s16(a + 2, b + 2));
    MEM_W16(o + 4, avg_s16(a + 4, b + 4));
    MEM_W8(o + 8, avg_u8(a + 8, b + 8));
    MEM_W8(o + 9, avg_u8(a + 9, b + 9));
    MEM_W8(o + 0xA, avg_u8(a + 0xA, b + 0xA));
    MEM_W8(o + 0xB, MEM_R8(a + 0xB));
    MEM_W8(o + 6, avg_u8(a + 6, b + 6));
    u32 v = avg_u8(a + 7, b + 7);
    MEM_W8(o + 7, v);
    V0 = v;
}

/* 8004800C (a, b, c, work): one triangle of a subdivided polygon, as a
 * POLY_GT3 packet: the three vertices projected (flags kept in work+0x28,
 * the average depth in work+0x18), skipped if the GTE flags it or it's
 * edge-on; colours from the vertices' words 2, UVs from their halves at
 * 6, CLUT and texture page from work+0x4A / +0x56; sorted into the
 * ordering table (work+0x10, shift work+0xC; the entry kept in
 * work+0x38). work+0x14 is the packet pointer. */
void n_gs_DivTriGT3(void) {
    u32 a = A0, b = A1, c = A2, w = A3;
    gte_vec(0, a);
    gte_vec(1, b);
    gte_vec(2, c);
    gte_command(GTE_RTPT);
    u32 pk = MEM_R32(w + 0x14);
    MEM_W32(pk + 4, MEM_R32(a + 8));
    MEM_W8(pk + 3, 9);
    MEM_W8(pk + 7, 0x34);
    MEM_W32(w + 0x28, gte_read_ctrl(31));
    u32 flag = MEM_R32(w + 0x28);
    V0 = flag;
    if ((s32)flag < 0) return;
    gte_command(GTE_NCLIP);
    if (gte_read_data(24) == 0) return;
    gte_command(GTE_AVSZ3);
    gte_swc2(pk + 0x8, 12);
    gte_swc2(pk + 0x14, 13);
    gte_swc2(pk + 0x20, 14);
    gte_swc2(w + 0x18, 7);
    MEM_W32(pk + 0xC, MEM_R16(a + 6));
    MEM_W32(pk + 0x18, MEM_R16(b + 6));
    MEM_W32(pk + 0x24, MEM_R16(c + 6));
    MEM_W32(pk + 0x10, MEM_R32(b + 8));
    MEM_W32(pk + 0x1C, MEM_R32(c + 8));
    MEM_W16(pk + 0xE, MEM_R16(w + 0x4A));
    MEM_W16(pk + 0x1A, MEM_R16(w + 0x56));
    u32 tag = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
    MEM_W32(w + 0x38, tag);
    MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFFu) | 0x09000000u);
    MEM_W32(MEM_R32(w + 0x38), pk & 0xFFFFFFu);
    V0 = MEM_R32(w + 0x14) + 0x28;
    MEM_W32(w + 0x14, V0);
}

/* 80047E4C (quad, work): the screen bounding box of a subdivided quad's
 * corners (x at +8, +0x14, +0x20, +0x2C, y two bytes on) into work+0x2C
 * (min x, min y) and +0x30 (max x, max y); 1 if it's entirely outside
 * the screen (half width, half height at work+0x34, +0x36), else 0.
 * 80045810 does the same for a flat quad's corners (+8, +0x10, +0x18,
 * +0x20). */
static void bound(u32 q, u32 w, u32 off, u32 st) {
    u32 lo = w + 0x2C + off, hi = w + 0x30 + off;
    if ((s16)MEM_R16(q + 8 + st + off) < (s16)MEM_R16(q + 8 + off)) {
        MEM_W16(hi, MEM_R16(q + 8 + off));
        MEM_W16(lo, MEM_R16(q + 8 + st + off));
    } else {
        MEM_W16(lo, MEM_R16(q + 8 + off));
        MEM_W16(hi, MEM_R16(q + 8 + st + off));
    }
    for (u32 k = 8 + 2 * st; k <= 8 + 3 * st; k += st) {
        s16 v = (s16)MEM_R16(q + k + off);
        if ((s16)MEM_R16(hi) < v) MEM_W16(hi, (u16)v);
        else if (v < (s16)MEM_R16(lo)) MEM_W16(lo, (u16)v);
    }
}
static u32 offscreen(u32 q, u32 w, u32 st, u32 poll1, u32 poll2) {
    bound(q, w, 0, st);
    s32 hw = (s16)MEM_R16(w + 0x34);
    if ((s16)MEM_R16(w + 0x30) < -hw || hw < (s16)MEM_R16(w + 0x2C)) return 1;
    bound(q, w, 2, st);
    s32 hh = (s16)MEM_R16(w + 0x36);
    if ((s16)MEM_R16(w + 0x32) < -hh) { LOOP_POLL(poll1); return 1; }
    if (hh < (s16)MEM_R16(w + 0x2E)) { LOOP_POLL(poll2); return 1; }
    return 0;
}
void n_gs_DivOffscreen(void) { V0 = offscreen(A0, A1, 0xC, 0x80047FE8u, 0x80047FFCu); }
void n_gs_DivOffscreenF(void) { V0 = offscreen(A0, A1, 8, 0x800459ACu, 0x800459C0u); }
/* 80044010: the test for 4-byte vertices (a POLY_F4 packet's corners). */
void n_gs_DivOffscreenF4(void) { V0 = offscreen(A0, A1, 4, 0x800441ACu, 0x800441C0u); }

/* 80045B04 (out, a, b): the midpoint of two subdivision vertices without
 * UVs: position and colour bytes 6-7 averaged. */
void n_gs_DivMidpointF(void) {
    u32 o = A0, a = A1, b = A2;
    MEM_W16(o, avg_s16(a, b));
    MEM_W16(o + 2, avg_s16(a + 2, b + 2));
    MEM_W16(o + 4, avg_s16(a + 4, b + 4));
    MEM_W8(o + 6, avg_u8(a + 6, b + 6));
    u32 v = avg_u8(a + 7, b + 7);
    MEM_W8(o + 7, v);
    V0 = v;
}

/* 800333D8 (unit list, ordering table, args): sort a model's units: for
 * each unit in the list (+0 next, -1 ends; +4 its block: a count and
 * that many words copied to args+0x14.., then a count of primitive
 * blocks, each run by the driver whose address leads it, with args
 * pointing at the block (the driver moves it on and returns where the
 * next one starts). args+4 is the ordering table, +8 its shift (14 less
 * its length), +0xC its offset, +0x10 the packet area, kept up to date
 * between drivers. */
void n_gs_SortUnit(void) {
    FRAME(0x28);
    u32 unit = A0, ot = A1, args = A2;
    MEM_W32(args, MEM_R32(unit + 4));
    u32 node = MEM_R32(unit + 4);
    MEM_W32(args + 4, ot);
    MEM_W32(args + 8, 14 - MEM_R32(ot));
    MEM_W32(args + 0x10, g_packetArea);
    MEM_W32(args + 0xC, MEM_R32(ot + 8));
    for (;;) {
        u32 cur = node;
        MEM_W32(args, node);
        node = MEM_R32(node);
        MEM_W32(args, cur + 4);
        u32 t = MEM_R32(cur + 4);
        if (MEM_R32(t) != 0) {
            for (u32 k = 0;;) {
                MEM_W32(args + 0x14 + k * 4, MEM_R32(t + 4 + k * 4));
                k++;
                if (!(k < MEM_R32(t))) break;
                LOOP_POLL(0x80033480u);
            }
        }
        u32 p = MEM_R32(args);
        MEM_W32(args, p + 4);
        u32 n = MEM_R32(p + 4);
        MEM_W32(args, p + 8);
        if (n != 0) {
            for (u32 j = 0;;) {
                u32 q = MEM_R32(args);
                u32 fn = MEM_R32(q);
                MEM_W32(args, q + 4);
                u32 r = gcall(fn, args, R[5], R[6], R[7]);
                j++;
                MEM_W32(args, r);
                MEM_W32(args + 0x10, g_packetArea);
                if (!(j < n)) break;
                LOOP_POLL(0x800334D4u);
            }
        }
        if (node == 0xFFFFFFFFu) break;
        LOOP_POLL(0x800334DCu);
    }
    V0 = node;
}

/* 800459D0 (a, b, c, work): one triangle of a subdivided flat-shaded
 * polygon, as a POLY_FT3 packet (colour from work+0x40): like
 * gs_DivTriGT3 without the per-corner colours; CLUT and texture page
 * from work+0x4A / +0x52. */
void n_gs_DivTriFT3(void) {
    u32 a = A0, b = A1, c = A2, w = A3;
    gte_vec(0, a);
    gte_vec(1, b);
    gte_vec(2, c);
    gte_command(GTE_RTPT);
    u32 pk = MEM_R32(w + 0x14);
    MEM_W32(pk + 4, MEM_R32(w + 0x40));
    MEM_W8(pk + 3, 7);
    MEM_W8(pk + 7, 0x24);
    MEM_W32(w + 0x28, gte_read_ctrl(31));
    u32 flag = MEM_R32(w + 0x28);
    V0 = flag;
    if ((s32)flag < 0) return;
    gte_command(GTE_NCLIP);
    if (gte_read_data(24) == 0) return;
    gte_command(GTE_AVSZ3);
    gte_swc2(pk + 0x8, 12);
    gte_swc2(pk + 0x10, 13);
    gte_swc2(pk + 0x18, 14);
    gte_swc2(w + 0x18, 7);
    MEM_W32(pk + 0xC, MEM_R16(a + 6));
    MEM_W32(pk + 0x14, MEM_R16(b + 6));
    MEM_W32(pk + 0x1C, MEM_R16(c + 6));
    MEM_W16(pk + 0xE, MEM_R16(w + 0x4A));
    MEM_W16(pk + 0x16, MEM_R16(w + 0x52));
    u32 tag = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
    MEM_W32(w + 0x38, tag);
    MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFFu) | 0x07000000u);
    MEM_W32(MEM_R32(w + 0x38), pk & 0xFFFFFFu);
    V0 = MEM_R32(w + 0x14) + 0x20;
    MEM_W32(w + 0x14, V0);
}

/* 8004792C / 8004535C (quad, work, depth): draw a quad subdivided until
 * its pieces are small enough on screen, gouraud-shaded and textured
 * (POLY_GT4, vertices 0xC bytes: position, UV at +6, colour at +8) or
 * flat-shaded (POLY_FT4, vertices 8 bytes, colour from the template).
 * The quad's four vertices come first, then room for its children's.
 * work+0x3C is the packet template the corners are projected into (flags
 * in work+0x28, depth in work+0x18). A quad wholly off screen is
 * dropped; at the depth limit (work+0) it's drawn unless the GTE flagged
 * it; otherwise it's drawn if smaller than work+0x20 x +0x22, and split
 * in four if not (or if its projection overflowed); one that already fits
 * the GPU's limits (1023 x 511) is split only once more. Each split also
 * runs the triangle driver over a corner, an edge midpoint and the next
 * corner (edge-on, so nothing is drawn unless the GTE flags it). */
typedef struct {
    u32 vs;                     /* vertex size = template stride */
    u32 child;                  /* the children's vertices */
    u32 words, code;            /* packet size, GP0 length tag */
    int gt;                     /* gouraud: colours from the vertices */
    u32 poll1, poll2;           /* the off-screen test's polls */
    void (*mid)(void), (*tri)(void);
    u32 ts;                     /* template stride (0: vs) */
    int g4;                     /* POLY_G4: no UVs, colour / xy pairs */
    u32 mid_a, tri_a;           /* translated midpoint / triangle parts */
} QuadKind;

static void call4(void (*f)(void), u32 a, u32 b, u32 c, u32 d) {
    R[4] = a; R[5] = b; R[6] = c; R[7] = d;
    f();
}
static void kmid(const void *kp, u32 a, u32 b, u32 c);
static void ktri(const void *kp, u32 a, u32 b, u32 c, u32 d);
static inline void copy_bytes(u32 dst, u32 src, u32 n) {
    for (u32 i = 0; i < n; i++) MEM_W8(dst + i, MEM_R8(src + i));
}

static void div_quad(const QuadKind *k, u32 q, u32 w, u32 depth) {
    u32 vs = k->vs, t = w + 0x3C, ts = k->ts ? k->ts : vs;
    gte_vec(0, q);
    gte_vec(1, q + vs);
    gte_vec(2, q + 2 * vs);
    gte_command(GTE_RTPT);
    if (!k->g4) for (u32 i = 0; i < 3; i++) MEM_W16(t + 0xC + i * vs, MEM_R16(q + 6 + i * vs));
    MEM_W32(w + 0x28, gte_read_ctrl(31));
    gte_swc2(t + 8, 12);
    gte_swc2(t + 8 + ts, 13);
    gte_swc2(t + 8 + 2 * ts, 14);
    gte_vec(0, q + 3 * vs);
    gte_command(GTE_RTPS);
    if (!k->g4) MEM_W16(t + 0xC + 3 * vs, MEM_R16(q + 6 + 3 * vs));
    MEM_W32(w + 0x24, gte_read_ctrl(31));
    MEM_W32(w + 0x28, MEM_R32(w + 0x28) | MEM_R32(w + 0x24));
    gte_swc2(t + 8 + 3 * ts, 14);
    gte_command(GTE_AVSZ4);
    MEM_W32(w + 0x18, (u32)((s32)gte_read_data(19) >> 2));
    if (offscreen(t, w, ts, k->poll1, k->poll2)) { V0 = 1; return; }

    int draw = 0;
    if (MEM_R32(w) == depth) {
        u32 f = MEM_R32(w + 0x28) & 0x7F85E000u;
        if (f) { V0 = f; return; }
        draw = 1;
    } else if ((s32)MEM_R32(w + 0x28) < 0) {
        depth++;
    } else {
        s32 dx = (s16)MEM_R16(w + 0x30) - (s16)MEM_R16(w + 0x2C);
        s32 dy = (s16)MEM_R16(w + 0x32) - (s16)MEM_R16(w + 0x2E);
        if (dx < (s16)MEM_R16(w + 0x20) && dy < (s16)MEM_R16(w + 0x22)) draw = 1;
        else if (dx < 1023 && dy < 511) depth = MEM_R32(w);
        else depth++;
    }
    if (draw) {
        for (u32 i = 0; i < k->words; i++) {
            u32 src = k->g4 ? ((i & 1) ? q + 8 + (i >> 1) * vs : t + i * 4)
                    : (k->gt && i % 3 == 1) ? q + 8 + (i / 3) * vs : t + i * 4;
            MEM_W32(MEM_R32(w + 0x14) + i * 4, MEM_R32(src));
        }
        u32 pk = MEM_R32(w + 0x14);
        u32 tag = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
        MEM_W32(w + 0x38, tag);
        MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFFu) | k->code);
        MEM_W32(MEM_R32(w + 0x38), MEM_R32(w + 0x14) & 0xFFFFFFu);
        V0 = MEM_R32(w + 0x14) + k->words * 4;
        MEM_W32(w + 0x14, V0);
        return;
    }

    u32 c = q + k->child;
    u32 v1 = q + vs, v2 = q + 2 * vs, v3 = q + 3 * vs;
    copy_bytes(c, q, vs);
    kmid(k, c + vs, q, v1);
    kmid(k, c + 2 * vs, q, v2);
    kmid(k, c + 3 * vs, q, v3);
    ktri(k, q, v1, c + vs, w);
    div_quad(k, c, w, depth);
    copy_bytes(c, v1, vs);
    kmid(k, c + 2 * vs, v1, v3);
    ktri(k, v1, v3, c + 2 * vs, w);
    div_quad(k, c, w, depth);
    copy_bytes(c, v3, vs);
    kmid(k, c + vs, v3, v2);
    ktri(k, v3, v2, c + vs, w);
    div_quad(k, c, w, depth);
    copy_bytes(c, v2, vs);
    kmid(k, c + 2 * vs, q, v2);
    ktri(k, v2, q, c + 2 * vs, w);
    div_quad(k, c, w, depth);
}
static void kmid(const void *kp, u32 a, u32 b, u32 c) {
    const QuadKind *k = kp;
    if (k->mid) call4(k->mid, a, b, c, 0);
    else gcall(k->mid_a, a, b, c, R[7]);
}
static void ktri(const void *kp, u32 a, u32 b, u32 c, u32 d) {
    const QuadKind *k = kp;
    if (k->tri) call4(k->tri, a, b, c, d);
    else if (k->tri_a) gcall(k->tri_a, a, b, c, d);
}

static const QuadKind k_gt4 = { 0xC, 0x30, 13, 0x0C000000u, 1, 0x80047FE8u, 0x80047FFCu,
                                n_gs_DivMidpoint, n_gs_DivTriGT3, 0, 0, 0, 0 };
static const QuadKind k_ft4 = { 8, 0x20, 10, 0x09000000u, 0, 0x800459ACu, 0x800459C0u,
                                n_gs_DivMidpointF, n_gs_DivTriFT3, 0, 0, 0, 0 };
void n_gs_DivQuadGT4(void) { div_quad(&k_gt4, A0, A1, A2); }
void n_gs_DivQuadFT4(void) { div_quad(&k_ft4, A0, A1, A2); }
/* The other driver files' copies: 80054C48 (as DivQuadGT4), and 8004A6EC
 * / 80049B20 (GT4 / FT4 without the edge triangles), each with its own
 * off-screen test's loop addresses. */
static const QuadKind k_gt4c = { 0xC, 0x30, 13, 0x0C000000u, 1, 0x80055304u, 0x80055318u,
                                 n_gs_DivMidpoint, n_gs_DivTriGT3, 0, 0, 0, 0 };
static const QuadKind k_gt4f = { 0xC, 0x30, 13, 0x0C000000u, 1, 0x8004AD58u, 0x8004AD6Cu,
                                 n_gs_DivMidpoint, 0, 0, 0, 0, 0 };
static const QuadKind k_ft4f = { 8, 0x20, 10, 0x09000000u, 0, 0x8004A120u, 0x8004A134u,
                                 n_gs_DivMidpointF, 0, 0, 0, 0, 0 };
void n_gs_DivQuadGT4c(void) { div_quad(&k_gt4c, A0, A1, A2); }
void n_gs_DivQuadGT4f(void) { div_quad(&k_gt4f, A0, A1, A2); }
void n_gs_DivQuadFT4f(void) { div_quad(&k_ft4f, A0, A1, A2); }
/* 8005224C: DivQuadFT4's copy in the 0x80050000 driver file. */
static const QuadKind k_ft4b = { 8, 0x20, 10, 0x09000000u, 0, 0x8005289Cu, 0x800528B0u,
                                 n_gs_DivMidpointF5, n_gs_DivTriFT3b, 0, 0, 0, 0 };
void n_gs_DivQuadFT4b(void) { div_quad(&k_ft4b, A0, A1, A2); }
/* 80046048: the POLY_G4 version (no UVs, the template's corners 8 bytes
 * apart, colours from the 0xC-byte vertices; midpoints 800467CC, edge
 * triangles 800466BC). */
static const QuadKind k_g4 = { 0xC, 0x30, 9, 0x08000000u, 0, 0x80046698u, 0x800466ACu,
                               0, 0, 8, 1, 0x800467CCu, 0x800466BCu };
void n_gs_DivQuadG4(void) { div_quad(&k_g4, A0, A1, A2); }

/* 8002C4B8 GsSortFastSprite(sp, ot, pri): a GsSPRITE (attribute +0, x
 * +4, y +6, w +8, h +0xA, tpage +0xC, u +0xE, v +0xF, clut x +0x10, y
 * +0x12, rgb +0x14..0x16) as a draw mode and SPRT packet (0x18 bytes at
 * the packet pointer 0x80065C08), at the screen offset (0x80065A84),
 * linked into the OT at pri. Hidden (attribute bit 31) or empty: nothing. */
void n_gs_SortFastSprite(void) {
    u32 s = A0, ot = A1, pri = A2;
    u32 attr = MEM_R32(s);
    if ((s32)attr < 0 || MEM_R16(s + 8) == 0 || MEM_R16(s + 0xA) == 0) return;
    u32 pk = GV(u32, 0x80065C08u);
    MEM_W32(pk + 4, (MEM_R16(s + 0xC) & 0x1F) | ((attr >> 17) & 0x180) | 0xE1000200u | ((attr >> 23) & 0x60));
    u32 x = (MEM_R16(s + 4) + GV(u16, 0x80065A84u)) & 0xFFFF;
    u32 y = MEM_R16(s + 6) + GV(u16, 0x80065A86u);
    MEM_W32(pk + 0xC, x | (y << 16));
    MEM_W32(pk + 8, ((attr >> 5) & 0x02000000u) | ((attr << 18) & 0x01000000u) | 0x64000000u
                    | ((u32)MEM_R8(s + 0x16) << 16) | ((u32)MEM_R8(s + 0x15) << 8) | MEM_R8(s + 0x14));
    MEM_W32(pk + 0x10, (u32)MEM_R8(s + 0xE) | ((u32)MEM_R8(s + 0xF) << 8)
                     | ((u32)(s32)(s16)MEM_R16(s + 0x12) << 22)
                     | (((u32)(s32)(s16)MEM_R16(s + 0x10) << 12) & 0x3F0000));
    MEM_W32(pk + 0x14, MEM_R16(s + 8) | ((u32)MEM_R16(s + 0xA) << 16));
    u32 tag = MEM_R32(ot + 4) + ((pri & 0xFFFF) << 2) - (MEM_R32(ot + 8) << 2);
    MEM_W32(pk, MEM_R32(tag) + 0x05000000u);
    MEM_W32(tag, pk & 0xFFFFFF);
    GV(u32, 0x80065C08u) = pk + 0x18;
}

/* 80043670: the same midpoint, in another driver file's copy. */
void n_gs_DivMidpoint2(void) { n_gs_DivMidpoint(); }

/* One axis of the triangle tests: the three vertices' coordinate (+off,
 * stride apart) into min (work + lo) and max (work + hi). */
static void tri_bounds(u32 v, u32 w, u32 off, u32 stride, u32 lo, u32 hi) {
    u32 a = MEM_R16(v + off), b = MEM_R16(v + off + stride), c = MEM_R16(v + off + 2 * stride);
    if ((s16)b < (s16)a) { MEM_W16(w + hi, (u16)a); MEM_W16(w + lo, (u16)b); }
    else { MEM_W16(w + lo, (u16)a); MEM_W16(w + hi, (u16)b); }
    if ((s16)MEM_R16(w + hi) < (s16)c) MEM_W16(w + hi, (u16)c);
    else if ((s16)c < (s16)MEM_R16(w + lo)) MEM_W16(w + lo, (u16)c);
}

/* Is the triangle (vertices stride apart, x at +8, y at +0xA) entirely
 * off screen? Its bounds (x into work + 0x28 / 0x2C, y + 0x2A / 0x2E)
 * against the half screen (work + 0x30, + 0x32, centred on 0): 1 if it
 * is, else 0. */
static void tri_offscreen(u32 stride, u32 poll1, u32 poll2) {
    u32 v = A0, w = A1;
    tri_bounds(v, w, 8, stride, 0x28, 0x2C);
    s32 hw = (s16)MEM_R16(w + 0x30);
    if ((s16)MEM_R16(w + 0x2C) < -hw || hw < (s16)MEM_R16(w + 0x28)) { V0 = 1; return; }
    tri_bounds(v, w, 0xA, stride, 0x2A, 0x2E);
    s32 hh = (s16)MEM_R16(w + 0x32);
    if ((s16)MEM_R16(w + 0x2E) < -hh) { LOOP_POLL(poll1); V0 = 1; return; }
    if (hh < (s16)MEM_R16(w + 0x2A)) { LOOP_POLL(poll2); V0 = 1; return; }
    V0 = 0;
}

/* 800433D4 (verts, work): tri_offscreen, 0xC-byte vertices. */
void n_gs_TriOffscreen(void) { tri_offscreen(0xC, 0x80043500u, 0x80043514u); }
/* 80041880 (verts, work): tri_offscreen, 8-byte vertices. */
void n_gs_TriOffscreen8(void) { tri_offscreen(8, 0x800419ACu, 0x800419C0u); }

/* More driver files' copies of the midpoints: 8004AD7C (DivMidpoint),
 * 80055474 (DivMidpoint), 80041B04, 8004A144, 80048B54 and 800529F4
 * (DivMidpointF). */
void n_gs_DivMidpoint3(void) { n_gs_DivMidpoint(); }
void n_gs_DivMidpointF2(void) { n_gs_DivMidpointF(); }
void n_gs_DivMidpointF3(void) { n_gs_DivMidpointF(); }
void n_gs_DivMidpoint4(void) { n_gs_DivMidpoint(); }
void n_gs_DivMidpointF4(void) { n_gs_DivMidpointF(); }
void n_gs_DivMidpointF5(void) { n_gs_DivMidpointF(); }

/* Copies in the fogged drivers' file: 80055328 (DivTriGT3), 800528C0
 * (DivTriFT3), the same code. */
void n_gs_DivTriGT3b(void) { n_gs_DivTriGT3(); }
void n_gs_DivTriFT3b(void) { n_gs_DivTriFT3(); }

/* 80043524 (a, b, c, work): a triangle of three 0xC-byte vertices (xyz
 * +0, uv +6, colour +8) as a POLY_GT3 at the packet pointer (work +
 * 0x14): projected (RTPT; flags into work + 0x24, nothing more when
 * flagged or edge-on (NCLIP 0)), average z (AVSZ3) into work + 0x18, CLUT
 * and page from work + 0x46 / + 0x52, sorted into the ordering table
 * (work + 0x10, shift + 0xC; the entry in work + 0x34). */
void n_gs_TriGT3Packet(void) {
    u32 a = A0, b = A1, c = A2, w = A3;
    gte_write_data(0, MEM_R32(a));
    gte_write_data(1, MEM_R32(a + 4));
    gte_write_data(2, MEM_R32(b));
    gte_write_data(3, MEM_R32(b + 4));
    gte_write_data(4, MEM_R32(c));
    gte_write_data(5, MEM_R32(c + 4));
    gte_command(0x0280030u);
    u32 p = MEM_R32(w + 0x14);
    MEM_W32(p + 4, MEM_R32(a + 8));
    MEM_W8(p + 3, 9);
    MEM_W8(p + 7, 0x34);
    MEM_W32(w + 0x24, gte_read_ctrl(31));
    V0 = MEM_R32(w + 0x24);
    if ((s32)V0 < 0) return;
    gte_command(0x1400006u);
    R[3] = gte_read_data(24);
    if (R[3] == 0) return;
    gte_command(0x158002Du);
    gte_swc2(p + 8, 12);
    gte_swc2(p + 0x14, 13);
    gte_swc2(p + 0x20, 14);
    gte_swc2(w + 0x18, 7);
    MEM_W32(p + 0xC, MEM_R16(a + 6));
    MEM_W32(p + 0x18, MEM_R16(b + 6));
    MEM_W32(p + 0x24, MEM_R16(c + 6));
    MEM_W32(p + 0x10, MEM_R32(b + 8));
    MEM_W32(p + 0x1C, MEM_R32(c + 8));
    MEM_W16(p + 0xE, MEM_R16(w + 0x46));
    MEM_W16(p + 0x1A, MEM_R16(w + 0x52));
    u32 tag = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
    MEM_W32(w + 0x34, tag);
    MEM_W32(p, (MEM_R32(tag) & 0xFFFFFF) | 0x09000000u);
    MEM_W32(MEM_R32(w + 0x34), p & 0xFFFFFF);
    V0 = MEM_R32(w + 0x14) + 0x28;
    MEM_W32(w + 0x14, V0);
}

/* The other driver files' identical copies of the helpers above (each
 * with its own loop addresses). */
void n_gs_BitWidth_800302C4(void) { V0 = bit_width((s32)A0, 0x800302D0u); }
void n_gs_BitWidth_8003092C(void) { V0 = bit_width((s32)A0, 0x80030938u); }
void n_gs_BitWidth_80030FB0(void) { V0 = bit_width((s32)A0, 0x80030FBCu); }
void n_gs_TriOffscreen8_80048A04(void) { tri_offscreen(8, 0x80048B30u, 0x80048B44u); }
void n_gs_TriOffscreen8_8004EAD4(void) { tri_offscreen(8, 0x8004EC00u, 0x8004EC14u); }
void n_gs_TriOffscreen8_80055CE4(void) { tri_offscreen(8, 0x80055E10u, 0x80055E24u); }
void n_gs_TriOffscreen_8004942C(void) { tri_offscreen(0xC, 0x80049558u, 0x8004956Cu); }
void n_gs_TriOffscreen_80050374(void) { tri_offscreen(0xC, 0x800504A0u, 0x800504B4u); }
void n_gs_TriOffscreen_800568D8(void) { tri_offscreen(0xC, 0x80056A04u, 0x80056A18u); }
void n_gs_DivMidpointF_8004ED58(void) { n_gs_DivMidpointF(); }
void n_gs_DivMidpointF_80055E34(void) { n_gs_DivMidpointF(); }
void n_gs_DivMidpointF_800575DC(void) { n_gs_DivMidpointF(); }
void n_gs_DivMidpoint_8004957C(void) { n_gs_DivMidpoint(); }
void n_gs_DivMidpoint_80050610(void) { n_gs_DivMidpoint(); }
void n_gs_DivMidpoint_80056A28(void) { n_gs_DivMidpoint(); }
void n_gs_DivMidpoint_8005841C(void) { n_gs_DivMidpoint(); }
void n_gs_TriGT3Packet_800504C4(void) { n_gs_TriGT3Packet(); }
void n_gs_DivOffscreenF_800464FC(void) { V0 = offscreen(A0, A1, 8, 0x80046698u, 0x800466ACu); }
void n_gs_DivOffscreenF_80049F84(void) { V0 = offscreen(A0, A1, 8, 0x8004A120u, 0x8004A134u); }
void n_gs_DivOffscreenF_80052700(void) { V0 = offscreen(A0, A1, 8, 0x8005289Cu, 0x800528B0u); }
void n_gs_DivOffscreenF_800535F4(void) { V0 = offscreen(A0, A1, 8, 0x80053790u, 0x800537A4u); }
void n_gs_DivOffscreenF_8005741C(void) { V0 = offscreen(A0, A1, 8, 0x800575B8u, 0x800575CCu); }
void n_gs_DivOffscreen_8004ABBC(void) { V0 = offscreen(A0, A1, 0xC, 0x8004AD58u, 0x8004AD6Cu); }
void n_gs_DivOffscreen_80055168(void) { V0 = offscreen(A0, A1, 0xC, 0x80055304u, 0x80055318u); }
void n_gs_DivOffscreen_8005825C(void) { V0 = offscreen(A0, A1, 0xC, 0x800583F8u, 0x8005840Cu); }

/* 800419D0 (a, b, c, work): a flat textured triangle (8-byte vertices:
 * xyz +0, uv +6) as a POLY_FT3, its colour work + 0x3C, page work +
 * 0x4E; otherwise as 80043524. */
void n_gs_TriFT3Packet(void) {
    u32 a = A0, b = A1, c = A2, w = A3;
    gte_write_data(0, MEM_R32(a));
    gte_write_data(1, MEM_R32(a + 4));
    gte_write_data(2, MEM_R32(b));
    gte_write_data(3, MEM_R32(b + 4));
    gte_write_data(4, MEM_R32(c));
    gte_write_data(5, MEM_R32(c + 4));
    gte_command(0x0280030u);
    u32 p = MEM_R32(w + 0x14);
    MEM_W32(p + 4, MEM_R32(w + 0x3C));
    MEM_W8(p + 3, 7);
    MEM_W8(p + 7, 0x24);
    MEM_W32(w + 0x24, gte_read_ctrl(31));
    V0 = MEM_R32(w + 0x24);
    if ((s32)V0 < 0) return;
    gte_command(0x1400006u);
    R[3] = gte_read_data(24);
    if (R[3] == 0) return;
    gte_command(0x158002Du);
    gte_swc2(p + 8, 12);
    gte_swc2(p + 0x10, 13);
    gte_swc2(p + 0x18, 14);
    gte_swc2(w + 0x18, 7);
    MEM_W32(p + 0xC, MEM_R16(a + 6));
    MEM_W32(p + 0x14, MEM_R16(b + 6));
    MEM_W32(p + 0x1C, MEM_R16(c + 6));
    MEM_W16(p + 0xE, MEM_R16(w + 0x46));
    MEM_W16(p + 0x16, MEM_R16(w + 0x4E));
    u32 tag = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
    MEM_W32(w + 0x34, tag);
    MEM_W32(p, (MEM_R32(tag) & 0xFFFFFF) | 0x07000000u);
    MEM_W32(MEM_R32(w + 0x34), p & 0xFFFFFF);
    V0 = MEM_R32(w + 0x14) + 0x20;
    MEM_W32(w + 0x14, V0);
}

/* 8002EB28 (args): a model's vertices projected and normals turned by
 * the light matrix, for its drivers. args +0 the object (+4 vertex
 * count, +8 first, +0xC first output; +0x10 normal count, +0x14 first,
 * +0x18 first output), +0x18 / +0x1C vertices in and out (screen xy,
 * then IR0 << 16 | z, or -1 when the GTE flags it), +0x20 / +0x24
 * normals in and out (8 bytes each). */
void n_gs_ProjectVerts(void) {
    u32 args = A0, obj = MEM_R32(args);
    s32 n = (s32)MEM_R32(obj + 4);
    u32 src = MEM_R32(args + 0x18) + (MEM_R32(obj + 8) << 3);
    u32 dst = MEM_R32(args + 0x1C) + (MEM_R32(obj + 0xC) << 3);
    if (n > 0) {
        for (;;) {
            gte_write_data(0, MEM_R32(src));
            gte_write_data(1, MEM_R32(src + 4));
            gte_command(0x0180001u);
            n--;
            src += 8;
            if (gte_read_ctrl(31) & 0x80000000u) {
                MEM_W32(dst + 4, 0xFFFFFFFFu);
            } else {
                gte_swc2(dst, 14);
                MEM_W32(dst + 4, (gte_read_data(8) << 16) | gte_read_data(19));
            }
            dst += 8;
            if (n == 0) break;
            LOOP_POLL(0x8002EBC0u);
        }
    }
    n = (s32)MEM_R32(obj + 0x10);
    src = MEM_R32(args + 0x20) + (MEM_R32(obj + 0x14) << 3);
    V0 = MEM_R32(obj + 0x18) << 3;
    dst = MEM_R32(args + 0x24) + V0;
    if (n > 0) {
        for (;;) {
            gte_write_data(0, MEM_R32(src));
            gte_write_data(1, MEM_R32(src + 4));
            gte_command(0x0C8041Eu);
            n--;
            src += 8;
            u32 i1 = gte_read_data(9), i2 = gte_read_data(10), i3 = gte_read_data(11);
            MEM_W32(dst, (i2 << 16) | i1);
            MEM_W16(dst + 4, (u16)i3);
            dst += 8;
            if (n == 0) break;
            LOOP_POLL(0x8002EC38u);
        }
    }
}

/* 8004EC24: another driver file's copy of 800419D0. */
void n_gs_TriFT3Packet_8004EC24(void) { n_gs_TriFT3Packet(); }

/* 8004BFF0 (args): a fogged, lit, textured quad TMD driver: for each of
 * the block's quads (0x1C-byte records: +0 uv0/clut, +4 uv1/tpage, +8
 * uv2, +0xC uv3, normals at -2, +4, +8, +0xC, vertices at +2, +6, +0xA,
 * +0xE; record pointer args + 0x14 by the block's index), project the four
 * corners, cull by NCLIP and the GTE flags, light (NCDT then NCDS on
 * 0x3C808080) and depth-cue each corner's colour by its IR0 (DPCS), and
 * sort a POLY_GT4 (0x34 bytes at args + 0x10) into the OT (args + 4,
 * shift + 8, offset + 0xC). args +0 the block pointer, moved past its
 * header; the packet pointer kept in 0x80065C08. Returns the new block
 * pointer + 4. */
void n_gs_FogGT4Lit(void) {
    FRAME(0x28);
    MEM_W32(sp + 0x20, R[30]);
    for (int i = 0; i < 8; i++) MEM_W32(sp + (u32)i * 4, R[16 + i]);
    const u32 a = A0, rgb = 0x3C808080u;
    u32 blk = MEM_R32(a);
    u32 pk = MEM_R32(a + 0x10), verts = MEM_R32(a + 0x18), norms = MEM_R32(a + 0x1C);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(a, blk + 4);
    u32 rec = MEM_R32(a + 0x14) + ((MEM_R32(blk + 4) & 0xFFFFFF) << 2);
#define VTX(o) (verts + ((u32)MEM_R16(q + (o)) << 3))
#define NRM(o) (norms + ((u32)MEM_R16(q + (o)) << 3))
    if (n != 0) {
        u32 q = rec + 0xC;
        for (u32 i = 0;;) {
            gte_write_data(0, MEM_R32(VTX(2)));
            gte_write_data(1, MEM_R32(VTX(2) + 4));
            gte_command(0x0180001u);
            gte_write_data(0, MEM_R32(VTX(6)));
            gte_write_data(1, MEM_R32(VTX(6) + 4));
            u32 flags = gte_read_ctrl(31);
            u32 ir0a = gte_read_data(8);
            gte_command(0x0180001u);
            gte_write_data(0, MEM_R32(VTX(0xA)));
            gte_write_data(1, MEM_R32(VTX(0xA) + 4));
            u32 f2 = gte_read_ctrl(31);
            u32 ir0b = gte_read_data(8);
            gte_command(0x0180001u);
            gte_write_data(0, MEM_R32(VTX(0xE)));
            gte_write_data(1, MEM_R32(VTX(0xE) + 4));
            flags |= f2;
            f2 = gte_read_ctrl(31);
            u32 ir0c = gte_read_data(8);
            gte_command(0x1400006u);
            if ((s32)gte_read_data(24) > 0) {
                gte_swc2(pk + 8, 12);
                gte_command(0x0180001u);
                gte_write_data(2, MEM_R32(NRM(4)));
                gte_write_data(3, MEM_R32(NRM(4) + 4));
                flags |= f2;
                flags |= gte_read_ctrl(31);
                if ((s32)flags >= 0) {
                    u32 ir0d = gte_read_data(8);
                    gte_command(0x168002Eu);
                    gte_swc2(pk + 0x14, 12);
                    gte_swc2(pk + 0x20, 13);
                    gte_swc2(pk + 0x2C, 14);
                    u32 otz = gte_read_data(7);
                    gte_write_data(0, MEM_R32(NRM(0xC)));
                    gte_write_data(1, MEM_R32(NRM(0xC) + 4));
                    gte_write_data(4, MEM_R32(NRM(8)));
                    gte_write_data(5, MEM_R32(NRM(8) + 4));
                    gte_write_data(6, rgb);
                    gte_command(0x108041Bu);
                    MEM_W32(pk + 0xC, MEM_R32(rec));
                    MEM_W32(pk + 0x18, MEM_R32(q - 8));
                    MEM_W32(pk + 0x24, MEM_R32(q - 4));
                    u32 c2 = gte_read_data(22);
                    gte_write_data(8, ir0d);
                    gte_write_data(6, c2);
                    gte_command(0x0780010u);
                    gte_swc2(pk + 0x28, 22);
                    gte_write_data(0, MEM_R32(NRM((u32)-2)));
                    gte_write_data(1, MEM_R32(NRM((u32)-2) + 4));
                    gte_write_data(6, rgb);
                    gte_command(0x118043Fu);
                    u32 z = (u32)((s32)(otz - MEM_R32(a + 0xC)) >> (MEM_R32(a + 8) & 31));
                    u32 tag = MEM_R32(MEM_R32(a + 4) + 4) + (z << 2);
                    MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFF) | 0x0C000000u);
                    MEM_W32(tag, pk & 0xFFFFFF);
                    u32 c0 = gte_read_data(20), c1 = gte_read_data(21), cc = gte_read_data(22);
                    gte_write_data(8, ir0a);
                    gte_write_data(6, c0);
                    gte_command(0x0780010u);
                    gte_swc2(pk + 4, 22);
                    gte_write_data(8, ir0b);
                    gte_write_data(6, c1);
                    gte_command(0x0780010u);
                    gte_swc2(pk + 0x10, 22);
                    gte_write_data(6, cc);
                    gte_write_data(8, ir0c);
                    gte_command(0x0780010u);
                    MEM_W32(pk + 0x30, MEM_R32(q));
                    gte_swc2(pk + 0x1C, 22);
                    pk += 0x34;
                }
            }
            i++;
            q += 0x1C;
            rec += 0x1C;
            if (!((s32)i < (s32)n)) break;
            LOOP_POLL(0x8004C300u);
        }
    }
#undef VTX
#undef NRM
    GV(u32, 0x80065C08u) = pk;
    V0 = MEM_R32(a) + 4;
}

/* 8004B500 (args): the fogged, lit, textured triangle driver (0x18-byte
 * records: +0 uv0/clut, +4 uv1/tpage, +8 uv2, normals +0xC, +0x10,
 * +0x14, vertices +0xE, +0x12, +0x16), as 8004BFF0 for POLY_GT3 (0x28
 * bytes): RTPT by hand (three RTPS, the next record's first vertex
 * loaded ahead), cull, light (NCDT on 0x34808080) with the normals of
 * the block's first record (as the library does), depth-cue, sort by
 * AVSZ3. The pieces kept on the stack as the original keeps them. */
void n_gs_FogGT3Lit(void) {
    FRAME(0x38);
    MEM_W32(sp + 0x20, R[20]);
    MEM_W32(sp + 0x30, R[30]);
    MEM_W32(sp + 0x2C, R[23]);
    MEM_W32(sp + 0x28, R[22]);
    MEM_W32(sp + 0x24, R[21]);
    for (int i = 0; i < 4; i++) MEM_W32(sp + 0x10 + (u32)i * 4, R[16 + i]);
    const u32 a = A0;
    MEM_W32(sp, 0x34808080u);
    u32 blk = MEM_R32(a);
    u32 pk = MEM_R32(a + 0x10), verts = MEM_R32(a + 0x18);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(a, blk + 4);
    u32 rec = MEM_R32(a + 0x14) + ((MEM_R32(blk + 4) & 0xFFFFFF) << 2);
    u32 norms = MEM_R32(a + 0x1C);
    u32 v0 = verts + ((u32)MEM_R16(rec + 0xE) << 3);
    MEM_W32(sp + 4, MEM_R32(MEM_R32(a + 4) + 4));
    u32 zoff = MEM_R32(a + 0xC), zsh = MEM_R32(a + 8);
    u32 n0 = norms + ((u32)MEM_R16(rec + 0xC) << 3);
    u32 n1 = norms + ((u32)MEM_R16(rec + 0x10) << 3);
    u32 n2 = norms + ((u32)MEM_R16(rec + 0x14) << 3);
    gte_write_data(0, MEM_R32(v0));
    gte_write_data(1, MEM_R32(v0 + 4));
    if (n != 0) {
        MEM_W32(sp + 8, n2);
        u32 q = rec + 8;
#define VTX(o) (verts + ((u32)MEM_R16(q + (o)) << 3))
        for (u32 i = 0;;) {
            gte_command(0x0180001u);
            gte_write_data(0, MEM_R32(VTX(0xA)));
            gte_write_data(1, MEM_R32(VTX(0xA) + 4));
            u32 ir0a = gte_read_data(8);
            u32 flags = gte_read_ctrl(31);
            gte_command(0x0180001u);
            gte_write_data(0, MEM_R32(VTX(0xE)));
            gte_write_data(1, MEM_R32(VTX(0xE) + 4));
            u32 ir0b = gte_read_data(8);
            flags |= gte_read_ctrl(31);
            gte_command(0x0180001u);
            gte_write_data(0, MEM_R32(VTX(0x1E)));
            gte_write_data(1, MEM_R32(VTX(0x1E) + 4));
            u32 ir0c = gte_read_data(8);
            flags |= gte_read_ctrl(31);
            if ((s32)flags >= 0) {
                gte_command(0x1400006u);
                if ((s32)gte_read_data(24) > 0) {
                    gte_swc2(pk + 8, 12);
                    gte_swc2(pk + 0x14, 13);
                    gte_swc2(pk + 0x20, 14);
                    gte_write_data(6, MEM_R32(sp));
                    gte_write_data(0, MEM_R32(n0));
                    gte_write_data(1, MEM_R32(n0 + 4));
                    gte_write_data(2, MEM_R32(n1));
                    gte_write_data(3, MEM_R32(n1 + 4));
                    u32 nn = MEM_R32(sp + 8);
                    gte_write_data(4, MEM_R32(nn));
                    gte_write_data(5, MEM_R32(nn + 4));
                    gte_command(0x118043Fu);
                    gte_write_data(0, MEM_R32(VTX(0x1E)));
                    gte_write_data(1, MEM_R32(VTX(0x1E) + 4));
                    u32 c0 = gte_read_data(20), c1 = gte_read_data(21), c2 = gte_read_data(22);
                    gte_write_data(8, ir0a);
                    gte_write_data(6, c0);
                    gte_command(0x0780010u);
                    MEM_W32(pk + 0xC, MEM_R32(rec));
                    gte_swc2(pk + 4, 22);
                    gte_write_data(8, ir0b);
                    gte_write_data(6, c1);
                    gte_command(0x0780010u);
                    MEM_W32(pk + 0x18, MEM_R32(q - 4));
                    gte_swc2(pk + 0x10, 22);
                    gte_write_data(8, ir0c);
                    gte_write_data(6, c2);
                    gte_command(0x0780010u);
                    MEM_W32(pk + 0x24, MEM_R32(q));
                    gte_swc2(pk + 0x1C, 22);
                    gte_command(0x158002Du);
                    u32 z = (u32)((s32)(gte_read_data(7) - zoff) >> (zsh & 31));
                    u32 tag = MEM_R32(sp + 4) + (z << 2);
                    MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFF) | 0x09000000u);
                    MEM_W32(tag, pk & 0xFFFFFF);
                    pk += 0x28;
                }
            }
            i++;
            q += 0x18;
            rec += 0x18;
            if (!((s32)i < (s32)n)) break;
            LOOP_POLL(0x8004B7CCu);
        }
#undef VTX
    }
    GV(u32, 0x80065C08u) = pk;
    V0 = MEM_R32(a) + 4;
}


/* 8002E648 (args): the flat triangle TMD driver: for each of the
 * block's triangles (0xC-byte records: +0 colour and code, +4 / +6 / +8
 * vertex indices), RTPT, cull by the flags and NCLIP, sort by AVSZ3 into
 * the OT (args + 4; offset + 0xC, shift + 8) as a POLY_F3 (0x14 bytes at
 * args + 0x10; the colour stored even when culled). The packet pointer
 * kept in 0x80065C08; returns the block pointer + 8. */
void n_gs_DriverF3(void) {
    const u32 a = A0;
    u32 blk = MEM_R32(a), ot = MEM_R32(a + 4);
    u32 n = MEM_R32(blk) >> 16;
    u32 rec = MEM_R32(a + 0x14) + (MEM_R32(blk + 4) << 2);
    u32 sh = MEM_R32(a + 8);
    u32 pk = MEM_R32(a + 0x10);
    if (n != 0) {
        u32 base = MEM_R32(ot + 4), zoff = MEM_R32(a + 0xC);
        u32 verts = MEM_R32(a + 0x18);
        for (;;) {
            u32 w = MEM_R32(rec + 4);
            u32 v0 = ((w << 16) >> 13) + verts, v1 = ((w >> 16) << 3) + verts;
            u32 v2 = ((u32)MEM_R16(rec + 8) << 3) + verts;
            gte_write_data(0, MEM_R32(v0));
            gte_write_data(1, MEM_R32(v0 + 4));
            gte_write_data(2, MEM_R32(v1));
            gte_write_data(3, MEM_R32(v1 + 4));
            gte_write_data(4, MEM_R32(v2));
            gte_write_data(5, MEM_R32(v2 + 4));
            gte_command(0x0280030u);
            MEM_W32(pk + 4, MEM_R32(rec));
            rec += 0xC;
            if ((s32)gte_read_ctrl(31) >= 0) {
                gte_command(0x1400006u);
                if ((s32)gte_read_data(24) > 0) {
                    gte_command(0x158002Du);
                    u32 tag = ((((gte_read_data(7) - zoff) >> (sh & 31)) & 0xFFFF) << 2) + base;
                    u32 old = MEM_R32(tag);
                    MEM_W32(pk, 0x04000000u | ((old << 8) >> 8));
                    MEM_W32(tag, (((pk ^ old) << 8) >> 8) ^ old);
                    gte_swc2(pk + 8, 12);
                    gte_swc2(pk + 0xC, 13);
                    gte_swc2(pk + 0x10, 14);
                    pk += 0x14;
                }
            }
            n--;
            if (n == 0) break;
            LOOP_POLL(0x8002E790u);
        }
    }
    GV(u32, 0x80065C08u) = pk;
    V0 = MEM_R32(a) + 8;
}

static void copy8(u32 d, u32 s) { for (u32 i = 0; i < 8; i++) MEM_W8(d + i, MEM_R8(s + i)); }

/* 800414B4 (tri, work, depth): the flat-textured version of 80042FB0
 * (8-byte vertices, children at tri + 0x18, POLY_FT3 packets of 0x20
 * bytes from the template alone; the parts 80041B04 / 800419D0). */
void n_gs_DivTriFT3R(void) {
    FRAME(0x48);
    u32 sv[8];
    for (int i = 0; i < 8; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x20 + (u32)i * 4, sv[i]); }
    u32 sfp = R[30];
    MEM_W32(sp + 0x40, sfp);
    MEM_W32(sp + 0x44, R[31]);
    R[21] = A1; R[22] = A2; R[20] = A0;
    R[30] = R[20] + 0x18; R[23] = R[30];
    const u32 t = R[20], w = R[21];
    gte_write_data(0, MEM_R32(t));
    gte_write_data(1, MEM_R32(t + 4));
    gte_write_data(2, MEM_R32(t + 8));
    gte_write_data(3, MEM_R32(t + 0xC));
    gte_write_data(4, MEM_R32(t + 0x10));
    gte_write_data(5, MEM_R32(t + 0x14));
    gte_command(0x0280030u);
    R[16] = w + 0x38;
    const u32 tp = R[16];
    MEM_W16(tp + 0xC, MEM_R16(t + 6));
    MEM_W16(tp + 0x14, MEM_R16(t + 0xE));
    MEM_W16(tp + 0x1C, MEM_R16(t + 0x16));
    MEM_W32(w + 0x24, gte_read_ctrl(31));
    gte_swc2(tp + 8, 12);
    gte_swc2(tp + 0x10, 13);
    gte_swc2(tp + 0x18, 14);
    gte_command(0x158002Du);
    MEM_W32(w + 0x18, (u32)((s32)gte_read_data(19) >> 2));
    if (gcall(0x80041880u, tp, w, R[6], R[7]) != 0) goto out;
    int emit = 0;
    if (MEM_R32(w) == R[22]) {
        V0 = MEM_R32(w + 0x24) & 0x7F85E000u;
        if (V0 != 0) goto out;
        emit = 1;
    } else if ((s32)MEM_R32(w + 0x24) < 0) {
        R[22]++;
    } else {
        s32 dx = (s16)MEM_R16(w + 0x2C) - (s16)MEM_R16(w + 0x28);
        s32 dy = (s16)MEM_R16(w + 0x2E) - (s16)MEM_R16(w + 0x2A);
        if (dx < (s16)MEM_R16(w + 0x20) && dy < (s16)MEM_R16(w + 0x22)) emit = 1;
        else if (!(dx < 1023)) R[22]++;
        else if (!(dy < 511)) R[22]++;
        else R[22] = MEM_R32(w);
    }
    if (emit) {
        for (u32 i = 0; i < 0x20; i += 4) MEM_W32(MEM_R32(w + 0x14) + i, MEM_R32(tp + i));
        u32 tag = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
        MEM_W32(w + 0x34, tag);
        MEM_W32(MEM_R32(w + 0x14), (MEM_R32(tag) & 0xFFFFFF) | 0x07000000u);
        MEM_W32(MEM_R32(w + 0x34), MEM_R32(w + 0x14) & 0xFFFFFF);
        V0 = MEM_R32(w + 0x14) + 0x20;
        MEM_W32(w + 0x14, V0);
        goto out;
    }
    R[19] = R[23] + 8;
    R[16] = R[20] + 8;
    copy8(R[23], R[20]);
    gcall(0x80041B04u, R[19], R[20], R[16], R[7]);
    R[18] = R[23] + 0x10;
    R[17] = R[20] + 0x10;
    gcall(0x80041B04u, R[18], R[20], R[17], R[7]);
    gcall(0x800414B4u, R[30], R[21], R[22], R[7]);
    gcall(0x800419D0u, R[20], R[16], R[19], R[21]);
    gcall(0x80041B04u, R[23], R[16], R[17], R[7]);
    gcall(0x800414B4u, R[30], R[21], R[22], R[7]);
    copy8(R[23] + 0x10, R[20] + 8);
    gcall(0x800414B4u, R[30], R[21], R[22], R[7]);
    gcall(0x800419D0u, R[16], R[17], R[23], R[21]);
    copy8(R[23] + 8, R[20] + 0x10);
    gcall(0x80041B04u, R[18], R[20], R[17], R[7]);
    gcall(0x800414B4u, R[30], R[21], R[22], R[7]);
    gcall(0x800419D0u, R[17], R[20], R[18], R[21]);
out:
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

static void copy12(u32 d, u32 s) { for (u32 i = 0; i < 12; i++) MEM_W8(d + i, MEM_R8(s + i)); }

/* 80042FB0 (tri, work, depth): the recursive subdivision of a textured
 * triangle (three 0xC-byte vertices at tri; the children built at tri +
 * 0x24): project it into the template at work + 0x38 (flags work + 0x24,
 * SZ3 / 4 in work + 0x18); off screen (800433D4): nothing. At the
 * deepest level (work + 0) emit it as a POLY_GT3 when the GTE flags
 * allow; otherwise emit it when it is small enough (under work + 0x20 /
 * 0x22), else split it four ways (midpoints 80043670, each part emitted
 * by 80043524 and recursed into one level deeper, or straight to the
 * deepest when it fits in 1023 x 511). The original's s-registers. */
void n_gs_DivTriGT3R(void) {
    FRAME(0x48);
    u32 sv[8];
    for (int i = 0; i < 8; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x20 + (u32)i * 4, sv[i]); }
    u32 sfp = R[30];
    MEM_W32(sp + 0x40, sfp);
    MEM_W32(sp + 0x44, R[31]);
    R[21] = A0; R[20] = A1; R[23] = A2;
    R[22] = R[21] + 0x24; R[30] = R[22];
    const u32 t = R[21], w = R[20];
    gte_write_data(0, MEM_R32(t));
    gte_write_data(1, MEM_R32(t + 4));
    gte_write_data(2, MEM_R32(t + 0xC));
    gte_write_data(3, MEM_R32(t + 0x10));
    gte_write_data(4, MEM_R32(t + 0x18));
    gte_write_data(5, MEM_R32(t + 0x1C));
    gte_command(0x0280030u);
    R[16] = w + 0x38;
    const u32 tp = R[16];
    MEM_W16(tp + 0xC, MEM_R16(t + 6));
    MEM_W16(tp + 0x18, MEM_R16(t + 0x12));
    MEM_W16(tp + 0x24, MEM_R16(t + 0x1E));
    MEM_W32(w + 0x24, gte_read_ctrl(31));
    gte_swc2(tp + 8, 12);
    gte_swc2(tp + 0x14, 13);
    gte_swc2(tp + 0x20, 14);
    gte_command(0x158002Du);
    MEM_W32(w + 0x18, (u32)((s32)gte_read_data(19) >> 2));
    if (gcall(0x800433D4u, tp, w, R[6], R[7]) != 0) goto out;
    int emit = 0;
    if (MEM_R32(w) == R[23]) {
        V0 = MEM_R32(w + 0x24) & 0x7F85E000u;
        if (V0 != 0) goto out;
        emit = 1;
    } else if ((s32)MEM_R32(w + 0x24) < 0) {
        R[23]++;
    } else {
        s32 dx = (s16)MEM_R16(w + 0x2C) - (s16)MEM_R16(w + 0x28);
        s32 dy = (s16)MEM_R16(w + 0x2E) - (s16)MEM_R16(w + 0x2A);
        if (dx < (s16)MEM_R16(w + 0x20) && dy < (s16)MEM_R16(w + 0x22)) emit = 1;
        else if (!(dx < 1023)) R[23]++;
        else if (!(dy < 511)) R[23]++;
        else R[23] = MEM_R32(w);
    }
    if (emit) {
        u32 p = MEM_R32(w + 0x14);
        MEM_W32(p, MEM_R32(tp));
        MEM_W32(MEM_R32(w + 0x14) + 4, MEM_R32(t + 8));
        MEM_W32(MEM_R32(w + 0x14) + 8, MEM_R32(tp + 8));
        MEM_W32(MEM_R32(w + 0x14) + 0xC, MEM_R32(tp + 0xC));
        MEM_W32(MEM_R32(w + 0x14) + 0x10, MEM_R32(t + 0x14));
        MEM_W32(MEM_R32(w + 0x14) + 0x14, MEM_R32(tp + 0x14));
        MEM_W32(MEM_R32(w + 0x14) + 0x18, MEM_R32(tp + 0x18));
        MEM_W32(MEM_R32(w + 0x14) + 0x1C, MEM_R32(t + 0x20));
        MEM_W32(MEM_R32(w + 0x14) + 0x20, MEM_R32(tp + 0x20));
        MEM_W32(MEM_R32(w + 0x14) + 0x24, MEM_R32(tp + 0x24));
        u32 tag = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
        MEM_W32(w + 0x34, tag);
        MEM_W32(MEM_R32(w + 0x14), (MEM_R32(tag) & 0xFFFFFF) | 0x09000000u);
        MEM_W32(MEM_R32(w + 0x34), MEM_R32(w + 0x14) & 0xFFFFFF);
        V0 = MEM_R32(w + 0x14) + 0x28;
        MEM_W32(w + 0x14, V0);
        goto out;
    }
    /* split */
    R[18] = R[30] + 0xC;
    R[16] = R[21] + 0xC;
    copy12(R[30], R[21]);
    gcall(0x80043670u, R[18], R[21], R[16], R[7]);
    R[19] = R[30] + 0x18;
    R[17] = R[21] + 0x18;
    gcall(0x80043670u, R[19], R[21], R[17], R[7]);
    gcall(0x80043524u, R[21], R[16], R[18], R[20]);
    gcall(0x80042FB0u, R[22], R[20], R[23], R[7]);
    gcall(0x80043670u, R[30], R[16], R[17], R[7]);
    gcall(0x80042FB0u, R[22], R[20], R[23], R[7]);
    copy12(R[30] + 0x18, R[21] + 0xC);
    gcall(0x80043524u, R[16], R[17], R[30], R[20]);
    gcall(0x80042FB0u, R[22], R[20], R[23], R[7]);
    copy12(R[30] + 0xC, R[21] + 0x18);
    gcall(0x80043670u, R[19], R[21], R[17], R[7]);
    gcall(0x80043524u, R[17], R[21], R[19], R[20]);
    gcall(0x80042FB0u, R[22], R[20], R[23], R[7]);
out:
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8003E334 (args): lit, gouraud-shaded triangles (POLY_G3, 7 words), a
 * normal per corner: each 0x10-byte polygon a colour word and three
 * (vertex << 16 | normal) words. Back faces (NCLIP) and ones the GTE
 * flags are skipped; the rest lit (NCDT) and sorted by AVSZ3. */
void n_gs_DriverG3Lit(void) {
    FRAME(0x18);
    for (int i = 0; i < 5; i++) MEM_W32(sp + (u32)i * 4, R[16 + i]);
    const u32 a = A0;
    u32 blk = MEM_R32(a);
    u32 pk = MEM_R32(a + 0x10), verts = MEM_R32(a + 0x18), norms = MEM_R32(a + 0x1C);
    u32 zoff = MEM_R32(a + 0xC), sh = MEM_R32(a + 8);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(a, blk + 4);
    u32 rec = MEM_R32(a + 0x14) + ((MEM_R32(blk + 4) & 0xFFFFFF) << 2);
    u32 ot = MEM_R32(MEM_R32(a + 4) + 4);
    if (n != 0) {
        for (u32 i = 0;;) {
            u32 w0 = MEM_R32(rec + 4), w1 = MEM_R32(rec + 8), w2 = MEM_R32(rec + 0xC);
            u32 v0 = verts + ((u32)((s32)w0 >> 16) << 3), v1 = verts + ((u32)((s32)w1 >> 16) << 3);
            u32 v2 = verts + ((u32)((s32)w2 >> 16) << 3);
            gte_write_data(0, MEM_R32(v0));
            gte_write_data(1, MEM_R32(v0 + 4));
            gte_write_data(2, MEM_R32(v1));
            gte_write_data(3, MEM_R32(v1 + 4));
            gte_write_data(4, MEM_R32(v2));
            gte_write_data(5, MEM_R32(v2 + 4));
            gte_command(0x0280030u);
            u32 flags = gte_read_ctrl(31);
            gte_command(0x1400006u);
            if ((s32)gte_read_data(24) > 0 && (s32)flags >= 0) {
                gte_command(0x158002Du);
                gte_write_data(6, MEM_R32(rec));
                u32 n0 = norms + ((w0 & 0xFFFF) << 3), n1 = norms + ((w1 & 0xFFFF) << 3), n2 = norms + ((w2 & 0xFFFF) << 3);
                gte_write_data(0, MEM_R32(n0));
                gte_write_data(1, MEM_R32(n0 + 4));
                gte_write_data(2, MEM_R32(n1));
                gte_write_data(3, MEM_R32(n1 + 4));
                gte_write_data(4, MEM_R32(n2));
                gte_write_data(5, MEM_R32(n2 + 4));
                u32 otz = gte_read_data(7);
                gte_command(0x118043Fu);
                u32 tag = ot + ((u32)((s32)(otz - zoff) >> (sh & 31)) << 2);
                MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFF) | 0x06000000u);
                MEM_W32(tag, pk & 0xFFFFFF);
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x10, 13);
                gte_swc2(pk + 0x18, 14);
                gte_swc2(pk + 4, 20);
                gte_swc2(pk + 0xC, 21);
                gte_swc2(pk + 0x14, 22);
                pk += 0x1C;
            }
            i++;
            rec += 0x10;
            if (!((s32)i < (s32)n)) break;
            LOOP_POLL(0x8003E4FCu);
        }
    }
    g_packetArea = pk;
    V0 = MEM_R32(a) + 4;
}

/* 8003E764 (args): lit flat quads (POLY_F4, 6 words), one normal: each
 * 0x10-byte polygon a colour word, (v0 << 16 | normal), (v2 << 16 |
 * v1), v3. Culled by NCLIP and the first three corners' flags; lit
 * (NCCS) and sorted by AVSZ4. */
void n_gs_DriverF4Lit(void) {
    FRAME(0x10);
    for (int i = 0; i < 4; i++) MEM_W32(sp + (u32)i * 4, R[16 + i]);
    const u32 a = A0;
    u32 blk = MEM_R32(a);
    u32 pk = MEM_R32(a + 0x10), verts = MEM_R32(a + 0x18), norms = MEM_R32(a + 0x1C);
    u32 zoff = MEM_R32(a + 0xC), sh = MEM_R32(a + 8);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(a, blk + 4);
    u32 rec = MEM_R32(a + 0x14) + ((MEM_R32(blk + 4) & 0xFFFFFF) << 2);
    u32 ot = MEM_R32(MEM_R32(a + 4) + 4);
#define VX(i) (verts + ((u32)(i) << 3))
    if (n != 0) {
        for (u32 i = 0;;) {
            u32 w0 = MEM_R32(rec + 4), w1 = MEM_R32(rec + 8);
            u32 a0 = VX((u32)((s32)w0 >> 16)), a1 = VX(w1 & 0xFFFF), a2 = VX((u32)((s32)w1 >> 16));
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            u32 nrm = w0 & 0xFFFF;
            u32 flags = gte_read_ctrl(31);
            gte_command(0x1400006u);
            if ((s32)gte_read_data(24) > 0 && (s32)flags >= 0) {
                u32 a3 = VX(MEM_R16(rec + 0xC));
                gte_write_data(0, MEM_R32(a3));
                gte_write_data(1, MEM_R32(a3 + 4));
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0xC, 13);
                gte_swc2(pk + 0x10, 14);
                gte_command(0x0180001u);
                u32 col = MEM_R32(rec);
                (void)gte_read_ctrl(31);
                gte_write_data(6, col);
                gte_command(0x168002Eu);
                u32 nn = norms + (nrm << 3);
                gte_write_data(0, MEM_R32(nn));
                gte_write_data(1, MEM_R32(nn + 4));
                u32 otz = gte_read_data(7);
                gte_command(0x108041Bu);
                u32 tag = ot + ((u32)((s32)(otz - zoff) >> (sh & 31)) << 2);
                MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFF) | 0x05000000u);
                gte_swc2(pk + 0x14, 14);
                gte_swc2(pk + 4, 22);
                MEM_W32(tag, pk & 0xFFFFFF);
                pk += 0x18;
            }
            i++;
            rec += 0x10;
            if (!((s32)i < (s32)n)) break;
            LOOP_POLL(0x8003E920u);
        }
    }
#undef VX
    g_packetArea = pk;
    V0 = MEM_R32(a) + 4;
}

/* 8003E950 (args): lit flat textured quads (POLY_FT4, 10 words), one
 * normal: each 0x18-byte polygon uv0/clut, uv1/tpage, uv2, uv3 (normal
 * in its top half), (v1 << 16 | v0), (v3 << 16 | v2). Culled by the
 * first three corners' flags and NCLIP; lit (NCCS on 0x2C808080, loaded
 * once) and sorted by AVSZ4. */
void n_gs_DriverFT4Lit(void) {
    FRAME(0x18);
    for (int i = 0; i < 5; i++) MEM_W32(sp + (u32)i * 4, R[16 + i]);
    const u32 a = A0;
    u32 blk = MEM_R32(a);
    u32 pk = MEM_R32(a + 0x10), verts = MEM_R32(a + 0x18), norms = MEM_R32(a + 0x1C);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(a, blk + 4);
    u32 rec = MEM_R32(a + 0x14) + ((MEM_R32(blk + 4) & 0xFFFFFF) << 2);
    gte_write_data(6, 0x2C808080u);
    u32 ot = MEM_R32(MEM_R32(a + 4) + 4);
    u32 zoff = MEM_R32(a + 0xC), sh = MEM_R32(a + 8);
#define VX(i) (verts + ((u32)(i) << 3))
    if (n != 0) {
        for (u32 i = 0;;) {
            u32 w0 = MEM_R32(rec + 0x10), w1 = MEM_R32(rec + 0x14);
            u32 a0 = VX(w0 & 0xFFFF), a1 = VX((u32)((s32)w0 >> 16)), a2 = VX(w1 & 0xFFFF);
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            u32 a3 = VX((u32)((s32)w1 >> 16));
            if ((s32)gte_read_ctrl(31) >= 0) {
                gte_command(0x1400006u);
                if ((s32)gte_read_data(24) > 0) {
                    gte_write_data(0, MEM_R32(a3));
                    gte_write_data(1, MEM_R32(a3 + 4));
                    gte_swc2(pk + 8, 12);
                    gte_swc2(pk + 0x10, 13);
                    gte_swc2(pk + 0x18, 14);
                    gte_command(0x0180001u);
                    MEM_W32(pk + 0x24, MEM_R32(rec + 0xC));
                    u32 nn = norms + ((u32)MEM_R16(rec + 0xE) << 3);
                    gte_write_data(0, MEM_R32(nn));
                    gte_write_data(1, MEM_R32(nn + 4));
                    (void)gte_read_ctrl(31);
                    gte_command(0x168002Eu);
                    MEM_W32(pk + 0xC, MEM_R32(rec));
                    MEM_W32(pk + 0x14, MEM_R32(rec + 4));
                    MEM_W32(pk + 0x1C, MEM_R32(rec + 8));
                    u32 otz = gte_read_data(7);
                    gte_command(0x108041Bu);
                    u32 tag = ot + ((u32)((s32)(otz - zoff) >> (sh & 31)) << 2);
                    MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFF) | 0x09000000u);
                    gte_swc2(pk + 0x20, 14);
                    gte_swc2(pk + 4, 22);
                    MEM_W32(tag, pk & 0xFFFFFF);
                    pk += 0x28;
                }
            }
            i++;
            rec += 0x18;
            if (!((s32)i < (s32)n)) break;
            LOOP_POLL(0x8003EB44u);
        }
    }
#undef VX
    g_packetArea = pk;
    V0 = MEM_R32(a) + 4;
}

/* 8003EB78 (args): lit gouraud quads (POLY_G4, 9 words), a normal per
 * corner: each 0x14-byte polygon a colour word and four (vertex << 16 |
 * normal) words. Culled by NCLIP and the first three corners' flags;
 * the fourth corner lit alone (NCCS), the others together (NCDT);
 * sorted by AVSZ4. */
void n_gs_DriverG4Lit(void) {
    FRAME(0x20);
    for (int i = 0; i < 8; i++) MEM_W32(sp + (u32)i * 4, R[16 + i]);
    const u32 a = A0;
    u32 blk = MEM_R32(a);
    u32 pk = MEM_R32(a + 0x10), verts = MEM_R32(a + 0x18), norms = MEM_R32(a + 0x1C);
    u32 zoff = MEM_R32(a + 0xC), sh = MEM_R32(a + 8);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(a, blk + 4);
    u32 rec = MEM_R32(a + 0x14) + ((MEM_R32(blk + 4) & 0xFFFFFF) << 2);
    u32 ot = MEM_R32(MEM_R32(a + 4) + 4);
#define VX(i) (verts + ((u32)(i) << 3))
#define NX(i) (norms + ((u32)(i) << 3))
#define VEC(r, p) do { gte_write_data((r), MEM_R32(p)); gte_write_data((r) + 1, MEM_R32((p) + 4)); } while (0)
    if (n != 0) {
        for (u32 i = 0;;) {
            u32 w0 = MEM_R32(rec + 4), w1 = MEM_R32(rec + 8), w2 = MEM_R32(rec + 0xC);
            VEC(0, VX((u32)((s32)w0 >> 16)));
            VEC(2, VX((u32)((s32)w1 >> 16)));
            VEC(4, VX((u32)((s32)w2 >> 16)));
            gte_command(0x0280030u);
            u32 w3 = MEM_R32(rec + 0x10);
            u32 flags = gte_read_ctrl(31);
            gte_command(0x1400006u);
            if ((s32)gte_read_data(24) > 0 && (s32)flags >= 0) {
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x10, 13);
                gte_swc2(pk + 0x18, 14);
                VEC(0, VX((u32)((s32)w3 >> 16)));
                gte_command(0x0180001u);
                VEC(0, NX(w3 & 0xFFFF));
                VEC(4, NX(w2 & 0xFFFF));
                (void)gte_read_ctrl(31);
                gte_command(0x168002Eu);
                gte_write_data(6, MEM_R32(rec));
                VEC(2, NX(w1 & 0xFFFF));
                u32 otz = gte_read_data(7);
                gte_command(0x108041Bu);
                VEC(0, NX(w0 & 0xFFFF));
                u32 tag = ot + ((u32)((s32)(otz - zoff) >> (sh & 31)) << 2);
                gte_swc2(pk + 0x1C, 22);
                gte_command(0x118043Fu);
                gte_swc2(pk + 0x20, 14);
                MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFF) | 0x08000000u);
                MEM_W32(tag, pk & 0xFFFFFF);
                gte_swc2(pk + 4, 20);
                gte_swc2(pk + 0xC, 21);
                gte_swc2(pk + 0x14, 22);
                pk += 0x24;
            }
            i++;
            rec += 0x14;
            if (!((s32)i < (s32)n)) break;
            LOOP_POLL(0x8003EDA8u);
        }
    }
#undef VX
#undef NX
#undef VEC
    g_packetArea = pk;
    V0 = MEM_R32(a) + 4;
}

/* 80049664 (unit): libgs's subdividing flat-textured quad driver (the
 * work area unit+0x20: packets, OT, shift, screen and clip limits, the
 * subdivision fields). Per quad (0x18-byte records, the next record's
 * vertex words fetched ahead into the stack): RTPT, back faces culled
 * (NCLIP), the fourth vertex, the packet's colour, UVs and positions,
 * the OT slot by AVSZ4; a quad near enough or off screen straight in
 * (or DivOffscreenF), else its vertices and UVs copied (unit+0x84) and
 * handed to DivQuadFT4f. The packet pointer into 0x80065C08; *unit + 4.
 * The original's s-registers. */

/* 8004867C (tri, work, depth): the flat version of 800414B4 (8-byte
 * vertices, children at tri + 0x18, POLY_F3-sized packets of 0x20 bytes
 * from the template): all four children recursed, none emitted between
 * (midpoints by 80048B54, off screen by 80048A04). The original's
 * s-registers. */
void n_gs_DivTriF3R(void) {
    FRAME(0x48);
    u32 sv[8];
    for (int i = 0; i < 8; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x20 + (u32)i * 4, sv[i]); }
    MEM_W32(sp + 0x40, R[31]);
    R[19] = A1; R[22] = A2; R[20] = A0;
    R[21] = R[20] + 0x18; R[23] = R[21];
    const u32 t = R[20], w = R[19];
    gte_write_data(0, MEM_R32(t));
    gte_write_data(1, MEM_R32(t + 4));
    gte_write_data(2, MEM_R32(t + 8));
    gte_write_data(3, MEM_R32(t + 0xC));
    gte_write_data(4, MEM_R32(t + 0x10));
    gte_write_data(5, MEM_R32(t + 0x14));
    gte_command(0x0280030u);
    R[16] = w + 0x38;
    const u32 tp = R[16];
    MEM_W16(tp + 0xC, MEM_R16(t + 6));
    MEM_W16(tp + 0x14, MEM_R16(t + 0xE));
    MEM_W16(tp + 0x1C, MEM_R16(t + 0x16));
    MEM_W32(w + 0x24, gte_read_ctrl(31));
    gte_swc2(tp + 8, 12);
    gte_swc2(tp + 0x10, 13);
    gte_swc2(tp + 0x18, 14);
    gte_command(0x158002Du);
    MEM_W32(w + 0x18, (u32)((s32)gte_read_data(19) >> 2));
    if (gcall(0x80048A04u, tp, w, R[6], R[7]) != 0) goto out;
    int emit = 0;
    if (MEM_R32(w) == R[22]) {
        V0 = MEM_R32(w + 0x24) & 0x7F85E000u;
        if (V0 != 0) goto out;
        emit = 1;
    } else if ((s32)MEM_R32(w + 0x24) < 0) {
        R[22]++;
    } else {
        s32 dx = (s16)MEM_R16(w + 0x2C) - (s16)MEM_R16(w + 0x28);
        s32 dy = (s16)MEM_R16(w + 0x2E) - (s16)MEM_R16(w + 0x2A);
        if (dx < (s16)MEM_R16(w + 0x20) && dy < (s16)MEM_R16(w + 0x22)) emit = 1;
        else if (!(dx < 1023)) R[22]++;
        else if (!(dy < 511)) R[22]++;
        else R[22] = MEM_R32(w);
    }
    if (emit) {
        for (u32 i = 0; i < 0x20; i += 4) MEM_W32(MEM_R32(w + 0x14) + i, MEM_R32(tp + i));
        u32 tag = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
        MEM_W32(w + 0x34, tag);
        MEM_W32(MEM_R32(w + 0x14), (MEM_R32(tag) & 0xFFFFFF) | 0x07000000u);
        MEM_W32(MEM_R32(w + 0x34), MEM_R32(w + 0x14) & 0xFFFFFF);
        V0 = MEM_R32(w + 0x14) + 0x20;
        MEM_W32(w + 0x14, V0);
        goto out;
    }
    R[17] = R[20] + 8;
    copy8(R[23], R[20]);
    gcall(0x80048B54u, R[23] + 8, R[20], R[17], R[7]);
    R[18] = R[23] + 0x10;
    R[16] = R[20] + 0x10;
    gcall(0x80048B54u, R[18], R[20], R[16], R[7]);
    gcall(0x8004867Cu, R[21], R[19], R[22], R[7]);
    gcall(0x80048B54u, R[23], R[17], R[16], R[7]);
    gcall(0x8004867Cu, R[21], R[19], R[22], R[7]);
    copy8(R[23] + 0x10, R[20] + 8);
    gcall(0x8004867Cu, R[21], R[19], R[22], R[7]);
    copy8(R[23] + 8, R[20] + 0x10);
    gcall(0x80048B54u, R[18], R[20], R[16], R[7]);
    gcall(0x8004867Cu, R[21], R[19], R[22], R[7]);
out:
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
}

/* 8004A1D8 (unit): the Gouraud-textured version of 80049664 (0x24-byte
 * records, all three vertex words fetched ahead into the stack, POLY_GT4
 * packets of 0x34 bytes, the copied quad at unit+0x90 with 0xC-byte
 * vertices carrying the colours; DivOffscreen 8004ABBC, DivQuadGT4f).
 * The original's s-registers. */

/* 80048BE8 (unit): the subdividing Gouraud-textured triangle driver
 * (0x1C-byte records, the next record's three vertex numbers fetched
 * ahead into the stack): RTPT, back faces culled (NCLIP into
 * unit+0x38), POLY_GT3 packets of 0x28 bytes by AVSZ3; near or off
 * screen (8004942C) or too big: the triangle copied (unit+0x80, 0xC-byte
 * vertices with their colours) and handed to 80049050. The original's
 * s-registers. */
void n_gs_DriverGT3Div(void) {
    FRAME(0x60);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x50, sv[6]);
    MEM_W32(sp + 0x5C, R[31]);
    MEM_W32(sp + 0x58, sfp);
    MEM_W32(sp + 0x54, sv[7]);
    for (int i = 5; i >= 0; i--) MEM_W32(sp + 0x38 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    R[22] = c;
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x28, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 verts = MEM_R32(c + 0x18);
    R[20] = verts;
    const u32 w = c + 0x20;
    R[16] = w;
    MEM_W32(w, GV(u32, 0x80062F50u));
    MEM_W16(w + 0x30, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x32, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    R[17] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[17]);
    R[21] = MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2);
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3B, 9);
    MEM_W8(w + 0x3F, 0x34);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    R[23] = 0;
    MEM_W32(sp + 0x10, MEM_R16(R[21] + 0x16));
    MEM_W32(sp + 0x14, MEM_R16(R[21] + 0x18));
    R[19] = c + 0x80;
    MEM_W32(sp + 0x24, c + 0x80);
    MEM_W32(sp + 0x18, MEM_R16(R[21] + 0x1A));
    if (n != 0) {
        MEM_W32(sp + 0x2C, c + 0x38);
        MEM_W32(sp + 0x30, 0x00FFFFFFu);
        R[18] = R[21] + 0x12;
#define VX(i) (verts + ((u32)(i) << 3))
        for (;;) {
            const u32 e = R[18], rec = R[21];
            u32 i0 = MEM_R32(sp + 0x10), i1 = MEM_R32(sp + 0x14), i2 = MEM_R32(sp + 0x18);
            u32 a0 = VX(i0), a1 = VX(i1), a2 = VX(i2);
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            R[30] = i0;
            MEM_W32(sp + 0x1C, i1);
            MEM_W32(sp + 0x10, MEM_R16(e + 0x20));
            MEM_W32(sp + 0x20, i2);
            MEM_W32(sp + 0x14, MEM_R16(e + 0x22));
            MEM_W32(sp + 0x18, MEM_R16(e + 0x24));
            MEM_W32(w + 0x24, gte_read_ctrl(31));
            gte_command(0x1400006u);
            const u32 pk = R[17];
            MEM_W32(pk + 0xC, MEM_R32(e - 6));
            const u32 zw = MEM_R32(sp + 0x2C);
            gte_swc2(zw, 24);
            if ((s32)MEM_R32(w + 0x18) > 0) {
                MEM_W32(pk + 0x18, MEM_R32(e - 2));
                MEM_W32(pk + 0x24, MEM_R32(e + 2));
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x14, 13);
                gte_swc2(pk + 0x20, 14);
                gte_command(0x158002Du);
                MEM_W32(pk + 4, MEM_R32(rec) | 0x02000000u);
                gte_swc2(zw, 7);
                MEM_W32(pk + 0x10, MEM_R32(e - 0xE));
                MEM_W32(pk + 0x1C, MEM_R32(e - 0xA));
                u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                MEM_W32(w + 0x34, slot);
                int direct = 0;
                if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(w + 0x24) >= 0)
                    direct = 1;
                else if (gcall(0x8004942Cu, pk, w, R[6], R[7]) == 0) {
                    if ((s32)MEM_R32(w + 0x24) >= 0
                        && (s32)(s16)MEM_R16(w + 0x2C) - (s16)MEM_R16(w + 0x28) < (s16)MEM_R16(w + 0x20)
                        && (s32)(s16)MEM_R16(w + 0x2E) - (s16)MEM_R16(w + 0x2A) < (s16)MEM_R16(w + 0x22)) {
                        LOOP_POLL(0x80048EF8u);
                        direct = 1;
                    } else {
                        const u32 q = R[19];
                        copy8(q, VX(R[30]));
                        copy8(q + 0xC, VX(MEM_R32(sp + 0x1C)));
                        copy8(q + 0x18, VX(MEM_R32(sp + 0x20)));
                        MEM_W32(q + 8, MEM_R32(pk + 4));
                        MEM_W32(q + 0x14, MEM_R32(pk + 0x10));
                        MEM_W32(q + 0x20, MEM_R32(pk + 0x1C));
                        MEM_W16(q + 6, MEM_R16(e - 6));
                        MEM_W16(q + 0x12, MEM_R16(e - 2));
                        MEM_W16(q + 0x1E, MEM_R16(e + 2));
                        MEM_W16(w + 0x46, MEM_R16(e - 4));
                        MEM_W32(w + 0x14, pk);
                        MEM_W16(w + 0x52, MEM_R16(e));
                        gcall(0x80049050u, MEM_R32(sp + 0x24), w, 0, R[7]);
                        R[17] = MEM_R32(w + 0x14);
                    }
                }
                if (direct) {
                    const u32 m = MEM_R32(sp + 0x30);
                    u32 sl = MEM_R32(w + 0x34);
                    MEM_W32(pk, (MEM_R32(sl) & m) | 0x09000000u);
                    MEM_W32(sl, pk & m);
                    R[17] = pk + 0x28;
                }
            }
            R[23]++;
            R[18] += 0x1C;
            R[21] += 0x1C;
            if (!((s32)R[23] < (s32)MEM_R32(sp + 0x28))) break;
            LOOP_POLL(0x80049008u);
        }
#undef VX
    }
    GV(u32, 0x80065C08u) = R[17];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 80049050 (tri, work, depth): 80042FB0 for 80048BE8 (0xC-byte vertices
 * with colours, children at tri + 0x24, POLY_GT3 packets): all four
 * children recursed, none emitted between (midpoints 8004957C, off
 * screen 8004942C). The original's s-registers. */
void n_gs_DivTriGT3S(void) {
    FRAME(0x48);
    u32 sv[8];
    for (int i = 0; i < 8; i++) { sv[i] = R[16 + i]; MEM_W32(sp + 0x20 + (u32)i * 4, sv[i]); }
    MEM_W32(sp + 0x40, R[31]);
    R[19] = A1; R[22] = A2; R[20] = A0;
    R[21] = R[20] + 0x24; R[23] = R[21];
    const u32 t = R[20], w = R[19];
    gte_write_data(0, MEM_R32(t));
    gte_write_data(1, MEM_R32(t + 4));
    gte_write_data(2, MEM_R32(t + 0xC));
    gte_write_data(3, MEM_R32(t + 0x10));
    gte_write_data(4, MEM_R32(t + 0x18));
    gte_write_data(5, MEM_R32(t + 0x1C));
    gte_command(0x0280030u);
    R[16] = w + 0x38;
    const u32 tp = R[16];
    MEM_W16(tp + 0xC, MEM_R16(t + 6));
    MEM_W16(tp + 0x18, MEM_R16(t + 0x12));
    MEM_W16(tp + 0x24, MEM_R16(t + 0x1E));
    MEM_W32(w + 0x24, gte_read_ctrl(31));
    gte_swc2(tp + 8, 12);
    gte_swc2(tp + 0x14, 13);
    gte_swc2(tp + 0x20, 14);
    gte_command(0x158002Du);
    MEM_W32(w + 0x18, (u32)((s32)gte_read_data(19) >> 2));
    if (gcall(0x8004942Cu, tp, w, R[6], R[7]) != 0) goto out;
    int emit = 0;
    if (MEM_R32(w) == R[22]) {
        V0 = MEM_R32(w + 0x24) & 0x7F85E000u;
        if (V0 != 0) goto out;
        emit = 1;
    } else if ((s32)MEM_R32(w + 0x24) < 0) {
        R[22]++;
    } else {
        s32 dx = (s16)MEM_R16(w + 0x2C) - (s16)MEM_R16(w + 0x28);
        s32 dy = (s16)MEM_R16(w + 0x2E) - (s16)MEM_R16(w + 0x2A);
        if (dx < (s16)MEM_R16(w + 0x20) && dy < (s16)MEM_R16(w + 0x22)) emit = 1;
        else if (!(dx < 1023)) R[22]++;
        else if (!(dy < 511)) R[22]++;
        else R[22] = MEM_R32(w);
    }
    if (emit) {
        static const u8 src_t[10] = { 0, 1, 0, 0, 1, 0, 0, 1, 0, 0 };
        static const u8 off[10] = { 0, 8, 8, 0xC, 0x14, 0x14, 0x18, 0x20, 0x20, 0x24 };
        for (u32 i = 0; i < 10; i++)
            MEM_W32(MEM_R32(w + 0x14) + i * 4, MEM_R32((src_t[i] ? t : tp) + off[i]));
        u32 tag = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
        MEM_W32(w + 0x34, tag);
        MEM_W32(MEM_R32(w + 0x14), (MEM_R32(tag) & 0xFFFFFF) | 0x09000000u);
        MEM_W32(MEM_R32(w + 0x34), MEM_R32(w + 0x14) & 0xFFFFFF);
        V0 = MEM_R32(w + 0x14) + 0x28;
        MEM_W32(w + 0x14, V0);
        goto out;
    }
    R[17] = R[20] + 0xC;
    copy12(R[23], R[20]);
    gcall(0x8004957Cu, R[23] + 0xC, R[20], R[17], R[7]);
    R[18] = R[23] + 0x18;
    R[16] = R[20] + 0x18;
    gcall(0x8004957Cu, R[18], R[20], R[16], R[7]);
    gcall(0x80049050u, R[21], R[19], R[22], R[7]);
    gcall(0x8004957Cu, R[23], R[17], R[16], R[7]);
    gcall(0x80049050u, R[21], R[19], R[22], R[7]);
    copy12(R[23] + 0x18, R[20] + 0xC);
    gcall(0x80049050u, R[21], R[19], R[22], R[7]);
    copy12(R[23] + 0xC, R[20] + 0x18);
    gcall(0x8004957Cu, R[18], R[20], R[16], R[7]);
    gcall(0x80049050u, R[21], R[19], R[22], R[7]);
out:
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
}

/* 80048240 (unit): 80048BE8 for flat-shaded textured triangles
 * (0x14-byte records, POLY_FT3 packets of 0x20 bytes, the copied
 * triangle at unit+0x78 with 8-byte vertices; its colour into
 * unit+0x5C; 80048A04, 8004867C). The original's s-registers. */
void n_gs_DriverFT3Div(void) {
    FRAME(0x60);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x50, sv[6]);
    MEM_W32(sp + 0x5C, R[31]);
    MEM_W32(sp + 0x58, sfp);
    MEM_W32(sp + 0x54, sv[7]);
    for (int i = 5; i >= 0; i--) MEM_W32(sp + 0x38 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    R[22] = c;
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x28, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 verts = MEM_R32(c + 0x18);
    R[21] = verts;
    const u32 w = c + 0x20;
    R[16] = w;
    MEM_W32(w, GV(u32, 0x80062F50u));
    MEM_W16(w + 0x30, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x32, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    R[18] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[18]);
    R[20] = MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2);
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3B, 7);
    MEM_W8(w + 0x3F, 0x24);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    R[23] = 0;
    MEM_W32(sp + 0x10, MEM_R16(R[20] + 0xE));
    MEM_W32(sp + 0x14, MEM_R16(R[20] + 0x10));
    R[19] = c + 0x78;
    MEM_W32(sp + 0x24, c + 0x78);
    MEM_W32(sp + 0x18, MEM_R16(R[20] + 0x12));
    if (n != 0) {
        R[17] = R[20] + 0xA;
        MEM_W32(sp + 0x2C, c + 0x38);
        MEM_W32(sp + 0x30, 0x00FFFFFFu);
#define VX(i) (verts + ((u32)(i) << 3))
        for (;;) {
            const u32 e = R[17], rec = R[20];
            u32 i0 = MEM_R32(sp + 0x10), i1 = MEM_R32(sp + 0x14), i2 = MEM_R32(sp + 0x18);
            u32 a0 = VX(i0), a1 = VX(i1), a2 = VX(i2);
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            R[30] = i0;
            MEM_W32(sp + 0x1C, i1);
            MEM_W32(sp + 0x10, MEM_R16(e + 0x18));
            MEM_W32(sp + 0x20, i2);
            MEM_W32(sp + 0x14, MEM_R16(e + 0x1A));
            MEM_W32(sp + 0x18, MEM_R16(e + 0x1C));
            MEM_W32(w + 0x24, gte_read_ctrl(31));
            gte_command(0x1400006u);
            const u32 pk = R[18];
            MEM_W32(pk + 0xC, MEM_R32(e - 6));
            const u32 zw = MEM_R32(sp + 0x2C);
            gte_swc2(zw, 24);
            if ((s32)MEM_R32(w + 0x18) > 0) {
                MEM_W32(pk + 0x1C, MEM_R32(e + 2));
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x10, 13);
                gte_swc2(pk + 0x18, 14);
                gte_command(0x158002Du);
                MEM_W32(pk + 4, MEM_R32(rec) | 0x02000000u);
                gte_swc2(zw, 7);
                MEM_W32(pk + 0x14, MEM_R32(e - 2));
                u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                MEM_W32(w + 0x34, slot);
                int direct = 0;
                if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(w + 0x24) >= 0)
                    direct = 1;
                else if (gcall(0x80048A04u, pk, w, R[6], R[7]) == 0) {
                    if ((s32)MEM_R32(w + 0x24) >= 0
                        && (s32)(s16)MEM_R16(w + 0x2C) - (s16)MEM_R16(w + 0x28) < (s16)MEM_R16(w + 0x20)
                        && (s32)(s16)MEM_R16(w + 0x2E) - (s16)MEM_R16(w + 0x2A) < (s16)MEM_R16(w + 0x22)) {
                        LOOP_POLL(0x80048538u);
                        direct = 1;
                    } else {
                        MEM_W32(w + 0x3C, MEM_R32(pk + 4));
                        const u32 q = R[19];
                        copy8(q, VX(R[30]));
                        copy8(q + 8, VX(MEM_R32(sp + 0x1C)));
                        copy8(q + 0x10, VX(MEM_R32(sp + 0x20)));
                        MEM_W16(q + 6, MEM_R16(e - 6));
                        MEM_W16(q + 0xE, MEM_R16(e - 2));
                        MEM_W16(q + 0x16, MEM_R16(e + 2));
                        MEM_W16(w + 0x46, MEM_R16(e - 4));
                        MEM_W32(w + 0x14, pk);
                        MEM_W16(w + 0x4E, MEM_R16(e));
                        gcall(0x8004867Cu, MEM_R32(sp + 0x24), w, 0, R[7]);
                        R[18] = MEM_R32(w + 0x14);
                    }
                }
                if (direct) {
                    const u32 m = MEM_R32(sp + 0x30);
                    u32 sl = MEM_R32(w + 0x34);
                    MEM_W32(pk, (MEM_R32(sl) & m) | 0x07000000u);
                    MEM_W32(sl, pk & m);
                    R[18] = pk + 0x20;
                }
            }
            R[23]++;
            R[17] += 0x14;
            R[20] += 0x14;
            if (!((s32)R[23] < (s32)MEM_R32(sp + 0x28))) break;
            LOOP_POLL(0x80048634u);
        }
#undef VX
    }
    GV(u32, 0x80065C08u) = R[18];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8002BF28 GsSortSprite(sp, ot, pri): GsSortFastSprite's sprite with
 * scale (+0x1C / +0x1E, 4096 = 1), rotation (+0x20, 4096 to a degree)
 * and flips (attribute bits 22 / 23) about the centre +0x18 / +0x1A.
 * Unscaled, unrotated and unflipped, or with attribute bit 27: the
 * fast path's draw mode and SPRT (0x18 bytes). Else the corners through
 * the GTE (the screen-twist matrix 0x80065B90 rotated, scaled and moved
 * to x, y and 8002C428's depth; 8002C438) as a draw mode and POLY_FT4
 * (0x28 bytes). Linked at pri (8002BE88), the packet pointer advanced.
 * The original's s-registers. */
void n_gs_SortSprite(void) {
    FRAME(0xB0);
    u32 sv[5];
    for (int i = 0; i < 5; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x98, sv[0]);
    MEM_W32(sp + 0xA4, sv[3]);
    MEM_W32(sp + 0xAC, R[31]);
    MEM_W32(sp + 0xA8, sv[4]);
    MEM_W32(sp + 0xA0, sv[2]);
    MEM_W32(sp + 0x9C, sv[1]);
    const u32 g = A0, ot = A1, pri = A2;
    R[16] = g; R[19] = ot; R[20] = pri;
    const u32 at = MEM_R32(g);
    R[17] = at;
    if ((s32)at < 0 || MEM_R16(g + 8) == 0 || MEM_R16(g + 0xA) == 0) goto out;
    {
        const u32 b27 = (at >> 27) & 1;
        R[18] = GV(u32, 0x80065C08u);
        const u32 pk = R[18];
        u32 fast;
        if (MEM_R32(g + 0x1C) != 0x10001000u || MEM_R32(g + 0x20) != 0) fast = b27;
        else fast = b27 | ((at & 0x00C00000u) < 1);
        const u32 rgb = ((u32)MEM_R8(g + 0x16) << 16) | ((u32)MEM_R8(g + 0x15) << 8) | MEM_R8(g + 0x14);
        const u32 abe = ((at >> 5) & 0x02000000u) | ((at << 18) & 0x01000000u);
        const u32 clut = ((u32)(s32)(s16)MEM_R16(g + 0x12) << 22) | (((u32)(s32)(s16)MEM_R16(g + 0x10) << 12) & 0x003F0000u);
        u32 last, words;
        if (fast) {
            u32 m = ((at >> 17) & 0x180) | 0xE1000200u;
            MEM_W32(pk + 4, (MEM_R16(g + 0xC) & 0x1F) | m | ((at >> 23) & 0x60));
            s32 x = (s16)MEM_R16(g + 4) + (s16)GV(u16, 0x80065A84u);
            s32 y = (s16)MEM_R16(g + 6) + (s16)GV(u16, 0x80065A86u);
            MEM_W32(pk + 8, abe | 0x64000000u | rgb);
            u32 xs = (u32)(x - (s16)MEM_R16(g + 0x18)) & 0xFFFF;
            u32 ys = (u32)(y - (s16)MEM_R16(g + 0x1A)) << 16;
            MEM_W32(pk + 0xC, xs | ys);
            MEM_W32(pk + 0x10, MEM_R8(g + 0xE) | ((u32)MEM_R8(g + 0xF) << 8) | clut);
            last = MEM_R16(g + 8) | ((u32)MEM_R16(g + 0xA) << 16);
            words = 5;
        } else {
            const u32 M = sp + 0x28;
            if (MEM_R32(g + 0x20) == 0) {
                for (u32 i = 0; i < 0x20; i += 4) MEM_W32(M + i, GV(u32, 0x80065B90u + i));
            } else {
                MEM_W16(sp + 0x48, 0);
                MEM_W16(sp + 0x4A, 0);
                s32 r = (s32)MEM_R32(g + 0x20);
                s32 hi = (s32)(((s64)r * (s32)0xB60B60B7) >> 32);
                MEM_W16(sp + 0x4C, (u16)(((hi + r) >> 8) - (r >> 31)));
                gcall(0x8002AA68u, sp + 0x48, M, R[6], R[7]);
            }
            if (MEM_R32(g + 0x1C) != 0x10001000u) {
                MEM_W32(sp + 0x50, (u32)(s32)(s16)MEM_R16(g + 0x1C));
                MEM_W32(sp + 0x58, 0);
                MEM_W32(sp + 0x54, (u32)(s32)(s16)MEM_R16(g + 0x1E));
                gcall(0x8002A638u, M, sp + 0x50, R[6], R[7]);
            }
            MEM_W32(sp + 0x50, (u32)(s32)(s16)MEM_R16(g + 4));
            MEM_W32(sp + 0x54, (u32)(s32)(s16)MEM_R16(g + 6));
            MEM_W32(sp + 0x58, gcall(0x8002C428u, R[4], sp + 0x50, R[6], R[7]));
            gcall(0x8002A608u, M, sp + 0x50, R[6], R[7]);
            gcall(0x8002A778u, M, R[5], R[6], R[7]);
            gcall(0x8002A7A8u, M, R[5], R[6], R[7]);
            u32 mx = (u32)-(s32)(s16)MEM_R16(g + 0x18), my = (u32)-(s32)(s16)MEM_R16(g + 0x1A);
            MEM_W16(sp + 0x64, 0);
            MEM_W16(sp + 0x60, (u16)mx);
            MEM_W16(sp + 0x62, (u16)my);
            MEM_W16(sp + 0x6A, (u16)my);
            MEM_W16(sp + 0x6C, 0);
            MEM_W16(sp + 0x70, (u16)mx);
            MEM_W16(sp + 0x68, (u16)(MEM_R16(g + 8) + mx));
            MEM_W16(sp + 0x74, 0);
            MEM_W16(sp + 0x72, (u16)(MEM_R16(g + 0xA) + my));
            MEM_W16(sp + 0x78, (u16)(MEM_R16(g + 8) + mx));
            MEM_W16(sp + 0x7C, 0);
            MEM_W16(sp + 0x7A, (u16)(MEM_R16(g + 0xA) + my));
            for (u32 i = 0; i < 6; i++) MEM_W32(sp + 0x10 + i * 4, sp + 0x80 + i * 4);
            gcall(0x8002C438u, sp + 0x60, sp + 0x68, sp + 0x70, sp + 0x78);
            u32 u0, u1, v0, v1;
            if (at & 0x00800000u) { u1 = MEM_R8(g + 0xE); u0 = u1 + MEM_R8(g + 8) - 1; }
            else { u0 = MEM_R8(g + 0xE); u1 = u0 + MEM_R8(g + 8) - 1; }
            if (at & 0x00400000u) { v1 = MEM_R8(g + 0xF); v0 = v1 + MEM_R8(g + 0xA) - 1; }
            else { v0 = MEM_R8(g + 0xF); v1 = v0 + MEM_R8(g + 0xA) - 1; }
            u0 &= 0xFF;
            v0 = (v0 & 0xFF) << 8;
            MEM_W32(pk + 8, MEM_R32(sp + 0x80));
            MEM_W32(pk + 4, abe | 0x2C000000u | rgb);
            MEM_W32(pk + 0x10, MEM_R32(sp + 0x84));
            MEM_W32(pk + 0xC, u0 | v0 | clut);
            MEM_W32(pk + 0x18, MEM_R32(sp + 0x88));
            u32 vv1 = (v1 & 0xFF) << 8;
            MEM_W32(pk + 0x1C, u0 | vv1);
            MEM_W32(pk + 0x20, MEM_R32(sp + 0x8C));
            u32 uu1 = u1 & 0xFF;
            MEM_W32(pk + 0x24, uu1 | vv1);
            last = uu1 | v0 | ((MEM_R16(g + 0xC) & 0x1F) << 16) | ((at >> 1) & 0x01800000u) | ((at >> 7) & 0x00600000u);
            words = 9;
        }
        MEM_W32(pk + 0x14, last);
        R[18] = gcall(0x8002BE88u, pk, ot, pri & 0xFFFF, words);
        GV(u32, 0x80065C08u) = R[18];
    }
out:
    for (int i = 0; i < 5; i++) R[16 + i] = sv[i];
}

/* A shared-vertex colour (two words: 11-bit fixed channels at bits
 * 5-15, 21-31 and the second word's 5-...), clamped to 255 each. */
static u32 shared_colour(u32 tab, u32 idx) {
    u32 e = ((idx << 16) >> 13) + tab;
    u32 c0 = MEM_R32(e), c1 = MEM_R32(e + 4);
    u32 r = (c0 << 16) >> 21, g = c0 >> 21, b = c1 >> 5;
    if (!((s32)r < 255)) r = 0xFF;
    if (!((s32)g < 255)) g = 0xFF;
    if (!((s32)b < 255)) b = 0xFF;
    return r | (g << 8) | (b << 16);
}

/* 8002EEC8 (unit): the HMD shared-vertex Gouraud-textured triangle
 * driver: vertices already projected (unit+0x1C: SXY, then SZ, -1 for
 * one not drawn), colours from unit+0x24; back faces culled (NCLIP);
 * POLY_GT3 packets by AVSZ3 into the OT. Returns the block + 8 (the
 * block pointer is not moved). */
void n_gs_SharedGT3(void) {
    const u32 c = A0, blk = MEM_R32(c);
    u32 pk = MEM_R32(c + 0x10);
    u32 n = MEM_R16(blk + 2);
    const u32 vt = MEM_R32(c + 0x1C), ct = MEM_R32(c + 0x24);
    u32 rec = MEM_R32(c + 0x14) + (MEM_R32(blk + 4) << 2);
    if (n != 0) {
        for (;;) {
            u32 w0 = MEM_R32(rec + 0xC), w1 = MEM_R32(rec + 0x10), w2 = MEM_R32(rec + 0x14);
            u32 a0 = ((w0 >> 16) << 3) + vt, a1 = ((w1 >> 16) << 3) + vt, a2 = ((w2 >> 16) << 3) + vt;
            u32 z;
            if ((z = MEM_R32(a0 + 4)) != 0xFFFFFFFFu) {
                gte_write_data(17, z);
                if ((z = MEM_R32(a1 + 4)) != 0xFFFFFFFFu) {
                    gte_write_data(18, z);
                    if ((z = MEM_R32(a2 + 4)) != 0xFFFFFFFFu) {
                        gte_write_data(19, z);
                        u32 s0 = MEM_R32(a0), s1 = MEM_R32(a1), s2 = MEM_R32(a2);
                        gte_write_data(12, s0);
                        gte_write_data(13, s1);
                        gte_write_data(14, s2);
                        gte_command(0x1400006u);
                        MEM_W32(pk + 8, s0);
                        MEM_W32(pk + 0x14, s1);
                        MEM_W32(pk + 0x20, s2);
                        if ((s32)gte_read_data(24) > 0) {
                            MEM_W32(pk + 4, shared_colour(ct, w0) | 0x34000000u);
                            LOOP_POLL(0x8002F048u);
                            MEM_W32(pk + 0x10, shared_colour(ct, w1));
                            LOOP_POLL(0x8002F050u);
                            MEM_W32(pk + 0x1C, shared_colour(ct, w2));
                            gte_command(0x158002Du);
                            MEM_W32(pk + 0xC, MEM_R32(rec));
                            MEM_W32(pk + 0x18, MEM_R32(rec + 4));
                            MEM_W32(pk + 0x24, MEM_R32(rec + 8));
                            u32 ot = MEM_R32(MEM_R32(c + 4) + 4);
                            u32 zoff = MEM_R32(c + 0xC), sh = MEM_R32(c + 8);
                            u32 slot = ((((gte_read_data(7) - zoff) >> (sh & 31)) & 0xFFFF) << 2) + ot;
                            u32 old = MEM_R32(slot);
                            MEM_W32(pk, 0x09000000u | (old & 0xFFFFFF));
                            MEM_W32(slot, (old & 0xFF000000u) | (pk & 0xFFFFFF));
                            pk += 0x28;
                        }
                    }
                }
            }
            n--;
            rec += 0x18;
            if (n == 0) break;
            LOOP_POLL(0x8002F0D8u);
        }
    }
    GV(u32, 0x80065C08u) = pk;
    V0 = MEM_R32(c) + 8;
}

static void shared_link(u32 c, u32 pk, u32 code) {
    u32 ot = MEM_R32(MEM_R32(c + 4) + 4);
    u32 zoff = MEM_R32(c + 0xC), sh = MEM_R32(c + 8);
    u32 slot = ((((gte_read_data(7) - zoff) >> (sh & 31)) & 0xFFFF) << 2) + ot;
    u32 old = MEM_R32(slot);
    MEM_W32(pk, code | (old & 0xFFFFFF));
    MEM_W32(slot, (old & 0xFF000000u) | (pk & 0xFFFFFF));
}

/* 8002F548 (unit): the shared-vertex Gouraud-textured quad driver (0x1C-
 * byte records): a quad facing away is tried as the triangle of its last
 * three corners (POLY_GT3 with the fourth corner's colour and UV, drawn
 * when that NCLIP is not positive); else a POLY_GT4. Keeps r16-r18 in
 * the unit (+0x18, +0x20, +0x10) as the
 * original, which also leaves +0x10 overwritten with the caller's r18.
 * Returns the block + 8. */
void n_gs_SharedGT4(void) {
    const u32 c = A0;
    MEM_W32(c + 0x18, R[16]);
    MEM_W32(c + 0x20, R[17]);
    const u32 blk = MEM_R32(c);
    u32 pk = MEM_R32(c + 0x10);
    u32 n = MEM_R16(blk + 2);
    const u32 vt = MEM_R32(c + 0x1C), ct = MEM_R32(c + 0x24);
    u32 rec = MEM_R32(c + 0x14) + (MEM_R32(blk + 4) << 2);
    if (n != 0) {
        MEM_W32(c + 0x10, R[18]);
        for (;;) {
            u32 w0 = MEM_R32(rec + 0xC), w1 = MEM_R32(rec + 0x10), w2 = MEM_R32(rec + 0x14), w3 = MEM_R32(rec + 0x18);
            u32 a0 = ((w0 >> 16) << 3) + vt, a1 = ((w1 >> 16) << 3) + vt;
            u32 a2 = ((w2 >> 16) << 3) + vt, a3 = ((w3 >> 16) << 3) + vt;
            int vis = 0;
            u32 z;
            if ((z = MEM_R32(a0 + 4)) != 0xFFFFFFFFu) {
                gte_write_data(16, z);
                if ((z = MEM_R32(a1 + 4)) != 0xFFFFFFFFu) {
                    gte_write_data(17, z);
                    if ((z = MEM_R32(a2 + 4)) != 0xFFFFFFFFu) {
                        gte_write_data(18, z);
                        if ((z = MEM_R32(a3 + 4)) != 0xFFFFFFFFu) {
                            gte_write_data(19, z);
                            vis = 1;
                        }
                    }
                }
            }
            if (vis) {
                u32 s0 = MEM_R32(a0), s1 = MEM_R32(a1), s2 = MEM_R32(a2);
                gte_write_data(12, s0);
                gte_write_data(13, s1);
                gte_write_data(14, s2);
                u32 s3 = MEM_R32(a3);
                u32 uv2 = MEM_R32(rec + 8);
                gte_command(0x1400006u);
                MEM_W32(pk + 8, s0);
                MEM_W32(pk + 0x14, s1);
                MEM_W32(pk + 0x20, s2);
                MEM_W32(pk + 0x2C, s3);
                if ((s32)gte_read_data(24) > 0) {
                    MEM_W32(pk + 4, shared_colour(ct, uv2 >> 16) | 0x3C000000u);
                    LOOP_POLL(0x8002F874u);
                    MEM_W32(pk + 0x10, shared_colour(ct, w1));
                    LOOP_POLL(0x8002F87Cu);
                    MEM_W32(pk + 0x1C, shared_colour(ct, w2));
                    LOOP_POLL(0x8002F884u);
                    MEM_W32(pk + 0x28, shared_colour(ct, w3));
                    gte_command(0x168002Eu);
                    MEM_W32(pk + 0xC, MEM_R32(rec));
                    MEM_W32(pk + 0x18, MEM_R32(rec + 4));
                    MEM_W32(pk + 0x24, uv2);
                    MEM_W32(pk + 0x30, w0);
                    shared_link(c, pk, 0x0C000000u);
                    pk += 0x34;
                } else {
                    gte_write_data(12, s3);
                    gte_command(0x1400006u);
                    MEM_W32(pk + 8, s3);
                    if (!((s32)gte_read_data(24) > 0)) {
                        MEM_W32(pk + 4, shared_colour(ct, w3) | 0x34000000u);
                        LOOP_POLL(0x8002F720u);
                        MEM_W32(pk + 0x10, shared_colour(ct, w1));
                        LOOP_POLL(0x8002F728u);
                        MEM_W32(pk + 0x1C, shared_colour(ct, w2));
                        gte_command(0x158002Du);
                        MEM_W32(pk + 0xC, (w0 & 0xFFFF) | (MEM_R32(rec) & 0xFFFF0000u));
                        MEM_W32(pk + 0x18, MEM_R32(rec + 4));
                        MEM_W32(pk + 0x24, uv2);
                        shared_link(c, pk, 0x09000000u);
                        pk += 0x28;
                    }
                }
            }
            n--;
            rec += 0x1C;
            if (n == 0) break;
            LOOP_POLL(0x8002F90Cu);
        }
    }
    GV(u32, 0x80065C08u) = pk;
    V0 = MEM_R32(c) + 8;
    R[16] = MEM_R32(c + 0x18);
    R[17] = MEM_R32(c + 0x20);
    R[18] = MEM_R32(c + 0x10);
}

/* 80045B98 (unit): the subdividing Gouraud quad driver (0x18-byte
 * records: four colours, then the vertex numbers, the next record's
 * fetched ahead): POLY_G4 packets of 0x24 bytes, near or off screen
 * (800464FC) or too big: the quad copied (unit+0x80, 0xC-byte vertices
 * with their colours) for 80046048. The original's s-registers. */
void n_gs_DriverG4Div(void) {
    FRAME(0x58);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x48, sv[6]);
    MEM_W32(sp + 0x30, sv[0]);
    MEM_W32(sp + 0x50, sfp);
    MEM_W32(sp + 0x54, R[31]);
    MEM_W32(sp + 0x4C, sv[7]);
    for (int i = 5; i >= 1; i--) MEM_W32(sp + 0x30 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    R[22] = c;
    const u32 w = c + 0x20;
    R[16] = w;
    R[30] = 0;
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x18, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 verts = MEM_R32(c + 0x18);
    R[20] = verts;
    MEM_W32(w, GV(u32, 0x80062F50u));
    MEM_W16(w + 0x34, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x36, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    R[17] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[17]);
    R[21] = MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2);
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3F, 8);
    MEM_W8(w + 0x43, 0x38);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    {
        u32 a = MEM_R32(R[21] + 0x10);
        R[23] = MEM_R32(R[21] + 0x14);
        R[19] = c + 0x80;
        MEM_W32(sp + 0x14, c + 0x80);
        MEM_W32(sp + 0x1C, a & 0xFFFF);
        MEM_W32(sp + 0x20, (u32)((s32)a >> 16));
        MEM_W32(sp + 0x24, R[23] & 0xFFFF);
    }
    if (n != 0) {
        MEM_W32(sp + 0x28, 0x00FFFFFFu);
        R[18] = R[21] + 0x16;
#define VX(i) (verts + ((u32)(i) << 3))
        for (;;) {
            const u32 e = R[18], rec = R[21];
            u32 a0 = VX(MEM_R32(sp + 0x1C)), a1 = VX(MEM_R32(sp + 0x20)), a2 = VX(MEM_R32(sp + 0x24));
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            u32 v3 = (u32)((s32)R[23] >> 16);
            u32 nx = MEM_R32(e + 0x12);
            R[23] = MEM_R32(e + 0x16);
            MEM_W32(sp + 0x1C, nx & 0xFFFF);
            MEM_W32(sp + 0x24, R[23] & 0xFFFF);
            MEM_W32(w + 0x24, gte_read_ctrl(31));
            gte_command(0x1400006u);
            MEM_W32(sp + 0x20, (u32)((s32)nx >> 16));
            if ((s32)gte_read_data(24) > 0) {
                u32 pk = R[17];
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x10, 13);
                gte_swc2(pk + 0x18, 14);
                u32 a3 = VX(v3);
                gte_write_data(0, MEM_R32(a3));
                gte_write_data(1, MEM_R32(a3 + 4));
                gte_command(0x0180001u);
                MEM_W32(pk + 4, MEM_R32(rec));
                MEM_W32(w + 0x28, gte_read_ctrl(31));
                gte_swc2(pk + 0x20, 14);
                gte_command(0x168002Eu);
                MEM_W32(pk + 0xC, MEM_R32(e - 0x12));
                MEM_W32(pk + 0x14, MEM_R32(e - 0xE));
                MEM_W32(pk + 0x1C, MEM_R32(e - 0xA));
                gte_swc2(w + 0x18, 7);
                u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                MEM_W32(w + 0x38, slot);
                int direct = 0;
                if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(w + 0x28) >= 0 && !(MEM_R32(w + 0x24) & 0x80000000u))
                    direct = 1;
                else if (gcall(0x800464FCu, pk, w, R[6], R[7]) == 0) {
                    if ((s32)MEM_R32(w + 0x28) >= 0 && (s32)MEM_R32(w + 0x24) >= 0
                        && (s32)(s16)MEM_R16(w + 0x30) - (s16)MEM_R16(w + 0x2C) < (s16)MEM_R16(w + 0x20)
                        && (s32)(s16)MEM_R16(w + 0x32) - (s16)MEM_R16(w + 0x2E) < (s16)MEM_R16(w + 0x22)) {
                        LOOP_POLL(0x80045EE4u);
                        direct = 1;
                    } else {
                        const u32 q = R[19];
                        copy8(q, VX(MEM_R16(e - 6)));
                        copy8(q + 0xC, VX(MEM_R16(e - 4)));
                        copy8(q + 0x18, VX(MEM_R16(e - 2)));
                        copy8(q + 0x24, VX(MEM_R16(e)));
                        MEM_W32(q + 8, MEM_R32(pk + 4));
                        MEM_W32(q + 0x14, MEM_R32(pk + 0xC));
                        MEM_W32(q + 0x20, MEM_R32(pk + 0x14));
                        MEM_W32(q + 0x2C, MEM_R32(pk + 0x1C));
                        MEM_W32(w + 0x14, pk);
                        gcall(0x80046048u, MEM_R32(sp + 0x14), w, 0, R[7]);
                        R[17] = MEM_R32(w + 0x14);
                    }
                }
                if (direct) {
                    const u32 m = MEM_R32(sp + 0x28);
                    u32 sl = MEM_R32(w + 0x38);
                    MEM_W32(pk, (MEM_R32(sl) & m) | 0x08000000u);
                    MEM_W32(sl, pk & m);
                    R[17] = pk + 0x24;
                }
            }
            R[30]++;
            R[18] += 0x18;
            R[21] += 0x18;
            if (!((s32)R[30] < (s32)MEM_R32(sp + 0x18))) break;
            LOOP_POLL(0x80046000u);
        }
#undef VX
    }
    GV(u32, 0x80065C08u) = R[17];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* ---- The game's water-aware drivers (its own extension of libgs's
 * subdividing drivers): before drawing each polygon, the floor height
 * under the player (0x800B4D68 x / 0x800B4D6C z, when 0x800A8674 asks
 * and 0x800AF56A hasn't found it yet) and under each live splash /
 * ripple (the 5 entries of 12 bytes at 0x800B3EA8, when 0x800AB514) is
 * taken from the polygon they lie over: its plane (OuterProduct0 of two
 * edges, scaled by the root of |ny| and normalised by 80041C38), the
 * height there, and the slope along the entity's heading (0x800B3D9C /
 * the entry's +8). ---- */

#define WV(i) (verts + ((u32)(i) << 3))
static u32 xz_word(u32 v) { return ((u32)(s32)(s16)MEM_R16(v + 4) << 16) | ((u32)(s32)(s16)MEM_R16(v) & 0xFFFF); }
static s32 xz_nclip(u32 a, u32 b, u32 c) {
    gte_write_data(12, a);
    gte_write_data(13, b);
    gte_write_data(14, c);
    gte_command(0x1400006u);
    return (s32)gte_read_data(24);
}
/* The plane of (b, c, d) relative to b: sp+0x20 its normal / sqrt|ny|,
 * normalised into sp+0x30 by 80041C38. 0 when ny is 0. */
static int water_plane(u32 sp, u32 b, u32 c, u32 d) {
    MEM_W32(sp + 0x10, (u32)((s32)(s16)MEM_R16(c) - (s16)MEM_R16(b)));
    MEM_W32(sp + 0x14, (u32)((s32)(s16)MEM_R16(c + 2) - (s16)MEM_R16(b + 2)));
    MEM_W32(sp + 0x18, (u32)((s32)(s16)MEM_R16(c + 4) - (s16)MEM_R16(b + 4)));
    MEM_W32(sp + 0x30, (u32)((s32)(s16)MEM_R16(d) - (s16)MEM_R16(b)));
    MEM_W32(sp + 0x34, (u32)((s32)(s16)MEM_R16(d + 2) - (s16)MEM_R16(b + 2)));
    MEM_W32(sp + 0x38, (u32)((s32)(s16)MEM_R16(d + 4) - (s16)MEM_R16(b + 4)));
    gcall(0x80041FE4u, sp + 0x10, sp + 0x30, sp + 0x20, R[7]);
    s32 ny = (s32)MEM_R32(sp + 0x24);
    if (ny == 0) return 0;
    if (ny < 0) ny = -ny;
    s32 r = (s32)gcall(0x8002A3D8u, (u32)ny, R[5], R[6], R[7]);
    s32 a = mips_div((s32)MEM_R32(sp + 0x20), r);
    s32 b2 = mips_div((s32)MEM_R32(sp + 0x24), r);
    s32 c2 = mips_div((s32)MEM_R32(sp + 0x28), r);
    MEM_W32(sp + 0x20, (u32)a);
    MEM_W32(sp + 0x24, (u32)b2);
    MEM_W32(sp + 0x28, (u32)c2);
    gcall(0x80041C38u, sp + 0x20, sp + 0x30, R[6], R[7]);
    return 1;
}
/* The plane's height offset at (x, z) from b (b.y less it) and the
 * slope along angle ang. */
static s32 water_height(u32 sp, u32 b, s32 x, s32 z) {
    MEM_W32(sp + 0x10, (u32)x);
    MEM_W32(sp + 0x14, 0);
    MEM_W32(sp + 0x18, (u32)z);
    s32 dx = x - (s16)MEM_R16(b);
    u32 p = (u32)((s32)MEM_R32(sp + 0x30) * dx);
    MEM_W32(sp + 0x10, (u32)dx);
    MEM_W32(sp + 0x14, (u32)-(s32)(s16)MEM_R16(b + 2));
    s32 dz = z - (s16)MEM_R16(b + 4);
    u32 q = (u32)((s32)MEM_R32(sp + 0x38) * dz);
    s32 h = mips_div((s32)(p + q), (s32)MEM_R32(sp + 0x34));
    MEM_W32(sp + 0x18, (u32)dz);
    return (s16)MEM_R16(b + 2) - h;
}
static s32 water_slope(u32 sp, s32 ang) {
    s32 c = (s32)gcall(0x8002A0C8u, (u32)ang, R[5], R[6], R[7]);
    u32 t = (u32)(-c) * MEM_R32(sp + 0x38);
    s32 sn = (s32)gcall(0x80029FF8u, (u32)ang, R[5], R[6], R[7]);
    t -= (u32)sn * MEM_R32(sp + 0x30);
    return mips_div((s32)t, (s32)MEM_R32(sp + 0x34));
}

/* The water checks for the polygon of vertices vv (3: a triangle, 4: a
 * quad in strip order 0 1 2 3); the player tests' NCLIPs stored at dest
 * (the unit's flags word for triangles, the stack for quads); splash
 * polls at poll; pre: the textured-Gouraud drivers' first test that the
 * normal has a height component. */
static void water_check(u32 sp, u32 dest, const u32 *vv, int nv, u32 poll, int pre) {
    static const s8 ptri[3][3] = { { 0, 1, -1 }, { 0, -1, 2 }, { -1, 1, 2 } };
    static const s8 pquad[4][3] = { { 0, 1, -1 }, { 0, -1, 2 }, { -1, 3, 2 }, { 1, 3, -1 } };
    static const u8 etri[3][2] = { { 0, 1 }, { 1, 2 }, { 2, 0 } };
    static const u8 equad[4][2] = { { 0, 1 }, { 1, 3 }, { 3, 2 }, { 2, 0 } };
    const u32 v0 = vv[0], v1 = vv[1], v2 = vv[2];
    if ((s16)GV(u16, 0x800A8674u) != 0 && (s16)GV(u16, 0x800AF56Au) == 0) {
        u32 P = ((u32)(s32)(s16)GV(u16, 0x800B4D6Cu) << 16) | ((u32)(s32)(s16)GV(u16, 0x800B4D68u) & 0xFFFF);
        int in = 1;
        for (int t = 0; t < nv && in; t++) {
            const s8 *x = nv == 3 ? ptri[t] : pquad[t];
            u32 a[3];
            for (int k = 0; k < 3; k++) a[k] = x[k] < 0 ? P : xz_word(vv[(int)x[k]]);
            MEM_W32(dest, (u32)xz_nclip(a[0], a[1], a[2]));
            if ((s32)MEM_R32(dest) > 0) in = 0;
        }
        if (in && pre) {
            for (u32 k = 0; k < 3; k++) {
                MEM_W32(sp + 0x10 + k * 4, (u32)((s32)(s16)MEM_R16(v1 + k * 2) - (s16)MEM_R16(v0 + k * 2)));
                MEM_W32(sp + 0x20 + k * 4, (u32)((s32)(s16)MEM_R16(v2 + k * 2) - (s16)MEM_R16(v0 + k * 2)));
            }
            gcall(0x80041FE4u, sp + 0x10, sp + 0x20, sp + 0x30, R[7]);
            if (MEM_R32(sp + 0x34) == 0) in = 0;
        }
        if (in && water_plane(sp, v0, v1, v2)) {
            s32 y = water_height(sp, v0, (s16)GV(u16, 0x800B4D68u), (s16)GV(u16, 0x800B4D6Cu));
            s32 d = y - (s16)GV(u16, 0x800B4D6Au);
            if (d < 301 && !(d < -300)) {
                s32 sl = water_slope(sp, (s16)GV(u16, 0x800B3D9Cu));
                GV(u16, 0x800A8674u) = 0;
                GV(u16, 0x800B4D6Au) = (u16)y;
                GV(u16, 0x800AF56Au) = 1;
                GV(u16, 0x800B4B9Cu) = (u16)sl;
            }
        }
    }
    if ((s16)GV(u16, 0x800AB514u) == 0) return;
    const u32 jc = sp + (nv == 3 ? 0x68 : 0x60);
    MEM_W16(jc, 0);
    for (;;) {
        const u32 ent = 0x800B3EA8u + (u32)((s32)(s16)MEM_R16(jc) * 12);
        s32 st = (s16)GV(u16, ent);
        if (!(st < 0) && st != 1) {
            s32 x = (s16)GV(u16, ent + 2), z = (s16)GV(u16, ent + 6);
            int out = 0;
            for (int k = 0; k < nv && !out; k++) {
                const u8 *ed = nv == 3 ? etri[k] : equad[k];
                u32 b = vv[ed[0]], c = vv[ed[1]];
                MEM_W32(sp + 0x10, (u32)(x - (s16)MEM_R16(b)));
                MEM_W32(sp + 0x14, (u32)-(s32)(s16)MEM_R16(b + 2));
                MEM_W32(sp + 0x18, (u32)(z - (s16)MEM_R16(b + 4)));
                MEM_W32(sp + 0x20, (u32)((s32)(s16)MEM_R16(c) - (s16)MEM_R16(b)));
                MEM_W32(sp + 0x24, (u32)-(s32)(s16)MEM_R16(b + 2));
                MEM_W32(sp + 0x28, (u32)((s32)(s16)MEM_R16(c + 4) - (s16)MEM_R16(b + 4)));
                gcall(0x80041FE4u, sp + 0x10, sp + 0x20, sp + 0x30, R[7]);
                if ((s32)MEM_R32(sp + 0x34) > 0) out = 1;
            }
            if (!out && water_plane(sp, v0, v1, v2)) {
                s32 y = water_height(sp, v0, x, z);
                s32 ang = (s16)GV(u16, ent + 8);
                s32 sl = water_slope(sp, ang);
                u32 a = gcall(0x8002BB48u, (u32)sl, 0x1000, R[6], R[7]);
                GV(u16, ent) = 1;
                GV(u16, ent + 4) = (u16)y;
                GV(u16, ent + 0xA) = (u16)sl;
                GV(u16, ent + 8) = (u16)a;
            }
        }
        u32 j = (u32)(MEM_R16(jc) + 1);
        MEM_W16(jc, (u16)j);
        if (!((s32)(s16)j < 5)) break;
        LOOP_POLL(poll);
    }
}

typedef struct {
    u32 stride, idx, e, copy;   /* record size, its vertex numbers, the
                                   field pointer, the copy area in the unit */
    u8 b3b, b3f;                /* the template's tag bytes */
    int gt;                     /* POLY_GT3 (colours per vertex) */
    u32 poll_splash, poll_direct, poll_loop;
    u32 offscreen, divide;
} WaterTri;

/* The water-aware triangle drivers: 80048240 / 80048BE8's records and
 * packets with the water checks first. */
static void water_tri_driver(const WaterTri *k) {
    FRAME(0xA8);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0xA4, R[31]);
    MEM_W32(sp + 0xA0, sfp);
    for (int i = 7; i >= 0; i--) MEM_W32(sp + 0x80 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    MEM_W32(sp + 0x50, c);
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x60, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 w = c + 0x20;
    MEM_W32(w, GV(u32, 0x80062F50u));
    const u32 verts = MEM_R32(c + 0x18);
    R[23] = verts;
    MEM_W16(w + 0x30, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x32, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    MEM_W32(sp + 0x5C, 0);
    R[30] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[30]);
    MEM_W32(sp + 0x40, MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2));
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3B, k->b3b);
    MEM_W8(w + 0x3F, k->b3f);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    {
        const u32 rec0 = MEM_R32(sp + 0x40);
        MEM_W32(sp + 0x44, MEM_R16(rec0 + k->idx));
        MEM_W32(sp + 0x48, MEM_R16(rec0 + k->idx + 2));
        MEM_W32(sp + 0x58, c + k->copy);
        MEM_W32(sp + 0x54, c + k->copy);
        MEM_W32(sp + 0x4C, MEM_R16(rec0 + k->idx + 4));
    }
    if (n != 0) {
        MEM_W32(sp + 0x70, c + 0x44);
        u32 e = MEM_R32(sp + 0x40) + k->e;
        for (;;) {
            u32 i0 = MEM_R32(sp + 0x44), i1 = MEM_R32(sp + 0x48), i2 = MEM_R32(sp + 0x4C);
            {
                const u32 vv[3] = { WV(i0), WV(i1), WV(i2) };
                water_check(sp, w + 0x24, vv, 3, k->poll_splash, k->gt);
            }
            u32 a0 = WV(i0), a1 = WV(i1), a2 = WV(i2);
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            R[16] = i0; R[17] = i1; R[18] = i2;
            MEM_W32(sp + 0x44, MEM_R16(e + k->stride + 4));
            MEM_W32(sp + 0x48, MEM_R16(e + k->stride + 6));
            MEM_W32(sp + 0x4C, MEM_R16(e + k->stride + 8));
            MEM_W32(MEM_R32(sp + 0x70), gte_read_ctrl(31));
            gte_command(0x1400006u);
            const u32 pk = R[30];
            MEM_W32(pk + 0xC, MEM_R32(e - 6));
            gte_swc2(w + 0x18, 24);
            if ((s32)MEM_R32(w + 0x18) > 0) {
                const u32 vs = k->gt ? 0xC : 8;
                if (k->gt) MEM_W32(pk + 0x18, MEM_R32(e - 2));
                MEM_W32(pk + 8 + 2 * vs + 4, MEM_R32(e + 2));
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 8 + vs, 13);
                gte_swc2(pk + 8 + 2 * vs, 14);
                gte_command(0x158002Du);
                MEM_W32(pk + 4, MEM_R32(MEM_R32(sp + 0x40)));
                gte_swc2(w + 0x18, 7);
                if (k->gt) {
                    MEM_W32(pk + 0x10, MEM_R32(e - 0xE));
                    MEM_W32(pk + 0x1C, MEM_R32(e - 0xA));
                } else MEM_W32(pk + 0x14, MEM_R32(e - 2));
                u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                MEM_W32(w + 0x34, slot);
                int direct = 0;
                if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(w + 0x24) >= 0)
                    direct = 1;
                else if (gcall(k->offscreen, pk, w, R[6], R[7]) == 0) {
                    if ((s32)MEM_R32(w + 0x24) >= 0
                        && (s32)(s16)MEM_R16(w + 0x2C) - (s16)MEM_R16(w + 0x28) < (s16)MEM_R16(w + 0x20)
                        && (s32)(s16)MEM_R16(w + 0x2E) - (s16)MEM_R16(w + 0x2A) < (s16)MEM_R16(w + 0x22)) {
                        LOOP_POLL(k->poll_direct);
                        direct = 1;
                    } else {
                        const u32 q = MEM_R32(sp + 0x54);
                        if (!k->gt) MEM_W32(w + 0x3C, MEM_R32(pk + 4));
                        copy8(q, WV(R[16]));
                        copy8(q + vs, WV(R[17]));
                        copy8(q + 2 * vs, WV(R[18]));
                        if (k->gt) {
                            MEM_W32(q + 8, MEM_R32(pk + 4));
                            MEM_W32(q + 0x14, MEM_R32(pk + 0x10));
                            MEM_W32(q + 0x20, MEM_R32(pk + 0x1C));
                        }
                        MEM_W16(q + 6, MEM_R16(e - 6));
                        MEM_W16(q + vs + 6, MEM_R16(e - 2));
                        MEM_W16(q + 2 * vs + 6, MEM_R16(e + 2));
                        MEM_W16(w + 0x46, MEM_R16(e - 4));
                        MEM_W32(w + 0x14, pk);
                        MEM_W16(w + (k->gt ? 0x52 : 0x4E), MEM_R16(e));
                        gcall(k->divide, MEM_R32(sp + 0x58), w, 0, R[7]);
                        R[30] = MEM_R32(w + 0x14);
                    }
                }
                if (direct) {
                    u32 sl = MEM_R32(w + 0x34);
                    MEM_W32(pk, (MEM_R32(sl) & 0xFFFFFF) | (k->gt ? 0x09000000u : 0x07000000u));
                    MEM_W32(sl, pk & 0xFFFFFF);
                    R[30] = pk + (k->gt ? 0x28 : 0x20);
                }
            }
            e += k->stride;
            u32 i = MEM_R32(sp + 0x5C) + 1;
            MEM_W32(sp + 0x5C, i);
            MEM_W32(sp + 0x40, MEM_R32(sp + 0x40) + k->stride);
            if (!((s32)i < (s32)MEM_R32(sp + 0x60))) break;
            LOOP_POLL(k->poll_loop);
        }
    }
    GV(u32, 0x80065C08u) = R[30];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 80040668 / 80042058 (unit): the water-aware FT3 and GT3 drivers. */
static const WaterTri w_ft3 = { 0x14, 0xE, 0xA, 0x78, 7, 0x24, 0, 0x800411B0u, 0x80041370u, 0x80041468u, 0x80041880u, 0x800414B4u };
static const WaterTri w_gt3 = { 0x1C, 0x16, 0x12, 0x80, 9, 0x34, 1, 0x80042C7Cu, 0x80042E54u, 0x80042F64u, 0x800433D4u, 0x80042FB0u };
void n_gs_WaterFT3(void) { water_tri_driver(&w_ft3); }
void n_gs_WaterGT3(void) { water_tri_driver(&w_gt3); }

typedef struct {
    int water;
    u32 colour_or, offscreen, divide;
    u32 poll_splash, poll_direct, poll_loop;
} QuadDrv;

static void quad_ft4_driver(const QuadDrv *k) {
    FRAME(0xB8);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0xB4, R[31]);
    MEM_W32(sp + 0xB0, sfp);
    for (int i = 7; i >= 0; i--) MEM_W32(sp + 0x90 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    R[22] = c;
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x78, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 verts = MEM_R32(c + 0x18);
    R[20] = verts;
    const u32 w = c + 0x20;
    R[16] = w;
    MEM_W32(w, GV(u32, 0x80062F50u));
    MEM_W16(w + 0x34, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x36, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    R[18] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[18]);
    R[21] = MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2);
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3F, 9);
    MEM_W8(w + 0x43, 0x2C);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    R[23] = 0;
    MEM_W32(sp + 0x7C, MEM_R32(R[21] + 0xC));
    MEM_W32(sp + 0x80, MEM_R32(R[21] + 0x10));
    R[30] = MEM_R32(R[21] + 0x14);
    R[19] = c + 0x84;
    MEM_W32(sp + 0x74, c + 0x84);
    if (n != 0) {
        MEM_W32(sp + 0x84, 0x00FFFFFFu);
        R[17] = R[21] + 0xA;
#define VX(i) (verts + ((u32)(i) << 3))
        for (;;) {
            const u32 e = R[17], rec = R[21];
            u32 w0 = MEM_R32(sp + 0x7C), w1 = MEM_R32(sp + 0x80), w2 = R[30];
            u32 a0 = VX((s32)w0 >> 16), a1 = VX((s32)w1 >> 16), a2 = VX(w2 & 0xFFFF);
            if (k->water) {
                const u32 vv[4] = { a0, a1, a2, VX((s32)w2 >> 16) };
                water_check(sp, sp + 0x40, vv, 4, k->poll_splash, 0);
            }
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            MEM_W32(sp + 0x7C, MEM_R32(e + 0x1A));
            MEM_W32(sp + 0x80, MEM_R32(e + 0x1E));
            R[30] = MEM_R32(e + 0x22);
            MEM_W32(w + 0x24, gte_read_ctrl(31));
            gte_command(0x1400006u);
            if ((s32)gte_read_data(24) > 0) {
                u32 pk = R[18];
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x10, 13);
                gte_swc2(pk + 0x18, 14);
                u32 a3 = VX((s32)w2 >> 16);
                gte_write_data(0, MEM_R32(a3));
                gte_write_data(1, MEM_R32(a3 + 4));
                gte_command(0x0180001u);
                MEM_W32(pk + 4, MEM_R32(rec) | k->colour_or);
                MEM_W32(w + 0x28, gte_read_ctrl(31));
                gte_swc2(pk + 0x20, 14);
                gte_command(0x168002Eu);
                MEM_W32(pk + 0x24, w1 & 0xFFFF);
                MEM_W32(pk + 0x1C, w0 & 0xFFFF);
                MEM_W32(pk + 0xC, MEM_R32(e - 6));
                MEM_W32(pk + 0x14, MEM_R32(e - 2));
                gte_swc2(w + 0x18, 7);
                u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                MEM_W32(w + 0x38, slot);
                int direct = 0;
                if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(w + 0x28) >= 0 && !(MEM_R32(w + 0x24) & 0x80000000u))
                    direct = 1;
                else if (gcall(k->offscreen, pk, w, R[6], R[7]) == 0) {
                    if ((s32)MEM_R32(w + 0x28) >= 0 && (s32)MEM_R32(w + 0x24) >= 0
                        && (s32)(s16)MEM_R16(w + 0x30) - (s16)MEM_R16(w + 0x2C) < (s16)MEM_R16(w + 0x20)
                        && (s32)(s16)MEM_R16(w + 0x32) - (s16)MEM_R16(w + 0x2E) < (s16)MEM_R16(w + 0x22)) {
                        LOOP_POLL(k->poll_direct);
                        direct = 1;
                    } else {
                        MEM_W32(w + 0x40, MEM_R32(pk + 4));
                        const u32 q = R[19];
                        copy8(q, VX(MEM_R16(e + 4)));
                        copy8(q + 8, VX(MEM_R16(e + 8)));
                        copy8(q + 0x10, VX(MEM_R16(e + 0xA)));
                        copy8(q + 0x18, VX(MEM_R16(e + 0xC)));
                        MEM_W16(q + 6, MEM_R16(e - 6));
                        MEM_W16(q + 0xE, MEM_R16(e - 2));
                        MEM_W16(q + 0x16, MEM_R16(e + 2));
                        MEM_W16(q + 0x1E, MEM_R16(e + 6));
                        MEM_W16(w + 0x4A, MEM_R16(e - 4));
                        MEM_W32(w + 0x14, pk);
                        MEM_W16(w + 0x52, MEM_R16(e));
                        gcall(k->divide, MEM_R32(sp + 0x74), w, 0, R[7]);
                        R[18] = MEM_R32(w + 0x14);
                    }
                }
                if (direct) {
                    const u32 m = MEM_R32(sp + 0x84);
                    u32 sl = MEM_R32(w + 0x38);
                    MEM_W32(pk, (MEM_R32(sl) & m) | 0x09000000u);
                    MEM_W32(sl, pk & m);
                    R[18] = pk + 0x28;
                }
            }
            R[23]++;
            R[17] += 0x18;
            R[21] += 0x18;
            if (!((s32)R[23] < (s32)MEM_R32(sp + 0x78))) break;
            LOOP_POLL(k->poll_loop);
        }
#undef VX
    }
    GV(u32, 0x80065C08u) = R[18];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 80049664: the plain subdividing FT4 driver; 80044330: the
 * water-aware one (no semi-transparency bit; DivQuadFT4). */
static const QuadDrv q_ft4div = { 0, 0x02000000u, 0x80049F84u, 0x80049B20u, 0, 0x80049998u, 0x80049AD8u };
static const QuadDrv q_ft4water = { 1, 0, 0x80045810u, 0x8004535Cu, 0x80044F88u, 0x800451C8u, 0x80045310u };
void n_gs_DriverFT4Div(void) { quad_ft4_driver(&q_ft4div); }
void n_gs_WaterFT4(void) { quad_ft4_driver(&q_ft4water); }

static void quad_gt4_driver(const QuadDrv *k) {
    FRAME(0xB8);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0xB4, R[31]);
    MEM_W32(sp + 0xB0, sfp);
    for (int i = 7; i >= 0; i--) MEM_W32(sp + 0x90 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    R[22] = c;
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x74, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 verts = MEM_R32(c + 0x18);
    R[20] = verts;
    const u32 w = c + 0x20;
    R[16] = w;
    MEM_W32(w, GV(u32, 0x80062F50u));
    MEM_W16(w + 0x34, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x36, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    R[18] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[18]);
    R[21] = MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2);
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3F, 0xC);
    MEM_W8(w + 0x43, 0x3C);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    R[23] = 0;
    MEM_W32(sp + 0x78, MEM_R32(R[21] + 0x18));
    MEM_W32(sp + 0x7C, MEM_R32(R[21] + 0x1C));
    R[19] = c + 0x90;
    MEM_W32(sp + 0x70, c + 0x90);
    MEM_W32(sp + 0x80, MEM_R32(R[21] + 0x20));
    if (n != 0) {
        MEM_W32(sp + 0x84, 0x00FFFFFFu);
        R[17] = R[21] + 0x16;
#define VX(i) (verts + ((u32)(i) << 3))
        for (;;) {
            const u32 e = R[17], rec = R[21];
            u32 w0 = MEM_R32(sp + 0x78), w1 = MEM_R32(sp + 0x7C), w2 = MEM_R32(sp + 0x80);
            u32 a0 = VX((s32)w0 >> 16), a1 = VX((s32)w1 >> 16), a2 = VX(w2 & 0xFFFF);
            if (k->water) {
                const u32 vv[4] = { a0, a1, a2, VX((s32)w2 >> 16) };
                water_check(sp, sp + 0x40, vv, 4, k->poll_splash, 0);
            }
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            MEM_W32(sp + 0x78, MEM_R32(e + 0x26));
            MEM_W32(sp + 0x7C, MEM_R32(e + 0x2A));
            MEM_W32(sp + 0x80, MEM_R32(e + 0x2E));
            MEM_W32(w + 0x24, gte_read_ctrl(31));
            gte_command(0x1400006u);
            R[30] = gte_read_data(24);
            if ((s32)R[30] > 0) {
                u32 pk = R[18];
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x14, 13);
                gte_swc2(pk + 0x20, 14);
                u32 a3 = VX((s32)w2 >> 16);
                gte_write_data(0, MEM_R32(a3));
                gte_write_data(1, MEM_R32(a3 + 4));
                gte_command(0x0180001u);
                MEM_W32(pk + 0x24, w0 & 0xFFFF);
                MEM_W32(pk + 0xC, MEM_R32(e - 6));
                MEM_W32(pk + 0x30, w1 & 0xFFFF);
                MEM_W32(pk + 0x18, MEM_R32(e - 2));
                R[30] = MEM_R32(rec) | k->colour_or;
                MEM_W32(w + 0x28, gte_read_ctrl(31));
                gte_swc2(pk + 0x2C, 14);
                gte_command(0x168002Eu);
                MEM_W32(pk + 4, R[30]);
                MEM_W32(pk + 0x28, MEM_R32(e - 0xA));
                MEM_W32(pk + 0x10, MEM_R32(e - 0x12));
                MEM_W32(pk + 0x1C, MEM_R32(e - 0xE));
                gte_swc2(w + 0x18, 7);
                u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                MEM_W32(w + 0x38, slot);
                int direct = 0;
                if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(w + 0x28) >= 0 && !(MEM_R32(w + 0x24) & 0x80000000u))
                    direct = 1;
                else if (gcall(k->offscreen, pk, w, R[6], R[7]) == 0) {
                    if ((s32)MEM_R32(w + 0x28) >= 0 && (s32)MEM_R32(w + 0x24) >= 0
                        && (s32)(s16)MEM_R16(w + 0x30) - (s16)MEM_R16(w + 0x2C) < (s16)MEM_R16(w + 0x20)
                        && (s32)(s16)MEM_R16(w + 0x32) - (s16)MEM_R16(w + 0x2E) < (s16)MEM_R16(w + 0x22)) {
                        LOOP_POLL(k->poll_direct);
                        direct = 1;
                    } else {
                        const u32 q = R[19];
                        copy8(q, VX(MEM_R16(e + 4)));
                        copy8(q + 0xC, VX(MEM_R16(e + 8)));
                        copy8(q + 0x18, VX(MEM_R16(e + 0xA)));
                        copy8(q + 0x24, VX(MEM_R16(e + 0xC)));
                        MEM_W32(q + 8, MEM_R32(pk + 4));
                        MEM_W32(q + 0x14, MEM_R32(pk + 0x10));
                        MEM_W32(q + 0x20, MEM_R32(pk + 0x1C));
                        MEM_W32(q + 0x2C, MEM_R32(pk + 0x28));
                        MEM_W16(q + 6, MEM_R16(e - 6));
                        MEM_W16(q + 0x12, MEM_R16(e - 2));
                        MEM_W16(q + 0x1E, MEM_R16(e + 2));
                        MEM_W16(q + 0x2A, MEM_R16(e + 6));
                        MEM_W16(w + 0x4A, MEM_R16(e - 4));
                        MEM_W32(w + 0x14, pk);
                        MEM_W16(w + 0x56, MEM_R16(e));
                        gcall(k->divide, MEM_R32(sp + 0x70), w, 0, R[7]);
                        R[18] = MEM_R32(w + 0x14);
                    }
                }
                if (direct) {
                    const u32 m = MEM_R32(sp + 0x84);
                    u32 sl = MEM_R32(w + 0x38);
                    MEM_W32(pk, (MEM_R32(sl) & m) | 0x0C000000u);
                    MEM_W32(sl, pk & m);
                    R[18] = pk + 0x34;
                }
            }
            R[23]++;
            R[17] += 0x24;
            R[21] += 0x24;
            if (!((s32)R[23] < (s32)MEM_R32(sp + 0x74))) break;
            LOOP_POLL(k->poll_loop);
        }
#undef VX
    }
    GV(u32, 0x80065C08u) = R[18];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8004A1D8: the plain subdividing GT4 driver; 80046880: the
 * water-aware one (no semi-transparency bit; DivQuadGT4). */
static const QuadDrv q_gt4div = { 0, 0x02000000u, 0x8004ABBCu, 0x8004A6ECu, 0, 0x8004A540u, 0x8004A6A4u };
static const QuadDrv q_gt4water = { 1, 0, 0x80047E4Cu, 0x8004792Cu, 0x80047508u, 0x80047774u, 0x800478E0u };
void n_gs_DriverGT4Div(void) { quad_gt4_driver(&q_gt4div); }
void n_gs_WaterGT4(void) { quad_gt4_driver(&q_gt4water); }

/* 8005128C (unit): the water-aware FT4 driver of the 0x80050000 file:
 * the record's own vertex numbers each time (no fetch ahead), the
 * player tests' NCLIPs into the unit (+0x48), and the colour depth-cued
 * (DPCS) into the packet. DivOffscreenF 80052700, DivQuadFT4b. */
void n_gs_WaterFT4Fog(void) {
    FRAME(0x98);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x94, R[31]);
    MEM_W32(sp + 0x90, sfp);
    for (int i = 7; i >= 0; i--) MEM_W32(sp + 0x70 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    MEM_W32(sp + 0x44, c);
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x54, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 w = c + 0x20;
    R[21] = w;
    MEM_W32(w, GV(u32, 0x80062F50u));
    const u32 verts = MEM_R32(c + 0x18);
    R[22] = verts;
    MEM_W16(w + 0x34, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x36, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    MEM_W32(sp + 0x50, 0);
    R[30] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[30]);
    MEM_W32(sp + 0x40, MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2));
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3F, 9);
    MEM_W8(w + 0x43, 0x2C);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    MEM_W32(sp + 0x4C, c + 0x84);
    MEM_W32(sp + 0x48, c + 0x84);
    if (n != 0) {
        u32 e = MEM_R32(sp + 0x40) + 0xA;
#define VX(i) (verts + ((u32)(i) << 3))
        for (;;) {
            const u32 rec = MEM_R32(sp + 0x40);
            u32 a0 = VX(MEM_R16(e + 4)), a1 = VX(MEM_R16(e + 8)), a2 = VX(MEM_R16(e + 0xA)), a3 = VX(MEM_R16(e + 0xC));
            {
                const u32 vv[4] = { a0, a1, a2, a3 };
                water_check(sp, w + 0x28, vv, 4, 0x80051EA8u, 0);
            }
            a0 = VX(MEM_R16(e + 4)); a1 = VX(MEM_R16(e + 8)); a2 = VX(MEM_R16(e + 0xA));
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            const u32 pk = R[30];
            MEM_W32(pk + 0xC, MEM_R32(e - 6));
            MEM_W32(pk + 0x1C, MEM_R32(e + 2));
            MEM_W32(w + 0x24, gte_read_ctrl(31));
            gte_command(0x1400006u);
            MEM_W32(pk + 0x24, MEM_R32(e + 6));
            gte_swc2(w + 0x18, 24);
            if ((s32)MEM_R32(w + 0x18) > 0) {
                MEM_W32(pk + 0x14, MEM_R32(e - 2));
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x10, 13);
                gte_swc2(pk + 0x18, 14);
                u32 a3b = VX(MEM_R16(e + 0xC));
                gte_write_data(0, MEM_R32(a3b));
                gte_write_data(1, MEM_R32(a3b + 4));
                gte_command(0x0180001u);
                MEM_W32(w + 0x28, gte_read_ctrl(31));
                gte_swc2(pk + 0x20, 14);
                gte_command(0x168002Eu);
                gte_write_data(6, MEM_R32(rec));
                gte_command(0x0780010u);
                gte_swc2(pk + 4, 22);
                gte_swc2(w + 0x18, 7);
                u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                MEM_W32(w + 0x38, slot);
                int direct = 0;
                if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(w + 0x28) >= 0 && !(MEM_R32(w + 0x24) & 0x80000000u))
                    direct = 1;
                else if (gcall(0x80052700u, pk, w, R[6], R[7]) == 0) {
                    if ((s32)MEM_R32(w + 0x28) >= 0 && (s32)MEM_R32(w + 0x24) >= 0
                        && (s32)(s16)MEM_R16(w + 0x30) - (s16)MEM_R16(w + 0x2C) < (s16)MEM_R16(w + 0x20)
                        && (s32)(s16)MEM_R16(w + 0x32) - (s16)MEM_R16(w + 0x2E) < (s16)MEM_R16(w + 0x22)) {
                        LOOP_POLL(0x800520B8u);
                        direct = 1;
                    } else {
                        MEM_W32(w + 0x40, MEM_R32(pk + 4));
                        const u32 q = MEM_R32(sp + 0x48);
                        copy8(q, VX(MEM_R16(e + 4)));
                        copy8(q + 8, VX(MEM_R16(e + 8)));
                        copy8(q + 0x10, VX(MEM_R16(e + 0xA)));
                        copy8(q + 0x18, VX(MEM_R16(e + 0xC)));
                        MEM_W16(q + 6, MEM_R16(e - 6));
                        MEM_W16(q + 0xE, MEM_R16(e - 2));
                        MEM_W16(q + 0x16, MEM_R16(e + 2));
                        MEM_W16(q + 0x1E, MEM_R16(e + 6));
                        MEM_W16(w + 0x4A, MEM_R16(e - 4));
                        MEM_W32(w + 0x14, pk);
                        MEM_W16(w + 0x52, MEM_R16(e));
                        gcall(0x8005224Cu, MEM_R32(sp + 0x4C), w, 0, R[7]);
                        R[30] = MEM_R32(w + 0x14);
                    }
                }
                if (direct) {
                    u32 sl = MEM_R32(w + 0x38);
                    MEM_W32(pk, (MEM_R32(sl) & 0xFFFFFF) | 0x09000000u);
                    MEM_W32(sl, pk & 0xFFFFFF);
                    R[30] = pk + 0x28;
                }
            }
            e += 0x18;
            u32 i = MEM_R32(sp + 0x50) + 1;
            MEM_W32(sp + 0x50, i);
            MEM_W32(sp + 0x40, MEM_R32(sp + 0x40) + 0x18);
            if (!((s32)i < (s32)MEM_R32(sp + 0x54))) break;
            LOOP_POLL(0x80052200u);
        }
#undef VX
    }
    GV(u32, 0x80065C08u) = R[30];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 80053978 (unit): the water-aware GT4 driver of the 0x80050000 file,
 * per-vertex depth-cued: each corner projected alone (RTPS; its flags at
 * unit+0x20.. and its depth cue IR0), back faces culled after three, the
 * colours depth-cued (DPCS) into the packet, a quad all beyond the far
 * plane dropped, the off-screen test inline (no polls); its work area at
 * unit+0x30, the copied quad at unit+0xA0 for DivQuadGT4c. */
void n_gs_WaterGT4Fog(void) {
    FRAME(0xA8);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0xA4, R[31]);
    MEM_W32(sp + 0xA0, sfp);
    for (int i = 7; i >= 0; i--) MEM_W32(sp + 0x80 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    MEM_W32(sp + 0x54, c);
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x64, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 w = c + 0x30, fl = c + 0x20;
    MEM_W32(w, GV(u32, 0x80062F50u));
    const u32 verts = MEM_R32(c + 0x18);
    MEM_W16(w + 0x34, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x36, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    MEM_W32(sp + 0x60, 0);
    R[22] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[22]);
    MEM_W32(sp + 0x40, MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2));
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3F, 0xC);
    MEM_W8(w + 0x43, 0x3C);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    MEM_W32(sp + 0x44, fl);
    MEM_W32(sp + 0x5C, c + 0xA0);
    MEM_W32(sp + 0x58, c + 0xA0);
    if (n != 0) {
        u32 e = MEM_R32(sp + 0x40) + 0x16;
#define VX(i) (verts + ((u32)(i) << 3))
        for (;;) {
            const u32 rec = MEM_R32(sp + 0x40);
            {
                const u32 vv[4] = { VX(MEM_R16(e + 4)), VX(MEM_R16(e + 8)), VX(MEM_R16(e + 0xA)), VX(MEM_R16(e + 0xC)) };
                water_check(sp, w + 0x28, vv, 4, 0x800545C8u, 0);
            }
            const u32 pk = R[22];
            const u32 w6 = MEM_R32(e + 2), w4 = MEM_R32(e + 6), w3 = MEM_R32(e + 0xA);
            u32 a = VX((s32)w6 >> 16);
            gte_write_data(0, MEM_R32(a));
            gte_write_data(1, MEM_R32(a + 4));
            gte_command(0x0180001u);
            a = VX((s32)w4 >> 16);
            gte_write_data(0, MEM_R32(a));
            gte_write_data(1, MEM_R32(a + 4));
            MEM_W32(fl, gte_read_ctrl(31));
            u32 ir0a = gte_read_data(8);
            gte_command(0x0180001u);
            a = VX(w3 & 0xFFFF);
            gte_write_data(0, MEM_R32(a));
            gte_write_data(1, MEM_R32(a + 4));
            MEM_W32(fl + 4, gte_read_ctrl(31));
            u32 ir0b = gte_read_data(8);
            gte_command(0x0180001u);
            MEM_W32(pk + 0x18, MEM_R32(e - 2));
            MEM_W32(fl + 8, gte_read_ctrl(31));
            u32 ir0c = gte_read_data(8);
            gte_command(0x1400006u);
            gte_swc2(w + 0x18, 24);
            int drawn = 0;
            if ((s32)MEM_R32(w + 0x18) > 0) {
                gte_swc2(pk + 8, 12);
                gte_write_data(8, ir0a);
                gte_write_data(6, MEM_R32(rec));
                gte_command(0x0780010u);
                gte_swc2(pk + 4, 22);
                gte_write_data(8, ir0b);
                gte_write_data(6, MEM_R32(rec + 4));
                gte_command(0x0780010u);
                gte_swc2(pk + 0x10, 22);
                gte_write_data(8, ir0c);
                gte_write_data(6, MEM_R32(rec + 8));
                gte_command(0x0780010u);
                MEM_W32(pk + 0xC, MEM_R32(e - 6));
                gte_swc2(pk + 0x1C, 22);
                a = VX((s32)w3 >> 16);
                gte_write_data(0, MEM_R32(a));
                gte_write_data(1, MEM_R32(a + 4));
                gte_command(0x0180001u);
                gte_write_data(6, MEM_R32(rec + 0xC));
                MEM_W32(fl + 0xC, gte_read_ctrl(31));
                gte_command(0x0780010u);
                MEM_W32(pk + 0x24, w6);
                MEM_W32(pk + 0x30, w4);
                gte_swc2(pk + 0x28, 22);
                if (!((MEM_R32(fl) & MEM_R32(fl + 4) & MEM_R32(fl + 8) & MEM_R32(fl + 0xC)) & 0x00040000u)) {
                    gte_command(0x168002Eu);
                    gte_swc2(pk + 0x14, 12);
                    gte_swc2(pk + 0x20, 13);
                    gte_swc2(pk + 0x2C, 14);
                    gte_swc2(w + 0x18, 7);
                    u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                    MEM_W32(w + 0x38, slot);
                    int direct = 0;
                    if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(fl) >= 0
                        && !(MEM_R32(fl + 0xC) & 0x80000000u) && !(MEM_R32(fl + 4) & 0x80000000u) && !(MEM_R32(fl + 8) & 0x80000000u))
                        direct = 1;
                    else {
                        bound(pk, w, 0, 0xC);
                        s32 hw = (s16)MEM_R16(w + 0x34);
                        if ((s16)MEM_R16(w + 0x30) < -hw || hw < (s16)MEM_R16(w + 0x2C)) goto next;
                        bound(pk, w, 2, 0xC);
                        s32 hh = (s16)MEM_R16(w + 0x36);
                        s32 yhi = (s16)MEM_R16(w + 0x32), ylo = (s16)MEM_R16(w + 0x2E);
                        if (yhi < -hh || hh < ylo) goto next;
                        if ((s32)(MEM_R32(fl) | MEM_R32(fl + 4) | MEM_R32(fl + 8) | MEM_R32(fl + 0xC)) >= 0
                            && (s32)(s16)MEM_R16(w + 0x30) - (s16)MEM_R16(w + 0x2C) < (s16)MEM_R16(w + 0x20)
                            && yhi - ylo < (s16)MEM_R16(w + 0x22)) {
                            LOOP_POLL(0x80054A90u);
                            direct = 1;
                        } else {
                            const u32 q = MEM_R32(sp + 0x58);
                            copy8(q, VX(MEM_R16(e + 4)));
                            copy8(q + 0xC, VX(MEM_R16(e + 8)));
                            copy8(q + 0x18, VX(MEM_R16(e + 0xA)));
                            copy8(q + 0x24, VX(MEM_R16(e + 0xC)));
                            MEM_W32(q + 8, MEM_R32(pk + 4));
                            MEM_W32(q + 0x14, MEM_R32(pk + 0x10));
                            MEM_W32(q + 0x20, MEM_R32(pk + 0x1C));
                            MEM_W32(q + 0x2C, MEM_R32(pk + 0x28));
                            MEM_W16(q + 6, MEM_R16(e - 6));
                            MEM_W16(q + 0x12, MEM_R16(e - 2));
                            MEM_W16(q + 0x1E, MEM_R16(e + 2));
                            MEM_W16(q + 0x2A, MEM_R16(e + 6));
                            MEM_W16(w + 0x4A, MEM_R16(e - 4));
                            MEM_W32(w + 0x14, pk);
                            MEM_W16(w + 0x56, MEM_R16(e));
                            gcall(0x80054C48u, MEM_R32(sp + 0x5C), w, 0, R[7]);
                            R[22] = MEM_R32(w + 0x14);
                        }
                    }
                    if (direct) {
                        u32 sl = MEM_R32(w + 0x38);
                        MEM_W32(pk, (MEM_R32(sl) & 0xFFFFFF) | 0x0C000000u);
                        MEM_W32(sl, pk & 0xFFFFFF);
                        R[22] = pk + 0x34;
                    }
                    drawn = 1;
                }
            }
            (void)drawn;
        next:
            e += 0x24;
            u32 i = MEM_R32(sp + 0x60) + 1;
            MEM_W32(sp + 0x60, i);
            MEM_W32(sp + 0x40, MEM_R32(sp + 0x40) + 0x24);
            if (!((s32)i < (s32)MEM_R32(sp + 0x64))) break;
            LOOP_POLL(0x80054BFCu);
        }
#undef VX
    }
    GV(u32, 0x80065C08u) = R[22];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8004D8F0 (unit): the water-aware FT3 driver of the 0x80050000 file:
 * the record's own vertex numbers each time (no fetch ahead) and the
 * colour depth-cued (DPCS) into the packet. TriOffscreen8 8004EAD4, the
 * divider 8004E708 (the copied triangle at unit+0x78). */
void n_gs_WaterFT3Fog(void) {
    FRAME(0x98);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x94, R[31]);
    MEM_W32(sp + 0x90, sfp);
    for (int i = 7; i >= 0; i--) MEM_W32(sp + 0x70 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    MEM_W32(sp + 0x44, c);
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x54, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 w = c + 0x20;
    R[20] = w;
    MEM_W32(w, GV(u32, 0x80062F50u));
    const u32 verts = MEM_R32(c + 0x18);
    R[23] = verts;
    MEM_W16(w + 0x30, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x32, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    MEM_W32(sp + 0x50, 0);
    R[30] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[30]);
    MEM_W32(sp + 0x40, MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2));
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3B, 7);
    MEM_W8(w + 0x3F, 0x24);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    MEM_W32(sp + 0x4C, c + 0x78);
    MEM_W32(sp + 0x48, c + 0x78);
    if (n != 0) {
        u32 e = MEM_R32(sp + 0x40) + 0xA;
#define VX(i) (verts + ((u32)(i) << 3))
        for (;;) {
            R[19] = e;
            const u32 rec = MEM_R32(sp + 0x40);
            {
                const u32 vv[3] = { VX(MEM_R16(e + 4)), VX(MEM_R16(e + 6)), VX(MEM_R16(e + 8)) };
                water_check(sp, w + 0x24, vv, 3, 0x8004E408u, 0);
            }
            u32 a0 = VX(MEM_R16(e + 4)), a1 = VX(MEM_R16(e + 6)), a2 = VX(MEM_R16(e + 8));
            gte_write_data(0, MEM_R32(a0));
            gte_write_data(1, MEM_R32(a0 + 4));
            gte_write_data(2, MEM_R32(a1));
            gte_write_data(3, MEM_R32(a1 + 4));
            gte_write_data(4, MEM_R32(a2));
            gte_write_data(5, MEM_R32(a2 + 4));
            gte_command(0x0280030u);
            const u32 pk = R[30];
            MEM_W32(pk + 0xC, MEM_R32(e - 6));
            MEM_W32(pk + 0x1C, MEM_R32(e + 2));
            MEM_W32(w + 0x24, gte_read_ctrl(31));
            gte_command(0x1400006u);
            gte_swc2(w + 0x18, 24);
            if ((s32)MEM_R32(w + 0x18) > 0) {
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x10, 13);
                gte_swc2(pk + 0x18, 14);
                gte_command(0x158002Du);
                gte_write_data(6, MEM_R32(rec));
                gte_command(0x0780010u);
                gte_swc2(pk + 4, 22);
                gte_swc2(w + 0x18, 7);
                MEM_W32(pk + 0x14, MEM_R32(e - 2));
                u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                MEM_W32(w + 0x34, slot);
                int direct = 0;
                if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(w + 0x24) >= 0)
                    direct = 1;
                else if (gcall(0x8004EAD4u, pk, w, R[6], R[7]) == 0) {
                    if ((s32)MEM_R32(w + 0x24) >= 0
                        && (s32)(s16)MEM_R16(w + 0x2C) - (s16)MEM_R16(w + 0x28) < (s16)MEM_R16(w + 0x20)
                        && (s32)(s16)MEM_R16(w + 0x2E) - (s16)MEM_R16(w + 0x2A) < (s16)MEM_R16(w + 0x22)) {
                        LOOP_POLL(0x8004E5B0u);
                        direct = 1;
                    } else {
                        MEM_W32(w + 0x3C, MEM_R32(pk + 4));
                        const u32 q = MEM_R32(sp + 0x48);
                        copy8(q, VX(MEM_R16(e + 4)));
                        copy8(q + 8, VX(MEM_R16(e + 6)));
                        copy8(q + 0x10, VX(MEM_R16(e + 8)));
                        MEM_W16(q + 6, MEM_R16(e - 6));
                        MEM_W16(q + 0xE, MEM_R16(e - 2));
                        MEM_W16(q + 0x16, MEM_R16(e + 2));
                        MEM_W16(w + 0x46, MEM_R16(e - 4));
                        MEM_W32(w + 0x14, pk);
                        MEM_W16(w + 0x4E, MEM_R16(e));
                        gcall(0x8004E708u, MEM_R32(sp + 0x4C), w, 0, R[7]);
                        R[30] = MEM_R32(w + 0x14);
                    }
                }
                if (direct) {
                    u32 sl = MEM_R32(w + 0x34);
                    MEM_W32(pk, (MEM_R32(sl) & 0xFFFFFF) | 0x07000000u);
                    MEM_W32(sl, pk & 0xFFFFFF);
                    R[30] = pk + 0x20;
                }
            }
            e += 0x14;
            u32 i = MEM_R32(sp + 0x50) + 1;
            MEM_W32(sp + 0x50, i);
            MEM_W32(sp + 0x40, MEM_R32(sp + 0x40) + 0x14);
            if (!((s32)i < (s32)MEM_R32(sp + 0x54))) break;
            LOOP_POLL(0x8004E6BCu);
        }
#undef VX
    }
    GV(u32, 0x80065C08u) = R[30];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8004EDEC (unit): the water-aware GT3 driver of the 0x80050000 file,
 * per-vertex depth-cued like WaterGT4Fog: each corner projected alone
 * (RTPS; its flags at unit+0x20.. and its depth cue IR0), the colours
 * depth-cued (DPCS) into the packet, a triangle all beyond the far plane
 * dropped, the off-screen test inline; its work area at unit+0x2C, the
 * copied triangle at unit+0x8C for the divider 8004FF50. */
void n_gs_WaterGT3Fog(void) {
    FRAME(0xA8);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0xA4, R[31]);
    MEM_W32(sp + 0xA0, sfp);
    for (int i = 7; i >= 0; i--) MEM_W32(sp + 0x80 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    MEM_W32(sp + 0x54, c);
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x64, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 w = c + 0x2C;
    R[19] = w;
    MEM_W32(w, GV(u32, 0x80062F50u));
    const u32 verts = MEM_R32(c + 0x18);
    R[23] = verts;
    MEM_W16(w + 0x30, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x32, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    MEM_W32(sp + 0x60, 0);
    R[30] = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, R[30]);
    MEM_W32(sp + 0x40, MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2));
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3B, 9);
    MEM_W8(w + 0x3F, 0x34);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    const u32 fl = c + 0x20;                    /* the corners' GTE flags */
    MEM_W32(sp + 0x44, fl);
    MEM_W32(sp + 0x5C, c + 0x8C);
    MEM_W32(sp + 0x58, c + 0x8C);
    if (n != 0) {
#define VX(i) (verts + ((u32)(i) << 3))
        for (;;) {
            const u32 rec = MEM_R32(sp + 0x40);
            R[20] = rec + 0x32;
            {
                const u32 vv[3] = { VX(MEM_R16(rec + 0x16)), VX(MEM_R16(rec + 0x18)), VX(MEM_R16(rec + 0x1A)) };
                water_check(sp, w + 0x24, vv, 3, 0x8004F9E4u, 1);
            }
            u32 v = VX(MEM_R16(rec + 0x16));
            gte_write_data(0, MEM_R32(v));
            gte_write_data(1, MEM_R32(v + 4));
            gte_command(0x0180001u);
            v = VX(MEM_R16(rec + 0x18));
            gte_write_data(0, MEM_R32(v));
            gte_write_data(1, MEM_R32(v + 4));
            MEM_W32(fl, gte_read_ctrl(31));
            u32 ir0a = gte_read_data(8);
            gte_command(0x0180001u);
            v = VX(MEM_R16(rec + 0x1A));
            gte_write_data(0, MEM_R32(v));
            gte_write_data(1, MEM_R32(v + 4));
            MEM_W32(fl + 4, gte_read_ctrl(31));
            u32 ir0b = gte_read_data(8);
            gte_command(0x0180001u);
            MEM_W32(fl + 8, gte_read_ctrl(31));
            u32 ir0c = gte_read_data(8);
            gte_command(0x1400006u);
            gte_swc2(w + 0x18, 24);
            const u32 pk = R[30];
            if ((s32)MEM_R32(w + 0x18) > 0) {
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0x14, 13);
                gte_swc2(pk + 0x20, 14);
                gte_write_data(8, ir0a);
                gte_write_data(6, MEM_R32(rec));
                gte_command(0x0780010u);
                MEM_W32(pk + 0x24, MEM_R32(rec + 0x14));
                gte_swc2(pk + 4, 22);
                gte_write_data(8, ir0b);
                gte_write_data(6, MEM_R32(rec + 4));
                gte_command(0x0780010u);
                MEM_W32(pk + 0xC, MEM_R32(rec + 0xC));
                gte_swc2(pk + 0x10, 22);
                gte_write_data(8, ir0c);
                gte_write_data(6, MEM_R32(rec + 8));
                gte_command(0x0780010u);
                MEM_W32(pk + 0x18, MEM_R32(rec + 0x10));
                gte_swc2(pk + 0x1C, 22);
                if (!(MEM_R32(fl) & MEM_R32(fl + 4) & MEM_R32(fl + 8) & 0x40000u)) {
                    gte_command(0x158002Du);
                    gte_swc2(w + 0x18, 7);
                    u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                    MEM_W32(w + 0x34, slot);
                    int direct = 0, draw = 1;
                    if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(fl) >= 0
                        && !(MEM_R32(fl + 4) & 0x80000000u) && !(MEM_R32(fl + 8) & 0x80000000u))
                        direct = 1;
                    else {
                        /* the screen extent: x into +0x28/+0x2C, y +0x2A/+0x2E */
                        for (int ax = 0; ax < 2 && draw; ax++) {
                            const u32 lo = w + 0x28 + ax * 2, hi = w + 0x2C + ax * 2;
                            u16 a = MEM_R16(pk + 8 + ax * 2), b = MEM_R16(pk + 0x14 + ax * 2), d = MEM_R16(pk + 0x20 + ax * 2);
                            if ((s16)b < (s16)a) { MEM_W16(hi, a); MEM_W16(lo, b); }
                            else { MEM_W16(lo, a); MEM_W16(hi, b); }
                            if ((s16)MEM_R16(hi) < (s16)d) MEM_W16(hi, d);
                            else if ((s16)d < (s16)MEM_R16(lo)) MEM_W16(lo, d);
                            s32 lim = (s16)MEM_R16(w + 0x30 + ax * 2);
                            if ((s16)MEM_R16(hi) < -lim || lim < (s16)MEM_R16(lo)) draw = 0;
                        }
                        if (draw) {
                            if ((s32)MEM_R32(fl) >= 0 && (s32)MEM_R32(fl + 4) >= 0 && (s32)MEM_R32(fl + 8) >= 0
                                && (s32)(s16)MEM_R16(w + 0x2C) - (s16)MEM_R16(w + 0x28) < (s16)MEM_R16(w + 0x20)
                                && (s32)(s16)MEM_R16(w + 0x2E) - (s16)MEM_R16(w + 0x2A) < (s16)MEM_R16(w + 0x22)) {
                                LOOP_POLL(0x8004FDC8u);
                                direct = 1;
                            } else {
                                const u32 q = MEM_R32(sp + 0x58);
                                copy8(q, VX(MEM_R16(rec + 0x16)));
                                copy8(q + 0xC, VX(MEM_R16(rec + 0x18)));
                                copy8(q + 0x18, VX(MEM_R16(rec + 0x1A)));
                                MEM_W32(q + 8, MEM_R32(pk + 4));
                                MEM_W32(q + 0x14, MEM_R32(pk + 0x10));
                                MEM_W32(q + 0x20, MEM_R32(pk + 0x1C));
                                MEM_W16(q + 6, MEM_R16(rec + 0xC));
                                MEM_W16(q + 0x12, MEM_R16(rec + 0x10));
                                MEM_W16(q + 0x1E, MEM_R16(rec + 0x14));
                                MEM_W16(w + 0x46, MEM_R16(rec + 0xE));
                                MEM_W32(w + 0x14, pk);
                                MEM_W16(w + 0x52, MEM_R16(rec + 0x12));
                                gcall(0x8004FF50u, MEM_R32(sp + 0x5C), w, 0, R[7]);
                                v = VX(MEM_R16(rec + 0x32));
                                gte_write_data(0, MEM_R32(v));
                                gte_write_data(1, MEM_R32(v + 4));
                                R[30] = MEM_R32(w + 0x14);
                            }
                        }
                    }
                    if (direct) {
                        u32 sl = MEM_R32(w + 0x34);
                        MEM_W32(pk, (MEM_R32(sl) & 0xFFFFFF) | 0x09000000u);
                        MEM_W32(sl, pk & 0xFFFFFF);
                        R[30] = pk + 0x28;
                    }
                }
            }
            u32 i = MEM_R32(sp + 0x60) + 1;
            MEM_W32(sp + 0x60, i);
            MEM_W32(sp + 0x40, rec + 0x1C);
            if (!((s32)i < (s32)MEM_R32(sp + 0x64))) break;
            LOOP_POLL(0x8004FF04u);
        }
#undef VX
    }
    GV(u32, 0x80065C08u) = R[30];
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 8003E14C (args): lit, flat-shaded, textured triangles (POLY_FT3, 8
 * words), one normal. Each 0x14-byte polygon: words 0-2 (CLUT/UV,
 * tpage/UV, UV) copied to the packet, then (v0 << 16 | normal) and
 * (v2 << 16 | v1). Back faces (and ones the GTE flags) are skipped; the
 * rest sorted by average depth. */
void n_gs_DriverFT3Lit(void) {
    FRAME(0x18);
    save_s_regs(sp, 5);
    u32 args = A0;
    u32 hdr = MEM_R32(args);
    u32 pk = MEM_R32(args + 0x10);
    u32 verts = MEM_R32(args + 0x18);
    u32 norms = MEM_R32(args + 0x1C);
    u32 n = MEM_R16(hdr + 2);
    MEM_W32(args, hdr + 4);
    u32 poly = MEM_R32(args + 0x14) + ((MEM_R32(hdr + 4) & 0xFFFFFFu) << 2);
    gte_write_data(6, 0x24808080u);
    u32 w0 = MEM_R32(poly + 0xC), w1 = MEM_R32(poly + 0x10);
    u32 ot = MEM_R32(MEM_R32(args + 4) + 4);
    u32 zoff = MEM_R32(args + 0xC), shift = MEM_R32(args + 8);
    for (u32 i = 0; i < n;) {
        gte_vec(0, verts + ((u32)((s32)w0 >> 16) << 3));
        gte_vec(1, verts + ((w1 & 0xFFFF) << 3));
        gte_vec(2, verts + ((u32)((s32)w1 >> 16) << 3));
        gte_command(GTE_RTPT);
        u32 nm = w0 & 0xFFFF;
        w1 = MEM_R32(poly + 0x24);                 /* the next polygon's */
        w0 = MEM_R32(poly + 0x20);
        s32 flag = (s32)gte_read_ctrl(31);
        gte_command(GTE_NCLIP);
        if ((s32)gte_read_data(24) > 0 && flag >= 0) {
            MEM_W32(pk + 0xC, MEM_R32(poly));
            gte_command(GTE_AVSZ3);
            gte_vec(0, norms + (nm << 3));
            MEM_W32(pk + 0x14, MEM_R32(poly + 4));
            u32 z = gte_read_data(7);
            MEM_W32(pk + 0x1C, MEM_R32(poly + 8));
            gte_command(GTE_NCCS);
            z = (u32)((s32)(z - zoff) >> (shift & 31));
            u32 tag = ot + (z << 2);
            MEM_W32(pk, (MEM_R32(tag) & 0xFFFFFFu) | 0x07000000u);
            gte_swc2(pk + 8, 12);
            gte_swc2(pk + 0x10, 13);
            gte_swc2(pk + 0x18, 14);
            gte_swc2(pk + 4, 22);
            MEM_W32(tag, pk & 0xFFFFFFu);
            pk += 0x20;
        }
        i++;
        poly += 0x14;
        if (!(i < n)) break;
        LOOP_POLL(0x8003E300u);
    }
    g_packetArea = pk;
    V0 = MEM_R32(args) + 4;
}

/* 80043758 (unit): the subdividing flat quad driver (POLY_F4, 5 words).
 * 0xC-byte records: the colour word, then the four vertex numbers
 * (fetched a record ahead). Back faces skipped; near or off-screen
 * quads tested (DivOffscreenF4) and divided (80043BCC, the copied quad
 * at unit+0x74), the rest sorted by average depth. */
void n_gs_DriverF4Div(void) {
    FRAME(0x68);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x64, R[31]);
    MEM_W32(sp + 0x60, sfp);
    for (int i = 7; i >= 0; i--) MEM_W32(sp + 0x40 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    u32 blk = MEM_R32(c);
    u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x30, n);
    MEM_W32(c, blk + 4);
    u32 first = MEM_R32(blk + 4);
    const u32 verts = MEM_R32(c + 0x18);
    const u32 w = c + 0x20;
    MEM_W32(w, GV(u32, 0x80062F50u));
    MEM_W16(w + 0x34, (u16)((s32)GV(u32, 0x80065B18u) / 2));
    MEM_W16(w + 0x36, (u16)((s32)GV(u32, 0x80065B1Cu) / 2));
    MEM_W16(w + 0x22, GV(u16, 0x80060D42u));
    MEM_W32(w + 0x1C, GV(u32, 0x80060D3Cu));
    MEM_W16(w + 0x20, GV(u16, 0x80060D40u));
    u32 pk = MEM_R32(c + 0x10);
    MEM_W32(w + 0x14, pk);
    u32 rec = MEM_R32(c + 0x14) + ((first & 0x00FFFFFFu) << 2);
    MEM_W32(w + 0x10, MEM_R32(MEM_R32(c + 4) + 4));
    MEM_W8(w + 0x3F, 5);
    MEM_W8(w + 0x43, 0x28);
    MEM_W32(w + 0xC, MEM_R32(c + 8));
    u32 i0 = MEM_R16(rec + 4), i1 = MEM_R16(rec + 6), i2 = MEM_R16(rec + 8), i3 = MEM_R16(rec + 0xA);
    const u32 q = c + 0x74;
    MEM_W32(sp + 0x2C, q);
#define VX(i) (verts + ((u32)(i) << 3))
    if (n != 0) {
        for (u32 i = 0;;) {
            gte_vec(0, VX(i0));
            gte_vec(1, VX(i1));
            gte_vec(2, VX(i2));
            gte_command(GTE_RTPT);
            const u32 c0 = i0, c1 = i1, c2 = i2, c3 = i3;
            i0 = MEM_R16(rec + 0x10); i1 = MEM_R16(rec + 0x12);
            i2 = MEM_R16(rec + 0x14); i3 = MEM_R16(rec + 0x16);
            MEM_W32(w + 0x24, gte_read_ctrl(31));
            gte_command(GTE_NCLIP);
            gte_swc2(w + 0x18, 24);
            if ((s32)MEM_R32(w + 0x18) > 0) {
                gte_swc2(pk + 8, 12);
                gte_swc2(pk + 0xC, 13);
                gte_swc2(pk + 0x10, 14);
                gte_vec(0, VX(c3));
                gte_command(GTE_RTPS);
                MEM_W32(w + 0x28, gte_read_ctrl(31));
                gte_swc2(pk + 0x14, 14);
                gte_command(GTE_AVSZ4);
                MEM_W32(pk + 4, MEM_R32(rec));
                gte_swc2(w + 0x18, 7);
                u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
                MEM_W32(w + 0x38, slot);
                int direct = 0;
                if (!((s32)MEM_R32(w + 0x18) < (s32)MEM_R32(w + 0x1C)) && (s32)MEM_R32(w + 0x28) >= 0
                    && !(MEM_R32(w + 0x24) & 0x80000000u))
                    direct = 1;
                else if (gcall(0x80044010u, pk, w, R[6], R[7]) == 0) {
                    if ((s32)MEM_R32(w + 0x28) >= 0 && (s32)MEM_R32(w + 0x24) >= 0
                        && (s32)(s16)MEM_R16(w + 0x30) - (s16)MEM_R16(w + 0x2C) < (s16)MEM_R16(w + 0x20)
                        && (s32)(s16)MEM_R16(w + 0x32) - (s16)MEM_R16(w + 0x2E) < (s16)MEM_R16(w + 0x22)) {
                        LOOP_POLL(0x80043A98u);
                        direct = 1;
                    } else {
                        MEM_W32(w + 0x40, MEM_R32(rec));
                        copy8(q, VX(c0));
                        copy8(q + 8, VX(c1));
                        copy8(q + 0x10, VX(c2));
                        copy8(q + 0x18, VX(c3));
                        MEM_W32(w + 0x14, pk);
                        gcall(0x80043BCCu, q, w, 0, R[7]);
                        pk = MEM_R32(w + 0x14);
                    }
                }
                if (direct) {
                    u32 sl = MEM_R32(w + 0x38);
                    MEM_W32(pk, (MEM_R32(sl) & 0xFFFFFFu) | 0x05000000u);
                    MEM_W32(sl, pk & 0xFFFFFFu);
                    pk += 0x18;
                }
            }
            i++;
            rec += 0xC;
            if (!((s32)i < (s32)MEM_R32(sp + 0x30))) break;
            LOOP_POLL(0x80043B84u);
        }
    }
#undef VX
    g_packetArea = pk;
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 800466BC (v0, v1, v2, work): DivQuadG4's edge triangle: a POLY_G3
 * (7 words; colours from the 0xC-byte vertices' +8) at the work area's
 * packet pointer (+0x14), dropped if the GTE flags it or it has no area,
 * else sorted by average depth (the OT entry kept at +0x38). */
void n_gs_TriG3Packet(void) {
    const u32 v0 = A0, v1 = A1, v2 = A2, w = A3;
    gte_vec(0, v0);
    gte_vec(1, v1);
    gte_vec(2, v2);
    gte_command(GTE_RTPT);
    const u32 pk = MEM_R32(w + 0x14);
    MEM_W32(pk + 4, MEM_R32(v0 + 8));
    MEM_W8(pk + 3, 6);
    MEM_W8(pk + 7, 0x30);
    MEM_W32(w + 0x28, gte_read_ctrl(31));
    V0 = MEM_R32(w + 0x28);
    if ((s32)V0 < 0) return;
    gte_command(GTE_NCLIP);
    if (gte_read_data(24) == 0) return;
    gte_command(GTE_AVSZ3);
    gte_swc2(pk + 8, 12);
    gte_swc2(pk + 0x10, 13);
    gte_swc2(pk + 0x18, 14);
    gte_swc2(w + 0x18, 7);
    MEM_W32(pk + 0xC, MEM_R32(v1 + 8));
    MEM_W32(pk + 0x14, MEM_R32(v2 + 8));
    u32 slot = MEM_R32(w + 0x10) + ((u32)((s32)MEM_R32(w + 0x18) >> (MEM_R32(w + 0xC) & 31)) << 2);
    MEM_W32(w + 0x38, slot);
    MEM_W32(pk, (MEM_R32(slot) & 0xFFFFFFu) | 0x06000000u);
    MEM_W32(MEM_R32(w + 0x38), pk & 0xFFFFFFu);
    V0 = MEM_R32(w + 0x14) + 0x1C;
    MEM_W32(w + 0x14, V0);
}

/* 8002C830 (w, h): libgs's 3D set-up for a w x h screen (0x80065B18 /
 * 1C): the identity matrix (0x80065B90) copied to 0x80065BD0 (its +8
 * then (h << 14) / w / 3) and as the light matrix (0x80065B30, diagonal
 * cleared) and its copy (0x80065B50); the clip window cleared; two
 * work packets' lengths and codes (0x800659D8, 0x800659E8); the screen
 * size into 0x80065A8C. */
void n_gs_Init3D(void) {
    GV(u32, 0x80065B18u) = A0 & 0xFFFF;
    GV(u32, 0x80065B1Cu) = A1 & 0xFFFF;
    s32 q = mips_div((s32)(GV(u32, 0x80065B1Cu) << 14), (s32)GV(u32, 0x80065B18u));
    const u32 m = 0x80065B90u;
    for (u32 k = 0; k < 0x10; k += 2) MEM_W16(m + k, 0);
    MEM_W32(m + 0x1C, 0); MEM_W32(m + 0x18, 0); MEM_W32(m + 0x14, 0);
    MEM_W16(m, 0x1000); MEM_W16(m + 8, 0x1000); MEM_W16(m + 0x10, 0x1000);
    for (u32 k = 0; k < 0x20; k += 4) GV(u32, 0x80065BD0u + k) = MEM_R32(m + k);
    for (u32 k = 0; k < 0x20; k += 4) GV(u32, 0x80065B30u + k) = MEM_R32(m + k);
    GV(u16, 0x80065B40u) = 0; GV(u16, 0x80065B38u) = 0; GV(u16, 0x80065B30u) = 0;
    for (u32 k = 0; k < 0x20; k += 4) GV(u32, 0x80065B50u + k) = GV(u32, 0x80065B30u + k);
    for (u32 k = 0; k < 0xC; k += 2) GV(u16, 0x80065A00u + k) = 0;
    GV(u16, 0x80065A8Au) = 0;
    GV(u16, 0x80065BD8u) = (u16)(q / 3);
    GV(u16, 0x80065A88u) = 0;
    GV(u8, 0x800659DBu) = 3; GV(u8, 0x800659DFu) = 2;
    GV(u8, 0x800659EBu) = 3; GV(u8, 0x800659EFu) = 2;
    GV(u32, 0x80065A90u) = 1;
    GV(u16, 0x80065A8Cu) = GV(u16, 0x80065B18u);
    GV(u16, 0x80065A8Eu) = GV(u16, 0x80065B1Cu);
    V0 = 1;
}

/* 8002C6AC (w, h, mode, dither, vram): GsInitGraph: ResetGraph (3 if
 * the mode's bits 4-5 are 3, else 0); libgs's draw environment
 * (0x80065A10: offsets, texture window, tpage cleared, dither, no
 * clearing) put; its display environment (0x80065A70: 0, 0, w, h, PAL
 * screen y 24 / wide, interlace = mode bit 0, 24-bit = vram) put; mode
 * bit 2 into 0x80065A96. */
void n_gs_InitGraph(void) {
    FRAME(0x30);
    u32 sv[6];
    for (int i = 0; i < 6; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x14, sv[1]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x24, sv[5]);
    MEM_W32(sp + 0x28, R[31]);
    const u32 w = A0, h = A1, mode = A2, dith = A3;
    const u32 vram = MEM_R16(sp + 0x40);
    R[17] = w; R[19] = h; R[20] = mode; R[18] = mode; R[16] = dith; R[21] = vram;
    gcall(0x80034FF8u, ((mode >> 4) & 3) == 3 ? 3 : 0, R[5], R[6], R[7]);
    const u32 de = 0x80065A10u;
    for (u32 k = 8; k < 0x16; k += 2) MEM_W16(de + k, 0);
    MEM_W8(de + 0x16, (u8)dith);
    MEM_W8(de + 0x17, 0);
    MEM_W8(de + 0x18, 0);
    gcall(0x800359D8u, de, R[5], R[6], R[7]);
    const u32 di = 0x80065A70u;
    R[16] = di;
    MEM_W16(di, 0);
    MEM_W16(di + 2, 0);
    MEM_W16(di + 4, (u16)w);
    MEM_W16(di + 6, (u16)h);
    for (u32 k = 8; k < 0x10; k += 2) MEM_W16(di + k, 0);
    u32 vm = gcall(0x8002629Cu, R[4], R[5], R[6], R[7]);
    if (vm == 1) {
        MEM_W16(di + 0xA, 0x18);
        MEM_W8(di + 0x12, 1);
    }
    MEM_W8(di + 0x10, (u8)(mode & 1));
    GV(u16, 0x80065A96u) = (u16)(mode & 4);
    MEM_W8(di + 0x11, (u8)vram);
    gcall(0x80035BA4u, di, R[5], R[6], R[7]);
    for (int i = 0; i < 6; i++) R[16 + i] = sv[i];
}

/* 8002E048 (tim, info): the TIM header read into a GsIMAGE: the flags
 * (+0), the CLUT's rect (+0x10) and data (+0x18) if bit 3, the image's
 * rect (+4) and pixels (+0xC). */
void n_gs_GetTimInfo(void) {
    const u32 t = A0, info = A1;
    const u32 f = MEM_R32(t);
    MEM_W32(info, f);
    u32 q;
    if ((f >> 3) & 1) {
        MEM_W16(info + 0x10, MEM_R16(t + 8));
        MEM_W16(info + 0x12, MEM_R16(t + 0xA));
        MEM_W16(info + 0x14, MEM_R16(t + 0xC));
        MEM_W32(info + 0x18, t + 0x10);
        MEM_W16(info + 0x16, MEM_R16(t + 0xE));
        q = t + 4 + ((MEM_R32(t + 4) >> 2) << 2) + 4;
        MEM_W16(info + 4, MEM_R16(q));
        MEM_W16(info + 6, MEM_R16(q + 2));
        MEM_W16(info + 8, MEM_R16(q + 4));
        MEM_W32(info + 0xC, q + 8);
        V0 = q + 8;
    } else {
        q = t + 8;
        MEM_W16(info + 4, MEM_R16(q));
        MEM_W16(info + 6, MEM_R16(q + 2));
        MEM_W16(info + 8, MEM_R16(q + 4));
        MEM_W32(info + 0xC, q + 8);
        V0 = MEM_R16(q + 6);
    }
    MEM_W16(info + 0xA, MEM_R16(q + 6));
}
