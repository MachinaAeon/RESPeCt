/* Drawing the title's sprites and text (see title.h): sprites become
 * POLY_FT4s in the packet area at 0x800F8000 (0x3000 bytes per buffer),
 * text is drawn glyph by glyph from the two text windows' byte codes;
 * and the text windows' contents (set, clear, the menu cursor marks).
 *
 * The text routines take the address of the caller's packet pointer (a
 * local on its stack) and advance it, as the originals do.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "title.h"

#define PACKETS        0x800F8000u
#define F_T_TEXT_A     0x80070254u   /* other text styles (flags 0x20, 0x40) */
#define F_T_TEXT_B     0x800707C0u

/* lh field << 16 >> 20: a 12.4 coordinate's whole part */
static s32 whole(u32 s, u32 o) { return (s32)(s16)SP_U16(s, o) >> 4; }

/* 8006FB64: draw every shown sprite: text sprites through the text
 * routines, the others as a textured quad (flags: 8 semi-transparent,
 * 0x10 mirrored), tinted by their colour and the screen's brightness. */
void n_title_DrawSprites(void) {
    FRAME(0x58);
    u32 pk = PACKETS + (u32)BUF * 0x3000;
    for (s32 i = 0; i < SPRITE_COUNT; i++) {
        u32 s = SPR(i);
        u16 f = SP_U16(s, 0x20);
        if ((f & SF_FREE) || !(f & SF_SHOWN)) goto next;
        if (f & 0x20) {
            GS(u32, sp + 0x20) = pk;
            T_TEXT_CLUT = SP_U16(s, 0xE);
            GS(u32, sp + 0x10) = sp + 0x20;
            GS(s32, sp + 0x14) = SP_S16(s, 0x18);
            GS(u32, sp + 0x18) = 2;
            GS(u32, sp + 0x1C) = SP_U16(s, 0x22);
            GCALL4(F_T_TEXT_A, GV(u32, s + 0x30), SP_U16(s, 0x1A), (u32)whole(s, 0x10), (u32)whole(s, 0x12));
            pk = GS(u32, sp + 0x20);
            goto next;
        }
        if (f & 0x40) {
            GS(u32, sp + 0x24) = pk;
            T_TEXT_CLUT = SP_U16(s, 0xE);
            GS(s32, sp + 0x10) = SP_S16(s, 0x18);
            GS(u32, sp + 0x14) = SP_U16(s, 0x22);
            GCALL4(F_T_TEXT_B, GV(u32, s + 0x30), (u32)whole(s, 0x10), (u32)whole(s, 0x12), sp + 0x24);
            pk = GS(u32, sp + 0x24);
            goto next;
        }
        if (f & 0x80) {
            GS(u32, sp + 0x28) = pk;
            GS(u32, sp + 0x10) = sp + 0x28;
            T_GLYPH_W = 0xE;
            GS(u32, sp + 0x18) = 2;
            GS(s32, sp + 0x14) = SP_S16(s, 0x18);
            GS(u32, sp + 0x1C) = SP_U16(s, 0x22);
            T_TEXT_CLUT = SP_U16(s, 0xE);
            GCALL4(F_T_TEXT, GV(u32, s + 0x30), SP_U16(s, 0x1A), (u32)whole(s, 0x10), (u32)whole(s, 0x12));
            pk = GS(u32, sp + 0x28);
            goto next;
        }
        {
            u32 p = pk;
            u16 tpage = (u16)(SP_U16(s, 2) | (SP_U16(s, 0) << 7));
            GV(u8, p + 3) = 9;
            GV(u8, p + 7) = 0x2C;                 /* POLY_FT4 */
            GCALL2(F_SET_SEMI, p, (SP_U16(s, 0x20) & 8) ? 1 : 0);
            GV(u16, p + 0x16) = tpage;
            GV(u16, p + 0xE) = (u16)((SP_U16(s, 0xE) << 6) | ((SP_U16(s, 0xC) >> 4) & 0x3F));
            if (SP_U16(s, 0x1A) == 0x80 && GV(u32, s + 0x1C) == 0x00800080u && T_FADE == 0x80) {
                GCALL2(F_SET_SHADE_TEX, p, 1);
                GV(u8, p + 4) = 0x80;
                GV(u8, p + 5) = 0x80;
                GV(u8, p + 6) = 0x80;
            } else {
                GCALL2(F_SET_SHADE_TEX, p, 0);
                GV(u8, p + 4) = (u8)((s32)((u32)SP_U16(s, 0x1A) * (u32)T_FADE) >> 7);
                GV(u8, p + 5) = (u8)((s32)((u32)SP_U16(s, 0x1C) * (u32)T_FADE) >> 7);
                GV(u8, p + 6) = (u8)((s32)((u32)SP_U16(s, 0x1E) * (u32)T_FADE) >> 7);
            }
            s32 w = (s32)((u32)SP_U16(s, 0x14) * SP_U16(s, 0x22)) >> 8;
            s32 h = (s32)((u32)SP_U16(s, 0x16) * SP_U16(s, 0x22)) >> 8;
            s32 x = whole(s, 0x10) + T_OFS_X * 2;
            s32 y = whole(s, 0x12) + T_OFS_Y * 2;
            if (SP_U16(s, 0x20) & 0x10) {             /* mirrored */
                GV(u16, p + 8) = (u16)(w + x);
                GV(u16, p + 0x10) = (u16)x;
                GV(u16, p + 0x18) = (u16)(w + x);
                GV(u16, p + 0x20) = (u16)x;
            } else {
                GV(u16, p + 8) = (u16)x;
                GV(u16, p + 0x10) = (u16)(w + x);
                GV(u16, p + 0x18) = (u16)x;
                GV(u16, p + 0x20) = (u16)(w + x);
            }
            GV(u16, p + 0xA) = (u16)y;
            GV(u16, p + 0x12) = (u16)y;
            GV(u16, p + 0x1A) = (u16)(h + y);
            GV(u16, p + 0x22) = (u16)(h + y);
            /* texture: one texel short where the image ends at a page edge */
            u32 tw = SP_U16(s, 8);
            if (SP_U16(s, 0) != 2 && (s32)(SP_U16(s, 4) + (s32)(s16)tw) >= 0x100) tw--;
            s32 th = SP_S16(s, 0xA);
            s32 th2 = th;
            if ((s32)(SP_U16(s, 6) + th) >= 0x100) th2 = th - 1;
            u32 dv = (u32)th2;
            if (T_SCREEN == 0x10 && SP_U16(s, 0) == 2) {
                dv = (u32)(th2 - 1);
                tw--;
            }
            u8 u0 = GV(u8, s + 4), v0 = GV(u8, s + 6);
            GV(u8, p + 0xC) = u0;
            GV(u8, p + 0xD) = v0;
            GV(u8, p + 0x14) = (u8)(u0 + tw);
            GV(u8, p + 0x15) = v0;
            GV(u8, p + 0x1C) = u0;
            GV(u8, p + 0x1D) = (u8)(v0 + dv);
            GV(u8, p + 0x24) = (u8)(u0 + tw);
            GV(u8, p + 0x25) = (u8)(v0 + dv);
            GCALL2(F_ADD_PRIM, ((u32)BUF << 10) + (u32)SP_S16(s, 0x18) * 4 + OT_PRIMS, p);
            pk = p + 0x28;
        }
    next:
        if (i + 1 < SPRITE_COUNT) LOOP_POLL(0x80070110);
    }
}

