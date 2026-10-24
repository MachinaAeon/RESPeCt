/* The status screen's map (CONPAU_E): a floor's map image (file M by
 * floor; the floors are 12-byte entries from the list panel's +0x38:
 * +0 the floor's rooms, +8 the file), its rooms shaded in the map's
 * palette (visited, the one the player is in, by the area flags), a
 * marker where the player stands (on the floor with the current room),
 * arrows to the floors above and below (0x800CFDFC, 0x800CFDFE).
 * Up/down slide to the next floor (the list panel's +0x24 the floor,
 * +0x2C the direction, +0x26 the map panel, +0x2A the marker).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "status.h"

void n_map_SlideIn(void);
void n_map_SlideFloor(void);
void n_map_ShadeRooms(void);
void n_map_LoadFloor(void);
void n_map_Arrows(void);
void n_map_Input(void);

#define MAP_ARROW(i)    GV(s16, 0x800CFDFC + (u32)(i) * 2)  /* the arrow panels, -1 none */
#define MAP_ARROW_U(i)  GV(u16, 0x800CFDFC + (u32)(i) * 2)
#define MAP_PROMPT(i)   GV(s16, 0x800CFDF8 + (u32)(i) * 2)  /* the button prompt panels */
#define CUR_ROOM        GV(s32, 0x800B3F08)
#define AREA_MODE       GV(s32, 0x800A866C)
#define PAL             0x800CF9F8u                         /* the map's palette (256 colours) */
#define PAL_ROOMS       (PAL + 0x20)                        /* per room, 4 bytes in 8 */
#define PAL_COPY        (PAL + 0x200)
#define F_SET_DISP_MASK_ 0x800352DCu
#define F_STORE_IMAGE   0x80035680u
#define F_ROOM_FLAG     0x8008F64Cu                         /* (flags, n): set? */

/* Move the map panel (and the marker) across by d: +, or - (sub). */
static void slide_marker(u32 c, u32 d, int sub) {
    s32 m = GV(s16, c + 0x2A);
    if (m == -1) return;
    u32 q = pnl_s(m);
    GV(u16, q + 0x10) = (u16)(sub ? GV(u16, q + 0x10) - d : GV(u16, q + 0x10) + d);
}

/* 800CC2E4: panel callback: slide the new floor's map in (from the right,
 * or with +0x2C from the left) to -0x800, the step (+0x2E) shrinking;
 * then the arrows (800CCD58). */
void n_map_SlideIn(void) {
    u32 c = g_curPanel;
    u32 p = pnl_s(GV(s16, c + 0x26));
    u32 d = GV(u16, p + 0x2E);
    s32 ds = (s16)d;
    if (GV(s16, c + 0x2C) == 0) {
        u16 x = GV(u16, p + 0x10);
        if (GV(s16, p + 0x10) - ds < -0x7FF) goto arrived;
        GV(u16, p + 0x10) = (u16)(x - d);
        GV(u16, p + 0x2E) = (u16)(GV(u16, p + 0x2E) - (u32)((s32)(d << 16) >> 20));
        slide_marker(c, d, 1);
        return;
    }
    {
        u16 x = GV(u16, p + 0x10);
        if (!(GV(s16, p + 0x10) + ds < -0x800)) goto arrived;
        GV(u16, p + 0x10) = (u16)(x + d);
        GV(u16, p + 0x2E) = (u16)(GV(u16, p + 0x2E) - (u32)((s32)(d << 16) >> 20));
        slide_marker(c, d, 0);
        return;
    }
arrived:
    GV(u32, c + 0x3C) = 0x800CCD58u;
    d = 0xFFFFF800u - GV(u16, p + 0x10);
    GV(u16, p + 0x10) = (u16)-0x800;
    slide_marker(c, d, 0);
}

/* 800CC424: panel callback: slide the floor's map out (left, or with +0x2C
 * right); off the screen, free it and the marker and load the next floor
 * (800CC8F4). */
