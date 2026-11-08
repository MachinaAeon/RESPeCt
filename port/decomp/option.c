/* The in-game Option menu (OPTION, loaded at slot 4, 0x800C3E48, right
 * after TEST; opened with select): the carousel of SOUND / MONITOR
 * TUNING / KEY CONFIG / ADJUST SCREEN / RESET / EXIT and their screens.
 *
 * Everything on screen is a sprite in a table of 72 (0x800EDA50, 0x38
 * bytes each):
 *   +0x04 u16 width (or texture x)     +0x06 u16 flags / texture page
 *   +0x08 / +0x0A u16 size             +0x0E u16 colour set
 *   +0x10 / +0x12 s16 x, y             +0x14 / +0x16 u16 copies of +8, +0xA
 *   +0x18 u16 depth                    +0x1A-0x1E u16 r, g, b
 *   +0x20 u16 flags (1 free, 2 shown)  +0x22 u16 scale (0x100 = 1)
 *   +0x24-0x28 u16 the update's own parameters
 *   +0x30 pointer to its text          +0x34 its update function
 * Each frame every sprite's update runs with 0x800CCE58 pointing at it.
 * The updates hide their sprite (+0x20 = 1) while the menu closes
 * (0x800A55A2 == 4) and otherwise show it, highlighted (colour set 1)
 * when it's the current item (0x800CCBE8), else dimmed (3).
 *
 *   0x800CCBD0  the screen being shown (3: a setting being changed)
 *   0x800CCBDC  how far the current value box is slid in (signed)
 *   0x800CCBE0  how far the menu is open
 *   0x800CCBEC  the current row of a list; 0x800CCBF0 the item edited
 *   0x80062E38  the controller mode (0x80000 pad, 0x200000 mouse,
 *               0x400000 the other pad layout)
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq, scenario options_game). */
#include "poll.h"

void n_option_ClearMarks(void);
void n_option_HideOnClose(void);
void n_option_DimUnlessCurrent(void);
void n_option_DimUnlessCurrentB(void);
void n_option_NumberText(void);
void n_option_EndMouseDrag(void);
void n_option_AllocSprite(void);
void n_option_ArrowPair(void);
void n_option_MouseShown(void);
void n_option_SlideOnEdit(void);
void n_option_ItemLabel(void);
void n_option_InitSprite(void);
void n_option_ValueBox(void);
void n_option_RowMarker(void);
void n_option_AttachedText(void);
void n_option_ValueSlider(void);
void n_option_Pressed(void);
void n_option_InitCursors(void);
void n_option_ScrollArrow(void);
void n_option_ListEntry(void);
void n_option_ValueButton(void);
void n_option_Wheel(void);
void n_option_KeyGlyph(void);
void n_option_EdgeSprite(void);
void n_option_ValueText(void);
void n_option_RowNumber(void);
void n_option_RunSprites(void);
void n_option_LoadText(void);
void n_option_Open(void);
void n_option_Frame(void);
void n_option_DrawText(void);
void n_option_Draw(void);
void n_option_DrawTextEx(void);
void n_option_DrawSprites(void);
void n_option_DrawBigText(void);
void n_option_ResetScreen(void);
void n_option_Carousel(void);
void n_option_AdjustScreen(void);
void n_option_KeyConfig(void);
void n_option_SoundScreen(void);

#define OPT_STATE    GV(s16, 0x800A55A2u)
#define CUR          GV(u32, 0x800CCE58u)      /* the sprite being updated */
#define SCREEN       GV(u32, 0x800CCBD0u)
#define SLIDE        GV(s32, 0x800CCBDCu)
#define OPENED       GV(s32, 0x800CCBE0u)
#define CURRENT      GV(u32, 0x800CCBE8u)
#define ROW          GV(u32, 0x800CCBECu)
#define EDITED       GV(u32, 0x800CCBF0u)
#define PAD_MODE     GV(u32, 0x80062E38u)
#define SPRITES      0x800EDA50u
#define SPRITE(i)    (SPRITES + (u32)(i) * 0x38)

/* The menu is closing: hide this sprite. 1 if so. */
static int hide_on_close(void) {
    if (OPT_STATE != 4) return 0;
    MEM_W16(CUR + 0x20, 1);
    return 1;
}

/* 800CBD48: clear the two mark bytes (0x800CCC6C and +0xF0). */
void n_option_ClearMarks(void) {
    MEM_W8(0x800CCC6Cu + 0xF0, 0xFF);
    MEM_W8(0x800CCC6Cu, 0xFF);
    V0 = 0xFF;
}

/* 800CBEA8: an update that only hides its sprite when the menu closes. */
void n_option_HideOnClose(void) { hide_on_close(); }

/* 800CC03C / 800CC0DC: highlighted if it's the current item (+0x24),
 * else dimmed. */
static void dim_unless_current(void) {
    if (hide_on_close()) return;
    u32 e = CUR;
    MEM_W16(e + 0xE, MEM_R16(e + 0x24) != CURRENT ? 3 : 1);
}
void n_option_DimUnlessCurrent(void) { dim_unless_current(); }
void n_option_DimUnlessCurrentB(void) { dim_unless_current(); }

/* 800CC090 (n, buf): a number as glyph codes (0xD minus, then 0x10 + n,
 * 0xFF end); also sets the text position (0x80062E18) from 0x800CCC24. */
static u32 number_text(s32 n, u32 b);
void n_option_NumberText(void) { V0 = number_text((s32)A0, A1); }
static u32 number_text(s32 n, u32 b) {
    if (n < 0) {
        MEM_W8(b, 0xD);
        b++;
        n = -n;
    }
    MEM_W8(b, (u32)n + 0x10);
    MEM_W8(b + 1, 0xFF);
    GV(u16, 0x80062E18u) = GV(u16, 0x800CCC24u);
    u32 v = GV(u16, 0x800CCC26u);
    GV(u16, 0x80062E1Au) = (u16)v;
    return v;
}

/* 800CBDE4: once per frame: in mouse mode, end a drag that has been let go
 * of (800113B4), then run the cursor update (8006F1E4). */
void n_option_EndMouseDrag(void) {
    FRAME(0x18);
    if (GV(u8, 0x80062E2Cu) == 0 && PAD_MODE == 0x200000) {
        gcall(0x800113B4u, R[4], R[5], R[6], R[7]);
        GV(u8, 0x80062E2Cu) = 0;
        GV(u8, 0x80062E40u) = 0;
    }
    gcall(0x8006F1E4u, R[4], R[5], R[6], R[7]);
}

/* 800CB248: take a free sprite (flags bit 0) and reset it: white, scale
 * 1. Its index, or -1. */
static u32 alloc_sprite(void) {
    u32 e = SPRITES;
    for (s32 i = 0;;) {
        u32 f = MEM_R16(e + 0x20);
        if (f & 1) {
            MEM_W16(e + 0x20, f & 0xFFFE);
            MEM_W16(e + 0x1E, 0x80);
            MEM_W16(e + 0x1C, 0x80);
            MEM_W16(e + 0x1A, 0x80);
            MEM_W32(e + 0x34, 0);
            MEM_W16(e + 0x22, 0x100);
            MEM_W16(e + 0xE, 0);
            return (u32)i;
        }
        i++;
        e += 0x38;
        if (!(i < 72)) break;
        LOOP_POLL(0x800CB29Cu);
    }
    return 0xFFFFFFFFu;
}
void n_option_AllocSprite(void) { V0 = alloc_sprite(); }

/* 800CBF40: the left / right arrow pair's glyphs (its text's codes 0 and
 * 0xC), lit on the side that can still move. */
void n_option_ArrowPair(void) {
    if (hide_on_close()) return;
    u32 l, r;
    if (CURRENT != 0) { l = 0xFC03; r = 0xFC01; }
    else { l = 0xFC01; r = 0xFC03; }
    u32 t = MEM_R32(CUR + 0x30);
    MEM_W16(t, l);
    MEM_W16(t + 0xC, r);
}

/* 800CBED0: shown only in mouse mode. */
void n_option_MouseShown(void) {
    if (hide_on_close()) return;
    u32 e = CUR;
    if (GV(u32, 0x800CCC10u) == 0x200000) MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 2);
    else MEM_W16(e + 0x20, MEM_R16(e + 0x20) & 0xFFFD);
}

/* 800CBFA8: the current item moves 0x20 down (y base +0x26) while its
 * setting is being changed. */
void n_option_SlideOnEdit(void) {
    if (hide_on_close()) return;
    u32 e = CUR;
    if (SCREEN == 3 && MEM_R16(e + 0x24) == CURRENT && !(OPENED < 3)) {
        MEM_W16(e + 6, MEM_R16(e + 0x26) + 0x20);
        return;
    }
    e = CUR;
    MEM_W16(e + 6, MEM_R16(e + 0x26));
}

/* 800C5FC8: a carousel label: shown, highlighted when current, its x
 * (+0x10) from +0x26, 0x40 left for a two-line label (text starting with
 * 0xD). */
void n_option_ItemLabel(void) {
    if (hide_on_close()) return;
    u32 e = CUR;
    u32 cur = CURRENT;
    u32 f = MEM_R16(e + 0x20), id = MEM_R16(e + 0x24);
    MEM_W16(e + 0x20, f | 2);
    MEM_W16(e + 0xE, id != cur ? 3 : 1);
    e = CUR;
    if (MEM_R8(MEM_R32(e + 0x30)) == 0xD) MEM_W16(e + 0x10, MEM_R16(e + 0x26) - 0x40);
    else MEM_W16(e + 0x10, MEM_R16(e + 0x26));
}

/* 800CBCB0 (sprite, template): set a sprite up from a 16-byte template:
 * its size kept in +0x14, the width field scaled (x2, x4 for a texture
 * without a page), the page byte widened, scale 1. */
static u32 init_sprite(u32 d, u32 s);
void n_option_InitSprite(void) { V0 = init_sprite(A0, A1); }
static u32 init_sprite(u32 d, u32 s) {
    for (u32 i = 0; i < 16; i++) MEM_W8(d + i, MEM_R8(s + i));
    u32 w = MEM_R16(d + 8), h = MEM_R16(d + 0xA), page = MEM_R16(d);
    MEM_W16(d + 0x14, w);
    MEM_W16(d + 0x16, h);
    if (page == 0) MEM_W16(d + 4, (MEM_R16(d + 4) & 0x3F) << 2);
    else MEM_W16(d + 4, (MEM_R16(d + 4) & 0x3F) << 1);
    u32 b = MEM_R8(d + 6);
    MEM_W16(d + 0x22, 0x100);
    MEM_W16(d + 6, b);
    return 0x100;
}

/* 800C919C: the value box sliding in beside the item being changed: its
 * width 0xE0 less the slide, size the slide. */
void n_option_ValueBox(void) {
    if (hide_on_close()) return;
    if (SCREEN == 3) {
        u32 e = CUR;
        if (CURRENT == MEM_R16(e + 0x24)) {
            s32 v = SLIDE;
            if (v != 0) {
                MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 2);
                if (v < 0) v = -v;
                MEM_W16(e + 4, 0xE0 - (u32)v);
                MEM_W16(e + 0x14, (u32)v);
                MEM_W16(e + 8, (u32)v);
                return;
            }
        }
    }
    u32 e = CUR;
    MEM_W16(e + 0x20, MEM_R16(e + 0x20) & 0xFFFD);
}

/* 800C9364: a list row's marker: shown with its row's sprite (+0x26),
 * highlighted on the current row (0x800CCBEC), x following the row's. */
void n_option_RowMarker(void) {
    if (hide_on_close()) return;
    u32 e = CUR;
    u32 row = SPRITE(MEM_R16(e + 0x26));
    if (!(MEM_R16(row + 0x20) & 2)) {
        MEM_W16(e + 0x20, MEM_R16(e + 0x20) & 0xFFFD);
        return;
    }
    u32 f = MEM_R16(e + 0x20), id = MEM_R16(e + 0x24);
    u32 r = ROW;
    MEM_W16(e + 0x20, f | 2);
    MEM_W16(e + 0xE, r != id ? 3 : 1);
    e = CUR;
    MEM_W16(e + 0x10, MEM_R16(row + 0x10) + MEM_R16(e + 0x28));
}

/* 800C4F84: a sprite carried by another (+0x24): placed at its offset
 * (+0x26, +0x28) scaled with it, just in front, the same colour and
 * scale. */
void n_option_AttachedText(void) {
    if (hide_on_close()) return;
    u32 e = CUR;
    u32 f = MEM_R16(e + 0x20), id = MEM_R16(e + 0x24);
    MEM_W16(e + 0x20, f | 2);
    u32 p = SPRITE(id);
    u32 ox = MEM_R16(e + 0x26);
    MEM_W16(e + 0x10, MEM_R16(p + 0x10) + (u32)((s32)(ox * MEM_R16(p + 0x22)) >> 4));
    u32 oy = MEM_R16(e + 0x28);
    MEM_W16(e + 0x12, MEM_R16(p + 0x12) + (u32)((s32)(oy * MEM_R16(p + 0x22)) >> 4));
    MEM_W16(e + 0x18, MEM_R16(p + 0x18) - 2);
    MEM_W16(e + 0x22, MEM_R16(p + 0x22));
    u32 c = MEM_R16(p + 0x1A);
    MEM_W16(e + 0x1E, c);
    MEM_W16(e + 0x1C, c);
    MEM_W16(e + 0x1A, c);
}

/* 800C9250: the slider of the setting being changed (screen 3, item 0):
 * lowered 0x10 unless it's the edited item; x from +0x26 plus the slide,
 * shown once past 48. */
void n_option_ValueSlider(void) {
    if (hide_on_close()) return;
    if (SCREEN != 3 || CURRENT != 0) {
        u32 e = CUR;
        MEM_W16(e + 0x20, MEM_R16(e + 0x20) & 0xFFFD);
        return;
    }
    if (OPENED != 0 && EDITED == MEM_R16(CUR + 0x24)) {
        u32 e = CUR;
        MEM_W16(e + 6, MEM_R16(e + 0x28));
    } else {
        u32 e = CUR;
        MEM_W16(e + 6, MEM_R16(e + 0x28) + 0x10);
    }
    u32 e = CUR;
    s32 v = SLIDE;
    u32 x = MEM_R16(e + 0x26) - 0x93;
    if (v < 0) v = -v;
    u32 t = x + (u32)v;
    if ((s32)t < 48) {
        MEM_W16(e + 0x20, MEM_R16(e + 0x20) & 0xFFFD);
    } else {
        u32 f = MEM_R16(e + 0x20);
        MEM_W16(e + 0x10, t << 4);
        MEM_W16(e + 0x20, f | 2);
    }
}

/* 800CB924 (pad, -, button): whether a menu button is held in the
 * buttons pad, by controller mode: button 0x20 confirm, 0x40 cancel,
 * 0x7FF / 0x800 the two extra ones (mouse: both buttons / a button,
 * swapped by 0x80062DD6). The polls are the original's backward jumps. */
