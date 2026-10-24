/* The title overlay's frame: setting up the display, the vertical-blank
 * callback that shows a finished frame (and reads the mouse), drawing a
 * frame (sprites, text, the white flash), sound effects, loading the
 * pictures, and the sprite records' allocation and update (see title.h).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "title.h"
#include "libgs.h"

#define T_MOUSE_X      GV(s32, 0x80080600)   /* smoothed mouse position */
#define T_MOUSE_Y      GV(s32, 0x80080604)
#define PIC_FILE       0x80138000u           /* the loaded picture file: offsets at +4 */
#define TIM_INFO       0x80080E68u           /* GsIMAGE of the last TIM uploaded */
#define FLASH_PRIMS    0x80080570u           /* a POLY_F4 (24 bytes) per buffer */
#define FLASH_MODE     0x800805A0u           /* a DR_MODE (12 bytes) per buffer */

/* 8006C55C: set up the display and the callbacks for the title. */
void n_title_Init(void) {
    FRAME(0x28);
    GCALL1(0x80039038u, 1);
    GCALL0(0x800390C8u);
    GCALL0(0x80038F88u);
    GCALL1(0x80038FB8u, 0);
    GCALL4(0x80013260u, 0x800633F8u, 0x80063420u, 0x80062E44u, 0x80062E4Cu);
    if (GV(u32, 0x80062DA4)) GCALL1(0x8001367Cu, 0);
    else GCALL1(0x80013698u, 0);
    GCALL1(0x80013698u, 0x10);
    GCALL0(0x8003D4CCu);
    GS(u16, sp + 0x1C) = 0x400;
    GS(u16, sp + 0x18) = 0;
    GS(u16, sp + 0x1A) = 0;
    GS(u16, sp + 0x1E) = 0x200;
    GCALL4(F_CLEAR_IMAGE, sp + 0x18, 0, 0, 0);
    GV(u16, 0x800637B0) = 0;
    GV(u16, 0x800637B2) = 0;
    GV(u16, 0x800637B4) = 0x140;
    GV(u16, 0x800637B6) = 0xF0;
    GV(u16, 0x800637B8) = 0;
    GV(u16, 0x800637BA) = 0x100;
    GV(u16, 0x800637BC) = 0x140;
    GV(u16, 0x800637BE) = 0xF0;
    GCALL1(0x80034FF8u, 0);
    GCALL1(F_SET_DISP_MASK, 0);
    gcall_ra(0x8006C660u, 0x8002A350u, 0, A1, A2, A3);   /* keeps $ra in 0x8005C05C */
    GS(u32, sp + 0x10) = 0;
    GCALL4(0x8002C638u, 0x140, 0xF0, 4, 1);
    GCALL4(0x8002D6F8u, 0, 0, 0, 0xF0);
    GV(u32, OT_TITLE + 0x14) = 8;
    GV(u32, OT_TITLE) = 8;
    GV(u32, OT_TITLE + 4) = OT_PRIMS;
    GV(u32, OT_TITLE + 0x18) = OT_PRIMS + 0x400;
    GCALL1(0x80014FF4u, 1);
    GCALL1(0x80011558u, F_T_FRAME_END);       /* the vertical-blank callback */
    GCALL1(0x80011564u, 0x8006CF00u);
}

/* Mouse smoothing: a new reading within 20 of the last moves a third of
 * the way there. */
static s32 smooth(s32 v, u32 prev_addr) {
    s32 prev = GV(s32, prev_addr);
    s32 d = (s32)((u32)v - (u32)prev);
    s32 ad = d < 0 ? -d : d;
    if (ad < 0x14) v = (s32)((u32)prev + (u32)(d / 3));
    return v;
}

/* 8006C6E4: the vertical-blank callback. Reads the mouse (when the game
 * asked for it, 0x80062DEC), and when a frame is ready swaps the buffers,
 * clears to black and draws it; then the per-frame sound job.
 * It runs on the callback's own stack, in compared RAM: so, as the
 * original, it keeps its saved registers in its frame, and gives callees
 * the $s0-$s2 and $ra the original has at each call. */
