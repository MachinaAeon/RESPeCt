/* The event scripts' screen overlay (TEST overlay): a full-screen box
 * (fades: the script's fade instruction, opcode 0x4C) and a picture of
 * up to two 256-wide sprites, drawn over the world; and the camera mode
 * for scripted scenes.
 *
 * State at 0x800BB1E0: +0 flags (1 draw the box, 2 draw the picture, set
 * for one frame each), +4 two box primitives (GsBOXF, 0x10 bytes, one a
 * buffer), +0x24 the box's attribute (semi-transparency), +0x28 its
 * shade; +0x2C four GsSPRITEs (two a buffer), +0xBC.. the picture's
 * position, size, texture, step, and +0xCC its attribute / texture page
 * (bit 7: two sprites wide). 0x800BBB70 is the camera mode: 0 normal,
 * 1 the player's view (angles from 0x800BB564/0x800BB516), 2 a script
 * camera (0x800BB2D0, looked along 0x800BB4C8).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "libgs.h"

void n_overlay_Init(void);
void n_overlay_Draw(void);
void n_script_Fade(void);

#define OVL             0x800BB1E0u
#define g_ovlFlags      GV(u32, OVL)
#define g_camMode       GV(s32, 0x800BBB70)
#define F_GS_SORT_BOXF  0x8002BDA8u   /* (box, ot, pri) */
#define F_APPLY_MATRIX  0x8002A5B8u
#define F_CAM_LOOK      0x80089E6Cu   /* (x, y, z): aim the script camera */
#define g_camYaw        GV(u16, 0x800A8606)

/* 8008C980: reset the overlay: no box, no picture, sprites cleared. */
void n_overlay_Init(void) {
    g_ovlFlags = 0;
    GV(u32, OVL + 0x24) = 0;
    GV(u32, OVL + 0x28) = 0;
    for (int i = 0; i < 2; i++) {
        u32 b = OVL + 4 + (u32)i * 0x10;
        GV(u32, b) = 0;
        GV(s16, b + 0x4) = -0xA0;
        GV(s16, b + 0x6) = -0x78;
        GV(s16, b + 0x8) = 0x140;
        GV(s16, b + 0xA) = 0xF0;
        GV(u8, b + 0xC) = 0;
        GV(u8, b + 0xD) = 0;
        GV(u8, b + 0xE) = 0;
    }
    GV(u32, OVL + 0xCC) = 0;
    GV(u16, OVL + 0xBC) = 0;
    GV(u16, OVL + 0xBE) = 0;
    GV(u16, OVL + 0xC4) = 0;
    GV(u16, OVL + 0xC6) = 0;
    GV(u16, OVL + 0xC8) = 2;
    for (int i = 0; i < 4; i++) {
        u32 s = OVL + 0x2C + (u32)i * 0x24;
        GV(u32, s) = 0;
        GV(u16, s + 0x12) = 0;
        GV(u16, s + 0x10) = 0;
        GV(u16, s + 0x08) = (u16)(0x100 - (i & 1) * 0xC0);
        GV(u16, s + 0x0A) = 0x100;
        GV(u8, s + 0x0F) = 0;
        GV(u8, s + 0x0E) = 0;
        GV(u8, s + 0x16) = 0x80;
        GV(u8, s + 0x15) = 0x80;
        GV(u8, s + 0x14) = 0x80;
        GV(u16, s + 0x1A) = 0;
        GV(u16, s + 0x18) = 0;
        GV(u16, s + 0x1E) = 0x1000;
        GV(u16, s + 0x1C) = 0x1000;
        GV(u32, s + 0x20) = 0;
    }
    g_camMode = 0;
    V0 = 0x800C0000u;
}

/* 8008CA90: once a frame: sort the box and the picture when asked, and
 * set the view for the camera mode. */
