/* The HUD panel system (TEST overlay).
 *
 * The in-game HUD (weapon box, bullet row, condition monitor, the weapon
 * view's state) is made of panels: 0xB4 slots of 0x44 bytes at 0x800C09D8.
 * Each live panel has a sprite and a per-frame callback; every frame the
 * live ones are listed (0x800C0868, ended by -1) and their callbacks run
 * with "the current panel" (0x800C39A8) pointing at them. After the
 * inventory screen closes the game frees every panel and builds the HUD
 * again from a table (0x800A65C8), because some panels capture the held
 * weapon when they're created.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "hud.h"

#include <string.h>

void n_hud_ResetPanels(void);
void n_hud_AllocPanel(void);
void n_hud_BuildPanels(void);
void n_hud_UpdatePanels(void);
void n_hud_SetSprite(void);
void n_weapon_SetupHud(void);
void n_weapon_CopyViewImage(void);
void n_hud_InitWeaponIcon(void);
void n_hud_InitBullets(void);
void n_hud_InitWeaponCtl(void);
void n_hud_WeaponCtl(void);
void n_hud_Animate(void);
void n_hud_IconUpdate(void);
void n_hud_BulletsUpdate(void);
void n_hud_FreeBullets(void);
void n_hud_BulletRow(void);
void n_hud_NewBullet(void);
void n_weapon_StartReload(void);
void n_hud_SetFrameB(void);
void n_hud_AnimLoopA(void);
void n_hud_AnimOnceA(void);
void n_hud_AnimLoopB(void);
void n_hud_AnimOnceB(void);
void n_hud_CtlTimer(void);
void n_hud_FreeAll(void);
void n_hud_FreePaused(void);

/* 80092DD0: free every panel. */
void n_hud_ResetPanels(void) {
    g_curPanel = PANELS;
    memset(gp(PANELS), 0, PANEL_COUNT * PANEL_SIZE);          /* BIOS memset */
    g_panelMode = 2;
    for (int i = 0; i < PANEL_COUNT; i++) {
        P16(g_curPanel, PANEL_FLAGS) = 1;
        g_curPanel += PANEL_SIZE;
    }
    V0 = 0;
}

/* 80092E3C: take a free panel; its index, or -1. */
void n_hud_AllocPanel(void) {
    for (s32 i = 0; i < PANEL_COUNT; i++) {
        u32 p = panel_addr(i);
        if (P16(p, PANEL_FLAGS) & 1) {
            P16(p, PANEL_FLAGS) = 2;
            V0 = (u32)i;
            return;
        }
    }
    V0 = (u32)-1;
}

/* 800945B4 (panel, sprite): give a panel a sprite. */
void n_hud_SetSprite(void) {
    u32 p = A0, s = A1;
    memcpy(gp(p), gp(s), 16);
    P16(p, 0x14) = P16(p, 0x08);
    P16(p, 0x16) = P16(p, 0x0A);
    u16 w = P16(p, 0x04) & 0x3F;
    P16(p, 0x04) = P16(p, 0x00) ? (u16)(w << 1) : (u16)(w << 2);   /* width in VRAM units */
    P16(p, 0x34) = 0x100;                                         /* scale */
    P16(p, 0x20) = 0;
    P16(p, 0x06) = GV(u8, p + 0x06);
    V0 = 0x100;
}

/* 80093444 (table): build panels from a table of 0x14-byte entries:
 * s16 sprite, s16 +0x18, s16 x, s16 y, u32 draw data, u16 _, u16 +0x22,
 * u16 flags, u32 init callback; sprite -1 ends it. Stops if the pool is
 * full. */
void n_hud_BuildPanels(void) {
    for (u32 e = A0; GV(s16, e) != -1; e += 0x14) {
        n_hud_AllocPanel();
        s16 i = (s16)V0;
        if (i == -1) return;
        u32 p = panel_addr(i);
        g_curPanel = p;
        P16(p, PANEL_FLAGS) |= GV(u16, e + 0xE);
        A0 = p;
        A1 = SPRITES + (u32)(s32)GV(s16, e) * 0x14;
        n_hud_SetSprite();
        p = g_curPanel;
        P16(p, 0x10) = (u16)((GV(u16, e + 4) - 0xA0) << 4);    /* screen centre relative, 12.4 */
        P16(p, 0x12) = (u16)((GV(u16, e + 6) - 0x78) << 4);
        P16(p, 0x18) = GV(u16, e + 2);
        P32(p, PANEL_DRAWPTR) = GV(u32, e + 8);
        P16(p, 0x22) = GV(u16, e + 0xC);
        u32 init = GV(u32, e + 0x10);
        if (init) {
            P32(p, PANEL_UPDATE) = init;
            call_panel(init, A0, A1);
        }
    }
}

