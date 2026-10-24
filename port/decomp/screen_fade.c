/* Screen fades (TEST overlay): a full-screen tile drawn over everything,
 * its colour stepped every frame.
 *
 * The fade tile is at 0x800BBCF0 (+0 tag/code, +4 s16 x, +6 s16 y, +8 w,
 * +A h, +C r, g, b), a second, dimming one at 0x800BBD00 (drawn while
 * 0x800BBD10 is set, colour 0x800A55A4). 0x800A55A2 is the fade state: low
 * byte 1 fading in (towards black = 0), 2 fading out (towards 255), 4 held
 * fully covered; high byte the speed. 0x800BBCE0 is the level. 0x800A55A0
 * says the screen is 640 wide.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "game_stack.h"
#include "poll.h"

void n_fade_SetupTiles(void);
void n_fade_StartIn(void);
void n_fade_StartOut(void);
void n_fade_Step(void);
void n_fade_Draw(void);
void n_white_Init(void);
void n_white_Draw(void);
void n_op_WhiteIn(void);
void n_op_WhiteOut(void);
void n_op_Fade(void);

#define FADE_TILE       0x800BBCF0u
#define DIM_TILE        0x800BBD00u
#define g_fadeState     GV(u16, 0x800A55A2)
#define g_fadeLevel     GV(u16, 0x800BBCE0)

/* 80090AB8 (white): set up both tiles to cover the screen; the fade tile
 * white (subtractive, code 0x50) or black (0x60). */
void n_fade_SetupTiles(void) {
    u32 white = A0;
    GV(u32, FADE_TILE) = white ? 0x50000000u : 0x60000000u;
    if (GV(u8, 0x800A55A0) == 1) {                       /* 640 wide */
        GV(u16, FADE_TILE + 8) = 0x280;
        GV(u16, FADE_TILE + 0xA) = 0xF0;
        GV(s16, FADE_TILE + 4) = -0x140;
    } else {
        GV(u16, FADE_TILE + 8) = 0x140;
        GV(u16, FADE_TILE + 0xA) = 0xF0;
        GV(s16, FADE_TILE + 4) = -0xA0;
    }
    GV(s16, FADE_TILE + 6) = -0x78;
    u8 c = white ? 0xFF : 0;
    GV(u8, FADE_TILE + 0xC) = c;
    GV(u8, FADE_TILE + 0xD) = c;
    GV(u8, FADE_TILE + 0xE) = c;
    GV(u16, DIM_TILE + 8) = 0x140;
    GV(u16, DIM_TILE + 0xA) = 0xF0;
    GV(s16, DIM_TILE + 4) = -0xA0;
    GV(s16, DIM_TILE + 6) = -0x78;
    V0 = (u32)-0x78;
}

/* 80090B90 (white, speed): fade in from fully covered (from 0x4FF, a
 * longer hold, if 0x800A8954 asked for it). */
void n_fade_StartIn(void) {
    FRAME(0x18);
    u32 speed = A1;
    n_fade_SetupTiles();
    g_fadeState = (u16)((speed << 8) | 5);
    g_fadeLevel = GV(u32, 0x800A8954) ? 0x4FF : 0xFF;
    GV(u32, 0x800A8954) = 0;
    V0 = 0x800B0000u;                                    /* as the original leaves it */
}

/* 80090BF0 (white, speed): fade out from clear. */
void n_fade_StartOut(void) {
    FRAME(0x18);
    u32 speed = A1;
    n_fade_SetupTiles();
    g_fadeState = (u16)((speed << 8) | 6);
    g_fadeLevel = 0;
    V0 = 0x800C0000u;                                    /* as the original leaves it */
}

/* 80090C90: step the fade and set the tile's colour. */
void n_fade_Step(void) {
    u16 st = g_fadeState;
    if (st & 1) {
        u16 l = (u16)(g_fadeLevel - (st >> 8));
        g_fadeLevel = l;
        if ((s16)l <= 0) {
            g_fadeLevel = 0;
            g_fadeState = 0;
        }
    } else if (st & 2) {
        u16 l = (u16)(g_fadeLevel + ((st & 0xFF00) >> 8));
        g_fadeLevel = l;
        if ((s16)l >= 0xFF) {
            g_fadeLevel = 0xFF;
            g_fadeState = 4;
        }
    }
    s32 v = GV(s16, 0x800BBCE0);
    if (v >= 0x100) v = 0xFF;
    if (v < 0) v = 0;
    GV(u8, FADE_TILE + 0xE) = (u8)v;
    GV(u8, FADE_TILE + 0xD) = (u8)v;
    GV(u8, FADE_TILE + 0xC) = (u8)v;
    V0 = FADE_TILE;
}

/* 80090D4C (ot, depth): add the tiles to the ordering table. */
void n_fade_Draw(void) {
    FRAME(0x20);
    u32 ot = A0, depth = A1;
    if (GV(u8, 0x800BBD10)) GCALL3(0x8002BDA8u, DIM_TILE, ot, GV(u16, 0x800A55A4));
    if (g_fadeState & 4) GCALL3(0x8002BDA8u, FADE_TILE, ot, depth & 0xFFFF);
}

/* ---- the script's white-out ----
 *
 * Two full-screen tiles per frame buffer (0x800A2910: [2][2] primitives of
 * 0x10 bytes, semi-transparent) and a draw-mode packet per buffer
 * (0x800BB1B8, 12 bytes) go into the buffer's ordering table at
 * 0x800AFCB8 (0x2000 bytes per buffer) while 0x800BB1B0 is set. Their
 * shade (0x800BB1AC, 0-0xFF) steps by 0x800BB1B4 a frame, up while
 * 0x800BB1A8 is set, else down. */

