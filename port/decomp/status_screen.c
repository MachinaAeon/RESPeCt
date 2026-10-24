/* The status screen (CONPAU_E overlay, unpacked into slot 4 at 0x800C3E48
 * when the game pauses; TYPEW and the other slot-4 overlays share these
 * addresses, so these natives only run while CONPAU_E is loaded).
 *
 * The screen runs on its own game thread (main loop 800C3F58, kept
 * translated): every frame it clears the ordering tables, runs the state
 * handler from the table at 0x800CF594 (state 0x800CF668: 1 load the
 * screen's files, 2 wait for input, 3 closing, 4 ...), updates the HUD
 * panels (8009358C) and sorts the 3D objects of the screen (800C4218).
 *
 * Everything on it is a HUD panel (hud.h): the menus and the item list
 * are panels whose update callbacks (+0x3C, the next one in +0x38) slide
 * them in and out and read the pad:
 *   - the command menu (0x800CF678: s16 menu id, count, +4, cursor, the
 *     cursor's panel (+8), the item panels (+0xA..)), built from the menu
 *     table at 0x800CF064 (12 bytes each: u8 count, u8 cancel action, u16
 *     look, s16 action per item; actions are callbacks from 0x800CF5A8);
 *   - the item list (0x800CF6B8: the items of the three lists (0x800B3DA0,
 *     the carried ones 0x800A8880 + 0x50, 0x800B4BE8 + 0x90) in groups of
 *     8 with 0xFF between groups, 0xFE for an empty place; count +0xE0,
 *     cursor +0xE2), its panels at 0x800CF6A8 (9 bytes, 0xFF = none);
 *   - an item's count shown as digit panels (up to three per item, their
 *     panel numbers in a 3-byte slot from 0x800CF698).
 * 800CBF10 tells whether a menu button is down for the pad type in use
 * (DualShock sticks, mouse buttons).
 *
 * Three of these can be on the screen thread's call chain while it waits
 * (800C4120 -> the state handler 800C4978 or 800C4BC8 -> ChangeThread), so
 * they save $ra and the s registers in their stack frames exactly as the
 * originals do: a save state made then continues there in their
 * translations, which reload them.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "status.h"

void n_status_SetView(void);
void n_status_Init(void);
void n_status_Frame(void);
void n_status_SortObjects(void);
void n_status_CountSlot(void);
void n_status_ItemCount(void);
void n_status_FreeCount(void);
void n_status_StartLoad(void);
void n_status_Load(void);
void n_status_Wait(void);
void n_status_SetState4(void);
void n_status_Close(void);
void n_status_PanelFrame(void);
void n_status_BuildMenu(void);
void n_status_MenuInput(void);
void n_status_OpenItems(void);
void n_status_MenuSlideIn(void);
void n_status_HideMenu(void);
void n_status_MenuSlideOut(void);
void n_status_Equip(void);
void n_status_MenuReset(void);
void n_status_BuildItems(void);
void n_status_BuildPrompts(void);
void n_status_ItemIcon(void);
void n_status_ListItems(void);
void n_status_ItemNames(void);
void n_status_ItemsSlideIn(void);
void n_status_ItemsInput(void);
void n_status_MarkerRow(void);
void n_status_ScrollUp(void);
void n_status_ScrollDown(void);
void n_status_MoveCount(void);
void n_status_PlaceCount(void);
void n_status_ClipPanel(void);
void n_status_ClipBar(void);
void n_status_Button(void);


/* 800C3FB0: the 3D view for the screen (not in mode 3): projection, the
 * camera, ambient light. */
void n_status_SetView(void) {
    FRAME(0x18);
    if (D_800B3D38 == 3) { V0 = 3; return; }
    JAL(0x800C3FD0u, F_SET_PROJECTION, 0x10E, A1, A2, A3);
    GV(u32, RVIEW) = 0;
    GV(u32, RVIEW + 4) = 0;
    GV(s32, RVIEW + 8) = -0x49C;
    for (u32 o = 0xC; o <= 0x1C; o += 4) GV(u32, RVIEW + o) = 0;
    JAL(0x800C4000u, F_SET_REF_VIEW2, RVIEW, A1, A2, A3);
    JAL(0x800C4010u, F_SET_AMBIENT, 0x9F6, 0x9F6, 0x9F6, A3);
}

/* 800C4020: the screen's state on opening. */
void n_status_Init(void) {
    FRAME(0x20);
    ST_SORT_OLD = 0;
    ST_STATE = 0;
    g_panelMode = 0x40;
    MENU_ID = 0;
    GV(u16, MENU + 6) = 0;
    ST_SOUND = 0;
    ST_SOUND_ON = 0;
    ST_OLD_BUF = (s16)(BUF_INDEX == 0);
    JAL(0x800C408Cu, F_MEMSET, COUNT_SLOTS, 0xFF, 9, A3);
    GV(u8, NAMES + 0x1A8) = 0x5C;
    GV(u8, NAMES + 0x1A9) = 0x90;
    GV(u8, NAMES + 0x1AA) = 0;
    GV(u8, NAMES + 0x1AB) = 0x80;
    GV(u32, NAMES + 0x1A4) = 0;
    ST_ITEMBOX = 0;
    ST_SCROLLED = 0;
    GV(u16, 0x800CFE00) = 0;
    JAL(0x800C40D0u, 0x800C3FB0u, A0, A1, A2, A3);
    ST_OBJ_ON = 0;
    u32 n = 0;
    while (GV(u8, 0x800B3E68u + n) != 0xFF) {
        n++;
        LOOP_POLL(0x800C40FC);
    }
    GV(u16, 0x800CF98C) = (u16)n;
    GV(u32, 0x800CF9CC) = 1;
    V0 = 1;
}

/* 800C4120: a frame: packet area and ordering tables for this buffer, the
 * state handler, the HUD panels. (Can wait in the handler: $ra and s0
 * saved as the original does.) */
void n_status_Frame(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[16];
    GS(u32, sp + 0x14) = R[31];
    JAL(0x800C4160u, F_GS_SET_WORKBASE, 0x800D28B0u + (u32)BUF_INDEX * 0x101D0u, A1, A2, A3);
    JAL(0x800C4188u, F_GS_CLEAR_OT, 0, 0, OT(OT_SCREEN), A3);
    JAL(0x800C41B0u, F_GS_CLEAR_OT, 0, 2, OT(OT_FRONT), A3);
    JAL(0x800C41D8u, F_GS_CLEAR_OT, 0, 1, OT(OT_WORLD), A3);
    jalr_ra(0x800C4200u, GV(u32, HANDLERS + (u32)ST_STATE * 4));
    JAL(0x800C4208u, F_HUD_UPDATE, A0, A1, A2, A3);
}

/* 800C4218: draw the HUD, sort the screen's 3D objects (when on) into the
 * world or front table, then those tables (and, while switching buffers,
 * the other buffer's) into the screen's. */
void n_status_SortObjects(void) {
    FRAME(0x70);
    JAL(0x800C4240u, F_HUD_SORT, A0, A1, A2, A3);
    if (ST_OBJ_ON != 0) {
        u32 n = GV(u8, OBJECTS);
        GV(u32, 0x800BAA08) = 0;
        u32 obj = OBJECTS + 0xC;
        for (u32 i = 0; (s32)i < (s32)n; i++, obj += 8) {
            if (i != 0) LOOP_POLL(0x800C4364);
            if (GV(u32, obj + 4) == 0 || GV(u8, OBJECTS + i + 2) == 0) continue;
            u32 coord = GV(u32, obj);
            if (coord != 0) {
                JAL(0x800C42B8u, F_GS_GET_LWS, coord, sp + 0x30, sp + 0x10, A3);
                JAL(0x800C42C0u, F_GS_SET_LS_MATRIX, sp + 0x10, A1, A2, A3);
                JAL(0x800C42C8u, F_GS_SET_LIGHT_MATRIX, sp + 0x30, A1, A2, A3);
            }
            int front;
            if (i == 1) front = 1;
            else {
                s32 m = ST_OBJ_MODE;
                if ((m < 6 || m == 0xC) && i == 3) front = 1;
                else if ((u32)(ST_OBJ_MODE - 6) < 6) front = (i == 2);
                else front = 0;
            }
            JAL(0x800C435Cu, F_GS_SORT_OBJECT, obj, OT(front ? OT_FRONT : OT_WORLD), SCRATCH_WORK, A3);
        }
    }
    JAL(0x800C43A0u, F_GS_SORT_OT, OT(OT_WORLD), OT(OT_SCREEN), A2, A3);
    JAL(0x800C43C8u, F_GS_SORT_OT, OT(OT_FRONT), OT(OT_SCREEN), A2, A3);
    if (ST_SORT_OLD == 0) { V0 = 0; return; }
    u32 old = (u32)(s32)ST_OLD_BUF * 20;
    JAL(0x800C440Cu, F_GS_SORT_OT, OT_WORLD + old, OT(OT_SCREEN), A2, A3);
    JAL(0x800C443Cu, F_GS_SORT_OT, OT_FRONT + old, OT(OT_SCREEN), A2, A3);
}

/* 800C4464: a free 3-byte slot for an item count's digit panels (its
 * address), or 0. */
void n_status_CountSlot(void) {
    u32 p = COUNT_SLOTS;
    for (u32 i = 0;;) {
        if (GV(u8, p) == 0xFF) { V0 = p; return; }
        i++;
        p += 3;
        if ((s16)i >= 3) { V0 = 0; return; }
        LOOP_POLL(0x800C449C);
    }
}

