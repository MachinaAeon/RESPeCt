/* The weapon view's HUD side (TEST overlay): what the bullet row does
 * when a shot is fired (a round leaves the row, the row slides along, a
 * spent casing flies out and clinks), per weapon type, and the reload
 * animations run by the weapon control panel.
 *
 * Every weapon id has two handlers (tables at 0x800A62A0 and 0x800A62E8):
 * "view" runs every frame from the bullet row's callback, "action" runs
 * every frame from the weapon control while a reload is going on and
 * returns 1 when it's over. A shot sets D_800AFCB0 bit 0, a dry fire
 * bit 1.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "hud.h"

#include <string.h>

void n_view_Handgun(void);
void n_hud_EjectCasing(void);
void n_hud_CasingFly(void);
void n_hud_CasingLand(void);
void n_hud_NewCasing(void);
void n_hud_CasingStep(void);
void n_view_Shotgun(void);
void n_hud_ShellOut(void);
void n_hud_ShellDrop(void);
void n_hud_ShellBounce(void);
void n_view_Magnum(void);
void n_view_MagnumSpent(void);
void n_hud_MagnumRoundStep(void);
void n_hud_EjectMagnumCasing(void);
void n_hud_MagnumCasingFly(void);
void n_view_Launcher(void);
void n_view_Rocket(void);
void n_action_Handgun(void);
void n_action_HandgunLoad(void);
void n_action_Shotgun(void);
void n_action_ShotgunLoad(void);
void n_action_MagnumLoad(void);
void n_action_Magnum(void);
void n_action_MagnumDone(void);
void n_action_Launcher(void);
void n_action_LauncherRow(void);
void n_action_LauncherSlide(void);
void n_action_LauncherDone(void);
void n_action_Rocket(void);
void n_reload_Handgun(void);
void n_reload_Magnum(void);
void n_reload_Shotgun(void);
void n_reload_Launcher(void);
void n_reload_Rocket(void);

#define F_RAND          0x80011570u
#define F_SHOT_SOUND    0x8008E368u           /* the weapon's shot */
#define CB_CASING_FLY   0x80095FD0u
#define CB_CASING_LAND  0x8009601Cu
#define CASING_ANIM     0x800A64C0u
#define g_hudScroll     GV(s16, 0x800C39E6)   /* how far the row still has to slide (12.4) */
#define g_hudRoundW     GV(s16, 0x800C39E8)   /* width of one round in the row */
#define g_clinked       GV(u8, 0x800C39FC)    /* a casing clinked this frame */
#define g_hudRowCount   GV(s16, 0x800C39C2)   /* rounds in the row when the reload started */
#define g_hudStep       GV(s16, 0x800C39EA)   /* shotgun: slide per frame */
#define g_hudTimer      GV(s16, 0x800C39FE)
#define g_hudCount      GV(s16, 0x800C39F0)   /* reload: frames of the magazine sliding in */
#define g_viewState     g_reloadBlock         /* 0x800C39F2: shotgun pump 1-2, magnum 4 */
#define F_PANEL_ANIM    0x80093A48u           /* (frame, frames, offsets): set sprite frame; -1 at the end */
#define F_ANIM_STEP     0x800941D4u           /* step the current panel's animation */
#define F_ANIM_STEP2    0x8009425C
#define F_ANIM_START    0x8009484C            /* (animation) */

static u32 rnd(void) { return GCALL0(F_RAND); }

/* A shot from a gun with a bullet row: the first round's panel goes, the
 * rest move up one place and the row starts sliding left by a round. */
static void row_shot(void) {
    u32 p = panel_addr(g_hudPanelsBullet[0]);
    P16(p, PANEL_FLAGS) = 1;
    g_curPanel = p;
    g_hudRounds = (s16)(g_hudRounds - 1);
    s16 i = 0;
    for (; i < g_hudRounds; i++) g_hudPanelsBullet[i] = g_hudPanelsBullet[i + 1];
    g_hudPanelsBullet[i] = -1;
    g_hudScroll = (s16)(g_hudScroll + g_hudRoundW);
}

/* The row slides: a third of the way (at least a third of a round's
 * width) each frame. */