void n_map_SlideFloor(void) {
    FRAME(0x18);
    SAVE_RA(0x10);
    u32 c = g_curPanel;
    u32 p = pnl_s(GV(s16, c + 0x26));
    u32 d = GV(u16, p + 0x2E);
    s32 ds = (s16)d;
    if (GV(s16, c + 0x2C) == 0) {
        u16 x = GV(u16, p + 0x10);
        if (!(GV(s16, p + 0x10) - ds < -0x18BF)) {
            GV(u16, p + 0x10) = (u16)(x - d);
            GV(u16, p + 0x2E) = (u16)(GV(u16, p + 0x2E) - (u32)((s32)(d << 16) >> 20));
            slide_marker(c, d, 1);
            return;
        }
    } else {
        u16 x = GV(u16, p + 0x10);
        if (GV(s16, p + 0x10) + ds < 0x8C0) {
            GV(u16, p + 0x10) = (u16)(x + d);
            GV(u16, p + 0x2E) = (u16)(GV(u16, p + 0x2E) - (u32)((s32)(d << 16) >> 20));
            slide_marker(c, d, 0);
            return;
        }
    }
    GV(u16, p + PANEL_FLAGS) = 1;
    s32 m = GV(s16, c + 0x2A);
    if (m != -1) {
        GV(u16, pnl_s(m) + PANEL_FLAGS) = 1;
        GV(u16, c + 0x2A) = 0xFFFF;
    }
    JAL(0x800CC518u, 0x800CC8F4u, (u32)(s32)GV(s16, g_curPanel + 0x24), A1, A2, A3);
}

/* Divide by 1000 as GCC does (signed). */
static s32 div1000(s32 v) {
    s32 hi = (s32)(((s64)v * 0x10624DD3) >> 32);
    return (hi >> 7) - (v >> 31);
}

/* 800CC570 (rooms, stride): shade the floor's rooms in its palette (read
 * back from VRAM, a copy kept): a room seen (its flags, 0x8008F64C) or
 * the one the player is in gets its colours, the others none (when
 * 0x800C3F18 is set); special cases by area mode; the palette back into
 * VRAM. Not in area modes 5 and 6. */
void n_map_ShadeRooms(void) {
    FRAME(0x58);
    SAVE_RA(0x54);
    GS(u32, sp + 0x50) = R[30];
    SAVE_SREG(7, 0x4C);
    SAVE_SREG(6, 0x48);
    SAVE_SREG(5, 0x44);
    SAVE_SREG(4, 0x40);
    SAVE_SREG(3, 0x3C);
    SAVE_SREG(2, 0x38);
    SAVE_SREG(1, 0x34);
    SAVE_SREG(0, 0x30);
    u32 rooms = A0;
    GS(u16, sp + 0x20) = (u16)A1;
    if ((u32)(AREA_MODE - 5) < 2) return;
    u32 r = sp + 0x10;
    GS(u16, r) = 0x140;
    GS(u16, r + 2) = 0x1EF;
    GS(u16, r + 4) = 0x100;
    GS(u16, r + 6) = 1;
    JAL(0x800CC5ECu, F_STORE_IMAGE, r, PAL, A2, A3);
    for (u32 o = 0;; o += 0x10) {
        for (u32 j = 0; j < 0x10; j += 4) GV(u32, PAL_COPY + o + j) = GV(u32, PAL + o + j);
        if (o + 0x10 == 0x200) break;
        LOOP_POLL(0x800CC678);
    }
    u32 s6 = PAL_ROOMS, s7 = PAL_ROOMS + 0x200;
    if (GV(s16, rooms) != -1) {
        for (;;) {
            u16 room = GV(u16, rooms);
            s32 rs = (s16)room;
            u32 e = GV(u32, 0x800B3D28) + (u32)rs * 8;
            u32 zone = GV(u32, e);
            s16 nz = GV(s16, e + 4);
            u32 seen = 0;
            if (CUR_ROOM == rs) seen = 1;
            else if (nz > 0) {
                for (s32 i = 0;;) {
                    GS(u32, sp + 0x28) = 0x800B0000u;
                    u32 v = JAL(0x800CC710u, F_ROOM_FLAG, 0x80062FE0u, GV(u32, zone + 0x10) & 0xFFFFFF, A2, A3);
                    i++;
                    if (v != 0) { seen = 1; break; }
                    if (!(i < nz)) break;
                    zone += 0x18;
                    LOOP_POLL(0x800CC720);
                }
            }
            u32 o = (u32)rs * 8;
            if ((seen & 0xFF) == 0) {
                if (GV(u8, 0x800C3F18) != 0) {
                    GV(u16, s7 + o + 2) = 0;
                    GV(u16, s6 + o + 2) = 0;
                    GV(u16, s7 + o + 4) = 0;
                    GV(u16, s6 + o + 4) = 0;
                }
            } else {
                u16 c2, c4;
                if (rs == CUR_ROOM) { c2 = 0xD9B; c4 = 0xC; }
                else { c2 = 0x6D83; c4 = 0x40A0; }
                GV(u16, s7 + o + 2) = c2;
                GV(u16, s6 + o + 2) = c2;
                GV(u16, s7 + o + 4) = c4;
                GV(u16, s6 + o + 4) = c4;
            }
            rooms += (u32)((s32)(s16)GS(u16, sp + 0x20) * 2);
            if (GV(s16, rooms) == -1) break;
            LOOP_POLL(0x800CC7DC);
        }
    }
    s32 am = AREA_MODE;
    if (am == 7) {
        GV(u16, s7 + 0x2A) = 0;
        GV(u16, s6 + 0x2A) = 0;
        GV(u16, s7 + 0x2C) = 0;
        GV(u16, s6 + 0x2C) = 0;
    } else if (am == 0xD && CUR_ROOM == 6) {
        GV(u16, s7 + 0x2A) = 0xD9B;
        GV(u16, s6 + 0x2A) = 0xD9B;
        GV(u16, s7 + 0x2C) = 0xC;
        GV(u16, s6 + 0x2C) = 0xC;
        for (u32 i = 0;;) {
            u32 o = (u32)(s32)(s16)i * 8;
            i++;
            GV(u16, s7 + o + 2) = 0x6D83;
            GV(u16, s6 + o + 2) = 0x6D83;
            GV(u16, s7 + o + 4) = 0x40A0;
            GV(u16, s6 + o + 4) = 0x40A0;
            if (!((s16)i < 5)) break;
            LOOP_POLL(0x800CC87C);
        }
    }
    u32 rr = sp + 0x18;
    GS(u32, rr) = GV(u32, 0x800CF580);
    GS(u32, rr + 4) = GV(u32, 0x800CF584);
    GS(u16, rr + 6) = 2;
    JAL(0x800CC8C4u, F_LOAD_IMAGE, rr, PAL, A2, A3);
}