void n_title_FrameEnd(void) {
    FRAME(0x20);
    u32 ra = R[31], s0 = R[16], s1 = R[17], s2 = R[18];
    GS(u32, sp + 0x1C) = ra;
    GS(u32, sp + 0x18) = s2;
    GS(u32, sp + 0x14) = s1;
    GS(u32, sp + 0x10) = s0;
    if (GV(u8, 0x80062DEC)) {
        u32 m = gcall_ra(0x8006C70Cu, F_PAD_GET_AXES, 0, A1, A2, A3);
        u32 x = ((m >> 8) & 0xFF00) | (m >> 24);
        x = (u32)(((uint64_t)(u32)(x * 320) * 0x057619F1u) >> 32) >> 3;
        GV(u32, 0x80062DD8) = m;
        s32 v = smooth((s32)(x - 0x40), 0x80080600u);
        T_MOUSE_X = v;
        GV(s32, 0x80062DDC) = v;
        u32 y = ((m & 0xFF) << 8) | ((m & 0xFF00) >> 8);
        v = smooth((s32)(y - 0x10), 0x80080604u);
        GV(s32, 0x80062DE0) = v;
        T_MOUSE_Y = v;
        GV(u8, 0x80062E2C) = 0;
    }
    R[18] = 0x80080000u;
    if (T_FRAME_READY) {
        R[17] = 0x80060000u;
        gcall_ra(0x8006C818u, F_GS_SWAP_DISP_BUFF, A0, A1, A2, A3);
        R[16] = OT_TITLE;
        gcall_ra(0x8006C844u, F_GS_SORT_CLEAR, 0, 0, 0, OT_TITLE + (u32)BUF * 20);
        gcall_ra(0x8006C860u, F_GS_DRAW_OT, OT_TITLE + (u32)BUF * 20, A1, A2, A3);
        T_FRAME_READY = 0;
        BUF = (u8)gcall_ra(0x8006C868u, F_GS_GET_ACTIVE_BUFF, A0, A1, A2, A3);
    }
    gcall_ra(0x8006C874u, 0x80011F24u, A0, A1, A2, A3);
    R[31] = ra;
    R[16] = s0;
    R[17] = s1;
    R[18] = s2;
}

/* 8006C9D8: draw the frame: clear the ordering table, the text windows,
 * the sprites, and the fading white flash (T_FLASH) over everything. */
void n_title_Draw(void) {
    FRAME(0x28);
    GCALL3(F_CLEAR_OT, 0, 0, OT_TITLE + (u32)BUF * 20);
    GCALL0(F_T_DRAW_TEXT);
    GCALL0(F_T_DRAW_SPRITES);
    s32 n = T_FLASH;
    if (n != 0) {
        T_FLASH = n - 1;
        if (n - 1 >= 4) {
            s32 c = (s32)((u32)(n - 4) * 255u) / 18;
            u32 p = FLASH_PRIMS + (u32)BUF * 24;
            if (c < 0) c = 0;
            if (c >= 0x100) c = 0xFF;
            GV(u8, p + 3) = 5;
            GV(u8, p + 7) = 0x28;                  /* POLY_F4 */
            GV(u16, p + 8) = (u16)(T_OFS_X * 2);
            GV(u16, p + 0xA) = (u16)(T_OFS_Y * 2);
            GV(u16, p + 0xC) = (u16)(T_OFS_X * 2 + 0x140);
            GV(u16, p + 0xE) = (u16)(T_OFS_Y * 2);
            GV(u16, p + 0x10) = (u16)(T_OFS_X * 2);
            GV(u16, p + 0x12) = (u16)(T_OFS_Y * 2 + 0xF0);
            GV(u16, p + 0x14) = (u16)(T_OFS_X * 2 + 0x140);
            GV(u16, p + 0x16) = (u16)(T_OFS_Y * 2 + 0xF0);
            GCALL2(F_SET_SEMI, p, 1);
            GCALL2(F_SET_SHADE_TEX, p, 0);
            GV(u8, p + 4) = (u8)c;
            GV(u8, p + 5) = (u8)c;
            GV(u8, p + 6) = (u8)c;
            GCALL2(F_ADD_PRIM, OT_PRIMS + ((u32)BUF << 10), p);
        }
        u32 m = FLASH_MODE + (u32)BUF * 12;
        GS(u32, sp + 0x10) = 0;
        GCALL4(F_DR_MODE, m, 0, 1, 0x20);
        GCALL2(F_ADD_PRIM, OT_PRIMS + ((u32)BUF << 10), m);
    }
    T_FRAME_READY = 1;
    V0 = 1;
}

/* 8006CC78: play sound effect `sound` of bank `bank` (0x800637C0, 12
 * bytes each) at volume (l, r) percent of T_VOLUME, on the next voice of
 * 10..16. */