static void row_slide(void) {
    s16 s = g_hudScroll;
    if (s == 0) return;
    s16 w = g_hudRoundW;
    s32 step = (w < s ? s : w) / 3;
    s16 left;
    if ((s32)s - (s16)step > 0) left = (s16)((u16)s - step);
    else { step = (u16)s; left = 0; }
    g_hudScroll = left;
    u16 dx = (u16)(step << 4);
    for (s16 i = 0; i < g_hudRounds; i++) {
        u32 p = panel_addr(g_hudPanelsBullet[i]);
        P16(p, 0x10) = (u16)(P16(p, 0x10) - dx);
    }
}

/* 80095D44: handguns' view handler. */
void n_view_Handgun(void) {
    if (D_800AFCB0 & 1) {                         /* fired */
        D_800AFCB0 &= 0xFFFE;
        GCALL0(F_SHOT_SOUND);
        GCALL2(0x80095F80u, 0x62, 0xCC);          /* a casing flies out */
        row_shot();
    }
    if (D_800AFCB0 & 2) {                         /* dry fire: click */
        D_800AFCB0 &= 0xFFFD;
        GCALL4(F_SOUND_CUE, 1, 0, 0x7F, 0x7F);
    }
    row_slide();
}

/* 80095F80 (x, y): a spent casing flies out from there. */
void n_hud_EjectCasing(void) {
    A0 = (u32)(s32)(s16)A0;
    A1 = (u32)(s32)(s16)A1;
    n_hud_NewCasing();
    u32 p = g_curPanel;
    P32(p, PANEL_DRAWPTR) = CASING_ANIM;
    P32(p, PANEL_UPDATE) = CB_CASING_FLY;
    n_hud_CasingFly();
}

/* 80095FD0: casing in the air; once down, it waits 12 frames to clink. */
void n_hud_CasingFly(void) {
    n_hud_CasingStep();
    if (!(V0 & 0xFFFF)) return;
    u32 p = g_curPanel;
    P16(p, 0x1A) = 0xC;
    P32(p, PANEL_UPDATE) = CB_CASING_LAND;
    P32(p, PANEL_DRAWPTR) = 1;                    /* the clink's sound */
}

/* 8009601C: casing down: clink (one per frame at most) and go. */
void n_hud_CasingLand(void) {
    if (g_inputLocked) { P16(g_curPanel, PANEL_FLAGS) = 1; return; }
    u32 p = g_curPanel;
    P16(p, 0x1A) = (u16)(P16(p, 0x1A) - 1);
    if ((s16)P16(p, 0x1A) >= 0) return;
    if (!g_clinked) {
        g_clinked = 1;
        GCALL4(F_SOUND_CUE, 1, P32(p, PANEL_DRAWPTR), 0x7F, 0x7F);
    }
    P16(g_curPanel, PANEL_FLAGS) = 1;
}

/* 800960B0 (x, y): a casing panel at x, y with a random throw: sideways
 * speed (+0x2E), upward speed (+0x30), spin towards / away (+0x28). */
void n_hud_NewCasing(void) {
    s32 x = (s32)A0, y = (s32)A1;
    n_hud_AllocPanel();
    s16 i = (s16)V0;
    if (i == -1) return;
    u32 p = panel_addr(i);
    g_curPanel = p;
    P16(p, PANEL_FLAGS) |= 0x20C;
    A0 = p;
    A1 = 0x800A6154u;
    n_hud_SetSprite();
    p = g_curPanel;
    P16(p, 0x10) = (u16)((x - 0xA0) << 4);
    P16(p, 0x18) = 0;
    P16(p, 0x12) = (u16)((y - 0x78) << 4);
    s32 r = (s32)(rnd() & 0x7FFFFFFF);
    P16(g_curPanel, 0x2E) = (u16)(r % 320 + 0x20);
    r = (s32)(rnd() & 0x7FFFFFFF);
    p = g_curPanel;
    P16(p, 0x26) = 0x300;                         /* depth */
    P16(p, 0x22) = P16(p, 0x04);
    P16(p, 0x24) = P16(p, 0x06);
    P16(p, 0x30) = (u16)(-0x80 - r % 160);
    s16 spin = (rnd() & 1) ? (s16)(rnd() & 0x3F) : (s16)-(s32)(rnd() & 0xF);
    P16(g_curPanel, 0x28) = (u16)spin;
    p = g_curPanel;
    P16(p, 0x34) = 0x100;
    P16(p, 0x1A) = 0;
}