static u32 pressed(u32 pad, s32 b);
void n_option_Pressed(void) { V0 = pressed(A0, (s16)A2); }
static u32 pressed(u32 pad, s32 b) {
    u32 mode = PAD_MODE;
    if (mode == 0x200000) {
        if (b == 0x40) {
            if (GV(s16, 0x80062DD6u) != 0) { LOOP_POLL(0x800CBA5Cu); return pad & 0x800; }
            return pad & 0x40;
        }
        if (b < 65) {
            if (b == 0x20) { LOOP_POLL(0x800CBA10u); return pad & 0x20; }
            return 0;
        }
        if (b == 0x7FF) { return (pad & 0x840) == 0x840; }
        if (b != 0x800) { return 0; }
        if (GV(s16, 0x80062DD6u) != 0) { return pad & 0x40; }
        LOOP_POLL(0x800CBA48u);
        return pad & 0x800;
    }
    if ((s32)0x200000 < (s32)mode) {
        if (mode != 0x400000) { return 0; }
        if (b == 0x40) { return pad & 0x2000; }
        if (b < 65) { return b == 0x20 ? pad & 0x20 : 0; }
        if (b == 0x7FF) { return pad & 0x40; }
        if (b != 0x800) { return 0; }
        return (pad & 0x8000) ? 0xFFFF8000u : 0;
    }
    if (mode != 0x80000) { return 0; }
    if (b == 0x40) { LOOP_POLL(0x800CB9CCu); return pad & 0x2000; }
    if (b < 65) {
        LOOP_POLL(0x800CB9D8u);
        return b == 0x20 ? pad & 0x20 : 0;
    }
    if (b != 0x7FF) {
        LOOP_POLL(0x800CB9E4u);
        if (b != 0x800) { return 0; }
        return (pad & 0x8000) ? 0xFFFF8000u : 0;
    }
    return pad & 0x40;
}

/* 800C40D4: set up the menu's two edge sprites (x 0 and 0xFF0, update
 * 800C41E8) and its state: no item picked (-2), the controller mode to
 * show (0x800CCC10: the mode, or 0x400000 for an analog pad, 0x3000 in
 * 0x80062ED0). */
void n_option_InitCursors(void) {
    FRAME(0x28);
    GV(u8, 0x80062DECu) = 0;
    for (u32 i = 0;;) {
        u32 n = alloc_sprite();
        if (n != 0xFFFFFFFFu) {
            u32 e = SPRITE(n);
            MEM_W16(e + 0x10, i ? 0xFF0 : 0);
            MEM_W16(e + 0x18, 5);
            MEM_W16(e + 0x12, 0);
            MEM_W16(e + 0x24, 0xFFFF);
            MEM_W16(e + 0x26, i);
            MEM_W32(e + 0x34, 0x800C41E8u);
        }
        i++;
        if (!((s32)i < 2)) break;
        LOOP_POLL(0x800C4164u);
    }
    GV(u32, 0x800CCBF8u) = 0xFFFFFFFEu;
    GV(u32, 0x800CCBCCu) = 1;
    SCREEN = 0;
    GV(u32, 0x800CCBD4u) = 0;
    u32 mode = PAD_MODE;
    u32 dual = GV(u32, 0x80062ED0u) & 0x3000;
    GV(u32, 0x800CCC10u) = mode;
    if (dual) GV(u32, 0x800CCC10u) = 0x400000;
    u32 m = GV(u32, 0x800CCC10u);
    GV(u32, 0x800CCC18u) = 0;
    GV(u32, 0x800CCC14u) = m;
}

/* The update hides its sprite (it stays allocated). */
static void hide(void) {
    u32 e = CUR;
    MEM_W16(e + 0x20, MEM_R16(e + 0x20) & 0xFFFD);
}

/* 800C7160: a list's scroll arrow (+0x28: 0 up, else down): hidden on
 * screens 3, 4 and 6 (up) or for the mouse's third row (down); nudged
 * 0x10 away from the list while its row (+0x24, against 0x800CCBE0)
 * is open. */
void n_option_ScrollArrow(void) {
    if (hide_on_close()) return;
    u32 e = CUR;
    if (MEM_R16(e + 0x28) == 0) {
        u32 k = GV(u32, 0x800CCBE4u);
        if (k - 3 < 2 || k == 6) { hide(); return; }
        if (MEM_R16(e + 0x24) == (u32)OPENED && !(SLIDE < 3)) {
            MEM_W16(e + 6, MEM_R16(e + 0x26) + 0x10);
            goto shown;
        }
    } else {
        if (GV(u32, 0x800CCC10u) == 0x200000 && MEM_R16(e + 0x24) == 2) {
            LOOP_POLL(0x800C723Cu);
            hide();
            return;
        }
        if (MEM_R16(e + 0x24) == (u32)OPENED && !(SLIDE < 3)) {
            MEM_W16(e + 6, MEM_R16(e + 0x26) - 0x10);
            goto shown;
        }
    }
    e = CUR;
    MEM_W16(e + 6, MEM_R16(e + 0x26));
shown:
    e = CUR;
    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 2);
}

/* 800C72B8: an entry of a list (+0x28: 0 the items, 1 the rows, 2 a
 * fixed label): highlighted when current (texture colours 0x1FC / 0x1FD
 * for a sprite without a palette), lowered 0x10 while it's the open
 * one. */
void n_option_ListEntry(void) {
    if (hide_on_close()) return;
    u32 e = CUR;
    u32 kind = MEM_R16(e + 0x28), sel = 0;
    if (kind == 0) {
        u32 k = GV(u32, 0x800CCBE4u);
        if (k - 3 < 2 || k == 6) { hide(); return; }
        sel = CURRENT;
    } else if (kind == 1) {
        if (GV(u32, 0x800CCC10u) == 0x200000 && MEM_R16(e + 0x24) == 2) {
            LOOP_POLL(0x800C7374u);
            hide();
            return;
        }
        sel = ROW;
    }
    e = CUR;
    u32 f = MEM_R16(e + 0x20) | 2, k2 = MEM_R16(e + 0x28);
    MEM_W16(e + 0x20, f);
    if (k2 == 2) return;
    u32 c;
    if (f & 0xE0) c = sel != MEM_R16(e + 0x24) ? 3 : 1;
    else c = sel != MEM_R16(e + 0x24) ? 0x1FD : 0x1FC;
    MEM_W16(e + 0xE, c);
    e = CUR;
    if ((u32)OPENED == MEM_R16(e + 0x24) && !(SLIDE < 3)) {
        MEM_W16(e + 0x12, MEM_R16(e + 0x26) + 0x10);
        return;
    }
    e = CUR;
    MEM_W16(e + 0x12, MEM_R16(e + 0x26));
}

/* 800C95FC: the confirm / cancel buttons beside a value being changed
 * (items 1 and 2 on screen 3), sliding in with the box; lit while their
 * button (+0x24: 1 cancel, else the extra button) is held. */
void n_option_ValueButton(void) {
    FRAME(0x18);
    if (hide_on_close()) return;
    if (SCREEN != 3) { hide(); return; }
    u32 sel = CURRENT;
    if (!(sel - 1 < 2)) { hide(); return; }
    s32 v = SLIDE;
    if (v < 0) v = -v;
    u32 e = CUR;
    u32 t = (u32)v + (MEM_R16(e + 0x26) - 0x93);
    if ((s32)t < 48) {
        MEM_W16(e + 0x20, MEM_R16(e + 0x20) & 0xFFFD);
        return;
    }
    u32 f = MEM_R16(e + 0x20);
    MEM_W16(e + 0x10, t << 4);
    MEM_W16(e + 0x20, f | 2);
    MEM_W16(e + 0x12, sel != 1 ? 0x780 : 0x590);
    s32 btn = MEM_R16(CUR + 0x24) != 1 ? 0x800 : 0x40;
    u32 r = pressed((u32)(s32)GV(s16, 0x800B4B94u), btn);
    e = CUR;
    if ((r << 16) != 0) MEM_W16(e + 0xE, MEM_R16(e + 0x28) + 1);
    else MEM_W16(e + 0xE, MEM_R16(e + 0x28) + 3);
}

/* 800C4E1C: one of the carousel's items, placed on the wheel by angle
 * (the current item 0x800CCBE8 plus its own +0x24, 4096 a turn): raised
 * and scaled up toward the front, darker toward the back. */
void n_option_Wheel(void) {
    FRAME(0x20);
    if (hide_on_close()) return;
    u32 e = CUR;
    u32 ang = (CURRENT + MEM_R16(e + 0x24) + 0x400) & 0xFFF;
    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 2);
    u32 sn = gcall(0x80029FF8u, ang, R[5], R[6], R[7]);
    s32 a = (s32)(sn * 12) >> 12;
    u32 cs = gcall(0x8002A0C8u, ang, R[5], R[6], R[7]);
    s32 q = (a + 0xC) << 6;
    u32 hi = (u32)(((s64)q * 0x2AAAAAAB) >> 32);
    u32 y = (u32)a + 0x5E;
    u32 d = 0x6C - y;
    e = CUR;
    MEM_W16(e + 0x12, y << 4);
    MEM_W16(e + 0x18, d);
    u32 scale = (u32)((s32)hi >> 3) - (u32)(q >> 31) + 0xC0;
    u32 w = MEM_R16(e + 8);
    MEM_W16(e + 0x22, scale);
    u32 prod = (w + 0x1C) * (scale & 0xFFFF);
    s32 t = (s32)(d << 16) >> 16;
    t -= 2;
    u32 x = (u32)((s32)(cs * 120) >> 12) + 0xA0;
    u32 c = 0x80 - (u32)(t * 3);
    s32 half = ((s32)prod >> 8) + (s32)(prod >> 31);
    x = (x - (u32)(half >> 1)) << 4;
    MEM_W16(e + 0x10, x);
    if (c != 0x80) {
        u32 c2 = c << 1;
        c = (u32)(((s64)(s32)c2 * 0x55555556) >> 32) - (u32)((s32)c2 >> 31);
    }
    MEM_W16(e + 0x1E, c);
    MEM_W16(e + 0x1C, c);
    MEM_W16(e + 0x1A, c);
}

/* 800C7448: a button glyph in the key config picture: the button
 * (+0x24) assigned on the current row (0x800CCB6C: 8 halfwords per row;
 * row 3 is the layout, 0x800CCC28 / 0x800CCC2C, and the mouse's rows
 * start at 3), its picture from 0x800CCB4C (6 bytes per glyph) and its
 * place from 0x800CCB94 (the mouse's from 4 on); a wide glyph from 3 on. */
void n_option_KeyGlyph(void) {
    if (hide_on_close()) return;
    u32 mode = GV(u32, 0x800CCC10u);
    u32 row = ROW;
    if (mode == 0x200000) row += 3;
    u32 off, id;
    if (ROW == 3) {
        if (mode == 0x200000) off = (GV(u32, 0x800CCC2Cu) + 3) << 3;
        else off = GV(u32, 0x800CCC28u) << 3;
        id = MEM_R16(CUR + 0x24);
    } else {
        id = MEM_R16(CUR + 0x24);
        off = row << 3;
    }
    s32 v = GV(s16, 0x800CCB6Cu + id * 2 + off);
    if (v <= 0) { hide(); return; }
    u32 e = CUR;
    u32 k = MEM_R16(e + 0x24);
    if (GV(u32, 0x800CCC10u) == 0x200000) k += 4;
    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 2);
    u32 g = 0x800CCB4Cu + (u32)v * 6;
    MEM_W16(e + 4, MEM_R16(e + 0x26) + GV(u16, g));
    MEM_W16(e + 6, MEM_R16(e + 0x28) + GV(u16, g + 2));
    u32 q = 0x800CCB94u + k * 4;
    MEM_W16(e + 0x10, GV(u16, q));
    MEM_W16(e + 0x12, GV(u16, q + 2));
    if (v >= 3) {
        u32 x = MEM_R16(e + 0x10);
        MEM_W16(e + 0x14, 0x48);
        MEM_W16(e + 8, 0x48);
        MEM_W16(e + 0x10, x - 0x40);
    } else {
        MEM_W16(e + 0x14, 0x40);
        MEM_W16(e + 8, 0x40);
    }
}

/* 800C41E8: the update of the two edge sprites (InitCursors): the
 * picture for the screen shown (0x800CCBF8: -1 hidden, -2 none; else a
 * 20-byte template at 0x800CC334), stretched to the screen's half (the
 * right one, +0x26 1, at x 0xFF0, one pixel narrower; pictures 7 (pad
 * only) and 11 one texel right, picture 4 a line shorter). */
void n_option_EdgeSprite(void) {
    FRAME(0x20);
    s32 pic = (s32)GV(u32, 0x800CCBF8u);
    if (pic == -1) { MEM_W16(CUR + 0x20, 1); return; }
    if (pic == -2) {
        u32 e = CUR;
        u32 f = MEM_R16(e + 0x20);
        MEM_W16(e + 0x24, 0xFFFE);
        MEM_W16(e + 0x20, f & 0xFFFD);
        return;
    }
    u32 e = CUR;
    if (MEM_R16(e + 0x24) == (u32)pic) return;
    MEM_W16(e + 0x24, GV(u16, 0x800CCBF8u));
    u32 f = MEM_R16(e + 0x20), id = MEM_R16(e + 0x24);
    MEM_W16(e + 0x20, f | 2);
    init_sprite(e, 0x800CC334u + id * 20);
    e = CUR;
    if (MEM_R16(e + 0x26) == 0) {
        if (MEM_R16(e) != 2) {
            MEM_W16(e + 0x14, 0xFF);
            MEM_W16(e + 8, 0xFF);
        } else {
            MEM_W16(e + 8, 0x100);
            MEM_W16(e + 0x14, 0x100);
        }
        return;
    }
    if (MEM_R16(e) == 2) {
        MEM_W16(e + 0x14, 0x3F);
        MEM_W16(e + 8, 0x3F);
        u32 h = MEM_R16(e + 2);
        MEM_W16(e + 0x10, 0x1000);
        MEM_W16(e + 2, h + 4);
        return;
    }
    MEM_W16(e + 0x14, 0x40);
    MEM_W16(e + 8, 0x40);
    u32 h = MEM_R16(e + 2);
    MEM_W16(e + 0x10, 0xFF0);
    MEM_W16(e + 2, h + 2);
    u32 p = GV(u32, 0x800CCBF8u);
    if (p == 7 ? GV(u32, 0x800CCC10u) != 0x200000 : p == 0xB) {
        e = CUR;
        MEM_W16(e + 4, MEM_R16(e + 4) + 1);
    }
    if (GV(u32, 0x800CCBF8u) == 4) {
        e = CUR;
        MEM_W16(e + 0x16, MEM_R16(e + 0x16) - 1);
    }
}

/* 800C8FC4: a sound setting's value (+0x24: 0 the mode, else a volume;
 * the values at the pointers 0x800CCBC0..): the mode's word (STEREO /
 * MONAURAL, 0x800CC984 / 0x800CC98C) or the volume in up to three
 * digits (0: the text at 0x800CC998), redone when it changes (+0x26). */
void n_option_ValueText(void) {
    if (hide_on_close()) return;
    u32 e = CUR;
    MEM_W16(e + 0xE, CURRENT != MEM_R16(e + 0x24) ? 3 : 1);
    e = CUR;
    u32 p = GV(u32, 0x800CCBC0u + MEM_R16(e + 0x24) * 4);
    if (MEM_R32(p) == MEM_R16(e + 0x26)) return;
    MEM_W16(e + 0x26, MEM_R16(p));
    if (MEM_R16(e + 0x24) == 0) {
        if (MEM_R32(p) == 0) {
            MEM_W32(e + 0x30, 0x800CC984u);
            MEM_W16(e + 0x10, 0x400);
        } else {
            MEM_W32(e + 0x30, 0x800CC98Cu);
            MEM_W16(e + 0x10, 0x380);
        }
        return;
    }
    MEM_W16(e + 0x10, 0x4C0);
    s32 v = (s32)MEM_R32(p);
    u32 t = MEM_R32(e + 0x30);
    if (v == 0) {
        for (u32 i = 0;;) {
            MEM_W8(t + i, GV(u8, 0x800CC998u + i));
            i++;
            if (!(i < 4)) break;
            LOOP_POLL(0x800C90CCu);
        }
        return;
    }
    if (!(v < 100)) {
        MEM_W8(t, 0x11);
        v -= 100;
    } else {
        MEM_W8(t, 0);
        u32 c = CUR;
        MEM_W16(c + 0x10, MEM_R16(c + 0x10) - 0x40);
    }
    if (MEM_R8(t) != 0 || !(v < 10)) {
        s32 q = v / 10;
        MEM_W8(t + 1, (u32)q + 0x10);
        v -= q * 10;
    } else {
        MEM_W8(t + 1, 0);
        u32 c = CUR;
        MEM_W16(c + 0x10, MEM_R16(c + 0x10) - 0x40);
    }
    MEM_W8(t + 2, (u32)v + 0x10);
    MEM_W8(t + 3, 0xFF);
}