/* 80070148: draw the two text windows: window 0 (the message) at its
 * brightness, window 1 at the rest of it; on screens 8 and 0x10 window
 * 1 at window 0's brightness on top. */
void n_title_DrawText(void) {
    FRAME(0x30);
    T_TEXT_CLUT = 0;
    GS(u32, sp + 0x18) = 0x80090000u + ((u32)BUF << 16);
    s32 fade = T_TEXT_FADE;
    if (fade != 0) {
        GS(u32, sp + 0x10) = sp + 0x18;
        GS(u32, sp + 0x14) = 1;
        GCALL4(F_TEXT_DRAW_WINDOW, TEXT_BUF, (u32)fade, (u32)(s32)GV(s16, TEXT_POS), (u32)(s32)GV(s16, TEXT_POS + 2));
    }
    fade = T_TEXT_FADE;
    if (fade != 0x80) {
        GS(u32, sp + 0x10) = sp + 0x18;
        GS(u32, sp + 0x14) = 3;
        T_TEXT_CLUT = 0;
        GCALL4(F_TEXT_DRAW_WINDOW, TEXT_BUF + 0x26C, (u32)(0x80 - fade), (u32)(s32)GV(s16, TEXT_POS + 4), (u32)(s32)GV(s16, TEXT_POS + 6));
    }
    s32 scr = T_SCREEN;
    if (scr == 0x10 || scr == 8) {
        GS(u32, sp + 0x10) = sp + 0x18;
        GS(u32, sp + 0x14) = 1;
        T_TEXT_CLUT = 0;
        GCALL4(F_TEXT_DRAW_WINDOW, TEXT_BUF + 0x26C, (u32)T_TEXT_FADE, (u32)(s32)GV(s16, TEXT_POS + 4), (u32)(s32)GV(s16, TEXT_POS + 6));
    }
}

