/* The staff credits (the STAFF module, loaded in slot 0 at 0x8006C118 after
 * the ending).
 *
 * staff's main (0x8006C11C; it runs for the whole credits in the mode's
 * thread, keeping the original's frame and registers so that the thread
 * resumes in the translation after a save state) loads the credits picture, sets up the display and the fade,
 * starts the music and then, every frame, runs the state's handler
 * (0x80076428[0x8007641C]: 0 scrolling, 1 fading out) and builds the
 * frame (credits_FrameBegin). The frame callback (credits_Flip) shows
 * the finished frame and swaps buffers.
 *
 * The credits are a list of text lines (0x80075F8C: 8 bytes each, u16 y,
 * u32 text, a 0 text ends; 0x800820F0 is the first one still on screen),
 * scrolled up by the scroll record (pointer at 0x8008293C: s16 speed,
 * s16 done, s32 position; y = line y + position / 512). A text is a list
 * of u16 character codes (0xFFFF ends; 0, 0x40 and 0x5F are spaces),
 * drawn as 8x16 sprites from the font in VRAM (32 a row; CLUT row 0x1E3,
 * or 0x1E4 for codes 0x60 and up) out of 1200 per buffer (0x80076510).
 * Each frame's sprites go in one ordering table entry with a draw mode
 * (0x80082090) and the fade (credits_Fade: a full-screen tile, 0x80082904,
 * and its draw mode, 0x80082924; state at 0x80082900: u16 level, u16
 * flags: 1 fading down, 2 up, 4 drawn, speed in the top byte).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "libgs.h"
#include "poll.h"

void n_credits_Init(void);
void n_credits_Flip(void);
void n_credits_Scroll(void);
void n_credits_DrawText(void);
void n_credits_DrawLines(void);
void n_credits_Fade(void);
void n_credits_FrameBegin(void);
void n_credits_LoadImage(void);
void n_credits_UploadTim(void);
void n_credits_InitFade(void);
void n_credits_FadeOutWait(void);
void n_credits_FadeStart(void);
void n_credits_FadeFlags(void);

#define g_pad           GV(u32, 0x8007640C)   /* buttons this frame */
#define g_padLast       GV(u32, 0x80076410)
#define g_sprites       GV(s32, 0x80076414)   /* sprites used this frame */
#define g_modes         GV(s32, 0x80076418)   /* draw modes used this frame */
#define g_state         GV(s32, 0x8007641C)
#define g_frameReady    GV(s32, 0x80076420)   /* for credits_Flip */
#define g_frames        GV(u32, 0x80076424)
#define g_volume        GV(u16, 0x80076438)   /* s16: music volume, fading at the end */
#define g_endWait       GV(s32, 0x8007643C)   /* frames since the scroll stopped */
#define TIM_INFO        0x80076440u           /* GsIMAGE */
#define OTS             0x80076460u           /* GsOT[2] */
#define OT_TAGS         0x80076490u           /* [2] x 16 entries */
#define SPRITES         0x80076510u           /* [2] x 1200 SPRT, 0x14 bytes */
#define LINES           0x80075F8Cu
#define g_firstLine     GV(s32, 0x800820F0)
#define MODES           0x80082090u           /* [2] x 4 DR_MODE, 0xC bytes */
#define FADE            0x80082900u
#define FADE_TILES      0x80082904u           /* [2] TILE, 0x10 bytes */
#define FADE_MODES      0x80082924u           /* [2] DR_MODE */
#define g_scroll        GV(u32, 0x8008293C)
#define g_stereo        GV(s32, 0x80062E20)
#define g_musicVolume   GV(s16, 0x80062DB6)   /* 0-100 */
#define g_padKind       GV(u32, 0x80062E38)
#define g_offX          GV(u16, 0x80062E18)
#define g_offY          GV(u16, 0x80062E1A)