void n_title_Sound(void) {
    FRAME(0x28);
    u32 bank = A0, sound = A1;
    s32 l = (s32)A2, r = (s32)A3;
    s16 h = GV(s16, 0x800637C0u + bank * 12 + 8);
    if (h < 0) return;
    s32 vol = T_VOLUME;
    if (vol == 0) return;
    s32 vl = (s32)((u32)l * (u32)vol) / 100;
    s32 vr = (s32)((u32)r * (u32)vol) / 100;
    GS(u32, sp + 0x10) = 0x3C;
    GS(u32, sp + 0x14) = 0x7F;
    GS(s32, sp + 0x18) = (s16)vl;
    GS(s32, sp + 0x1C) = (s16)vr;
    gcall(F_SPU_PLAY_NOTE, (u32)(s32)GV(s16, 0x8007F17C), (u32)(s32)h, (sound & 0xF0) >> 4, sound & 0xF);
    s32 n = GV(s32, 0x8007F17C) + 1;
    GV(s32, 0x8007F17C) = n;
    if (n >= 0x11) GV(s32, 0x8007F17C) = 10;
}

/* 8006CE20: load picture file `which` from the title's file table; file 2
 * goes to 0x800F8000 and is uploaded at once, the others to PIC_FILE. */
void n_title_LoadPics(void) {
    FRAME(0x20);
    u32 which = A0;
    if (which == 2) {
        GS(u32, sp + 0x10) = 0x800F8000u;
        GCALL4(F_LOAD_FILE, 0x8006C11Cu, 0x80058548u, 0x8007EF74u, 2);
        GCALL2(F_T_UPLOAD_TIM, 0x800F8000u, 0);
    } else {
        GS(u32, sp + 0x10) = PIC_FILE;
        GCALL4(F_LOAD_FILE, 0x8006C11Cu, 0x80058548u, 0x8007EF74u, which);
    }
}

/* 8006D094: run every sprite's update (in use, with an update). */
void n_title_RunSprites(void) {
    FRAME(0x28);
    for (s32 i = 0; i < SPRITE_COUNT; i++) {
        u32 s = SPR(i);
        if (!(SP_U16(s, 0x20) & SF_FREE)) {
            u32 fn = GV(u32, s + 0x34);
            if (fn) {
                T_CUR = s;
                GCALL0(fn);
            }
        }
        if (i + 1 < SPRITE_COUNT) LOOP_POLL(0x8006D0F8);
    }
}

/* 8006F2CC: upload images first..first+count-1 of the loaded picture
 * file (compressed TIMs); T_PICS = first. */
void n_title_LoadImages(void) {
    FRAME(0x28);
    s32 first = (s32)A0, count = (s32)A1;
    T_PICS = first;
    for (s32 i = 0; i < count; i++) {
        u32 off = GV(u32, PIC_FILE + (u32)(i + first) * 4 + 4);
        GCALL2(F_T_UPLOAD_TIM, PIC_FILE + off, 1);
        if (i + 1 < count) LOOP_POLL(0x8006F334);
    }
}

/* 8006F56C: upload a TIM (decompressing it to 0x80108000 first if asked):
 * the image, and its CLUT if it has one. */
void n_title_UploadTim(void) {
    FRAME(0x20);
    u32 tim = A0;
    if (A1) {
        GCALL2(F_DECOMPRESS, tim, 0x80108000u);
        tim = 0x80108000u;
    }
    if (GV(u32, tim) != 0x10) return;
    GCALL2(F_GET_TIM_INFO, tim + 4, TIM_INFO);
    GCALL2(F_LOAD_IMAGE, TIM_INFO + 4, GV(u32, TIM_INFO + 0xC));
    GCALL1(F_DRAW_SYNC, 0);
    if ((GV(u32, TIM_INFO) >> 3) & 1) {
        if (GV(u16, TIM_INFO + 0x10) | GV(u16, TIM_INFO + 0x12))
            GCALL2(F_LOAD_IMAGE, TIM_INFO + 0x10, GV(u32, TIM_INFO + 0x18));
    }
    GCALL1(F_DRAW_SYNC, 0);
}

/* 8006F6D0: give sprite `index` a shadow: a copy one ordering-table slot
 * behind, CLUT row 0xFF, updated by title_ShadowUpdate. */