void n_overlay_Draw(void) {
    FRAME(0x48);
    if (g_ovlFlags & 1) {
        u32 b = OVL + 4 + (u32)BUF_INDEX * 0x10;
        GV(u32, b) = GV(u32, OVL + 0x24);
        u8 c = GV(u8, OVL + 0x28);
        GV(u8, b + 0xE) = c;
        GV(u8, b + 0xD) = c;
        GV(u8, b + 0xC) = c;
        GCALL3(F_GS_SORT_BOXF, b, OT(OT_FRONT), 0);
        g_ovlFlags &= ~1u;
    }
    if (g_ovlFlags & 2) {
        u32 s = OVL + 0x2C + (u32)BUF_INDEX * 0x48;
        s32 n = (GV(u8, OVL + 0xCC) >> 7) + 1;
        for (s32 i = 0; i < n; i++, s += 0x24) {
            GV(u32, s) = (u32)((GV(s32, OVL + 0xCC) >> 5) & 3) << 24;
            GV(u16, s + 0x4) = (u16)(GV(u16, OVL + 0xBC) + (u32)i * 0x100);
            GV(u16, s + 0x6) = GV(u16, OVL + 0xBE);
            GV(u16, s + 0x8) = (u16)(GV(u16, OVL + 0xC0) - (u32)i * 0xC0);
            GV(u16, s + 0xA) = GV(u16, OVL + 0xC2);
            GV(u16, s + 0x10) = GV(u16, OVL + 0xC4);
            GV(u16, s + 0x12) = GV(u16, OVL + 0xC6);
            GV(u16, s + 0xC) = (u16)((GV(u16, OVL + 0xCC) & 0x1F) + (u32)i * (u32)(s32)GV(s16, OVL + 0xC8));
            GCALL3(F_GS_SORT_SPRITE, s, OT(OT_FRONT), 0);
        }
        g_ovlFlags &= ~2u;
    }
    switch (g_camMode) {
    case 0:
        if (D_800A8610) GCALL1(F_GS_SET_REF_VIEW2, 0x800B3EE8u);
        break;
    case 1:
        GV(u16, 0x800A865C) = GV(u16, 0x800BB564);
        g_camYaw = GV(u16, 0x800BB516);
        GCALL1(F_GS_SET_REF_VIEW2, 0x800B3EE8u);
        break;
    case 2: {
        GS(s16, sp + 0x12) = 0;
        GS(s16, sp + 0x10) = 0;
        GS(s16, sp + 0x14) = -0x3E8;
        GCALL3(F_APPLY_MATRIX, GV(u32, 0x800BB2D0) + 4, sp + 0x10, sp + 0x18);
        u32 x = GS(u32, sp + 0x18), y = GS(u32, sp + 0x1C), z = GS(u32, sp + 0x20);
        GV(u32, 0x800BB4D0 + 0x18) = 0;
        GV(u32, 0x800BB4D0 + 0x1C) = 0;
        GV(u32, 0x800BB4D0 + 0x20) = 0;
        GCALL3(F_CAM_LOOK, x, y, z);
        GV(u16, 0x800A865C) = GV(u16, 0x800BB4C8);
        g_camYaw = GV(u16, 0x800BB4CA);
        break;
    }
    }
}

/* 8008CD70 (thread): {op, s16 semi, s16 shade, s16 step, s16 frames}:
 * fade the box: shade from the given value by step a frame for frames. */
void n_script_Fade(void) {
    u32 t = A0;
    s16 st = GV(s16, t + 4);
    if (st == 0) {
        u32 pc = GV(u32, t);
        GV(u16, t + 0xA) = GV(u16, pc + 8);
        GV(u16, t + 4) = (u16)(st + 1);
        GV(u16, t + 0xC) = GV(u16, pc + 4);
    } else if (st != 1) {
        GV(u32, t) += 0xA;
        V0 = 0;
        return;
    }
    u32 pc = GV(u32, t);
    GV(u32, OVL + 0x24) = ((u32)(2 - GV(s16, pc + 2)) << 28) | 0x40000000u;
    s32 c = GV(s16, t + 0xC) + GV(s16, GV(u32, t) + 6);
    if (c < 0) c = 0;
    if (c >= 0x100) c = 0xFF;
    GV(u16, t + 0xC) = (u16)c;
    GV(u32, OVL + 0x28) = (u32)c;
    g_ovlFlags |= 1;
    GV(u16, t + 0xA) -= 1;
    if (GV(s16, t + 0xA) > 0) { V0 = 1; return; }
    GV(u32, t) += 0xA;
    V0 = 0;
}