/* 80096250: move a casing one frame (animation frames from the list at
 * +0x38, gravity, perspective scale from depth); 1 once it's on the
 * ground. */
void n_hud_CasingStep(void) {
    if (g_inputLocked) { P16(g_curPanel, PANEL_FLAGS) = 1; V0 = 1; return; }
    u32 p = g_curPanel;
    P16(p, 0x1A) = (u16)(P16(p, 0x1A) - 1);
    if ((s16)P16(p, 0x1A) <= 0) {                 /* next animation frame */
        u32 a = P32(p, PANEL_DRAWPTR);
        if (GV(s16, a) == -1) a -= (u32)((s32)GV(s16, a + 2) * 2);   /* loop */
        P16(p, 0x1A) = GV(u16, a);
        P16(p, 0x04) = (u16)(P16(p, 0x22) + GV(u16, a + 2));
        P16(p, 0x06) = (u16)(P16(p, 0x24) + GV(u16, a + 4));
        P16(p, 0x08) = GV(u16, a + 6);
        P32(p, PANEL_DRAWPTR) = a + 10;
        P16(p, 0x0A) = GV(u16, a + 8);
    }
    p = g_curPanel;
    u16 z = (u16)(P16(p, 0x26) + P16(p, 0x28));
    s32 scale = mips_div(0x30000, (s16)z);
    s32 sw = (s32)P16(p, 0x08) * (s32)P16(p, 0x34);
    u16 vx = P16(p, 0x2E);
    P16(p, 0x10) = (u16)(P16(p, 0x10) + vx);
    s32 sh = (s32)P16(p, 0x0A) * (s32)P16(p, 0x34);
    P16(p, 0x26) = z;
    P16(p, 0x2E) = (u16)(vx - ((s16)vx >> 4));    /* air drag */
    u16 vy = P16(p, 0x30);
    P16(p, 0x12) = (u16)(P16(p, 0x12) + vy);
    P16(p, 0x30) = (u16)(vy + 0x20);              /* gravity */
    P16(p, 0x14) = (u16)(sw >> 8);
    P16(p, 0x16) = (u16)(sh >> 8);
    P16(p, 0x34) = (u16)scale;
    if ((u16)scale >= 0x200) P16(p, 0x34) = 0x200;
    if (P16(g_curPanel, 0x34) < 0x34) P16(g_curPanel, 0x34) = 0x33;
    V0 = (s16)P16(g_curPanel, 0x12) >= 0x800;
}

static void sound(u32 a1) { GCALL4(F_SOUND_CUE, 1, a1, 0x7F, 0x7F); }

/* 80096414: shotguns' view handler. A shot: the pump sound, the spent
 * shell's sprite replaces the first round (80096840) and the row slides
 * a round's width in quarter steps; once it has, the round is gone (the
 * last one: the whole row falls away). */