/* 800C44AC (panel, item slot or -1, x, y, [flags]): show an item's count
 * as digit panels to the left of (x, y) (an infinity mark when it has no
 * limit), their numbers kept in a count slot (panel +0x38). Slot -1 takes
 * the count from the panel (+0x2A, digit offset +0x2C). */
void n_status_ItemCount(void) {
    FRAME(0x38);
    u32 panel = A0;
    s16 slot = (s16)A1;
    u32 x = A2, y = A3;
    u16 flags = GS(u16, sp + 0x48);
    GS(u32, sp + 0x38) = panel;
    u32 off;                              /* digit sprite v offset (s8) */
    u32 count;
    if (slot == -1) {
        off = GV(u8, panel + 0x2C);
        count = GV(u16, panel + 0x2A);
    } else {
        off = 0;
        u32 e = ITEMS_B + (u32)(s32)slot * 8;
        s16 id = GV(s16, e);
        if (GV(u8, WEAPON_TABLE + (u32)(s32)id * 8 + 6) & 0x10) {
            s16 extra = GV(s16, e + 6);
            if (extra == 0) count = GV(u16, e + 2);
            else if (extra <= 0) count = GV(u16, e + 6);
            else count = (u32)GV(u16, e + 6) + GV(u16, e + 2);
            if (id == 8) off = (u32)GV(u8, e + 4) - 0xC;
        } else {
            count = GV(u16, e + 2);
            off = (u32)(s32)id - 0xC;
            if (!((((u32)(s32)id - 0xD) & 0xFF) < 3)) off = 0;
        }
    }
    u32 cs = JAL(0x800C45B8u, 0x800C4464u, A0, A1, A2, A3);
    x -= 0x280;
    if (cs == 0) return;
    y += 0x40;
    GV(u32, GS(u32, sp + 0x38) + 0x38) = cs;
    if ((s16)count < 0) {
        u32 a1 = JAL(0x800C45ECu, F_ALLOC_PANEL, A0, A1, A2, A3);
        if ((s16)a1 == -1) return;
        u32 p = pnl_s((s16)a1);
        GV(u8, cs) = (u8)a1;
        GV(u16, p + 0x1C) = GV(u16, GS(u32, sp + 0x38) + 0x1C);
        JAL(0x800C4638u, F_SET_SPRITE, p, SPR_COUNT, A2, A3);
        GV(u16, p + 4) = (u16)(GV(u16, p + 4) + GV(u16, DIGITS + 0x2C));
        u16 v = GV(u16, DIGITS + 0x2E);
        u16 old6 = GV(u16, p + 6), oldE = GV(u16, p + 0xE);
        GV(u16, p + 0x22) = 0x10;
        GV(u16, p + 0x10) = (u16)x;
        GV(u16, p + 0x12) = (u16)y;
        GV(u16, p + 0x26) = (u16)y;
        GV(u16, p + 0x18) = flags;
        GV(u16, p + 0xA) = 0;
        GV(u16, p + 0x24) = old6;
        GV(u16, p + 0xE) = (u16)(oldE + (u32)(s32)(s8)off);
        GV(u16, p + 8) = v;
        return;
    }
    u32 digits = 0;
    if ((s16)count != 0) {
        u32 q = count;
        for (;;) {
            q = (u32)((s32)(s16)q / 10);
            digits++;
            if ((q & 0xFFFF) == 0) break;
            LOOP_POLL(0x800C46D0);
        }
    }
    if ((count & 0xFFFF) == 0) digits = 1;
    x += (u32)(((s32)(s16)digits - 1) << 7);
    if ((s16)digits <= 0) return;
    for (;;) {
        u32 a1 = JAL(0x800C4710u, F_ALLOC_PANEL, A0, A1, A2, A3);
        if ((s16)a1 != -1) {
            u32 p = pnl_s((s16)a1);
            GV(u8, cs) = (u8)a1;
            GV(u16, p + 0x1C) = GV(u16, GS(u32, sp + 0x38) + 0x1C);
            JAL(0x800C4760u, F_SET_SPRITE, p, SPR_COUNT, A2, A3);
            s32 c = (s16)count;
            s32 qd = c / 10;
            s32 r = c - qd * 10;
            s32 r2 = (s16)((u32)r << 1);
            GV(u16, p + 4) = (u16)(GV(u16, p + 4) + GV(u16, DIGITS + (u32)(r2 * 2)));
            u16 w = GV(u16, DIGITS + (u32)((r2 + 1) * 2));
            u16 old6 = GV(u16, p + 6), oldE = GV(u16, p + 0xE);
            GV(u16, p + 0x22) = 0x10;
            count = (u32)qd;
            GV(u16, p + 0x10) = (u16)x;
            GV(u16, p + 0x12) = (u16)y;
            GV(u16, p + 0x26) = (u16)y;
            GV(u16, p + 0x18) = flags;
            GV(u16, p + 0xA) = 0;
            GV(u16, p + 0x24) = old6;
            GV(u16, p + 8) = w;
            x -= (u32)w << 4;
            GV(u16, p + 0xE) = (u16)(oldE + (u32)(s32)(s8)off);
        }
        digits--;
        cs++;
        if ((s16)digits <= 0) return;
        LOOP_POLL(0x800C4820);
    }
}

/* 800C4858 (panel): free the digit panels of its count (panel +0x38). */
void n_status_FreeCount(void) {
    u32 panel = A0;
    u32 s = GV(u32, panel + 0x38);
    if (s == 0) { V0 = 0; return; }
    u32 i = 0;
    for (;;) {
        if (GV(u8, s) == 0xFF) { V0 = i + 1; break; }
        i++;
        GV(u16, pnl(GV(u8, s)) + PANEL_FLAGS) = 1;
        GV(u8, s) = 0xFF;
        s++;
        if (!((s16)i < 3)) { V0 = 0; break; }
        LOOP_POLL(0x800C48BC);
    }
    GV(u32, panel + 0x38) = 0;
}

/* 800C48D0: on to state 1 (loading). */
void n_status_StartLoad(void) {
    ST_STATE = 1;
    ST_SORT_OLD = 0;
    if (D_800B3D38 == 3) { V0 = 3; return; }
    g_hudFrame = 1;
    V0 = 1;
}

/* 800C4908: state 1: load the screen's file (13) and its HUD images, build
 * its panels; on to state 2. */
void n_status_Load(void) {
    FRAME(0x20);
    GS(u32, sp + 0x10) = 0x800D9DE0u;
    JAL(0x800C4940u, F_FILE_LOAD, 0x800C3E4Cu, 0x8005853Cu, 0x800A0C74u, 0xD);
    JAL(0x800C4950u, F_HUD_LOAD, 0x800D9DE0u, 0x800D28B0u, (u32)-1, A3);
    ST_STATE = 2;
    JAL(0x800C4968u, F_HUD_BUILD, 0x800CF198u, A1, A2, A3);
}

/* |stick byte - 0x80| >= 0x61: pushed. */
static int pushed(u8 b) {
    s32 d = (s32)b - 0x80;
    if (d < 0) d = -d;
    return d >= 0x61;
}

/* 800C4978: state 2: read the pad; START (or, on a DualShock, a stick
 * pushed this frame) closes the screen (state 3); a mouse's two buttons
 * together ask to cancel. Then the music change, if any. (Can wait in
 * the pad read: $ra, s0, s1 saved as the original does.) */
void n_status_Wait(void) {
    FRAME(0x20);
    GS(u32, sp + 0x10) = R[16];
    GS(u32, sp + 0x14) = R[17];
    GS(u32, sp + 0x18) = R[31];
    JAL(0x800C498Cu, 0x80071584u, A0, A1, A2, A3);
    s32 mode = D_800B3D38;
    if (mode != 0 && mode != 3) {
        u32 t = g_padTypeBit;
        int close = 0;
        if (t == 0x200000) {
            if ((g_buttons & 0x840) == 0x840 && (g_buttonsPrev & 0x840) != 0x840) ST_CANCEL = 1;
        } else if (t == 0x80000 || t == 0x400000) {
            if ((g_buttons & 0x800) && !(g_buttonsPrev & 0x800)) close = 1;
            else if (pushed(GV(u8, 0x800A8843)) && !pushed(GV(u8, 0x800B3DF7)) && !pushed(GV(u8, 0x800B3DF6)))
                close = 1;
            else if (pushed(GV(u8, 0x800A8842)) && !pushed(GV(u8, 0x800B3DF7)) && !pushed(GV(u8, 0x800B3DF6)))
                close = 1;
        }
        if (close) {
            ST_STATE = 3;
            g_buttonsPrev = g_buttons;
            JAL(0x800C4B24u, F_SOUND_CUE, 0, 3, 0x7F, 0x7F);
        }
    }
    if (ST_SOUND != ST_SOUND_ON) {
        JAL(0x800C4B8Cu, F_SOUND_STOP, A0, A1, A2, A3);
        JAL(0x800C4B98u, F_SOUND_PLAY, ST_SOUND, 1, A2, A3);
        ST_SOUND_ON = ST_SOUND;
    }
}

/* 800C4BB8: on to state 4. */
void n_status_SetState4(void) {
    ST_STATE = 4;
    V0 = 4;
}

/* 800C4BC8: close the screen: free the HUD, stop the music, wait a frame,
 * the game's projection and light back, and switch back to the game's
 * thread. ($ra saved as the original does: it waits there.) */
