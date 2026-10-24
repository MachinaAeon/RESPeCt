/* The player's death and the continue screen (TEST overlay, called from
 * the game-over sequence at 0x8008369C).
 *
 * Death: the room is hidden and a 16x8 grid of textured quads (the
 * picture loaded by death_LoadImage) is drawn through the GTE with a
 * coordinate system at 0x800B5FD0; death_Update ripples it outward from
 * the centre while the camera pulls back. State block at 0x800B5FC8:
 *   +0/+4 the quads (one set per frame buffer), +8 the coordinate system
 *   (GsCOORDINATE2-like, matrix at +0xC, translation +0x20), +0x58 scale,
 *   +0x94 ripple phase, +0x98 ripple speed, +0x9C vertex grid (17x9).
 *
 * Continue: a "CONTINUE" banner, a two-digit countdown, a row of
 * continue-left marks and two background halves, 26 quads per frame
 * buffer at 0x800B6068[buf]. D_800B6070 counts down from 0x275 frames
 * (60 a second; the attack button skips to the next second, fire /
 * Start takes a continue), D_800B6074 times the flash of the used mark,
 * D_800B6078 is the result (0 counting, 1 gave up, 3 flashing, 2
 * continued), D_800B608C the continues left. A heartbeat speeds up as
 * time runs out.
 *
 * Quads are POLY_FT4 (0x28 bytes): +4 rgb, +7 code, +8/+0x10/+0x18/+0x20
 * xy, +0xC/+0x14/+0x1C/+0x24 uv, +0xE clut, +0x16 tpage.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "poll.h"

#define DEATH            0x800B5FC8u
#define DEATH_COORD      (DEATH + 8)
#define DEATH_SCALE      (DEATH + 0x58)
#define DEATH_PHASE      GV(s32, DEATH + 0x94)
#define DEATH_SPEED      GV(s32, DEATH + 0x98)
#define DEATH_GRID       GV(u32, DEATH + 0x9C)
#define CAMERA_EYE       0x800B3EE8u
#define CONT_PRIMS       0x800B6068u
#define g_contTimer      GV(s32, 0x800B6070)
#define g_contFlash      GV(s32, 0x800B6074)
#define g_contResult     GV(s32, 0x800B6078)
#define g_contBeatState  GV(s32, 0x800B607C)
#define g_contLeft       GV(s32, 0x800B608C)
#define g_contUsed       GV(u8, 0x80062DBE)
#define g_beatFrames     GV(s32, 0x800B4DD8)
#define g_beatPitch      GV(s32, 0x800B4DDC)
#define g_beatInterval   GV(s32, 0x800B4DE0)
#define g_volume         GV(s16, 0x80062DB6)
#define g_frameBuf       GV(u8, 0x80062D1C)
#define ORDER_TABLES     0x800AB530u   /* 0x2000 bytes per frame buffer */
#define IDENTITY_MATRIX  0x80065B90u
#define IMAGE_BUF        0x80150000u

#define F_LOAD_FILE      0x800115C0u   /* (name, file, table, index, [dest]) */
#define F_UPLOAD_TIMS    0x8006F370u
#define F_LOAD_IMAGE     0x80035620u
#define F_DRAW_SYNC      0x80035374u
#define F_GET_LS         0x800339E8u
#define F_SET_LS         0x8002CF14u
#define F_SQRT           0x8002A3D8u
#define F_RCOS           0x80029FF8u
#define F_ROT_TRANS_PERS4 0x8002A9E8u
#define F_SOUND_AT       0x80080524u

/* libgpu's little helpers */
static void set_poly_ft4(u32 p) { GV(u8, p + 3) = 9; GV(u8, p + 7) = 0x2C; }
static void set_semi_trans(u32 p, s32 on) { GV(u8, p + 7) = on ? GV(u8, p + 7) | 2 : GV(u8, p + 7) & 0xFD; }
static void set_shade_tex(u32 p, s32 on) { GV(u8, p + 7) = on ? GV(u8, p + 7) | 1 : GV(u8, p + 7) & 0xFE; }
static u16 get_clut(s32 x, s32 y) { return (u16)((y << 6) | ((x >> 4) & 0x3F)); }
static u16 get_tpage(s32 tp, s32 abr, s32 x, s32 y) {
    return (u16)(((tp & 3) << 7) | ((abr & 3) << 5) | ((y & 0x100) >> 4) | ((x & 0x3FF) >> 6) | ((y & 0x200) << 2));
}
static void add_prim(u32 ot, u32 p) {
    GV(u32, p) = (GV(u32, p) & 0xFF000000u) | (GV(u32, ot) & 0xFFFFFFu);
    GV(u32, ot) = (GV(u32, ot) & 0xFF000000u) | (p & 0xFFFFFFu);
}
static u32 ot_front(void) { return ORDER_TABLES + ((u32)g_frameBuf << 13); }