void n_view_Shotgun(void) {
    if (D_800AFCB0 & 1) {
        D_800AFCB0 &= 0xFFFE;
        sound(4);
        g_viewState = 1;
        GCALL2(0x80096840u, 0x62, 0xCC);
        g_hudRounds = (s16)(g_hudRounds - 1);
        g_hudStep = (s16)(g_hudRoundW / 4);
        g_hudScroll = 0;
    }
    if (D_800AFCB0 & 2) {
        D_800AFCB0 &= 0xFFF9;
        sound(0);
    }
    if (g_viewState == 1) {                       /* the shell's animation */
        u32 p = panel_addr(g_hudPanelsBullet[0]);
        g_curPanel = p;
        GCALL0(F_ANIM_STEP);
        p = g_curPanel;
        if ((s16)P16(p, 0x20) != -1) return;
        s32 t = (s32)(u16)g_hudRoundW * (s32)(u16)g_hudRowCount;
        P16(p, 0x08) = 0;
        g_viewState = 2;
        P16(p, 0x10) = (u16)(P16(p, 0x10) + (t << 4));
        return;
    }
    if (g_viewState != 2) return;
    s32 step = g_hudStep;                         /* the row slides */
    if (step + g_hudScroll < g_hudRoundW) g_hudScroll = (s16)((u16)g_hudScroll + step);
    else { step = (s32)(u16)g_hudRoundW - (s32)(u16)g_hudScroll; g_hudScroll = 0; }
    u32 dx = (u32)step << 4;
    u32 p = panel_addr(g_hudPanelsBullet[0]);
    g_curPanel = p;
    P16(p, 0x08) = (u16)(P16(p, 0x08) + step);
    P16(p, 0x10) = (u16)(P16(p, 0x10) - dx);
    for (s16 i = 1; i < g_hudRowCount; i++) {
        u32 q = panel_addr(g_hudPanelsBullet[i]);
        P16(q, 0x10) = (u16)(P16(q, 0x10) - dx);
    }
    if (g_hudScroll != 0) return;
    if (GV(s16, g_currentWeaponAddr + 2) == 0) {   /* empty: the shells drop */
        for (s16 i = 0; i < g_hudRowCount; i++) {
            u32 q = panel_addr(g_hudPanelsBullet[i]);
            g_curPanel = q;
            P16(q, PANEL_FLAGS) = 0x206;
            u32 r = rnd();
            q = g_curPanel;
            P16(q, 0x1A) = (u16)((r & 0xF) + 0x1E);
            P32(q, PANEL_UPDATE) = 0x800968C4u;
        }
        for (s16 i = 0; i < 0x11; i++) g_hudPanelsBullet[i] = -1;
        sound(2);
        g_viewState = 0;
        g_clinked = 0;
        return;
    }
    g_viewState = 0;
    sound(3);
    u16 first = (u16)g_hudPanelsBullet[0];        /* the spent one goes to the end */
    s16 i = 0;
    for (; i < g_hudRowCount - 1; i++) g_hudPanelsBullet[i] = g_hudPanelsBullet[i + 1];
    g_hudPanelsBullet[i] = (s16)first;
}

/* 80096840 (x, y): the first round's panel becomes the spent shell. */
void n_hud_ShellOut(void) {
    u16 x = (u16)A0, y = (u16)A1;
    u32 p = panel_addr(g_hudPanelsBullet[0]);
    g_curPanel = p;
    A0 = p;
    A1 = 0x800A6154u;
    n_hud_SetSprite();
    p = g_curPanel;
    P16(p, 0x18) = 0;
    P16(p, 0x10) = x;
    P16(p, 0x12) = y;
    GCALL1(F_ANIM_START, 0x800A6340u);
}

/* 800968C4: a dropped shell waits, then tumbles (one of two animations). */
void n_hud_ShellDrop(void) {
    u32 p = g_curPanel;
    P16(p, 0x1A) = (u16)(P16(p, 0x1A) - 1);
    if ((s16)P16(p, 0x1A) > 0) return;
    P16(p, 0x1E) = 0;
    P16(p, 0x26) = (u16)(((s16)P16(p, 0x10) >> 4) + 0xA2);
    P16(p, 0x28) = (u16)(((s16)P16(p, 0x12) >> 4) + 0x7E);
    u32 a1, a2;
    if (rnd() & 1) { a1 = 0x800A6348u; a2 = 0x800A63B4u; P32(g_curPanel, PANEL_DRAWPTR) = 0x800A6400u; }
    else { a1 = 0x800A6408u; a2 = 0x800A646Cu; P32(g_curPanel, PANEL_DRAWPTR) = 0x800A64B8u; }
    GCALL3(F_PANEL_ANIM, 0, a1, a2);
    p = g_curPanel;
    P32(p, PANEL_UPDATE) = 0x800969A4u;
    P16(p, 0x1A) = (u16)(P16(p, 0x1A) + 1);
}

/* 800969A4: tumbling shell; at the end of its animation it clinks. */
void n_hud_ShellBounce(void) {
    GCALL0(F_ANIM_STEP2);
    u32 p = g_curPanel;
    if (!(P16(p, PANEL_FLAGS) & 1)) return;
    P16(p, PANEL_FLAGS) = 2;
    P16(p, 0x1A) = 6;
    P32(p, PANEL_UPDATE) = CB_CASING_LAND;
    P32(p, PANEL_DRAWPTR) = 6;                    /* the clink's sound */
}

/* 80096A04: the magnum's view handler. A shot: the first round turns into
 * a spent one (sprite from 0x800A6564) that the view handler 80096C04
 * then animates; the row slides in thirds. */