#define F_SET_SPRT      0x80038328u
#define F_SET_DRAW_MODE_ 0x800383E8u  /* (p, dfe, dtd, tpage, [tw]) */
#define F_GS_INIT_GRAPH 0x8002C7C8u   /* (w, h, mode, dither, [vram]) */
#define F_GS_DEF_DISP_BUFF 0x8002D6F8u
#define F_SET_VSYNC_CB  0x80011558u
#define F_SET_VSYNC_CB2 0x80011564u
#define F_PAD_BUTTONS   0x80013A5Cu
#define F_MAP_BUTTONS   0x8001189Cu
#define F_MASTER_VOLUME 0x80015484u   /* (l, r, l2, r2) */
#define F_FADE_START    0x8006CF78u   /* (speed): fade out (credits_FadeStart) */

/* Put primitive p first in ordering table entry ot (addPrim). */
static void add_prim(u32 ot, u32 p) {
    GV(u32, p) = (GV(u32, p) & 0xFF000000u) | (GV(u32, ot) & 0xFFFFFFu);
    GV(u32, ot) = (GV(u32, ot) & 0xFF000000u) | (p & 0xFFFFFFu);
}

/* 8006C30C: the display (320x240, two buffers, ordering tables of 16
 * entries), the frame callback, and the sprites (grey, opaque). */
void n_credits_Init(void) {
    FRAME(0x38);
    GCALL1(F_SET_DISP_MASK, 0);
    gcall_ra(0x8006C340u, 0x8002A350u, A0, A1, A2, A3);   /* keeps $ra in a global */
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_GS_INIT_GRAPH, 0x140, 0xF0, 4, 1);
    GCALL4(F_GS_DEF_DISP_BUFF, 0, 0, 0, 0x100);
    GV(u32, OTS + 0x14) = 4;
    GV(u32, OTS) = 4;
    GV(u32, OTS + 4) = OT_TAGS;
    GV(u32, OTS + 0x18) = OT_TAGS + 0x40;
    GCALL1(F_SET_VSYNC_CB, 0x8006C448u);
    GCALL1(F_SET_VSYNC_CB2, 0);
    for (u32 b = 0;;) {
        for (u32 k = 0;;) {
            u32 s = SPRITES + b * 0x5DC0 + k * 0x14;
            GCALL1(F_SET_SPRT, s);
            GV(u8, s + 4) = 0x80;
            GV(u8, s + 5) = 0x80;
            GV(u8, s + 6) = 0x80;
            GCALL2(F_SET_SEMI_TRANS, s, 0);
            if (++k >= 0x4B0) break;
            LOOP_POLL(0x8006C3F0u);
        }
        if (++b >= 2) break;
        LOOP_POLL(0x8006C400u);
    }
    g_sprites = 0;
    g_modes = 0;
    g_frameReady = 0;
    V0 = 0x80070000u;
}

/* 8006C448: frame callback (in the vertical blank): if a frame is ready,
 * count it, swap buffers, set the drawing offsets and clip, clear the new
 * buffer and draw the ready one's ordering table. It runs on the
 * interrupt's stack, in RAM the game compares: saved registers and
 * return addresses are kept as the original keeps them. */
void n_credits_Flip(void) {
    FRAME(0x28);
    u32 ra = R[31], s0 = R[16], s1 = R[17];
    GS(u32, sp + 0x20) = ra;
    GS(u32, sp + 0x1C) = s1;
    GS(u32, sp + 0x18) = s0;
    if (g_frameReady != 0) {
        g_frames++;
        gcall_ra(0x8006C47Cu, F_GS_SWAP_DISP_BUFF, A0, A1, A2, A3);
        u16 ox = g_offX, oy = g_offY;
        if (BUF_INDEX == 0) gcall_ra(0x8006C4D0u, F_GS_SET_DRAW_OFFSETS, ox, oy, 0, 0x100);
        else gcall_ra(0x8006C4D0u, F_GS_SET_DRAW_OFFSETS, 0, 0, ox, (u16)(oy + 0x100));
        ox = g_offX;
        GS(u16, sp + 0x10) = ox;
        GS(u16, sp + 0x14) = (s16)ox < 0 ? (u16)(((s16)ox >> 1) + 0x140) : (u16)(0x140 - ox);
        oy = g_offY;
        GS(u16, sp + 0x12) = oy;
        GS(u16, sp + 0x16) = (s16)oy < 0 ? (u16)(((s16)oy >> 1) + 0xF0) : (u16)(0xF0 - oy);
        gcall_ra(0x8006C538u, F_GS_SET_CLIP, sp + 0x10, A1, A2, A3);
        R[17] = 0x80060000u;                   /* %hi of BUF_INDEX */
        R[16] = OTS;
        gcall_ra(0x8006C568u, F_GS_SORT_CLEAR, 0, 0, 0, OTS + (u32)BUF_INDEX * 0x14);
        gcall_ra(0x8006C584u, F_GS_DRAW_OT, OTS + (u32)BUF_INDEX * 0x14, A1, A2, A3);
        BUF_INDEX = (u8)gcall_ra(0x8006C58Cu, F_GS_GET_ACTIVE_BUFF, A0, A1, A2, A3);
        g_frameReady = 0;
        V0 = 0x80070000u;
    } else {
        V0 = 0;
    }
    R[31] = ra;
    R[16] = s0;
    R[17] = s1;
}

