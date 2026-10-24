/* The 3D frame (TEST overlay): ordering tables, sorting the room objects,
 * enemies and their shadows into them, lights and view, the buffer flip,
 * TIM loading and the display set-up.
 *
 * Every frame the main loop clears this frame's ordering tables
 * (render_ClearOts), sorts the scene with the stack moved to the
 * scratchpad (render_SortFrame: enemies, extra objects, shadows, the 14
 * room objects at 0x800A89C0), and the frame callback (render_Flip,
 * installed by display_Init) draws the finished table and swaps buffers.
 *
 * Model records (enemies, 0x8C4 bytes; room objects, 0x318 bytes) start
 * with per-part visibility bytes; +0x28 part count, +0x29 shown,
 * +0x2A kind (enemies: 1 = active), +0x2B flags (1 hidden, 4 has a
 * "special" hit part), then 8-byte part entries at +0x2C (coordinate,
 * model data). Enemies: +0x16C bone coordinates (0x50 bytes each),
 * +0x1D0/+0x1D4 animated texture parts, +0x320 flags, +0x898 per-frame
 * hook, +0x8B4 "draw in front", +0x8BC shadow (see shadow_Init).
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). */
#include "libgs.h"

void n_render_ClearOts(void);
void n_render_AddObject(void);
void n_render_ClearObjects(void);
void n_render_SortModels(void);
void n_render_SortFrame(void);
void n_shadow_Update(void);
void n_model_AnimateTextures(void);
void n_render_SetLights(void);
void n_view_Init(void);
void n_render_Flip(void);
void n_tim_Load(void);
void n_vram_Clear(void);
void n_gfx_InitLibGs(void);
void n_render_InitOts(void);
void n_coord_SetMatrix(void);
void n_rot_Step(void);
void n_reticle_Draw(void);
void n_display_Init(void);

#define EXTRA_OBJECTS   0x800A8850u   /* extra models sorted every frame (overlays add them) */
#define EXTRA_COUNT     GV(s32, 0x800AB51C)
#define ROOM_OBJECTS    0x800A89C0u   /* 14 records of 0x318 bytes */

/* 8006D068: this frame's packet area and clear its ordering tables. */
void n_render_ClearOts(void) {
    FRAME(0x18);
    GCALL1(F_GS_SET_WORKBASE, PACKET_AREA + (u32)BUF_INDEX * 0x17700);
    GCALL3(F_GS_CLEAR_OT, 0, 0, OT(OT_SCREEN));
    GCALL3(F_GS_CLEAR_OT, 0, 5, OT(OT_OVERLAY));
    GCALL3(F_GS_CLEAR_OT, 0, 10, OT(OT_FRONT));
    GCALL3(F_GS_CLEAR_OT, 0, 20, OT(OT_WORLD));
}

/* 8006D158 (object): add a model to this frame's extra list. */
void n_render_AddObject(void) {
    s32 n = EXTRA_COUNT;
    GV(u32, EXTRA_OBJECTS + (u32)n * 4) = A0;
    EXTRA_COUNT = n + 1;
    V0 = EXTRA_OBJECTS + (u32)n * 4;           /* as the original leaves it */
}

/* 8006D180: empty the extra list. */
void n_render_ClearObjects(void) {
    EXTRA_COUNT = 0;
    V0 = 0x800B0000u;
}

/* Sort the parts of a model record into ot (with lighting matrices from
 * each part's coordinate when it has one). lw/ls: two stack matrices. */
static void sort_parts(u32 obj, u32 nparts, u32 ot, u32 lw, u32 ls) {
    u32 ent = obj + 0x2C;
    for (u32 p = 0; p < nparts; p++, ent += 8) {
        if (GV(u32, ent + 4) == 0 || GV(u8, obj + p) == 0) continue;
        u32 coord = GV(u32, ent);
        if (coord) {
            GCALL3(F_GS_GET_LWS, coord, lw, ls);
            GCALL1(F_GS_SET_LS_MATRIX, ls);
            GCALL1(F_GS_SET_LIGHT_MATRIX, lw);
        }
        GCALL3(F_GS_SORT_OBJECT, ent, ot, SCRATCH_WORK);
    }
}

