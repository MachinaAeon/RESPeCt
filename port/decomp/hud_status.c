/* The HUD's condition display and weapon images (TEST overlay).
 *
 * The condition icon (an ECG) shows the player's health state: 0x800A8584
 * s16 health, 0x800A8586 u8 condition (0 fine, 1 caution, 2 danger, 3
 * poison), 0x800A8587 u8 poisoned. It is three panels: the icon (its
 * sprite frame +0x0E = base +0x2C + condition), a trace line (+0x2A; a
 * gouraud box whose colours come from the condition's colour at
 * 0x800A61C4 scaled by how far the trace has swept) and a fading tail
 * (+0x24). When the condition changes the icon plays a short change
 * animation (0x80095A40) before going back to the trace (0x8009504C).
 * While poisoned, health drops by 1 every 60 frames down to 1.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "hud.h"
#include "game_stack.h"

void n_hud_LoadWeaponImages(void);
void n_hud_LoadAmmoImage(void);
void n_hud_InitCondition(void);
void n_hud_ConditionUpdate(void);
void n_hud_ConditionChange(void);

#define g_health        GV(s16, 0x800A8584)
#define g_condition     GV(u8, 0x800A8586)
#define g_poisoned      GV(u8, 0x800A8587)
#define g_poisonTimer   GV(u8, 0x800B4BE0)
#define COND_COLOURS    0x800A61C4u           /* r, g, b per condition */
#define ECG_ANIM        0x800A61BCu
#define F_LOAD_FILE     0x800115C0u
#define F_BCOPY         0x80039868u           /* (src, dst, n) */
#define F_UNPACK_IMAGES 0x80092878u

/* 80094A30 (buffer): load the HUD's images from the CONPAU archive: file
 * 12 (models set up at once) into buffer, file 13 at buffer + 40000, and
 * unpack the held weapon's images. */
void n_hud_LoadWeaponImages(void) {
    FRAME(0x30);
    u32 buf = A0, second = buf + 0x9C40;
    GS(u32, sp + 0x10) = buf;
    GCALL4(F_LOAD_FILE, 0x8006C94Cu, 0x8005853Cu, 0x800A0C74u, 0xC);
    GCALL2(0x800927D8u, buf, (u32)-1);
    GS(u32, sp + 0x10) = second;
    GCALL4(F_LOAD_FILE, 0x8006C94Cu, 0x8005853Cu, 0x800A0C74u, 0xD);
    GCALL3(F_UNPACK_IMAGES, second, buf, (s32)GV(s16, g_currentWeaponAddr));
}

/* 80094C60 (ammo item): load the bullet image for an ammo item (its file
 * from the {item, file} pairs at 0x800A617C) to 0x800D28B0 and set it up. */
void n_hud_LoadAmmoImage(void) {
    FRAME(0x20);
    s32 item = (s16)A0;
    u32 i = 0;
    if (GV(s16, 0x800A617C) != item) {
        do i += 2;
        while (GV(s16, 0x800A617Cu + (u32)((s32)(i << 16) >> 15)) != item);
    }
    u32 e = 0x800A617Cu + (u32)((s32)(i << 16) >> 15);
    GS(u32, sp + 0x10) = 0x800D28B0u;
    GCALL4(F_LOAD_FILE, 0x8006C94Cu, 0x8005853Cu, 0x800A0C74u, (s32)GV(s16, e + 2));
    GCALL1(0x8006F370u, 0x800D28B0u);
}