/* 800CC8F4 (floor): show a floor: clear the map image (screen off), load
 * its file, the image into VRAM, shade its rooms, the player's marker
 * (on the floor of the current room: placed by the player's position,
 * turned with him), the map panel; it slides in (800CC2E4, run now). */
void n_map_LoadFloor(void) {
    FRAME(0x38);
    SAVE_RA(0x34);
    SAVE_SREG(4, 0x30);
    SAVE_SREG(3, 0x2C);
    SAVE_SREG(2, 0x28);
    SAVE_SREG(1, 0x24);
    SAVE_SREG(0, 0x20);
    s32 floor = (s16)A0;
    u32 c = g_curPanel;
    u32 r = sp + 0x18;
    GS(u32, r) = GV(u32, 0x800C3F1C);
    GS(u32, r + 4) = GV(u32, 0x800C3F20);
    GV(u16, c + 0x2A) = 0xFFFF;
    u32 fe = GV(u32, c + 0x38) + (u32)floor * 12;
    u16 file = GV(u16, fe + 8);
    JAL(0x800CC978u, F_SET_DISP_MASK_, 0, A1, A2, A3);
    JAL(0x800CC98Cu, F_CLEAR_IMAGE, r, 0, 0, 0);
    GS(u32, sp + 0x10) = 0x800D28B0u;
    JAL(0x800CC9B4u, F_FILE_LOAD, 0x800C3F24u, 0x80058578u, 0x800CF4CCu, (u32)(s32)(s16)file);
    JAL(0x800CC9BCu, F_DRAW_SYNC, 0, A1, A2, A3);
    JAL(0x800CC9C4u, 0x8006F370u, 0x800D28B0u, A1, A2, A3);
    u32 rooms = GV(u32, fe);
    JAL(0x800CC9D4u, 0x800CC570u, rooms, 7, A2, A3);
    if (GV(s16, rooms) != -1) {
        u32 pl = g_player;
        u32 e = rooms;
        for (;;) {
            if ((s32)GV(s16, e) == CUR_ROOM) {
                u32 n = JAL(0x800CCA10u, F_ALLOC_PANEL, A0, A1, A2, A3);
                if ((s16)n == -1) break;
                u32 m = pnl_s((s16)n);
                GV(u16, g_curPanel + 0x2A) = (u16)n;
                GV(u16, m + PANEL_FLAGS) = 0x3124;
                JAL(0x800CCA60u, F_SET_SPRITE, m, 0x800A6050u, A2, A3);
                GV(u16, m + 4) = (u16)(GV(u16, m + 4) + GV(u16, 0x800CF56C));
                GV(u16, m + 6) = (u16)(GV(u16, m + 6) + GV(u16, 0x800CF56E));
                GV(u16, m + 8) = GV(u16, 0x800CF570);
                GV(u16, m + 0xA) = GV(u16, 0x800CF572);
                /* the player's offset in the room, map pixels (1/1000 * 8);
                 * +0xA the room's axes swapped */
                u32 s3 = 0, s2 = 0;
                s32 rx = GV(s16, e + 2), px = GV(s32, pl + 0x18);
                s32 dx = div1000((s32)((u32)(rx < px ? px - rx : rx - px) << 3));
                if (GV(s16, e + 0xA) != 0) s3 = (u32)dx; else s2 = (u32)dx;
                s32 rz = GV(s16, e + 4), pz = GV(s32, pl + 0x20);
                s32 dz = div1000((s32)((u32)(rz < pz ? pz - rz : rz - pz) << 3));
                if (GV(s16, e + 0xA) != 0) s2 = (u32)dz; else s3 = (u32)dz;
                u16 x;
                if (GV(s16, g_curPanel + 0x2C) == 0) x = (u16)((((u32)GV(u16, e + 6) + s2) << 4) + 0x8C0);
                else x = (u16)((((u32)GV(u16, e + 6) + s2) << 4) - 0x18C0);
                GV(u16, m + 0x10) = x;
                u16 ry = GV(u16, e + 8), w = GV(u16, m + 8);
                GV(u16, m + 0x24) = 4;
                GV(u16, m + 0x18) = 0;
                GV(u16, m + 0x12) = (u16)((((u32)ry + s3) << 4) - 0x7C0);
                GV(u16, m + 0x22) = (u16)(GV(u16, m + 8) >> 1);
                u16 v = GV(u16, m + 0xE);
                GV(u16, m + 0x10) = (u16)(GV(u16, m + 0x10) - ((u32)w << 3));
                GV(u16, m + 0x28) = 0;
                GV(u16, m + 0x2A) = v;
                GV(u32, m + 0x38) = (u32)((s32)GV(s16, pl + 0x46) + GV(s16, e + 0xC)) * 360;
                break;
            }
            e += 0xE;
            if (GV(s16, e) == -1) break;
            LOOP_POLL(0x800CCC24);
        }
    }
    u32 n = JAL(0x800CCC34u, F_ALLOC_PANEL, A0, A1, A2, A3);
    if ((s16)n != -1) {
        u32 m = pnl_s((s16)n);
        GV(u16, g_curPanel + 0x26) = (u16)n;
        GV(u16, m + PANEL_FLAGS) = GV(u16, m + PANEL_FLAGS) | 0x1104;
        JAL(0x800CCC88u, F_SET_SPRITE, m, 0x800CF574u, A2, A3);
        GV(u16, m + 0x18) = 3;
        GV(u16, m + 0x10) = GV(s16, g_curPanel + 0x2C) != 0 ? (u16)-0x18C0 : 0x8C0;
        u16 v = GV(u16, m + 0xE);
        GV(u16, m + 0x12) = (u16)-0x780;
        GV(u16, m + 0x2E) = 0x280;
        GV(u32, m + 0x38) = 0;
        GV(u16, m + 0x22) = v;
    }
    GV(u32, g_curPanel + 0x3C) = 0x800CC2E4u;
    V0 = 0x800CC2E4u;
    jalr_ra(0x800CCCE0u, 0x800CC2E4u);
    u32 q = pnl_s(MAP_PROMPT(0));
    GV(u16, q + PANEL_FLAGS) = GV(u16, q + PANEL_FLAGS) & 0xFFFB;
    q = pnl_s(MAP_PROMPT(1));
    GV(u16, q + PANEL_FLAGS) = GV(u16, q + PANEL_FLAGS) & 0xFFFB;
}