/* 8006D18C: sort the enemies (or, during the scenes where the input lock
 * is 4-12, the one model at +0x2D73C in the stage data) and the extra
 * objects. */
void n_render_SortModels(void) {
    FRAME(0x78);
    u32 e = ENEMIES;
    s32 n = ENEMY_COUNT;
    if ((u16)(g_inputLocked - 4) < 9) {
        n = 1;
        e = GV(u32, 0x80010050) + 0x2D73C;
    }
    for (s32 i = 0; i < n; i++, e += ENEMY_SIZE) {
        if (GV(u8, e + 0x29) == 0 || (GV(u8, e + 0x2B) & 1)) continue;
        GV(u32, 0x800AB518) = e;                      /* the model being drawn */
        u32 hook = GV(u32, e + 0x898);
        u32 nparts = GV(u8, e + 0x28);
        if (hook) gcall(hook, e, 0, 0, 0);
        u32 ot;
        if (GV(u32, e + 0x320) & 0x2000) ot = OT(OT_OVERLAY);
        else if (GV(u32, e + 0x8B4) == 0) ot = OT(OT_FRONT);
        else ot = OT(OT_WORLD);
        GV(u32, 0x800BAA08) = GV(u32, e + 0x894);
        sort_parts(e, nparts, ot, sp + 0x10, sp + 0x30);
        GCALL3(F_GS_SET_AMBIENT, 0x400, 0x400, 0x400);
        GV(u8, GV(u32, e + 0x1D4) + 0x1B) = 0;
    }
    for (s32 i = 0; i < EXTRA_COUNT; i++) {
        u32 o = GV(u32, EXTRA_OBJECTS + (u32)i * 4);
        if (GV(u8, o + 0x29) == 0 || (GV(u8, o + 0x2B) & 1)) continue;
        u32 nparts = GV(u8, o + 0x28);
        u32 ot = GV(u32, o + 0x8B4) == 0 ? OT(OT_FRONT) : OT(OT_WORLD);
        sort_parts(o, nparts, ot, sp + 0x10, sp + 0x30);
    }
}

