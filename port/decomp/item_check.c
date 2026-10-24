/* The status screen's Check command (CONPAU_E): the item under the list
 * cursor shown large: its picture (file by item id) zooms in from the
 * middle of the screen (panel in the list panel's +0x24, zoom in +0x34,
 * step +0x26), waits for OK or cancel, zooms out, back to the list.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "status.h"

void n_status_CheckOpen(void);
void n_status_CheckZoomIn(void);
void n_status_CheckWait(void);
void n_status_CheckZoomOut(void);
void n_status_CheckLoad(void);

#define CHECK_STATE    GV(s16, 0x800BA9FC)

/* The picture's size and place for its zoom (0x100 = full size), centred
 * on (-0x30, -0x18). */
static void zoom_place(u32 p, s32 z) {
    s32 w = ((s32)GV(u16, p + 8) * z) >> 8;
    s32 h = ((s32)GV(u16, p + 0xA) * z) >> 8;
    GV(u16, p + 0x14) = (u16)w;
    GV(u16, p + 0x16) = (u16)h;
    GV(u16, p + 0x10) = (u16)((0xFFD0u - (((u32)w & 0xFFFF) >> 1)) << 4);
    V0 = (0xFFE8u - ((u32)GV(u16, p + 0x16) >> 1)) << 4;
    GV(u16, p + 0x12) = (u16)V0;
}

/* 800C8BCC: menu action: Check: load the picture, the panel at 1/4 size;
 * the list panel zooms it in (800C8D70). */
void n_status_CheckOpen(void) {
    FRAME(0x20);
    SAVE_RA(0x1C);
    SAVE_SREG(2, 0x18);
    SAVE_SREG(1, 0x14);
    SAVE_SREG(0, 0x10);
    CHECK_STATE = 0;
    sound(0x800C8BFCu, 2);
    GV(u8, 0x800A8822) = 0;
    u32 q = pnl(INV_PANEL(0));
    GV(u16, q + 0xE) = (u16)(GV(u16, q + 0xE) + 1);
    q = pnl(INV_PANEL(1));
    GV(u16, q + PANEL_FLAGS) = GV(u16, q + PANEL_FLAGS) & 0xFFFB;
    s32 id = GV(s16, ITEMS_B + (u32)GV(u8, LIST + (u32)(s32)LIST_CURSOR) * 8);
    JAL(0x800C8C8Cu, 0x800CB0B4u, (u32)id, A1, A2, A3);
    GV(u32, g_curPanel + 0x3C) = 0x800C8D70u;
    u32 n = JAL(0x800C8CA0u, F_ALLOC_PANEL, A0, A1, A2, A3);
    if ((s16)n == -1) return;
    u32 p = pnl_s((s16)n);
    GV(u16, g_curPanel + 0x24) = (u16)n;
    JAL(0x800C8CDCu, F_SET_SPRITE, p, 0x800A6168u, A2, A3);
    GV(u16, p + 0x34) = 0x40;
    GV(u16, p + 0x26) = 0x40;
    GV(u16, p + 0x18) = 0;
    GV(u16, p + PANEL_FLAGS) = GV(u16, p + PANEL_FLAGS) | 0x16C;
    zoom_place(p, 0x40);
}

/* 800C8D70: panel callback: zoom in by a growing step; at full size wait
 * (800C8E58). */
void n_status_CheckZoomIn(void) {
    u32 c = g_curPanel;
    u32 p = pnl_s(GV(s16, c + 0x24));
    s32 z = GV(s16, p + 0x34);
    u32 nz = (u32)z + GV(u16, p + 0x26);
    if (z + GV(s16, p + 0x26) >= 0x100) {
        nz = 0x100;
        GV(u32, c + 0x3C) = 0x800C8E58u;
        GV(u16, p + 0x34) = 0x100;
        GV(u16, p + 0x26) = 0x10;
    } else {
        u16 d = GV(u16, p + 0x26);
        GV(u16, p + 0x34) = (u16)nz;
        GV(u16, p + 0x26) = (u16)(d + 0x10);
    }
    zoom_place(p, (s16)nz);
}

/* 800C8E58: panel callback: OK or cancel (or a mouse's two buttons): the
 * click, the music stopped, zoom out (800C8F44). */
void n_status_CheckWait(void) {
    FRAME(0x18);
    SAVE_RA(0x14);
    SAVE_SREG(0, 0x10);
    s16 st = CHECK_STATE;
    if (st != 0 && st != 2) return;
    if (ST_CANCEL == 0) {
        if (!(BTN(0x800C8EA0u, 0x20) && !BTN_PREV(0x800C8EB8u, 0x20))) {
            if (!BTN(0x800C8ED0u, 0x7FF)) return;
            if (BTN_PREV(0x800C8EE8u, 0x7FF)) return;
        }
    }
    ST_CANCEL = 0;
    sound(0x800C8F0Cu, 3);
    ST_SOUND_ON = 0;
    ST_SOUND = 0;
    GV(u32, g_curPanel + 0x3C) = 0x800C8F44u;
    JAL(0x800C8F34u, F_SOUND_STOP, A0, A1, A2, A3);
}

/* 800C8F44: panel callback: zoom out by a growing step; when small, free
 * it and back to the list (800C90B8). */
void n_status_CheckZoomOut(void) {
    FRAME(0x18);
    SAVE_RA(0x10);
    u32 p = pnl_s(GV(s16, g_curPanel + 0x24));
    u16 z = GV(u16, p + 0x34), d = GV(u16, p + 0x26);
    if (GV(s16, p + 0x34) - GV(s16, p + 0x26) < 0x11) {
        GV(u16, p + PANEL_FLAGS) = 1;
        GV(u8, 0x800A8822) = 1;
        u32 q = pnl(INV_PANEL(0));
        GV(u16, q + 0xE) = (u16)(GV(u16, q + 0xE) - 1);
        q = pnl(INV_PANEL(1));
        GV(u16, q + PANEL_FLAGS) = GV(u16, q + PANEL_FLAGS) | 4;
        JAL(0x800C8FF4u, 0x800C90B8u, INVP, 1, p, PANELS);
        return;
    }
    u32 nz = (u32)z - d;
    GV(u16, p + 0x34) = (u16)nz;
    GV(u16, p + 0x26) = (u16)(GV(u16, p + 0x26) + 0x10);
    zoom_place(p, (s16)nz);
}

/* 800CB0B4 (item): load the item's picture (file B by item) into
 * 0x800D28B0, its image into VRAM; its sound data. */
void n_status_CheckLoad(void) {
    FRAME(0x20);
    SAVE_SREG(0, 0x18);
    SAVE_RA(0x1C);
    GS(u32, sp + 0x10) = 0x800D28B0u;
    JAL(0x800CB0F0u, F_FILE_LOAD, 0x800C3EC8u, 0x800585C8u, 0x800CEF80u, (u32)(s32)(s16)A0);
    JAL(0x800CB0FCu, 0x8006F370u, GV(u32, 0x800D28B4) + 0x800D28B0u, A1, A2, A3);
    V0 = GV(u32, 0x800D28B8) + 0x800D28B0u;
    ST_SOUND = V0;
}
