/* Drawing the HUD panels (TEST overlay), and the frame setters that
 * position a panel for an animation frame.
 *
 * Every frame 0x80092E94 turns each live, visible panel into a GPU
 * primitive in the current buffer (0x800BD028 + 0x1C20 per buffer index
 * 0x80062D1C) and links it into the HUD ordering table (0x800B4C44,
 * 0x80 bytes per buffer) at the panel's depth (+0x18). Panel kinds by
 * flags (+0x1C): 0x400 a sprite in the 3D world (the effects: drawn as a
 * camera-facing textured quad, 0x80098B60), 0x8000 a gouraud box
 * (colours at +0x22..+0x2C), 0x8 a scaled textured quad, 0x2000 a
 * rotated GsSPRITE, else a plain sprite.
 *
 * Panel fields used here: +0 clut x/16 and +2 clut y (the clut is
 * +2 | +0 << 7... as stored), +4/+6 texture u/v, +8/+A size, +C/+E texture
 * page x/y, +10/+12 screen x/y (12.4, centre relative; world x/4 and y
 * for world sprites), +14/+16 drawn size, +18 depth (world z/16 for world
 * sprites), +22..+2A the frame's base offsets, +30 angle, +34 scale (0x100
 * = 1.0), +38 animation, +3C update callback.
 *
 * The drawing context passed to the panel drawers is on the stack:
 * {u32 next primitive, u32 ordering table, s16 panel index}.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "hud.h"
#include "game_stack.h"

void n_hud_DrawPanels(void);
void n_hud_SetFrameA(void);
void n_hud_DrawBox(void);
void n_hud_StartAnimA(void);
void n_hud_StartAnimB(void);
void n_hud_DrawWorldSprite(void);
void n_hud_DrawModelSlot(void);

#define g_buffer        GV(u8, 0x80062D1C)    /* double-buffer index */
#define F_SET_POLY_FT4  0x800382C8u
#define F_SET_POLY_G4   0x800382E8u
#define F_SET_SPRT      0x80038328u
#define F_SET_SEMI      0x80038248u
#define F_SET_SHADE_TEX 0x80038278u
#define F_ADD_PRIM      0x800381E8u
#define F_SET_DRAW_MODE 0x800383E8u
#define F_SORT_SPRITE   0x8002BF28u
#define F_ROT_MATRIX_Y  0x8002ACF8u
#define F_APPLY_MATRIX  0x8002A5B8u
#define IDENTITY        0x80065B90u           /* identity MATRIX */
#define F_SET_FRAME_B   0x80093A48u

static inline u32 tpage_of(u32 p) {
    return ((P16(p, 0xC) >> 4) & 0x3F) | ((P16(p, 0xE) & 0x1FF) << 6);
}

/* 80092E94: draw the HUD panels (in the field: 0x800BBB70 == 2, or
 * 0x800A8610). */