/* A floor arrow panel at x (blinking from frame +0xE). */
static void new_arrow(u32 ra, u32 m, u16 x) {
    GV(u16, m + PANEL_FLAGS) = 0x1104;
    JAL(ra, F_SET_SPRITE, m, 0x800A5E5Cu, A2, A3);
    u16 e = GV(u16, m + 0xE);
    GV(u16, m + 0x18) = 0;
    GV(u16, m + 0x12) = 0;
    GV(u16, m + 0x10) = x;
    GV(u16, m + 0x22) = e;
    GV(u16, m + 0xE) = (u16)(e + 1);
}

/* 800CCD58: panel callback: the floor in place: arrows to the floors
 * below (on the lowest, the prompt instead) and above (the last has the
 * end mark -1 after it), the screen back on; input (800CCF64). */
void n_map_Arrows(void) {
    FRAME(0x20);
    SAVE_SREG(1, 0x14);
    SAVE_RA(0x18);
    SAVE_SREG(0, 0x10);
    u32 c = g_curPanel;
    MAP_ARROW_U(1) = 0xFFFF;
    MAP_ARROW_U(0) = 0xFFFF;
    if (GV(s16, c + 0x24) == 0) {
        u32 n = JAL(0x800CCD9Cu, F_ALLOC_PANEL, A0, A1, A2, A3);
        if ((s16)n != -1) {
            u32 m = pnl_s((s16)n);
            MAP_ARROW_U(1) = (u16)n;
            GV(u16, m + PANEL_FLAGS) = 0x1104;
            JAL(0x800CCDE4u, F_SET_SPRITE, m, 0x800A5E5Cu, A2, A3);
            u16 w = GV(u16, m + 8), e = GV(u16, m + 0xE);
            GV(u16, m + 0x18) = 0;
            GV(u16, m + 0x12) = 0;
            GV(u16, m + 0x10) = (u16)((0x8Cu - w) << 4);
            GV(u16, m + 0x22) = e;
            GV(u16, m + 0xE) = (u16)(e + 1);
        }
    } else {
        u32 q = pnl_s(MAP_PROMPT(1));
        GV(u16, q + PANEL_FLAGS) = GV(u16, q + PANEL_FLAGS) | 4;
    }
    c = g_curPanel;
    s32 next = (s16)(GV(u16, c + 0x24) + 1);
    u32 rooms = GV(u32, GV(u32, c + 0x38) + (u32)next * 12);
    if (rooms == 0xFFFFFFFFu) {
        u32 n = JAL(0x800CCE90u, F_ALLOC_PANEL, A0, A1, A2, A3);
        if ((s16)n != -1) {
            u32 m = pnl_s((s16)n);
            MAP_ARROW_U(0) = (u16)n;
            new_arrow(0x800CCED8u, m, (u16)-0x8C0);
        }
    } else {
        u32 q = pnl_s(MAP_PROMPT(0));
        GV(u16, q + PANEL_FLAGS) = GV(u16, q + PANEL_FLAGS) | 4;
    }
    c = g_curPanel;
    GV(u16, c + 0x1A) = 0xF;
    GV(u32, c + 0x3C) = 0x800CCF64u;
    JAL(0x800CCF50u, F_SET_DISP_MASK_, 1, A1, A2, A3);
}

