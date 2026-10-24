/* Panel chains (TEST overlay): animations made of several panels.
 *
 * A chain animation's frames ({s16 offset entry (-1 ends), u16 duration,
 * s16 panel count}) give an offset entry ({u, v, x, y, w, h}) for each
 * panel of the chain: the head is the current panel, the rest linked by
 * +0x36 (panel index, -1 ends), made as copies of the head when a frame
 * needs more of them (their callback does nothing) and freed when it
 * needs fewer. Positions are relative to the head's base (+0x22..+0x2A,
 * shared); world sprites (flag 0x400) are offset along the player's
 * facing (plus their own angle +0x30 with flag 8).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "hud.h"
#include "libgs.h"
#include "game_stack.h"

void n_hud_SetFrameChain(void);
void n_hud_FreeChain(void);
void n_hud_AnimChain(void);
void n_hud_StartAnimChain(void);
void n_hud_NoUpdate(void);

#define CB_NO_UPDATE 0x8009343Cu

/* 8009343C: the update callback of a chain's extra panels: nothing. */
void n_hud_NoUpdate(void) {}

/* 80093BBC (frame, frames, offsets): set the current panel's chain to a
 * frame. -1 at the end of the animation, 1 when the pool ran out, else 0. */
void n_hud_SetFrameChain(void) {
    FRAME(0x78);
    u32 e = A1 + (u32)((s32)(s16)(A0 * 3) * 2);
    s16 k = GV(s16, e);
    if (k == -1) { V0 = (u32)-1; return; }
    u32 o = A2 + (u32)((s32)(s16)(k * 3) * 4);
    u16 count = GV(u16, e + 4);
    u32 p = g_curPanel;
    P16(p, 0x1A) = GV(u16, e + 2);
    u16 fx = 0, fz = 0;
    if (P16(p, PANEL_FLAGS) & 0x400) {
        u32 ang = GV(u16, 0x800A8606);
        if (P16(p, PANEL_FLAGS) & 8) ang += P16(p, 0x30);
        ang &= 0xFFF;
        u32 m = sp + 0x28;
        copy_words(m, IDENTITY_MATRIX, 8);
        GS(s16, sp + 0x10) = 0x3E8;
        GS(s16, sp + 0x12) = 0;
        GS(s16, sp + 0x14) = 0;
        GCALL2(F_ROT_MATRIX_Y, ang, m);
        GCALL1(F_SET_ROT_MATRIX, m);
        GCALL1(F_SET_TRANS_MATRIX, m);
        GCALL3(F_ROT_TRANS, sp + 0x10, sp + 0x18, sp + 0x48);
        fx = GS(u16, sp + 0x18);
        fz = GS(u16, sp + 0x20);
    }
    u32 link = 0xFFFFFFFFu;                  /* $a1: the last panel's old link */
    p = g_curPanel;
    s32 n = (s16)count;
    for (s32 i = 0; i < n; i++) {
        P16(p, 0x04) = (u16)(P16(p, 0x22) + GV(u16, o));
        P16(p, 0x06) = (u16)(P16(p, 0x24) + GV(u16, o + 2));
        s32 dx = ((s32)GV(s16, o + 4) * P16(p, 0x34)) >> 8;
        if (P16(p, PANEL_FLAGS) & 0x400) {
            s32 s = (s16)dx;
            s32 a = (s32)((u32)s * (u32)(s32)(s16)fx) / 50 - 0xA0;
            P16(p, 0x10) = (u16)(P16(p, 0x26) + div_pow2(a, 2));
            P16(p, 0x18) = (u16)(P16(p, 0x2A) + (s32)((u32)s * (u32)(s32)(s16)fz) / 800);
        } else {
            P16(p, 0x10) = (u16)((P16(p, 0x26) + dx - 0xA0) << 4);
        }
        s32 dy = ((s32)GV(s16, o + 6) * P16(p, 0x34)) >> 8;
        if (P16(p, PANEL_FLAGS) & 0x400)
            P16(p, 0x12) = (u16)(P16(p, 0x28) + (s16)dy * 20 - 0x78);
        else
            P16(p, 0x12) = (u16)((P16(p, 0x28) + dy - 0x78) << 4);
        u32 w = GV(u16, o + 8), h = GV(u16, o + 10);
        o += 12;
        P16(p, 0x08) = (u16)w;
        P16(p, 0x0A) = (u16)h;
        if (P16(p, PANEL_FLAGS) & 8) {
            w = (u32)(((s32)(s16)w * P16(p, 0x34)) >> 8);
            P16(p, 0x14) = (u16)w;
            h = (u32)(((s32)(s16)h * P16(p, 0x34)) >> 8);
            P16(p, 0x16) = (u16)h;
        }
        if (P16(p, PANEL_FLAGS) & 0x20) {
            P16(p, 0x10) = (u16)(P16(p, 0x10) - (s16)w * 8);
            P16(p, 0x12) = (u16)(P16(p, 0x12) - (s16)h * 8);
        }
        link = P16(p, 0x36);
        if (i < n - 1) {
            if ((s16)link == -1) {
                n_hud_AllocPanel();
                link = V0;
                if ((s16)link == -1) { V0 = 1; return; }
                u32 q = panel_addr((s16)link);
                for (u32 k = 0; k < PANEL_SIZE; k += 4) P32(q, k) = P32(p, k);
                P32(q, PANEL_UPDATE) = CB_NO_UPDATE;
                P16(q, 0x36) = 0xFFFF;
                P16(p, 0x36) = (u16)link;
            }
            p = panel_addr((s16)link);
        } else {
            P16(p, 0x36) = 0xFFFF;
        }
    }
    /* free the panels beyond the frame's count */
    for (s16 i = (s16)link; i != -1;) {
        u32 q = panel_addr(i);
        i = (s16)P16(q, 0x36);
        P16(q, PANEL_FLAGS) = 1;
    }
    V0 = 0;
}

