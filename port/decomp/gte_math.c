/* libgte matrix arithmetic (kernel), done on the GTE as the library does
 * it, register for register: run for every model part's coordinate
 * system every frame.
 *
 * A MATRIX is 3x3 s16 (+0..+0x11, row by row) and a translation of three
 * s32 (+0x14..+0x1C).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "libgs.h"
#include "poll.h"

void n_gte_ApplyMatrixLV(void);
void n_gte_MulMatrixA(void);
void n_gte_MulMatrixB(void);
void n_gte_CompMatrixLV(void);
void n_gte_ScaleMatrix(void);
void n_gte_RotMatrix(void);
void n_gte_RotMatrixGPF(void);
void n_gte_SetRotMatrix(void);
void n_gte_SetTransMatrix(void);
void n_gte_RotTrans(void);
void n_gte_SquareRoot0(void);
void n_gte_PushMatrix(void);
void n_gte_PopMatrix(void);
void n_gte_rsin(void);
void n_gte_sin_1(void);
void n_gte_rcos(void);
void n_gte_RotMatrixYZX(void);
void n_gte_SetLsMatrix(void);
void n_gte_SetLightMatrix(void);
void n_gte_OuterProduct0(void);
void n_gte_SetLocalLight(void);
void n_gte_CompMatrix(void);
void n_gte_RotTransPers4(void);
void n_gs_GetLws(void);
void n_gs_GetLw(void);
void n_gs_GetLs(void);
void n_gs_SetRefView2(void);
void n_gs_ClearOt(void);
void n_gte_SetBackColor(void);
void n_gs_SetAmbient(void);
void n_gs_BitWidth(void);
void n_gs_NextBlock(void);
void n_gs_DrawOt(void);
void n_gs_GetActiveBuff(void);
void n_gs_SetWorkBase(void);
void n_gte_ApplyMatrix(void);
void n_gte_TransposeMatrix(void);
void n_gs_SetClipRect(void);
void n_gs_ApplyClip(void);
void n_gs_DefDispBuff(void);
void n_gs_SortOt(void);
void n_gte_AxisMatrix(void);
void n_gs_SwapDispBuff(void);
void n_gs_MaxAbs6(void);
void n_gs_MaxAbs6At8(void);
void n_gs_Fit16At8(void);
void n_gte_ratan2(void);
void n_gs_ScanUnit(void);
void n_gs_Fit16(void);
void n_gs_RotateZDeg(void);
void n_gte_SetGeomOffset(void);
void n_gs_SetDrawOffset(void);
void n_gs_SortClear(void);
void n_gte_RotMatrixY(void);
void n_gte_RotMatrixYXZ(void);
void n_gs_LerpCoord(void);
void n_gs_SetRefViewL(void);
void n_gs_SortPacket(void);
void n_gte_RotTransPers3(void);
void n_gs_MimeBlock(void);
void n_gs_LerpCoordRT(void);
void n_gte_ReadGeomScreen(void);
void n_gs_RotTransPers4Z(void);
void n_gte_VectorNormalD(void);
void n_gte_SetFlatLight(void);
void n_gte_CordicAngle(void);
void n_gs_LerpCoordYXZ(void);
void n_gte_SetGeomScreen(void);
void n_gs_ScreenBlock(void);
void n_gs_InterpBlock(void);
void n_gs_AddScaled(void);
void n_gs_CallObj28(void);
void n_gs_CopyRuns(void);

#define GTE_MVMVA_LV_HI 0x041E012u   /* MVMVA rotation x IR, sf 0 */
#define GTE_MVMVA_LV_LO 0x049E012u   /* MVMVA rotation x IR, sf 1 */
#define GTE_MVMVA_V0    0x0486012u   /* MVMVA rotation x V0, sf 1 */

/* Load the rotation matrix (control registers 0-4) from m. */
static inline void set_rot(u32 m) {
    u32 a = MEM_R32(m), b = MEM_R32(m + 4), c = MEM_R32(m + 8), d = MEM_R32(m + 0xC), e = MEM_R32(m + 0x10);
    gte_write_ctrl(0, a);
    gte_write_ctrl(1, b);
    gte_write_ctrl(2, c);
    gte_write_ctrl(3, d);
    gte_write_ctrl(4, e);
}

/* A long's top part (x / 2^15, toward zero) and low part (the rest,
 * signed), as the library splits them. */
static inline void split(u32 x, u32 *hi, u32 *lo) {
    if ((s32)x >= 0) {
        *hi = (u32)((s32)x >> 15);
        *lo = x & 0x7FFF;
    } else {
        u32 p = 0u - x;
        *hi = 0u - (u32)((s32)p >> 15);
        *lo = 0u - (p & 0x7FFF);
    }
}

/* 8002D348 (m, v, out): out = m x v for a long (s32) vector v, exact:
 * the top and low 15 bits multiplied separately. Returns out. */
void n_gte_ApplyMatrixLV(void) {
    u32 m = A0, v = A1, out = A2;
    set_rot(m);
    u32 x = MEM_R32(v), y = MEM_R32(v + 4), z = MEM_R32(v + 8);
    u32 xh, xl, yh, yl, zh, zl;
    split(x, &xh, &xl);
    split(y, &yh, &yl);
    split(z, &zh, &zl);
    gte_write_data(9, xh);
    gte_write_data(10, yh);
    gte_write_data(11, zh);
    gte_command(GTE_MVMVA_LV_HI);
    u32 hx = gte_read_data(25), hy = gte_read_data(26), hz = gte_read_data(27);
    gte_write_data(9, xl);
    gte_write_data(10, yl);
    gte_write_data(11, zl);
    gte_command(GTE_MVMVA_LV_LO);
    hx <<= 3;
    hy <<= 3;
    hz <<= 3;
    u32 lx = gte_read_data(25), ly = gte_read_data(26), lz = gte_read_data(27);
    MEM_W32(out, lx + hx);
    MEM_W32(out + 4, ly + hy);
    MEM_W32(out + 8, lz + hz);
    V0 = out;
}

/* The product of the rotation in the GTE and matrix b (column by column),
 * written to dst (which may be b). */
static void mul_into(u32 b, u32 dst) {
    gte_write_data(0, MEM_R16(b) | (MEM_R32(b + 4) & 0xFFFF0000u));
    gte_write_data(1, MEM_R32(b + 0xC));
    gte_command(GTE_MVMVA_V0);
    u32 c1lo = MEM_R16(b + 2), c1hi = MEM_R32(b + 8) << 16;
    u32 c1z = (u32)(s32)(s16)MEM_R16(b + 0xE);
    u32 a1 = gte_read_data(9), a2 = gte_read_data(10), a3 = gte_read_data(11);
    gte_write_data(0, c1lo | c1hi);
    gte_write_data(1, c1z);
    gte_command(GTE_MVMVA_V0);
    u32 c2lo = MEM_R16(b + 4), c2hi = MEM_R32(b + 8) & 0xFFFF0000u;
    u32 c2z = MEM_R32(b + 0x10);
    u32 b1 = gte_read_data(9), b2 = gte_read_data(10), b3 = gte_read_data(11);
    gte_write_data(0, c2lo | c2hi);
    gte_write_data(1, c2z);
    gte_command(GTE_MVMVA_V0);
    MEM_W32(dst, (b1 << 16) | (a1 & 0xFFFF));
    MEM_W32(dst + 0xC, (b3 << 16) | (a3 & 0xFFFF));
    u32 c1 = gte_read_data(9), c2 = gte_read_data(10);
    MEM_W32(dst + 4, (c1 & 0xFFFF) | (a2 << 16));
    MEM_W32(dst + 8, (c2 << 16) | (b2 & 0xFFFF));
    gte_swc2(dst + 0x10, 11);
}

/* 8002D4A8 (a, b): a = a x b (rotations). Returns a. */
void n_gte_MulMatrixA(void) {
    u32 a = A0, b = A1;
    set_rot(a);
    mul_into(b, a);
    V0 = a;
}

/* 8002D5B8 (a, b): b = a x b (rotations). Returns b. */
void n_gte_MulMatrixB(void) {
    u32 a = A0, b = A1;
    set_rot(a);
    mul_into(b, b);
    V0 = b;
}

/* 8002D094 (a, b): b = a x b with translation: b's translation becomes
 * a x (b's translation) + a's translation. */
void n_gte_CompMatrixLV(void) {
    FRAME(0x30);
    u32 a = A0, b = A1;
    gcall(0x8002D348u, a, b + 0x14, sp + 0x10, A3);
    GCALL2(0x8002D5B8u, a, b);
    MEM_W32(b + 0x14, GS(u32, sp + 0x10) + MEM_R32(a + 0x14));
    MEM_W32(b + 0x18, GS(u32, sp + 0x14) + MEM_R32(a + 0x18));
    V0 = GS(u32, sp + 0x18) + MEM_R32(a + 0x1C);
    MEM_W32(b + 0x1C, V0);
}

/* 8002A638 (m, s): scale matrix m's columns by the 4.12 vector s
 * (x, y, z): m[i][j] = m[i][j] * s[j] >> 12. Returns m. */
static inline u32 scl(u32 e, u32 s) { return (u32)((s32)((u32)(s32)(s16)e * s) >> 12); }
void n_gte_ScaleMatrix(void) {
    u32 m = A0, v = A1;
    u32 sx = MEM_R32(v), sy = MEM_R32(v + 4), sz = MEM_R32(v + 8);
    u32 w = MEM_R32(m);
    MEM_W32(m, (scl(w, sx) & 0xFFFF) | (scl(w >> 16, sy) << 16));
    w = MEM_R32(m + 4);
    MEM_W32(m + 4, (scl(w, sz) & 0xFFFF) | (scl(w >> 16, sx) << 16));
    w = MEM_R32(m + 8);
    MEM_W32(m + 8, (scl(w, sy) & 0xFFFF) | (scl(w >> 16, sz) << 16));
    w = MEM_R32(m + 0xC);
    MEM_W32(m + 0xC, (scl(w, sx) & 0xFFFF) | (scl(w >> 16, sy) << 16));
    MEM_W32(m + 0x10, scl(MEM_R32(m + 0x10), sz));
    V0 = m;
}

/* The sine table's entry for angle a (4096 to a turn): sin in the low
 * half, cos in the high, with sin's sign for a negative a. */
static inline void sin_cos(s16 a, s32 *s, s32 *c) {
    u32 i = (u32)(a >= 0 ? a : -(s32)a) & 0xFFF;
    u32 w = GV(u32, 0x8005C4FCu + i * 4);
    *s = a >= 0 ? (s32)(s16)w : -(s32)(s16)w;
    *c = (s32)w >> 16;
}
static inline s32 fx(s32 a, s32 b) { return (s32)((u32)a * (u32)b) >> 12; }

/* 8002AA68 (r, m): m = the rotation matrix for angles r (x, y, z), as
 * RotMatrix makes it. Returns m. */