/* 8009358C: every frame, run the live panels' callbacks (unless input is
 * locked for something other than state 14). */
void n_hud_UpdatePanels(void) {
    if (g_inputLocked != 0 && g_inputLocked != 0xE) return;
    s32 n = 0;
    s32 paused = D_800B3D38;
    for (s32 i = 0; i < PANEL_COUNT; i++) {
        u16 f = P16(panel_addr(i), PANEL_FLAGS);
        if (f & 0x181) continue;
        if (paused && (f & 0x400)) continue;
        g_panelList[n++] = (s16)i;
    }
    g_panelList[n] = -1;
    /* The list is re-read each step: a callback can change it. */
    for (s32 k = 0; g_panelList[k] != -1; k++) {
        s16 i = g_panelList[k];
        u32 p = PANELS + (u32)(s32)i * PANEL_SIZE;
        g_curPanel = p;
        call_panel(P32(p, PANEL_UPDATE), (u32)(s32)i << 16, (u16)i);
    }
}

/* 80094AE4 (weapon, slot): copy a weapon's view graphics (two VRAM
 * rectangles from its sprite entry) to where the slot's entry says. */
void n_weapon_CopyViewImage(void) {
    u32 from = SPRITES + (u32)(s32)(s16)A0 * 0x14 + 4;
    u32 to = SPRITES + (u32)(s32)(s16)A1 * 0x14 + 4;
    GameStack st;
    u32 rect = gstack_alloc(&st, 8);
    memcpy(gp(rect), gp(from), 8);
    GS(s16, rect + 4) = (s16)(GS(s16, rect + 4) >> 2);         /* 4-bit image: width / 4 */
    GCALL3(F_MOVE_IMAGE, rect, (s32)GV(s16, to), (s32)GV(s16, to + 2));
    memcpy(gp(rect), gp(from + 8), 8);
    GCALL3(F_MOVE_IMAGE, rect, (s32)GV(s16, to + 8), (s32)GV(s16, to + 10));
    GCALL1(F_DRAW_SYNC, 0);
    gstack_free(&st);
}

/* 8009493C: after the inventory screen: reset the weapon HUD state, load
 * the HUD images, set up the held weapon's view and rebuild the panels. */
void n_weapon_SetupHud(void) {
    GV(s16, 0x800C39B0) = 0;
    GV(s16, 0x800C3A00) = 0;
    GV(u8, 0x800C39FC) = 0;
    GV(s16, 0x800C39F2) = 0;
    for (u32 i = 0; i < 0x11; i++) GV(s16, 0x800C39C4 + i * 2) = -1;   /* bullet panels */
    GCALL1(F_WEAPON_FILES, 0x800C3E50u);
    s16 *w = gp(g_currentWeaponAddr);
    GCALL1(F_AMMO_IMAGE, (s32)w[2]);
    s16 id = ((s16 *)gp(g_currentWeaponAddr))[0];
    A0 = (u32)(s32)id;
    A1 = 0x44;
    n_weapon_CopyViewImage();
    g_weaponViewOn = GV(u32, 0x800A62A0 + (u32)(s32)id * 4);
    g_weaponAction = GV(u32, 0x800A62E8 + (u32)(s32)id * 4);
    A0 = 0x800A65C8u;
    n_hud_BuildPanels();
}

/* 80094D00: init of the weapon icon panel: offset it for the icon variant
 * (0x800A8586) and create its parts. */
void n_hud_InitWeaponIcon(void) {
    u32 p = g_curPanel;
    u32 b = GV(u8, 0x800A8586);
    const u32 t = 0x800A61A8u;                 /* u, v, then x/y offsets per variant */
    P16(p, 0x22) = P16(p, 0x04);
    P16(p, 0x24) = P16(p, 0x06);
    P16(p, 0x28) = 0;
    P16(p, 0x26) = (u16)b;
    P16(p, 0x08) = GV(u16, t);
    P16(p, 0x0A) = GV(u16, t + 2);
    P16(p, 0x04) = (u16)(P16(p, 0x04) + GV(u16, t + (b + 1) * 4));
    P32(p, PANEL_UPDATE) = CB_WEAPON_ICON;
    P16(p, 0x06) = (u16)(P16(p, 0x06) + GV(u16, t + (b + 1) * 4 + 2));
    GCALL2(F_ICON_PARTS, 0x100, 0xC0);
}