void n_status_Close(void) {
    FRAME(0x18);
    GS(u32, sp + 0x10) = R[31];
    s32 mode = D_800B3D38;
    GV(u32, 0x800A8838) = 0;
    if (mode != 0) g_panelMode = 2;
    g_hudFrame = 0;
    JAL(0x800C4BFCu, F_HUD_FREE, A0, A1, A2, A3);
    JAL(0x800C4C04u, F_SOUND_STOP, A0, A1, A2, A3);
    JAL(0x800C4C0Cu, F_VSYNC, 0, A1, A2, A3);
    if (GV(s16, 0x800CFE00) != 0) JAL(0x800C4C28u, 0x800CE0A4u, A0, A1, A2, A3);
    JAL(0x800C4C30u, F_SET_PROJECTION, 0x10E, A1, A2, A3);
    JAL(0x800C4C40u, F_SET_AMBIENT, 0x400, 0x400, 0x400, A3);
    JAL(0x800C4C48u, 0x800131C8u, 0, A1, A2, A3);
    JAL(0x800C4C50u, 0x800130B4u, A0, A1, A2, A3);
}

/* 800C4C60: panel callback: show frame +0x22 of the panel's frame list
 * (+0x38: u, v offsets, w, h), then the standard callback. */
void n_status_PanelFrame(void) {
    u32 p = g_curPanel;
    u32 f = GV(u32, p + 0x38) + (u32)(s32)GV(s16, p + 0x22) * 8;
    GV(u16, p + 4) = (u16)(GV(u16, p + 4) + GV(u16, f));
    GV(u16, p + 6) = (u16)(GV(u16, p + 6) + GV(u16, f + 2));
    GV(u16, p + 8) = GV(u16, f + 4);
    GV(u32, p + 0x3C) = 0x8009343Cu;
    GV(u16, p + 0xA) = GV(u16, f + 6);
    V0 = 0x8009343Cu;
}

/* Copy a sprite rectangle (u, v offsets, w, h) onto a panel. */
static void panel_rect(u32 p, u32 r) {
    GV(u16, p + 4) = (u16)(GV(u16, p + 4) + GV(u16, r));
    GV(u16, p + 6) = (u16)(GV(u16, p + 6) + GV(u16, r + 2));
    GV(u16, p + 8) = GV(u16, r + 4);
    GV(u16, p + 0xA) = GV(u16, r + 6);
}

/* A new panel with the menu sprite, flagged 0x144, made current. */
static u32 menu_panel(u32 ra_set, u32 idx) {
    u32 p = pnl_s((s16)idx);
    GV(u16, p + PANEL_FLAGS) = GV(u16, p + PANEL_FLAGS) | 0x144;
    g_curPanel = p;
    JAL(ra_set, F_SET_SPRITE, p, SPR_MENU, A2, A3);
    return g_curPanel;
}

/* 800C4CC8: build the command menu (MENU_ID) beside the current panel:
 * its frame, a panel per command (the one under the cursor highlighted),
 * the hint box. In mode 3 the panel runs 800C54B8 instead. */
void n_status_BuildMenu(void) {
    FRAME(0x38);
    if (D_800B3D38 == 3) {
        GV(u32, g_curPanel + 0x3C) = 0x800C54B8u;
        u32 v = JAL(0x800C4D20u, 0x800C741Cu, (u32)(s32)GV(s16, 0x800B3D04), A1, A2, A3);
        u32 fn = GV(u32, g_curPanel + 0x3C);
        GV(u32, 0x800CF9C0) = v;
        R[4] = fn;
        jalr_ra(0x800C4D38u, fn);
        return;
    }
    GV(u32, 0x800CF9C0) = 0;
    u32 self = g_curPanel;
    u32 def = MENU_DEFS + (u32)(s32)MENU_ID * 12;
    GV(u16, MENU + 2) = GV(u8, def);
    GV(u16, MENU + 4) = GV(u16, def + 4);
    u32 a2 = JAL(0x800C4D88u, F_ALLOC_PANEL, A0, A1, A2, A3);
    if ((s16)a2 != -1) {
        GV(u16, MENU + 8) = (u16)a2;
        u32 p = menu_panel(0x800C4DD4u, a2);
        GV(u16, p + 0x10) = GV(u16, self + 0x10);
        GV(u16, p + 0x18) = 3;
        GV(u16, p + 0x12) = GV(u16, self + 0x12);
        panel_rect(p, MENU_LOOKS + (u32)GV(u16, def + 2) * 8);
    }
    if (MENU_COUNT > 0) {
        u32 i = 0;
        for (;;) {
            a2 = JAL(0x800C4E6Cu, F_ALLOC_PANEL, A0, A1, A2, A3);
            if ((s16)a2 != -1) {
                s32 k = (s16)i;
                MENU_PANEL(k) = (s16)a2;
                u32 p = menu_panel(0x800C4EC4u, a2);
                GV(u16, p + 0x18) = 0;
                u32 r = ITEM_LOOKS + (u32)((s32)((u32)GV(u16, def + (u32)k * 2 + 4) << 18) >> 15);
                u16 u = (u16)(GV(u16, p + 4) + GV(u16, r));
                GV(u16, p + 4) = u;
                GV(u16, p + 0x22) = u;
                GV(u16, p + 6) = (u16)(GV(u16, p + 6) + GV(u16, r + 2));
                GV(u16, p + 8) = GV(u16, r + 4);
                GV(u16, p + 0xA) = GV(u16, r + 6);
                GV(u16, p + 0x10) = (u16)(GV(u16, self + 0x10) + ((u32)GV(u16, p + 0x22) << 4));
                GV(u16, p + 0x12) = (u16)(GV(u16, self + 0x12) + 0x80);
                if (MENU_CURSOR == k) GV(u16, p + 0xE) = (u16)(GV(u16, p + 0xE) + 2);
            }
            i++;
            if (!((s16)i < MENU_COUNT)) break;
            LOOP_POLL(0x800C4F88);
        }
    }
    a2 = JAL(0x800C4F98u, F_ALLOC_PANEL, A0, A1, A2, A3);
    if ((s16)a2 == -1) {
        MENU_HINT = 0xFF;
    } else {
        u32 p = pnl_s((s16)a2);
        u16 fl = GV(u16, p + PANEL_FLAGS);
        g_curPanel = p;
        MENU_HINT = (u8)a2;
        GV(u16, p + PANEL_FLAGS) = fl | 0x144;
        JAL(0x800C4FECu, F_SET_SPRITE, p, SPR_MENU, A2, A3);
        p = g_curPanel;
        GV(u16, p + 8) = 0x28;
        GV(u16, p + 0xA) = 0x10;
        GV(u16, p + 4) = (u16)(GV(u16, p + 4) + 0x48);
        GV(u16, p + 6) = (u16)(GV(u16, p + 6) + 0x60);
        GV(u16, p + 0x10) = (u16)(GV(u16, self + 0x10) + 0xFE0);
        GV(u16, p + 0x18) = 0;
        GV(u16, p + 0x12) = (u16)(GV(u16, self + 0x12) + 0x80);
    }
    if (D_800B3D38 != 0) GV(u32, self + 0x3C) = 0x800C50A8u;
    u16 e = GV(u16, g_curPanel + 0xE);
    g_curPanel = self;
    GV(u16, self + 0xE) = e;
    V0 = 0x800C0000u;
}

/* Move the menu cursor to `to`: the old command back to normal, the new
 * one highlighted, the click. */
static void menu_move(u32 ra, s32 to) {
    u32 cur = g_curPanel;
    GV(u16, pnl_s(MENU_PANEL(MENU_CURSOR)) + 0xE) = GV(u16, cur + 0xE);
    GV(u16, pnl_s(MENU_PANEL((s16)to)) + 0xE) = (u16)(GV(u16, cur + 0xE) + 1);
    GV(u16, MENU + 6) = (u16)to;
    sound(ra, 0);
}

/* 800C50A8: panel callback: the command menu's input: up/down (repeating
 * every 10 frames while held), OK runs the command's action, cancel the
 * menu's cancel action. */
void n_status_MenuInput(void) {
    FRAME(0x28);
    u32 count = GV(u16, MENU + 2);
    GV(u16, pnl_s(MENU_PANEL(MENU_CURSOR)) + 0xE) = (u16)(GV(u16, g_curPanel + 0xE) + 1);
    if (BTN(0x800C511Cu, 0x800)) {
        if (!BTN(0x800C5134u, 0x40)) {
            if (BTN_PREV(0x800C514Cu, 0x800)) {
                MENU_REPEAT = (u16)(MENU_REPEAT - 1);
                if ((s16)MENU_REPEAT > 0) goto not_up;
            }
            MENU_REPEAT = 0xA;
            s32 to = (s16)(MENU_CURSOR - 1);
            u32 t0 = (u32)(MENU_CURSOR - 1);
            if (to < 0) t0 = count - 1;
            /* the old command's look is restored before the new cursor is read */
            u32 cur = g_curPanel;
            GV(u16, pnl_s(MENU_PANEL(MENU_CURSOR)) + 0xE) = GV(u16, cur + 0xE);
            GV(u16, pnl_s(MENU_PANEL((s16)t0)) + 0xE) = (u16)(GV(u16, cur + 0xE) + 1);
            GV(u16, MENU + 6) = (u16)t0;
            sound(0x800C5204u, 0);
            return;
        }
    }
not_up:
    if (BTN(0x800C521Cu, 0x40) && !BTN(0x800C5234u, 0x800)) {
        if (BTN_PREV(0x800C5250u, 0x40)) {
            MENU_REPEAT = (u16)(MENU_REPEAT - 1);
            if ((s16)MENU_REPEAT > 0) goto not_down;
        }
        MENU_REPEAT = 0xA;
        u32 t0 = (u32)(MENU_CURSOR + 1);
        if (!((s32)(t0 << 16) < (s32)(count << 16))) t0 = 0;
        menu_move(0x800C5324u, (s32)t0);
        return;
    }
not_down:
    if (BTN(0x800C5338u, 0x20) && !BTN_PREV(0x800C5350u, 0x20)) {
        s32 c = MENU_CURSOR;
        u32 cur = g_curPanel;
        GV(u16, pnl_s(MENU_PANEL(c)) + 0xE) = (u16)(GV(u16, cur + 0xE) + 2);
        s16 act = GV(s16, MENU_DEFS + (u32)(s32)MENU_ID * 12 + (u32)c * 2 + 4);
        GV(u32, cur + 0x3C) = GV(u32, ACTIONS + (u32)(s32)act * 4);
        if (act == 1) {
            ST_OBJ_MODE = 0;
            GV(u16, 0x800CF8C4) = 0xC00;
            JAL(0x800C5404u, 0x800CB410u, A0, A1, A2, A3);
        }
    }
    int cancel;
    if (BTN(0x800C5414u, 0x7FF)) cancel = !BTN_PREV(0x800C542Cu, 0x7FF) || ST_CANCEL != 0;
    else cancel = ST_CANCEL != 0;
    if (cancel) {
        ST_CANCEL = 0;
        u32 a = GV(u8, MENU_DEFS + (u32)(s32)MENU_ID * 12 + 1);
        GV(u32, g_curPanel + 0x3C) = GV(u32, ACTIONS + a * 4);
    }
}