void n_gte_RotMatrix(void) {
    u32 r = A0, m = A1;
    s32 sx, cx, sy, cy, sz, cz;
    sin_cos((s16)MEM_R16(r), &sx, &cx);
    sin_cos((s16)MEM_R16(r + 2), &sy, &cy);
    sin_cos((s16)MEM_R16(r + 4), &sz, &cz);
    s32 nsy = -sy;
    MEM_W16(m + 4, (u16)sy);
    MEM_W16(m + 0xA, (u16)((s32)(0u - (u32)cy * (u32)sx) >> 12));
    MEM_W16(m + 0x10, (u16)fx(cy, cx));
    MEM_W16(m, (u16)fx(cz, cy));
    MEM_W16(m + 2, (u16)((s32)(0u - (u32)sz * (u32)cy) >> 12));
    s32 t = fx(cz, nsy);
    MEM_W16(m + 6, (u16)(fx(sz, cx) - fx(t, sx)));
    MEM_W16(m + 0xC, (u16)(fx(sz, sx) + fx(t, cx)));
    t = fx(sz, nsy);
    MEM_W16(m + 8, (u16)(fx(cz, cx) + fx(t, sx)));
    MEM_W16(m + 0xE, (u16)(fx(cz, sx) - fx(t, cx)));
    V0 = m;
}

/* The sine table word for angle a with sin's sign restored, as the
 * library does it with the sign mask s (0 or -1): cos high, sin low. */
static inline u32 sc_word(s32 a, u32 *sign) {
    u32 s = (u32)(a >> 31);
    u32 w = GV(u32, 0x8005C4FCu + ((((u32)a + s) ^ s) << 2 & 0x3FFC));
    *sign = s;
    return (u32)(((s32)w >> 16) << 16) | ((((w << 16) + s) ^ s) >> 16);
}
#define GTE_GPF 0x198003Du

/* 8002B128 (r, m): m = the rotation matrix for angles r (x, y, z), the
 * GTE's GPF doing the products (another of libgte's RotMatrix
 * variants). Returns m. */
void n_gte_RotMatrixGPF(void) {
    u32 r = A0, m = A1, sg;
    u32 r0 = MEM_R32(r);
    u32 wz = sc_word((s16)MEM_R16(r + 4), &sg);
    u32 wy = sc_word((s32)r0 >> 16, &sg);
    u32 wx = sc_word((s16)r0, &sg);
    u32 cx = (u32)((s32)wx >> 16);
    u32 sy = (u32)(s32)(s16)wy, sz = (u32)(s32)(s16)wz;
    u32 cy = (u32)((s32)wy >> 16), cz = (u32)((s32)wz >> 16);
    u32 sx = (u32)(s32)(s16)wx;
    gte_write_data(8, cx);
    gte_write_data(9, sy);
    gte_write_data(10, sz);
    gte_write_data(11, cz);
    gte_command(GTE_GPF);
    u32 lo = (u32)((s32)cy * (s32)cx);
    u32 a1 = gte_read_data(9), a2 = gte_read_data(10), a3 = gte_read_data(11);
    gte_write_data(8, sx);
    gte_write_data(9, sy);
    gte_write_data(10, sz);
    gte_write_data(11, cz);
    gte_command(GTE_GPF);
    MEM_W16(m + 0x10, (u16)((s32)lo >> 12));
    u32 b1 = gte_read_data(9), b2 = gte_read_data(10), b3 = gte_read_data(11);
    gte_write_data(8, cz);
    gte_write_data(9, cy);
    lo = (u32)((s32)cy * (s32)sx);
    gte_write_data(10, b1);
    gte_write_data(11, a1);
    gte_command(GTE_GPF);
    u32 c1 = gte_read_data(9), c2 = gte_read_data(10), c3 = gte_read_data(11);
    gte_write_data(8, sz);
    gte_write_data(9, cy);
    gte_write_data(10, b1);
    gte_write_data(11, a1);
    gte_command(GTE_GPF);
    MEM_W32(m + 4, ((c2 + a2) << 16) | (sy & 0xFFFF));
    u32 d1 = gte_read_data(9);
    MEM_W32(m, ((0u - d1) << 16) | (c1 & 0xFFFF));
    u32 d2 = gte_read_data(10), d3 = gte_read_data(11);
    MEM_W32(m + 8, ((0u - (u32)((s32)lo >> 12)) << 16) | ((a3 - d2) & 0xFFFF));
    MEM_W32(m + 0xC, ((b3 + d3) << 16) | ((b2 - c3) & 0xFFFF));
    V0 = m;
}

/* 8002A778 SetRotMatrix(m). */
void n_gte_SetRotMatrix(void) { set_rot(A0); }

/* 8002A7A8 SetTransMatrix(m): the translation (m + 0x14) into control
 * registers 5-7. */
void n_gte_SetTransMatrix(void) {
    u32 m = A0;
    u32 a = MEM_R32(m + 0x14), b = MEM_R32(m + 0x18), c = MEM_R32(m + 0x1C);
    gte_write_ctrl(5, a);
    gte_write_ctrl(6, b);
    gte_write_ctrl(7, c);
}

/* 8002A958 RotTrans(v, out, flag): out = rotation x v + translation (a
 * long vector, MVMVA), *flag the GTE's flags, returned too. */
void n_gte_RotTrans(void) {
    gte_write_data(0, MEM_R32(A0));
    gte_write_data(1, MEM_R32(A0 + 4));
    gte_command(0x0480012u);
    gte_swc2(A1, 25);
    gte_swc2(A1 + 4, 26);
    gte_swc2(A1 + 8, 27);
    V0 = gte_read_ctrl(31);
    MEM_W32(A2, V0);
}

/* 8002A3D8 SquareRoot0(a): the integer square root, from the table at
 * 0x8005C06C (indexed by the top bits, scaled by the count of leading
 * zeros the GTE finds). 0 for 0. */
void n_gte_SquareRoot0(void) {
    u32 a = A0;
    gte_write_data(30, a);
    u32 lz = gte_read_data(31);
    if (lz == 32) { V0 = 0; return; }
    u32 even = lz & ~1u;
    u32 sh = (u32)((s32)(31 - even) >> 1);
    s32 k = (s32)even - 24;
    u32 top = k >= 0 ? a << (k & 31) : (u32)((s32)a >> ((24 - even) & 31));
    u32 idx = (top - 64) << 1;
    u32 t = (u32)(s32)GV(s16, 0x8005C06Cu + idx);
    V0 = (t << (sh & 31)) >> 12;
}

/* The matrix stack: 0x8005C1F8 its depth in bytes (20 deep, 0x20 bytes
 * each), the matrices from 0x8005C1FC. */
#define MSTACK_SP   GV(u32, 0x8005C1F8u)

/* 8002A468 PushMatrix: save the rotation and translation. Full: print
 * an error (0x8005C47C). */
void n_gte_PushMatrix(void) {
    u32 d = MSTACK_SP;
    if (!((s32)d < 640)) {
        GV(u32, 0x8005C1ECu) = R[31];
        gcall(0x800398C8u, 0x8005C47Cu, R[5], R[6], R[7]);
        return;
    }
    u32 p = 0x8005C1FCu + d;
    for (int i = 0; i < 8; i++) MEM_W32(p + (u32)i * 4, gte_read_ctrl(i));
    MSTACK_SP = d + 0x20;
}

/* 8002A50C PopMatrix: restore them. Empty: print an error (0x8005C4AD). */
void n_gte_PopMatrix(void) {
    u32 d = MSTACK_SP;
    if (!((s32)d > 0)) {
        GV(u32, 0x8005C1ECu) = R[31];
        gcall(0x800398C8u, 0x8005C4ADu, R[5], R[6], R[7]);
        return;
    }
    d -= 0x20;
    MSTACK_SP = d;
    u32 p = 0x8005C1FCu + d;
    for (int i = 0; i < 8; i++) gte_write_ctrl(i, MEM_R32(p + (u32)i * 4));
}

/* rsin / rcos: the quarter-wave table at 0x8005B81C (s16, 1025 entries,
 * 4096 to a turn, 4096 = 1.0). */
static inline s32 qsin(u32 i) { return (s16)GV(u16, 0x8005B81Cu + i * 2); }

/* 8002A034 sin_1(a): the sine of a, 0 <= a < 4096. */
void n_gte_sin_1(void) {
    s32 a = (s32)A0;
    if (a < 2049) V0 = (u32)(a < 1025 ? qsin((u32)a) : qsin((u32)(2048 - a)));
    else if (a < 3073) V0 = (u32)-qsin((u32)(a - 2048));
    else V0 = (u32)-qsin((u32)(4096 - a));
}

/* 80029FF8 rsin(a). */
void n_gte_rsin(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    s32 a = (s32)A0;
    if (a < 0) V0 = 0u - gcall(0x8002A034u, (0u - (u32)a) & 0xFFF, R[5], R[6], R[7]);
    else gcall(0x8002A034u, (u32)a & 0xFFF, R[5], R[6], R[7]);
}

/* 8002A0C8 rcos(a). */
void n_gte_rcos(void) {
    s32 a = (s32)A0;
    s32 x = (s32)((a >= 0 ? (u32)a : 0u - (u32)a) & 0xFFF);
    if (x < 2049) V0 = (u32)(x < 1025 ? qsin((u32)(1024 - x)) : -qsin((u32)(x - 1024)));
    else if (x < 3073) V0 = (u32)-qsin((u32)(3072 - x));
    else V0 = (u32)qsin((u32)(x - 3072));
}

/* 8002AE98 (r, m): m = Ry x Rz x Rx for angles r (x, y, z). Returns m. */
void n_gte_RotMatrixYZX(void) {
    u32 r = A0, m = A1;
    s32 sx, cx, sy, cy, sz, cz;
    sin_cos((s16)MEM_R16(r + 4), &sz, &cz);
    sin_cos((s16)MEM_R16(r + 2), &sy, &cy);
    sin_cos((s16)MEM_R16(r), &sx, &cx);
    MEM_W16(m + 6, (u16)sz);
    MEM_W16(m, (u16)fx(cz, cy));
    MEM_W16(m + 0xC, (u16)((s32)(0u - (u32)cz * (u32)sy) >> 12));
    MEM_W16(m + 8, (u16)fx(cx, cz));
    MEM_W16(m + 0xA, (u16)((s32)(0u - (u32)sx * (u32)cz) >> 12));
    s32 t = fx(sz, sx);
    MEM_W16(m + 0x10, (u16)(fx(cx, cy) - fx(t, sy)));
    MEM_W16(m + 4, (u16)(fx(t, cy) + fx(sy, cx)));
    t = fx(sz, cx);
    MEM_W16(m + 0xE, (u16)(fx(t, sy) + fx(cy, sx)));
    MEM_W16(m + 2, (u16)(fx(sx, sy) - fx(t, cy)));
    V0 = m;
}

/* 8002CF14 (m): load m as the rotation and translation (SetRotMatrix,
 * SetTransMatrix). */
void n_gte_SetLsMatrix(void) {
    FRAME(0x18);
    u32 s0 = R[16];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, R[31]);
    R[16] = A0;
    gcall(0x8002A778u, A0, R[5], R[6], R[7]);
    gcall(0x8002A7A8u, R[16], R[5], R[6], R[7]);
    R[16] = s0;
}

/* 8002D6C8 SetLightMatrix(m): control registers 8-12. */
void n_gte_SetLightMatrix(void) {
    u32 m = A0;
    u32 a = MEM_R32(m), b = MEM_R32(m + 4), c = MEM_R32(m + 8), d = MEM_R32(m + 0xC), e = MEM_R32(m + 0x10);
    gte_write_ctrl(8, a);
    gte_write_ctrl(9, b);
    gte_write_ctrl(10, c);
    gte_write_ctrl(11, d);
    gte_write_ctrl(12, e);
}