void n_view_Magnum(void) {
    if (D_800AFCB0 & 1) {
        g_viewState = 4;
        D_800AFCB0 &= 0xFFFE;
        sound(4);
        const u32 t = 0x800A6566u;
        u32 p = panel_addr(g_hudPanelsBullet[0]);
        g_curPanel = p;
        P16(p, 0x04) = (u16)(P16(p, 0x04) + GV(u16, t));
        P16(p, 0x06) = (u16)(P16(p, 0x06) + GV(u16, t + 2));
        P16(p, 0x08) = GV(u16, t + 4);
        u16 h = GV(u16, t + 6);
        P32(p, PANEL_DRAWPTR) = 0x800A65B8u;
        g_weaponViewOn = 0x80096C04u;
        P16(p, 0x0A) = h;
    }
    if (D_800AFCB0 & 2) {
        D_800AFCB0 &= 0xFFFD;
        sound(0);
    }
    s16 s = g_hudScroll;
    if (s == 0) return;
    s32 step = g_hudRoundW / 3;
    if ((s32)s - (s16)step < 0) {
        step = (u16)s;
        sound(3);
    }
    g_hudScroll = (s16)((u16)g_hudScroll - step);
    u16 dx = (u16)(step << 4);
    for (s16 i = 0; i < g_hudRounds; i++) {
        u32 p = panel_addr(g_hudPanelsBullet[i]);
        P16(p, 0x10) = (u16)(P16(p, 0x10) - dx);
    }
}

/* 80096C04: magnum, after a shot: the spent round rises out of the row;
 * when it's gone a casing flies out and the row moves up. */
void n_view_MagnumSpent(void) {
    u32 p = panel_addr(g_hudPanelsBullet[0]);
    g_curPanel = p;
    n_hud_MagnumRoundStep();
    if (!(P16(g_curPanel, PANEL_FLAGS) & 1)) return;
    A0 = 0x62; A1 = 0xCC;
    n_hud_EjectMagnumCasing();
    g_hudRounds = (s16)(g_hudRounds - 1);
    s16 i = 0;
    for (; i < g_hudRounds; i++) g_hudPanelsBullet[i] = g_hudPanelsBullet[i + 1];
    g_hudPanelsBullet[i] = -1;
    g_weaponViewOn = 0x80096A04u;
    g_viewState = 0;
    g_hudScroll = (s16)(g_hudScroll + g_hudRoundW);
}

/* 80096D30: move the spent magnum round by the next offset of its list;
 * at the end (-1) it's freed with a sound. */
void n_hud_MagnumRoundStep(void) {
    u32 p = g_curPanel;
    u32 a = P32(p, PANEL_DRAWPTR);
    u16 v = GV(u16, a);
    P32(p, PANEL_DRAWPTR) = a + 2;
    if ((s16)v == -1) {
        P16(p, PANEL_FLAGS) = 1;
        sound(3);
        return;
    }
    P16(p, 0x12) = (u16)(P16(p, 0x12) + (v << 4));
}

/* 80096DA8 (x, y): the magnum's casing (its own animation frames). */
void n_hud_EjectMagnumCasing(void) {
    A0 = (u32)(s32)(s16)A0;
    A1 = (u32)(s32)(s16)A1;
    n_hud_NewCasing();
    u32 p = g_curPanel;
    P32(p, PANEL_DRAWPTR) = 0x800A6564u;
    P32(p, PANEL_UPDATE) = 0x80096DF8u;
    n_hud_MagnumCasingFly();
}

/* 80096DF8: as 80095FD0. */
void n_hud_MagnumCasingFly(void) {
    n_hud_CasingStep();
    if (!(V0 & 0xFFFF)) return;
    u32 p = g_curPanel;
    P16(p, 0x1A) = 0xC;
    P32(p, PANEL_UPDATE) = CB_CASING_LAND;
    P32(p, PANEL_DRAWPTR) = 1;
}

/* The one-round launchers: a shot removes the round's panel. */
static void view_single(void) {
    if (D_800AFCB0 & 1) {
        D_800AFCB0 &= 0xFFFE;
        sound(3);
        P16(panel_addr(g_hudPanelsBullet[0]), PANEL_FLAGS) = 1;
        g_hudPanelsBullet[0] = -1;
    }
    if (D_800AFCB0 & 2) {
        D_800AFCB0 &= 0xFFFD;
        sound(0);
    }
}

/* 80096E44: the grenade launcher's view handler. */
void n_view_Launcher(void) { view_single(); }