/* 800C7498: menu action: the item list (click, then build it). */
void n_status_OpenItems(void) {
    FRAME(0x18);
    JAL(0x800C74B4u, F_SOUND_CUE, 0, 2, 0x7F, 0x7F);
    JAL(0x800C74BCu, 0x800C90F4u, A0, A1, A2, A3);
}

/* Move the menu's panels across by d (+0x10). */
static void menu_shift(u32 d, int sub, u32 pc) {
    if (MENU_COUNT > 0) {
        for (u32 i = 0;;) {
            u32 p = pnl_s(MENU_PANEL((s16)i));
            i++;
            GV(u16, p + 0x10) = (u16)(sub ? GV(u16, p + 0x10) - d : GV(u16, p + 0x10) + d);
            if (!((s16)i < MENU_COUNT)) break;
            LOOP_POLL(pc);
        }
    }
    u32 p = pnl_s(MENU_CURPANEL);
    GV(u16, p + 0x10) = (u16)(sub ? GV(u16, p + 0x10) - d : GV(u16, p + 0x10) + d);
    u32 h = MENU_HINT;
    if (h != 0xFF) {
        p = pnl(h);
        GV(u16, p + 0x10) = (u16)(sub ? GV(u16, p + 0x10) - d : GV(u16, p + 0x10) + d);
    }
}

/* 800C74CC: panel callback: slide the panel (and the menu) in by +0x2E a
 * frame, the step growing; at 0xA00 the menu is rebuilt there and the
 * input callback (800C7704) takes over. */
void n_status_MenuSlideIn(void) {
    FRAME(0x18);
    u32 p = g_curPanel;
    u32 d = GV(u16, p + 0x2E);
    if ((s32)GV(s16, p + 0x10) + GV(s16, p + 0x2E) >= 0xA00) {
        GV(u16, p + 0x10) = 0xA00;
        JAL(0x800C750Cu, 0x800C7634u, A0, A1, A2, A3);
        JAL(0x800C7514u, 0x800C4CC8u, A0, A1, A2, A3);
        p = g_curPanel;
        GV(u16, p + 0x2E) = 0x280;
        GV(u32, p + 0x3C) = 0x800C7704u;
        V0 = 0x800C7704u;
        return;
    }
    GV(u16, p + 0x10) = (u16)(GV(u16, p + 0x10) + d);
    menu_shift(d, 0, 0x800C75A0);
    s32 s = (s32)(d << 16) >> 20;
    p = g_curPanel;
    GV(u16, p + 0x2E) = (u16)(GV(u16, p + 0x2E) + (u32)s);
    V0 = (u32)s;
}

/* 800C7634: free the menu's panels. */
void n_status_HideMenu(void) {
    if (MENU_COUNT > 0) {
        for (u32 i = 0;;) {
            u32 a = MENU + 0xA + (u32)(s32)(s16)i * 2;
            i++;
            GV(u16, pnl_s(GV(s16, a)) + PANEL_FLAGS) = 1;
            GV(u16, a) = 0xFFFF;
            if (!((s16)i < MENU_COUNT)) break;
            LOOP_POLL(0x800C76A0);
        }
    }
    GV(u16, pnl_s(MENU_CURPANEL) + PANEL_FLAGS) = 1;
    u32 h = MENU_HINT;
    if (h != 0xFF) GV(u16, pnl(h) + PANEL_FLAGS) = 1;
    GV(u16, MENU + 8) = 0xFFFF;
    V0 = 0xFFFFFFFFu;
}

/* 800C7704: panel callback: slide the panel and the menu out by +0x2E;
 * past -0x780 the next callback (+0x38) takes over. */
void n_status_MenuSlideOut(void) {
    u32 p = g_curPanel;
    u32 d = (u32)(s32)GV(s16, p + 0x2E);
    u16 x = GV(u16, p + 0x10);
    if ((s32)GV(s16, p + 0x10) - (s32)GV(s16, p + 0x2E) < -0x77F) {
        d = (u32)x + 0x780;
        GV(u16, p + 0x10) = 0xF880;
        GV(u32, p + 0x3C) = GV(u32, p + 0x38);
    } else {
        GV(u16, p + 0x10) = (u16)(x - d);
    }
    menu_shift(d, 1, 0x800C77B8);
    s32 s = (s32)(d << 16) >> 20;
    p = g_curPanel;
    GV(u16, p + 0x2E) = (u16)(GV(u16, p + 0x2E) + (u32)s);
    V0 = (u32)s;
}

/* 800C7888: menu action: equip the item under the list's cursor (a
 * weapon not already held: the HUD's weapon icon, its sounds), else the
 * buzz; back to the list. */
void n_status_Equip(void) {
    FRAME(0x28);
    u32 e = ITEMS_B + (u32)GV(u8, LIST + (u32)(s32)LIST_CURSOR) * 8;
    s16 id = GV(s16, e);
    if (id != GV(s16, g_currentWeaponAddr) && (GV(u8, WEAPON_TABLE + (u32)(s32)id * 8 + 6) & 0x10)) {
        JAL(0x800C7910u, F_SOUND_CUE, 0, 2, 0x7F, 0x7F);
        g_currentWeaponAddr = ITEMS_B + (u32)GV(u8, LIST + (u32)(s32)LIST_CURSOR) * 8;
        JAL(0x800C7934u, F_EQUIP_ICON, (u32)(s32)id, 0x44, A2, A3);
        GV(u16, 0x800AFCB0) = GV(u16, 0x800AFCB0) & 0xFFF7;
        GV(u16, 0x80062DBC) = GV(u16, 0x80062DBC) & 0x7FFF;
        JAL(0x800C7958u, F_LOAD_SOUNDS, A0, A1, A2, A3);
    } else {
        JAL(0x800C7974u, F_SOUND_CUE, 0, 4, 0x7F, 0x7F);
    }
    JAL(0x800C797Cu, 0x800C90B8u, A0, A1, A2, A3);
}

/* 800C90B8: back to the item list: menu 0, the panel slides the menu in
 * (800C74CC) then reads the list's input (800C9E80). */
void n_status_MenuReset(void) {
    u32 p = g_curPanel;
    GV(u16, p + 0x2E) = 0x280;
    MENU_ID = 0;
    GV(u16, MENU + 6) = 0;
    GV(u32, p + 0x38) = 0x800C9E80u;
    GV(u32, p + 0x3C) = 0x800C74CCu;
    V0 = 0x800C74CCu;
}

/* 800C90F4: build the item list: the list, its four frame panels (from
 * 0x800CF160, 14 bytes each), the names box, the button prompts, the icon
 * of the item under the cursor; the panel slides it in (800C9B74). */
void n_status_BuildItems(void) {
    FRAME(0x28);
    GV(u16, LIST + 0xE2) = 2;
    JAL(0x800C9120u, 0x800C964Cu, A0, A1, A2, A3);
    for (u32 i = 0;;) {
        INV_PANEL((u32)(s32)(s16)i) = 0xFF;
        i++;
        if (!((s16)i < 9)) break;
        LOOP_POLL(0x800C9154);
    }
    u32 src = 0x800CF160u;
    for (u32 i = 0;;) {
        u32 a2 = JAL(0x800C9180u, F_ALLOC_PANEL, A0, A1, A2, A3);
        if ((s16)a2 != -1) {
            u32 p = pnl_s((s16)a2);
            INV_PANEL((u32)(s32)(s16)i) = (u8)a2;
            JAL(0x800C91C8u, F_SET_SPRITE, p, SPR_ITEMS, A2, A3);
            GV(u16, p + 4) = (u16)(GV(u16, p + 4) + GV(u16, src));
            GV(u16, p + 6) = (u16)(GV(u16, p + 6) + GV(u16, src + 2));
            GV(u16, p + 8) = GV(u16, src + 4);
            GV(u16, p + 0xA) = GV(u16, src + 6);
            GV(u16, p + 0x10) = GV(u16, src + 8);
            GV(u16, p + 0x12) = GV(u16, src + 0xA);
            u16 fl = GV(u16, src + 0xC);
            GV(u16, p + PANEL_FLAGS) = GV(u16, p + PANEL_FLAGS) | 0x154;
            GV(u16, p + 0x18) = fl;
            src += 0xE;
        }
        i++;
        if (!((s16)i < 4)) break;
        LOOP_POLL(0x800C924C);
    }
    JAL(0x800C925Cu, 0x800C98D0u, 0xAA, (u32)-0x46, A2, A3);
    JAL(0x800C9264u, 0x800C92E0u, A0, A1, A2, A3);
    u32 it = GV(u8, LIST + (u32)(s32)LIST_CURSOR);
    if (it != 0xFF) {
        u32 v = JAL(0x800C9294u, 0x800C94C4u, it, 0x1700, 0x40, 6);
        INV_PANEL(6) = (u8)v;
    }
    u32 p = g_curPanel;
    GV(u16, p + 0x2E) = 0x280;
    GV(u16, p + 0x10) = 0xA00;
    GV(u32, p + 0x3C) = 0x800C9B74u;
    V0 = 0x800C9B74u;
}