/* 80094E44 (x, y): create the condition icon and its two child panels. */
void n_hud_InitCondition(void) {
    FRAME(0x28);
    s32 x = (s32)A0, y = (s32)A1;
    g_panelSave = g_curPanel;
    n_hud_AllocPanel();
    if ((s16)V0 != -1) {
        u32 p = panel_addr((s16)V0);
        g_curPanel = p;
        P16(p, PANEL_FLAGS) |= 0x244;
        A0 = p; A1 = 0x800A612Cu;
        n_hud_SetSprite();
        p = g_curPanel;
        P16(p, 0x10) = (u16)((x - 0xA0) << 4);
        P16(p, 0x12) = (u16)((y - 0x78) << 4);
        P16(p, 0x18) = 2;
        u16 frame = P16(p, 0x0E);
        P16(p, 0x28) = 0;
        P32(p, PANEL_UPDATE) = 0x8009504Cu;
        P16(p, 0x26) = g_condition;
        P16(p, 0x2C) = frame;                            /* the icon's first frame */
    }
    n_hud_AllocPanel();
    u32 k = V0;
    if ((s16)k != -1) {                                  /* the trace */
        u32 c = panel_addr((s16)k);
        P16(g_curPanel, 0x2A) = (u16)k;
        GCALL3(F_BCOPY, g_curPanel, c, 0x44);
        P16(c, 0x08) = 0;
        P16(c, PANEL_FLAGS) |= 0x8100;
        u32 col = COND_COLOURS + (u32)g_condition * 3;
        P16(c, 0x2E) = GV(u8, col);
        P16(c, 0x30) = GV(u8, col + 1);
        P16(c, 0x18) = (u16)(P16(c, 0x18) + 1);
        P16(c, 0x32) = GV(u8, col + 2);
    }
    n_hud_AllocPanel();
    k = V0;
    if ((s16)k != -1) {                                  /* the tail */
        u32 c = panel_addr((s16)k);
        P16(g_curPanel, 0x24) = (u16)k;
        GCALL3(F_BCOPY, g_curPanel, c, 0x44);
        for (u32 o = 0x22; o <= 0x2C; o += 2) P16(c, o) = 0;
        P16(c, PANEL_FLAGS) |= 0x8100;
        P16(c, 0x18) = (u16)(P16(c, 0x18) + 2);
    }
    n_hud_ConditionUpdate();
    V0 = g_panelSave;
    g_curPanel = g_panelSave;
}

/* Move both child panels with the icon. */
static void children_follow(u32 icon, u32 trace) {
    u32 tail = panel_addr((s16)P16(icon, 0x24));
    P16(trace, 0x10) = P16(icon, 0x10);
    P16(tail, 0x10) = P16(icon, 0x10);
    P16(trace, 0x12) = P16(icon, 0x12);
    P16(tail, 0x12) = P16(icon, 0x12);
}

/* 8009504C: the condition icon's update: work out the condition (and
 * the poison damage), sweep the trace; a change of condition starts the
 * change animation. */