void n_hud_DrawPanels(void) {
    FRAME(0x50);
    if (GV(s32, 0x800BBB70) != 2 && GV(u32, 0x800A8610) == 0) { return; }
    u32 ctx = sp + 0x18;
    g_curPanel = PANELS;
    u32 buf = g_buffer;
    GS(u16, ctx + 8) = 0;
    GV(u32, 0x800C3A8C) += 1;
    GS(u32, ctx + 4) = 0x800B4C44u + buf * 0x80;
    GS(u32, ctx) = 0x800BD028u + buf * 0x1C20;
    do {
        u32 p = g_curPanel;
        u16 fl = P16(p, PANEL_FLAGS);
        if (!(fl & 1) && (fl & (u16)GV(s16, 0x800C09D4)) && (fl & 4)) {
            if (fl & 0x400) {
                A0 = ctx;
                n_hud_DrawWorldSprite();
            } else if (fl & 0x8000) {
                A0 = ctx;
                n_hud_DrawBox();
            } else {
                u32 w = P16(p, 0x14), h = P16(p, 0x16);
                u32 sw = P16(p, 0x08), sh = P16(p, 0x0A);
                u32 u = P16(p, 0x04), v = P16(p, 0x06);
                u32 clut = P16(p, 0x02) | ((u32)P16(p, 0x00) << 7);
                u32 ot = GS(u32, ctx + 4);
                fl = P16(g_curPanel, PANEL_FLAGS);
                if (fl & 8) {                                /* scaled quad */
                    u32 q = GS(u32, ctx);
                    GCALL1(F_SET_POLY_FT4, q);
                    GCALL2(F_SET_SEMI, q, (P16(g_curPanel, PANEL_FLAGS) & 0x10) ? 1 : 0);
                    GCALL2(F_SET_SHADE_TEX, q, 1);
                    if ((s16)u + (s16)sw >= 0x100) { w--; sw--; }
                    if ((s16)v + (s16)sh >= 0x100) { h--; sh--; }
                    p = g_curPanel;
                    if (P16(p, 0x34) >= 0x200) { sw--; sh--; }      /* magnified: no edge bleed */
                    u32 u1 = u + sw, v1 = v + sh;
                    s16 x = (s16)P16(p, 0x10) >> 4, y = (s16)P16(p, 0x12) >> 4;
                    GV(u8, q + 0xC) = (u8)u;  GV(u8, q + 0xD) = (u8)v;
                    GV(u8, q + 0x14) = (u8)u1; GV(u8, q + 0x15) = (u8)v;
                    GV(u8, q + 0x1C) = (u8)u; GV(u8, q + 0x1D) = (u8)v1;
                    GV(u8, q + 0x24) = (u8)u1; GV(u8, q + 0x25) = (u8)v1;
                    GV(s16, q + 0xA) = y;
                    GV(s16, q + 0x12) = y;
                    GV(u16, q + 0x1A) = (u16)(y + h);
                    GV(u16, q + 0x22) = (u16)(y + h);
                    GV(s16, q + 0x8) = x;
                    GV(u16, q + 0x10) = (u16)(x + w);
                    GV(s16, q + 0x18) = x;
                    GV(u16, q + 0x20) = (u16)(x + w);
                    GV(u8, q + 4) = 0x80; GV(u8, q + 5) = 0x80; GV(u8, q + 6) = 0x80;
                    GV(u16, q + 0x16) = (u16)clut;
                    GV(u16, q + 0xE) = (u16)tpage_of(p);
                    GCALL2(F_ADD_PRIM, ot + (u32)((s32)(s16)P16(g_curPanel, 0x18) * 4), q);
                    GS(u32, ctx) = q + 0x28;
                } else if (fl & 0x2000) {                    /* rotated GsSPRITE */
                    u32 s = GS(u32, ctx);
                    p = g_curPanel;
                    GV(u32, s) = 0;
                    GV(u16, s + 4) = (u16)(P16(p, 0x22) + ((s16)P16(p, 0x10) >> 4));
                    GV(u16, s + 6) = (u16)(P16(p, 0x24) + ((s16)P16(p, 0x12) >> 4));
                    GV(u16, s + 8) = P16(p, 0x08);
                    GV(u16, s + 0xA) = P16(p, 0x0A);
                    GV(u16, s + 0xC) = (u16)clut;
                    GV(u8, s + 0xE) = GV(u8, p + 4);
                    GV(u8, s + 0xF) = GV(u8, p + 6);
                    GV(u16, s + 0x10) = P16(p, 0x0C);
                    GV(u16, s + 0x12) = P16(p, 0x0E);
                    GV(u8, s + 0x14) = 0x80; GV(u8, s + 0x15) = 0x80; GV(u8, s + 0x16) = 0x80;
                    GV(u16, s + 0x18) = P16(p, 0x22);
                    GV(u16, s + 0x1A) = P16(p, 0x24);
                    GV(u16, s + 0x1C) = 0x1000;
                    GV(u16, s + 0x1E) = 0x1000;
                    GV(u32, s + 0x20) = P32(p, 0x38);
                    GCALL3(F_SORT_SPRITE, s, 0x800AFC88u + (u32)g_buffer * 20, P16(p, 0x18));
                    GS(u32, ctx) = s + 0x24;
                } else {                                     /* plain sprite */
                    u32 q = GS(u32, ctx);
                    GCALL1(F_SET_SPRT, q);
                    GCALL2(F_SET_SEMI, q, (P16(g_curPanel, PANEL_FLAGS) & 0x10) ? 1 : 0);
                    GCALL2(F_SET_SHADE_TEX, q, 1);
                    if ((s16)u + (s16)sw >= 0x100) sw--;
                    if ((s16)v + (s16)sh >= 0x100) sh--;
                    p = g_curPanel;
                    GV(s16, q + 8) = (s16)P16(p, 0x10) >> 4;
                    GV(u8, q + 0xC) = (u8)u;
                    GV(u8, q + 0xD) = (u8)v;
                    GV(u16, q + 0x10) = (u16)sw;
                    GV(u16, q + 0x12) = (u16)sh;
                    GV(s16, q + 0xA) = (s16)P16(p, 0x12) >> 4;
                    GV(u8, q + 4) = 0x80; GV(u8, q + 5) = 0x80; GV(u8, q + 6) = 0x80;
                    GV(u16, q + 0xE) = (u16)tpage_of(p);
                    u32 mode = q + 0x14;
                    GCALL2(F_ADD_PRIM, ot + (u32)((s32)(s16)P16(g_curPanel, 0x18) * 4), q);
                    GS(u32, ctx) = mode;
                    GS(u32, sp + 0x10) = 0;                  /* texture window */
                    GCALL4(F_SET_DRAW_MODE, mode, 0, 0, (u32)(s32)(s16)clut);
                    GCALL2(F_ADD_PRIM, GS(u32, ctx + 4) + (u32)((s32)(s16)P16(g_curPanel, 0x18) * 4), mode);
                    GS(u32, ctx) = mode + 0xC;
                }
            }
        }
        GS(u16, ctx + 8) = (u16)(GS(u16, ctx + 8) + 1);
        g_curPanel += PANEL_SIZE;
    } while (GS(s16, ctx + 8) < PANEL_COUNT);
}