/* 8006C5AC (state 0): the pad; the scroll moves on by its speed until
 * the end (-0x205000), where it stops. From frame 0x23F0 the music fades
 * (by 17 a frame); 480 frames after the scroll has stopped (and from
 * frame 0x2473), or on a press of start (not with pad kind 0x8000), it
 * fades out to state 1. */
void n_credits_Scroll(void) {
    FRAME(0x18);
    g_padLast = g_pad;
    u32 raw = GCALL1(F_PAD_BUTTONS, 0);
    u32 v = GCALL1(F_MAP_BUTTONS, raw);
    u32 sc = g_scroll;
    s16 speed = GV(s16, sc);
    s32 pos = (s32)(GV(u32, sc + 4) - (u32)(s32)speed);
    g_pad = v & 0xFFFF;
    GV(s32, sc + 4) = pos;
    if (!(-0x205000 < pos) && speed != 0) {
        GV(s32, sc + 4) = -0x205000;
        GV(s16, sc) = 0;
        g_endWait = 0;
    }
    if (g_frames >= 0x23F0) {
        if ((s16)g_volume > 0) {
            g_volume = (u16)(g_volume - 0x11);
            s32 vol = (s16)g_volume;
            if (g_stereo == 0) {
                GCALL4(F_MASTER_VOLUME, (u32)vol, 0, (u32)vol, 0);
            } else {
                s32 h = vol / 2;
                GCALL4(F_MASTER_VOLUME, (u32)h, (u32)h, (u32)h, (u32)h);
            }
        } else {
            GCALL4(F_MASTER_VOLUME, 0, 0, 0, 0);
        }
    }
    if (GV(s16, g_scroll) == 0 && g_frames >= 0x2473) {
        g_endWait++;
        if (g_endWait >= 0x1E1) {
            GCALL1(F_FADE_START, 0x10);
            g_state = 1;
        }
    }
    if (g_padKind != 0x8000 && (g_pad & 0x800) && !(g_padLast & 0x800)) {
        GCALL1(F_FADE_START, 0x10);
        g_state = 1;
    }
}

/* 8006C838 (y, v, -, text): draw a text line at y from x 0 (v: the font's
 * row offset). */
void n_credits_DrawText(void) {
    FRAME(0x40);
    u32 y = A0, text = A3;
    GS(u16, sp + 0x10) = (u16)A1;
    u32 b = BUF_INDEX;
    u32 s = SPRITES + b * 0x5DC0 + (u32)g_sprites * 0x14;
    u32 ot = OT_TAGS + b * 0x40;
    u16 x = 0;
    for (;; text += 2) {
        u16 c = GV(u16, text);
        if (c == 0x40 || c == 0 || c == 0x5F) {
            x += 8;
        } else if (c == 0xFFFF) {
            break;
        } else {
            GV(u16, s + 0x10) = 8;
            GV(u16, s + 0x12) = 0x10;
            GV(u8, s + 0xC) = (u8)((c & 0x1F) << 3);
            GV(u16, s + 8) = x;
            GV(u16, s + 0xA) = (u16)y;
            GV(u8, s + 0xD) = (u8)(GS(u16, sp + 0x10) + ((c >> 5) << 4));
            GV(u16, s + 0xE) = (u16)GCALL2(F_GET_CLUT, 0x170, c < 0x60 ? 0x1E3 : 0x1E4);
            x += 8;
            add_prim(ot, s);
            g_sprites++;
            s += 0x14;
        }
        LOOP_POLL(0x8006C9C0u);
    }
    V0 = 0xFFFF;
}

