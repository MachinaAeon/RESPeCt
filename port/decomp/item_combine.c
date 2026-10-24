/* The status screen's Combine command (CONPAU_E): pick a second item in
 * the list (ST_ITEMBOX, 0x800CF669, set while choosing: the first item's
 * place in the list panel's +0x2A, its icon in panel 7, a marker copied
 * from the cursor in panel 8), then combine them: a weapon with its
 * rounds (loads it, the grenade launcher changes round type), two of the
 * same rounds, or two items of a recipe (0x800A0C50: 4 bytes, two items
 * and the result, 0xFF ends). Both must be in the same of the three
 * lists.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "status.h"

void n_status_CombineStart(void);
void n_status_Combine(void);

#define F_COPY          0x80039868u   /* (src, dst, n) */
#define RECIPES         0x800A0C50u
#define LAST_PLACE      GV(u16, 0x800CF79A)   /* where the list cursor goes after */

/* 800C7998: menu action: Combine: the first item (its icon, the marker),
 * back to the list to pick the second. */
void n_status_CombineStart(void) {
    FRAME(0x28);
    SAVE_RA(0x20);
    SAVE_SREG(3, 0x1C);
    SAVE_SREG(2, 0x18);
    SAVE_SREG(1, 0x14);
    SAVE_SREG(0, 0x10);
    sound(0x800C79C4u, 2);
    GV(u16, g_curPanel + 0x2A) = GV(u16, LIST + 0xE2);
    ST_ITEMBOX = 1;
    u32 v = JAL(0x800C7A04u, 0x800C94C4u, GV(u8, LIST + (u32)(s32)LIST_CURSOR), 0x500, (u32)-0x2C0, 6);
    INV_PANEL(7) = (u8)v;
    JAL(0x800C7A34u, 0x800CAE78u, pnl(v & 0xFF), (u32)-0x2C0, A2, A3);
    u32 n = JAL(0x800C7A3Cu, F_ALLOC_PANEL, A0, A1, A2, A3);
    if ((s16)n != -1) {
        u32 q = pnl_s((s16)n);
        INV_PANEL(8) = (u8)n;
        JAL(0x800C7A84u, F_COPY, pnl(INV_PANEL(1)), q, 0x44, A3);
        u16 h = GV(u16, q + 0xA), v6 = GV(u16, q + 6), y = GV(u16, q + 0x12);
        GV(u16, q + 0x22) = h;
        GV(u16, q + 0x24) = v6;
        GV(u16, q + 0x26) = y;
        GV(u16, q + 0xE) = (u16)(GV(u16, q + 0xE) + 1);
        GV(u16, q + 0x18) = (u16)(GV(u16, q + 0x18) - 1);
    }
    GV(u32, g_curPanel + 0x3C) = 0x800C9E80u;
}

static inline u32 item(u32 slot) { return ITEMS_B + slot * 8; }

/* 800C7AE0: panel callback: combine the item under the cursor with the
 * first one; on success the list is rebuilt (800C8A98 removes the used
 * item) and the screen goes on to 800C8340 (the marker and icons put
 * back, with a slide: copies of the icon in panel +0x26); else the buzz
 * and back to the list. The icon panel (7) shows the result (+0x28 id,
 * +0x2A count, +0x2C digit offset); the list panel's +0x28 is the place
 * of the item used up (-1 none), +0x20 whether it was a weapon pair. */