void n_sprite_AddShadow(void) {
    FRAME(0x20);
    u32 index = A0;
    u32 s = SPR(index);
    s32 k = (s32)GCALL0(F_SPRITE_ALLOC);
    if (k == -1) { V0 = (u32)-1; return; }
    u32 d = SPR(k);
    for (u32 o = 0;; o += 0x10) {
        for (u32 j = 0; j < 0x10; j += 4) GV(u32, d + o + j) = GV(u32, s + o + j);
        if (o + 0x10 == 0x30) break;
        LOOP_POLL(0x8006F750);
    }
    GV(u32, d + 0x30) = GV(u32, s + 0x30);
    GV(u32, d + 0x34) = GV(u32, s + 0x34);
    SP_U16(d, 0xC) = 0x140;
    SP_U16(d, 0xE) = 0xFF;
    u16 f = SP_U16(d, 0x20), t = SP_U16(d, 2);
    SP_U16(d, 0x24) = (u16)index;
    SP_U16(d, 0x20) = f | 0xC;
    SP_U16(d, 2) = t | 0x40;
    SP_U16(d, 0x12) = SP_U16(s, 0x12);
    GV(u32, d + 0x34) = 0x8006E394u;
    SP_U16(d, 0x18) = (u16)(SP_U16(s, 0x18) + 1);
    V0 = 0x8006E394u;
}

/* 8006FA64: the "decided" sounds and the white flash. */
void n_title_ConfirmFlash(void) {
    FRAME(0x18);
    GCALL4(F_T_SOUND, 0, 2, 0x7F, 0x7F);
    GCALL4(F_T_SOUND, 2, 0, 0x7F, 0x7F);
    GCALL4(F_T_SOUND, 2, 1, 0x7F, 0x7F);
    GCALL4(F_T_SOUND, 2, 2, 0x7F, 0x7F);
    GCALL4(F_T_SOUND, 2, 3, 0x7F, 0x7F);
    T_FLASH = 0x18;
    GV(s32, 0x80080E84) = 0x7F;               /* music volume */
    T_STATE = 0x100;
    V0 = 0x100;
}

/* 8006FB00: take a free sprite record (reset: colour 0x80, no update,
 * scale 1, CLUT row 0): its index, or -1. */
void n_sprite_Alloc(void) {
    for (s32 i = 0; i < SPRITE_COUNT; i++) {
        u32 s = SPR(i);
        u16 f = SP_U16(s, 0x20);
        if (f & SF_FREE) {
            SP_U16(s, 0x20) = f & 0xFFFE;
            SP_U16(s, 0x1E) = 0x80;
            SP_U16(s, 0x1C) = 0x80;
            SP_U16(s, 0x1A) = 0x80;
            GV(u32, s + 0x34) = 0;
            SP_U16(s, 0x22) = 0x100;
            SP_U16(s, 0xE) = 0;
            V0 = (u32)i;
            return;
        }
        if (i + 1 < SPRITE_COUNT) LOOP_POLL(0x8006FB54);
    }
    V0 = (u32)-1;
}

/* 80071464: free every sprite. */
void n_sprites_Clear(void) {
    for (s32 i = SPRITE_COUNT - 1; i >= 0; i--) {
        SP_U16(SPR(i), 0x20) = SF_FREE;
        if (i > 0) LOOP_POLL(0x80071480);
    }
    V0 = SPR(0) - SPRITE_SIZE;
}

/* 80071490: give a sprite an image (descriptor: 16 bytes to +0); its
 * screen size is the image's, u in pixels from the 16/8-bit page column. */
void n_sprite_SetImage(void) {
    u32 s = A0, img = A1;
    u32 w[4];
    for (int k = 0; k < 4; k++)
        w[k] = (u32)GV(u8, img + k * 4) | (u32)GV(u8, img + k * 4 + 1) << 8 |
               (u32)GV(u8, img + k * 4 + 2) << 16 | (u32)GV(u8, img + k * 4 + 3) << 24;
    for (int k = 0; k < 4; k++) {
        GV(u8, s + k * 4) = (u8)w[k];
        GV(u8, s + k * 4 + 1) = (u8)(w[k] >> 8);
        GV(u8, s + k * 4 + 2) = (u8)(w[k] >> 16);
        GV(u8, s + k * 4 + 3) = (u8)(w[k] >> 24);
    }
    SP_U16(s, 0x14) = SP_U16(s, 8);
    SP_U16(s, 0x16) = SP_U16(s, 0xA);
    if (SP_U16(s, 0)) SP_U16(s, 4) = (u16)((SP_U16(s, 4) & 0x3F) << 1);
    else SP_U16(s, 4) = (u16)((SP_U16(s, 4) & 0x3F) << 2);
    SP_U16(s, 0x22) = 0x100;
    SP_U16(s, 6) = GV(u8, s + 6);
    V0 = 0x100;
}