/* Start sliding to another floor: the click, the other arrow freed. */
static void change_floor(u32 ra, s32 other) {
    sound(ra, 6);
    if (other != -1) GV(u16, pnl_s(other) + PANEL_FLAGS) = 1;
}

/* An arrow lit: the one pressed highlighted (+1), the other normal. */
static void light_arrows(s32 hi, s32 lo) {
    if (hi != -1) {
        u32 q = pnl_s(hi);
        GV(u16, q + 0xE) = (u16)(GV(u16, q + 0x22) + 1);
    }
    u32 q = pnl_s(lo);
    GV(u16, q + 0xE) = GV(u16, q + 0x22);
}

/* 800CCF64: panel callback: the map's input: up/down (when there is no
 * arrow that way) slide to the next floor (800CC424), else light the
 * arrows; the marker blinks; the prompts animate every 8 frames; cancel,
 * a keyboard, or OK with an arrow showing, closes the map (800CD4C0) and
 * back to the command menu (800C50A8). */
void n_map_Input(void) {
    FRAME(0x20);
    SAVE_RA(0x18);
    SAVE_SREG(1, 0x14);
    SAVE_SREG(0, 0x10);
    if (ST_CANCEL != 0) goto close;
    if (BTN(0x800CCF90u, 0x800) && !BTN(0x800CCFA8u, 0x40)) {
        if (MAP_ARROW(0) == -1) {
            change_floor(0x800CCFDCu, MAP_ARROW(1));
            u32 c = g_curPanel;
            GV(u32, c + 0x3C) = 0x800CC424u;
            u16 f = GV(u16, c + 0x24);
            GV(u16, c + 0x2C) = 1;
            GV(u16, c + 0x24) = (u16)(f + 1);
            GV(u16, pnl_s(GV(s16, c + 0x26)) + 0x2E) = 0x280;
        } else {
            light_arrows(MAP_ARROW(1), MAP_ARROW(0));
        }
    }
    if (BTN(0x800CD0C8u, 0x40) && !BTN(0x800CD0E0u, 0x800)) {
        if (MAP_ARROW(1) == -1) {
            change_floor(0x800CD114u, MAP_ARROW(0));
            u32 c = g_curPanel;
            u16 f = GV(u16, c + 0x24);
            GV(u32, c + 0x3C) = 0x800CC424u;
            GV(u16, c + 0x2C) = 0;
            GV(u16, c + 0x24) = (u16)(f - 1);
            GV(u16, pnl_s(GV(s16, c + 0x26)) + 0x2E) = 0x280;
        } else {
            light_arrows(MAP_ARROW(0), MAP_ARROW(1));
        }
    }
    {
        s32 mk = GV(s16, g_curPanel + 0x2A);
        if (mk != -1) {
            u32 m = pnl_s(mk);
            u16 t = (u16)(GV(u16, m + 0x28) + 1);
            GV(u16, m + 0x28) = t;
            if (!((s16)t < 0xA)) GV(u16, m + 0x28) = 0;
            GV(u16, m + 0xE) = (u16)(GV(u16, m + 0x2A) + (u32)div_pow2(GV(s16, m + 0x28), 1));
        }
    }
    {
        u32 c = g_curPanel;
        u16 t = (u16)(GV(u16, c + 0x1A) - 1);
        GV(u16, c + 0x1A) = t;
        if (t == 0) {
            GV(u16, c + 0x1A) = 8;
            u32 m = pnl_s(GV(s16, c + 0x26));
            u16 e = GV(u16, m + 0xE);
            u32 add;
            if ((u32)e == (u32)(s32)GV(s16, m + 0x22)) {
                GV(u16, m + 0xE) = (u16)(e + 1);
                add = 0x30;
            } else {
                GV(u16, m + 0xE) = (u16)(e - 1);
                add = 0;
            }
            u32 q = pnl_s(MAP_PROMPT(0));
            GV(u16, q + 4) = (u16)(GV(u16, q + 0x22) + add);
            q = pnl_s(MAP_PROMPT(1));
            GV(u16, q + 4) = (u16)(GV(u16, q + 0x22) + add);
        }
    }
    if (BTN(0x800CD380u, 0x7FF) && !BTN_PREV(0x800CD398u, 0x7FF)) goto close;
    if (g_padTypeBit == 0x8000) goto close;
    if (!BTN(0x800CD3C4u, 0x20)) return;
    if (BTN_PREV(0x800CD3DCu, 0x20)) return;
    for (u32 i = 0;;) {
        s32 a = MAP_ARROW((s32)(s16)i);
        if (a != -1) {
            u32 q = pnl_s(a);
            if ((u32)GV(u16, q + 0xE) == (u32)(s32)GV(s16, q + 0x22)) goto close;
        }
        i++;
        if (!((s16)i < 2)) return;
        LOOP_POLL(0x800CD44C);
    }
close:
    sound(0x800CD470u, 5);
    {
        u32 c = g_curPanel;
        g_panelMode = 0x40;
        GV(u16, c + PANEL_FLAGS) = GV(u16, c + PANEL_FLAGS) & 0xEFFF;
        JAL(0x800CD494u, 0x800CD4C0u, A0, A1, A2, A3);
        ST_CANCEL = 0;
        GV(u32, g_curPanel + 0x3C) = 0x800C50A8u;
    }
}