void n_status_Combine(void) {
    FRAME(0x30);
    SAVE_RA(0x2C);
    SAVE_SREG(4, 0x28);
    SAVE_SREG(3, 0x24);
    SAVE_SREG(2, 0x20);
    SAVE_SREG(1, 0x1C);
    SAVE_SREG(0, 0x18);
    u32 first_weapon = 0;              /* s4 */
    s32 cur = LIST_CURSOR;
    s32 at = GV(s16, g_curPanel + 0x2A);
    if (at == cur) goto fail;
    u32 slot1 = GV(u8, LIST + (u32)at);
    u32 slot2 = GV(u8, LIST + (u32)cur);
    u16 id1 = GV(u16, item(slot1));
    u16 id2 = GV(u16, item(slot2));
    int carried;
    if ((s32)slot1 < 0x50) {
        if (!((s32)slot2 < 0x50)) goto fail;
        carried = 0;
    } else if ((s32)slot1 < 0x90) {
        if (!(slot2 - 0x50 < 0x40)) goto fail;
        carried = 1;
    } else {
        if ((s32)slot2 < 0x90) goto fail;
        carried = 0;
    }
    if (carried) {
        u32 lp = g_curPanel;
        GV(u16, lp + 0x20) = 1;
        GV(u16, lp + 0x28) = 0xFFFF;
        u32 icon = pnl(INV_PANEL(7));          /* s0 */
        GV(u16, icon + 0x28) = id1;
        u32 wslot, oslot, wid, oid, t2;
        if (GV(u8, WEAPON_TABLE + (u32)(s32)(s16)id1 * 8 + 6) & 0x10) {
            first_weapon = 1;
            t2 = 1;
            wslot = slot1; oslot = slot2; wid = id1; oid = id2;
        } else if (GV(u8, WEAPON_TABLE + (u32)(s32)(s16)id2 * 8 + 6) & 0x10) {
            t2 = 0;
            wslot = slot2; oslot = slot1; wid = id2; oid = id1;
        } else {
            /* two of the same rounds: into the second */
            if ((s32)(s16)id1 != (s32)(s16)id2) goto fail;
            u32 e2 = item(slot2), e1 = item(slot1);
            GV(u16, lp + 0x28) = GV(u16, lp + 0x2A);
            u16 c2 = GV(u16, e2 + 2), c1 = GV(u16, e1 + 2), i2 = GV(u16, e2);
            GV(u16, e2 + 2) = (u16)(c2 + c1);
            GV(u16, icon + 0x28) = i2;
            GV(u16, icon + 0x2A) = GV(u16, e2 + 2);
            GV(u16, icon + 0x2C) = (u16)(GV(u16, e2) - 0xC);
            goto rebuild;
        }
        u32 w = item(wslot), o = item(oslot);
        s32 wid_s = (s16)wid;
        if (wid_s == 8) {
            /* grenade launcher: rounds 0xD to 0xF */
            if (!((u16)(oid - 0xD) < 3)) goto fail;
            if (GV(s16, w + 4) == (s16)oid) {
                if (GV(s16, w + 6) != -1) GV(u16, w + 6) = (u16)(GV(u16, w + 6) + GV(u16, o + 2));
                GV(u16, icon + 0x2C) = (u16)(oid - 0xC);
                if (t2) {
                    GV(u16, g_curPanel + 0x28) = LAST_PLACE;
                    GV(u16, icon + 0x2A) = GV(u16, w + 6);
                } else {
                    u32 c = g_curPanel;
                    GV(u16, c + 0x28) = GV(u16, c + 0x2A);
                    GV(u16, icon + 0x2A) = 0;
                }
            } else {
                /* another round type: the loaded ones go back */
                u16 n = GV(u16, o + 2);
                if (GV(s16, w + 2) == 0) {
                    GV(u16, o + 2) = 0;
                    if (t2) GV(u16, g_curPanel + 0x28) = LAST_PLACE;
                    else { u32 c = g_curPanel; GV(u16, c + 0x28) = GV(u16, c + 0x2A); }
                } else {
                    GV(u16, o + 2) = (u16)(GV(u16, w + 6) + 1);
                    GV(u16, o) = GV(u16, w + 4);
                }
                GV(u16, w + 6) = (u16)(n - 1);
                GV(u16, w + 2) = 1;
                GV(u16, w + 4) = (u16)oid;
                if (t2) {
                    GV(u16, icon + 0x2A) = n;
                    GV(u16, icon + 0x2C) = (u16)(oid - 0xC);
                } else {
                    GV(u16, icon + 0x28) = GV(u16, o);
                    GV(u16, icon + 0x2A) = GV(u16, o + 2);
                    GV(u16, icon + 0x2C) = (u16)(GV(u16, o) - 0xC);
                }
            }
            GV(u16, 0x800AFCB0) = GV(u16, 0x800AFCB0) & 0xFFF7;
            g_fireCooldown = g_fireCooldown & 0x7FFF;
            JAL(0x800C7E64u, F_LOAD_SOUNDS, A0, A1, A2, A3);
        } else {
            /* load rounds of its type */
            if (GV(s16, w + 4) != (s16)oid) goto fail;
            if (GV(s16, w + 2) == GV(s16, WEAPON_TABLE + (u32)(s32)GV(s16, w) * 8 + 4)) {
                ST_SOUND_ON = 0;
                ST_SOUND = 0x800CF310u;            /* full */
                goto fail;
            }
            GV(u16, icon + 0x2C) = 0;
            u16 mag = GV(u16, WEAPON_TABLE + (u32)wid_s * 8 + 4);
            s16 n = GV(s16, o + 2);
            u16 nu = GV(u16, o + 2);
            if (n < 0) {
                GV(u16, w + 2) = mag;
            } else if ((s32)(s16)mag < (s32)GV(s16, w + 2) + n) {
                GV(u16, o + 2) = (u16)(nu - (mag - GV(u16, w + 2)));
                GV(u16, w + 2) = mag;
            } else {
                if (t2) GV(u16, g_curPanel + 0x28) = LAST_PLACE;
                else { u32 c = g_curPanel; GV(u16, c + 0x28) = GV(u16, c + 0x2A); }
                GV(u16, w + 2) = (u16)(GV(u16, w + 2) + GV(u16, o + 2));
                GV(u16, o + 2) = 0;
            }
            if (t2) GV(u16, icon + 0x2A) = GV(u16, w + 2);
            else GV(u16, icon + 0x2A) = GV(u16, o + 2);
        }
        /* the weapon held: its bullet row again */
        if (g_currentWeaponAddr == w) {
            JAL(0x800C7FD4u, 0x80095CDCu, A0, A1, A2, A3);
            JAL(0x800C7FE8u, 0x80095B90u, 0x62, 0xD4, (u32)(s32)GV(s16, g_currentWeaponAddr + 2), A3);
        }
    } else {
        /* a recipe */
        u32 r = RECIPES, r1 = RECIPES + 1;
        u32 a = (u32)(s32)(s16)id1, b = (u32)(s32)(s16)id2;
        GV(u16, g_curPanel + 0x20) = 0;
        u32 v;
        for (;;) {
            v = GV(u8, r);
            if (v == a && GV(u8, r1) == b) break;
            if (v == b && GV(u8, r1) == a) { v = GV(u8, r); break; }
            r += 4;
            r1 += 4;
            if (GV(u8, r) == 0xFF) { v = GV(u8, r); break; }
            LOOP_POLL(0x800C8060);
        }
        if (v == 0xFF) goto fail;
        u32 c = g_curPanel;
        GV(u16, c + 0x2C) = GV(u8, r + 2);
        GV(u16, c + 0x28) = GV(u16, c + 0x2A);
        u32 e = item(GV(u8, LIST + (u32)(s32)LIST_CURSOR));
        GV(u16, e) = GV(u8, r + 2);
        GV(u16, e + 2) = 0;
        if (GV(s16, c + 0x20) == 0) GV(u32, e + 4) = 0;
        else GV(u32, e + 4) = GV(u32, WEAPON_TABLE + (u32)GV(u8, r + 2) * 8);
    }
rebuild: {
        u32 c = g_curPanel;
        s32 used = GV(s16, c + 0x28);
        if (used != -1) {
            JAL(0x800C8120u, 0x800C8A98u, GV(u8, LIST + (u32)used), A1, A2, A3);
            JAL(0x800C8128u, 0x800C964Cu, A0, A1, A2, A3);
            u32 pl = first_weapon ? GV(u16, g_curPanel + 0x2A) : GV(u16, LIST + 0xE2);
            if (GV(s16, g_curPanel + 0x28) < (s16)pl) {
                u32 q = pl - 1;
                pl = q;
                if ((((s32)(s16)q - 2) & 7) == 0) pl = q - 1;
            }
            LAST_PLACE = (u16)pl;
        } else {
            if (first_weapon) LAST_PLACE = GV(u16, c + 0x2A);
            JAL(0x800C81B0u, 0x800C964Cu, A0, A1, A2, A3);
        }
    }
    GV(u32, NAMES + 0x1A4) = 0x50000000u;
    GV(u8, NAMES + 0x1AB) = 0x50;
    {
        u32 c = g_curPanel;
        GV(u16, c + 0x1A) = 8;
        GV(u16, c + 0x22) = 0xA;
        GV(u32, c + 0x3C) = 0x800C8340u;
        GV(u16, pnl(INV_PANEL(8)) + PANEL_FLAGS) = 1;
        INV_PANEL(8) = 0xFF;
        u32 n = JAL(0x800C8224u, F_ALLOC_PANEL, A0, A1, A2, A3);
        if ((s16)n != -1) {
            u32 q = pnl_s((s16)n);
            GV(u16, g_curPanel + 0x26) = (u16)n;
            s32 d;
            if (first_weapon) {
                JAL(0x800C8278u, F_COPY, pnl(INV_PANEL(6)), q, 0x44, A3);
                d = -0x300;
            } else {
                JAL(0x800C82ACu, F_COPY, pnl(INV_PANEL(7)), q, 0x44, A3);
                d = 0x300;
            }
            GV(u16, q + 0x30) = (u16)mips_div(d, GV(s16, g_curPanel + 0x1A));
            GV(u16, q + 0x18) = (u16)(GV(u16, q + 0x18) - 1);
        }
        sound(0x800C82F0u, 2);
    }
    return;
fail:
    sound(0x800C830Cu, 4);
    GV(u32, g_curPanel + 0x3C) = 0x800C9E80u;
}