/* 8006C9F8: draw the lines on screen (up to 30 from the first one still
 * on it; one scrolled off the top, above -7, moves the first on), then
 * their draw mode. */
void n_credits_DrawLines(void) {
    FRAME(0x38);
    s32 first = g_firstLine;
    u32 ln = LINES + (u32)first * 8;
    u32 ot = OT_TAGS + (u32)BUF_INDEX * 0x40;
    for (s32 i = first; i < first + 30;) {
        s32 pos = GV(s32, g_scroll + 4);
        u16 ly = GV(u16, ln);
        u32 text = GV(u32, ln + 4);
        if (text == 0) break;
        s32 y = (s16)(ly + (pos >> 9));
        if (!(y < 0xF0)) break;
        if (y < -7) g_firstLine++;
        else GCALL4(0x8006C838u, (u32)y, 0, 8, text);
        i++;
        ln += 8;
        if (!(i < first + 30)) break;
        LOOP_POLL(0x8006CABCu);
    }
    u32 m = MODES + (u32)BUF_INDEX * 0x30 + (u32)g_modes * 0xC;
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_SET_DRAW_MODE_, m, 0, 0, 0x16);
    add_prim(ot, m);
    g_modes++;
    V0 = (u32)g_modes;
}

/* 8006CB7C: step the fade (down to 0, where it stops, or up to 0xFF,
 * where it stays drawn) and, while it's drawn, put its tile and draw
 * mode in front. */
void n_credits_Fade(void) {
    u16 lvl = GV(u16, FADE), st = GV(u16, FADE + 2);
    if (st & 1) {
        lvl = (u16)(lvl - (st >> 8));
        if ((s16)lvl <= 0) { lvl = 0; st = 0; }
    } else if (st & 2) {
        lvl = (u16)(lvl + (st >> 8));
        if (!((s16)lvl < 0xFF)) { lvl = 0xFF; st = 6; }
    }
    GV(u16, FADE + 2) = st;
    GV(u16, FADE) = lvl;
    if (!(st & 4)) return;
    u32 b = BUF_INDEX;
    u32 t = FADE_TILES + b * 16;
    GV(u8, t + 4) = (u8)lvl;
    GV(u8, t + 5) = (u8)lvl;
    GV(u8, t + 6) = (u8)lvl;
    u32 ot = OT_TAGS + (u32)BUF_INDEX * 0x40;
    add_prim(ot, t);
    add_prim(ot, FADE_MODES + b * 12);
}

/* 8006CD90: start a frame: clear the ordering table, the fade, the
 * lines; the frame is ready for credits_Flip. Returns 1. */
void n_credits_FrameBegin(void) {
    FRAME(0x18);
    GCALL3(F_GS_CLEAR_OT, 0, 0, OTS + (u32)BUF_INDEX * 0x14);
    GCALL0(0x8006CB7Cu);
    g_sprites = 0;
    g_modes = 0;
    GCALL0(0x8006C9F8u);
    g_frameReady = 1;
    V0 = 1;
}

/* 8006CDF8: upload the credits picture (the TIM at 0x8006EF2C). */
void n_credits_LoadImage(void) {
    FRAME(0x18);
    GCALL1(0x8006CE1Cu, 0x8006EF2Cu);
}

/* 8006CE1C (tim): upload a TIM's image and (if it has one) its CLUT. */
void n_credits_UploadTim(void) {
    FRAME(0x20);
    u32 tim = A0;
    if (GV(u32, tim) != 0x10) return;
    GCALL2(F_GS_GET_TIM_INFO, tim + 4, TIM_INFO);
    GCALL2(F_LOAD_IMAGE, TIM_INFO + 4, GV(u32, TIM_INFO + 0xC));
    GCALL1(F_DRAW_SYNC, 0);
    if (((GV(u32, TIM_INFO) >> 3) & 1) && (GV(u16, TIM_INFO + 0x10) | GV(u16, TIM_INFO + 0x12)) != 0)
        GCALL2(F_LOAD_IMAGE, TIM_INFO + 0x10, GV(u32, TIM_INFO + 0x18));
    GCALL1(F_DRAW_SYNC, 0);
}