/* 800C9420: the number of the row being changed, sliding in with its
 * value box: up to three digits (0xD for a hundred, digits from glyph
 * 0xC) at 0x800CCC5C, 0x70 left for each leading blank; row 0 shows the
 * text at 0x800CC99C. */
void n_option_RowNumber(void) {
    if (hide_on_close()) return;
    if (SCREEN != 3 || CURRENT != MEM_R16(CUR + 0x24)) { hide(); return; }
    u32 e = CUR;
    u32 f = MEM_R16(e + 0x20) | 2;
    s32 v = SLIDE;
    MEM_W16(e + 0x20, f);
    if (v < 0) v = -v;
    s32 x = v + 0x1E;
    if (x < 48) { MEM_W16(e + 0x20, f & 0xFFFD); return; }
    s32 n = (s32)ROW;
    MEM_W16(e + 0x10, (u32)x << 4);
    if (n == 0) { MEM_W32(e + 0x30, 0x800CC99Cu); return; }
    const u32 buf = 0x800CCC5Cu;
    if (!(n < 100)) {
        GV(u16, buf) = 0xD;
        n -= (n / 100) * 100;
    } else {
        GV(u16, buf) = 0;
    }
    if (GV(u16, buf) != 0 || !(n < 10)) {
        s32 q = n / 10;
        GV(u16, buf + 2) = (u16)(q + 0xC);
        n -= q * 10;
    } else {
        GV(u16, buf + 2) = 0;
    }
    GV(u16, buf + 4) = (u16)(n + 0xC);
    e = CUR;
    GV(u16, buf + 6) = 0xFFFF;
    MEM_W32(e + 0x30, buf);
    for (u32 i = 0;;) {
        if (GV(u16, buf + i * 2) == 0) MEM_W16(e + 0x10, MEM_R16(e + 0x10) - 0x70);
        i++;
        if (!(i < 2)) break;
        LOOP_POLL(0x800C95ECu);
    }
}

/* 800CBC28: run every sprite's update (0x800CCE58 pointing at it). The
 * updates see the original's s-registers. */
void n_option_RunSprites(void) {
    FRAME(0x28);
    u32 s0 = R[16], s1 = R[17], s2 = R[18], s3 = R[19];
    u32 e = SPRITES;
    for (u32 i = 0;;) {
        if (!(MEM_R16(e + 0x20) & 1)) {
            u32 fn = MEM_R32(e + 0x34);
            if (fn != 0) {
                CUR = e;
                R[16] = e + 0x34; R[17] = e; R[18] = i; R[19] = 0x800D0000u;
                gcall(fn, R[4], R[5], R[6], R[7]);
            }
        }
        i++;
        e += 0x38;
        if (!((s32)i < 72)) break;
        LOOP_POLL(0x800CBC8Cu);
    }
    R[16] = s0; R[17] = s1; R[18] = s2; R[19] = s3;
}

/* 800CBE40 (n): load text file n (+0x24 in the option texts' table
 * 0x800CC9FC) to 0x800CDA50 and set it up (800927D8), unless it's the
 * one loaded (0x800CCC1C). */
static void load_text(u32 n) {
    FRAME(0x20);
    if (n == GV(u32, 0x800CCC1Cu)) return;
    GV(u32, 0x800CCC1Cu) = n;
    GS(u32, sp + 0x10) = 0x800CDA50u;
    u32 s0 = R[16];
    R[16] = 0x800CDA50u;
    gcall(0x800115C0u, 0x800C3E4Cu, 0x8005854Cu, 0x800CC9FCu, n + 0x24);
    gcall(0x800927D8u, 0x800CDA50u, 0xFFFFFFFFu, R[6], R[7]);
    R[16] = s0;
}
void n_option_LoadText(void) { load_text(A0); }

/* 800C3E80: opening the menu: the first texts, the screen (800354F8 with
 * a box 0x17E, 0x1E3, 2, 4), and a copy of the settings it can change
 * (text position, button layouts, screen offset, sound...) to restore
 * on cancel; the pause flag 0x800BBD10 taken over (0x800CCC40). */
void n_option_Open(void) {
    FRAME(0x20);
    GV(u32, 0x800CCBCCu) = 0;
    GV(u32, 0x800CCC04u) = 0;
    GV(u32, 0x800CCC1Cu) = 0xFFFFFFFFu;
    load_text(1);
    GS(u16, sp + 0x10) = 0x17E;
    GS(u16, sp + 0x12) = 0x1E3;
    GS(u16, sp + 0x14) = 2;
    GS(u16, sp + 0x16) = 4;
    gcall(0x800354F8u, sp + 0x10, 0, 0, 0);
    u32 paused = GV(u8, 0x800BBD10u);
    GV(u8, 0x800BBD10u) = 0;
    GV(u8, 0x80062E1Cu) = 0;
    GV(u16, 0x800CCC24u) = GV(u16, 0x80062E18u);
    GV(u16, 0x800CCC26u) = GV(u16, 0x80062E1Au);
    GV(s32, 0x800CCC28u) = GV(s16, 0x80062DD4u);
    GV(s32, 0x800CCC2Cu) = GV(s16, 0x80062DD6u);
    GV(u16, 0x800CCC30u) = GV(u16, 0x80062DACu);
    GV(u16, 0x800CCC32u) = (u16)(GV(u16, 0x80062DAEu) + 4);
    GV(u32, 0x800CCC34u) = GV(u32, 0x80062E20u);
    GV(s32, 0x800CCC38u) = GV(s16, 0x80062DB4u);
    GV(s32, 0x800CCC3Cu) = GV(s16, 0x80062DB6u);
    GV(u32, 0x800CCC40u) = paused;
}

/* 800C3F9C: once per frame: when the controller changes while the menu
 * is up (0x800CCBCC set), re-open on the key config screen for the new
 * one (unless only the pad's analog mode changed); then run the screen's
 * handler (0x800CCABC by 0x800CCBCC) and count the frame. */
void n_option_Frame(void) {
    FRAME(0x18);
    if (GV(u32, 0x800CCBCCu) != 0) {
        gcall(0x80090C90u, R[4], R[5], R[6], R[7]);
        if (OPT_STATE == 0) {
            u32 m = PAD_MODE;
            if (GV(u32, 0x80062ED0u) & 0x3000) m = 0x400000;
            if (m != GV(u32, 0x800CCC14u)) {
                GV(u32, 0x800CCC14u) = m;
                if (GV(u32, 0x800CCC10u) == 0x400000 && m == 0x80000) GV(u32, 0x800CCC18u) = 0;
                else GV(u32, 0x800CCC18u) = 0x10;
            }
            if (!(GV(s32, 0x800CCC18u) < 16) && GV(u32, 0x800CCC10u) != GV(u32, 0x800CCC14u)) {
                GV(u32, 0x800CCBCCu) = 1;
                SCREEN = 4;
                GV(u32, 0x800CCBD4u) = 0;
                gcall(0x80090BF0u, 0, 0x10, R[6], R[7]);
                GV(u32, 0x800CCC04u) = 0;
            }
        }
    }
    gcall(GV(u32, 0x800CCABCu + GV(u32, 0x800CCBCCu) * 4), R[4], R[5], R[6], R[7]);
    GV(u32, 0x800CCC0Cu)++;
}

#define F_SET_POLY_FT4  0x800382C8u
#define F_SET_SEMI      0x80038248u
#define F_SET_SHADE_TEX 0x80038278u
#define F_ADD_PRIM      0x800381E8u
#define BUFFER          GV(u8, 0x80062D1Cu)    /* the drawing buffer, 0 or 1 */
#define PALETTE         GV(u16, 0x800CCE4Cu)   /* the text palette row */

/* 800CA860 (text, x, y, &packets, [depth, scale]): draw a line of 8x8
 * glyphs (0xFF ends; glyph c at u (c & 0x1F) * 8, v (c >> 5) * 8 - 0x38 of
 * the font page) as textured quads, scale 0x100 = 1 pixel per texel
 * (x8 / 0x100 per glyph), offset by the text position 0x800CCC24 and
 * centred on (0xA0, 0x78), into ordering table `depth`. */
void n_option_DrawText(void) {
    FRAME(0x40);
    u32 str = A0, pp = A3;
    u32 depth = GS(u32, sp + 0x50);
    u32 scale = GS(u32, sp + 0x54);
    GS(u16, sp + 0x12) = (u16)A2;
    GS(u16, sp + 0x10) = (u16)A1;
    s32 step = (s32)(scale << 3) >> 8;
    u32 w1 = (u32)step - 0xA0, h1 = (u32)step - 0x78;
    for (;;) {
        u32 c = MEM_R8(str);
        str++;
        if (c == 0xFF) break;
        gcall(F_SET_POLY_FT4, MEM_R32(pp), R[5], R[6], R[7]);
        u32 p = MEM_R32(pp);
        MEM_W16(p + 0xE, ((PALETTE + 0x1E3) << 6) | 0x17);
#define X0 (GS(u16, sp + 0x10) + GV(u16, 0x800CCC24u))
#define Y0 (GS(u16, sp + 0x12) + GV(u16, 0x800CCC26u))
        MEM_W16(MEM_R32(pp) + 0x8, X0 - 0xA0);
        MEM_W16(MEM_R32(pp) + 0xA, Y0 - 0x78);
        MEM_W16(MEM_R32(pp) + 0x10, X0 + w1);
        MEM_W16(MEM_R32(pp) + 0x12, Y0 - 0x78);
        MEM_W16(MEM_R32(pp) + 0x18, X0 - 0xA0);
        MEM_W16(MEM_R32(pp) + 0x1A, Y0 + h1);
        MEM_W16(MEM_R32(pp) + 0x20, X0 + w1);
        MEM_W16(MEM_R32(pp) + 0x22, Y0 + h1);
#undef X0
#undef Y0
        u32 u = (c & 0x1F) << 3, v = (c >> 5) << 3;
        MEM_W8(MEM_R32(pp) + 0xC, u);
        MEM_W8(MEM_R32(pp) + 0xD, v - 0x38);
        MEM_W8(MEM_R32(pp) + 0x14, u + 8);
        MEM_W8(MEM_R32(pp) + 0x15, v - 0x38);
        MEM_W8(MEM_R32(pp) + 0x1C, u);
        MEM_W8(MEM_R32(pp) + 0x1D, v - 0x30);
        MEM_W8(MEM_R32(pp) + 0x24, u + 8);
        MEM_W8(MEM_R32(pp) + 0x25, v - 0x30);
        MEM_W16(MEM_R32(pp) + 0x16, 0x16);
        gcall(F_SET_SEMI, MEM_R32(pp), 0, R[6], R[7]);
        gcall(F_SET_SHADE_TEX, MEM_R32(pp), 1, R[6], R[7]);
        u32 b = BUFFER;
        gcall(F_ADD_PRIM, (b << 13) + depth * 4 + 0x800AFCB8u, MEM_R32(pp), R[6], R[7]);
        MEM_W32(pp, MEM_R32(pp) + 0x28);
        GS(u16, sp + 0x10) = (u16)(GS(u16, sp + 0x10) + (u32)step);
        LOOP_POLL(0x800CAAA0u);
    }
}

/* 800CBA8C: draw the menu: clear this buffer's ordering tables, the two
 * text lines (0x800CCC6C and +0xF0, at 0x800CCC64 / 0x800CCC68) and the
 * sprites (800CB2AC) into its packet area (0x800E5A50 + buffer * 0x3000),
 * then hand the tables over (8002DF38) and flag the frame drawn. */
void n_option_Draw(void) {
    FRAME(0x50);
    gcall(0x8002E208u, (BUFFER << 13) + 0x800EEA50u, R[5], R[6], R[7]);
    gcall(0x8002DED8u, 0, 0, BUFFER * 20 + 0x800AFC88u, R[7]);
    gcall(0x8002DED8u, 0, 0xA, BUFFER * 20 + 0x800B4BB8u, R[7]);
    gcall(0x80090D4Cu, BUFFER * 20 + 0x800AFC88u, 0, R[6], R[7]);
    GV(u32, 0x800CCE4Cu) = 0;
    GS(u32, sp + 0x20) = BUFFER * 0x3000 + 0x800E5A50u;
    GS(u32, sp + 0x10) = sp + 0x20;
    GS(u32, sp + 0x14) = 1;
    GS(u32, sp + 0x18) = 0x100;
    gcall(0x800CAAD8u, 0x800CCC6Cu, 0x80, (u32)(s32)GV(s16, 0x800CCC64u), (u32)(s32)GV(s16, 0x800CCC66u));
    GS(u32, sp + 0x10) = sp + 0x20;
    GS(u32, sp + 0x14) = 1;
    GS(u32, sp + 0x18) = 0x100;
    u32 a2 = (u32)(s32)GV(s16, 0x800CCC68u), a3 = (u32)(s32)GV(s16, 0x800CCC6Au);
    GV(u32, 0x800CCE4Cu) = 0;
    gcall(0x800CAAD8u, 0x800CCC6Cu + 0xF0, 0x80, a2, a3);
    gcall(0x800CB2ACu, R[4], R[5], R[6], R[7]);
    u32 o = BUFFER * 20;
    gcall(0x8002DF38u, o + 0x800B4BB8u, o + 0x800AFC88u, R[6], R[7]);
    GV(u32, 0x80062DF0u) = 1;
}

/* 800CAAD8 (text, shade, x, y, [&packets, depth, scale]): draw text with
 * control codes: 0xFF end, 0xFE new line (down by the line height
 * 0x800CCE54, scaled), 0xFC n palette row, 0xF8 n line height, 0xF5 n
 * glyph height (0x800CCE50; 0xE: the 18-per-row big font, 14 high);
 * below 0x10 a two-byte glyph (800CAF9C, 14/8 of a cell wide); else glyph
 * c - 0x10 of the font. shade 0x80 draws the texture as it is, else
 * tinted grey shade. */