/* 800953BC: init of the bullet row panel: remember the weapon and its
 * ammo, then lay out the rounds. */
void n_hud_InitBullets(void) {
    u32 p = g_curPanel;
    P16(p, 0x28) = 0;
    g_hudBullets = panel_index(p);
    P16(p, 0x10) = (u16)(P16(p, 0x10) - (P16(p, 0x08) << 3));
    P16(p, 0x12) = (u16)(P16(p, 0x12) - (P16(p, 0x0A) << 3));
    u16 *w = gp(g_currentWeaponAddr);
    P16(p, 0x26) = w[0];
    P32(p, PANEL_UPDATE) = CB_BULLETS;
    P16(p, 0x2A) = w[2];
    GCALL3(F_BULLET_ROW, 0x62, 0xD4, (s32)(s16)w[1]);
}

/* 800958DC: init of the (invisible) weapon control panel. */
void n_hud_InitWeaponCtl(void) {
    u32 p = g_curPanel;
    P32(p, PANEL_DRAWPTR) = 0x800A61D0u;
    P16(p, 0x1A) = GV(u16, 0x800A61D0);
    P16(p, 0x1E) = 0;
    P16(p, 0x22) = P16(p, 0x0E);
    g_hudWeaponCtl = panel_index(p);
    P32(p, PANEL_UPDATE) = CB_WEAPON_CTL;
}

/* 80095948: the weapon control, every frame: while an action (reload) is
 * running, step it with the weapon's handler; when that says it's done,
 * refresh the bullet row and put the rounds back in the weapon. */
void n_hud_WeaponCtl(void) {
    if (!(P16(g_curPanel, PANEL_FLAGS) & 4) || D_800BBB58) return;
    GCALL0(F_ACTION_PRE);
    g_panelSave = g_curPanel;
    u32 done = GCALL0(g_weaponAction);
    if (!(done & 0xFFFF)) return;
    u32 ctl = g_panelSave;
    P16(ctl, PANEL_FLAGS) &= 0xFFFB;
    g_curPanel = ctl;
    u32 b = PANELS + (u32)(s32)g_hudBullets * PANEL_SIZE;
    g_curPanel = b;
    D_800AFCB0 &= 0xFFFB;
    P16(b, PANEL_FLAGS) |= 0x204;
    g_fireCooldown &= 0x7FFF;
    GV(u16, g_currentWeaponAddr + 2) = (u16)g_hudRounds;
}

/* 80094BDC (positions): move the current panel to its position for the
 * HUD animation frame, if that changed; 1 if it moved. */
void n_hud_Animate(void) {
    u32 p = g_curPanel, t = A0;
    s16 f = g_hudFrame;
    if ((s16)P16(p, 0x28) == f) { V0 = 0; return; }
    P16(p, 0x28) = (u16)f;
    u32 e = t + (u32)(s32)f * 4;
    P16(p, 0x10) = GV(u16, e);
    P16(p, 0x12) = GV(u16, e + 2);
    if (P16(p, PANEL_FLAGS) & 0x20) {               /* centred */
        P16(p, 0x10) = (u16)(P16(p, 0x10) - (P16(p, 0x08) << 3));
        P16(p, 0x12) = (u16)(P16(p, 0x12) - (P16(p, 0x0A) << 3));
    }
    V0 = 1;
}

/* 80094DB4: weapon icon, every frame: animate, and switch to the icon
 * variant in 0x800A8586 when told to. */
void n_hud_IconUpdate(void) {
    A0 = 0x800A61A0u;
    n_hud_Animate();
    if (!g_iconChanged) return;
    u32 p = g_curPanel;
    u32 b = GV(u8, 0x800A8586);
    const u32 t = 0x800A61A8u;
    P16(p, 0x26) = (u16)b;
    P16(p, 0x04) = (u16)(P16(p, 0x22) + GV(u16, t + (b + 1) * 4));
    g_iconChanged = 0;
    P16(p, 0x06) = (u16)(P16(p, 0x24) + GV(u16, t + (b + 1) * 4 + 2));
}