static void set_rgb(u32 p, u8 c) { GV(u8, p + 4) = c; GV(u8, p + 5) = c; GV(u8, p + 6) = c; }
static void set_xy(u32 p, s16 x0, s16 y0, s16 x1, s16 y1) {
    GV(s16, p + 8) = x0;    GV(s16, p + 0xA) = y0;
    GV(s16, p + 0x10) = x1; GV(s16, p + 0x12) = y0;
    GV(s16, p + 0x18) = x0; GV(s16, p + 0x1A) = y1;
    GV(s16, p + 0x20) = x1; GV(s16, p + 0x22) = y1;
}
static void set_uv(u32 p, u8 u0, u8 v0, u8 u1, u8 v1) {
    GV(u8, p + 0xC) = u0;  GV(u8, p + 0xD) = v0;
    GV(u8, p + 0x14) = u1; GV(u8, p + 0x15) = v0;
    GV(u8, p + 0x1C) = u0; GV(u8, p + 0x1D) = v1;
    GV(u8, p + 0x24) = u1; GV(u8, p + 0x25) = v1;
}

/* sp: the caller's frame (argument 5 goes at sp + 0x10). */
static void load_file(u32 sp, u32 name, u32 file, u32 table, u32 index, u32 dest) {
    GS(u32, sp + 0x10) = dest;
    GCALL4(F_LOAD_FILE, name, file, table, index);
}

/* 8007A4F8: load the death picture and upload it. */
void n_death_LoadImage(void) {
    FRAME(0x20);
    load_file(sp, 0x8006C2FC, 0x80058530, 0x800A1074, 0x73, IMAGE_BUF);
    GCALL1(F_UPLOAD_TIMS, IMAGE_BUF);
}

/* 8007A540: set up the death scene: hide the room's objects, reset the
 * camera and player, build the quads. */
void n_death_Setup(void) {
    FRAME(0x38);
    for (s32 i = 13; i >= 0; i--) {
        GV(u8, 0x800A89C0 + (u32)i * 0x318 + 0x29) = 1;
        if (i > 0) LOOP_POLL(0x8007A588);
    }
    for (s32 i = 0; i < GV(s32, 0x800AF530); i++) {
        GV(u8, GV(u32, 0x800AB510) + (u32)i * 0x8C4 + 0x29) = 0;
        if (i + 1 < GV(s32, 0x800AF530)) LOOP_POLL(0x8007A5C8);
    }

    GV(s32, CAMERA_EYE) = 0;
    GV(s32, CAMERA_EYE + 4) = 0;
    GV(s32, CAMERA_EYE + 8) = -0xFA0;
    for (u32 k = 0xC; k <= 0x18; k += 4) GV(s32, CAMERA_EYE + k) = 0;
    for (u32 k = 0; k < 6; k += 2) GV(s16, g_player + 0x44 + k) = 0;
    for (u32 k = 0; k < 12; k += 4) GV(s32, g_player + 0x18 + k) = 0;
    for (u32 k = 0; k < 6; k += 2) GV(s16, CAMERA_OBJ + 0x44 + k) = 0;
    for (u32 k = 0; k < 12; k += 4) GV(s32, CAMERA_OBJ + 0x18 + k) = 0;

    GV(u32, DEATH) = 0x80150000;
    GV(u32, DEATH + 4) = 0x80151400;
    DEATH_GRID = 0x80152800;
    GV(s16, DEATH + 0x8C) = 0;
    DEATH_PHASE = 0;
    DEATH_SPEED = 0;

    for (s32 buf = 0; buf < 2; buf++) {
        u32 p = GV(u32, DEATH + (u32)buf * 4);
        for (s32 i = 0; i < 128; i++, p += 0x28) {
            set_poly_ft4(p);
            set_semi_trans(p, 0);
            set_shade_tex(p, 1);
            set_rgb(p, 0x80);
            s32 v = i / 16 * 16;
            s32 u = (i - v) << 4;
            GV(u8, p + 0xC) = (u8)u;
            GV(u8, p + 0xD) = (u8)v;
            GV(u8, p + 0x14) = u == 0xF0 ? 0xFF : (u8)(u + 0x10);
            GV(u8, p + 0x15) = (u8)v;
            GV(u8, p + 0x1C) = (u8)u;
            GV(u8, p + 0x1D) = v == 0xF0 ? 0xFF : (u8)(v + 0x10);
            GV(u8, p + 0x24) = u == 0xF0 ? 0xFF : (u8)(u + 0x10);
            GV(u8, p + 0x25) = v == 0xF0 ? 0xFF : (u8)(v + 0x10);
            GV(u16, p + 0xE) = get_clut(0x140, 0x1E0);
            GV(u16, p + 0x16) = get_tpage(1, 0, 0x380, 0);
            if (i + 1 < 128) LOOP_POLL(0x8007A774);
        }
        if (buf + 1 < 2) LOOP_POLL(0x8007A784);
    }

    u32 c = DEATH_COORD;
    GV(u32, c) = 0;
    for (u32 k = 0; k < 0x20; k += 4) GV(u32, c + 4 + k) = GV(u32, IDENTITY_MATRIX + k);
    for (u32 k = 0; k < 0x20; k += 4) GV(u32, c + 0x24 + k) = GV(u32, IDENTITY_MATRIX + k);
    GV(s16, c + 0x44) = 0;
    GV(s16, c + 0x46) = 0;
    GV(s16, c + 0x48) = 0;
    GV(u32, c + 0x4C) = 0;
    GV(s32, DEATH_SCALE + 8) = 0x1000;
    GV(s32, DEATH_SCALE + 4) = 0x1000;
    GV(s32, DEATH_SCALE) = 0x1000;
    V0 = 0x1000;
}