/* 800936E0 (frame, frames, offsets): set the current panel to frame of an
 * animation: frames are {s16 offset entry (-1 ends), u16 duration};
 * offset entries {u, v, x, y, w, h} are relative to the panel's base
 * (+0x22..+0x2A). World sprites (flag 0x400) are offset in x/z along the
 * player's facing (or their own angle +0x30, flag 0x800; plus it with
 * flag 0x2000). Returns -1 at the end, else 0. */
void n_hud_SetFrameA(void) {
    FRAME(0x58);
    u32 e = A1 + (u32)((s32)(A0 << 17) >> 15);
    u32 offsets = A2;
    s16 k = GV(s16, e);
    if (k == -1) { V0 = (u32)-1; return; }
    u32 o = offsets + (u32)((s32)((u32)(k * 3) << 17) >> 15);
    u32 p = g_curPanel;
    P16(p, 0x1A) = GV(u16, e + 2);
    u16 fl = P16(p, PANEL_FLAGS);
    P16(p, 0x04) = (u16)(P16(p, 0x22) + GV(u16, o));
    P16(p, 0x06) = (u16)(P16(p, 0x24) + GV(u16, o + 2));
    u32 ax = 0, az = 0;
    if (fl & 0x400) {
        u32 ang = (fl & 0x800) ? P16(p, 0x30) : GV(u16, 0x800A8606);
        if (P16(g_curPanel, PANEL_FLAGS) & 0x2000) ang += P16(g_curPanel, 0x30);
        ang &= 0xFFF;
        u32 m = sp + 0x28;
        for (u32 i = 0; i < 0x20; i += 4) GS(u32, m + i) = GV(u32, IDENTITY + i);
        GS(s16, sp + 0x10) = 0x3E8;
        GS(s16, sp + 0x12) = 0;
        GS(s16, sp + 0x14) = 0;
        GCALL2(F_ROT_MATRIX_Y, ang, m);
        GCALL3(F_APPLY_MATRIX, m, sp + 0x10, sp + 0x18);
        ax = GS(u16, sp + 0x18);
        az = GS(u16, sp + 0x20);
    }
    p = g_curPanel;
    s32 dx = ((s32)GV(s16, o + 4) * P16(p, 0x34)) >> 8;
    if (P16(p, PANEL_FLAGS) & 0x400) {
        s32 s = (s16)dx;
        s32 a = (s32)((u32)s * (u32)(s32)(s16)ax) / 50 - 0xA0;
        P16(p, 0x10) = (u16)(P16(p, 0x26) + (div_pow2(a, 2)));
        P16(p, 0x18) = (u16)(P16(p, 0x2A) + (s32)((u32)s * (u32)(s32)(s16)az) / 800);
    } else {
        P16(p, 0x10) = (u16)((P16(p, 0x26) + dx - 0xA0) << 4);
    }
    p = g_curPanel;
    s32 dy = ((s32)GV(s16, o + 6) * P16(p, 0x34)) >> 8;
    if (P16(p, PANEL_FLAGS) & 0x400)
        P16(p, 0x12) = (u16)(P16(p, 0x28) + (s16)dy * 20 - 0x78);
    else
        P16(p, 0x12) = (u16)((P16(p, 0x28) + dy - 0x78) << 4);
    p = g_curPanel;
    u32 w = GV(u16, o + 8), h = GV(u16, o + 10);
    P16(p, 0x08) = (u16)w;
    P16(p, 0x0A) = (u16)h;
    if (P16(p, PANEL_FLAGS) & 8) {                       /* scaled */
        w = (u32)(((s32)(s16)w * P16(p, 0x34)) >> 8);
        P16(p, 0x14) = (u16)w;
        h = (u32)(((s32)(s16)h * P16(p, 0x34)) >> 8);
        P16(p, 0x16) = (u16)h;
    }
    p = g_curPanel;
    if (P16(p, PANEL_FLAGS) & 0x20) {                    /* centred */
        P16(p, 0x10) = (u16)(P16(p, 0x10) - (s16)w * 8);
        P16(p, 0x12) = (u16)(P16(p, 0x12) - (s16)h * 8);
    }
    V0 = 0;
}