/* 8006CEB8: the fade's two tiles (full screen, black, semi-transparent)
 * and draw modes (texture page 0x40: subtracting). */
void n_credits_InitFade(void) {
    FRAME(0x30);
    for (u32 k = 0;;) {
        u32 t = FADE_TILES + k * 16;
        GV(u8, t + 7) = 0x60;
        GV(u16, t + 0xC) = 0x140;
        GV(u8, t + 3) = 3;
        GV(u16, t + 8) = 0;
        GV(u16, t + 0xA) = 0;
        GV(u16, t + 0xE) = 0xF0;
        GCALL2(F_SET_SEMI_TRANS, t, 1);
        GV(u8, t + 4) = 0;
        GV(u8, t + 5) = 0;
        GV(u8, t + 6) = 0;
        GS(u32, sp + 0x10) = 0;
        GCALL4(F_SET_DRAW_MODE_, FADE_MODES + k * 12, 0, 0, 0x40);
        if (++k >= 2) break;
        LOOP_POLL(0x8006CF50u);
    }
}

/* 8006C770 (state 1): the music fades (by 17 a frame); once the screen
 * is black (fade flags 6), stop the music (waiting a frame at a time
 * until it's stopped) and mark the credits done. */
void n_credits_FadeOutWait(void) {
    FRAME(0x18);
    if ((s16)g_volume > 0) {
        g_volume = (u16)(g_volume - 0x11);
        s32 vol = (s16)g_volume;
        if (g_stereo == 0) {
            GCALL4(F_MASTER_VOLUME, (u32)vol, 0, (u32)vol, 0);
        } else {
            s32 h = vol / 2;
            GCALL4(F_MASTER_VOLUME, (u32)h, (u32)h, (u32)h, (u32)h);
        }
    } else {
        GCALL4(F_MASTER_VOLUME, 0, 0, 0, 0);
    }
    u32 f = GCALL0(0x8006CF94u) & 0xFFFF;
    V0 = f;
    if (f != 6) return;
    for (;;) {
        if ((s32)GCALL1(0x80015AE8u, 0) >= 0) break;     /* stop the music */
        GCALL1(F_VSYNC, 0);
        LOOP_POLL(0x8006C814u);
    }
    GV(u16, g_scroll + 2) = 1;
    V0 = 1;
}

/* 8006CF78 (speed): fade to black: level 0, going up. */
void n_credits_FadeStart(void) {
    GV(u16, FADE + 2) = (u16)((A0 << 8) | 6);
    GV(u16, FADE) = 0;
    V0 = FADE;
}

/* 8006CF94: the fade's flags. */
void n_credits_FadeFlags(void) {
    V0 = GV(u16, FADE + 2);
}


#define CD_RETRY(cond, poll) for (;;) { if (cond) break; \
        gcall_ra((poll), 0x800255F8u, 0, A1, A2, A3); LOOP_POLL(poll); }

/* 8006C11C: the credits: the CD queue made ready and the music's XA
 * track (0x80082100, channel 0) started at the sound volume (as 0..255,
 * centred on mono), then a frame at a time the state's handler, the
 * frame and the vertical blank (mode switch) until the scroll is done. */