/* 80095CDC: free the bullet row's panels. */
void n_hud_FreeBullets(void) {
    for (int i = 0; i < 0x11; i++) {
        s16 k = g_hudPanelsBullet[i];
        if (k == -1) continue;
        P16(panel_addr(k), PANEL_FLAGS) = 1;
        g_hudPanelsBullet[i] = -1;
    }
}

/* 80097B48: a panel for one round in the bullet row; its index or -1. */
void n_hud_NewBullet(void) {
    n_hud_AllocPanel();
    s16 i = (s16)V0;
    if (i == -1) { V0 = (u32)-1; return; }
    u32 p = panel_addr(i);
    P16(p, PANEL_FLAGS) |= 0x304;
    A0 = p;
    A1 = 0x800A6154u;
    n_hud_SetSprite();
    u32 t = BULLET_LOOKS + (u32)(s32)GV(s16, g_currentWeaponAddr) * 8;
    P16(p, 0x04) = (u16)(P16(p, 0x04) + GV(u16, t));
    P16(p, 0x06) = (u16)(P16(p, 0x06) + GV(u16, t + 2));
    P16(p, 0x08) = GV(u16, t + 4);
    P16(p, 0x18) = 0;
    P16(p, 0x0A) = GV(u16, t + 6);
    V0 = (u32)(s32)i;
}

/* 80095B90 (x, y, rounds): lay out the bullet row, one panel per round.
 * The grenade and rocket launchers' row starts further right. */
void n_hud_BulletRow(void) {
    s32 x = (s32)A0, y = (s32)A1;
    u16 n = (u16)A2;
    u16 id = GV(u16, g_currentWeaponAddr);
    if ((u16)(id - 7) < 2) x = 0x98;
    g_hudRounds = (s16)n;
    u16 size = GV(u16, BULLET_LOOKS + (((u32)(s32)(s16)id << 3) | 4));
    GV(u16, 0x800C39E6) = 0;
    GV(u16, 0x800C39C2) = n;
    GV(u16, 0x800C39E8) = size;
    if ((s16)n <= 0) return;
    for (s16 i = 0; i < g_hudRounds; i++) {
        n_hud_NewBullet();
        s16 k = (s16)V0;
        g_hudPanelsBullet[i] = k;
        u32 p = PANELS + (u32)((s32)k * PANEL_SIZE);
        P16(p, 0x12) = (u16)((y - (P16(p, 0x0A) >> 1) - 0x78) << 4);
        P16(p, 0x10) = (u16)((x + (s32)i * P16(p, 0x08) - 0xA0) << 4);
    }
}

/* 800956F8: the magazine is empty: start a reload if there's ammo. The
 * new magazine's rounds go in g_hudRounds; they come from the weapon's
 * spare (grenade launcher) or an ammo box in the inventory (an emptied box
 * is removed). Returns 1 if a reload starts; without ammo sets "out of
 * ammo" (D_800AFCB0 & 8). */
void n_weapon_StartReload(void) {
    u32 w = g_currentWeaponAddr;
    V0 = 0;
    if (GV(s16, w + 2) != 0 || g_reloadBlock != 0) return;
    GV(u16, 0x800C39C2) = 0;
    s16 spare = GV(s16, w + 6);
    if (spare != 0) {
        u16 mag = GV(u16, WEAPON_TABLE + (u32)(s32)GV(s16, w) * 8 + 4);
        if (spare < 0) g_hudRounds = (s16)mag;                        /* unlimited */
        else if (!((s16)mag < spare)) { g_hudRounds = spare; GV(u16, w + 6) = 0; }
        else { g_hudRounds = (s16)mag; GV(u16, w + 6) = (u16)(GV(u16, w + 6) - mag); }
        V0 = 1;
        return;
    }
    if (GV(s16, w) == 8) { D_800AFCB0 |= 8; return; }
    u16 ammo = GV(u16, w + 4);
    for (s16 i = 0; i < GV(s16, g_inventory + 0x40); i++) {
        u32 e = ITEMS + (u32)GV(u8, g_inventory + (u32)(s32)i) * 8;
        if (GV(s16, e) != (s16)ammo) continue;
        s16 cnt = GV(s16, e + 2);
        u16 mag = GV(u16, WEAPON_TABLE + (u32)(s32)GV(s16, g_currentWeaponAddr) * 8 + 4);
        if (cnt < 0) g_hudRounds = (s16)mag;
        else if (!((s16)mag < cnt)) {                                  /* the box runs out */
            g_hudRounds = cnt;
            GCALL4(F_REMOVE_ITEM, (s32)i, (u16)cnt, mag, e);
        } else { g_hudRounds = (s16)mag; GV(u16, e + 2) = (u16)(GV(u16, e + 2) - mag); }
        V0 = 1;
        return;
    }
    D_800AFCB0 |= 8;
    V0 = 0;
}