/* 8006D4D0: sort the whole scene into this frame's tables. */
void n_render_SortFrame(void) {
    FRAME(0x58);
    u32 ls = sp + 0x10;
    GV(s16, 0x800AF56A) = 0;
    GV(u32, 0x800BAA08) = 0;
    GV(u16, 0x800B3D9C) = GV(u16, g_player + 0x46);   /* player yaw */
    u32 enemies = ENEMIES;
    /* models and shadows with the stack in the scratchpad */
    u32 saved = GCALL0(F_GET_SP);
    GCALL1(F_SET_SP, SCRATCH_STACK);
    n_render_SortModels();
    if (GV(u32, 0x800AB524) == 0) n_shadow_Update();
    GCALL1(F_SET_SP, saved);

    u32 obj = ROOM_OBJECTS;
    for (int k = 0; k < 14; k++, obj += 0x318) {
        if (GV(u8, obj + 0x2B) & 5) continue;
        if (GV(u8, obj + 0x29)) continue;
        u32 nparts = GV(u8, obj + 0x28);
        u32 ent = obj + 0x2C;
        for (u32 p = 0; p < nparts; p++, ent += 8) {
            if (GV(u32, ent + 4) == 0) continue;
            if (GV(u32, ent)) {
                GCALL2(F_GS_GET_LS, GV(u32, ent), ls);
                GCALL1(F_GS_SET_LS_MATRIX, ls);
                /* the object the camera follows (+0x314 id): the player's
                 * offset from it */
                if (GV(s16, 0x800B3D3C) != 0 && GV(u32, obj + 0x314) == GV(u32, 0x800A8614) &&
                    GV(u8, obj + 0x2A) == 0 && GV(s16, 0x800AF56A) == 0) {
                    GV(s16, 0x800A8674) = 1;
                    GV(u16, 0x800B4D68) = (u16)(GV(u16, g_player + 0x18) - GV(u16, GV(u32, ent) + 0x18));
                    GV(u16, 0x800B4D6C) = (u16)(GV(u16, g_player + 0x20) - GV(u16, GV(u32, ent) + 0x20));
                } else {
                    GV(s16, 0x800A8674) = 0;
                }
                GV(s16, 0x800AB514) = 0;
                /* the same for the first five enemies tied to an object
                 * (0x800B3EA8: 12-byte slots, -1/1 unused, -2 not this one) */
                if (GV(u8, obj + 0x2A) == 0 && GV(s16, 0x800AF568) != 0) {
                    u32 en = enemies;
                    for (int i = 0; i < 5; i++, en += ENEMY_SIZE) {
                        u32 slot = 0x800B3EA8u + (u32)i * 12;
                        s16 v = GV(s16, slot);
                        if (v == -1 || v == 1) continue;
                        if (GV(u32, obj + 0x314) != (u32)(s32)GV(s16, en + 0x856)) {
                            GV(s16, slot) = -2;
                            continue;
                        }
                        GV(s16, slot) = 0;
                        GV(u16, slot + 2) = (u16)(GV(u16, en + 0x198) - GV(u16, GV(u32, ent) + 0x18));
                        GV(s16, 0x800AB514) = 1;
                        GV(u16, slot + 6) = (u16)(GV(u16, en + 0x1A0) - GV(u16, GV(u32, ent) + 0x20));
                    }
                }
            }
            u32 ot = GV(u8, obj + 0x2A) == 0 ? OT(OT_WORLD) : OT(OT_FRONT);
            GCALL3(F_GS_SORT_OBJECT, ent, ot, SCRATCH_WORK);
        }
    }
    GCALL0(0x80090C90u);
    GCALL2(0x80090D4Cu, OT(OT_SCREEN), 0);
    GCALL0(0x8008CA90u);
    GCALL0(0x80092E94u);
    GCALL2(F_GS_SORT_OT, OT(OT_WORLD), OT(OT_SCREEN));
    GCALL2(F_GS_SORT_OT, OT(OT_FRONT), OT(OT_SCREEN));
    GCALL2(F_GS_SORT_OT, OT(OT_OVERLAY), OT(OT_SCREEN));
    if (GV(u32, 0x800A0CB0))
        GCALL2(F_ADD_PRIM, GV(u32, OT(OT_SCREEN) + 0x10), 0x800B3CF8u);
}

/* 8006D8B4: place the enemies' shadows (two quads under each, see
 * shadow_Init) and sort them. Shadow record: +0x50 coordinate (+0x68 its
 * position, +0x96 yaw), +0xA0 scale, +0xB0 height, +0xB4 four corner
 * vectors, +0xD4 hidden, +0xD6 size limit, +0xD8 bone it follows. */