/* 800C92E0: the button prompt panels (4 and 5) for the pad type in use. */
void n_status_BuildPrompts(void) {
    FRAME(0x28);
    ST_PADTYPE = g_padTypeBit;
    for (u32 i = 4;;) {
        u32 a0 = JAL(0x800C9328u, F_ALLOC_PANEL, A0, A1, A2, A3);
        if ((s16)a0 != -1) {
            s32 k = (s16)i;
            INV_PANEL((u32)k) = (u8)a0;
            u32 p = pnl_s((s16)a0);
            JAL(0x800C9370u, F_SET_SPRITE, p, SPR_ITEMS, A2, A3);
            GV(u16, p + PANEL_FLAGS) = GV(u16, p + PANEL_FLAGS) | 0x154;
            u32 t = g_padTypeBit;
            if (t == 0x200000) {
                u16 u;
                if (k == 4) { GV(u16, p + 0x12) = 0xFC00; u = (u16)(GV(u16, p + 4) + 0xB0); }
                else { GV(u16, p + 0x12) = 0x100; u = (u16)(GV(u16, p + 4) + 0xB8); }
                GV(u16, p + 4) = u;
                GV(u16, p + 0xA) = 0x10;
                GV(u16, p + 6) = (u16)(GV(u16, p + 6) + 0x70);
            } else if (t == 0x80000 || t == 0x400000) {
                u16 u, v;
                if (k == 4) {
                    GV(u16, p + 0x12) = 0xFC00;
                    u = (u16)(GV(u16, p + 4) + 0xB0);
                    v = (u16)(GV(u16, p + 6) + 0x70);
                } else {
                    GV(u16, p + 0x12) = 0x100;
                    u = (u16)(GV(u16, p + 4) + 0xB8);
                    v = (u16)(GV(u16, p + 6) + 0x79);
                }
                GV(u16, p + 4) = u;
                GV(u16, p + 6) = v;
                GV(u16, p + 0xA) = 6;
            }
            u16 e = (u16)(GV(u16, p + 0xE) + 1);
            GV(u16, p + 0x10) = 0x13C0;
            GV(u16, p + 8) = 8;
            u16 h = GV(u16, p + 0xA);
            GV(u16, p + 0x18) = 0;
            GV(u16, p + 0xE) = e;
            GV(u16, p + 0x22) = e;
            GV(u16, p + 0x12) = (u16)(GV(u16, p + 0x12) - ((u32)h << 3));
        }
        i++;
        if (!((s16)i < 6)) break;
        LOOP_POLL(0x800C949C);
    }
    V0 = 0;
}

/* 800C94C4 (item slot or 0xFE, x, y, flags): a panel showing the item's
 * icon (0xFE: the empty one) at (x, y), with its count for items that
 * have one; the panel's number, or -1. */
void n_status_ItemIcon(void) {
    FRAME(0x38);
    u32 slot = A0, x = A1, y = A2, fl = A3;
    u32 id;
    if ((s16)slot == 0xFE) id = 0x1E;
    else id = GV(u16, ITEMS_B + (u32)(s32)(s16)slot * 8);
    u32 n = JAL(0x800C9530u, F_ALLOC_PANEL, A0, A1, A2, A3);
    if ((s16)n != -1) {
        u32 p = pnl_s((s16)n);
        s32 sid = (s16)id;
        JAL(0x800C9584u, F_SET_SPRITE, p, SPRITES + (u32)sid * 20, A2, A3);
        u16 h = GV(u16, p + 0xA);
        u16 old6 = GV(u16, p + 6);
        GV(u16, p + PANEL_FLAGS) = GV(u16, p + PANEL_FLAGS) | 0x154;
        u16 w = GV(u16, p + 8);
        GV(u16, p + 0x12) = (u16)(y - ((u32)h << 3));
        u16 h2 = GV(u16, p + 0xA), y2 = GV(u16, p + 0x12);
        GV(u16, p + 0x18) = (u16)fl;
        GV(u32, p + 0x38) = 0;
        GV(u16, p + 0x24) = old6;
        GV(u16, p + 0x10) = (u16)(x - ((u32)w << 3));
        GV(u16, p + 0x22) = h2;
        GV(u16, p + 0x26) = y2;
        if (GV(u8, WEAPON_TABLE + (u32)sid * 8 + 6) & 0x70) {
            GS(u32, sp + 0x10) = (u32)(s32)(s16)fl;
            JAL(0x800C961Cu, 0x800C44ACu, p, (u32)(s32)(s16)slot, (u32)(s32)(s16)x, (u32)(s32)(s16)y);
        }
    }
    V0 = (u32)(s32)(s16)n;
}

/* Copy one item list into the screen's list, a 0xFF after every 8 (from
 * the third place on). */
static u32 list_copy(u32 pos, u32 list, u32 count_off, u32 add, u32 pc) {
    s32 n = GV(s16, list + count_off);
    if (n <= 0) return pos;
    for (u32 i = 0;;) {
        if (((((u32)(s32)(s16)pos) - 2) & 7) == 0) {
            GV(u8, LIST + (u32)(s32)(s16)pos) = 0xFF;
            pos++;
        }
        u32 at = pos;
        pos++;
        u32 k = (u32)(s32)(s16)i;
        i++;
        GV(u8, LIST + (u32)(s32)(s16)at) = (u8)(GV(u8, list + k) + add);
        if (!((s16)i < n)) break;
        LOOP_POLL(pc);
    }
    return pos;
}

/* 800C964C: the screen's item list from the three lists; at least six
 * places (0xFE empty, place 2 a 0xFF); its count. */
void n_status_ListItems(void) {
    u32 pos = 0;
    pos = list_copy(pos, 0x800B3DA0u, 0x50, 0, 0x800C96E4);
    pos = list_copy(pos, g_inventory, 0x40, 0x50, 0x800C9784);
    pos = list_copy(pos, 0x800B4BE8u, 0x50, 0x90, 0x800C9824);
    if (((((u32)(s32)(s16)pos) - 2) & 7) == 0) {
        GV(u8, LIST + (u32)(s32)(s16)pos) = 0xFF;
        pos++;
    }
    if ((s16)pos < 6) {
        for (;;) {
            s32 k = (s16)pos;
            if (k == 2) GV(u8, LIST + 2) = 0xFF;
            else GV(u8, LIST + (u32)k) = 0xFE;
            pos++;
            if (!((s16)pos < 6)) break;
            LOOP_POLL(0x800C98BC);
        }
    }
    GV(u16, LIST + 0xE0) = (u16)pos;
    V0 = 0;
}

/* 800C98D0 (x, y): the names box at (x, y): seven lines of item names
 * around the cursor (the middle one bracketed by 0xFC01/0xFC00, each
 * ended by 0xFFFE), the scroll bar (panel 2) and marker (panel 3). */
void n_status_ItemNames(void) {
    u32 t1 = A1 - 0x13;
    GV(u8, NAMES + 0x1AA) = 1;
    GV(u16, NAMES + 0x1BA) = (u16)A0;
    GV(u16, NAMES + 0x1BC) = (u16)A1;
    u32 t0 = 0;
    u32 v1 = INV_PANEL(8);
    if (v1 != 0xFF) {
        t0 = pnl(v1);
        GV(u16, t0 + 0xA) = 0;
    }
    for (u32 i = 0;;) {
        s32 k = (s16)i;
        GV(u16, NAMES + 0x1AC + (u32)k * 2) = (u16)t1;
        s32 cnt = LIST_COUNT;
        s32 r = mips_rem(LIST_CURSOR + k + (cnt - 3), cnt);
        if (t0 != 0 && (s16)r == GV(s16, g_curPanel + 0x2A) && k > 0 && k < 6) {
            u16 h = GV(u16, t0 + 0x22), v = GV(u16, t0 + 0x24);
            u16 y = (u16)((t1 - 1) << 4);
            GV(u16, t0 + 0x12) = y;
            GV(u16, t0 + 0x26) = y;
            GV(u16, t0 + 0xA) = h;
            GV(u16, t0 + 6) = v;
        }
        u32 it = GV(u8, LIST + (u32)(s32)(s16)r);
        u32 txt;
        if (it == 0xFF) txt = 0x800CF2ECu;
        else if (it == 0xFE) txt = 0x800CF300u;
        else txt = GV(u32, ITEM_LOOK_TAB + (u32)(s32)GV(s16, ITEMS_B + it * 8) * 4);
        u32 d = NAMES + (u32)k * 60;
        if (k == 3) { GV(u16, d) = 0xFC01; d += 2; }
        u16 c = GV(u16, txt);
        if (c != 0xFFFF) {
            for (;;) {
                txt += 2;
                GV(u16, d) = c;
                c = GV(u16, txt);
                d += 2;
                if (c == 0xFFFF) break;
                LOOP_POLL(0x800C9A64);
            }
        }
        if (k == 3) { GV(u16, d) = 0xFC00; d += 2; }
        GV(u16, d) = 0xFFFE;
        i++;
        t1 += 0x13;
        if (!((s16)i < 7)) break;
        LOOP_POLL(0x800C9AA4);
    }
    u32 bar = pnl(INV_PANEL(2));
    GV(u16, bar + PANEL_FLAGS) = GV(u16, bar + PANEL_FLAGS) | 8;
    GV(u16, bar + 0x14) = GV(u16, bar + 8);
    s32 step = mips_div(0x400, LIST_COUNT);
    GV(u16, bar + 0x16) = (u16)(step >> 4);
    s32 r = mips_rem(LIST_CURSOR + (LIST_COUNT - 2), LIST_COUNT);
    u16 h = GV(u16, bar + 0x16);
    GV(u16, bar + 0x12) = (u16)((u32)r * (u32)step - 0x380);
    u16 v6 = GV(u16, bar + 6), y = GV(u16, bar + 0x12);
    GV(u16, bar + 0x28) = (u16)step;
    GV(u16, bar + 0x22) = h;
    GV(u16, bar + 0x24) = v6;
    GV(u16, bar + 0x26) = y;
    u32 mk = INV_PANEL(3);
    GV(u16, pnl(mk) + 0xA) = 0;
    V0 = mk * PANEL_SIZE;
}