/* 80095480: bullet row, every frame: follow ammo / weapon changes, run
 * the weapon view, and when the magazine is empty start a reload (handing
 * over to the weapon control with the weapon's reload handler). */
void n_hud_BulletsUpdate(void) {
    u32 p = g_curPanel;
    GV(u8, 0x800C39FC) = 0;
    u32 w = g_currentWeaponAddr;
    if ((s16)P16(p, 0x2A) != GV(s16, w + 4)) {
        P16(p, 0x2A) = GV(u16, w + 4);
        GCALL1(F_AMMO_IMAGE, (s32)GV(s16, w + 4));
    }
    if (!(P16(g_curPanel, PANEL_FLAGS) & 4)) return;
    A0 = 0x800A6198u;
    n_hud_Animate();
    w = g_currentWeaponAddr;
    p = g_curPanel;
    s16 id = GV(s16, w);
    if (id != (s16)P16(p, 0x26)) {                                   /* weapon changed */
        P16(p, 0x26) = (u16)id;
        g_weaponViewOn = GV(u32, 0x800A62A0 + (u32)(s32)id * 4);
        g_weaponAction = GV(u32, 0x800A62E8 + (u32)(s32)id * 4);
        n_hud_FreeBullets();
        A0 = 0x62; A1 = 0xD4; A2 = (u32)(s32)GV(s16, g_currentWeaponAddr + 2);
        n_hud_BulletRow();
    }
    u16 held = GV(u16, g_currentWeaponAddr);
    g_panelSave = g_curPanel;
    call_ptr(g_weaponViewOn);
    if (D_800B3D38) return;
    g_curPanel = g_panelSave;
    if (D_800AFCB0 & 8) return;
    n_weapon_StartReload();
    if (!(V0 & 0xFFFF)) return;
    P16(g_curPanel, PANEL_FLAGS) &= 0xFFFB;
    u32 ctl = PANELS + (u32)((s32)g_hudWeaponCtl * PANEL_SIZE);
    g_curPanel = ctl;
    P16(ctl, PANEL_FLAGS) |= 0x204;
    u32 reload = GV(u32, 0x800A62C4 + (u32)(s32)(s16)held * 4);
    D_800AFCB0 |= 4;
    call_ptr(reload);
}

/* 80093A48 (frame, frames, offsets): set the current panel to frame of an
 * animation: frames are {s16 offset entry (-1 ends), u16 duration, s16 dx,
 * s16 dy}; offset entries {u, v, x, y, w, h} are relative to the panel's
 * base (+0x22..+0x28). Returns -1 at the end, else 0. */
void n_hud_SetFrameB(void) {
    u32 e = A1 + (u32)(((s32)(A0 << 18)) >> 15);
    s16 k = GV(s16, e);
    if (k == -1) { V0 = (u32)-1; return; }
    u32 o = A2 + (u32)(((s32)((u32)(k * 3) << 17)) >> 15);
    u32 p = g_curPanel;
    P16(p, 0x1A) = GV(u16, e + 2);
    P16(p, 0x04) = (u16)(P16(p, 0x22) + GV(u16, o));
    P16(p, 0x06) = (u16)(P16(p, 0x24) + GV(u16, o + 2));
    P16(p, 0x10) = (u16)((P16(p, 0x26) + GV(u16, o + 4) - 0xA0) << 4);
    P16(p, 0x12) = (u16)((P16(p, 0x28) + GV(u16, o + 6) - 0x78) << 4);
    P16(p, 0x10) = (u16)(P16(p, 0x10) + (GV(u16, e + 4) << 4));
    P16(p, 0x12) = (u16)(P16(p, 0x12) + (GV(u16, e + 6) << 4));
    u32 w = GV(u16, o + 8), h = GV(u16, o + 10);
    P16(p, 0x08) = (u16)w;
    P16(p, 0x0A) = (u16)h;
    if (P16(p, PANEL_FLAGS) & 8) {                 /* scaled */
        w = (u32)(((s32)(s16)w * P16(p, 0x34)) >> 8);
        P16(p, 0x14) = (u16)w;
        h = (u32)(((s32)(s16)h * P16(p, 0x34)) >> 8);
        P16(p, 0x16) = (u16)h;
    }
    p = g_curPanel;
    if (P16(p, PANEL_FLAGS) & 0x20) {              /* centred */
        P16(p, 0x10) = (u16)(P16(p, 0x10) - (s16)w * 8);
        P16(p, 0x12) = (u16)(P16(p, 0x12) - (s16)h * 8);
    }
    V0 = 0;
}