void n_credits_Main(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x1C, R[31]);
    MEM_W32(sp + 0x18, R[18]);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 in_s0 = R[16], in_s1 = R[17], in_s2 = R[18];
    CD_RETRY(gcall_ra(0x8006C138u, 0x80014B14u, 0x40, A1, A2, A3) == 0, 0x8006C148u);
    CD_RETRY((s32)gcall_ra(0x8006C158u, 0x80015AE8u, 0, A1, A2, A3) >= 0, 0x8006C168u);
    for (;;) {
        const s32 r = (s32)gcall_ra(0x8006C178u, 0x80014FF4u, 0, A1, A2, A3);
        R[17] = 0x80080000u;
        if (r >= 0) break;
        gcall_ra(0x8006C188u, 0x800255F8u, 0, A1, A2, A3);
        LOOP_POLL(0x8006C188u);
    }
    R[16] = 0x80060000u;
    CD_RETRY((s32)gcall_ra(0x8006C1A4u, 0x80015D44u, 0x80082100u, 1, 0x800585D4u, A3) >= 0, 0x8006C1B4u);
    CD_RETRY((s32)gcall_ra(0x8006C1C4u, 0x80015840u, 1, 0, A2, A3) >= 0, 0x8006C1D4u);
    CD_RETRY(gcall_ra(0x8006C1E4u, 0x80014B14u, 0x40, A1, A2, A3) == 0, 0x8006C1F4u);
    {
        const s32 v0 = (s32)GV(s16, 0x80062DB6u) * 255;
        const s32 v = (s32)(((s64)v0 * 0x51EB851FLL) >> 37) - (v0 >> 31);
        GV(u16, 0x80076438u) = (u16)v;
        const s32 vs = (s32)(s16)v;
        if (GV(u32, 0x80062E20u) == 0) gcall_ra(0x8006C278u, 0x80015484u, (u32)vs, 0, (u32)vs, 0);
        else {
            const u32 h = (u32)((vs + (s32)(((u32)v << 16) >> 31)) >> 1);
            gcall_ra(0x8006C278u, 0x80015484u, h, h, h, h);
        }
    }
    gcall_ra(0x8006C284u, 0x8001569Cu, 0x800585D8u, A1, A2, A3);
    if (GV(s16, MEM_R32(0x8008293Cu) + 2) == 0) {
        R[18] = 0x80076428u;
        R[17] = 0x80070000u;
        R[16] = 0x80080000u;
        for (;;) {
            gcall_ra(0x8006C2CCu, GV(u32, 0x80076428u + (GV(u32, 0x8007641Cu) << 2)), A0, A1, A2, A3);
            gcall_ra(0x8006C2D4u, 0x8006CD90u, A0, A1, A2, A3);
            gcall_ra(0x8006C2DCu, 0x8001306Cu, 1, A1, A2, A3);
            if (GV(s16, MEM_R32(0x8008293Cu) + 2) != 0) break;
            LOOP_POLL(0x8006C2ECu);
        }
    }
    R[16] = in_s0;
    R[17] = in_s1;
    R[18] = in_s2;
}

/* 8006CCF8: the credits' state: the sound bank, state 0, the scroll
 * record (0x80082940: speed 0xE6, not done, position 0), the first line
 * on screen 0. */
void n_credits_Reset(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    GV(u8, 0x80062E1Cu) = 0;
    gcall_ra(0x8006CD10u, 0x80011994u, 1, A1, A2, A3);
    const u32 r = 0x80082940u;
    GV(u32, 0x8007641Cu) = 0;
    GV(u16, r + 2) = 0;
    GV(u32, r + 4) = 0;
    GV(u32, 0x8008293Cu) = r;
    GV(u16, r) = 0xE6;
    GV(u32, 0x800820F0u) = 0;
}

/* 8006CD4C: the end: the sound stopped, the frame callbacks cleared,
 * the next mode (3) asked for and a vertical blank. */
void n_credits_End(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall_ra(0x8006CD5Cu, 0x80011D48u, 1, A1, A2, A3);
    gcall_ra(0x8006CD64u, 0x80011558u, 0, A1, A2, A3);
    gcall_ra(0x8006CD6Cu, 0x80011564u, 0, A1, A2, A3);
    GV(u8, 0x80062DFCu) = 3;
    gcall_ra(0x8006CD80u, 0x8001306Cu, 1, A1, A2, A3);
}

/* 8006CCB0: STAFF's main: reset, the picture, the display, the fade,
 * the credits, the end. */
void n_credits_Entry(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall_ra(0x8006CCC0u, 0x8006CCF8u, A0, A1, A2, A3);
    gcall_ra(0x8006CCC8u, 0x8006CDF8u, A0, A1, A2, A3);
    gcall_ra(0x8006CCD0u, 0x8006C30Cu, A0, A1, A2, A3);
    gcall_ra(0x8006CCD8u, 0x8006CEB8u, A0, A1, A2, A3);
    gcall_ra(0x8006CCE0u, 0x8006C11Cu, A0, A1, A2, A3);
    gcall_ra(0x8006CCE8u, 0x8006CD4Cu, A0, A1, A2, A3);
}