/* 80096EF4: the rocket launcher's view handler. */
void n_view_Rocket(void) { view_single(); }

/* 80096FA4: handguns' reload, first part: a wait with the magazine-out
 * sound at its end, then the row slides out (g_hudScroll counts it);
 * then 80097028 takes over. Returns 0 (not done). */
void n_action_Handgun(void) {
    g_hudTimer = (s16)(g_hudTimer - 1);
    if (g_hudTimer < 0) {
        g_hudScroll = (s16)(g_hudScroll - 1);
        if (g_hudScroll == 0) g_weaponAction = 0x80097028u;
    } else if (g_hudTimer == 0) sound(2);
    V0 = 0;
}

/* A new round's panel at the given place in the row (reloads). */
static u32 new_round(s16 slot) {
    n_hud_NewBullet();
    u32 idx = V0;
    g_hudPanelsBullet[slot] = (s16)idx;
    return panel_addr((s16)idx);
}

/* 80097028: handguns' reload, second part: the new magazine's rounds
 * slide into the row from the left (over g_hudCount frames), placed
 * after the rounds still there; done (1) when they're in. */
void n_action_HandgunLoad(void) {
    u16 c = (u16)(g_hudCount - 1);
    g_hudCount = (s16)c;
    s32 add;                                      /* rounds still to add to the row */
    if (c == 0) {
        g_hudScroll = (s16)((u32)(u16)g_hudRounds * (u16)g_hudRoundW);
        add = (s32)(u16)g_hudRounds - (u16)g_hudRowCount;
    } else {
        u32 sc = (u32)(u16)g_hudScroll + (u16)g_hudStep;
        s32 q = mips_div((s16)sc + g_hudRoundW - 1, g_hudRoundW);   /* rounds shown so far */
        g_hudScroll = (s16)sc;
        add = q - (u16)g_hudRowCount;
    }
    s16 n = g_hudRowCount;                         /* make room at the front */
    if (n > 0) {
        s32 t = (s16)add - 1;
        u32 k = (u32)n;
        do {
            s16 kk = (s16)k;
            k--;
            g_hudPanelsBullet[kk + t] = g_hudPanelsBullet[kk - 1];
        } while ((s16)k > 0);
    }
    for (u32 s0 = (u32)(add - 1); (s16)s0 >= 0; s0--) {
        g_hudRowCount = (s16)((u16)g_hudRowCount + 1);
        u32 p = new_round((s16)s0);
        P16(p, 0x10) = (u16)-0x3E0;
        u16 w = P16(p, 0x08), u = P16(p, 0x04), h = P16(p, 0x0A);
        P16(p, 0x08) = 0;
        P16(p, 0x22) = u;
        P16(p, 0x24) = w;
        P16(p, 0x12) = (u16)((0x5C - (h >> 1)) << 4);
    }
    u32 x = (u16)g_hudScroll;                      /* clip the rounds against the left edge */
    for (u32 k = (u32)((u16)g_hudRowCount - 1); (s16)k >= 0; k--) {
        u32 p = panel_addr(g_hudPanelsBullet[(s16)k]);
        x -= (u16)g_hudRoundW;
        if ((s16)x < 0) {
            P16(p, 0x04) = (u16)-x;
            P16(p, 0x10) = (u16)-0x3E0;
            P16(p, 0x08) = (u16)(P16(p, 0x24) + x);
        } else {
            P16(p, 0x10) = (u16)((x - 0x3E) << 4);
            P16(p, 0x04) = P16(p, 0x22);
            P16(p, 0x08) = P16(p, 0x24);
        }
    }
    V0 = 0;
    if (g_hudCount != 0) return;
    sound(3);
    g_weaponAction = 0x80096FA4u;
    g_hudScroll = 0;
    V0 = 1;
}

/* 800972D0: shotguns' reload: a wait, then 80097308. */
void n_action_Shotgun(void) {
    g_hudScroll = (s16)(g_hudScroll - 1);
    if (g_hudScroll == 0) g_weaponAction = 0x80097308u;
    V0 = 0;
}

/* 80097308: shotguns load one shell at a time: each drops into the row
 * from above (slowing down), with a sound when it lands; done (1) when
 * the row holds the new magazine count. */