/* The textured quad of a glyph: position (x, y) on screen plus the
 * screen offset, w x h. */
static void glyph_xy(u32 p, s32 x, s32 y, s32 w, s32 h) {
    s32 ox = T_OFS_X * 2, oy = T_OFS_Y * 2;
    GV(u16, p + 8) = (u16)(x + ox);
    GV(u16, p + 0xA) = (u16)(y + oy);
    GV(u16, p + 0x10) = (u16)(x + ox + w);
    GV(u16, p + 0x12) = (u16)(y + oy);
    GV(u16, p + 0x18) = (u16)(x + ox);
    GV(u16, p + 0x1A) = (u16)(y + oy + h);
    GV(u16, p + 0x20) = (u16)(x + ox + w);
    GV(u16, p + 0x22) = (u16)(y + oy + h);
}

static void glyph_uv(u32 p, u8 u0, u8 v0, u8 u1, u8 v1) {
    GV(u8, p + 0xC) = u0;
    GV(u8, p + 0xD) = v0;
    GV(u8, p + 0x14) = u1;
    GV(u8, p + 0x15) = v0;
    GV(u8, p + 0x1C) = u0;
    GV(u8, p + 0x1D) = v1;
    GV(u8, p + 0x24) = u1;
    GV(u8, p + 0x25) = v1;
}

/* A glyph's tint: as drawn when fade, brightness and colour are all
 * 0x80, else their product. */
static void glyph_tint(u32 pp, s32 fade, s32 color) {
    if (fade == 0x80 && T_FADE == fade && color == fade) {
        GCALL2(F_SET_SHADE_TEX, GS(u32, pp), 1);
    } else {
        GCALL2(F_SET_SHADE_TEX, GS(u32, pp), 0);
        u8 c = (u8)((s32)((u32)fade * (u32)T_FADE * (u32)color) >> 14);
        GV(u8, GS(u32, pp) + 4) = c;
        GV(u8, GS(u32, pp) + 5) = c;
        GV(u8, GS(u32, pp) + 6) = c;
    }
}

/* 80070A7C: draw text (byte codes, see title.h) at (x, y): arguments
 * text, fade, x, y, [packet pointer's address, ordering table slot, mode
 * (1: shadow CLUT, 2: opaque), scale (0x100 = 1)]. */