void n_option_DrawTextEx(void) {
    FRAME(0x58);
    u32 str = A0, shade = A1;
    u32 pp = GS(u32, sp + 0x68), scale = GS(u32, sp + 0x70);
    GS(u32, sp + 0x60) = A2;
    GS(u16, sp + 0x22) = (u16)A3;
    u32 s8 = scale << 3;
    GS(u32, sp + 0x28) = s8;
    GS(u32, sp + 0x2C) = (u32)((s32)((s8 - scale) << 1) >> 8);
    for (;;) {                                    /* a line */
        if (MEM_R8(str) == 0xFF) return;
        GS(u16, sp + 0x20) = GS(u16, sp + 0x60);
        s32 step = (s32)GS(u32, sp + 0x28) >> 8;
        u32 w1 = (u32)step - 0xA0;
        for (;;) {
            u32 c = MEM_R8(str);
            str++;
            if (c == 0xFF) return;
            if (c == 0xFE) {
                u32 d = (u32)((s32)(GV(u32, 0x800CCE54u) * scale) >> 8);
                GS(u16, sp + 0x22) = (u16)(GS(u16, sp + 0x22) + d);
                LOOP_POLL(0x800CAF64u);
                break;
            }
            if (c == 0xFC) { GV(u32, 0x800CCE4Cu) = MEM_R8(str); str++; LOOP_POLL(0x800CAB94u); continue; }
            if (c == 0xF8) { GV(u32, 0x800CCE54u) = MEM_R8(str); str++; LOOP_POLL(0x800CABB0u); continue; }
            if (c == 0xF5) { GV(u32, 0x800CCE50u) = MEM_R8(str); str++; LOOP_POLL(0x800CABCCu); continue; }
            if (c < 0x10) {
                u32 c2 = MEM_R8(str);
                str++;
                GS(u32, sp + 0x10) = pp;
                GS(u32, sp + 0x18) = scale;
                GS(u32, sp + 0x14) = GS(u32, sp + 0x6C);
                gcall(0x800CAF9Cu, ((c << 8) | c2) & 0xFFFF, shade,
                      (u32)(s32)GS(s16, sp + 0x20), (u32)(s32)GS(s16, sp + 0x22));
                GS(u16, sp + 0x20) = (u16)(GS(u16, sp + 0x20) + GS(u32, sp + 0x2C));
                LOOP_POLL(0x800CAC1Cu);
                continue;
            }
            u32 g = (c - 0x10) & 0xFF;
            gcall(F_SET_POLY_FT4, MEM_R32(pp), R[5], R[6], R[7]);
            MEM_W16(MEM_R32(pp) + 0xE, ((PALETTE + 0x1E3) << 6) | 0x17);
#define X0 (GS(u16, sp + 0x20) + GV(u16, 0x800CCC24u))
#define Y0 (GS(u16, sp + 0x22) + GV(u16, 0x800CCC26u))
            MEM_W16(MEM_R32(pp) + 0x8, X0 - 0xA0);
            MEM_W16(MEM_R32(pp) + 0xA, Y0 - 0x78);
            MEM_W16(MEM_R32(pp) + 0x10, X0 + w1);
            MEM_W16(MEM_R32(pp) + 0x12, Y0 - 0x78);
            u32 hgt = GV(u32, 0x800CCE50u);
            u32 h = (u32)((s32)(hgt * scale) >> 8) - 0x78;
            MEM_W16(MEM_R32(pp) + 0x18, X0 - 0xA0);
            MEM_W16(MEM_R32(pp) + 0x1A, Y0 + h);
            MEM_W16(MEM_R32(pp) + 0x20, X0 + w1);
            MEM_W16(MEM_R32(pp) + 0x22, Y0 + h);
#undef X0
#undef Y0
            MEM_W16(MEM_R32(pp) + 0x16, 0x16);
            if (hgt == 0xE) {
                u32 q = g / 18;
                u32 u = ((g - q * 18) & 0xFF) << 3, v = (q & 0xFF) * 14;
                MEM_W8(MEM_R32(pp) + 0xC, u);
                MEM_W8(MEM_R32(pp) + 0xD, v + 0x38);
                MEM_W8(MEM_R32(pp) + 0x14, u + 8);
                MEM_W8(MEM_R32(pp) + 0x15, v + 0x38);
                MEM_W8(MEM_R32(pp) + 0x1C, u);
                MEM_W8(MEM_R32(pp) + 0x1D, v + 0x46);
                MEM_W8(MEM_R32(pp) + 0x24, u + 8);
                MEM_W8(MEM_R32(pp) + 0x25, v + 0x46);
            } else {
                u32 u = (g & 0x1F) << 3, u2 = u + 8;
                if (!((s32)u2 < 256)) u2 = u + 7;
                u32 v = (g >> 5) << 3;
                MEM_W8(MEM_R32(pp) + 0xC, u);
                MEM_W8(MEM_R32(pp) + 0xD, v - 0x38);
                MEM_W8(MEM_R32(pp) + 0x14, u2);
                MEM_W8(MEM_R32(pp) + 0x15, v - 0x38);
                MEM_W8(MEM_R32(pp) + 0x1C, u);
                MEM_W8(MEM_R32(pp) + 0x1D, v - 0x30);
                MEM_W8(MEM_R32(pp) + 0x24, u2);
                MEM_W8(MEM_R32(pp) + 0x25, v - 0x30);
            }
            gcall(F_SET_SEMI, MEM_R32(pp), 0, R[6], R[7]);
            if (shade == 0x80) {
                gcall(F_SET_SHADE_TEX, MEM_R32(pp), 1, R[6], R[7]);
            } else {
                gcall(F_SET_SHADE_TEX, MEM_R32(pp), 0, R[6], R[7]);
                MEM_W8(MEM_R32(pp) + 4, shade);
                MEM_W8(MEM_R32(pp) + 5, shade);
                MEM_W8(MEM_R32(pp) + 6, shade);
            }
            u32 b = BUFFER;
            gcall(F_ADD_PRIM, (b << 13) + GS(u32, sp + 0x6C) * 4 + 0x800AFCB8u, MEM_R32(pp), R[6], R[7]);
            MEM_W32(pp, MEM_R32(pp) + 0x28);
            GS(u16, sp + 0x20) = (u16)(GS(u16, sp + 0x20) + (u32)step);
            LOOP_POLL(0x800CAF3Cu);
        }
    }
}

/* A sprite's position field (12.4 fixed point, s16) in pixels. */
static inline u32 px(u32 e, u32 off) { return (u32)((s32)(MEM_R16(e + off) << 16) >> 20); }

/* 800CB2AC: draw the shown sprites into this buffer's packet area
 * (0x800EBA50 + buffer * 0x1000), each into ordering table +0x18: a
 * framed box (flags 0x20, 800CA41C), a text line (0x40) or a text with
 * codes in the big font (0x80), else a textured quad: texture page from
 * +0 / +2, palette +0xC / +0xE, rgb (or plain when all 0x80), size
 * +0x14 / +0x16 times the scale, mirrored with flag 0x10, translucent with
 * flag 8. */
void n_option_DrawSprites(void) {
    FRAME(0x58);
    u32 p = (BUFFER << 12) + 0x800EBA50u;
    u32 e = SPRITES;
    for (u32 i = 0;;) {
        u32 f = MEM_R16(e + 0x20);
        if (!(f & 1) && (f & 2)) {
            if (f & 0x20) {
                GS(u32, sp + 0x20) = p;
                u32 pal = MEM_R16(e + 0xE), shade = MEM_R16(e + 0x1A);
                u32 x = px(e, 0x10), y = px(e, 0x12);
                GS(u32, sp + 0x10) = sp + 0x20;
                GS(u32, sp + 0x14) = (u32)(s32)(s16)MEM_R16(e + 0x18);
                GS(u32, sp + 0x18) = MEM_R16(e + 0x22);
                GV(u32, 0x800CCE4Cu) = pal;
                gcall(0x800CA41Cu, MEM_R32(e + 0x30), shade, x, y);
                p = GS(u32, sp + 0x20);
            } else if (f & 0x40) {
                u32 pal = MEM_R16(e + 0xE);
                u32 x = px(e, 0x10), y = px(e, 0x12);
                GS(u32, sp + 0x24) = p;
                GS(u32, sp + 0x10) = (u32)(s32)(s16)MEM_R16(e + 0x18);
                GS(u32, sp + 0x14) = MEM_R16(e + 0x22);
                GV(u32, 0x800CCE4Cu) = pal;
                gcall(0x800CA860u, MEM_R32(e + 0x30), x, y, sp + 0x24);
                p = GS(u32, sp + 0x24);
            } else if (f & 0x80) {
                GS(u32, sp + 0x28) = p;
                GV(u32, 0x800CCE50u) = 0xE;
                u32 pal = MEM_R16(e + 0xE), shade = MEM_R16(e + 0x1A);
                u32 x = px(e, 0x10), y = px(e, 0x12);
                GS(u32, sp + 0x10) = sp + 0x28;
                GS(u32, sp + 0x14) = (u32)(s32)(s16)MEM_R16(e + 0x18);
                GS(u32, sp + 0x18) = MEM_R16(e + 0x22);
                GV(u32, 0x800CCE4Cu) = pal;
                gcall(0x800CAAD8u, MEM_R32(e + 0x30), shade, x, y);
                p = GS(u32, sp + 0x28);
            } else {
                u32 tpage = MEM_R16(e + 2) | (MEM_R16(e) << 7);
                MEM_W8(p + 3, 9);
                MEM_W8(p + 7, 0x2C);
                gcall(F_SET_SEMI, p, (MEM_R16(e + 0x20) & 8) ? 1 : 0, R[6], R[7]);
                MEM_W16(p + 0x16, tpage);
                MEM_W16(p + 0xE, (MEM_R16(e + 0xE) << 6) | ((MEM_R16(e + 0xC) >> 4) & 0x3F));
                u32 r = MEM_R16(e + 0x1A);
                if (r == 0x80 && MEM_R32(e + 0x1C) == 0x00800080u) {
                    gcall(F_SET_SHADE_TEX, p, 1, R[6], R[7]);
                    MEM_W8(p + 4, r);
                    MEM_W8(p + 5, r);
                    MEM_W8(p + 6, r);
                } else {
                    gcall(F_SET_SHADE_TEX, p, 0, R[6], R[7]);
                    MEM_W8(p + 4, MEM_R8(e + 0x1A));
                    MEM_W8(p + 5, MEM_R8(e + 0x1C));
                    MEM_W8(p + 6, MEM_R8(e + 0x1E));
                }
                u32 sc = MEM_R16(e + 0x22);
                u32 w = (u32)((s32)(MEM_R16(e + 0x14) * sc) >> 8);
                u32 h = (u32)((s32)(MEM_R16(e + 0x16) * sc) >> 8);
                if (MEM_R16(e + 0x20) & 0x10) {
                    u32 x0 = px(e, 0x10) + (u32)(s32)GV(s16, 0x800CCC24u) - 0xA0;
                    u32 oy = (u32)(s32)GV(s16, 0x800CCC26u);
                    u32 y0 = px(e, 0x12) + oy - 0x78;
                    MEM_W16(p + 8, w + x0);
                    MEM_W16(p + 0x10, x0);
                    MEM_W16(p + 0x18, w + x0);
                    MEM_W16(p + 0x20, x0);
                    MEM_W16(p + 0xA, y0);
                    MEM_W16(p + 0x12, y0);
                    MEM_W16(p + 0x1A, h + y0);
                    MEM_W16(p + 0x22, h + y0);
                } else {
                    w -= 0xA0;
                    h -= 0x78;
#define XS (GV(u16, 0x800CCC24u) + px(e, 0x10))
#define YS (GV(u16, 0x800CCC26u) + px(e, 0x12))
                    MEM_W16(p + 8, XS - 0xA0);
                    MEM_W16(p + 0xA, YS - 0x78);
                    MEM_W16(p + 0x10, XS + w);
                    MEM_W16(p + 0x12, YS - 0x78);
                    MEM_W16(p + 0x18, XS - 0xA0);
                    MEM_W16(p + 0x1A, YS + h);
                    MEM_W16(p + 0x20, XS + w);
                    MEM_W16(p + 0x22, YS + h);
#undef XS
#undef YS
                }
                s32 sw = (s16)MEM_R16(e + 8);
                if (!((s32)(MEM_R16(e + 4) + (u32)sw) < 256)) sw--;
                s32 sh = (s16)MEM_R16(e + 0xA);
                if (!((s32)(MEM_R16(e + 6) + (u32)sh) < 256)) sh--;
                MEM_W8(p + 0xC, MEM_R8(e + 4));
                MEM_W8(p + 0xD, MEM_R8(e + 6));
                MEM_W8(p + 0x14, MEM_R8(e + 4) + (u32)sw);
                MEM_W8(p + 0x15, MEM_R8(e + 6));
                MEM_W8(p + 0x1C, MEM_R8(e + 4));
                MEM_W8(p + 0x1D, MEM_R8(e + 6) + (u32)sh);
                MEM_W8(p + 0x24, MEM_R8(e + 4) + (u32)sw);
                MEM_W8(p + 0x25, MEM_R8(e + 6) + (u32)sh);
                u32 b = BUFFER;
                gcall(F_ADD_PRIM, (b << 13) + ((u32)(s32)(s16)MEM_R16(e + 0x18) << 2) + 0x800AFCB8u, p, R[6], R[7]);
                p += 0x28;
            }
        }
        e += 0x38;
        i++;
        if (!((s32)i < 72)) break;
        LOOP_POLL(0x800CB770u);
    }
}

/* 800CA41C (text, shade, x, y, [&packets, depth, scale]): draw a text of
 * 16-bit codes in the big 14x14 font: 0xFFFF end, 0xFFFE new line (22
 * pixels, scaled), 0xFCnn palette row nn, 0x8000 and up a blank; glyph g
 * at (g % 18, g / 18) * 14 on texture page 0x140, from 0x120 on page
 * 0x180. */
void n_option_DrawBigText(void) {
    FRAME(0x48);
    u32 str = A0, pp = GS(u32, sp + 0x58), scale = GS(u32, sp + 0x60);
    GS(u32, sp + 0x4C) = A1;
    GS(u32, sp + 0x50) = A2;
    GS(u16, sp + 0x12) = (u16)A3;
    GS(u32, sp + 0x18) = scale << 3;
    for (;;) {
        if (MEM_R16(str) == 0xFFFF) return;
        GS(u16, sp + 0x10) = GS(u16, sp + 0x50);
        s32 step = (s32)((GS(u32, sp + 0x18) - GS(u32, sp + 0x60)) << 1) >> 8;
        u32 w1 = (u32)step - 0xA0, h1 = (u32)step - 0x78;
        for (;;) {
            u32 c = MEM_R16(str);
            str += 2;
            if (c == 0xFFFF) return;
            if (c == 0xFFFE) {
                u32 s = GS(u32, sp + 0x60);
                GS(u16, sp + 0x12) = (u16)(GS(u16, sp + 0x12) + (u32)((s32)(s * 22) >> 8));
                LOOP_POLL(0x800CA828u);
                break;
            }
            if ((c & 0xFF00) == 0xFC00) {
                GV(u32, 0x800CCE4Cu) = c & 0xFF;
                LOOP_POLL(0x800CA4DCu);
                continue;
            }
            if (!(c & 0x8000)) {
                gcall(F_SET_POLY_FT4, MEM_R32(pp), R[5], R[6], R[7]);
                MEM_W16(MEM_R32(pp) + 0xE, ((PALETTE + 0x1E3) << 6) | 0x17);
#define X0 (GS(u16, sp + 0x10) + GV(u16, 0x800CCC24u))
#define Y0 (GS(u16, sp + 0x12) + GV(u16, 0x800CCC26u))
                MEM_W16(MEM_R32(pp) + 0x8, X0 - 0xA0);
                MEM_W16(MEM_R32(pp) + 0xA, Y0 - 0x78);
                MEM_W16(MEM_R32(pp) + 0x10, X0 + w1);
                MEM_W16(MEM_R32(pp) + 0x12, Y0 - 0x78);
                MEM_W16(MEM_R32(pp) + 0x18, X0 - 0xA0);
                MEM_W16(MEM_R32(pp) + 0x1A, Y0 + h1);
                MEM_W16(MEM_R32(pp) + 0x20, X0 + w1);
                MEM_W16(MEM_R32(pp) + 0x22, Y0 + h1);
#undef X0
#undef Y0
                u32 g = c, tp = 0x140;
                if (!(c < 0x120)) { g = (c - 0x120) & 0xFFFF; tp = 0x180; }
                u32 q = g / 18;
                u32 u = ((g - q * 18) & 0xFFFF) * 14, v = (q & 0xFFFF) * 14;
                MEM_W8(MEM_R32(pp) + 0xC, u);
                MEM_W8(MEM_R32(pp) + 0xD, v);
                MEM_W8(MEM_R32(pp) + 0x14, u + 14);
                MEM_W8(MEM_R32(pp) + 0x15, v);
                MEM_W8(MEM_R32(pp) + 0x1C, u);
                MEM_W8(MEM_R32(pp) + 0x1D, v + 14);
                MEM_W8(MEM_R32(pp) + 0x24, u + 14);
                MEM_W8(MEM_R32(pp) + 0x25, v + 14);
                MEM_W16(MEM_R32(pp) + 0x16, (tp >> 6) | 0x10);
                gcall(F_SET_SEMI, MEM_R32(pp), 0, R[6], R[7]);
                if (GS(u32, sp + 0x4C) == 0x80) {
                    gcall(F_SET_SHADE_TEX, MEM_R32(pp), 1, R[6], R[7]);
                } else {
                    gcall(F_SET_SHADE_TEX, MEM_R32(pp), 0, R[6], R[7]);
                    MEM_W8(MEM_R32(pp) + 4, GS(u8, sp + 0x4C));
                    MEM_W8(MEM_R32(pp) + 5, GS(u8, sp + 0x4C));
                    MEM_W8(MEM_R32(pp) + 6, GS(u8, sp + 0x4C));
                }
                u32 b = BUFFER;
                gcall(F_ADD_PRIM, (b << 13) + GS(u32, sp + 0x5C) * 4 + 0x800AFCB8u, MEM_R32(pp), R[6], R[7]);
                MEM_W32(pp, MEM_R32(pp) + 0x28);
            }
            GS(u16, sp + 0x10) = (u16)(GS(u16, sp + 0x10) + (u32)step);
            LOOP_POLL(0x800CA7FCu);
        }
    }
}