/* 800C9B74: panel callback: slide the item list in by +0x2E (the names
 * box follows); at -0x800 the input callback (800C9E80) takes over. */
void n_status_ItemsSlideIn(void) {
    FRAME(0x18);
    u32 p = g_curPanel;
    u32 d = (u32)(s32)GV(s16, p + 0x2E);
    if ((s32)GV(s16, p + 0x10) - (s32)GV(s16, p + 0x2E) < -0x7FF) {
        d = (u32)GV(u16, p + 0x10) + 0x800;
        GV(u32, p + 0x3C) = 0x800C9E80u;
    }
    p = g_curPanel;
    u32 x = (u32)GV(u16, p + 0x10) - d;
    GV(u16, p + 0x10) = (u16)x;
    GV(u16, 0x800A8832) = (u16)(((s32)(x << 16) >> 20) + 0xA);
    for (u32 i = 0;;) {
        u32 n = INV_PANEL((u32)(s32)(s16)i);
        if (n != 0xFF) {
            u32 q = pnl(n);
            GV(u16, q + 0x10) = (u16)(GV(u16, q + 0x10) - d);
        }
        i++;
        if (!((s16)i < 7)) break;
        LOOP_POLL(0x800C9C3C);
    }
    u32 ic = INV_PANEL(6);
    if (ic != 0xFF) JAL(0x800C9C84u, 0x800CAD84u, pnl(ic), (u32)(s32)(s16)(0u - d), 0, A3);
    s32 s = (s32)(d << 16) >> 20;
    p = g_curPanel;
    GV(u16, p + 0x2E) = (u16)(GV(u16, p + 0x2E) + (u32)s);
    V0 = (u32)s;
}

/* The icon of the item under the cursor (panel 6). */
static void cursor_icon(u32 ra, u32 a2) {
    u32 it = GV(u8, LIST + (u32)(s32)LIST_CURSOR);
    if (it == 0xFF) { INV_PANEL(6) = (u8)it; return; }
    u32 v = JAL(ra, 0x800C94C4u, it, 0x500, a2, 6);
    INV_PANEL(6) = (u8)v;
    GV(u16, pnl(v & 0xFF) + 0xA) = 0;
}

/* The start of a scroll: 8 frames, the list's callback cb; in the item
 * box, the box's marker. */
static void scroll_start(u32 ra_stop, u32 ra_snd, u32 cb, u32 ra_box, s32 adj, int up) {
    JAL(ra_stop, F_SOUND_STOP, A0, A1, A2, A3);
    sound(ra_snd, 0);
    u32 p = g_curPanel;
    GV(u16, p + 0x1A) = 8;
    s32 step = mips_div(0x134, GV(s16, p + 0x1A));
    u8 box = ST_ITEMBOX;
    GV(u16, p + 0x10) = 0;
    GV(u32, p + 0x3C) = cb;
    GV(u16, p + 0x2E) = (u16)step;
    if (box != 0) {
        s32 a0 = GV(s16, p + 0x2A);
        ST_SCROLLED = 0;
        u32 v = JAL(ra_box, 0x800CA66Cu, (u32)a0, A1, A2, A3);
        if ((s16)v > 0) {
            ST_SCROLLED = 1;
        } else {
            s32 r = mips_rem(LIST_CURSOR + (LIST_COUNT + adj), LIST_COUNT);
            if ((s16)r == GV(s16, g_curPanel + 0x2A)) {
                u32 q = pnl(INV_PANEL(8));
                if (up) {
                    ST_SCROLLED = 1;
                    GV(u16, q + 0xA) = 0;
                    GV(u16, q + 0x12) = 0x180;
                    GV(u16, q + 0x26) = 0x180;
                } else {
                    u16 h = GV(u16, q + 0x22);
                    ST_SCROLLED = 1;
                    GV(u16, q + 0xA) = 0;
                    u16 y = (u16)(0xFFFFFBA0u - ((u32)h << 4));
                    GV(u16, q + 0x12) = y;
                    GV(u16, q + 0x26) = y;
                }
            }
        }
    }
}

/* After the cursor moved: the old icon is kept for the scroll (panel
 * +0x22), the scroll bar's step. */
static void scroll_setup(void) {
    u32 p = g_curPanel;
    s32 v = mips_div(0x280, GV(s16, p + 0x1A));
    GV(u16, p + 0x22) = INV_PANEL(6);
    GV(u16, p + 0x24) = (u16)v;
    u32 bar = pnl(INV_PANEL(2));
    GV(u16, bar + 0x30) = (u16)mips_div(GV(s16, bar + 0x28), GV(s16, p + 0x1A));
}

/* 800C9E80: panel callback: the item list's input. The prompts follow a
 * pad type change; OK on an item opens its command menu (1, or 2 for the
 * carried items; in the item box 800C7AE0), on an empty place buzzes,
 * on the end closes; up/down scroll the list (8 frames); cancel closes
 * (in the item box, puts the list back first). */
void n_status_ItemsInput(void) {
    FRAME(0x28);
    ST_SOUND_ON = 0;
    s16 cancel = ST_CANCEL;
    ST_SOUND = 0;
    if (cancel != 0) goto cancelled;
    {
        u32 t = g_padTypeBit;
        if (t != 0x8000 && ST_PADTYPE != t) {
            GV(u16, pnl(INV_PANEL(4)) + PANEL_FLAGS) = 1;
            GV(u16, pnl(INV_PANEL(5)) + PANEL_FLAGS) = 1;
            JAL(0x800C9F1Cu, 0x800C92E0u, A0, A1, A2, A3);
            GV(u16, pnl(INV_PANEL(4)) + 0x10) = 0x1C0;
            GV(u16, pnl(INV_PANEL(5)) + 0x10) = 0x1C0;
        }
    }
    if (BTN(0x800C9F64u, 0x20) && !BTN_PREV(0x800C9F7Cu, 0x20)) {
        u32 it = GV(u8, LIST + (u32)(s32)LIST_CURSOR);
        if (it == 0xFE) {
            sound(0x800C9FE4u, 4);
        } else if (it == 0xFF) {
            if (ST_ITEMBOX != 0) goto box_close;
            goto close;
        } else {
            if (ST_ITEMBOX != 0) {
                GV(u32, g_curPanel + 0x3C) = 0x800C7AE0u;
                return;
            }
            MENU_ID = (it - 0x50) < 0x40 ? 2 : 1;
            GV(u16, MENU + 6) = 0;
            sound(0x800CA050u, 2);
            u32 p = g_curPanel;
            GV(u16, p + 0x2E) = 0x280;
            GV(u32, p + 0x38) = 0x800C50A8u;
            GV(u32, p + 0x3C) = 0x800C74CCu;
            return;
        }
    }
    if (BTN(0x800CA088u, 0x5FF)) {
        scroll_start(0x800CA09Cu, 0x800CA0B0u, 0x800CA6E0u, 0x800CA0FCu, 3, 1);
        u16 c = (u16)(GV(u16, LIST + 0xE2) + 1);
        GV(u16, LIST + 0xE2) = c;
        if (!((s16)c < LIST_COUNT)) GV(u16, LIST + 0xE2) = 0;
        scroll_setup();
        cursor_icon(0x800CA428u, 0x2C0);
        return;
    }
    if (BTN(0x800CA250u, 0x6FF)) {
        scroll_start(0x800CA264u, 0x800CA278u, 0x800CAA2Cu, 0x800CA2C4u, -3, 0);
        u16 c = (u16)(GV(u16, LIST + 0xE2) - 1);
        GV(u16, LIST + 0xE2) = c;
        if ((s16)c < 0) GV(u16, LIST + 0xE2) = (u16)(GV(u16, LIST + 0xE0) - 1);
        scroll_setup();
        cursor_icon(0x800CA428u, (u32)-0x240);
        return;
    }
    if (!BTN(0x800CA45Cu, 0x7FF)) return;
    if (BTN_PREV(0x800CA474u, 0x7FF)) return;
cancelled:
    ST_CANCEL = 0;
    JAL(0x800CA48Cu, F_SOUND_STOP, A0, A1, A2, A3);
    if (ST_ITEMBOX == 0) goto close;
box_close:
    sound(0x800CA4B0u, 3);
    {
        u32 n = INV_PANEL(8);
        if (n != 0xFF) {
            GV(u16, pnl(n) + PANEL_FLAGS) = 1;
            INV_PANEL(8) = 0xFF;
        }
        n = INV_PANEL(7);
        if (n != 0xFF) {
            JAL(0x800CA514u, 0x800C4858u, pnl(n), A1, A2, A3);
            GV(u16, pnl(INV_PANEL(7)) + PANEL_FLAGS) = 1;
            INV_PANEL(7) = 0xFF;
        }
        n = INV_PANEL(6);
        if (n != 0xFF) {
            JAL(0x800CA560u, 0x800C4858u, pnl(n), A1, A2, A3);
            GV(u16, pnl(INV_PANEL(6)) + PANEL_FLAGS) = 1;
            INV_PANEL(6) = 0xFF;
        }
        u16 c = GV(u16, g_curPanel + 0x2A);
        GV(u16, LIST + 0xE2) = c;
        u32 it = GV(u8, LIST + (u32)(s32)(s16)c);
        if (it != 0xFF) {
            u32 v = JAL(0x800CA5C4u, 0x800C94C4u, it, 0x500, 0x2C0, 6);
            INV_PANEL(6) = (u8)v;
            GV(u16, pnl(INV_PANEL(6)) + 0xA) = 0;
        }
        GV(u32, NAMES + 0x1A4) = 0x50000000u;
        GV(u8, NAMES + 0x1AB) = 0x50;
        u32 p = g_curPanel;
        GV(u16, p + 0x1A) = 8;
        GV(u16, p + 0x22) = 0xA;
        GV(u32, p + 0x3C) = 0x800C8880u;
        return;
    }
close:
    sound(0x800CA630u, 3);
    {
        u32 p = g_curPanel;
        GV(u16, p + 0x2E) = 0x280;
        GV(u16, p + 0x10) = 0xF800;
        GV(u32, p + 0x3C) = 0x800C9CB0u;
    }
}