void n_shadow_Update(void) {
    FRAME(0x68);
    u32 e = ENEMIES;
    GV(u32, 0x800B3E60) = e;
    for (s32 i = 0; i < ENEMY_COUNT; i++, e += ENEMY_SIZE) {
        u32 s = GV(u32, e + 0x8BC);
        if (s == 0 || GV(s16, s + 0xD4) != 0) continue;
        if ((GV(u8, e + 0x2B) & 1) || GV(u8, e + 0x29) == 0) continue;
        u32 flags = GV(u32, e + 0x320);
        if ((flags & 0x14000) && GV(s32, s + 0xA0) < GV(s16, s + 0xD6)) {
            /* growing (a pool of blood?): its own palette, scale up */
            u32 y = (flags & 0x4000) ? 0x1EC : 0x1ED;
            GV(u16, s + 0xE) = (u16)GCALL2(F_GET_CLUT, 0x190, y);
            GV(u16, s + 0x36) = (u16)GCALL2(F_GET_CLUT, 0x190, y);
            GV(u32, s + 0xA0) += 0x40;
            GV(u32, s + 0xA8) += 0x48;
        } else {
            s32 h = GV(s32, s + 0xB0);
            if (h != 0 && GV(s16, e + 0x31E) > 0) {
                s32 v = (h >> 1) + 0x1000;            /* smaller when higher */
                GV(s32, s + 0xA0) = v;
                GV(s32, s + 0xA8) = v;
                if (v < 0) {
                    GV(s32, s + 0xA0) = 0;
                    GV(s32, s + 0xA8) = 0;
                }
            }
        }
        u32 bone = GV(u32, s + 0xD8);
        GV(u32, s + 0x68) = GV(u32, bone + 0x38);
        GV(u32, s + 0x6C) = GV(u32, e + 0x19C) - GV(u32, s + 0xB0);
        GV(u32, s + 0x70) = GV(u32, GV(u32, s + 0xD8) + 0x40);
        GV(u16, s + 0x96) = GV(u16, e + 0x1C6);
        GCALL2(0x8006F69Cu, s + 0x50, s + 0xA0);       /* coord_SetMatrix */
        GCALL2(F_GS_GET_LS, s + 0x50, sp + 0x28);
        GCALL1(F_GS_SET_LS_MATRIX, sp + 0x28);
        u32 prim = s + (u32)BUF_INDEX * 40;
        GS(u32, sp + 0x10) = prim + 0x08;
        GS(u32, sp + 0x14) = prim + 0x10;
        GS(u32, sp + 0x18) = prim + 0x18;
        GS(u32, sp + 0x1C) = prim + 0x20;
        GS(u32, sp + 0x20) = sp + 0x48;
        GS(u32, sp + 0x24) = sp + 0x4C;
        GCALL4(F_ROT_TRANS_PERS4, s + 0xB4, s + 0xBC, s + 0xC4, s + 0xCC);
        u32 ot = (GV(u32, e + 0x320) & 1) ? 0x800AB530u : 0x800AB534u;
        u32 b = BUF_INDEX;
        GCALL2(F_ADD_PRIM, ot + (b << 13), s + b * 40);
    }
}

/* 8006DB40: point each enemy's animated texture parts (+0x1D4, count
 * +0x1D0) at the frame +0x860 of their animation tables. */
void n_model_AnimateTextures(void) {
    u32 e = ENEMIES;
    for (s32 i = 0; i < ENEMY_COUNT; i++, e += ENEMY_SIZE) {
        s32 n = GV(s32, e + 0x1D0);
        for (s32 j = 0; j < n; j++) {
            u32 slot = e + (u32)j * 4;
            u32 p = GV(u32, slot + 0x1D4);
            GV(u32, slot + 0x274) = p + 0x18;
            GV(u16, GV(u32, slot + 0x1D4) + 0xA) = 0xFFFF;
            GV(u16, GV(u32, slot + 0x1D4) + 0x8) = 0xFFFF;
            GV(u8, GV(u32, slot + 0x1D4) + 0xD) = 0x10;
            u32 fr = GV(u32, slot + 0x274) + (u32)((s32)GV(s16, e + 0x860) * 4);
            GV(u16, GV(u32, slot + 0x1D4) + 0x16) = (u16)(GV(u16, fr) + 1);
            p = GV(u32, slot + 0x1D4);
            fr = GV(u32, slot + 0x274) + (u32)((s32)GV(s16, e + 0x860) * 4);
            GV(u8, p + 0xC) = GV(u8, fr + 2);
        }
    }
}