#define F_FADE_TO      0x80090B90u   /* (a, frames) */
#define F_FADE_STEP    0x80090C90u
#define F_FADE_OUT     0x80090BF0u   /* (a, frames) */
#define F_MENU_SOUND   0x800805F4u   /* (0, sound, 0x7F, 0x7F) */
#define PAD1           ((u32)(s32)GV(s16, 0x800B4B94u))   /* buttons pressed this frame */
#define PAD2           ((u32)(s32)GV(s16, 0x800B3CC4u))   /* buttons held */
#define REPEAT         GV(u32, 0x800CCBD4u)              /* key repeat / wait count */

/* A new sprite at (x, y) with a text and an update, shown, drawn as a
 * text line (flags 0x20 | 2). */
static void text_sprite(u32 x, u32 y, u32 text, u32 update) {
    u32 n = alloc_sprite();
    if (n == 0xFFFFFFFFu) return;
    u32 e = SPRITE(n);
    MEM_W16(e + 0x10, x);
    MEM_W16(e + 0x12, y);
    MEM_W16(e + 0x18, 1);
    MEM_W32(e + 0x30, text);
    u32 f = MEM_R16(e + 0x20);
    MEM_W32(e + 0x34, update);
    MEM_W16(e + 0x20, f | 0x22);
}

/* 800C5054: the RESET screen ("GAME RESET?" YES / NO): set up and fade
 * in (screen 0, 1), choose (2: left / right toggles, with a repeat delay;
 * confirm, or both extra buttons, picks), fade out (3) and then reset the
 * game (0x80062DFC) or go back to the carousel. */
void n_option_ResetScreen(void) {
    FRAME(0x20);
    u32 st = SCREEN;
    if (st == 0) {
        text_sprite(0x7D0, 0x200, 0x800CC9C4u, 0x800CBEA8u);
        text_sprite(0x530, 0x630, 0x800CC9D0u, 0x800CBEA8u);
        text_sprite(0x760, 0x7F0, 0x800CC9E8u, 0x800CBF40u);
        CURRENT = 1;
        gcall(F_FADE_TO, 0, 0x10, R[6], R[7]);
        u32 s = SCREEN;
        REPEAT = 0x10;
        SCREEN = s + 1;
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        return;
    }
    if (st == 1) {
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        if (OPT_STATE != 0) return;
        u32 s = SCREEN;
        REPEAT = 0;
        SCREEN = s + 1;
        return;
    }
    if (st != 2) {
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        if (OPT_STATE != 4) return;
        if (CURRENT == 0) {
            GV(u8, 0x80062DFCu) = 1;
        } else {
            GV(u32, 0x800CCBCCu) = 1;
            SCREEN = 0;
            REPEAT = 0;
        }
        return;
    }
    u32 snd;
    if ((pressed(PAD1, 0x7FF) << 16) != 0 && (pressed(PAD2, 0x7FF) << 16) == 0) {
        CURRENT = 1;
        snd = 3;
    } else {
        if ((pressed(PAD1, 0x800) << 16) != 0 || (pressed(PAD1, 0x40) << 16) != 0) {
            u32 t = REPEAT;
            if (t != 0) { REPEAT = t - 1; return; }
            u32 c = CURRENT;
            REPEAT = 0x10;
            CURRENT = c ^ 1;
            gcall(F_MENU_SOUND, 0, 1, 0x7F, 0x7F);
            return;
        }
        REPEAT = 0;
        if ((pressed(PAD1, 0x20) << 16) == 0) return;
        if ((pressed(PAD2, 0x20) << 16) != 0) return;
        if (CURRENT == 0) {
            snd = 2;
        } else {
            CURRENT = 1;
            snd = 3;
        }
    }
    gcall(F_MENU_SOUND, 0, snd, 0x7F, 0x7F);
    SCREEN = SCREEN + 1;
    gcall(F_FADE_OUT, 0, 8, R[6], R[7]);
}

/* Glyphs in a 16-bit text (0xFFFF ends), counted as the original loop
 * does (a poll per glyph after the first). */
static u32 text_len(u32 t, u32 poll) {
    u32 n = 0;
    if (MEM_R16(t) == 0xFFFF) return 0;
    t += 2;
    for (;;) {
        n++;
        u32 c = MEM_R16(t);
        t += 2;
        if (c == 0xFFFF) break;
        LOOP_POLL(poll);
    }
    return n;
}

#define TURN_DIR  GV(s32, 0x800CCBF4u)   /* the wheel's turn per frame */
#define TURN_WAIT GV(u32, 0x800CCBD8u)   /* key repeat delay */

/* 800C43B4: the carousel (SOUND / MONITOR TUNING / KEY CONFIG / ADJUST
 * SCREEN / RESET / EXIT, 0x800CC92C: two texts each; the analog pad gets
 * a different fifth item and the plain pad none): screen 0 builds it
 * (wheel sprites, labels, the arrows) and fades in, 1 waits, 2 turns it
 * (left / right, 0x80 a frame until it reaches the next item) and picks
 * (confirm, or both extra buttons for EXIT), 3 fades out into the picked
 * screen (0x800CCAF0 by item), 4 closes. */
void n_option_Carousel(void) {
    FRAME(0x48);
    u32 st = SCREEN;
    if (st == 0) {
        u32 c = REPEAT + 1;
        REPEAT = c;
        if (c == 0x10) {
            if (GV(u32, 0x800CCC1Cu) != 3) load_text(3);
            GV(u32, 0x800CCBF8u) = 0xFFFFFFFEu;
            u32 m = PAD_MODE, d = GV(u32, 0x80062ED0u) & 0x3000;
            GV(u32, 0x800CCC10u) = m;
            if (d) GV(u32, 0x800CCC10u) = 0x400000;
            u32 m2 = GV(u32, 0x800CCC10u);
            GV(u32, 0x800CCC18u) = 0;
            GV(u32, 0x800CCC14u) = m2;
            text_sprite(0x760, 0x200, 0x800CCAE0u, 0x800CBEA8u);
            u32 mode = GV(u32, 0x800CCC10u);
            u32 step = (mode == 0x400000 || mode == 0x200000) ? 0x2AA : 0x333;
            const u32 tbl = 0x800CC92Cu;
            u32 ang = 0;
            for (u32 i = 0;;) {
                if (i == 4) {
                    u32 md = GV(u32, 0x800CCC10u);
                    if (md != 0x400000 && md != 0x200000) goto next;
                }
                {
                    u32 n = alloc_sprite();
                    if (n != 0xFFFFFFFFu) {
                        u32 e = SPRITE(n);
                        MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 4);
                        init_sprite(e, 0x800CC370u);
                        u32 w = MEM_R16(e + 0x14);
                        MEM_W16(e + 0x24, ang);
                        MEM_W16(e + 0x26, i);
                        MEM_W32(e + 0x34, 0x800C4E1Cu);
                        MEM_W16(e + 0x14, w + 0x1C);
                    }
                    if (GV(u32, tbl + i * 8) != 0) {
                        u32 m3 = alloc_sprite();
                        if (m3 != 0xFFFFFFFFu) {
                            u32 e = SPRITE(m3), k = i;
                            u32 f = MEM_R16(e + 0x20);
                            MEM_W16(e + 0xE, 1);
                            MEM_W16(e + 0x24, n);
                            MEM_W16(e + 0x20, f | 0x20);
                            if (i == 4 && GV(u32, 0x800CCC10u) == 0x400000) k = 6;
                            u32 t = GV(u32, tbl + k * 8);
                            MEM_W32(e + 0x30, t);
                            u32 len = text_len(t, 0x800C4664u);
                            MEM_W16(e + 0x26, (u32)((s32)(0x8C - len * 14) >> 1));
                            MEM_W16(e + 0x28, GV(u32, tbl + k * 8 + 4) != 0 ? 9 : 0x11);
                            MEM_W32(e + 0x34, 0x800C4F84u);
                            u32 row = tbl + k * 8;
                            if (GV(u32, row + 4) != 0) {
                                u32 m4 = alloc_sprite();
                                if (m4 != 0xFFFFFFFFu) {
                                    e = SPRITE(m4);
                                    f = MEM_R16(e + 0x20);
                                    MEM_W16(e + 0xE, 1);
                                    MEM_W16(e + 0x24, n);
                                    MEM_W16(e + 0x20, f | 0x20);
                                    t = GV(u32, row + 4);
                                    MEM_W32(e + 0x30, t);
                                    len = text_len(t, 0x800C4734u);
                                    u32 x = (u32)((s32)(0x8C - len * 14) >> 1);
                                    MEM_W16(e + 0x26, x);
                                    if (i == 3) MEM_W16(e + 0x26, x + 6);
                                    MEM_W16(e + 0x28, 0x1C);
                                    MEM_W32(e + 0x34, 0x800C4F84u);
                                }
                            }
                        }
                    }
                }
                ang += step;
            next:
                i++;
                if (!((s32)i < 6)) break;
                LOOP_POLL(0x800C4788u);
            }
            for (u32 j = 0;;) {
                u32 n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 6);
                    init_sprite(e, 0x800CC35Cu);
                    MEM_W16(e + 0x14, 0xE);
                    MEM_W16(e + 8, 0xE);
                    MEM_W16(e + 0x16, 0xE);
                    MEM_W16(e + 0xA, 0xE);
                    u32 v = MEM_R16(e + 6), pal = MEM_R16(e + 0xE);
                    MEM_W16(e + 6, v + 0x1C);
                    MEM_W16(e + 0xE, pal + 1);
                    u32 u = MEM_R16(e + 4);
                    if (j == 0) { MEM_W16(e + 0x10, 0x600); MEM_W16(e + 4, u + 0xA8); }
                    else { MEM_W16(e + 0x10, 0xD00); MEM_W16(e + 4, u + 0xB6); }
                    MEM_W16(e + 0x12, 0xA00);
                    MEM_W16(e + 0x18, 0);
                    MEM_W32(e + 0x34, 0x800CBEA8u);
                }
                n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 6);
                    init_sprite(e, 0x800CC35Cu);
                    if (j == 0) { MEM_W16(e + 4, 8); MEM_W16(e + 0x10, 0x640); }
                    else { MEM_W16(e + 4, 0x10); MEM_W16(e + 0x10, 0xD40); }
                    MEM_W16(e + 6, 0xD0);
                    MEM_W16(e + 0x12, 0x940);
                    u32 pal = MEM_R16(e + 0xE);
                    MEM_W16(e + 0x14, 8);
                    MEM_W16(e + 8, 8);
                    MEM_W16(e + 0x16, 8);
                    MEM_W16(e + 0xA, 8);
                    MEM_W16(e + 0x18, 0);
                    MEM_W32(e + 0x34, 0x800CBED0u);
                    MEM_W16(e + 0xE, pal + 1);
                }
                j++;
                if (!((s32)j < 2)) break;
                LOOP_POLL(0x800C48F0u);
            }
            if (GV(u32, 0x800CCC10u) == 0x200000) gcall(0x800CBD60u, 0x800CC850u, 0, 0x10, 0xB8);
            s32 idx = (s32)GV(u32, 0x800CCC04u);
            if (idx >= 3) {
                u32 md = GV(u32, 0x800CCC10u);
                if (md != 0x400000 && md != 0x200000) idx--;
                else idx = (s32)GV(u32, 0x800CCC04u);
            } else {
                idx = (s32)GV(u32, 0x800CCC04u);
            }
            CURRENT = (u32)(idx * (s32)step);
            SCREEN = SCREEN + 1;
            gcall(F_FADE_TO, 0, 0x10, R[6], R[7]);
        }
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        return;
    }
    if (st == 1) {
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        if (OPT_STATE != 0) return;
        REPEAT = 0;
        TURN_WAIT = 0x10;
        u32 s = SCREEN;
        SLIDE = 1;
        SCREEN = s + 1;
        return;
    }
    if (st == 2) {
        u32 mode = GV(u32, 0x800CCC10u);
        s32 step, max;
        if (mode == 0x400000 || mode == 0x200000) { step = 0x2AA; max = 0xFFC; }
        else { step = 0x333; max = 0xFFF; }
        if ((pressed(PAD1, 0x7FF) << 16) != 0 && (pressed(PAD2, 0x7FF) << 16) == 0) {
            ROW = 0;
            goto confirm;
        }
        {
            s32 cur = (s32)CURRENT;
            if (cur % step != 0) {
                s32 target = (s32)ROW;
                u32 v = (u32)(cur + TURN_DIR) & 0xFFF;
                CURRENT = v;
                if ((s32)(((u32)target - v + 0x40) & 0xFFF) < 129) CURRENT = (u32)target;
                return;
            }
        }
        if ((pressed(PAD1, 0x800) << 16) != 0 || (pressed(PAD1, 0x40) << 16) != 0) {
            s32 t = (s32)REPEAT;
            if (t > 0) { REPEAT = (u32)(t - 1); return; }
            if ((pressed(PAD1, 0x800) << 16) != 0 && (pressed(PAD1, 0x40) << 16) != 0) return;
            if ((pressed(PAD1, 0x800) << 16) != 0) {
                TURN_DIR = 0x80;
                s32 v = (s32)CURRENT + step;
                ROW = (u32)v;
                if (!(v < max)) ROW = 0;
            } else {
                TURN_DIR = -0x80;
                s32 v = (s32)CURRENT - step;
                ROW = (u32)v;
                if (v < 0) ROW = (u32)(max - step);
            }
            if (TURN_DIR != 0) gcall(F_MENU_SOUND, 0, 1, 0x7F, 0x7F);
            CURRENT = (CURRENT + (u32)TURN_DIR) & 0xFFF;
            REPEAT = TURN_WAIT;
            return;
        }
        REPEAT = 0;
        TURN_WAIT = 0x10;
        SLIDE = 0;
        if ((pressed(PAD1, 0x20) << 16) == 0) return;
        if ((pressed(PAD2, 0x20) << 16) != 0) return;
        {
            s32 q = (s32)CURRENT / step;
            ROW = (u32)q;
            if (q >= 2) {
                u32 md = GV(u32, 0x800CCC10u);
                if (md != 0x400000 && md != 0x200000) ROW = (u32)(q + 1);
            }
        }
    confirm:
        SCREEN = SCREEN + 1;
        gcall(F_FADE_OUT, 0, 0x10, R[6], R[7]);
        gcall(F_MENU_SOUND, 0, ROW != 0 ? 2 : 3, 0x7F, 0x7F);
        return;
    }
    if (st == 3) {
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        if (OPT_STATE != 4) return;
        n_option_ClearMarks();
        s32 k = GV(s16, 0x800CCAF0u + ROW * 2);
        if (k == 5 && GV(u32, 0x800CCC10u) == 0x400000) GV(u32, 0x800CCBCCu) = 7;
        else GV(u32, 0x800CCBCCu) = (u32)(s32)GV(s16, 0x800CCAF0u + ROW * 2);
        REPEAT = 0;
        SCREEN = 0;
        GV(u32, 0x800CCC04u) = ROW;
        return;
    }
    gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
    if (OPT_STATE != 4) return;
    n_option_ClearMarks();
    SCREEN = 0;
}