/* 80041FE4 (a, b, out): out = a x b (the GTE's outer product, OP sf 0),
 * the rotation matrix's diagonal borrowed for a and put back. */
void n_gte_OuterProduct0(void) {
    u32 r0 = gte_read_ctrl(0), r2 = gte_read_ctrl(2), r4 = gte_read_ctrl(4);
    u32 a0 = MEM_R32(A0), a1 = MEM_R32(A0 + 4), a2 = MEM_R32(A0 + 8);
    gte_write_ctrl(0, a0);
    gte_write_ctrl(2, a1);
    gte_write_ctrl(4, a2);
    gte_write_data(11, MEM_R32(A1 + 8));
    gte_write_data(9, MEM_R32(A1));
    gte_write_data(10, MEM_R32(A1 + 4));
    gte_command(0x170000Cu);
    gte_swc2(A2, 25);
    gte_swc2(A2 + 4, 26);
    gte_swc2(A2 + 8, 27);
    gte_write_ctrl(0, r0);
    gte_write_ctrl(2, r2);
    gte_write_ctrl(4, r4);
}

/* 8002CF40 (m): the light matrix for a local frame: GsSetLightMatrix's
 * world light matrix (0x80065B30) times m, loaded (the GTE's rotation
 * and translation kept). */
void n_gte_SetLocalLight(void) {
    FRAME(0x40);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x30, s0);
    MEM_W32(sp + 0x34, s1);
    MEM_W32(sp + 0x38, R[31]);
    R[17] = A0;
    for (u32 i = 0; i < 0x20; i += 4) MEM_W32(sp + 0x10 + i, GV(u32, 0x80065B30u + i));
    gcall(0x8002A468u, R[4], R[5], R[6], R[7]);         /* PushMatrix */
    R[16] = sp + 0x10;
    gcall(0x8002D4A8u, R[16], R[17], R[6], R[7]);       /* MulMatrix */
    gcall(0x8002A50Cu, R[4], R[5], R[6], R[7]);         /* PopMatrix */
    gcall(0x8002D6C8u, R[16], R[5], R[6], R[7]);        /* SetLightMatrix */
    R[16] = s0; R[17] = s1;
}

/* 8002D114 CompMatrix(m0, m1): m0 = m0 x m1, its translation plus m0's
 * rotation applied to m1's. */
void n_gte_CompMatrix(void) {
    FRAME(0x30);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x20, s0);
    MEM_W32(sp + 0x24, s1);
    MEM_W32(sp + 0x28, R[31]);
    R[16] = A0; R[17] = A1;
    gcall(0x8002D348u, R[16], R[17] + 0x14, sp + 0x10, R[7]);   /* ApplyMatrixLV */
    gcall(0x8002D4A8u, R[16], R[17], R[6], R[7]);               /* MulMatrix */
    for (u32 i = 0; i < 12; i += 4) {
        V0 = MEM_R32(sp + 0x10 + i) + MEM_R32(R[16] + 0x14 + i);
        MEM_W32(R[16] + 0x14 + i, V0);
    }
    R[16] = s0; R[17] = s1;
}

/* 8002A9E8 RotTransPers4(v0, v1, v2, v3, sxy0, sxy1, sxy2, sxy3, p,
 * flag): the four corners through the perspective (RTPT, RTPS), the
 * flags of both into *flag; returns the average z (AVSZ4, OTZ). */
void n_gte_RotTransPers4(void) {
    u32 sp = R[29];
    gte_write_data(0, MEM_R32(A0));
    gte_write_data(1, MEM_R32(A0 + 4));
    gte_write_data(2, MEM_R32(A1));
    gte_write_data(3, MEM_R32(A1 + 4));
    gte_write_data(4, MEM_R32(A2));
    gte_write_data(5, MEM_R32(A2 + 4));
    gte_command(0x0280030u);
    gte_swc2(MEM_R32(sp + 0x10), 12);
    gte_swc2(MEM_R32(sp + 0x14), 13);
    gte_swc2(MEM_R32(sp + 0x18), 14);
    u32 f = gte_read_ctrl(31);
    gte_write_data(0, MEM_R32(A3));
    gte_write_data(1, MEM_R32(A3 + 4));
    gte_command(0x0180001u);
    gte_swc2(MEM_R32(sp + 0x1C), 14);
    u32 f2 = gte_read_ctrl(31);
    gte_swc2(MEM_R32(sp + 0x20), 8);
    MEM_W32(MEM_R32(sp + 0x24), f2 | f);
    gte_command(0x168002Eu);
    V0 = gte_read_data(7);
}

/* ---- GsCOORDINATE2: +0 flag (the frame it was computed: GsWSMATRIX's
 * counter 0x80065A90, 0 dirty), +4 coord (MATRIX), +0x24 workm (the
 * world matrix), +0x4C super ---- */
#define GS_CLOCK    GV(u32, 0x80065A90u)
#define GS_CHAIN    0x80065C30u            /* the coordinates walked */
static inline void copy_matrix(u32 dst, u32 src) {
    for (u32 i = 0; i < 0x20; i += 4) MEM_W32(dst + i, MEM_R32(src + i));
}

/* GsGetLw / GsGetLws's walk: the local-to-world matrix of coord into
 * lw (R[18]), recomputing the chain up from the first coordinate
 * computed this frame or the root, each workm kept and flagged; s1 the
 * depth, s0 the chain pointer, as the original has them. */
static void lw_chain(u32 c, u32 poll_up, u32 poll_down) {
    u32 dirty = 100;
    R[17] = 0;
    for (;;) {
        GV(u32, GS_CHAIN + (R[17] << 2)) = c;
        u32 up = MEM_R32(c + 0x4C);
        u32 flg = MEM_R32(c);
        if (up != 0) {
            if (flg == GS_CLOCK) { copy_matrix(R[18], c + 0x24); break; }
            c = up;
            if (flg == 0) dirty = R[17];
            R[17]++;
            LOOP_POLL(poll_up);
            continue;
        }
        if (flg == GS_CLOCK || flg == 0) {
            copy_matrix(c + 0x24, c + 4);
            copy_matrix(R[18], c + 0x24);
            MEM_W32(c, GS_CLOCK);
        } else if (dirty == 100) {
            copy_matrix(R[18], GV(u32, GS_CHAIN) + 0x24);
            R[17] = 0;
        } else {
            R[17] = dirty + 1;
            copy_matrix(R[18], GV(u32, GS_CHAIN + (R[17] << 2)) + 0x24);
        }
        break;
    }
    V0 = 0;
    if ((s32)R[17] > 0) {
        R[16] = GS_CHAIN - 4 + (R[17] << 2);
        for (;;) {
            gcall(0x8002D114u, R[18], MEM_R32(R[16]) + 4, R[6], R[7]);   /* CompMatrix */
            R[17]--;
            copy_matrix(MEM_R32(R[16]) + 0x24, R[18]);
            V0 = GS_CLOCK;
            MEM_W32(MEM_R32(R[16]), V0);
            R[16] -= 4;
            if (!((s32)R[17] > 0)) break;
            LOOP_POLL(poll_down);
        }
    }
}

/* 80033CB8 GsGetLws(coord, lw, ls): lw_chain, and the local to screen
 * matrix (GsWSMATRIX 0x80065BB0 x lw) into ls. */
void n_gs_GetLws(void) {
    FRAME(0x28);
    u32 sv[4] = { R[16], R[17], R[18], R[19] };
    for (int i = 0; i < 4; i++) MEM_W32(sp + 0x10 + (u32)i * 4, sv[i]);
    MEM_W32(sp + 0x20, R[31]);
    R[18] = A1; R[19] = A2;
    lw_chain(A0, 0x80033ED4u, 0x80033F58u);
    copy_matrix(R[19], R[18]);
    gcall(0x8002D094u, 0x80065BB0u, R[19], R[6], R[7]);                 /* CompMatrixLV */
    for (int i = 0; i < 4; i++) R[16 + i] = sv[i];
}

/* 80033728 GsGetLw(coord, lw): lw_chain alone. */
void n_gs_GetLw(void) {
    FRAME(0x20);
    u32 sv[3] = { R[16], R[17], R[18] };
    for (int i = 0; i < 3; i++) MEM_W32(sp + 0x10 + (u32)i * 4, sv[i]);
    MEM_W32(sp + 0x1C, R[31]);
    R[18] = A1;
    lw_chain(A0, 0x8003393Cu, 0x800339C0u);
    for (int i = 0; i < 3; i++) R[16 + i] = sv[i];
}

/* 8002DED8 GsClearOt(offset, point, ot): a GsOT (+0 length (log2), +4
 * org, +8 offset, +0xC point, +0x10 tag = the last entry) cleared
 * (ClearOTag of its 1 << length entries). */
void n_gs_ClearOt(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 ot = A2;
    MEM_W32(ot + 8, A0 & 0xFFFF);
    MEM_W32(ot + 0xC, A1 & 0xFFFF);
    u32 len = MEM_R32(ot);
    MEM_W32(ot + 0x10, MEM_R32(ot + 4) + (4u << (len & 31)) - 4);
    gcall(0x80035860u, MEM_R32(ot + 4), 1u << (MEM_R32(ot) & 31), ot, R[7]);
}

/* 8002DE88 SetBackColor(r, g, b): control registers 13-15, x16. */
void n_gte_SetBackColor(void) {
    gte_write_ctrl(13, A0 << 4);
    gte_write_ctrl(14, A1 << 4);
    gte_write_ctrl(15, A2 << 4);
}

/* 8002DE58 GsSetAmbient(r, g, b): SetBackColor of each / 16. */
void n_gs_SetAmbient(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall(0x8002DE88u, (u32)((s32)A0 >> 4), (u32)((s32)A1 >> 4), (u32)((s32)A2 >> 4), R[7]);
}

/* 8003455C (n): the number of bits n takes (0 for n <= 0). */
u32 bit_width(s32 n, u32 poll) {
    u32 b = 0;
    if (n > 0) {
        for (;;) {
            n >>= 1;
            b++;
            if (!(n > 0)) break;
            LOOP_POLL(poll);
        }
    }
    return b;
}
void n_gs_BitWidth(void) { V0 = bit_width((s32)A0, 0x80034568u); }

/* 8002FBF8 (pp): the word after the block *pp points at (its first
 * halfword the length in words). */
void n_gs_NextBlock(void) {
    u32 p = MEM_R32(A0);
    V0 = p + ((u32)MEM_R16(p) << 2);
}

/* 8002DEA8 GsDrawOt(ot): draw its table (DrawOTag 80035968 of the tag). */
void n_gs_DrawOt(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall(0x80035968u, MEM_R32(A0 + 0x10), R[5], R[6], R[7]);
}

/* 8002CB88 GsGetActiveBuff. */
void n_gs_GetActiveBuff(void) { V0 = (u32)(s32)(s16)GV(u16, 0x80065A94u); }

/* 8002E208 GsSetWorkBase(p): the packet pointer. */
void n_gs_SetWorkBase(void) { GV(u32, 0x80065C08u) = A0; }

/* 8002A5B8 ApplyMatrix(m, v, out): out = m x v (a short vector, long
 * results). Returns out. */