/* 8006DC24: projection, three flat lights, ambient, and fog when the
 * room has it (0x800B3CC2; near/far 0x800AF564/0x800A8848, colour
 * 0x800B3D0C). */
void n_render_SetLights(void) {
    FRAME(0x28);
    GCALL1(F_GS_SET_PROJECTION, GV(u32, 0x800A850C));
    static const s32 dir[3][3] = {{0, -50, 100}, {0, -50, -100}, {0, 100, 0}};
    for (u32 i = 0; i < 3; i++) {
        u32 l = 0x800A8510u + i * 0x10;
        GV(s32, l) = dir[i][0];
        GV(s32, l + 4) = dir[i][1];
        GV(s32, l + 8) = dir[i][2];
        GV(u8, l + 0xC) = 0x60;
        GV(u8, l + 0xD) = 0x60;
        GV(u8, l + 0xE) = 0x60;
        GCALL2(F_GS_SET_FLAT_LIGHT, i, l);
    }
    GCALL3(F_GS_SET_AMBIENT, 0x400, 0x400, 0x400);
    u32 mode = 0;
    if (GV(s16, 0x800B3CC2) != 0) {
        GCALL3(F_SET_FOG_NEAR_FAR, GV(u32, 0x800AF564), GV(u32, 0x800A8848), GV(u32, 0x800A850C));
        GCALL3(F_SET_FAR_COLOR, GV(u8, 0x800B3D0C), GV(u8, 0x800B3D0D), GV(u8, 0x800B3D0E));
        mode = 1;
    }
    GCALL1(F_GS_SET_LIGHT_MODE, mode);
}

/* 8006DD5C: projection 270, the view (GsRVIEW2 at 0x800B3EE8, eye 0x1F4
 * above and 0xBB8 behind, relative to the camera coordinate 0x800A8618,
 * itself parented to the player) and the player's coordinate reset. */
void n_view_Init(void) {
    FRAME(0x18);
    GV(u32, 0x800A850C) = 0x10E;
    GCALL1(F_GS_SET_PROJECTION, 0x10E);
    u32 v = D_800B3EE8_ADDR, cam = CAMERA_OBJ, pl = g_player;
    GV(u32, v) = 0;
    GV(u32, v + 0x10) = 0x1F4;
    GV(s32, v + 0x14) = -0xBB8;
    GV(u32, v + 4) = 0;
    GV(u32, v + 8) = 0;
    GV(u32, v + 0xC) = 0;
    GV(u32, v + 0x18) = 0;
    GV(s32, cam + 0x1C) = -0x640;
    GV(u32, cam + 0x4C) = pl;
    GV(u32, v + 0x1C) = cam;
    GV(u16, pl + 0x44) = 0;
    GV(u16, pl + 0x46) = 0x800;
    GV(u16, pl + 0x48) = 0;
    copy_words(pl + 4, IDENTITY_MATRIX, 8);
    GV(u32, pl + 0x4C) = 0;
    GV(s32, pl + 0x20) = -0x44C;
    GCALL1(F_GS_SET_REF_VIEW2, v);
    g_playerMoveSpeed = -0x50;
    V0 = (u32)-0x50;
}

/* 8006F1E4: the frame callback. When a frame is ready (0x80062DF0):
 * swap buffers, set the draw offsets and clip for the new one (screen
 * offset 0x80062E18/1A), add the overlay primitive, clear to black,
 * draw the table. Then the per-frame jobs.
 * It runs on the callback's own stack, in compared RAM (0x8005Bxxx), so
 * this keeps the original's registers as far as they reach memory: the
 * prologue's saves ($s0, $s1, $ra), $ra at every call, and $s0/$s1 as
 * the original sets them (callees save them in their frames). */