/* The animation steppers: count down the frame time; then show the next
 * frame with one of the frame setters (A 800936E0, B 80093A48, C
 * 80093BBC) from the {frames, offsets} pair at +0x38; at the end loop,
 * or free the panel. +0x20 = -1 marks "ended". */
static int anim_next(u32 setter) {
    u32 p = g_curPanel;
    P16(p, 0x1A) = (u16)(P16(p, 0x1A) - 1);
    if ((s16)P16(p, 0x1A) > 0) return 0;
    u32 a = P32(p, PANEL_DRAWPTR);
    u16 f = (u16)(P16(p, 0x1E) + 1);
    P16(p, 0x1E) = f;
    A0 = (u32)(s32)(s16)f; A1 = GV(u32, a); A2 = GV(u32, a + 4);
    if (setter == 0x80093A48u) n_hud_SetFrameB();
    else { u32 a0 = A0, a1 = A1, a2 = A2; GCALL3(setter, a0, a1, a2); }
    return (s16)V0 == -1;
}
static void anim_loop(u32 setter) {
    if (!anim_next(setter)) return;
    P16(g_curPanel, 0x1E) = 0;
    P16(g_curPanel, 0x20) = 0xFFFF;
}
static void anim_once(u32 setter) {
    if (!anim_next(setter)) return;
    P16(g_curPanel, PANEL_FLAGS) = 1;
    P16(g_curPanel, 0x20) = 0xFFFF;
}
void n_hud_AnimLoopA(void) { anim_loop(0x800936E0u); }      /* 800940C4 */
void n_hud_AnimOnceA(void) { anim_once(0x800936E0u); }      /* 8009414C */
void n_hud_AnimLoopB(void) { anim_loop(0x80093A48u); }      /* 800941D4 */
void n_hud_AnimOnceB(void) { anim_once(0x80093A48u); }      /* 8009425C */

/* 80094454: the weapon control's timer list (+0x38: {u16 frames, u16 y}
 * pairs, -1 loops): step it. */
void n_hud_CtlTimer(void) {
    u32 p = g_curPanel;
    P16(p, 0x1A) = (u16)(P16(p, 0x1A) - 1);
    if ((s16)P16(p, 0x1A) > 0) return;
    u32 t = P32(p, PANEL_DRAWPTR);
    u16 f = (u16)(P16(p, 0x1E) + 1);
    P16(p, 0x1E) = f;
    s16 v = GV(s16, t + (u32)((s32)(s16)f * 4));
    if (v == -1) {
        f = 0;
        P16(p, 0x1E) = 0;
        P16(p, 0x20) = (u16)v;
    }
    u32 e = t + (u32)((s32)(s16)f * 4);
    p = g_curPanel;
    P16(p, 0x1A) = GV(u16, e);
    P16(p, 0x0E) = (u16)(P16(p, 0x22) + GV(u16, e + 2));
}

/* 800944F0: free every panel except those flagged 0x200 / 0x400. */
void n_hud_FreeAll(void) {
    for (int i = 0; i < PANEL_COUNT; i++) {
        u32 p = panel_addr(i);
        if (!(P16(p, PANEL_FLAGS) & 0x601)) P16(p, PANEL_FLAGS) = 1;
    }
}

/* 8009453C: free the panels flagged 0x400 (pause screen ones) and clear
 * the pause screen state. */
void n_hud_FreePaused(void) {
    g_curPanel = PANELS;
    for (int i = 0; i < PANEL_COUNT; i++) {
        if (P16(g_curPanel, PANEL_FLAGS) & 0x400) P16(g_curPanel, PANEL_FLAGS) = 1;
        g_curPanel += PANEL_SIZE;
    }
    GV(u32, 0x800C3A14) = 0;
    GV(u32, 0x800C3A10) = 0;
    GV(u8, 0x800C3A98 + 0x2C4) = 0;
    GV(u8, 0x800C3A98 + 0x1D8) = 0;
}