/* 800CA66C (place): the row (1 to 5) of the names box showing that list
 * place, or -1. */
void n_status_MarkerRow(void) {
    s32 place = (s16)A0;
    s32 cnt = LIST_COUNT, cur = LIST_CURSOR;
    for (u32 i = 1;;) {
        s32 k = (s16)i;
        if ((s16)mips_rem(cur + k + (cnt - 3), cnt) == place) { V0 = (u32)k; return; }
        i++;
        if (!((s16)i < 6)) { V0 = (u32)-1; return; }
        LOOP_POLL(0x800CA6D0);
    }
}

/* 800CA6E0: panel callback: the list scrolling up, a frame (+0x1A frames
 * left): the names box lines, the box marker, the old and new icons and
 * their counts clipped to the list, the scroll bar; then the names are
 * rewritten, the old icon freed, the new one placed. */
void n_status_ScrollUp(void) {
    FRAME(0x28);
    u32 p = g_curPanel;
    u16 left = (u16)(GV(u16, p + 0x1A) - 1);
    GV(u16, p + 0x1A) = left;
    if ((s16)left <= 0) {
        JAL(0x800CA728u, 0x800C98D0u, (u32)-0x76, (u32)-0x46, A2, A3);
        u32 a0 = g_curPanel;
        GV(u32, a0 + 0x3C) = 0x800C9E80u;
        u32 q = pnl(INV_PANEL(5));
        GV(u16, q + 0xE) = GV(u16, q + 0x22);
        s32 old = GV(s16, a0 + 0x22);
        if (old != 0xFF) {
            q = pnl_s(old);
            JAL(0x800CA790u, 0x800C4858u, q, A1, A2, A3);
            GV(u16, q + PANEL_FLAGS) = 1;
        }
        u32 ic = INV_PANEL(6);
        if (ic == 0xFF) return;
        q = pnl(ic);
        u16 h = GV(u16, q + 0x22);
        GV(u16, q + 0xA) = h;
        u16 y = (u16)(0x40 - ((u32)h << 3));
        GV(u16, q + 0x12) = y;
        GV(u16, q + 0x26) = y;
        JAL(0x800CA7E0u, 0x800CAE78u, q, 0x40, A2, A3);
        return;
    }
    u16 x = GV(u16, p + 0x10);
    u32 nx = (u32)x + GV(u16, p + 0x2E);
    s32 d = ((s32)((u32)x << 16) >> 20) - ((s32)(nx << 16) >> 20);
    GV(u16, p + 0x10) = (u16)nx;
    for (u32 i = 0;;) {
        u32 a = NAMES + (u32)(s32)(s16)i * 2;
        i++;
        GV(u16, a + 0x1AC) = (u16)(GV(u16, a + 0x1AC) + (u32)d);
        if (!((s16)i < 7)) break;
        LOOP_POLL(0x800CA83C);
    }
    if (ST_ITEMBOX != 0 && ST_SCROLLED != 0) {
        u32 q = pnl(INV_PANEL(8));
        GV(u16, q + 0x26) = (u16)(GV(u16, q + 0x26) + ((u32)d << 4));
        JAL(0x800CA8A4u, 0x800CAEECu, q, (u32)-0x460, 0x180, A3);
    }
    u32 cur = g_curPanel;
    u16 dy = GV(u16, cur + 0x24);
    s32 old = GV(s16, cur + 0x22);
    if (old != 0xFF) {
        u32 q = pnl_s(old);
        GV(u16, q + 0x26) = (u16)(GV(u16, q + 0x26) - dy);
        JAL(0x800CA8F4u, 0x800CAEECu, q, (u32)-0x100, 0x180, A3);
        JAL(0x800CA90Cu, 0x800CAD84u, q, (u32)(s32)(s16)(0u - dy), 1, A3);
    }
    u32 ic = INV_PANEL(6);
    if (ic != 0xFF) {
        u32 q = pnl(ic);
        GV(u16, q + 0x26) = (u16)(GV(u16, q + 0x26) - dy);
        JAL(0x800CA954u, 0x800CAEECu, q, (u32)-0x100, 0x180, A3);
        JAL(0x800CA96Cu, 0x800CAD84u, q, (u32)(s32)(s16)(0u - dy), 1, A3);
    }
    u32 bar = pnl(INV_PANEL(2));
    GV(u16, bar + 0x26) = (u16)(GV(u16, bar + 0x26) + GV(u16, bar + 0x30));
    u32 cut = JAL(0x800CA9A8u, 0x800CAFB4u, bar, (u32)-0x380, 0x80, A3);
    if ((cut & 0xFFFF) != 0) {
        u32 q = pnl(INV_PANEL(3));
        GV(u16, q + 0x12) = 0xFC80;
        GV(u16, q + 0xA) = (u16)cut;
    }
    u32 q = pnl(INV_PANEL(5));
    s16 h = GV(s16, q + 0x22);
    u16 e = GV(u16, q + 0xE);
    GV(u16, q + 0xE) = (u16)((u32)e == (u32)(s32)h ? (u32)(s32)h - 1 : (u32)(s32)h);
    V0 = e;
}

/* 800CAA2C: panel callback: the list scrolling down, a frame: as
 * 800CA6E0 the other way (prompt panel 4). */