void n_action_ShotgunLoad(void) {
    if (g_hudScroll == 0) {
        s16 t = g_hudTimer;
        g_hudTimer = (s16)(t - 1);
        if (t > 0) { V0 = 0; return; }
        s16 n = g_hudRowCount;
        g_hudStep = 0x140;
        if (n == g_hudRounds) {
            sound(2);
            g_weaponAction = 0x800972D0u;
            V0 = 1;
            return;
        }
        g_hudRowCount = (s16)((u16)g_hudRowCount + 1);
        u32 p = new_round(n);
        s32 x = (s32)n * P16(p, 0x08);
        g_curPanel = p;
        P16(p, 0x12) = (u16)((0x98 - (P16(p, 0x0A) >> 1)) << 4);
        P16(p, 0x10) = (u16)((x - 0x3E) << 4);
    }
    s32 step = g_hudStep;
    u32 p = panel_addr(g_hudPanelsBullet[(s16)(g_hudRowCount - 1)]);
    u16 y = (u16)g_hudScroll;
    g_curPanel = p;
    if (step + g_hudScroll >= 0x3C0) {            /* landed */
        step = 0x3C0 - y;
        g_hudScroll = 0;
        GCALL4(F_SOUND_CUE, 1, 1, 0x7F, 0x7F);
        g_hudTimer = 6;
    } else g_hudScroll = (s16)(y + step);
    p = g_curPanel;
    P16(p, 0x12) = (u16)(P16(p, 0x12) - step);
    g_hudStep = (s16)(step - ((s16)step >> 3));
    V0 = 0;
}

/* 800974E4: the magnum's speed loader: the rounds drop in, four (the
 * first time) then the rest; 80097798 finishes. */
void n_action_MagnumLoad(void) {
    if (g_hudScroll == 0) {
        s16 n = g_hudRowCount;
        g_hudStep = 0xC0;
        s32 first = (n != 0) * 4;
        if (n == g_hudRounds) {
            g_hudScroll = 0xF;
            g_weaponAction = 0x80097798u;
            V0 = 0;
            return;
        }
        if (g_hudRounds - (s16)first < 4) g_hudRowCount = g_hudRounds;
        else g_hudRowCount = (s16)(first + 4);
        for (s32 i = first; (s16)i < g_hudRowCount; i++) {
            u32 p = new_round((s16)i);
            s32 x = (s32)(s16)i * P16(p, 0x08);
            P16(p, 0x12) = (u16)((0x98 - (P16(p, 0x0A) >> 1)) << 4);
            P16(p, 0x10) = (u16)((x - 0x3E) << 4);
            g_curPanel = p;
        }
    }
    s32 first = (g_hudRowCount >= 5) * 4;
    s32 step = g_hudStep;
    u16 y = (u16)g_hudScroll;
    if (step + g_hudScroll >= 0x3C0) {
        step = 0x3C0 - y;
        sound(2);
        g_hudScroll = 0x1E;
        g_weaponAction = 0x80097760u;
    } else g_hudScroll = (s16)(y + step);
    for (s32 i = first; (s16)i < g_hudRowCount; i++) {
        u32 p = panel_addr(g_hudPanelsBullet[(s16)i]);
        P16(p, 0x12) = (u16)(P16(p, 0x12) - step);
    }
    g_hudStep = (s16)(step - ((s16)step >> 3));
    V0 = 0;
}

/* 80097760: the magnum's reload, a wait between batches. */
void n_action_Magnum(void) {
    g_hudScroll = (s16)(g_hudScroll - 1);
    if (g_hudScroll == 0) g_weaponAction = 0x800974E4u;
    V0 = 0;
}

/* 80097798: the magnum's reload, done after a wait. */
void n_action_MagnumDone(void) {
    g_hudScroll = (s16)(g_hudScroll - 1);
    if (g_hudScroll != 0) { V0 = 0; return; }
    g_weaponAction = 0x80097760u;
    V0 = 1;
}

/* 800977D8: the grenade launcher's reload: a wait, a sound. */
void n_action_Launcher(void) {
    g_hudScroll = (s16)(g_hudScroll - 1);
    if (g_hudScroll == 0) {
        g_weaponAction = 0x80097844u;
        GCALL4(F_SOUND_CUE, 1, 1, 0x7F, 0x7F);
        g_hudScroll = 6;
    }
    V0 = 0;
}