void n_render_Flip(void) {
    FRAME(0x28);
    u32 ra = R[31], s0 = R[16], s1 = R[17];
    GS(u32, sp + 0x20) = ra;
    GS(u32, sp + 0x1C) = s1;
    GS(u32, sp + 0x18) = s0;
    if (GV(u32, 0x80062DF0)) {
        gcall_ra(0x8006F208u, F_GS_SWAP_DISP_BUFF, A0, A1, A2, A3);
        u16 ox = GV(u16, 0x80062E18), oy = GV(u16, 0x80062E1A);
        if (BUF_INDEX == 0) gcall_ra(0x8006F25Cu, F_GS_SET_DRAW_OFFSETS, ox, oy, 0, 0x100);
        else gcall_ra(0x8006F25Cu, F_GS_SET_DRAW_OFFSETS, 0, 0, ox, (u16)(oy + 0x100));
        ox = GV(u16, 0x80062E18);
        GS(u16, sp + 0x10) = ox;
        GS(u16, sp + 0x14) = (s16)ox < 0 ? (u16)(((s16)ox >> 1) + 0x140) : (u16)(0x140 - ox);
        oy = GV(u16, 0x80062E1A);
        GS(u16, sp + 0x12) = oy;
        GS(u16, sp + 0x16) = (s16)oy < 0 ? (u16)(((s16)oy >> 1) + 0xF0) : (u16)(0xF0 - oy);
        gcall_ra(0x8006F2C4u, F_GS_SET_CLIP, sp + 0x10, A1, A2, A3);
        R[16] = OT_SCREEN;
        R[17] = 0x80060000u;                   /* %hi of BUF_INDEX */
        gcall_ra(0x8006F2F4u, F_ADD_PRIM, GV(u32, OT(OT_SCREEN) + 0x10), 0x800B3CE8u, A2, A3);
        gcall_ra(0x8006F318u, F_GS_SORT_CLEAR, 0, 0, 0, OT(OT_SCREEN));
        gcall_ra(0x8006F334u, F_GS_DRAW_OT, OT(OT_SCREEN), A1, A2, A3);
        GV(u32, 0x80062DF0) = 0;
        BUF_INDEX = (u8)gcall_ra(0x8006F340u, F_GS_GET_ACTIVE_BUFF, A0, A1, A2, A3);
    }
    gcall_ra(0x8006F34Cu, 0x80079D90u, A0, A1, A2, A3);
    gcall_ra(0x8006F354u, 0x80011F24u, A0, A1, A2, A3);
    gcall_ra(0x8006F35Cu, 0x8008C680u, A0, A1, A2, A3);
    R[31] = ra;
    R[16] = s0;
    R[17] = s1;
}

/* 8006F370 (file data): upload a TIM (magic 0x10) to VRAM: the image,
 * then its palette when it has one. */
#define TIM_INFO 0x800A8550u   /* GsIMAGE: pmode, x,y,w,h, pixels, cx,cy,cw,ch, clut */
void n_tim_Load(void) {
    u32 p = A0;
    FRAME(0x20);
    if (GV(u32, p) != 0x10) {
        V0 = 0x10;
    } else {
        GCALL2(F_GS_GET_TIM_INFO, p + 4, TIM_INFO);
        GCALL2(F_LOAD_IMAGE, TIM_INFO + 4, GV(u32, TIM_INFO + 0xC));
        GCALL1(F_DRAW_SYNC, 0);
        if (((GV(u32, TIM_INFO) >> 3) & 1) && (GV(u16, TIM_INFO + 0x10) | GV(u16, TIM_INFO + 0x12)))
            GCALL2(F_LOAD_IMAGE, TIM_INFO + 0x10, GV(u32, TIM_INFO + 0x18));
        GCALL1(F_DRAW_SYNC, 0);
    }
}

/* 8006F40C (x, y, w): clear a 512-line-high VRAM area to black. */
void n_vram_Clear(void) {
    FRAME(0x20);
    GS(u16, sp + 0x10) = (u16)A0;
    GS(u16, sp + 0x12) = (u16)A1;
    GS(u16, sp + 0x14) = (u16)A2;
    GS(u16, sp + 0x16) = 0x200;
    GCALL4(F_CLEAR_IMAGE, sp + 0x10, 0, 0, 0);
}