/* 80094650 (ctx): a gouraud-shaded box: top colour +0x22/24/26, bottom
 * +0x28/2A/2C, into the ordering table one entry before the HUD's. */
void n_hud_DrawBox(void) {
    FRAME(0x20);
    u32 ctx = A0;
    u32 ot = 0x800B4C40u + (u32)g_buffer * 0x80;
    u32 q = GS(u32, ctx);
    GCALL1(F_SET_POLY_G4, q);
    GCALL2(F_SET_SEMI, q, 0);
    GCALL2(F_SET_SHADE_TEX, q, 1);
    u32 p = g_curPanel;
    for (int c = 0; c < 3; c++) {
        u8 top = GV(u8, p + 0x22 + c * 2), bot = GV(u8, p + 0x28 + c * 2);
        GV(u8, q + 0x14 + c) = top;
        GV(u8, q + 0x04 + c) = top;
        GV(u8, q + 0x1C + c) = bot;
        GV(u8, q + 0x0C + c) = bot;
    }
    s16 x = (s16)P16(p, 0x10) >> 4, y = (s16)P16(p, 0x12) >> 4;
    GV(s16, q + 0x08) = x;
    GV(s16, q + 0x0A) = y;
    GV(s16, q + 0x12) = y;
    GV(s16, q + 0x18) = x;
    GV(u16, q + 0x10) = (u16)(x + P16(p, 0x08));
    GV(u16, q + 0x1A) = (u16)(y + P16(p, 0x0A));
    GV(u16, q + 0x20) = (u16)(x + P16(p, 0x08));
    GV(u16, q + 0x22) = (u16)(y + P16(p, 0x0A));
    GCALL2(F_ADD_PRIM, ot + (u32)((s32)(s16)P16(p, 0x18) * 4), q);
    GS(u32, ctx) = q + 0x24;
}