/* 8007A874: a frame of the death scene: ripple the grid (a cosine of the
 * distance from the centre behind the wave front), project it into the
 * quads, then pull the camera back, speed the ripple up, advance it. */
void n_death_Update(void) {
    FRAME(0x90);
    u32 grid = DEATH_GRID;
    GV(s32, DEATH + 0x20) = 0;
    GV(s32, DEATH + 0x24) = 0;
    GV(s32, DEATH + 0x28) = 0;
    GCALL2(F_OBJ_MATRIX, DEATH_COORD, DEATH_SCALE);

    u32 mat = sp + 0x28, vec = sp + 0x48;
    GCALL2(F_GET_LS, DEATH_COORD, mat);
    GCALL1(F_SET_LS, mat);

    for (s32 i = 0; i < 0x99; i++) {          /* 17 x 9 vertices */
        s32 x = (i % 17) * 16 - 0x80;
        s32 y = (i / 17) * 16 - 0x20;
        s32 d = (s32)GCALL1(F_SQRT, (u32)(x * x + y * y));
        s32 z = 0;
        if (d < DEATH_PHASE) z = (s32)GCALL1(F_RCOS, (u32)((d - DEATH_PHASE) << 6)) >> 7;
        u32 g = grid + (u32)i * 6;
        GV(s16, g) = (s16)(x * 10);
        GV(s16, g + 2) = (s16)(y * 10);
        GV(s16, g + 4) = (s16)(z * 10);
        if (i + 1 < 0x99) LOOP_POLL(0x8007A988);
    }

    for (s32 i = 0; i < 128; i++) {
        s32 idx = (i & 15) + (i >> 4) * 17;
        u32 g = grid + (u32)idx * 6;
        static const u32 corner[4] = {0, 6, 0x66, 0x6C};   /* the quad's four vertices */
        for (s32 k = 0; k < 4; k++)
            for (u32 j = 0; j < 6; j += 2) GS(u16, vec + (u32)k * 8 + j) = GV(u16, g + corner[k] + j);
        u32 p = GV(u32, DEATH + (u32)g_frameBuf * 4) + (u32)i * 0x28;
        GS(u32, sp + 0x10) = p + 8;
        GS(u32, sp + 0x14) = p + 0x10;
        GS(u32, sp + 0x18) = p + 0x18;
        GS(u32, sp + 0x1C) = p + 0x20;
        GS(u32, sp + 0x20) = sp + 0x68;
        GS(u32, sp + 0x24) = sp + 0x6C;
        s32 otz = (s32)GCALL4(F_ROT_TRANS_PERS4, vec, vec + 8, vec + 0x10, vec + 0x18);
        if (i >= 0x30) {                      /* rows 3+: texture rows by speed */
            s32 speed = DEATH_SPEED;
            u32 q = GV(u32, DEATH + (u32)g_frameBuf * 4) + (u32)i * 0x28;
            s32 dv = mips_div(0x200, speed + 0x10);
            s32 r = i - 0x30;
            if (r < 0) r = i - 0x21;
            s32 v = (r >> 4) * dv + 0x30;
            GV(u8, q + 0x15) = (u8)v;
            GV(u8, q + 0xD) = (u8)v;
            GV(u8, q + 0x25) = (u8)(v + dv);
            GV(u8, q + 0x1D) = (u8)(v + dv);
        }
        add_prim(ot_front() + (u32)((otz >> 3) << 2), GV(u32, DEATH + (u32)g_frameBuf * 4) + (u32)i * 0x28);
        if (i + 1 < 128) LOOP_POLL(0x8007AB64);
    }

    s32 eye_z = GV(s32, CAMERA_EYE + 8);
    if (eye_z < -0x9C4) GV(s32, CAMERA_EYE + 8) = (s32)(V0 = (u32)(eye_z + 0xF));
    else if (DEATH_SPEED < 0x40) DEATH_SPEED = (s32)(V0 = (u32)(DEATH_SPEED + 1));
    else {
        s32 ph = DEATH_PHASE + 1;
        DEATH_PHASE = ph;
        V0 = ph > 0x7FFF;
        if (ph > 0x7FFF) DEATH_PHASE = (s32)(V0 = 0x4000);
    }
}