/* 8006F490: the 3D library's set-up. */
void n_gfx_InitLibGs(void) {
    FRAME(0x18);
    GCALL0(F_GS_INIT_3D);
}

/* 8006F4B0: screen rectangles, the ordering tables (GsOT length and
 * tag arrays per buffer), the overlay primitives. */
void n_render_InitOts(void) {
    FRAME(0x18);
    u32 r = 0x800637B0u;
    GV(u16, r) = 0;
    GV(u16, r + 2) = 0;
    GV(u16, r + 4) = 0x140;
    GV(u16, r + 6) = 0xF0;
    GV(u16, r + 8) = 0;
    GV(u16, r + 0xA) = 0x100;
    GV(u16, r + 0xC) = 0x140;
    GV(u16, r + 0xE) = 0xF0;
    gcall_ra(0x8006F4F4u, 0x8002A350u, A0, A1, A2, A3);   /* keeps $ra in 0x8005C05C */
    static const struct { u32 ot, len, tags, step; } ots[] = {
        {OT_SCREEN, 5, 0x800B4C40u, 0x80},
        {OT_WORLD, 0xB, 0x800AB530u, 0x2000},
        {OT_FRONT, 0xB, 0x800AFCB8u, 0x2000},
        {OT_OVERLAY, 1, 0x800B3CD0u, 8},
    };
    for (int i = 0; i < 4; i++) {
        GV(u32, ots[i].ot + 0x14) = ots[i].len;
        GV(u32, ots[i].ot) = ots[i].len;
        GV(u32, ots[i].ot + 4) = ots[i].tags;
        GV(u32, ots[i].ot + 0x18) = ots[i].tags + ots[i].step;
    }
    GCALL1(0x80014FF4u, 1);
    D_80062DEC = 0;
    GCALL2(F_PRIM_INIT_383B8, 0x800B3CE8u, 0);
    GCALL2(F_PRIM_INIT_383B8, 0x800B3CF8u, 1);
    GCALL0(0x80076B74u);
}

/* 8006F69C (coord, scale or 0): a GsCOORDINATE2's matrix from its
 * rotation (+0x44) and translation (+0x18..+0x20), scaled (default
 * 1.0); marks it changed (flg = 0). */
void n_coord_SetMatrix(void) {
    u32 c = A0, scale = A1;
    FRAME(0x50);
    if (scale == 0) {
        scale = sp + 0x30;
        GS(u32, sp + 0x30) = 0x1000;
        GS(u32, sp + 0x34) = 0x1000;
        GS(u32, sp + 0x38) = 0x1000;
    }
    u32 m = sp + 0x10;
    copy_words(m, IDENTITY_MATRIX, 8);
    GS(u32, m + 0x14) = GS(u32, c + 0x18);
    GS(u32, m + 0x18) = GS(u32, c + 0x1C);
    GS(u32, m + 0x1C) = GS(u32, c + 0x20);
    GCALL2(F_ROT_MATRIX, c + 0x44, m);
    GCALL2(F_SCALE_MATRIX, m, scale);
    copy_words(c + 4, m, 8);
    GS(u32, c) = 0;
}

/* 8006F7A8 (rot, out, speed): out = the vector (0, 0, speed) rotated by
 * rot, rounded (computed at 128x precision). */