/* Remember the panel's current place as the animation's base. */
static void anim_base(u32 p) {
    P16(p, 0x1E) = 0;
    P16(p, 0x22) = P16(p, 0x04);
    P16(p, 0x24) = P16(p, 0x06);
    P16(p, 0x26) = P16(p, 0x10);
    P16(p, 0x28) = P16(p, 0x12);
}

/* 800947D4 (anim {frames, offsets}): start a frame-setter-A animation on
 * the current panel (its depth becomes part of the base too). */
void n_hud_StartAnimA(void) {
    FRAME(0x20);
    u32 anim = A0;
    u32 p = g_curPanel;
    u16 z = P16(p, 0x18);
    anim_base(p);
    P16(p, 0x2A) = z;
    A0 = 0; A1 = GV(u32, anim); A2 = GV(u32, anim + 4);
    n_hud_SetFrameA();
    P32(g_curPanel, PANEL_DRAWPTR) = anim;
    V0 = g_curPanel;
}

/* 8009484C (anim): the same with frame setter B (0x80093A48). */
void n_hud_StartAnimB(void) {
    FRAME(0x20);
    u32 anim = A0;
    anim_base(g_curPanel);
    GCALL3(F_SET_FRAME_B, 0, GV(u32, anim), GV(u32, anim + 4));
    P32(g_curPanel, PANEL_DRAWPTR) = anim;
    V0 = g_curPanel;
}

/* 80098B60 (ctx): a world sprite: a quad of the panel's size (x10 units)
 * at its world position (+10 x/4, +12 y, +18 z/16), turned to face the
 * camera, perspective transformed and depth sorted into the world's
 * ordering table (0x800AFCB8; 0x800AB530 with flag 0x800; at the front
 * with flag 0x1000). Flag 0x20: drawn by 0x8009EE48 instead; 0x8000:
 * stage 5's own drawer (overlay 0x800F2DE8). Flag 0x200: lit by +0x2C. */