#define WHITE_PRIMS     0x800A2910u
#define WHITE_MODES     0x800BB1B8u
#define g_whiteUp       GV(s32, 0x800BB1A8)
#define g_whiteLevel    GV(s32, 0x800BB1AC)
#define g_whiteOn       GV(s32, 0x800BB1B0)
#define g_whiteStep     GV(s32, 0x800BB1B4)
#define g_drawBuf       GV(u8, 0x80062D1C)
#define F_SET_DRAW_MODE_383E8 0x800383E8u  /* (p, dfe, dtd, tpage, [tw]) */
#define F_PRIM_INIT_38348 0x80038348u      /* tile set-up */
#define F_SET_SEMI_TRANS 0x80038248u
#define F_ADD_PRIM      0x800381E8u

/* 80085E14: set up the white-out (unless a stage is running a scene,
 * 0x800BBB68 >= 0). */
void n_white_Init(void) {
    FRAME(0x38);
    if (GV(s32, 0x800BBB68) >= 0) return;
    g_whiteOn = 0;
    g_whiteUp = 0;
    g_whiteLevel = 0;
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_SET_DRAW_MODE_383E8, WHITE_MODES, 0, 0, 0xC0);
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_SET_DRAW_MODE_383E8, WHITE_MODES + 0xC, 0, 0, 0xC0);
    for (u32 i = 0;; i++) {
        for (u32 j = 0;; j++) {
            u32 p = WHITE_PRIMS + i * 0x20 + j * 0x10;
            GCALL1(F_PRIM_INIT_38348, p);
            GCALL2(F_SET_SEMI_TRANS, p, 1);
            if (j + 1 >= 2) break;
            LOOP_POLL(0x80085EC8);
        }
        if (i + 1 >= 2) break;
        LOOP_POLL(0x80085ED8);
    }
    g_whiteStep = 8;
}

/* 80085F10: step the white-out's shade and draw it. */
void n_white_Draw(void) {
    FRAME(0x20);
    if (g_whiteUp != 0) {
        if (!(0xFF - g_whiteStep < g_whiteLevel)) g_whiteLevel = g_whiteLevel + g_whiteStep;
    } else {
        if (!(g_whiteLevel < g_whiteStep)) g_whiteLevel = g_whiteLevel - g_whiteStep;
    }
    u8 l = (u8)g_whiteLevel;
    u32 p = WHITE_PRIMS + (u32)g_drawBuf * 0x10;
    GV(u8, p + 0x24) = l; GV(u8, p + 0x25) = l; GV(u8, p + 0x26) = l;
    GV(u8, p + 4) = l; GV(u8, p + 5) = l; GV(u8, p + 6) = l;
    if (g_whiteOn != 0 && g_whiteLevel > 0) {
        u32 ot = 0x800AFCB8u + (u32)g_drawBuf * 0x2000;
        GCALL2(F_ADD_PRIM, ot, WHITE_PRIMS + (u32)g_drawBuf * 0x10);
        ot = 0x800AFCB8u + (u32)g_drawBuf * 0x2000;
        GCALL2(F_ADD_PRIM, ot, WHITE_PRIMS + 0x20 + (u32)g_drawBuf * 0x10);
        ot = 0x800AFCB8u + (u32)g_drawBuf * 0x2000;
        GCALL2(F_ADD_PRIM, ot, WHITE_MODES + (u32)g_drawBuf * 12);
    }
}

/* 8008606C (thread): script op: white out (0 hide the HUD, 1 start, 2
 * wait for the shade 0xF0). */
void n_op_WhiteIn(void) {
    u32 th = A0;
    s16 step = GV(s16, th + 4);
    if (step == 0) {
        GV(u16, 0x800C09D4) = 0x400;
        GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
    } else if (step == 1) {
        g_whiteUp = 1;
        g_whiteOn = 1;
        GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
    } else if (step == 2 && g_whiteLevel == 0xF0) {
        GV(u32, th) += 2;
        V0 = 0;
        return;
    }
    V0 = 1;
}

/* 80088678 (thread): script op: back from white (if it's on: fade down
 * and wait for 0, then off). */
void n_op_WhiteOut(void) {
    u32 th = A0;
    s16 step = GV(s16, th + 4);
    if (step == 0) {
        if (g_whiteOn != 0) {
            g_whiteUp = 0;
            GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
        } else {
            GV(u16, th + 4) = 2;
        }
    } else if (step == 1 && g_whiteLevel == 0) {
        g_whiteOn = 0;
        GV(u32, th) += 2;
        V0 = 0;
        return;
    }
    V0 = 1;
}

/* 80086A40 (thread): script op: fade pc[1] (1 in, 2 out) at speed pc[2]
 * and wait until it's done (state bit 2 clear / state 4). Any other kind
 * does nothing. */
void n_op_Fade(void) {
    FRAME(0x18);
    u32 th = A0;
    s16 step = GV(s16, th + 4);
    if (step == 0) {
        u32 pc = GV(u32, th);
        s16 kind = GV(s16, pc + 2);
        if (kind == 1) GCALL2(0x80090B90u, 0, (u32)(s32)GV(s16, pc + 4));
        else if (kind == 2) GCALL2(0x80090BF0u, 0, (u32)(s32)GV(s16, pc + 4));
        else { GV(u32, th) = pc + 6; V0 = 0; return; }
        GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
    } else if (step != 1) {
        V0 = 1;
        return;
    }
    u32 pc = GV(u32, th);
    s16 kind = GV(s16, pc + 2);
    int done;
    if (kind == 1) done = (g_fadeState & 4) == 0;
    else if (kind == 2) done = (s16)g_fadeState == 4;
    else { V0 = 1; return; }
    if (!done) { V0 = 1; return; }
    GV(u32, th) = pc + 6;
    V0 = 0;
}