/* 8007ABF0: load and upload the continue screen's two pictures. */
void n_continue_LoadImages(void) {
    FRAME(0x30);
    load_file(sp, 0x8006C310, 0x80058538, 0x800A1260, 0, IMAGE_BUF);
    GCALL1(F_UPLOAD_TIMS, IMAGE_BUF);
    load_file(sp, 0x8006C310, 0x80058538, 0x800A1260, 1, IMAGE_BUF);
    GCALL1(F_UPLOAD_TIMS, IMAGE_BUF);
}

static void cont_quad(u32 p, s32 clut_y, s32 abr, s32 semi) {
    set_poly_ft4(p);
    set_rgb(p, 0x80);
    if (semi) GV(u8, p + 7) |= 2;
    GV(u16, p + 0xE) = get_clut(0x140, clut_y);
    GV(u16, p + 0x16) = get_tpage(1, abr, 0x2C0, 0x100);
}

/* 8007AC80: set up the continue screen: palettes to VRAM, the quads for
 * both frame buffers, the countdown (none without continues left). */
void n_continue_Setup(void) {
    FRAME(0x48);
    u32 rect = sp + 0x10;                     /* local RECT */
    for (u32 k = 0; k < 8; k++) GS(u8, rect + k) = GV(u8, 0x8006C324 + k);
    GV(u32, CONT_PRIMS) = 0x80150000;
    GV(u32, CONT_PRIMS + 4) = 0x80150640;
    GS(s32, sp + 0x18) = 0;                   /* the loop counter lives on the stack */
    for (s32 i = 0; i < 0x20; i++) {          /* the palette, 32 times along */
        GCALL2(F_LOAD_IMAGE, rect, 0x800A1330);
        GS(u16, rect) += 8;
        GS(s32, sp + 0x18) = i + 1;
        if (i + 1 < 0x20) LOOP_POLL(0x8007AD1C);
    }
    GS(u16, rect) = 0x230;
    GCALL2(F_LOAD_IMAGE, rect, 0x800A1340);
    GS(u16, rect) += 8;
    GS(s32, sp + 0x18) = 0;
    GCALL2(F_LOAD_IMAGE, rect, 0x800A1340);
    GCALL1(F_DRAW_SYNC, 0);

    for (s32 buf = 0; buf < 2; buf++) {
        u32 p = GV(u32, CONT_PRIMS + (u32)buf * 4);
        for (s32 n = 0; n < 2; n++) {         /* banner and the two digits */
            cont_quad(p, n + 0x1E1, n + 1, 1);
            set_xy(p, -0x70, -0x48, 0x58, -0x30);
            set_uv(p, 0, 8, 0xC8, 0x20);
            p += 0x28;
            cont_quad(p, n + 0x1E1, n + 1, 1);
            set_xy(p, 0x48, -0x49, 0x60, -0x31);
            set_uv(p, 0, 0x30, 0x18, 0x48);
            p += 0x28;
            cont_quad(p, n + 0x1E1, n + 1, 1);
            set_xy(p, 0x60, -0x49, 0x78, -0x31);
            set_uv(p, 0, 0x30, 0x18, 0x48);
            p += 0x28;
            if (n + 1 < 2) LOOP_POLL(0x8007AF9C);
        }
        for (s32 n = 0; n < 2; n++) {         /* the "game over" text */
            cont_quad(p, n + 0x1E1, n + 1, 1);
            set_xy(p, -0x80, -0x48, 0x80, 8);
            set_uv(p, 0, 0x50, 0xFF, 0xA0);
            p += 0x28;
            if (n + 1 < 2) LOOP_POLL(0x8007B064);
        }
        for (s32 n = 0; n < 2; n++) {         /* the continues-left marks */
            s32 x = -0x90;
            for (s32 m = 0; m < 8; m++, x += 0x10) {
                cont_quad(p, n + 0x1E1, n + 1, 1);
                set_xy(p, (s16)x, 0x50, (s16)(x + 0x10), 0x68);
                set_uv(p, 0xD8, 8, 0xE8, 0x20);
                p += 0x28;
                if (m + 1 < 8) LOOP_POLL(0x8007B140);
            }
            if (n + 1 < 2) LOOP_POLL(0x8007B150);
        }
        for (s32 n = 0; n < 2; n++) {         /* the background, two halves */
            set_poly_ft4(p);
            set_rgb(p, 0x80);
            set_xy(p, (s16)(-0xA0 + n * 0x100), -0x78, (s16)(0x60 + n * 0x40), 0x78);
            set_uv(p, 0, 0, (u8)(-1 - n * 0xBF), 0xF0);
            GV(u16, p + 0xE) = get_clut(0x140, 0x1E0);
            GV(u16, p + 0x16) = get_tpage(1, 0, 0x340 + n * 0x80, 0x100);
            p += 0x28;
            if (n + 1 < 2) LOOP_POLL(0x8007B218);
        }
        GS(s32, sp + 0x18) = buf + 1;
        if (buf + 1 < 2) LOOP_POLL(0x8007B230);
    }
    g_contTimer = g_contLeft > 0 ? 0x275 : -1;
    g_contResult = 0;
    g_contFlash = 0xC;
    g_contBeatState = 0;
    V0 = 0x800B0000u;
}