void n_gte_ApplyMatrix(void) {
    set_rot(A0);
    gte_write_data(0, MEM_R32(A1));
    gte_write_data(1, MEM_R32(A1 + 4));
    gte_command(GTE_MVMVA_V0);
    gte_swc2(A2, 25);
    gte_swc2(A2 + 4, 26);
    gte_swc2(A2 + 8, 27);
    V0 = A2;
}

/* 80030948 TransposeMatrix(m, out): out = m's rotation transposed, by
 * words and halfwords in the library's order. Returns out. */
void n_gte_TransposeMatrix(void) {
    u32 m = A0, o = A1;
    u32 a = MEM_R32(m), b = MEM_R32(m + 4);
    MEM_W32(o + 4, a);
    MEM_W32(o, b);
    MEM_W16(o, (u16)a);
    u32 c = MEM_R32(m + 8), d = MEM_R32(m + 0xC);
    MEM_W32(o + 0xC, c);
    MEM_W32(o + 8, d);
    MEM_W16(o + 0xC, (u16)b);
    MEM_W16(o + 8, (u16)c);
    u32 e = (u32)(s32)(s16)MEM_R16(m + 0x10);
    MEM_W16(o + 4, (u16)d);
    MEM_W16(o + 0x10, (u16)e);
    V0 = o;
}

/* libgs's drawing environment (0x80065A10, its clip RECT first) gets
 * clip (x, y offset by the buffer's origin 0x800659F8 / 0x800659FC,
 * by GsGetActiveBuff 0x80065A94) and is sent (PutDrawEnv). */
static void set_clip(u32 r) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 b = (u32)(s32)(s16)GV(u16, 0x80065A94u) << 1;
    s32 ox = (s16)GV(u16, 0x800659F8u + b), oy = (s16)GV(u16, 0x800659FCu + b);
    const u32 env = 0x80065A10u;
    MEM_W16(env, (u16)((s16)MEM_R16(r) + ox));
    MEM_W16(env + 2, (u16)((s16)MEM_R16(r + 2) + oy));
    MEM_W16(env + 4, MEM_R16(r + 4));
    MEM_W16(env + 6, MEM_R16(r + 6));
    gcall(0x800359D8u, env, R[5], R[6], R[7]);
}

/* 8002CD28 GsSetClip(rect). */
void n_gs_SetClipRect(void) { set_clip(A0); }
/* 8002CCA8: the same with the clip kept at 0x80065A88. */
void n_gs_ApplyClip(void) { set_clip(0x80065A88u); }

/* 8002E218 GsDefDispBuff(x0, y0, x1, y1): the two buffers' origins
 * (0x800659F8 x0, x1; 0x800659FC y0, y1), and the same as the drawing
 * offsets (0x80065A00 / 0x80065A04) unless interlaced (0x80065A96): 0. */
void n_gs_DefDispBuff(void) {
    MEM_W16(0x800659F8u, (u16)A0);
    MEM_W16(0x800659FAu, (u16)A2);
    MEM_W16(0x800659FCu, (u16)A1);
    MEM_W16(0x800659FEu, (u16)A3);
    int il = (s16)GV(u16, 0x80065A96u) != 0;
    MEM_W16(0x80065A00u, il ? 0 : (u16)A0);
    MEM_W16(0x80065A02u, il ? 0 : (u16)A2);
    MEM_W16(0x80065A04u, il ? 0 : (u16)A1);
    MEM_W16(0x80065A06u, il ? 0 : (u16)A3);
    V0 = 0x80065A04u;
}

/* 8002DF38 GsSortOt(src, dst): link the ordering table src into dst at
 * src's point (less dst's offset): the entry two before src's last
 * (as the library finds it) gets that dst entry's link, the dst entry
 * src's tag. Returns dst. */
void n_gs_SortOt(void) {
    u32 src = A0, dst = A1;
    u32 p6 = MEM_R32(src + 4), p7 = p6, p8 = p6;
    u32 idx = MEM_R32(src + 0xC) - MEM_R32(dst + 8);
    if ((MEM_R32(p8) & 0xFFFFFF) != 0xFFFFFF) {
        for (;;) {
            p8 = p7;
            u32 l = MEM_R32(p6);
            p7 = p6;
            p6 = l & 0xFFFFFF;
            if ((MEM_R32(p6) & 0xFFFFFF) == 0xFFFFFF) break;
            LOOP_POLL(0x8002DF98u);
        }
    }
    u32 tag = (idx << 2) + MEM_R32(dst + 4);
    MEM_W32(p8, (MEM_R32(p8) & 0xFF000000u) | (MEM_R32(tag) & 0xFFFFFF));
    MEM_W32(tag, (MEM_R32(tag) & 0xFF000000u) | (MEM_R32(src + 0x10) & 0xFFFFFF));
    V0 = dst;
}

/* 8002E138 (m, sin, cos, axis): m = the identity (0x80065B90) turned
 * about axis 'X', 'Y' or 'Z' (either case) by the angle with that sine
 * and cosine; another letter leaves it the identity. */
void n_gte_AxisMatrix(void) {
    u32 m = A0, s = A1, c = A2;
    copy_matrix(m, 0x80065B90u);
    u32 k = (u32)(s32)(s8)(u8)(A3 - 0x58);
    if (!(k < 0x23)) { V0 = k << 2; return; }
    V0 = MEM_R32(0x80010740u + (k << 2));
    LOOP_POLL(0x8002E1ACu);
    if (k == 0 || k == 32) {
        MEM_W16(m + 8, (u16)c); MEM_W16(m + 0x10, (u16)c);
        MEM_W16(m + 0xA, (u16)(0u - s)); MEM_W16(m + 0xE, (u16)s);
    } else if (k == 1 || k == 33) {
        MEM_W16(m, (u16)c); MEM_W16(m + 0x10, (u16)c);
        MEM_W16(m + 4, (u16)s); MEM_W16(m + 0xC, (u16)(0u - s));
    } else if (k == 2 || k == 34) {
        MEM_W16(m, (u16)c); MEM_W16(m + 8, (u16)c);
        MEM_W16(m + 2, (u16)(0u - s)); MEM_W16(m + 6, (u16)s);
    } else return;
    V0 = 0u - s;
}

/* 8002CDA8 GsSwapDispBuff: show the buffer drawn (PutDispEnv 80035BA4
 * of 0x80065A70 at its origin), display on, the clock (0x80065A90, never
 * 0) ticked, the other buffer made active with its clip and offset
 * (8002CCA8, 8002CB98). */
void n_gs_SwapDispBuff(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 b = (u32)(s32)(s16)GV(u16, 0x80065A94u) << 1;
    MEM_W16(0x80065A70u, GV(u16, 0x800659F8u + b));
    MEM_W16(0x80065A72u, GV(u16, 0x800659FCu + b));
    gcall(0x80035BA4u, 0x80065A70u, R[5], R[6], R[7]);
    gcall(0x800352DCu, 1, R[5], R[6], R[7]);
    u32 c = GS_CLOCK + 1;
    GS_CLOCK = c;
    if (c == 0) GS_CLOCK = 1;
    GV(u16, 0x80065A94u) = (u16)((u32)(s32)(s16)GV(u16, 0x80065A94u) < 1);
    gcall(0x8002CCA8u, R[4], R[5], R[6], R[7]);
    gcall(0x8002CB98u, R[4], R[5], R[6], R[7]);
}

/* 80034494 (v): the largest magnitude of six longs. */
void n_gs_MaxAbs6(void) {
    u32 v = A0;
    s32 m = (s32)MEM_R32(v);
    if (m < 0) m = (s32)(0u - (u32)m);
    for (u32 i = 4; i < 0x18; i += 4) {
        s32 x = (s32)MEM_R32(v + i);
        if (x < 0) x = (s32)(0u - (u32)x);
        if (m < x) m = x;
    }
    V0 = (u32)m;
}

/* 800343A8 (src, dst): six longs copied, shifted down to fit 16 bits
 * signed when the largest (80034494) takes more (8003455C). */
void n_gs_Fit16(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
    R[16] = A0; R[17] = A1;
    u32 m = gcall(0x80034494u, R[16], R[5], R[6], R[7]);
    u32 n = gcall(0x8003455Cu, m, R[5], R[6], R[7]);
    s32 sh = (s32)n < 16 ? 0 : (s32)n - 15;
    R[3] = n - 15;
    for (u32 i = 0; i < 0x18; i += 4) {
        V0 = (u32)((s32)MEM_R32(R[16] + i) >> (sh & 31));
        MEM_W32(R[17] + i, V0);
    }
    R[16] = s0; R[17] = s1;
}

/* 800302E8 (m, angle): m = m x a turn about z by angle / 360 (rcos,
 * rsin; the matrix built on the stack); angle 0 leaves m. */
void n_gs_RotateZDeg(void) {
    FRAME(0x40);
    u32 sv[3] = { R[16], R[17], R[18] };
    MEM_W32(sp + 0x30, sv[0]);
    MEM_W32(sp + 0x34, sv[1]);
    MEM_W32(sp + 0x38, sv[2]);
    MEM_W32(sp + 0x3C, R[31]);
    R[17] = A1; R[18] = A0;
    R[16] = (u32)((s32)R[17] / 360);
    u32 c = gcall(0x8002A0C8u, R[16], R[5], R[6], R[7]);
    u32 q = R[16];
    R[16] = c;
    u32 sn = gcall(0x80029FF8u, q, R[5], R[6], R[7]);
    R[3] = sn;
    if (R[17] != 0) {
        const u32 t = sp + 0x10;
        MEM_W16(t + 2, (u16)(0u - sn));
        MEM_W16(t, (u16)R[16]);
        MEM_W16(t + 4, 0);
        MEM_W16(t + 6, (u16)sn);
        MEM_W16(t + 8, (u16)R[16]);
        MEM_W16(t + 0xA, 0);
        MEM_W16(t + 0xC, 0);
        MEM_W16(t + 0xE, 0);
        MEM_W16(t + 0x10, 0x1000);
        MEM_W32(t + 0x14, 0);
        MEM_W32(t + 0x18, 0);
        MEM_W32(t + 0x1C, 0);
        gcall(0x8002D4A8u, R[18], t, R[6], R[7]);
    }
    R[16] = sv[0]; R[17] = sv[1]; R[18] = sv[2];
}

/* 8002A8F8 SetGeomOffset(x, y): control registers 24, 25 (16.16). */
void n_gte_SetGeomOffset(void) {
    gte_write_ctrl(24, A0 << 16);
    gte_write_ctrl(25, A1 << 16);
}

/* 8002CB98: the drawing offset for the active buffer (0x80065A94): the
 * offset kept at 0x80065A08 plus a buffer origin (0x800659F8 / FC; the
 * other buffer's entry, as the library indexes it) as the GTE's screen
 * offset and libgs's (0x80065A84); interlaced (0x80065A96), the drawing
 * environment's offset (0x80065A18) instead, sent with PutDrawEnv and
 * libgs's cleared. */