void n_hud_DrawWorldSprite(void) {
    FRAME(0xF8);
    u32 ctx = A0;
    u32 p = g_curPanel;
    u16 fl = P16(p, PANEL_FLAGS);
    if (!(fl & (u16)(s16)(GV(u16, 0x800C09D4) & 0xE7BF))) { return; }
    /* a1 still holds the panel, a2/a3 what they were */
    if (fl & 0x20) { gcall(0x8009EE48u, ctx, p, A2, A3); return; }
    if (fl & 0x8000) {
        if (GV(s32, 0x800A866C) == 5) gcall(0x800F2DE8u, ctx, p, A2, A3);
        return;
    }
    u32 obj = sp + 0x28;                          /* a coordinate system, pos +0x18, rot +0x44 */
    GS(s32, obj + 0x18) = (s32)(s16)P16(p, 0x10) * 4;
    GS(s32, obj + 0x1C) = (s16)P16(p, 0x12);
    GS(u16, obj + 0x44) = GV(u16, CAMERA_OBJ + 0x44);
    GS(s32, obj + 0x20) = (s32)(s16)P16(p, 0x18) * 16;
    u32 yaw;
    if (P16(p, PANEL_FLAGS) & 0x2000)
        yaw = P16(p, 0x30) + (u32)(u16)(GV(u16, 0x800A8606) + GV(u16, CAMERA_OBJ + 0x46));
    else
        yaw = GV(u16, 0x800A8606) + GV(u16, CAMERA_OBJ + 0x46);
    GS(u16, obj + 0x46) = (u16)(yaw & 0xFFF);
    GS(u32, obj + 0x4C) = 0;                      /* no parent */
    GS(u16, obj + 0x48) = GV(u16, 0x800A8660);
    GCALL2(0x80082760u, obj, 0);                  /* its matrix */
    GCALL2(0x800339E8u, obj, sp + 0x78);          /* to world */
    GCALL1(0x8002CF14u, sp + 0x78);               /* load it */
    p = g_curPanel;
    u32 w = P16(p, 0x14), h = P16(p, 0x16);
    u32 vx = sp + 0x98;                           /* corners */
    GS(u16, vx + 0x04) = 0; GS(u16, vx + 0x0C) = 0; GS(u16, vx + 0x14) = 0; GS(u16, vx + 0x1C) = 0;
    s16 nw = (s16)(-(s32)w * 10), nh = (s16)(-(s32)h * 10), pw = (s16)(w * 10), ph = (s16)(h * 10);
    GS(s16, vx + 0x08) = pw; GS(s16, vx + 0x10) = pw;
    GS(s16, vx + 0x12) = ph; GS(s16, vx + 0x1A) = ph;
    GS(s16, vx + 0x00) = nw; GS(s16, vx + 0x02) = nh;
    GS(s16, vx + 0x0A) = nh; GS(s16, vx + 0x18) = nw;
    GS(u32, sp + 0x10) = sp + 0xB8;               /* screen xy of each corner, p, flag */
    GS(u32, sp + 0x14) = sp + 0xC0;
    GS(u32, sp + 0x18) = sp + 0xC8;
    GS(u32, sp + 0x1C) = sp + 0xD0;
    GS(u32, sp + 0x20) = sp + 0xD8;
    GS(u32, sp + 0x24) = sp + 0xDC;
    u32 otz = GCALL4(0x8002A9E8u, sp + 0x98, sp + 0xA0, sp + 0xB0, sp + 0xA8);   /* RotTransPers4 */
    if (otz - 0x10 >= 0x3FF0) { return; }
    u32 q = GS(u32, ctx);
    GCALL1(F_SET_POLY_FT4, q);
    GCALL2(F_SET_SEMI, q, (P16(g_curPanel, PANEL_FLAGS) & 0x10) ? 1 : 0);
    GCALL2(F_SET_SHADE_TEX, q, (P16(g_curPanel, PANEL_FLAGS) & 0x200) ? 0 : 1);
    p = g_curPanel;
    u8 u = GV(u8, p + 4), v = GV(u8, p + 6);
    u8 u1 = (u8)(u + GV(u8, p + 8) + 0xFF), v1 = (u8)(v + GV(u8, p + 0xA) + 0xFF);
    GV(u8, q + 0x0C) = (u8)(u + 1);
    GV(u8, q + 0x0D) = (u8)(v + 1);
    GV(u8, q + 0x14) = u1;
    GV(u8, q + 0x15) = (u8)(v + 1);
    GV(u8, q + 0x1C) = (u8)(u + 1);
    GV(u8, q + 0x1D) = v1;
    GV(u8, q + 0x24) = u1;
    GV(u8, q + 0x25) = v1;
    GV(u16, q + 0x0E) = (u16)tpage_of(p);
    GV(u16, q + 0x16) = (u16)(P16(p, 0x02) | ((u32)P16(p, 0x00) << 7));
    u8 c = (P16(p, PANEL_FLAGS) & 0x200) ? GV(u8, p + 0x2C) : 0x80;
    GV(u8, q + 4) = c; GV(u8, q + 5) = c; GV(u8, q + 6) = c;
    GV(u16, q + 0x08) = GS(u16, sp + 0xB8);
    GV(u16, q + 0x0A) = GS(u16, sp + 0xBA);
    GV(u16, q + 0x10) = GS(u16, sp + 0xC0);
    GV(u16, q + 0x12) = GS(u16, sp + 0xC2);
    GV(u16, q + 0x18) = GS(u16, sp + 0xC8);
    GV(u16, q + 0x1A) = GS(u16, sp + 0xCA);
    GV(u16, q + 0x20) = GS(u16, sp + 0xD0);
    GV(u16, q + 0x22) = GS(u16, sp + 0xD2);
    fl = P16(g_curPanel, PANEL_FLAGS);
    u32 ot = (fl & 0x800) ? 0x800AB530u : 0x800AFCB8u;
    if (!(fl & 0x1000)) ot += (u32)((s32)otz >> 3) * 4;
    ot += (u32)g_buffer << 13;
    GCALL2(F_ADD_PRIM, ot, q);
    GS(u32, ctx) = q + 0x28;
}