void n_title_Text(void) {
    u32 args = R[29] + 0x10;
    FRAME(0x58);
    u32 pp = GS(u32, args), mode = GS(u32, args + 8), scale = GS(u32, args + 0xC);
    u32 t = A0;
    s32 fade = (s32)A1;
    s32 color = 0x80;
    GS(u32, sp + 0x60) = A2;
    u16 x0 = (u16)A2, y = (u16)A3, x;
    s32 adv = (s32)(scale << 3) >> 8;
    for (;;) {
        if (GV(u8, t) == 0xFF) return;
        x = x0;
        for (;;) {
            u32 c = GV(u8, t);
            t++;
            if (c == 0xFF) return;
            if (c == 0xFE) break;
            if (c == 0xF6) { LOOP_POLL(0x80070B14); continue; }
            if (c == 0xFC) { T_TEXT_CLUT = GV(u8, t); t++; LOOP_POLL(0x80070B30); continue; }
            if (c == 0xFA) {
                u32 b = GV(u8, t);
                t++;
                if (b >= 0x81) b |= 0xFF00;
                x = (u16)(x + b);
                LOOP_POLL(0x80070B78);
                continue;
            }
            if (c == 0xF9) { color = GV(u8, t); t++; LOOP_POLL(0x80070B8C); continue; }
            if (c == 0xF8) { T_LINE_H = GV(u8, t); t++; LOOP_POLL(0x80070BA8); continue; }
            if (c == 0xF7) {
                u32 b = GV(u8, t);
                t++;
                if (b < 0x80) { y = (u16)(y + b); LOOP_POLL(0x80070BD8); }
                else { y = (u16)(y + b + 0xFF00); LOOP_POLL(0x80070BF0); }
                continue;
            }
            if (c == 0xF5) { T_GLYPH_W = GV(u8, t); t++; LOOP_POLL(0x80070C0C); continue; }
            if (c < 0x10) {                         /* a two-byte glyph */
                u32 code = GV(u8, t) | (c << 8);
                t++;
                GS(s32, sp + 0x10) = (s16)y;
                GS(u32, sp + 0x14) = pp;
                GS(u32, sp + 0x18) = GS(u32, sp + 0x6C);
                GS(u32, sp + 0x1C) = mode;
                GS(u32, sp + 0x20) = GS(u32, sp + 0x74);
                GCALL4(F_T_GLYPH, code & 0xFFFF, (u32)fade, (u32)color, (u32)(s32)(s16)x);
                x = (u16)(x + 0xE);
                LOOP_POLL(0x80070C68);
                continue;
            }
            u32 g = c - 0x10;
            GCALL1(F_SET_POLY_FT4, GS(u32, pp));
            u32 p = GS(u32, pp);
            if (mode == 1) GV(u16, p + 0xE) = 0x3FD4;
            else GV(u16, p + 0xE) = (u16)(((GV(u16, 0x80080EF8) + 0x1E3) << 6) | 0x17);
            s32 gw = T_GLYPH_W;
            s32 h = (s32)((u32)gw * GS(u32, sp + 0x74)) >> 8;
            glyph_xy(p, x, (s16)y, adv, h);
            if (gw == 0xE) {
                u32 n = g & 0xFFFF;
                u32 row = n / 18;
                u32 u = ((n - row * 18) & 0xFFFF) << 3;
                u32 v = (row & 0xFFFF) * 14;
                glyph_uv(p, (u8)u, (u8)(v + 0x38), (u8)(u + 8), (u8)(v + 0x46));
            } else {
                u32 n = g & 0xFFFF;
                u32 u = (n & 0x1F) << 3;
                u32 u1 = u + 8;
                if ((s32)u1 >= 0x100) u1 = u + 7;
                u32 v = (n >> 5) << 3;
                glyph_uv(p, (u8)u, (u8)(v - 0x38), (u8)u1, (u8)(v - 0x30));
            }
            if (fade == 0x80 || mode == 2) {
                GV(u16, GS(u32, pp) + 0x16) = 0x16;
                GCALL2(F_SET_SEMI, GS(u32, pp), 0);
            } else {
                GV(u16, GS(u32, pp) + 0x16) = mode == 0 ? 0x36 : 0x56;
                GCALL2(F_SET_SEMI, GS(u32, pp), 1);
            }
            glyph_tint(pp, fade, color);
            GCALL2(F_ADD_PRIM, ((u32)BUF << 10) + (GS(u32, sp + 0x6C) << 2) + OT_PRIMS, GS(u32, pp));
            GS(u32, pp) = GS(u32, pp) + 0x28;
            x = (u16)(x + adv);
            LOOP_POLL(0x80071014);
        }
        y = (u16)(y + ((s32)((u32)T_LINE_H * GS(u32, sp + 0x74)) >> 8));
        LOOP_POLL(0x80071040);
    }
}

/* 80071078: draw a two-byte glyph (a 14x14 cell of the big font: 18 to
 * a row, the first 0x120 in one texture page, the rest in the next):
 * code, fade, colour, x, [y, packet pointer's address, ordering table
 * slot, mode, scale]. */
void n_title_Glyph(void) {
    u32 args = R[29] + 0x10;
    FRAME(0x38);
    s32 y = GS(s32, args);
    u32 pp = GS(u32, args + 4), slot = GS(u32, args + 8), mode = GS(u32, args + 0xC);
    s32 scale = GS(s32, args + 0x10);
    u32 code = A0;
    s32 fade = (s32)A1, color = (s32)A2, x = (s32)A3;
    GCALL1(F_SET_POLY_FT4, GS(u32, pp));
    u32 p = GS(u32, pp);
    if (mode == 1) GV(u16, p + 0xE) = 0x3FD4;
    else GV(u16, p + 0xE) = (u16)(((GV(u16, 0x80080EF8) + 0x1E3) << 6) | 0x17);
    s32 d = (s32)((u32)scale * 14) >> 8;
    glyph_xy(p, x, y, d, d);
    u32 n = code & 0xFFFF, page;
    if (n < 0x120) page = 0x140;
    else { n = (code - 0x120) & 0xFFFF; page = 0x180; }
    u32 row = n / 18;
    u32 u = (((n - row * 18) & 0xFFFF) * 14);
    u32 v = (row & 0xFFFF) * 14;
    glyph_uv(p, (u8)u, (u8)v, (u8)(u + 0xE), (u8)(v + 0xE));
    if (fade == 0x80 || mode == 2) {
        GV(u16, GS(u32, pp) + 0x16) = (u16)((page >> 6) | 0x10);
        GCALL2(F_SET_SEMI, GS(u32, pp), 0);
    } else {
        GV(u16, GS(u32, pp) + 0x16) = (u16)((page >> 6) | (mode ? 0x50 : 0x30));
        GCALL2(F_SET_SEMI, GS(u32, pp), 1);
    }
    glyph_tint(pp, fade, color);
    GCALL2(F_ADD_PRIM, ((u32)BUF << 10) + slot * 4 + OT_PRIMS, GS(u32, pp));
    GS(u32, pp) = GS(u32, pp) + 0x28;
}