void n_gs_SetDrawOffset(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
    if ((s16)GV(u16, 0x80065A96u) != 0) {
        u32 b = (u32)(s32)(s16)GV(u16, 0x80065A94u) << 1;
        GV(u16, 0x80065A86u) = 0;
        GV(u16, 0x80065A84u) = 0;
        MEM_W16(0x80065A18u, GV(u16, 0x80065A08u) + GV(u16, 0x800659F8u + b));
        MEM_W16(0x80065A1Au, GV(u16, 0x80065A0Au) + GV(u16, 0x800659FCu + b));
        gcall(0x800359D8u, 0x80065A10u, R[5], R[6], R[7]);
    } else {
        R[16] = 0x80065A08u;
        s32 b = (s16)GV(u16, 0x80065A94u);
        s32 x = (s16)GV(u16, R[16]);
        R[17] = (u32)(x + (s16)GV(u16, b ? 0x800659F8u : 0x800659FAu));
        s32 y = (s16)GV(u16, R[16] + 2);
        R[16] = (u32)(y + (s16)GV(u16, b ? 0x800659FCu : 0x800659FEu));
        gcall(0x8002A8F8u, R[17], R[16], R[6], R[7]);
        GV(u16, 0x80065A84u) = (u16)R[17];
        GV(u16, 0x80065A86u) = (u16)R[16];
    }
    R[16] = s0; R[17] = s1;
}

/* 8002CA50 GsSortClear(r, g, b, ot): the active buffer's clear packet
 * (0x800659D8 + buffer * 16: colour, its origin, the screen size
 * 0x80065B18 / 0x80065B1C, the width x1.5 in high resolution
 * 0x80065A81) added to the OT's tag. */
void n_gs_SortClear(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    u32 b = (u32)(s32)(s16)GV(u16, 0x80065A94u);
    u32 p = 0x800659D8u + (b << 4);
    MEM_W8(p + 4, (u8)A0);
    MEM_W8(p + 5, (u8)A1);
    MEM_W8(p + 6, (u8)A2);
    MEM_W16(p + 8, GV(u16, 0x800659F8u + (b << 1)));
    MEM_W16(p + 0xE, GV(u16, 0x80065B1Cu));
    MEM_W16(p + 0xA, GV(u16, 0x800659FCu + (b << 1)));
    if (GV(u8, 0x80065A81u)) {
        s32 w = (s32)(GV(u32, 0x80065B18u) * 3);
        MEM_W16(p + 0xC, (u16)((w + (s32)((u32)w >> 31)) >> 1));
    } else {
        MEM_W16(p + 0xC, GV(u16, 0x80065B18u));
    }
    gcall(0x800381E8u, MEM_R32(A3 + 0x10), p, R[6], R[7]);
}

/* 8002ACF8 RotMatrixY(angle, m): m = Ry(angle) x m (rows 0 and 2).
 * Returns m. */
void n_gte_RotMatrixY(void) {
    s32 a = (s32)A0;
    u32 m = A1;
    u32 w = GV(u32, 0x8005C4FCu + (((a >= 0 ? (u32)a : 0u - (u32)a) & 0xFFF) << 2));
    s32 c = (s32)w >> 16;
    s32 ns = a >= 0 ? -(s32)(s16)w : (s32)(s16)w;    /* -sin */
    s32 r0[3], r2[3];
    for (int i = 0; i < 3; i++) {
        r0[i] = (s16)MEM_R16(m + (u32)i * 2);
        r2[i] = (s16)MEM_R16(m + 0xC + (u32)i * 2);
    }
    for (int i = 0; i < 3; i++)
        MEM_W16(m + (u32)i * 2, (u16)((s32)((u32)c * (u32)r0[i] - (u32)ns * (u32)r2[i]) >> 12));
    for (int i = 0; i < 3; i++)
        MEM_W16(m + 0xC + (u32)i * 2, (u16)((s32)((u32)ns * (u32)r0[i] + (u32)c * (u32)r2[i]) >> 12));
    V0 = m;
}

/* 800339E8 GsGetLs(coord, ls): lw_chain into ls, then GsWSMATRIX x it. */
void n_gs_GetLs(void) {
    FRAME(0x20);
    u32 sv[3] = { R[16], R[17], R[18] };
    for (int i = 0; i < 3; i++) MEM_W32(sp + 0x10 + (u32)i * 4, sv[i]);
    MEM_W32(sp + 0x1C, R[31]);
    R[18] = A1;
    lw_chain(A0, 0x80033BFCu, 0x80033C80u);
    gcall(0x8002D094u, 0x80065BB0u, R[18], R[6], R[7]);                 /* CompMatrixLV */
    for (int i = 0; i < 3; i++) R[16 + i] = sv[i];
}

/* 80033FD8 GsSetRefView2(view): the world-to-screen matrix GsWSMATRIX
 * (0x80065BB0) for a GsRVIEW2 (viewpoint +0, reference point +0xC, twist
 * +0x18, super +0x1C): from the identity (0x80065BD0), turned by -twist,
 * then about x and y to look along view -> reference (their difference
 * scaled to fit 16 bits, 8003435C..), moved to -viewpoint; relative to
 * super's world matrix (GsGetLw) when there is one. Kept also as
 * 0x80065B70. Returns 0, or 1 when the two points are the same. The
 * original's s-registers. */
void n_gs_SetRefView2(void) {
    FRAME(0xC0);
    u32 sv[6] = { R[16], R[17], R[18], R[19], R[20], R[21] };
    for (int i = 0; i < 6; i++) MEM_W32(sp + 0xA0 + (u32)i * 4, sv[i]);
    MEM_W32(sp + 0xB8, R[31]);
    R[20] = A0; R[21] = 0x80065BB0u;
    const u32 ws = R[21], v = sp + 0x10;
    copy_matrix(ws, 0x80065BD0u);
    gcall(0x800302E8u, ws, 0u - MEM_R32(R[20] + 0x18), R[6], R[7]);    /* turn by -twist */
    gcall(0x800343A8u, R[20], v, R[6], R[7]);                          /* fit to 16 bits */
#define VV(i) MEM_R32(v + (i) * 4)
    u32 dx = VV(3) - VV(0), dy = VV(4) - VV(1), dz = VV(5) - VV(2);
    R[18] = gcall(0x8002A3D8u, dx * dx + dy * dy + dz * dz, R[5], R[6], R[7]);
    if (R[18] == 0) { V0 = 1; goto out; }
    R[17] = VV(1) - VV(4);
    R[16] = R[17] << 12;
    R[16] = (u32)mips_div((s32)R[16], (s32)R[18]);
    dx = VV(3) - VV(0); dz = VV(5) - VV(2);
    R[16] = 0u - R[16];
    R[17] = gcall(0x8002A3D8u, dx * dx + dz * dz, R[5], R[6], R[7]);
    u32 c = (u32)mips_div((s32)(R[17] << 12), (s32)R[18]);
    R[19] = sp + 0x30;
    gcall(0x8002E138u, R[19], (u32)(s32)(s16)R[16], (u32)(s32)(s16)c, 0x78);
    gcall(0x8002D4A8u, ws, R[19], R[6], R[7]);
    R[18] = R[17];
    if (R[17] != 0) {
        R[17] = VV(3) - VV(0);
        u32 sn = (u32)mips_div((s32)(R[17] << 12), (s32)R[18]);
        R[17] = VV(5) - VV(2);
        u32 cs = (u32)mips_div((s32)(R[17] << 12), (s32)R[18]);
        gcall(0x8002E138u, R[19], (u32)(s32)(s16)(0u - sn), (u32)(s32)(s16)cs, 0x79);
        gcall(0x8002D4A8u, ws, R[19], R[6], R[7]);
    }
#undef VV
    R[17] = sp + 0x90;
    MEM_W32(sp + 0x90, 0u - MEM_R32(R[20]));
    MEM_W32(sp + 0x94, 0u - MEM_R32(R[20] + 4));
    MEM_W32(sp + 0x98, 0u - MEM_R32(R[20] + 8));
    gcall(0x8002D348u, ws, R[17], ws + 0x14, R[7]);                      /* ApplyMatrixLV */
    u32 sup = MEM_R32(R[20] + 0x1C);
    if (sup != 0) {
        gcall(0x80033728u, sup, R[19], R[6], R[7]);                      /* GsGetLw */
        R[16] = sp + 0x50;
        gcall(0x80030948u, R[19], R[16], R[6], R[7]);                    /* TransposeMatrix */
        gcall(0x8002D348u, R[16], sp + 0x44, R[17], R[7]);
        MEM_W32(sp + 0x64, 0u - MEM_R32(sp + 0x90));
        MEM_W32(sp + 0x6C, 0u - MEM_R32(sp + 0x98));
        MEM_W32(sp + 0x68, 0u - MEM_R32(sp + 0x94));
        gcall(0x8002D094u, ws, R[16], R[6], R[7]);                       /* CompMatrixLV */
        copy_matrix(ws, sp + 0x50);
    }
    copy_matrix(0x80065B70u, ws);
    V0 = 0;
out:
    for (int i = 0; i < 6; i++) R[16 + i] = sv[i];
}

/* 80032478 (r, m): m = Ry x Rx x Rz for angles r (x, y, z). Returns m. */
void n_gte_RotMatrixYXZ(void) {
    u32 r = A0, m = A1;
    s32 sx, cx, sy, cy, sz, cz;
    sin_cos((s16)MEM_R16(r), &sx, &cx);
    sin_cos((s16)MEM_R16(r + 2), &sy, &cy);
    MEM_W16(m + 0xA, (u16)(0u - (u32)sx));
    MEM_W16(m + 4, (u16)fx(sy, cx));
    sin_cos((s16)MEM_R16(r + 4), &sz, &cz);
    MEM_W16(m + 0x10, (u16)fx(cy, cx));
    MEM_W16(m + 6, (u16)fx(sz, cx));
    MEM_W16(m + 8, (u16)fx(cz, cx));
    s32 t = fx(sy, sx);
    MEM_W16(m, (u16)(fx(cy, cz) + fx(t, sz)));
    MEM_W16(m + 2, (u16)(fx(t, cz) - fx(cy, sz)));
    t = fx(cy, sx);
    MEM_W16(m + 0xE, (u16)(fx(sy, sz) + fx(t, cz)));
    MEM_W16(m + 0xC, (u16)(fx(t, sz) - fx(sy, cz)));
    V0 = m;
}

/* 8002A918 SetGeomScreen(h): control register 26. */
void n_gte_SetGeomScreen(void) { gte_write_ctrl(26, A0); }

/* 8002FDE8 (args): an HMD block setting the projection distance
 * (SetGeomScreen of *(args + 0x14)); returns the block pointer + 4. */
void n_gs_ScreenBlock(void) {
    FRAME(0x18);
    u32 s0 = R[16];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, R[31]);
    R[16] = A0;
    gcall(0x8002A918u, MEM_R32(MEM_R32(R[16] + 0x14)), R[5], R[6], R[7]);
    V0 = MEM_R32(R[16]) + 4;
    R[16] = s0;
}

/* 8002FD58 (args): an HMD interpolation block: 80033150 (block, args +
 * 0x18, + 0x1C, + 0x20); returns the block pointer past it (its first
 * halfword the length in words). */
void n_gs_InterpBlock(void) {
    FRAME(0x18);
    u32 s0 = R[16];
    MEM_W32(sp + 0x14, R[31]);
    MEM_W32(sp + 0x10, s0);
    u32 a = A0;
    R[16] = MEM_R32(a);
    gcall(0x80033150u, R[16], MEM_R32(a + 0x18), MEM_R32(a + 0x1C), MEM_R32(a + 0x20));
    V0 = R[16] + ((u32)MEM_R16(R[16]) << 2);
    R[16] = s0;
}