/* 8009EE48 (ctx, panel): flag 0x20's drawer: the current panel's model
 * slot (0x800C3A98 + +0x22 x 0xEC: u8 object count at +0, last frame's
 * blink at +1, world at +2; objects of 8 bytes from +4: coordinates,
 * model; animated parts' count at +0xA8, the parts from +0xAC) placed at
 * the panel (x/4, y, z/16, rotation +0x24..+0x28, scale +0x34) and depth
 * sorted, at the front unless +2 says in the world. Unpositioned objects
 * (no coordinates) blink their parts with the shards' timer
 * (0x800C3A1C) once per frame (0x800C3A8C), or show them still. */
void n_hud_DrawModelSlot(void) {
    FRAME(0x88);
    u32 p = g_curPanel;
    u32 m = 0x800C3A98u + (u32)((s32)(s16)P16(p, 0x22) * 0xEC);
    GV(s32, m + 0x70) = (s32)(s16)P16(p, 0x10) << 2;
    GV(s32, m + 0x74) = (s16)P16(p, 0x12);
    GV(s32, m + 0x78) = (s32)(s16)P16(p, 0x18) << 4;
    GV(u16, m + 0x9C) = P16(p, 0x24);
    GV(u16, m + 0x9E) = P16(p, 0x26);
    GV(u32, 0x800BAA08) = 0;
    GV(u16, m + 0xA0) = P16(p, 0x28);
    u32 sc = (u32)P16(p, 0x34) << 4;
    GS(u32, sp + 0x58) = sc;
    GS(u32, sp + 0x54) = sc;
    GS(u32, sp + 0x50) = sc;
    gcall(0x8006F69Cu, m + 0x58, sp + 0x50, p, A3);
    u32 n = GV(u8, m + 0);
    for (u32 i = 0; i < n; i++) {
        if (i) RT_POLL_NOSAVE(0x8009F098u);
        u32 obj = m + 4 + i * 8;
        if (GV(u32, obj + 4) == 0) continue;
        u32 c = GV(u32, obj + 0);
        if (c != 0) {
            GV(u32, c) = 0;
            gcall(0x80033CB8u, GV(u32, obj + 0), sp + 0x30, sp + 0x10, 0);
            GCALL1(0x8002CF14u, sp + 0x10);
            GCALL1(0x8002CF40u, sp + 0x30);
        } else if (GV(u8, m + 1) != GV(u8, 0x800C3A8C) && GV(s32, 0x800C3A1C) != 0) {
            for (s32 j = 0; j < GV(s32, m + 0xA8); j++) {
                if (j) RT_POLL_NOSAVE(0x8009EFF8u);
                GV(u8, GV(u32, m + 0xAC + (u32)j * 4) + 0xD) = GV(u8, 0x800C3A1C);
            }
            GV(u8, m + 1) = GV(u8, 0x800C3A8C);
        } else {
            for (s32 j = 0; j < GV(s32, m + 0xA8); j++) {
                if (j) RT_POLL_NOSAVE(0x8009F03Cu);
                GV(u8, GV(u32, m + 0xAC + (u32)j * 4) + 0xD) = 0;
            }
        }
        u32 ot = GV(u8, m + 2) ? 0x800B3D70u : 0x800B4BB8u;
        gcall(0x800333D8u, obj, ot + (u32)g_buffer * 20, 0x1F800000u, 0);
    }
}