/* 80097844: then the new round's panel, off to the right. */
void n_action_LauncherRow(void) {
    g_hudScroll = (s16)(g_hudScroll - 1);
    if (g_hudScroll == 0) {
        g_weaponAction = 0x800978E0u;
        A0 = 0x62; A1 = 0xD4; A2 = (u32)(s32)g_hudRounds;
        n_hud_BulletRow();
        u32 p = panel_addr(g_hudPanelsBullet[0]);
        P16(p, 0x10) = 0x500;
        P16(p, 0x08) = 0;
        g_hudScroll = 5;
        g_hudStep = 0x119;
    }
    V0 = 0;
}

/* 800978E0: it slides in, growing to full width. */
void n_action_LauncherSlide(void) {
    u16 c = (u16)(g_hudScroll - 1);
    u32 p = panel_addr(g_hudPanelsBullet[0]);
    g_hudScroll = (s16)c;
    if (c == 0) {
        P16(p, 0x10) = (u16)-0x80;
        g_weaponAction = 0x8009798Cu;
        g_hudScroll = 6;
    } else {
        P16(p, 0x10) = (u16)(P16(p, 0x10) - (u16)g_hudStep);
        u16 w = (u16)(P16(p, 0x08) + ((s16)g_hudStep >> 4));
        P16(p, 0x08) = w;
        if (g_hudRoundW < (s32)w) P16(p, 0x08) = (u16)g_hudRoundW;
    }
    V0 = 0;
}

/* 8009798C: then a wait and the closing sound; done. */
void n_action_LauncherDone(void) {
    g_hudScroll = (s16)(g_hudScroll - 1);
    if (g_hudScroll != 0) { V0 = 0; return; }
    g_hudScroll = 0;
    g_weaponAction = 0x800977D8u;
    sound(2);
    V0 = 1;
}

/* 800979F4: the rocket launcher's reload: a wait, then the rocket slides
 * in from the right (shown as far as the window allows). */
void n_action_Rocket(void) {
    s16 t = g_hudTimer;
    if (t > 0) {
        g_hudTimer = (s16)(t - 1);
        V0 = 0;
        if ((u16)(t - 1) != 0) return;
        n_hud_NewBullet();
        u32 idx = V0;
        u32 p = panel_addr((s16)idx);
        g_hudPanelsBullet[0] = (s16)idx;
        u16 h = P16(p, 0x0A), w = P16(p, 0x08);
        P16(p, 0x10) = 0x500;
        P16(p, 0x08) = 0;
        P16(p, 0x12) = (u16)((0x5C - (h >> 1)) << 4);
        P16(p, 0x22) = w;
        g_hudCount = 5;
        g_hudStep = 0x119;
        V0 = 0;
        return;
    }
    u32 p = panel_addr(g_hudPanelsBullet[0]);
    g_hudCount = (s16)(g_hudCount - 1);
    if (g_hudCount == 0) {
        P16(p, 0x10) = (u16)-0x80;
        GCALL4(F_SOUND_CUE, 1, 1, 0x7F, 0x7F);
        V0 = 1;
        return;
    }
    P16(p, 0x10) = (u16)(P16(p, 0x10) - (u16)g_hudStep);
    s32 w = 0x50 - ((s16)P16(p, 0x10) >> 4);
    P16(p, 0x08) = w < (s16)P16(p, 0x22) ? (u16)w : P16(p, 0x22);
    V0 = 0;
}

/* 80095640: handguns start reloading: the row slides out over 8 frames;
 * the wait and the scroll come per weapon from 0x800A630C. */
void n_reload_Handgun(void) {
    s32 step = mips_div((s32)g_hudRounds * g_hudRoundW, 8);
    u32 t = 0x800A630Cu + (u32)(s32)GV(s16, g_currentWeaponAddr) * 2;
    g_hudTimer = GV(u8, t);
    g_hudCount = 8;
    g_hudScroll = GV(u8, t + 1);
    g_hudStep = (s16)step;
}

/* 800956B0, 800956C0, 800956D8, 800956E8: the other weapons' waits. */
void n_reload_Magnum(void) { g_hudScroll = 0x14; }
void n_reload_Shotgun(void) { g_hudScroll = 0x3C; g_hudTimer = 0; }
void n_reload_Launcher(void) { g_hudScroll = 0xF; }
void n_reload_Rocket(void) { g_hudTimer = 0x2D; }