/* 800330E8 (dst, src, n, w): dst[i] += src[i] x w / 4096 for n short
 * vectors (8 bytes apart; the GTE's GPF, at least once). */
void n_gs_AddScaled(void) {
    u32 d = A0, s = A1;
    s32 n = (s32)A2;
    gte_write_data(8, A3);
    for (;;) {
        u32 w = MEM_R32(s);
        u32 z = (u32)(s32)(s16)MEM_R16(s + 4);
        gte_write_data(9, w);
        gte_write_data(10, (u32)((s32)w >> 16));
        gte_write_data(11, z);
        s += 8;
        n--;
        gte_command(0x198003Du);
        u32 dw = MEM_R32(d);
        u32 dz = (u32)(s32)(s16)MEM_R16(d + 4);
        u32 m1 = gte_read_data(25), m2 = gte_read_data(26), m3 = gte_read_data(27);
        MEM_W16(d, (u16)(dw + m1));
        MEM_W16(d + 2, (u16)((u32)((s32)dw >> 16) + m2));
        MEM_W16(d + 4, (u16)(dz + m3));
        d += 8;
        if (!(n > 0)) break;
        LOOP_POLL(0x80033140u);
    }
}

/* 80031BA0 (p): 800319C8 (p, p + 0x28's word). */
void n_gs_CallObj28(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall(0x800319C8u, A0, MEM_R32(A0 + 0x28), R[6], R[7]);
}

/* 80033150 (hdr, table, src, dst): an HMD copy block: for each of the
 * header's entries (count in its top half), the table's run list (count
 * in the top half of its first word; 0xC-byte runs: dst index, src
 * index, flags | count << 16): runs flagged in their low half are taken
 * (that flag's byte cleared, as the library does with swl) and count
 * 8-byte entries copied from src to dst. */
void n_gs_CopyRuns(void) {
    u32 p = A0 + 4, tab = A1, src0 = A2, dst0 = A3;
    s32 n = (s32)MEM_R32(A0) >> 16;
    for (;;) {
        if (n == 0) break;
        u32 idx = MEM_R32(p);
        p += 4;
        n--;
        u32 r10 = tab + (idx << 2);
        u32 r12 = r10 - 8;
        s32 m = (s32)MEM_R32(r10) >> 16;
        for (;;) {
            if (m == 0) { LOOP_POLL(0x80033188u); break; }
            r12 += 0xC;
            m--;
            u32 w = MEM_R32(r12 + 8);
            if ((w & 0xFFFF) == 0) { LOOP_POLL(0x800331A4u); continue; }
            rt_swl(r12 + 8, 0);
            s32 c = (s32)w >> 16;
            u32 d = dst0 + (MEM_R32(r12) << 3), s = src0 + (MEM_R32(r12 + 4) << 3);
            for (;;) {
                if (c == 0) { LOOP_POLL(0x800331CCu); break; }
                c--;
                u32 a = MEM_R32(s), b = MEM_R32(s + 4);
                s += 8;
                MEM_W32(d, a);
                MEM_W32(d + 4, b);
                d += 8;
                LOOP_POLL(0x800331ECu);
            }
        }
    }
}

/* 80030864 (p): the largest magnitude of the six longs at p + 8. */
void n_gs_MaxAbs6At8(void) {
    u32 v = A0 + 8;
    s32 m = (s32)MEM_R32(v);
    if (m < 0) m = (s32)(0u - (u32)m);
    for (u32 i = 4; i < 0x18; i += 4) {
        s32 x = (s32)MEM_R32(v + i);
        if (x < 0) x = (s32)(0u - (u32)x);
        if (m < x) m = x;
    }
    V0 = (u32)m;
}

/* 80030778 (src, dst): 800343A8 for the six longs at src + 8 (80030864,
 * 8003092C). */
void n_gs_Fit16At8(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x10, s0);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x18, R[31]);
    R[16] = A0; R[17] = A1;
    u32 m = gcall(0x80030864u, R[16], R[5], R[6], R[7]);
    u32 n = gcall(0x8003092Cu, m, R[5], R[6], R[7]);
    s32 sh = (s32)n < 16 ? 0 : (s32)n - 15;
    R[3] = n - 15;
    for (u32 i = 0; i < 0x18; i += 4) {
        V0 = (u32)((s32)MEM_R32(R[16] + 8 + i) >> (sh & 31));
        MEM_W32(R[17] + i, V0);
    }
    R[16] = s0; R[17] = s1;
}

/* 8002BB48 ratan2(y, x): the angle of (x, y), 4096 to a turn, from the
 * arctangent table at 0x8006051C (1024 steps of the ratio). */
void n_gte_ratan2(void) {
    s32 y = (s32)A0, x = (s32)A1;
    int nx = 0, ny = 0;
    if (x < 0) { nx = 1; x = (s32)(0u - (u32)x); }
    if (y < 0) { ny = 1; y = (s32)(0u - (u32)y); }
    if (x == 0 && y == 0) { V0 = 0; return; }
    s32 a;
    if (y < x) {
        s32 q = (y & 0x7FE00000) ? mips_div(y, x >> 10) : mips_div((s32)((u32)y << 10), x);
        a = (s16)GV(u16, 0x8006051Cu + (u32)q * 2);
    } else {
        s32 q = (x & 0x7FE00000) ? mips_div(x, y >> 10) : mips_div((s32)((u32)x << 10), y);
        a = 0x400 - (s16)GV(u16, 0x8006051Cu + (u32)q * 2);
    }
    if (nx) a = 0x800 - a;
    if (ny) a = -a;
    V0 = (u32)a;
}

/* 80033208 GsScanUnit(start, unit, coord, args): walk an HMD's blocks.
 * start set: begin (0x80060D50 the block pointer, 0x80060D4C the next
 * header, 0x80060D54 the type table, 0x80060D58 blocks left); returns
 * 0. Otherwise the next block (moving to the next header when this one
 * is done; -1 there: 0): unit gets its primitive and type (+4 flags,
 * top bit cleared) or zeros, args its driver arguments (block, coord,
 * 0xE - *coord, coord + 8's word, the packet pointer, the type's
 * words); returns 1. */
void n_gs_ScanUnit(void) {
    u32 start = A0, unit = A1, coord = A2, args = A3;
#define BP   GV(u32, 0x80060D50u)
#define NEXT GV(u32, 0x80060D4Cu)
#define TYPE GV(u32, 0x80060D54u)
#define LEFT GV(u16, 0x80060D58u)
    if (start != 0) {
        u32 a = MEM_R32(start), b = MEM_R32(start + 4), c = MEM_R16(start + 8);
        BP = start + 0xC;
        NEXT = a;
        TYPE = b;
        LEFT = (u16)c;
        V0 = 0;
        return;
    }
    u32 c = LEFT - 1u;
    LEFT = (u16)c;
    if ((s32)(c << 16) < 0) {
        u32 h = NEXT;
        if (h == 0xFFFFFFFFu) { V0 = 0; return; }
        BP = h;
        u32 a = MEM_R32(h), b = MEM_R32(h + 4), n = MEM_R16(h + 8);
        BP = h + 0xC;
        NEXT = a;
        TYPE = b;
        LEFT = (u16)(n - 1);
    }
    u32 p = BP;
    if ((s32)MEM_R32(p + 4) < 0) {
        MEM_W32(unit + 4, p);
        MEM_W32(unit, MEM_R32(p));
        MEM_W32(p + 4, MEM_R32(p + 4) & 0x7FFFFFFFu);
    } else {
        MEM_W32(unit, 0);
        MEM_W32(unit + 4, 0);
    }
    MEM_W32(args + 4, coord);
    MEM_W32(args, BP + 4);
    MEM_W32(args + 8, 0xEu - MEM_R32(coord));
    u32 w8 = MEM_R32(coord + 8);
    MEM_W32(args + 0x10, GV(u32, 0x80065C08u));
    MEM_W32(args + 0xC, w8);
    u32 t = TYPE;
    if (MEM_R32(t) != 0) {
        u32 o = args;
        for (u32 j = 0;;) {
            u32 v = MEM_R32(t + 4 + (j << 2));
            j++;
            MEM_W32(o + 0x14, v);
            o += 4;
            if (!(j < MEM_R32(t))) break;
            LOOP_POLL(0x80033380u);
        }
    }
    u32 q = BP;
    BP = q + 4;
    BP = q + 4 + ((u32)MEM_R16(q + 4) << 2);
    V0 = 1;
#undef BP
#undef NEXT
#undef TYPE
#undef LEFT
}

/* libgs HMD animation, linear key interpolation of a coordinate (ctx):
 * the sequence (ctx+0x14 + index*4: key, from, to, out) at t/n
 * (key+0x10/0x12; n 0: 1), each value (from*t + to*(n-t)) / n: the
 * translation words into the target (key word: block, offset) +0x18,
 * the rotation halves into +0x44 and the matrix (+4) by rot (RotMatrix
 * or RotMatrixYXZ), the scale on the stack and ScaleMatrix; the target's
 * flag cleared, everything copied to out when set. 0. The original's
 * s-registers. */