void n_hud_ConditionUpdate(void) {
    FRAME(0x28);
    u32 p = g_curPanel;
    u32 trace = panel_addr((s16)P16(p, 0x2A));
    if (g_poisoned) {
        P16(p, 0x0E) = (u16)(P16(p, 0x2C) + 1);
        g_condition = 3;
        if ((GV(u32, 0x800B3D38) | GV(u32, 0x800B3D20) | GV(u32, 0x800BBB58)) == 0 && g_health >= 2) {
            g_poisonTimer = (u8)(g_poisonTimer + 1);
            if (g_poisonTimer >= 0x3C) {
                g_poisonTimer = 0;
                GV(u16, 0x800A8584) = (u16)(GV(u16, 0x800A8584) - 1);
            }
        }
    } else {
        s16 h = g_health;
        u16 fr;
        if (h < 0x2D) { g_condition = 2; fr = (u16)(P16(p, 0x2C) + 2); }
        else if (h < 0x69) { g_condition = 1; fr = (u16)(P16(p, 0x2C) + 1); }
        else { g_condition = 0; fr = P16(p, 0x2C); }
        P16(p, 0x0E) = fr;
    }
    A0 = ECG_ANIM;
    n_hud_Animate();
    if (V0 & 0xFFFF) children_follow(g_curPanel, trace);
    p = g_curPanel;
    if ((s16)P16(p, 0x26) != g_condition) {             /* changed: play the change animation */
        P16(trace, PANEL_FLAGS) &= 0xFFFB;
        A0 = p; A1 = 0x800A6118u;
        n_hud_SetSprite();
        p = g_curPanel;
        P32(p, PANEL_UPDATE) = 0x80095A40u;
        P16(p, 0x20) = 0;
        P16(p, 0x1E) = 0;
        P32(p, PANEL_DRAWPTR) = 0x800A6214u;
        P16(p, 0x1A) = GV(u16, 0x800A6214);
        P16(p, 0x22) = P16(p, 0x0E);
        V0 = 0x800A6214u;                                /* as the original leaves it */
        return;
    }
    u32 lo = 0, hi;
    if ((s16)P16(trace, 0x10) == (s16)P16(p, 0x10) && P16(trace, 0x08) < 0x20) {
        u16 w = (u16)(P16(trace, 0x08) + 2);             /* the trace grows */
        P16(trace, 0x08) = w;
        if (w >= 0x20) {
            P16(trace, 0x08) = 0x20;
            P16(trace, 0x10) = P16(p, 0x10);
        }
        s32 v = (s32)(0x20 - P16(trace, 0x08)) << 4;
        lo = (v >= 0 ? (u32)v : (u32)(v + 0x1F)) >> 5;
        hi = 0x10;
    } else {
        u32 icon = g_curPanel;                           /* the trace sweeps on */
        u16 tx = (u16)(P16(trace, 0x10) + 0x20);
        P16(trace, 0x10) = tx;
        s32 ix = (s16)P16(icon, 0x10);
        hi = 0x10;
        if (ix + 0x180 < (s16)tx) {
            u16 w = (u16)((ix - ((s16)tx - 0x380)) >> 4);
            P16(trace, 0x08) = w;
            if (w == 0) {
                P16(trace, 0x08) = 0;
                P16(trace, 0x10) = P16(icon, 0x10);
                V0 = P16(icon, 0x10);
                return;
            }
            s32 v = (s32)P16(trace, 0x08) << 4;
            s32 m = (s32)(((s64)v * (s32)0x92492493u) >> 32);
            hi = (u32)(m + v) >> 5;                      /* v / 56 */
        }
    }
    s32 a = (s16)lo, b = (s16)hi;
    s32 r = GV(s16, trace + 0x2E), g = GV(s16, trace + 0x30), bl = GV(s16, trace + 0x32);
    P16(trace, 0x22) = (u16)((r * a) >> 4);
    P16(trace, 0x24) = (u16)((g * a) >> 4);
    P16(trace, 0x26) = (u16)((bl * a) >> 4);
    P16(trace, 0x28) = (u16)((r * b) >> 4);
    P16(trace, 0x2A) = (u16)((g * b) >> 4);
    P16(trace, 0x2C) = (u16)((bl * b) >> 4);
    V0 = (u32)((bl * b) >> 4);
}

/* 80095A40: the condition change animation; at its end, back to the
 * trace in the new condition's colour. */
void n_hud_ConditionChange(void) {
    FRAME(0x18);
    A0 = ECG_ANIM;
    n_hud_Animate();
    if (V0 & 0xFFFF) {                                   /* move the children with it */
        u32 p = g_curPanel;
        u32 trace = panel_addr((s16)P16(p, 0x2A));
        P16(trace, 0x10) = P16(p, 0x10);
        P16(trace, 0x12) = P16(p, 0x12);
        u32 tail = panel_addr((s16)P16(p, 0x24));
        P16(tail, 0x10) = P16(p, 0x10);
        P16(tail, 0x12) = P16(p, 0x12);
    }
    GCALL0(0x80094454u);                                 /* step the change animation's timer */
    u32 p = g_curPanel;
    V0 = (u32)-1;
    if ((s16)P16(p, 0x20) == -1) {
        g_iconChanged = 1;
        A0 = p; A1 = 0x800A612Cu;
        n_hud_SetSprite();
        p = g_curPanel;
        P16(p, 0x26) = g_condition;
        u32 trace = panel_addr((s16)P16(p, 0x2A));
        u32 col = COND_COLOURS + (u32)g_condition * 3;
        P16(trace, PANEL_FLAGS) |= 4;
        P16(trace, 0x2E) = GV(u8, col);
        P16(trace, 0x30) = GV(u8, col + 1);
        P16(trace, 0x32) = GV(u8, col + 2);
        P32(p, PANEL_UPDATE) = 0x8009504Cu;
        V0 = 0x8009504Cu;
    }
}