void n_rot_Step(void) {
    u32 rot = A0, out = A1, speed = A2;
    FRAME(0x50);
    u32 m = sp + 0x10, v = sp + 0x30;
    copy_words(m, IDENTITY_MATRIX, 8);
    GS(u16, v) = 0;
    GS(u16, v + 2) = 0;
    GS(u16, v + 4) = (u16)(speed << 7);
    GCALL2(F_ROT_MATRIX, rot, m);
    GCALL1(F_SET_ROT_MATRIX, m);
    GCALL1(F_SET_TRANS_MATRIX, m);
    GCALL3(F_ROT_TRANS, v, out, sp + 0x38);
    s16 f[3];
    for (int i = 0; i < 3; i++) {
        f[i] = (s16)(GS(u8, out + i * 4) >> 4);
        GS(s16, v + i * 2) = f[i];
    }
    for (int i = 0; i < 3; i++) GS(s32, out + i * 4) = GS(s32, out + i * 4) >> 7;
    V0 = 1;
    for (int i = 0; i < 3; i++) {
        if (f[i] >= 8) {
            GS(u32, out + i * 4) += 1;
            V0 = GS(u32, out + i * 4);
        } else {
            V0 = 1;
        }
    }
}

/* 80070FA0: draw the aiming reticle sprite (0x800BBB98) when aiming
 * this frame (not in the attract demo). */
void n_reticle_Draw(void) {
    FRAME(0x18);
    if (GV(u32, 0x800A0CB8)) {
        if (GV(u32, 0x800B3CE4) != 1)
            GCALL3(F_GS_SORT_SPRITE, 0x800BBB98u, OT(OT_OVERLAY), 0);
        GV(u32, 0x800A0CB8) = 0;
    }
}

/* 80072DC8: the display: two black fade tiles (0x800A8420, with draw
 * modes at 0x800A8410), default draw/display environments, then twelve
 * frames of fade, then install the frame callbacks (render_Flip). */
void n_display_Init(void) {
    FRAME(0x38);
    u32 tile = 0x800A8420u, mode = 0x800A8410u;
    u32 w = 0x100;
    for (u32 i = 0; i < 2; i++, tile += 0x14, mode += 8) {
        u32 x = i << 8;
        u32 tp = GCALL4(F_GET_TPAGE, 2, 2, x, 0);
        /* the 5th argument (texture window) is whatever is on the stack */
        GCALL4(F_SET_DRAW_MODE, mode, 1, 0, tp & 0xFFFF);
        GCALL1(F_SET_TILE, tile);
        GV(u16, tile + 0x10) = (u16)w;
        w -= 0x80;
        GV(u16, tile + 8) = (u16)x;
        GV(u16, tile + 0xA) = 0;
        GV(u8, tile + 0xC) = 0;
        GV(u8, tile + 0xD) = 0;
        GV(u16, tile + 0x12) = 0x100;
        GV(u8, tile + 4) = 0x7B;
        GV(u8, tile + 5) = 0x7B;
        GV(u8, tile + 6) = 0x7B;
    }
    GS(u32, sp + 0x10) = 0xF0;
    GCALL4(F_SET_DEF_DRAW_ENV, 0x800A83B0u, 0, 0, 0x140);
    GS(u32, sp + 0x10) = 0xF0;
    GCALL4(F_SET_DEF_DISP_ENV, 0x800A8390u, 0, 0, 0x140);
    GCALL1(F_VSYNC, 0);
    GCALL1(F_PUT_DISP_ENV, 0x800A8390u);
    GCALL1(F_PUT_DRAW_ENV, 0x800A83B0u);
    for (int i = 0; i < 12; i++) {
        GCALL1(F_VSYNC, 3);
        GCALL1(F_DRAW_PRIM, 0x800A8410u);
        GCALL1(F_DRAW_PRIM, 0x800A8420u);
        GCALL1(F_DRAW_PRIM, 0x800A8418u);
        GCALL1(F_DRAW_PRIM, 0x800A8434u);
    }
    GCALL1(F_VSYNC, 0);
    GCALL1(F_SET_DISP_MASK, 0);
    GCALL1(0x80011558u, 0x8006F1E4u);           /* frame callback: render_Flip */
    GCALL1(0x80011564u, 0x8006F1C0u);
}