#define F_MOUSE_HINT 0x800CBD60u   /* (text, 0, 0x10, 0xB8): the mouse help line */

/* 800C53D4: the ADJUST SCREEN screen: rows EXIT (0), UP/DOWN (1),
 * LEFT/RIGHT (2), DEFAULT (3) with the offsets 0x800CCC24 / 0x800CCC26
 * (-8..7) shown as numbers; in mouse mode also the screen frame picture.
 * Screen 0 builds it, 1 fades in, 2 picks a row (left / right, wrapping;
 * confirm opens it, the extra buttons or EXIT leave), 3 changes the
 * offset (cancel restores it) or waits out EXIT / DEFAULT, 4 closes back
 * to the carousel. */
void n_option_AdjustScreen(void) {
    FRAME(0x68);
    u32 st = SCREEN;
    if (st == 0) {
        u32 c = REPEAT + 1;
        REPEAT = c;
        if (c == 3) {
            if (GV(u32, 0x800CCC1Cu) != 4) load_text(4);
            GV(u32, 0x800CCBF8u) = 4;
            u32 pos = 0x800CCAFCu;
            for (u32 i = 0;;) {
                u32 n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 6);
                    init_sprite(e, 0x800CC398u);
                    u32 v = MEM_R16(e + 6);
                    MEM_W16(e + 0x14, 0x58);
                    MEM_W16(e + 0x16, 0x20);
                    MEM_W16(e + 0xA, 0x20);
                    MEM_W16(e + 0x24, i);
                    MEM_W16(e + 0x26, v);
                    MEM_W16(e + 0x10, GV(u16, pos));
                    u32 y = GV(u16, pos + 2);
                    MEM_W16(e + 0x18, 3);
                    MEM_W32(e + 0x34, 0x800CBFA8u);
                    MEM_W16(e + 0x12, y);
                }
                i++;
                pos += 4;
                if (!((s32)i < 4)) break;
                LOOP_POLL(0x800C5520u);
            }
            pos = 0x800CCB0Cu;
            u32 txt = 0x800CC49Cu;
            for (u32 i = 0;;) {
                u32 n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | (i == 0 ? 0x22 : 0x42));
                    MEM_W16(e + 0xE, 0);
                    MEM_W16(e + 0x10, GV(u16, pos));
                    u32 y = GV(u16, pos + 2);
                    MEM_W16(e + 0x18, 2);
                    MEM_W16(e + 0x12, y);
                    u32 t = GV(u32, txt);
                    MEM_W16(e + 0x24, i);
                    MEM_W32(e + 0x34, 0x800CC03Cu);
                    MEM_W32(e + 0x30, t);
                }
                pos += 4;
                i++;
                txt += 4;
                if (!((s32)i < 4)) break;
                LOOP_POLL(0x800C55CCu);
            }
            number_text(GV(s16, 0x800CCC24u), 0x800CCC00u);
            number_text(GV(s16, 0x800CCC26u), 0x800CCBFCu);
            pos = 0x800CCB1Cu;
            for (u32 j = 0;;) {
                u32 n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 0x40);
                    u32 x = GV(u16, pos);
                    MEM_W16(e + 0x26, x);
                    MEM_W16(e + 0x10, x);
                    u32 y = GV(u16, pos + 2);
                    MEM_W16(e + 0x18, 2);
                    MEM_W16(e + 0x24, j + 1);
                    MEM_W16(e + 0x12, y);
                    MEM_W32(e + 0x30, j == 0 ? 0x800CCC00u : 0x800CCBFCu);
                    MEM_W32(e + 0x34, 0x800C5FC8u);
                }
                n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 0x22);
                    u32 x = GV(u16, pos) - 0x1F0;
                    MEM_W16(e + 0x26, x);
                    MEM_W16(e + 0x10, x);
                    u32 y = GV(u16, pos + 2);
                    MEM_W32(e + 0x30, 0x800CC5A0u);
                    MEM_W16(e + 0x18, 2);
                    MEM_W16(e + 0x24, j + 1);
                    MEM_W32(e + 0x34, 0x800CC03Cu);
                    MEM_W16(e + 0x12, y - 0x30);
                }
                pos += 4;
                j++;
                if (!((s32)j < 2)) break;
                LOOP_POLL(0x800C5718u);
            }
            if (PAD_MODE == 0x200000) {
                for (u32 k = 0; k < 32; k++) GS(u8, sp + 0x20 + k) = GV(u8, 0x800C3E60u + k);
                GS(u16, sp + 0x1A) = 0x18C;
                GS(u16, sp + 0x18) = 0x140;
                GS(u16, sp + 0x1C) = 0x40;
                GS(u16, sp + 0x1E) = 0x2C;
                gcall(0x800354F8u, sp + 0x18, 0xFF, 0xFF, 0xFF);
                GS(u16, sp + 0x1C) = 0x10;
                GS(u16, sp + 0x18) = 0x140;
                GS(u16, sp + 0x1A) = 0x1B8;
                GS(u16, sp + 0x1E) = 1;
                gcall(0x80035620u, sp + 0x18, sp + 0x20, R[6], R[7]);
                gcall(0x80035374u, 0, R[5], R[6], R[7]);
                u32 n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 2, 0x15);
                    MEM_W16(e + 6, 0x8C);
                    MEM_W16(e + 8, 0x100);
                    MEM_W16(e + 0x14, 0x12C);
                    MEM_W16(e + 0x12, 0xB40);
                    MEM_W16(e + 0x10, 0xA0);
                    MEM_W16(e + 0x18, 3);
                    u32 f = MEM_R16(e + 0x20);
                    MEM_W16(e + 0, 0);
                    MEM_W16(e + 4, 0);
                    MEM_W16(e + 0x16, 0x2C);
                    MEM_W16(e + 0xA, 0x2C);
                    MEM_W16(e + 0xC, 0x140);
                    MEM_W16(e + 0xE, 0x1B8);
                    MEM_W32(e + 0x34, 0x800CBEA8u);
                    MEM_W16(e + 0x20, f | 0xE);
                }
            }
            CURRENT = 0;
            if (GV(u32, 0x800CCC10u) == 0x200000) gcall(F_MOUSE_HINT, 0x800CC4ACu, 0, 0x10, 0xB8);
            REPEAT = 0;
            TURN_WAIT = 0x10;
            gcall(F_FADE_TO, 0, 0x10, R[6], R[7]);
            u32 s = SCREEN;
            TURN_WAIT = 0x10;
            SCREEN = s + 1;
        }
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        return;
    }
    if (st == 1) {
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        if (OPT_STATE != 0) return;
        SCREEN = SCREEN + 1;
        return;
    }
    if (st == 2) {
        if ((pressed(PAD1, 0x7FF) << 16) != 0 && (pressed(PAD2, 0x7FF) << 16) == 0) goto leave;
        if ((pressed(PAD1, 0x800) << 16) != 0 || (pressed(PAD1, 0x40) << 16) != 0) {
            u32 t = REPEAT;
            if (t != 0) { REPEAT = t - 1; return; }
            u32 w = TURN_WAIT;
            REPEAT = w;
            if ((pressed(PAD1, 0x800) << 16) != 0) CURRENT = CURRENT + 1;
            else CURRENT = CURRENT - 1;
            u32 c = CURRENT & 3;
            s32 tw = (s32)TURN_WAIT;
            CURRENT = c;
            if (tw > 0 || (GV(u32, 0x800CCC0Cu) & 1)) gcall(F_MENU_SOUND, 0, 2, 0x7F, 0x7F);
            return;
        }
        REPEAT = 0;
        TURN_WAIT = 0x10;
        OPENED = 0;
        if ((pressed(PAD1, 0x20) << 16) == 0) return;
        if ((pressed(PAD2, 0x20) << 16) != 0) return;
        {
            u32 c = CURRENT;
            if (c == 0) { LOOP_POLL(0x800C5B18u); goto leave; }
            if (c == 2) {
                SLIDE = GV(s16, 0x800CCC26u);
            } else if (c == 1) {
                SLIDE = GV(s16, 0x800CCC24u);
            } else if (c == 3) {
                GV(u16, 0x800CCC26u) = 0;
                GV(u16, 0x800CCC24u) = 0;
                number_text(0, 0x800CCC00u);
                number_text(GV(s16, 0x800CCC26u), 0x800CCBFCu);
            }
            if (GV(u32, 0x800CCC10u) == 0x200000 && CURRENT - 1 < 2) gcall(F_MOUSE_HINT, 0x800CC52Cu, 0, 0x10, 0xB8);
            TURN_WAIT = 0x10;
            REPEAT = 0x10;
            u32 s = SCREEN;
            OPENED = 6;
            SCREEN = s + 1;
            gcall(F_MENU_SOUND, 0, 2, 0x7F, 0x7F);
            return;
        }
    leave:
        CURRENT = 0;
        {
            u32 s = SCREEN;
            OPENED = 6;
            SCREEN = s + 1;
        }
        gcall(F_MENU_SOUND, 0, 3, 0x7F, 0x7F);
        return;
    }
    if (st == 3) {
        u32 c = CURRENT;
        if (c == 0 || c == 3) {
            s32 o = OPENED - 1;
            OPENED = o;
            if (o > 0) return;
            if (c == 0) {
                SCREEN = 4;
                gcall(F_FADE_OUT, 0, 0x10, R[6], R[7]);
            } else {
                SCREEN = 2;
            }
            OPENED = 1;
            return;
        }
        u32 val, buf;
        if (c == 1) { val = 0x800CCC24u; buf = 0x800CCC00u; }
        else { val = 0x800CCC26u; buf = 0x800CCBFCu; }
        if ((pressed(PAD1, 0x7FF) << 16) != 0 && (pressed(PAD2, 0x7FF) << 16) == 0) {
            u32 v = GV(u16, 0x800CCBDCu);
            GV(u16, val) = (u16)v;
            number_text((s16)v, buf);
            if (GV(u32, 0x800CCC10u) == 0x200000) gcall(F_MOUSE_HINT, 0x800CC4ACu, 0, 0x10, 0xB8);
            TURN_WAIT = 0x10;
            u32 s = SCREEN;
            REPEAT = 0x10;
            SCREEN = s - 1;
            gcall(F_MENU_SOUND, 0, 3, 0x7F, 0x7F);
            return;
        }
        if ((pressed(PAD1, 0x800) << 16) != 0 || (pressed(PAD1, 0x40) << 16) != 0) {
            u32 t = REPEAT;
            if (t != 0) { REPEAT = t - 1; return; }
            u32 w = TURN_WAIT;
            REPEAT = w;
            TURN_WAIT = w - 1;
            if ((s32)(w - 1) < 0) TURN_WAIT = 0;
            s32 old = GV(s16, val);
            if ((pressed(PAD1, 0x800) << 16) != 0) {
                if (!(GV(s16, val) < -7)) GV(u16, val) = (u16)(GV(u16, val) - 1);
            } else {
                if (GV(s16, val) < 8) GV(u16, val) = (u16)(GV(u16, val) + 1);
            }
            s32 nv = GV(s16, val);
            if (nv == old) return;
            number_text(nv, buf);
            if (!(GV(s32, 0x800CCBD8u) < 2) || (GV(u32, 0x800CCC0Cu) & 1)) gcall(F_MENU_SOUND, 0, 2, 0x7F, 0x7F);
            return;
        }
        REPEAT = 0;
        TURN_WAIT = 0x10;
        if ((pressed(PAD1, 0x20) << 16) == 0) return;
        if ((pressed(PAD2, 0x20) << 16) != 0) return;
        if (GV(u32, 0x800CCC10u) == 0x200000) gcall(F_MOUSE_HINT, 0x800CC4ACu, 0, 0x10, 0xB8);
        u32 s = SCREEN;
        TURN_WAIT = 0x10;
        REPEAT = 0x10;
        SCREEN = s - 1;
        gcall(F_MENU_SOUND, 0, 2, 0x7F, 0x7F);
        return;
    }
    gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
    if (OPT_STATE != 4) return;
    n_option_ClearMarks();
    REPEAT = 0;
    SCREEN = 0;
    GV(u32, 0x800CCBCCu) = 1;
}

#define KSTATE GV(u32, 0x800CCBE4u)    /* the key config screen's step */
#define KEY_LABEL 0x800CCB3Cu         /* the layout name's glyph */

/* A held direction's click, every frame at first, then every other. */
static void repeat_sound(u32 snd) {
    if (!(GV(s32, 0x800CCBD8u) < 2) || (GV(u32, 0x800CCC0Cu) & 1)) gcall(F_MENU_SOUND, 0, snd, 0x7F, 0x7F);
}

/* The controller's key config texts: pad (9, picture 7), analog pad (10,
 * picture 11). */
static void pad_texts(void) {
    if (PAD_MODE == 0x400000) {
        load_text(0xA);
        GV(u32, 0x800CCBF8u) = 0xB;
    } else {
        load_text(9);
        GV(u32, 0x800CCBF8u) = 7;
    }
}

/* The layout name shown (0x800CCB3C): the mouse's (0x800CCC2C) on row 0,
 * the pad's (0x800CCC28) otherwise. */
static void show_layout(void) {
    u32 v = CURRENT == 0 ? GV(u16, 0x800CCC2Cu) : GV(u16, 0x800CCC28u);
    GV(u16, KEY_LABEL) = (u16)(v + 0x1D);
}

/* 800C61C8: the KEY CONFIG screen: the controller picture with its
 * button glyphs and the layouts (TYPE A / B / C, EXIT): built on screen
 * 0, then by step (0x800CCBE4): 1 fade in, 2 pick the controller (mouse,
 * pad, EXIT), 3 / 4 pick the pad's / the mouse's layout (0x80062DD4 /
 * 0x80062DD6; the mouse has no third), 5 fade out to the carousel, 6
 * reload the pad's texts. */