void n_status_ScrollDown(void) {
    FRAME(0x28);
    SAVE_SREG(0, 0x10);
    SAVE_RA(0x20);
    SAVE_SREG(3, 0x1C);
    SAVE_SREG(2, 0x18);
    SAVE_SREG(1, 0x14);
    u32 p = g_curPanel;
    u16 left = (u16)(GV(u16, p + 0x1A) - 1);
    GV(u16, p + 0x1A) = left;
    if ((s16)left <= 0) {
        JAL(0x800CAA74u, 0x800C98D0u, (u32)-0x76, (u32)-0x46, A2, A3);
        u32 a0 = g_curPanel;
        GV(u32, a0 + 0x3C) = 0x800C9E80u;
        u32 q = pnl(INV_PANEL(4));
        GV(u16, q + 0xE) = GV(u16, q + 0x22);
        s32 old = GV(s16, a0 + 0x22);
        if (old != 0xFF) {
            q = pnl_s(old);
            JAL(0x800CAADCu, 0x800C4858u, q, A1, A2, A3);
            GV(u16, q + PANEL_FLAGS) = 1;
        }
        u32 ic = INV_PANEL(6);
        if (ic == 0xFF) return;
        q = pnl(ic);
        GV(u16, q + 0xA) = GV(u16, q + 0x22);
        u16 h = GV(u16, q + 0xA);
        GV(u16, q + 6) = GV(u16, q + 0x24);
        u16 y = (u16)(0x40 - ((u32)h << 3));
        GV(u16, q + 0x12) = y;
        GV(u16, q + 0x26) = y;
        JAL(0x800CAB38u, 0x800CAE78u, q, 0x40, A2, A3);
        return;
    }
    u16 x = GV(u16, p + 0x10);
    u32 nx = (u32)x + GV(u16, p + 0x2E);
    s32 d = ((s32)((u32)x << 16) >> 20) - ((s32)(nx << 16) >> 20);
    GV(u16, p + 0x10) = (u16)nx;
    for (u32 i = 0;;) {
        u32 a = NAMES + (u32)(s32)(s16)i * 2;
        i++;
        GV(u16, a + 0x1AC) = (u16)(GV(u16, a + 0x1AC) - (u32)d);
        if (!((s16)i < 7)) break;
        LOOP_POLL(0x800CAB94);
    }
    if (ST_ITEMBOX != 0 && ST_SCROLLED != 0) {
        u32 q = pnl(INV_PANEL(8));
        GV(u16, q + 0x26) = (u16)(GV(u16, q + 0x26) - ((u32)d << 4));
        JAL(0x800CABFCu, 0x800CAEECu, q, (u32)-0x460, 0x180, A3);
    }
    u32 cur = g_curPanel;
    u16 dy = GV(u16, cur + 0x24);
    s32 old = GV(s16, cur + 0x22);
    if (old != 0xFF) {
        u32 q = pnl_s(old);
        GV(u16, q + 0x26) = (u16)(GV(u16, q + 0x26) + dy);
        JAL(0x800CAC4Cu, 0x800CAEECu, q, (u32)-0x100, 0x180, A3);
        JAL(0x800CAC60u, 0x800CAD84u, q, (u32)(s32)(s16)dy, 1, A3);
    }
    u32 ic = INV_PANEL(6);
    if (ic != 0xFF) {
        u32 q = pnl(ic);
        GV(u16, q + 0x26) = (u16)(GV(u16, q + 0x26) + dy);
        JAL(0x800CACA8u, 0x800CAEECu, q, (u32)-0x100, 0x180, A3);
        JAL(0x800CACBCu, 0x800CAD84u, q, (u32)(s32)(s16)dy, 1, A3);
    }
    u32 bar = pnl(INV_PANEL(2));
    GV(u16, bar + 0x26) = (u16)(GV(u16, bar + 0x26) - GV(u16, bar + 0x30));
    u32 cut = JAL(0x800CACF8u, 0x800CAFB4u, bar, (u32)-0x380, 0x80, A3);
    if ((cut & 0xFFFF) != 0) {
        u32 q = pnl(INV_PANEL(3));
        GV(u16, q + 0x12) = (u16)(0x80 - (cut << 4));
        GV(u16, q + 0xA) = (u16)cut;
    }
    u32 q = pnl(INV_PANEL(4));
    s16 h = GV(s16, q + 0x22);
    u16 e = GV(u16, q + 0xE);
    GV(u16, q + 0xE) = (u16)((u32)e == (u32)(s32)h ? (u32)(s32)h - 1 : (u32)(s32)h);
    V0 = e;
}

/* 800CAD84 (panel, dy, clip): move a panel's count digits by dy (clipped
 * to the list like the icon when clip, else just across +0x10). */
void n_status_MoveCount(void) {
    FRAME(0x28);
    u32 s = GV(u32, A0 + 0x38);
    u32 dy = A1;
    s16 clip = (s16)A2;
    if (s == 0) return;
    for (u32 i = 0;;) {
        u32 n = GV(u8, s);
        if (n == 0xFF) return;
        if (clip != 0) {
            u32 q = pnl(n);
            GV(u16, q + 0x26) = (u16)(GV(u16, q + 0x26) + dy);
            JAL(0x800CAE08u, 0x800CAEECu, q, (u32)-0x100, 0x180, A3);
        } else {
            u32 q = pnl(GV(u8, s));
            GV(u16, q + 0x10) = (u16)(GV(u16, q + 0x10) + dy);
        }
        s++;
        i++;
        if (!((s16)i < 3)) return;
        LOOP_POLL(0x800CAE50);
    }
}

/* 800CAE78 (panel, y): place a panel's count digits at y + 0x40, whole. */
void n_status_PlaceCount(void) {
    u32 s = GV(u32, A0 + 0x38);
    if (s == 0) { V0 = 0; return; }
    u32 y = A1 + 0x40;
    for (u32 i = 0;;) {
        u32 n = GV(u8, s);
        if (n == 0xFF) { V0 = 0xFF; return; }
        s++;
        u32 q = pnl(n);
        i++;
        u16 h = GV(u16, q + 0x22);
        GV(u16, q + 0x12) = (u16)y;
        GV(u16, q + 0xA) = h;
        V0 = q;
        if (!((s16)i < 3)) return;
        LOOP_POLL(0x800CAEDC);
    }
}

/* 800CAEEC (panel, top, bottom): clip a panel (y +0x26, full height +0x22,
 * v +0x24) to the rows top..bottom: its shown y, v and height. */
void n_status_ClipPanel(void) {
    u32 p = A0;
    u32 t2 = GV(u16, p + 0x26);
    s32 t1 = (s16)(t2 + ((u32)GV(u16, p + 0x22) << 4));
    s32 top = (s16)A1;
    if (!(top < t1)) { GV(u16, p + 0xA) = 0; V0 = t2 << 16; return; }
    s32 t0 = (s16)t2, bot = (s16)A2;
    if (!(t0 < bot)) { GV(u16, p + 0xA) = 0; V0 = (u32)(top < t0); return; }
    if (!(top < t0)) {
        GV(u16, p + 0x12) = (u16)A1;
        s32 d = (top - GV(s16, p + 0x26)) >> 4;
        GV(u16, p + 6) = (u16)(GV(u16, p + 0x24) + (u32)d);
        GV(u16, p + 0xA) = (u16)(GV(u16, p + 0x22) - (u32)d);
        V0 = (u32)d;
        return;
    }
    if (!(t1 < bot)) {
        u16 v = GV(u16, p + 0x24);
        GV(u16, p + 0x12) = (u16)t2;
        GV(u16, p + 6) = v;
        s32 d = (t1 - bot) >> 4;
        GV(u16, p + 0xA) = (u16)(GV(u16, p + 0x22) - (u32)d);
        V0 = (u32)d;
        return;
    }
    u16 v = GV(u16, p + 0x24), h = GV(u16, p + 0x22);
    GV(u16, p + 0x12) = (u16)t2;
    GV(u16, p + 6) = v;
    GV(u16, p + 0xA) = h;
    V0 = v;
}

/* 800CAFB4 (bar, top, bottom): the same for the scroll bar (its height in
 * +0x16); returns how much was cut off (all of it when outside). */
void n_status_ClipBar(void) {
    u32 p = A0;
    u32 t2 = GV(u16, p + 0x26);
    s32 t0 = (s16)(t2 + ((u32)GV(u16, p + 0x22) << 4));
    s32 top = (s16)A1;
    s32 t1 = (s16)t2, bot = (s16)A2;
    if (!(top < t0) || !(t1 < bot)) {
        V0 = (u32)(s32)GV(s16, p + 0x22);
        GV(u16, p + 0x16) = 0;
        return;
    }
    if (!(top < t1)) {
        s32 d = (top - GV(s16, p + 0x26)) >> 4;
        u16 h = GV(u16, p + 0x22);
        GV(u16, p + 0x12) = (u16)A1;
        GV(u16, p + 0x16) = (u16)(h - (u32)d);
        V0 = (u32)d;
        return;
    }
    if (!(t0 < bot)) {
        s32 d = (t0 - bot) >> 4;
        u16 h = GV(u16, p + 0x22);
        GV(u16, p + 0x12) = (u16)t2;
        GV(u16, p + 0x16) = (u16)(h - (u32)d);
        V0 = (u32)d;
        return;
    }
    u16 h = GV(u16, p + 0x22);
    GV(u16, p + 0x12) = (u16)t2;
    GV(u16, p + 0x16) = h;
    V0 = 0;
}

/* 800CBF10 (buttons, which): is a menu button down, for the pad type in
 * use: which is a button (0x20 OK, 0x40, 0x800) or a direction / cancel
 * code (0x4FF, 0x5FF, 0x6FF, 0x7FF); a DualShock's left stick counts for
 * the directions, a mouse has its own buttons (by its setting at
 * 0x80062DD6). */
void n_status_Button(void) {
    FRAME(0x28);
    u32 b = A0;
    s32 w = (s16)A1;
    u32 t = g_padTypeBit;
    if (t == 0x200000) {
        if (w == 0x4FF) { V0 = (b & 0x840) == 0x840; return; }
        if (w == 0x20) { V0 = b & 0x20; return; }
        if (w == 0x40 || w == 0x5FF) {
            if (GV(s16, 0x80062DD6) == 0) {
                LOOP_POLL(0x800CC10C);
                V0 = b & 0x40;
            } else {
                V0 = b & 0x800;
            }
            return;
        }
        if (w == 0x6FF || w == 0x800) {
            V0 = GV(s16, 0x80062DD6) == 0 ? (b & 0x800) : (b & 0x40);
            return;
        }
        V0 = 0;
        return;
    }
    if (t == 0x400000) {
        u32 ax = JAL(0x800CBF70u, F_PAD_GET_AXES, 0, A1, A2, A3);
        GS(u32, sp + 0x10) = ax;
        u8 x = GS(u8, sp + 0x10), y = GS(u8, sp + 0x11);
        if (w == 0x5FF && x >= 0xB1) { V0 = 1; return; }
        if (w == 0x40 && y >= 0xB1) { V0 = 1; return; }
        if (w == 0x6FF && x < 0x50) { V0 = 1; return; }
        if (w == 0x800 && y < 0x50) { V0 = 1; return; }
    } else if (t != 0x80000) {
        V0 = 0;
        return;
    }
    switch (w) {
    case 0x20:  V0 = b & 0xA0; break;
    case 0x40:  V0 = b & 0x2000; break;
    case 0x5FF: V0 = b & 0x4000; break;
    case 0x6FF: V0 = b & 0x1000; break;
    case 0x7FF: V0 = b & 0x40; break;
    case 0x800: V0 = (u32)(s32)(s16)(b & 0x8000); break;
    default:    V0 = 0; break;
    }
}