/* 8009436C (panel): free a chain. */
void n_hud_FreeChain(void) {
    u32 p = A0;
    for (s16 i = (s16)P16(p, 0x36); i != -1; i = (s16)P16(p, 0x36)) {
        P16(p, PANEL_FLAGS) = 1;
        p = panel_addr(i);
    }
    P16(p, PANEL_FLAGS) = 1;
    V0 = 1;
}

/* 800943D0: update callback: play the current panel's chain animation
 * (+0x38 {frames, offsets}) once, freeing the chain at its end. */
void n_hud_AnimChain(void) {
    FRAME(0x18);
    u32 p = g_curPanel;
    P16(p, 0x1A) = (u16)(P16(p, 0x1A) - 1);
    if ((s16)P16(p, 0x1A) > 0) { V0 = (u32)P16(p, 0x1A) << 16; return; }
    u16 f = (u16)(P16(p, 0x1E) + 1);
    u32 anim = P32(p, 0x38);
    P16(p, 0x1E) = f;
    A0 = (u32)(s32)(s16)f; A1 = GV(u32, anim); A2 = GV(u32, anim + 4);
    n_hud_SetFrameChain();
    if ((s16)V0 == -1) {
        P16(g_curPanel, 0x20) = 0xFFFF;
        A0 = g_curPanel;
        n_hud_FreeChain();
        return;
    }
    V0 = (u32)-1;
}

/* 800948BC (anim {frames, offsets}): start a chain animation on the
 * current panel. */
void n_hud_StartAnimChain(void) {
    FRAME(0x20);
    u32 anim = A0;
    u32 p = g_curPanel;
    u16 u = P16(p, 0x04), v = P16(p, 0x06), x = P16(p, 0x10), y = P16(p, 0x12), z = P16(p, 0x18);
    P16(p, 0x1E) = 0;
    P16(p, 0x36) = 0xFFFF;
    P16(p, 0x22) = u;
    P16(p, 0x24) = v;
    P16(p, 0x26) = x;
    P16(p, 0x28) = y;
    P16(p, 0x2A) = z;
    A0 = 0; A1 = GV(u32, anim); A2 = GV(u32, anim + 4);
    n_hud_SetFrameChain();
    P32(g_curPanel, PANEL_DRAWPTR) = anim;
    V0 = g_curPanel;
}