static void lerp_coord(u32 rot) {
    FRAME(0x40);
    u32 sv[6];
    for (int i = 0; i < 6; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x38, R[31]);
    for (int i = 5; i >= 0; i--) MEM_W32(sp + 0x20 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    const u32 q = c + 0x14 + (MEM_R32(c + 0x14) << 2);
    R[20] = q;
    u32 key = MEM_R32(q);
    if (MEM_R16(key + 0x12) == 0) { V0 = 1; goto out; }
    {
        const s32 t = (s16)MEM_R16(key + 0x10);
        const u32 n = MEM_R16(key + 0x12);
        const s32 u = (s32)n - t;
        R[17] = (u32)t; R[16] = n; R[18] = (u32)u;
#define LW(o) ((u32)((u32)(s32)MEM_R32(MEM_R32(q + 4) + (o)) * (u32)t + (u32)(s32)MEM_R32(MEM_R32(q + 8) + (o)) * (u32)u))
#define LH(o) ((u32)((u32)(s32)(s16)MEM_R16(MEM_R32(q + 4) + (o)) * (u32)t + (u32)(s32)(s16)MEM_R16(MEM_R32(q + 8) + (o)) * (u32)u))
#define DV(x) ((u32)((s32)(x) / (s32)n))
        u32 v = DV(LW(0));
        u32 kw = MEM_R32(key);
        const u32 tg = MEM_R32(c + 0x14 + ((kw >> 24) << 2)) + ((kw & 0x00FFFFFFu) << 2);
        R[21] = tg;
        MEM_W32(tg + 0x18, v);
        MEM_W32(tg + 0x1C, DV(LW(4)));
        MEM_W32(tg + 0x20, DV(LW(8)));
        MEM_W16(tg + 0x44, (u16)DV(LH(0xC)));
        MEM_W16(tg + 0x46, (u16)DV(LH(0xE)));
        MEM_W16(tg + 0x48, (u16)DV(LH(0x10)));
        R[19] = tg + 4;
        gcall(rot, tg + 0x44, tg + 4, R[6], R[7]);
        MEM_W32(sp + 0x10, DV(LH(0x12)));
        MEM_W32(sp + 0x14, DV(LH(0x14)));
        MEM_W32(sp + 0x18, DV(LH(0x16)));
#undef LW
#undef LH
#undef DV
        gcall(0x8002A638u, tg + 4, sp + 0x10, R[6], R[7]);
        MEM_W32(tg, 0);
        u32 o = MEM_R32(q + 0xC);
        if (o) {
            MEM_W32(o, MEM_R32(tg + 0x18));
            MEM_W32(MEM_R32(q + 0xC) + 4, MEM_R32(tg + 0x1C));
            MEM_W32(MEM_R32(q + 0xC) + 8, MEM_R32(tg + 0x20));
            MEM_W16(MEM_R32(q + 0xC) + 0xC, MEM_R16(tg + 0x44));
            MEM_W16(MEM_R32(q + 0xC) + 0xE, MEM_R16(tg + 0x46));
            MEM_W16(MEM_R32(q + 0xC) + 0x10, MEM_R16(tg + 0x48));
            MEM_W16(MEM_R32(q + 0xC) + 0x12, MEM_R16(sp + 0x10));
            MEM_W16(MEM_R32(q + 0xC) + 0x14, MEM_R16(sp + 0x14));
            MEM_W16(MEM_R32(q + 0xC) + 0x16, MEM_R16(sp + 0x18));
        }
        V0 = 0;
    }
out:
    for (int i = 0; i < 6; i++) R[16 + i] = sv[i];
}

/* 80032708 / 80032BF8: lerp_coord with RotMatrix / RotMatrixYXZ. */
void n_gs_LerpCoord(void) { lerp_coord(0x8002AA68u); }
void n_gs_LerpCoordYXZ(void) { lerp_coord(0x80032478u); }

/* 800303A8 (view): libgs's view from an HMD camera (params view+0x14:
 * view point +8, reference by Fit16At8, twist +0x1C, super view+0x18):
 * the screen-twist matrix (0x80065BD0, RotateZDeg), the X then Y axis
 * rotations toward the reference (AxisMatrix / MulMatrixA; none when the
 * points meet), the view point applied, then composed with the inverse
 * of the super coordinate (GetLw, TransposeMatrix); the result into
 * 0x80065BB0 and 0x80065B70. *view + 4. The original's s-registers. */
void n_gs_SetRefViewL(void) {
    FRAME(0xC0);
    u32 sv[6];
    for (int i = 0; i < 6; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0xB4, sv[5]);
    MEM_W32(sp + 0xB0, sv[4]);
    MEM_W32(sp + 0xB8, R[31]);
    MEM_W32(sp + 0xAC, sv[3]);
    MEM_W32(sp + 0xA8, sv[2]);
    MEM_W32(sp + 0xA4, sv[1]);
    MEM_W32(sp + 0xA0, sv[0]);
    const u32 c = A0, M = 0x80065BB0u;
    R[21] = c; R[20] = M;
    const u32 p = MEM_R32(c + 0x14);
    R[19] = p;
    for (u32 i = 0; i < 0x20; i += 4) MEM_W32(M + i, GV(u32, 0x80065BD0u + i));
    gcall(0x800302E8u, M, (u32)-MEM_R32(p + 0x1C), R[6], R[7]);
    gcall(0x80030778u, p, sp + 0x10, R[6], R[7]);
#define SV(o) MEM_R32(sp + (o))
    u32 dx = SV(0x1C) - SV(0x10), dy = SV(0x20) - SV(0x14), dz = SV(0x24) - SV(0x18);
    u32 d = gcall(0x8002A3D8u, dx * dx + dy * dy + dz * dz, R[5], R[6], R[7]);
    R[18] = d;
    if (d != 0) {
        R[17] = SV(0x14) - SV(0x20);
        R[16] = (u32)-mips_div((s32)(R[17] << 12), (s32)d);
        dx = SV(0x1C) - SV(0x10); dz = SV(0x24) - SV(0x18);
        u32 h = gcall(0x8002A3D8u, dx * dx + dz * dz, R[5], R[6], R[7]);
        R[17] = h;
        u32 cs = (u32)mips_div((s32)(h << 12), (s32)d);
        R[18] = sp + 0x30;
        R[16] <<= 16;
        gcall(0x8002E138u, sp + 0x30, (u32)((s32)R[16] >> 16), (u32)(s32)(s16)cs, 0x78);
        R[16] = h;
        gcall(0x8002D4A8u, M, sp + 0x30, R[6], R[7]);
        if (h != 0) {
            R[17] = SV(0x1C) - SV(0x10);
            u32 a5 = (u32)mips_div((s32)(R[17] << 12), (s32)h);
            R[17] = SV(0x24) - SV(0x18);
            u32 a6 = (u32)mips_div((s32)(R[17] << 12), (s32)h);
            gcall(0x8002E138u, sp + 0x30, (u32)(s32)(s16)(0u - a5), (u32)(s32)(s16)a6, 0x79);
            gcall(0x8002D4A8u, M, sp + 0x30, R[6], R[7]);
        }
        R[17] = sp + 0x90;
        MEM_W32(sp + 0x90, (u32)-MEM_R32(p + 8));
        MEM_W32(sp + 0x94, (u32)-MEM_R32(p + 0xC));
        MEM_W32(sp + 0x98, (u32)-MEM_R32(p + 0x10));
        gcall(0x8002D348u, M, sp + 0x90, M + 0x14, R[7]);
        gcall(0x80033728u, MEM_R32(c + 0x18), R[18], R[6], R[7]);
        R[16] = sp + 0x50;
        gcall(0x80030948u, R[18], sp + 0x50, R[6], R[7]);
        gcall(0x8002D348u, sp + 0x50, sp + 0x44, sp + 0x90, R[7]);
        MEM_W32(sp + 0x64, (u32)-SV(0x90));
        MEM_W32(sp + 0x6C, (u32)-SV(0x98));
        MEM_W32(sp + 0x68, (u32)-SV(0x94));
        gcall(0x8002D094u, M, sp + 0x50, R[6], R[7]);
        for (u32 i = 0; i < 0x20; i += 4) MEM_W32(M + i, SV(0x50 + i));
        for (u32 i = 0; i < 0x20; i += 4) GV(u32, 0x80065B70u + i) = MEM_R32(M + i);
    }
#undef SV
    V0 = MEM_R32(c) + 4;
    for (int i = 0; i < 6; i++) R[16 + i] = sv[i];
}

/* 8002BE88 (packet, ot, z, words): link a packet into an OT at z
 * (relative to the OT's base z +8; below it: a message by printf); its
 * length byte words. Returns the end of the packet. The original's
 * s-registers. */
void n_gs_SortPacket(void) {
    FRAME(0x28);
    u32 sv[5];
    for (int i = 0; i < 5; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x10, sv[0]);
    MEM_W32(sp + 0x18, sv[2]);
    MEM_W32(sp + 0x20, sv[4]);
    MEM_W32(sp + 0x1C, sv[3]);
    MEM_W32(sp + 0x24, R[31]);
    MEM_W32(sp + 0x14, sv[1]);
    const u32 p = A0, ot = A1, n = A3;
    R[16] = p; R[18] = ot; R[20] = n; R[19] = n;
    s32 d = (s32)((A2 & 0xFFFF) - MEM_R32(ot + 8));
    R[17] = (u32)d;
    if (d < 0) gcall(0x800398C8u, 0x800106C0u, ot, A2 & 0xFFFF, n);
    u32 slot = MEM_R32(ot + 4) + ((u32)d << 2);
    MEM_W32(p, MEM_R32(slot));
    MEM_W8(p + 3, (u8)n);
    MEM_W32(slot, p);
    MEM_W8(slot + 3, 0);
    V0 = p + ((n & 0xFF) << 2) + 4;
    for (int i = 0; i < 5; i++) R[16 + i] = sv[i];
}

/* 8002A988 RotTransPers3(v0, v1, v2, sxy0, sxy1, sxy2, p, flag): the
 * three projected points, the depth cue (IR0) and flags; returns OTZ. */
void n_gte_RotTransPers3(void) {
    const u32 sp = R[29];
    gte_write_data(0, MEM_R32(A0));
    gte_write_data(1, MEM_R32(A0 + 4));
    gte_write_data(2, MEM_R32(A1));
    gte_write_data(3, MEM_R32(A1 + 4));
    gte_write_data(4, MEM_R32(A2));
    gte_write_data(5, MEM_R32(A2 + 4));
    gte_command(0x0280030u);
    u32 s1 = MEM_R32(sp + 0x10), s2 = MEM_R32(sp + 0x14), pp = MEM_R32(sp + 0x18), fl = MEM_R32(sp + 0x1C);
    gte_swc2(A3, 12);
    gte_swc2(s1, 13);
    gte_swc2(s2, 14);
    u32 f = gte_read_ctrl(31);
    gte_swc2(pp, 8);
    MEM_W32(fl, f);
    gte_command(0x158002Du);
    V0 = gte_read_data(7);
}

/* 800319C8 (unit, verts): an HMD vertex-mime (shared vertex
 * animation) block: per record (unit+0x20 + index*4: key count +2, the
 * reset list +0, its key mask +4, then the key offsets), each key whose
 * bit is set and whose weight (unit+0x14, up to unit+0x18 keys) is not
 * 0 adds its scaled differences into the vertices (800330E8); any
 * applied: the record's reset list flagged 1. Returns the next block.
 * The original's s-registers (the caller-saved ones on the stack round
 * each add, as the original). */
void n_gs_MimeBlock(void) {
    FRAME(0x60);
    u32 sv[8];
    for (int i = 0; i < 8; i++) sv[i] = R[16 + i];
    u32 sfp = R[30];
    MEM_W32(sp + 0x5C, R[31]);
    MEM_W32(sp + 0x58, sfp);
    for (int i = 7; i >= 0; i--) MEM_W32(sp + 0x38 + (u32)i * 4, sv[i]);
    const u32 c = A0;
    MEM_W32(sp + 0x10, A1);
    MEM_W32(sp + 0x14, MEM_R32(c + 0x14));
    MEM_W32(sp + 0x18, MEM_R32(c + 0x18));
    const u32 blk = MEM_R32(c);
    const u32 n = MEM_R16(blk + 2);
    MEM_W32(sp + 0x1C, n);
    const u32 base = MEM_R32(c + 0x20);
    R[20] = 0;
    if (n != 0) {
        R[30] = blk + 4;
        for (;;) {
            u32 any = 0;
            R[16] = 0; R[18] = 0;
            R[17] = base + (MEM_R32(R[30]) << 2);
            R[22] = MEM_R16(R[17] + 2);
            MEM_W16(sp + 0x20, MEM_R16(R[17]));
            R[21] = MEM_R32(R[17] + 4);
            R[23] = R[17] + 8;
            if ((R[22] & 0xFFFF) != 0) {
                R[19] = R[22] & 0xFFFF;
                for (;;) {
                    u32 k = R[18] & 0xFFFF;
                    if (!(k < MEM_R32(sp + 0x18))) break;
                    if (R[21] & (1u << (k & 31))) {
                        u32 wt = MEM_R32(MEM_R32(sp + 0x14) + (k << 2));
                        if (wt != 0) {
                            u32 e = R[17] + (MEM_R32(R[23] + ((R[16] & 0xFFFF) << 2)) << 2);
                            MEM_W32(sp + 0x28, base);
                            MEM_W32(sp + 0x2C, blk);
                            MEM_W32(sp + 0x30, 1);
                            gcall(0x800330E8u, MEM_R32(sp + 0x10) + (MEM_R32(e) << 3), e + 8, MEM_R16(e + 6), wt);
                            any = 1;
                        }
                        R[16]++;
                    }
                    R[18]++;
                    if (!((R[16] & 0xFFFF) < R[19])) break;
                    LOOP_POLL(0x80031AFCu);
                }
            }
            if (any) {
                u32 m = MEM_R16(sp + 0x20);
                if (m != 0) {
                    u32 p = R[17] + (R[22] << 2) + 8;
                    for (s32 i = 0;;) {
                        u32 v = MEM_R32(p);
                        i++;
                        MEM_W16(base + (v << 2), 1);
                        if (!(i < (s32)m)) break;
                        p += 4;
                        LOOP_POLL(0x80031B44u);
                    }
                }
            }
            R[20]++;
            if (!((s32)R[20] < (s32)MEM_R32(sp + 0x1C))) break;
            R[30] += 4;
            LOOP_POLL(0x80031B58u);
        }
    }
    V0 = blk + ((u32)MEM_R16(blk) << 2);
    for (int i = 0; i < 8; i++) R[16 + i] = sv[i];
    R[30] = sfp;
}

/* 80032118 (ctx): lerp_coord without the scale: translation and
 * rotation (RotMatrixYXZ) only. The original's s-registers. */
void n_gs_LerpCoordRT(void) {
    FRAME(0x20);
    u32 s0 = R[16], s1 = R[17];
    MEM_W32(sp + 0x18, R[31]);
    MEM_W32(sp + 0x14, s1);
    MEM_W32(sp + 0x10, s0);
    const u32 c = A0;
    const u32 q = c + 0x14 + (MEM_R32(c + 0x14) << 2);
    R[17] = q;
    u32 key = MEM_R32(q);
    V0 = 1;
    if (MEM_R16(key + 0x12) != 0) {
        const s32 t = (s16)MEM_R16(key + 0x10);
        const u32 n = MEM_R16(key + 0x12);
        const s32 u = (s32)n - t;
#define LW(o) ((u32)((u32)(s32)MEM_R32(MEM_R32(q + 4) + (o)) * (u32)t + (u32)(s32)MEM_R32(MEM_R32(q + 8) + (o)) * (u32)u))
#define LH(o) ((u32)((u32)(s32)(s16)MEM_R16(MEM_R32(q + 4) + (o)) * (u32)t + (u32)(s32)(s16)MEM_R16(MEM_R32(q + 8) + (o)) * (u32)u))
#define DV(x) ((u32)((s32)(x) / (s32)n))
        u32 v = DV(LW(0));
        u32 kw = MEM_R32(key);
        const u32 tg = MEM_R32(c + 0x14 + ((kw >> 24) << 2)) + ((kw & 0x00FFFFFFu) << 2);
        R[16] = tg;
        MEM_W32(tg + 0x18, v);
        MEM_W32(tg + 0x1C, DV(LW(4)));
        MEM_W32(tg + 0x20, DV(LW(8)));
        MEM_W16(tg + 0x44, (u16)DV(LH(0xC)));
        MEM_W16(tg + 0x46, (u16)DV(LH(0xE)));
        MEM_W16(tg + 0x48, (u16)DV(LH(0x10)));
#undef LW
#undef LH
#undef DV
        gcall(0x80032478u, tg + 0x44, tg + 4, R[6], R[7]);
        MEM_W32(tg, 0);
        u32 o = MEM_R32(q + 0xC);
        if (o) {
            MEM_W32(o, MEM_R32(tg + 0x18));
            MEM_W32(MEM_R32(q + 0xC) + 4, MEM_R32(tg + 0x1C));
            MEM_W32(MEM_R32(q + 0xC) + 8, MEM_R32(tg + 0x20));
            MEM_W16(MEM_R32(q + 0xC) + 0xC, MEM_R16(tg + 0x44));
            MEM_W16(MEM_R32(q + 0xC) + 0xE, MEM_R16(tg + 0x46));
            MEM_W16(MEM_R32(q + 0xC) + 0x10, MEM_R16(tg + 0x48));
        }
        V0 = 0;
    }
    R[16] = s0; R[17] = s1;
}

/* 8002C428 ReadGeomScreen: the projection distance H. */
void n_gte_ReadGeomScreen(void) { V0 = gte_read_ctrl(26); }

/* 8002C438: libgs's RotTransPers4 for sprites: as 8002A9E8 but returns
 * the fourth point's depth / 4 (SZ3) instead of the average. */
void n_gs_RotTransPers4Z(void) {
    u32 sp = R[29];
    gte_write_data(0, MEM_R32(A0));
    gte_write_data(1, MEM_R32(A0 + 4));
    gte_write_data(2, MEM_R32(A1));
    gte_write_data(3, MEM_R32(A1 + 4));
    gte_write_data(4, MEM_R32(A2));
    gte_write_data(5, MEM_R32(A2 + 4));
    gte_command(0x0280030u);
    gte_swc2(MEM_R32(sp + 0x10), 12);
    gte_swc2(MEM_R32(sp + 0x14), 13);
    gte_swc2(MEM_R32(sp + 0x18), 14);
    u32 f = gte_read_ctrl(31);
    gte_write_data(0, MEM_R32(A3));
    gte_write_data(1, MEM_R32(A3 + 4));
    gte_command(0x0180001u);
    gte_swc2(MEM_R32(sp + 0x1C), 14);
    gte_swc2(MEM_R32(sp + 0x20), 8);
    u32 f2 = gte_read_ctrl(31);
    u32 z = gte_read_data(19);
    MEM_W32(MEM_R32(sp + 0x24), f2 | f);
    V0 = (u32)((s32)z >> 2);
}

/* 80041C38 (in, out): libgte's VectorNormal (32-bit vectors, as the
 * driver file's own copy): the squared length by SQR, its leading zeros
 * (LZCS), the reciprocal square root from the table at 0x80062B90, the
 * vector scaled by it (GPF) and shifted back. Returns the squared
 * length. */
void n_gte_VectorNormalD(void) {
    const u32 in = A0, out = A1;
    u32 x = MEM_R32(in), y = MEM_R32(in + 4), z = MEM_R32(in + 8);
    gte_write_data(9, x);
    gte_write_data(10, y);
    gte_write_data(11, z);
    gte_command(0x0A00428u);
    u32 sq = gte_read_data(25) + gte_read_data(26) + gte_read_data(27);
    gte_write_data(30, sq);
    u32 lz = gte_read_data(31) & 0xFFFFFFFEu;
    u32 sh = (u32)((s32)(0x1F - lz) >> 1);
    s32 k = (s32)lz - 0x18;
    u32 m = k < 0 ? (u32)((s32)sq >> ((0x18 - lz) & 31)) : sq << (k & 31);
    u32 r = (u32)(s32)(s16)GV(u16, 0x80062B90u + ((m - 0x40) << 1));
    gte_write_data(8, r);
    gte_write_data(9, x);
    gte_write_data(10, y);
    gte_write_data(11, z);
    gte_command(0x190003Du);
    MEM_W32(out, (u32)((s32)gte_read_data(25) >> (sh & 31)));
    MEM_W32(out + 4, (u32)((s32)gte_read_data(26) >> (sh & 31)));
    MEM_W32(out + 8, (u32)((s32)gte_read_data(27) >> (sh & 31)));
    V0 = sq;
}

/* 8002D838 (id, light): GsSetFlatLight: light id (0-2; others leave
 * things as they are): its direction normalised and negated (<< 12) into
 * row id of the light matrix kept at 0x80065B30, its colour (r, g, b
 * bytes at +0xC; * 4096 / 255) into column id of the GTE's colour matrix
 * (read with 8002DD54, set with 8002DCF0). Returns -1 for a zero-length
 * direction, else 0. */
void n_gte_SetFlatLight(void) {
    FRAME(0x90);
    u32 sv[5];
    for (int i = 0; i < 5; i++) sv[i] = R[16 + i];
    MEM_W32(sp + 0x7C, sv[1]);
    MEM_W32(sp + 0x78, sv[0]);
    MEM_W32(sp + 0x8C, R[31]);
    MEM_W32(sp + 0x88, sv[4]);
    MEM_W32(sp + 0x84, sv[3]);
    MEM_W32(sp + 0x80, sv[2]);
    const u32 id = A0, lt = A1;
    R[17] = id; R[16] = lt;
    const u32 cr = MEM_R8(lt + 0xC), cg = MEM_R8(lt + 0xD), cb = MEM_R8(lt + 0xE);
    R[18] = cr; R[19] = cg; R[20] = cb;
    for (u32 k = 0; k < 0x20; k += 4) MEM_W32(sp + 0x10 + k, GV(u32, 0x80065B30u + k));
    gcall(0x8002DD54u, sp + 0x30, R[5], R[6], R[7]);
    s32 x = (s32)MEM_R32(lt), y = (s32)MEM_R32(lt + 4), z = (s32)MEM_R32(lt + 8);
    s32 len = (s32)gcall(0x8002A3D8u, (u32)x * (u32)x + (u32)y * (u32)y + (u32)z * (u32)z, R[5], R[6], R[7]);
    u32 ret;
    if (len == 0) ret = 0xFFFFFFFFu;
    else {
        if (id < 3) {
            const u32 row = sp + 0x10 + id * 6;
            MEM_W16(row, (u16)mips_div((s32)((u32)-(s32)MEM_R32(lt) << 12), len));
            MEM_W16(row + 2, (u16)mips_div((s32)((u32)-(s32)MEM_R32(lt + 4) << 12), len));
            s32 vz = mips_div((s32)((u32)-(s32)MEM_R32(lt + 8) << 12), len);
            const u32 col = sp + 0x30 + id * 2;
            MEM_W16(col, (u16)((s32)(cr << 12) / 255));
            MEM_W16(col + 6, (u16)((s32)(cg << 12) / 255));
            MEM_W16(col + 0xC, (u16)((s32)(cb << 12) / 255));
            MEM_W16(row + 4, (u16)vz);
        }
        for (u32 k = 0; k < 0x20; k += 4) GV(u32, 0x80065B30u + k) = MEM_R32(sp + 0x10 + k);
        gcall(0x8002DCF0u, sp + 0x30, R[5], R[6], R[7]);
        ret = 0;
    }
    V0 = ret;
    for (int i = 0; i < 5; i++) R[16 + i] = sv[i];
}

/* 8002A278 (y): an angle by 12 CORDIC steps from (4096, y), the step
 * angles at 0x8005C02C. */
void n_gte_CordicAngle(void) {
    s32 x = 0x1000, y = (s32)A0, a = 0;
    for (int i = 0; i < 12; i++) {
        const s32 t = (s32)GV(u32, 0x8005C02Cu + (u32)i * 4);
        s32 nx, ny;
        if (y < 0) { nx = x - (y >> i); ny = y + (x >> i); a -= t; }
        else { nx = x + (y >> i); ny = y - (x >> i); a += t; }
        x = nx; y = ny;
        if (i < 11) LOOP_POLL(0x8002A334u);
    }
    V0 = (u32)a;
}