void n_option_KeyConfig(void) {
    FRAME(0x80);
    if (SCREEN == 0) {
        u32 c = REPEAT + 1;
        REPEAT = c;
        if (c == 3) {
            GS(u16, sp + 0x58) = 0x170;
            GS(u16, sp + 0x5A) = 0x1E3;
            GS(u16, sp + 0x5C) = 0x10;
            GS(u16, sp + 0x5E) = 2;
            gcall(0x80035680u, sp + 0x58, sp + 0x18, R[6], R[7]);
            gcall(0x80035374u, 0, R[5], R[6], R[7]);
            load_text(5);
            gcall(0x80035620u, sp + 0x58, sp + 0x18, R[6], R[7]);
            GV(u32, 0x800CCBF8u) = 7;
            if (PAD_MODE == 0x200000) {
                if (GV(u32, 0x800CCC1Cu) != 8) {
                    load_text(8);
                    GV(u32, 0x800CCBF8u) = 7;
                }
            } else {
                pad_texts();
            }
            if (GV(u32, 0x800CCC10u) == 0x200000) gcall(F_MOUSE_HINT, 0x800CC62Cu, 0, 0x10, 0xB8);
            u32 x = 0x100;
            for (u32 i = 0;;) {
                u32 n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 6);
                    init_sprite(e, 0x800CC3ACu);
                    MEM_W16(e + 8, 0x54);
                    MEM_W16(e + 0x14, 0x48);
                    MEM_W16(e + 0x16, 0x10);
                    MEM_W16(e + 0xA, 0x10);
                    MEM_W16(e + 0x12, 0x300);
                    MEM_W16(e + 0x18, 3);
                    MEM_W16(e + 0x28, 1);
                    u32 v = MEM_R16(e + 6);
                    MEM_W16(e + 0x10, x);
                    MEM_W16(e + 0x24, i);
                    MEM_W32(e + 0x34, 0x800C7160u);
                    MEM_W16(e + 6, v + 0x10);
                    MEM_W16(e + 0x26, v + 0x10);
                }
                n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n), f;
                    if (i == 3) {
                        MEM_W16(e + 0x10, x + 0x80);
                        MEM_W16(e + 0x12, 0x310);
                        f = MEM_R16(e + 0x20);
                        MEM_W32(e + 0x30, 0x800CC474u);
                        f |= 0x22;
                    } else {
                        MEM_W16(e + 0x10, x + 0xC0);
                        MEM_W16(e + 0x12, 0x340);
                        f = MEM_R16(e + 0x20);
                        MEM_W32(e + 0x30, 0x800CCB24u + i * 7);
                        f |= 0x42;
                    }
                    MEM_W16(e + 0x20, f);
                    MEM_W16(e + 0xE, 3);
                    MEM_W16(e + 0x18, 2);
                    MEM_W16(e + 0x28, 1);
                    u32 y = MEM_R16(e + 0x12);
                    MEM_W16(e + 0x24, i);
                    MEM_W32(e + 0x34, 0x800C72B8u);
                    MEM_W16(e + 0x26, y);
                }
                x += 0x480;
                i++;
                if (!((s32)i < 4)) break;
                LOOP_POLL(0x800C6528u);
            }
            u32 n = alloc_sprite();
            if (n != 0xFFFFFFFFu) {
                u32 e = SPRITE(n);
                MEM_W16(e + 0x10, 0xC40);
                MEM_W16(e + 0x12, 0x190);
                MEM_W16(e + 0x18, 2);
                MEM_W16(e + 0x28, 2);
                MEM_W32(e + 0x30, KEY_LABEL);
                u32 f = MEM_R16(e + 0x20);
                MEM_W16(e + 0xE, 0);
                MEM_W32(e + 0x34, 0x800C72B8u);
                MEM_W16(e + 0x20, f | 0x22);
            }
            for (u32 i = 0;;) {
                n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 6);
                    init_sprite(e, 0x800CC3ACu);
                    u32 u = MEM_R16(e + 4), v = MEM_R16(e + 6);
                    MEM_W16(e + 0x14, 0x40);
                    MEM_W16(e + 8, 0x40);
                    MEM_W16(e + 0x16, 0x10);
                    MEM_W16(e + 0xA, 0x10);
                    MEM_W16(e + 0x18, 1);
                    MEM_W16(e + 0x24, i);
                    MEM_W32(e + 0x34, 0x800C7448u);
                    MEM_W16(e + 0x26, u);
                    MEM_W16(e + 0x28, v);
                }
                i++;
                if (!((s32)i < 4)) break;
                LOOP_POLL(0x800C6634u);
            }
            u32 v;
            if (PAD_MODE == 0x200000) {
                CURRENT = 0;
                v = GV(u16, 0x800CCC2Cu);
            } else {
                CURRENT = 1;
                v = GV(u16, 0x800CCC28u);
            }
            GV(u16, KEY_LABEL) = (u16)(v + 0x1D);
            REPEAT = 0x10;
            TURN_WAIT = 0x10;
            TURN_WAIT = 0;
            OPENED = 0;
            gcall(F_FADE_TO, 0, 0x10, R[6], R[7]);
            u32 s = SCREEN;
            KSTATE = 1;
            SCREEN = s + 1;
        }
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        return;
    }
    u32 k = KSTATE;
    if (k == 1) {
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        if (OPT_STATE != 0) return;
        KSTATE = PAD_MODE == 0x200000 ? 4 : 3;
        TURN_WAIT = 0x10;
        return;
    }
    if (k == 2) {
        s32 d = SLIDE;
        if (d != 0) {
            d--;
            SLIDE = d;
            if (d != 0) return;
            if (CURRENT == 0) { KSTATE = 4; ROW = GV(u32, 0x800CCC2Cu); }
            else { KSTATE = 3; ROW = GV(u32, 0x800CCC28u); }
            GV(u16, KEY_LABEL) = (u16)(GV(u16, 0x800CCBECu) + 0x1D);
            if (GV(u32, 0x800CCC10u) == 0x200000) gcall(F_MOUSE_HINT, 0x800CC62Cu, 0, 0x10, 0xB8);
            return;
        }
        if ((pressed(PAD1, 0x7FF) << 16) != 0 && (pressed(PAD2, 0x7FF) << 16) == 0) {
            CURRENT = 2;
            goto pick;
        }
        if ((pressed(PAD1, 0x800) << 16) != 0 || (pressed(PAD1, 0x40) << 16) != 0) {
            u32 t = REPEAT;
            if (t != 0) { REPEAT = t - 1; return; }
            REPEAT = TURN_WAIT;
            if ((pressed(PAD1, 0x800) << 16) != 0) {
                s32 cc = (s32)CURRENT - 1;
                CURRENT = (u32)cc;
                if (cc < 0) CURRENT = 2;
            } else {
                s32 cc = (s32)CURRENT + 1;
                CURRENT = (u32)cc;
                if (!(cc < 3)) CURRENT = 0;
            }
            repeat_sound(1);
            show_layout();
            return;
        }
        REPEAT = 0;
        TURN_WAIT = 0x10;
        OPENED = 0;
        if ((pressed(PAD1, 0x20) << 16) == 0) { show_layout(); return; }
        if ((pressed(PAD2, 0x20) << 16) != 0) { show_layout(); return; }
    pick:
        SLIDE = 6;
        {
            u32 cc = CURRENT;
            OPENED = (s32)cc;
            if (cc == 2) {
                KSTATE = 5;
                gcall(F_MENU_SOUND, 0, 3, 0x7F, 0x7F);
                gcall(F_FADE_OUT, 0, 0x10, R[6], R[7]);
                return;
            }
        }
        gcall(F_MENU_SOUND, 0, 2, 0x7F, 0x7F);
        if (CURRENT == 0) {
            if (GV(u32, 0x800CCC1Cu) != 8) load_text(8);
            GV(u32, 0x800CCBF8u) = 7;
        } else {
            pad_texts();
        }
        show_layout();
        return;
    }
    if (k == 3 || k == 4) {
        if (k == 3) {
            u32 want = PAD_MODE == 0x400000 ? 0xA : 9;
            if (GV(u32, 0x800CCC1Cu) != want) { KSTATE = 6; return; }
        }
        s32 d = SLIDE;
        if (d != 0) {
            d--;
            SLIDE = d;
            if (d != 0) return;
            if (ROW != 3) return;
            TURN_WAIT = 0x10;
            REPEAT = 0x10;
            KSTATE = 5;
            gcall(F_FADE_OUT, 0, 0x10, R[6], R[7]);
            OPENED = 1;
            return;
        }
        if ((pressed(PAD1, 0x7FF) << 16) != 0 && (pressed(PAD2, 0x7FF) << 16) == 0) goto exit_row;
        if ((pressed(PAD1, 0x800) << 16) != 0 || (pressed(PAD1, 0x40) << 16) != 0) {
            u32 t = REPEAT;
            if (t != 0) { REPEAT = t - 1; return; }
            REPEAT = TURN_WAIT;
            if ((pressed(PAD1, 0x800) << 16) != 0) {
                s32 r = (s32)ROW - 1;
                ROW = (u32)r;
                if (r < 0) ROW = 3;
                if (k == 4 && ROW == 2) ROW = 1;
            } else {
                s32 r = (s32)ROW + 1;
                ROW = (u32)r;
                if (!(r < 4)) ROW = 0;
                if (k == 4 && ROW == 2) ROW = 3;
            }
            repeat_sound(1);
            return;
        }
        REPEAT = 0;
        TURN_WAIT = 0x10;
        if ((pressed(PAD1, 0x20) << 16) == 0) return;
        if ((pressed(PAD2, 0x20) << 16) != 0) return;
        if (ROW == 3) goto exit_row;
        {
            u32 v = GV(u16, 0x800CCBECu);
            if (k == 3) {
                GV(u16, 0x80062DD4u) = (u16)v;
                GV(s32, 0x800CCC28u) = (s16)v;
            } else {
                GV(u16, 0x80062DD6u) = (u16)v;
                GV(s32, 0x800CCC2Cu) = (s16)v;
            }
            GV(u16, KEY_LABEL) = (u16)(v + 0x1D);
            gcall(F_MENU_SOUND, 0, 2, 0x7F, 0x7F);
            u32 r = ROW;
            SLIDE = 6;
            OPENED = (s32)r;
            return;
        }
    exit_row:
        if (k == 3) {
            ROW = 3;
            SLIDE = 6;
            OPENED = 3;
            gcall(F_MENU_SOUND, 0, 3, 0x7F, 0x7F);
        } else {
            ROW = 3;
            gcall(F_MENU_SOUND, 0, 3, 0x7F, 0x7F);
            SLIDE = 6;
            OPENED = (s32)ROW;
        }
        return;
    }
    if (k == 5) {
        s32 d = SLIDE;
        if (d != 0) { SLIDE = d - 1; return; }
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        if (OPT_STATE != 4) return;
        n_option_ClearMarks();
        REPEAT = 0;
        SCREEN = 0;
        GV(u32, 0x800CCBCCu) = 1;
        return;
    }
    pad_texts();
    KSTATE = 3;
}

#define F_SET_PAIR_VOLUME 0x8001DDA8u   /* (pair, left, right) */
#define F_GET_PAIR_VOLUME 0x8001DEA0u   /* (pair, 0, &left, &right) */
#define F_SOUND_STEREO    0x80024038u
#define F_SOUND_MONO      0x80024048u
#define F_CD_MIX          0x800269A0u
#define SAVED_STEREO      GV(u32, 0x800CCC34u)
#define SAVED_BGM         GV(u32, 0x800CCC38u)
#define SAVED_SE          GV(u32, 0x800CCC3Cu)
#define MUSIC_LEFT        0x800CCC50u
#define MUSIC_RIGHT       0x800CCC52u

/* The music pair now playing (0x80062E70 by 0x80062E78), -1 for none. */
static s32 music_pair(void) {
    return GV(s16, 0x80062E70u + (u32)(GV(s16, 0x80062E78u) * 2));
}

/* Set the music's volume to vol percent of its full volume (kept in
 * 0x800CCC50 / 52 when the screen opened). */
static void music_volume(s32 pair, s32 vol) {
    s32 l = GV(s16, MUSIC_LEFT) * vol, r = GV(s16, MUSIC_RIGHT) * vol;
    gcall(F_SET_PAIR_VOLUME, (u32)pair, (u32)(s32)(s16)(l / 100), (u32)(s32)(s16)(r / 100), R[7]);
}

/* 800C7D80: the SOUND screen: rows SOUND (stereo / monaural), BGM and
 * S.E. (0-100) and EXIT. Screen 0 builds it, 1 fades in, 2 picks a row,
 * 3 slides the value box out (0x800CCBDC up to 147) and changes the
 * setting, live (the music's volume, the CD's mix) with cancel restoring
 * it, then slides the box back; 4 fades out to the carousel. Outside the
 * mouse, triangle works as square and cross as circle here. */