/* 80071528: empty both text windows; window 0 fully bright. */
void n_text_Clear(void) {
    GV(u8, TEXT_BUF + 0x26C) = 0xFF;
    GV(u8, TEXT_BUF) = 0xFF;
    T_TEXT_FADE = 0x80;
    V0 = 0x80;
}

/* 8007154C: copy text (up to its 0xFF) into window `window` at (x, y). */
void n_text_Set(void) {
    u32 src = A0, win = A1;
    u32 dst = win ? TEXT_BUF + 0x26C : TEXT_BUF;
    u8 c = GV(u8, src++);
    GV(u8, dst++) = c;
    if (c != 0xFF) {
        do {
            c = GV(u8, src++);
            GV(u8, dst++) = c;
            if (c != 0xFF) LOOP_POLL(0x80071594);
        } while (c != 0xFF);
    }
    GV(u16, TEXT_POS + win * 4) = (u16)A2;
    GV(u16, TEXT_POS + win * 4 + 2) = (u16)A3;
    T_LINE_H = 0xF;
    T_GLYPH_W = 0xE;
    V0 = 0xE;
}

/* 800715D0: window `window` has text. */
void n_text_Shown(void) {
    V0 = GV(u8, TEXT_BUF + A0 * 0x26C) != 0xFF;
}

/* 80071604: mark menu item `item` of window `window` as the chosen one:
 * each cursor mark (0xF9, colour byte) gets 0x80 for it, 0x20 for the
 * others (as the original, every other mark from one it set). With
 * `skip`, start after the window's first 0xF6. */
void n_text_Cursor(void) {
    u32 item = A0;
    u32 a = TEXT_BUF + A1 * 0x26C;
    u32 n = 0;
    if (A2) {
        u32 c = GV(u8, a);
        a++;
        if (c != 0xF6) {
            u32 d;
            do {
                d = GV(u8, a);
                a++;
                if (d != 0xF6) LOOP_POLL(0x80071648);
            } while (d != 0xF6);
        }
    }
    u32 c = GV(u8, a);
    if (c == 0xFF) return;
    for (;;) {
        if (c == 0xF9) {
            a++;
            GV(u8, a) = n == item ? 0x80 : 0x20;
            a++;
            u32 d = GV(u8, a);
            n++;
            if (d != 0xFF) {
                for (;;) {
                    if (d == 0xF9) { LOOP_POLL(0x800716A8); a++; break; }
                    a++;
                    d = GV(u8, a);
                    if (d == 0xFF) break;
                    LOOP_POLL(0x800716BC);
                }
            }
        }
        a++;
        c = GV(u8, a);
        if (c == 0xFF) return;
        LOOP_POLL(0x800716D0);
    }
}

/* 800717BC: draw a text window: text, fade, x, y, [packet pointer's
 * address, ordering table slot]. Fade 0 draws the text's second string;
 * below full brightness a shadow (mode 1, one slot behind) too. */
void n_title_DrawWindow(void) {
    u32 args = R[29] + 0x10;
    FRAME(0x40);
    u32 pp = GS(u32, args), slot = GS(u32, args + 4);
    u32 t = A0, fade = A1, x = A2, y = A3;
    if (fade == 0) {
        u32 c = GV(u8, t);
        t++;
        if (c != 0xFF) {
            do {
                c = GV(u8, t);
                t++;
                if (c != 0xFF) LOOP_POLL(0x80071818);
            } while (c != 0xFF);
        }
    }
    GS(u32, sp + 0x10) = pp;
    GS(u32, sp + 0x14) = slot;
    GS(u32, sp + 0x18) = 0;
    GS(u32, sp + 0x1C) = 0x100;
    GCALL4(F_T_TEXT, t, fade, x, y);
    if (fade != 0x80) {
        GS(u32, sp + 0x14) = slot + 1;
        GS(u32, sp + 0x10) = pp;
        GS(u32, sp + 0x18) = 1;
        GS(u32, sp + 0x1C) = 0x100;
        GCALL4(F_T_TEXT, t, fade, x, y);
    }
}