/* Skip to the next second of the countdown. */
static void skip_second(void) {
    u32 t = (u32)(g_contTimer - 0x3C);
    if (t < 0x20C) g_contTimer = (s32)t / 60 * 60 + 0x2C;
}

static s32 pressed(s32 b) { return (g_buttons & b) && !(g_buttonsPrev & b); }

/* Brightness of a digit: fades in and out over the second. */
static s32 digit_fade(s32 f) {
    s32 c = f < 0x1E ? f * 8 : 0x1E0 - f * 8;
    return c > 0x80 ? 0x80 : c;
}

/* 8007B2A8: a frame of the continue screen; returns the result. */
void n_continue_Update(void) {
    FRAME(0x50);
    s32 bright = 0, tens = 0, ones = 0, tens_c = 0, ones_c = 0, over_c = 0;

    if (g_contResult == 0) {
        s32 pad = (s32)g_padTypeBit;
        s32 choose = 0, input = 1;
        if (pad == 0x200000) {                /* GunCon: A or B skips, trigger continues */
            if (pressed(0x800) || pressed(0x40)) skip_second();
            choose = pressed(0x20);
        } else if (pad == 0x400000 || pad == 0x80000) {
            if (pressed(0x40)) skip_second();
            choose = pressed(0x20) || pressed(0x800);
        } else {
            input = 0;
        }
        if (input && choose) {
            s32 left = g_contLeft;
            if (left > 0 && g_contTimer >= 0) {
                g_contResult = 3;
                g_contLeft = left - 1;
                g_contUsed++;
                GCALL4(F_SOUND_CUE, 6, 2, 0x7F, 0x7F);
                GCALL4(F_SOUND_CUE, 6, 3, 0x7F, 0x7F);
            }
            if (g_contTimer < -0x60) g_contResult = 1;
        }
        if (g_contTimer < -0x14C) g_contResult = 1;
    }

    s32 t = g_contTimer;
    if ((u32)t < 0x258) {
        s32 sec = t / 60;
        tens = sec / 10 - 1;
        ones = sec - sec / 10 * 10 - 1;
        s32 f = t - sec * 60;
        if (tens >= 0) tens_c = digit_fade(f);
        ones_c = digit_fade(f);
        if (tens < 0) tens = 9;
        if (ones < 0) ones = 9;
        t = g_contTimer;
        bright = t >= 0x247 ? (0x257 - t) << 3 : t >= 0x10 ? 0x80 : t << 3;
    } else if (t < -0x20) {
        over_c = -((t + 0x20) * 2);
        if (over_c > 0x80) over_c = 0x80;
    }
    if (g_contTimer == -0x1E) {
        GCALL4(F_SOUND_CUE, 6, 4, 0x7F, 0x7F);
        GCALL4(F_SOUND_CUE, 6, 5, 0x7F, 0x7F);
    }
    s32 fl = g_contFlash;
    s32 flash_c = fl < 8 ? fl << 4 : (0xC - fl) << 5;

    u32 p = GV(u32, CONT_PRIMS + (u32)g_frameBuf * 4);
    for (s32 n = 0; n < 2; n++) {
        set_rgb(p, (u8)bright);               /* banner */
        add_prim(ot_front(), p);
        p += 0x28;
        s32 u = tens * 24;                    /* tens digit */
        set_uv(p, (u8)u, 0x30, (u8)(u + 0x18), 0x48);
        set_rgb(p, (u8)tens_c);
        add_prim(ot_front(), p);
        p += 0x28;
        u = ones * 24;                        /* ones digit */
        set_uv(p, (u8)u, 0x30, (u8)(u + 0x18), 0x48);
        set_rgb(p, (u8)ones_c);
        add_prim(ot_front(), p);
        p += 0x28;
        if (n + 1 < 2) LOOP_POLL(0x8007B890);
    }
    for (s32 n = 0; n < 2; n++) {
        set_rgb(p, (u8)over_c);
        add_prim(ot_front(), p);
        p += 0x28;
        if (n + 1 < 2) LOOP_POLL(0x8007B8EC);
    }
    for (s32 n = 0; n < 2; n++) {
        for (s32 m = 0; m < 8; m++, p += 0x28) {
            s32 left = g_contLeft;
            if (m < left) {
                set_rgb(p, (u8)bright);
                add_prim(ot_front(), p);
            } else if (left == m) {           /* the continue being used flashes */
                if (g_contResult == 3 && n == 0) {
                    set_rgb(p, (u8)(g_contFlash < 8 ? flash_c : bright));
                    add_prim(ot_front(), p);
                } else if (g_contLeft == m && g_contResult == 3 && n > 0) {
                    GV(u8, p + 7) &= 0xFD;
                    u16 tp = get_tpage(1, 1, 0x2C0, 0x100);
                    set_rgb(p, (u8)flash_c);
                    GV(u16, p + 0x16) = tp;
                    add_prim(ot_front(), p);
                }
            }
            if (m + 1 < 8) LOOP_POLL(0x8007BA00);
        }
        if (n + 1 < 2) LOOP_POLL(0x8007BA10);
    }
    for (s32 n = 0; n < 2; n++, p += 0x28) {
        add_prim(ot_front(), p);
        if (n + 1 < 2) LOOP_POLL(0x8007BA44);
    }

    s32 r = g_contResult;
    if (r == 0) g_contTimer--;
    if (r == 3) {
        s32 v = g_contFlash - 1;
        g_contFlash = v;
        if (v < 0) g_contResult = 2;
    }
    if (g_contTimer > 0) {
        if (g_contBeatState == 0) { g_beatFrames = 0; g_contBeatState = 1; }
        if (g_contBeatState == 1) {           /* heartbeat */
            s32 x = 0x275 - g_contTimer;
            g_beatPitch = 0x3C - x / 60;
            g_beatInterval = (s32)((u32)x * (u32)x) / 7000 + 0x1E;
            if (g_beatInterval < g_beatFrames) {
                for (s32 s = 0; s < 2; s++) {
                    s32 vol = g_volume * 127 / 100;
                    GS(s32, sp + 0x10) = vol;  /* arguments 5 and 6: left, right */
                    GS(s32, sp + 0x14) = vol;
                    GCALL4(F_SOUND_AT, 6, s, (u32)g_beatPitch, 0x7F);
                }
                g_beatFrames = 0;
            }
        }
        g_beatFrames++;
    }
    V0 = (u32)g_contResult;
}