void n_option_SoundScreen(void) {
    FRAME(0x40);
    u32 st = SCREEN;
    if (st == 0) {
        u32 c = REPEAT + 1;
        REPEAT = c;
        if (c == 3) {
            load_text(6);
            text_sprite(0x810, 0x180, 0x800CC8C0u, 0x800CBEA8u);
            u32 y = 0x300, txt = 0x800CC974u;
            for (u32 row = 0;;) {
                u32 n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 6);
                    init_sprite(e, 0x800CC3E8u);
                    MEM_W16(e + 0x14, 0x4D);
                    MEM_W16(e + 8, 0x4D);
                    MEM_W16(e + 0x10, 0x300);
                    MEM_W16(e + 0x18, 3);
                    MEM_W16(e + 0x12, y);
                    MEM_W32(e + 0x34, 0x800CBEA8u);
                }
                n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 0x22);
                    u32 len = text_len(GV(u32, txt), 0x800C7F68u);
                    s32 x = 0x4D - (s32)(len * 14);
                    x = (x + (s32)((u32)x >> 31)) >> 1;
                    MEM_W16(e + 0x10, (u32)(x + 0x32) << 4);
                    MEM_W16(e + 0x12, y + 0x40);
                    if (row == 3) MEM_W16(e + 0x12, y + 0x80);
                    MEM_W16(e + 0x18, 2);
                    MEM_W16(e + 0x24, row);
                    u32 t = GV(u32, txt);
                    MEM_W32(e + 0x34, 0x800CC0DCu);
                    MEM_W32(e + 0x30, t);
                }
                if (row != 3) {
                    n = alloc_sprite();
                    if (n != 0xFFFFFFFFu) {
                        u32 e = SPRITE(n);
                        MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 0x42);
                        MEM_W16(e + 0x10, row ? 0x4C0 : 0x400);
                        MEM_W16(e + 0x12, y + 0x140);
                        MEM_W16(e + 0x18, 2);
                        u32 p = GV(u32, 0x800CCBB4u + row * 4);
                        MEM_W32(e + 0x30, p);
                        MEM_W8(p, 0xFF);
                        MEM_W16(e + 0x26, 0xFFFF);
                        MEM_W16(e + 0x24, row);
                        MEM_W32(e + 0x34, 0x800C8FC4u);
                    }
                    n = alloc_sprite();
                    if (n != 0xFFFFFFFFu) {
                        u32 e = SPRITE(n);
                        MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 4);
                        init_sprite(e, 0x800CC3E8u);
                        MEM_W16(e + 0x10, 0x7D0);
                        MEM_W16(e + 0x18, 6);
                        MEM_W16(e + 0x12, y);
                        MEM_W16(e + 0x24, row);
                        MEM_W32(e + 0x34, 0x800C919Cu);
                    }
                }
                if (row == 0) {
                    for (u32 j = 0;;) {
                        u32 a = alloc_sprite();
                        if (a != 0xFFFFFFFFu) {
                            u32 e = SPRITE(a);
                            MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 4);
                            init_sprite(e, 0x800CC3ACu);
                            MEM_W16(e + 8, 0x54);
                            MEM_W16(e + 0x14, 0x45);
                            MEM_W16(e + 0x16, 0x10);
                            MEM_W16(e + 0xA, 0x10);
                            MEM_W16(e + 0x10, j ? 0xC70 : 0x810);
                            MEM_W16(e + 0x12, 0x380);
                            MEM_W16(e + 0x18, 5);
                            u32 x = MEM_R16(e + 0x10);
                            MEM_W32(e + 0x34, 0x800C9250u);
                            u32 v = MEM_R16(e + 6);
                            MEM_W16(e + 0x24, j);
                            MEM_W16(e + 0x26, (u32)((s32)(x << 16) >> 20));
                            MEM_W16(e + 0x28, v);
                        }
                        u32 b = alloc_sprite();
                        if (b != 0xFFFFFFFFu) {
                            u32 e = SPRITE(b);
                            u32 f = MEM_R16(e + 0x20);
                            MEM_W16(e + 0x12, 0x3C0);
                            MEM_W16(e + 0x24, j);
                            MEM_W16(e + 0x26, a);
                            MEM_W16(e + 0x20, f | 0x40);
                            if (j == 0) {
                                MEM_W32(e + 0x30, 0x800CC984u);
                                MEM_W16(e + 0x28, 0xA0);
                            } else {
                                MEM_W32(e + 0x30, 0x800CC98Cu);
                                MEM_W16(e + 0x28, 0x30);
                            }
                            MEM_W16(e + 0x18, 4);
                            MEM_W32(e + 0x34, 0x800C9364u);
                        }
                        j++;
                        if (!((s32)j < 2)) break;
                        LOOP_POLL(0x800C820Cu);
                    }
                }
                if (row - 1 < 2) {
                    n = alloc_sprite();
                    if (n != 0xFFFFFFFFu) {
                        u32 e = SPRITE(n);
                        MEM_W16(e + 0xE, 1);
                        MEM_W16(e + 0x12, y + 0x90);
                        MEM_W16(e + 0x18, 5);
                        u32 f = MEM_R16(e + 0x20);
                        MEM_W16(e + 0x24, row);
                        MEM_W32(e + 0x34, 0x800C9420u);
                        MEM_W16(e + 0x20, f | 0x20);
                    }
                }
                y += 0x200;
                row++;
                txt += 4;
                if (!((s32)row < 4)) break;
                LOOP_POLL(0x800C8284u);
            }
            for (u32 j = 0;;) {
                u32 n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 4);
                    init_sprite(e, 0x800CC35Cu);
                    u32 u = MEM_R16(e + 4);
                    if (j == 0) { MEM_W16(e + 0x10, 0x900); u += 0xA8; }
                    else { MEM_W16(e + 0x10, 0xF00); u += 0xB6; }
                    MEM_W16(e + 4, u);
                    MEM_W16(e + 0x18, 5);
                    MEM_W32(e + 0x34, 0x800C95FCu);
                    u32 v = MEM_R16(e + 6), x = MEM_R16(e + 0x10);
                    MEM_W16(e + 0x16, 0xE);
                    MEM_W16(e + 0x14, 0xE);
                    MEM_W16(e + 0xA, 0xE);
                    MEM_W16(e + 8, 0xE);
                    MEM_W16(e + 0x24, j);
                    MEM_W16(e + 6, v + 0x1C);
                    u32 pal = MEM_R16(e + 0xE);
                    MEM_W16(e + 0x26, (u32)((s32)(x << 16) >> 20));
                    MEM_W16(e + 0x28, pal);
                }
                n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 6);
                    init_sprite(e, 0x800CC3D4u);
                    if (j == 0) {
                        MEM_W16(e + 0x12, 0x280);
                        u32 u = MEM_R16(e + 4), v = MEM_R16(e + 6);
                        MEM_W16(e + 4, u + 0xA8);
                        MEM_W16(e + 6, v + 0x70);
                    } else {
                        u32 u = MEM_R16(e + 4);
                        MEM_W16(e + 0x12, 0xA80);
                        MEM_W16(e + 4, u + 0x9A);
                    }
                    MEM_W16(e + 0x10, 0x200);
                    MEM_W16(e + 0x18, 1);
                    u32 pal = MEM_R16(e + 0xE);
                    MEM_W16(e + 0x14, 0xE);
                    MEM_W16(e + 8, 0xE);
                    MEM_W16(e + 0x16, 0xE);
                    MEM_W16(e + 0xA, 0xE);
                    MEM_W32(e + 0x34, 0x800CBEA8u);
                    MEM_W16(e + 0xE, pal + 1);
                }
                n = alloc_sprite();
                if (n != 0xFFFFFFFFu) {
                    u32 e = SPRITE(n);
                    MEM_W16(e + 0x20, MEM_R16(e + 0x20) | 6);
                    init_sprite(e, 0x800CC35Cu);
                    u32 u = MEM_R16(e + 4);
                    if (j == 0) { MEM_W16(e + 0x12, 0x290); u += 8; }
                    else { MEM_W16(e + 0x12, 0xAD0); u += 0x10; }
                    MEM_W16(e + 4, u);
                    MEM_W16(e + 0x14, 8);
                    MEM_W16(e + 8, 8);
                    MEM_W16(e + 0x16, 8);
                    MEM_W16(e + 0xA, 8);
                    MEM_W16(e + 0x10, 0x180);
                    MEM_W16(e + 0x18, 1);
                    MEM_W32(e + 0x34, 0x800CBED0u);
                    u32 v = MEM_R16(e + 6), pal = MEM_R16(e + 0xE);
                    MEM_W16(e + 6, v + 0xD0);
                    MEM_W16(e + 0xE, pal + 1);
                }
                j++;
                if (!((s32)j < 2)) break;
                LOOP_POLL(0x800C84BCu);
            }
            if (GV(u32, 0x800CCC10u) == 0x200000) gcall(F_MOUSE_HINT, 0x800CC4ACu, 0, 0x10, 0xB8);
            gcall(F_FADE_TO, 0, 0x10, R[6], R[7]);
            SCREEN = SCREEN + 1;
        }
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        return;
    }
    if (st == 1) {
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        if (OPT_STATE != 0) return;
        REPEAT = 0;
        TURN_WAIT = 0x10;
        OPENED = 0;
        u32 s = SCREEN;
        CURRENT = 0;
        SCREEN = s + 1;
        return;
    }
    if (st == 2) {
        if (PAD_MODE != 0x200000) {
            u32 b = GV(u32, 0x800B4B94u);
            GV(u32, 0x800B4B94u) = (b & 0x1000) ? (b | 0x8000) : (b & 0xFFFF7FFFu);
            b = GV(u32, 0x800B4B94u);
            GV(u32, 0x800B4B94u) = (b & 0x4000) ? (b | 0x2000) : (b & 0xFFFFDFFFu);
        }
        if ((pressed(PAD1, 0x7FF) << 16) != 0 && (pressed(PAD2, 0x7FF) << 16) == 0) goto exit_screen;
        if ((pressed(PAD1, 0x800) << 16) != 0 || (pressed(PAD1, 0x40) << 16) != 0) {
            s32 t = (s32)REPEAT;
            if (t > 0) { REPEAT = (u32)(t - 1); return; }
            REPEAT = TURN_WAIT;
            if ((pressed(PAD1, 0x800) << 16) != 0) {
                s32 cc = (s32)CURRENT - 1;
                CURRENT = (u32)cc;
                if (cc < 0) CURRENT = 3;
            } else {
                s32 cc = (s32)CURRENT + 1;
                CURRENT = (u32)cc;
                if (!(cc < 4)) CURRENT = 0;
            }
            repeat_sound(1);
            return;
        }
        REPEAT = 0;
        TURN_WAIT = 0x10;
        OPENED = 0;
        if ((pressed(PAD1, 0x20) << 16) == 0) return;
        if ((pressed(PAD2, 0x20) << 16) != 0) return;
        {
            u32 c = CURRENT;
            if (c == 3) { LOOP_POLL(0x800C878Cu); goto exit_screen; }
            u32 s = SCREEN;
            SLIDE = 0;
            OPENED = 0;
            SCREEN = s + 1;
            if (c == 0) {
                ROW = SAVED_STEREO;
            } else if (c == 1) {
                ROW = SAVED_BGM;
                s32 k = music_pair();
                if (k != -1) gcall(F_GET_PAIR_VOLUME, (u32)k, 0, MUSIC_LEFT, MUSIC_RIGHT);
                s32 b = (s32)SAVED_BGM;
                s32 l = mips_div(GV(s16, MUSIC_LEFT) * 100, b);
                s32 r = mips_div(GV(s16, MUSIC_RIGHT) * 100, b);
                GV(u16, MUSIC_LEFT) = (u16)l;
                GV(u16, MUSIC_RIGHT) = (u16)r;
            } else {
                u32 v = SAVED_SE;
                ROW = v;
                GV(u32, 0x800CCC08u) = v;
            }
            gcall(F_MENU_SOUND, 0, 2, 0x7F, 0x7F);
            return;
        }
    exit_screen:
        CURRENT = 3;
        SCREEN = 4;
        gcall(F_FADE_OUT, 0, 0x10, R[6], R[7]);
        gcall(F_MENU_SOUND, 0, 3, 0x7F, 0x7F);
        return;
    }
    if (st != 3) {
        gcall(F_FADE_STEP, R[4], R[5], R[6], R[7]);
        if (OPT_STATE != 4) return;
        n_option_ClearMarks();
        REPEAT = 0;
        SCREEN = 0;
        GV(u32, 0x800CCBCCu) = 1;
        return;
    }
    s32 d = SLIDE;
    if (d < 0) {                                  /* the box sliding back */
        s32 o = OPENED;
        SLIDE = -d;
        if (o != 0) {
            OPENED = o - 1;
        } else {
            s32 v = -d - 0x40;
            SLIDE = v;
            if (!(v > 0)) {
                REPEAT = 0;
                SLIDE = 0;
                SCREEN = 2;
                TURN_WAIT = 0x10;
                OPENED = 1;
            }
        }
        SLIDE = -SLIDE;
        return;
    }
    if (d < 147) {                                /* sliding out */
        s32 v = d + 0x40;
        SLIDE = v;
        if (!(v < 147)) SLIDE = 0x93;
        return;
    }
    if ((pressed(PAD1, 0x7FF) << 16) != 0 && (pressed(PAD2, 0x7FF) << 16) == 0) {
        u32 c = CURRENT;
        if (c == 2) {
            SAVED_SE = GV(u32, 0x800CCC08u);
            GV(u16, 0x80062DB6u) = GV(u16, 0x800CCC08u);
        } else if (c == 1) {
            s32 k = music_pair();
            if (k != -1) {
                s32 b = (s32)SAVED_BGM;
                music_volume(k, b);
            }
        }
        gcall(F_MENU_SOUND, 0, 3, 0x7F, 0x7F);
        goto close_box;
    }
    {
        s32 o = OPENED;
        if (o != 0) OPENED = o - 1;
    }
    if (CURRENT == 0) {
        if ((pressed(PAD1, 0x800) << 16) != 0 || (pressed(PAD1, 0x40) << 16) != 0) {
            u32 t = REPEAT;
            if (t != 0) { REPEAT = t - 1; return; }
            u32 w = TURN_WAIT;
            REPEAT = w;
            TURN_WAIT = w - 1;
            if ((s32)(w - 1) < 0) TURN_WAIT = 0;
            ROW = ROW ^ 1;
            repeat_sound(1);
            return;
        }
        REPEAT = 0;
        TURN_WAIT = 0x10;
        if ((pressed(PAD1, 0x20) << 16) == 0) return;
        if ((pressed(PAD2, 0x20) << 16) != 0) return;
        u32 m = ROW;
        SAVED_STEREO = m;
        if (m == 0) {
            gcall(F_SOUND_MONO, R[4], R[5], R[6], R[7]);
            GV(u8, 0x80062D35u) = 1;
            GV(u8, 0x800CCC54u) = 0x7F;
            GV(u8, 0x800CCC55u) = 0;
            GV(u8, 0x800CCC56u) = 0x7F;
            GV(u8, 0x800CCC57u) = 0;
        } else {
            gcall(F_SOUND_STEREO, R[4], R[5], R[6], R[7]);
            GV(u8, 0x80062D35u) = 0;
            s32 pairs = (s16)gcall(0x80012DD0u, R[4], R[5], R[6], R[7]);
            for (s32 i = 0;;) {
                if ((pairs >> (i & 31)) & 1) gcall(0x80012A48u, (u32)(s32)(s16)i, R[5], R[6], R[7]);
                i++;
                if (!(i < 3)) break;
                LOOP_POLL(0x800C8C5Cu);
            }
            GV(u8, 0x800CCC54u) = 0x3F;
            GV(u8, 0x800CCC55u) = 0x3F;
            GV(u8, 0x800CCC56u) = 0x3F;
            GV(u8, 0x800CCC57u) = 0x3F;
        }
        gcall(F_CD_MIX, 0x800CCC54u, R[5], R[6], R[7]);
        OPENED = 4;
        EDITED = ROW;
        LOOP_POLL(0x800C8CB4u);
        gcall(F_MENU_SOUND, 0, 2, 0x7F, 0x7F);
        goto close_box;
    }
    if ((pressed(PAD1, 0x800) << 16) != 0 || (pressed(PAD1, 0x40) << 16) != 0) {
        s32 old = (s32)ROW;
        u32 t = REPEAT;
        if (t != 0) {
            REPEAT = t - 1;
        } else {
            u32 w = TURN_WAIT;
            REPEAT = w;
            TURN_WAIT = w - 1;
            if ((s32)(w - 1) < 0) TURN_WAIT = 0;
            if ((pressed(PAD1, 0x800) << 16) != 0) {
                s32 v = (s32)ROW + 1;
                ROW = (u32)v;
                if (!(v < 101)) ROW = 100;
            } else {
                s32 v = (s32)ROW - 1;
                ROW = (u32)v;
                if (v < 0) ROW = 0;
            }
            if (old != (s32)ROW) {
                if (!(GV(s32, 0x800CCBD8u) < 2) || !(GV(u32, 0x800CCC0Cu) & 1))
                    gcall(F_MENU_SOUND, 0, 1, 0x7F, 0x7F);
            }
        }
        if (CURRENT == 2) {
            GV(u16, 0x80062DB6u) = GV(u16, 0x800CCBECu);
            return;
        }
        s32 v = (s32)ROW;
        if (old == v) return;
        s32 k = music_pair();
        if (k == -1) return;
        music_volume(k, v);
        return;
    }
    REPEAT = 0;
    TURN_WAIT = 8;
    if ((pressed(PAD1, 0x20) << 16) == 0) return;
    if ((pressed(PAD2, 0x20) << 16) != 0) return;
    MEM_W32(GV(u32, 0x800CCBC0u + CURRENT * 4), ROW);
    gcall(F_MENU_SOUND, 0, 2, 0x7F, 0x7F);
    if (CURRENT != 1) {
        LOOP_POLL(0x800C8F40u);
    } else {
        GV(u16, 0x80062DB4u) = GV(u16, 0x800CCC38u);
        LOOP_POLL(0x800C8F54u);
    }
close_box:
    REPEAT = 1;
    {
        s32 v = SLIDE;
        TURN_WAIT = 0x10;
        SLIDE = -v;
    }
}
