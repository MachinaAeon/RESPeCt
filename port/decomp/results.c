/* The results screen (the SCORE module, loaded in slot 0 at 0x8006C118
 * after the credits).
 *
 * score's main (0x8006D150) works out the results (results_Compute),
 * loads the pictures (results_Load), sets up the display and the fade,
 * then runs its loop (0x8006D1E0): every frame the state's handler
 * (0x8006DCA0[0x8006DC98]: fade in, wait for a press, fade out, wait for
 * the fade) and the frame (results_Frame), until the record's done flag
 * is set; 0x8006D278 then hands back to the kernel. The loop runs for
 * the whole screen in the mode's thread: its native keeps the original's
 * frame, registers and return addresses, so the thread, switched away in
 * it, resumes in the translation after a save state. The frame callback (results_Flip) shows the
 * finished frame and swaps buffers.
 *
 * The record (pointer at 0x8006FD4C, at 0x8006FD30): s32 rank (0 to 4,
 * which also picks the picture), s32 hit average (%), s32 guns found
 * (+1), s32 continues, u32 clear time (frames? capped at 9999 for the
 * digits), s32 done, u32 frames. The screen is a list of text entries
 * (0x8006DA68, 24 bytes each: text, u16 length, x, y, CLUT row, font; -1
 * ends), drawn from the right as sprites from the font in VRAM (font 0:
 * 14x14 cells, 18 a row; font 1: 8x8, 32 a row); the numbers are digit
 * arrays inside those texts that results_Compute adds the digits to.
 *
 * The fade (0x8006FD50): u16 level, u16 flags (1 down, 2 up, 4 drawn,
 * speed in the top byte), then a tile and a draw mode per buffer.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "libgs.h"
#include "poll.h"

void n_results_Init(void);
void n_results_Flip(void);
void n_results_DrawText(void);
void n_results_DrawTexts(void);
void n_results_DrawBackground(void);
void n_results_Compute(void);
void n_results_TimeDigits(void);
void n_results_Rank(void);
void n_results_Fade(void);
void n_results_Begin(void);
void n_results_Frame(void);
void n_results_Load(void);
void n_results_UploadTim(void);
void n_results_InitFade(void);
void n_results_Digits6(void);
void n_results_Digits4(void);
void n_results_StateFadeIn(void);
void n_results_StatePad(void);
void n_results_StateFadeOut(void);
void n_results_StateWaitFade(void);
void n_results_FadeInStart(void);
void n_results_FadeOutStart(void);
void n_results_FadeFlags(void);
void n_results_Pow(void);

#define g_sprites       GV(s32, 0x8006DC90)   /* sprites used this frame */
#define g_modes         GV(s32, 0x8006DC94)   /* draw modes used this frame */
#define g_state         GV(s32, 0x8006DC98)
#define g_frameReady    GV(s32, 0x8006DC9C)   /* for results_Flip */
#define TIM_INFO        0x8006DCB0u           /* GsIMAGE */
#define g_pad           GV(u32, 0x8006DCCC)   /* buttons this frame */
#define g_padLast       GV(u32, 0x8006DCD0)
#define OTS             0x8006DCE0u           /* GsOT[2] */
#define OT_TAGS         0x8006DD10u           /* [2] x 16 entries */
#define SPRITES         0x8006DD90u           /* [2] x 200 SPRT, 0x14 bytes */
#define MODES           0x8006FCD0u           /* [2] x 4 DR_MODE, 0xC bytes */
#define RECORD          0x8006FD30u
#define g_record        GV(u32, 0x8006FD4C)
#define FADE            0x8006FD50u
#define FADE_TILES      0x8006FD54u           /* [2] TILE, 0x10 bytes */
#define FADE_MODES      0x8006FD74u           /* [2] DR_MODE */
#define TEXTS           0x8006DA68u
#define RANK_LETTERS    0x8006C11Cu           /* u16[5]: the rank's glyph */

/* the game's tallies (kernel) */
#define SHOTS           0x80063448u   /* s32 shots, hits, u32 time, u32 continues */
#define KILLS           0x80063460u   /* s32 enemies, killed, s32 kills shown, -, s32 guns */
#define g_gunsFound     GV(u32, 0x80062FA0)   /* a bit per gun, from bit 31 */
#define g_continues     GV(u8, 0x80062DBE)
#define g_rankSeen      GV(u8, 0x80062D3C)
#define g_padKind       GV(u32, 0x80062E38)
#define g_offX          GV(u16, 0x80062E18)
#define g_offY          GV(u16, 0x80062E1A)

#define F_SET_SPRT       0x80038328u
#define F_SET_DRAW_MODE_ 0x800383E8u  /* (p, dfe, dtd, tpage, [tw]) */
#define F_GS_INIT_GRAPH  0x8002C7C8u  /* (w, h, mode, dither, [vram]) */
#define F_GS_DEF_DISP_BUFF 0x8002D6F8u
#define F_SET_VSYNC_CB   0x80011558u
#define F_SET_VSYNC_CB2  0x80011564u
#define F_PAD_BUTTONS    0x80013A5Cu
#define F_MAP_BUTTONS    0x8001189Cu
#define F_FILE_LOAD2     0x800116A4u  /* (name, table, index, dest) */

/* Put primitive p first in ordering table entry ot (addPrim). */
static void add_prim(u32 ot, u32 p) {
    GV(u32, p) = (GV(u32, p) & 0xFF000000u) | (GV(u32, ot) & 0xFFFFFFu);
    GV(u32, ot) = (GV(u32, ot) & 0xFF000000u) | (p & 0xFFFFFFu);
}

/* 8006C154: the display (320x240, two buffers, ordering tables of 16
 * entries), the frame callback, and the sprites (grey, opaque). */
void n_results_Init(void) {
    FRAME(0x38);
    GCALL1(F_SET_DISP_MASK, 0);
    gcall_ra(0x8006C188u, 0x8002A350u, A0, A1, A2, A3);   /* keeps $ra in a global */
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_GS_INIT_GRAPH, 0x140, 0xF0, 4, 1);
    GCALL4(F_GS_DEF_DISP_BUFF, 0, 0, 0, 0x100);
    GV(u32, OTS + 0x14) = 4;
    GV(u32, OTS) = 4;
    GV(u32, OTS + 4) = OT_TAGS;
    GV(u32, OTS + 0x18) = OT_TAGS + 0x40;
    GCALL1(F_SET_VSYNC_CB, 0x8006C290u);
    GCALL1(F_SET_VSYNC_CB2, 0);
    for (u32 b = 0;;) {
        for (u32 k = 0;;) {
            u32 s = SPRITES + b * 0xFA0 + k * 0x14;
            GCALL1(F_SET_SPRT, s);
            GV(u8, s + 4) = 0x80;
            GV(u8, s + 5) = 0x80;
            GV(u8, s + 6) = 0x80;
            GCALL2(F_SET_SEMI_TRANS, s, 0);
            if (++k >= 200) break;
            LOOP_POLL(0x8006C238u);
        }
        if (++b >= 2) break;
        LOOP_POLL(0x8006C248u);
    }
    g_sprites = 0;
    g_modes = 0;
    g_frameReady = 0;
    V0 = 0x80070000u;
}

/* 8006C290: frame callback (in the vertical blank): if a frame is ready,
 * swap buffers, set the drawing offsets and clip, clear the new buffer
 * and draw the ready one's ordering table. It runs on the interrupt's
 * stack, in RAM the game compares: saved registers and return addresses
 * are kept as the original keeps them. */
void n_results_Flip(void) {
    FRAME(0x28);
    u32 ra = R[31], s0 = R[16], s1 = R[17];
    GS(u32, sp + 0x20) = ra;
    GS(u32, sp + 0x1C) = s1;
    GS(u32, sp + 0x18) = s0;
    if (g_frameReady != 0) {
        gcall_ra(0x8006C2B4u, F_GS_SWAP_DISP_BUFF, A0, A1, A2, A3);
        u16 ox = g_offX, oy = g_offY;
        if (BUF_INDEX == 0) gcall_ra(0x8006C308u, F_GS_SET_DRAW_OFFSETS, ox, oy, 0, 0x100);
        else gcall_ra(0x8006C308u, F_GS_SET_DRAW_OFFSETS, 0, 0, ox, (u16)(oy + 0x100));
        ox = g_offX;
        GS(u16, sp + 0x10) = ox;
        GS(u16, sp + 0x14) = (s16)ox < 0 ? (u16)(((s16)ox >> 1) + 0x140) : (u16)(0x140 - ox);
        oy = g_offY;
        GS(u16, sp + 0x12) = oy;
        GS(u16, sp + 0x16) = (s16)oy < 0 ? (u16)(((s16)oy >> 1) + 0xF0) : (u16)(0xF0 - oy);
        gcall_ra(0x8006C370u, F_GS_SET_CLIP, sp + 0x10, A1, A2, A3);
        R[17] = 0x80060000u;                   /* %hi of BUF_INDEX */
        R[16] = OTS;
        gcall_ra(0x8006C3A0u, F_GS_SORT_CLEAR, 0, 0, 0, OTS + (u32)BUF_INDEX * 0x14);
        gcall_ra(0x8006C3BCu, F_GS_DRAW_OT, OTS + (u32)BUF_INDEX * 0x14, A1, A2, A3);
        BUF_INDEX = (u8)gcall_ra(0x8006C3C4u, F_GS_GET_ACTIVE_BUFF, A0, A1, A2, A3);
        g_frameReady = 0;
        V0 = 0x80070000u;
    } else {
        V0 = 0;
    }
    R[31] = ra;
    R[16] = s0;
    R[17] = s1;
}

/* 8006C3E4 (text, length, x, y, [CLUT row, font]): draw a text from its
 * last character to its first, right to left from x (0xFFFF ends it
 * early). Font 0: 14x14 cells, 18 to a row; font 1: 8x8, 32 to a row;
 * any other font draws nothing. */
void n_results_DrawText(void) {
    FRAME(0x38);
    u32 text = A0;
    s32 i = (s32)((A1 & 0xFFFF) - 2);
    u32 x = A2;
    u16 y = (u16)A3;
    u32 row = GS(u32, sp + 0x48), font = GS(u32, sp + 0x4C);
    u32 b = BUF_INDEX;
    u32 s = SPRITES + b * 0xFA0 + (u32)g_sprites * 0x14;
    u32 ot = OT_TAGS + b * 0x40;
    if (i < 0) return;
    u32 p = text + (u32)i * 2;
    for (;;) {
        u16 c = GV(u16, p);
        if (c == 0xFFFF) break;
        if (font == 0) {
            u32 q = c / 18;
            u32 col = (u32)(c - q * 18) & 0xFFFF;
            GV(u16, s + 0x12) = 0xE;
            GV(u16, s + 0x10) = 0xE;
            GV(u8, s + 0xC) = (u8)(col * 14);
            GV(u8, s + 0xD) = (u8)((q & 0xFFFF) * 14);
            GV(u16, s + 0xE) = (u16)GCALL2(F_GET_CLUT, 0x170, row + 0x1E3);
            x -= 14;
            GV(u16, s + 8) = (u16)x;
            GV(u16, s + 0xA) = y;
            add_prim(ot, s);
            g_sprites++;
            s += 0x14;
        } else if (font == 1) {
            GV(u16, s + 0x12) = 8;
            GV(u16, s + 0x10) = 8;
            GV(u8, s + 0xC) = (u8)((c & 0x1F) << 3);
            GV(u8, s + 0xD) = (u8)((c >> 5) << 3);
            GV(u16, s + 0xE) = (u16)GCALL2(F_GET_CLUT, 0x170, row + 0x1E3);
            x -= 8;
            GV(u16, s + 8) = (u16)x;
            GV(u16, s + 0xA) = y;
            add_prim(ot + 4, s);
            g_sprites++;
            s += 0x14;
        }
        p -= 2;
        if (--i < 0) break;
        LOOP_POLL(0x8006C618u);
    }
}

/* 8006C650: draw the screen's texts, then the draw modes for the two
 * fonts' ordering table entries. */
void n_results_DrawTexts(void) {
    FRAME(0x30);
    u32 b = BUF_INDEX;
    u32 m = MODES + b * 0x30 + (u32)g_modes * 0xC;
    u32 ot = OT_TAGS + b * 0x40;
    u32 e = TEXTS;
    if (GV(u32, e) != 0xFFFFFFFFu) {
        for (;;) {
            GS(u32, sp + 0x10) = GV(u32, e + 0x10);
            GS(u32, sp + 0x14) = GV(u32, e + 0x14);
            gcall(0x8006C3E4u, GV(u32, e), GV(u16, e + 4), GV(u32, e + 8), GV(u32, e + 0xC));
            e += 0x18;
            if (GV(u32, e) == 0xFFFFFFFFu) break;
            LOOP_POLL(0x8006C704u);
        }
    }
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_SET_DRAW_MODE_, m, 0, 0, 0x15);
    add_prim(ot, m);
    g_modes++;
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_SET_DRAW_MODE_, m + 0xC, 0, 0, 0x16);
    add_prim(ot + 4, m + 0xC);
    g_modes++;
    V0 = (u32)g_modes;
}

/* 8006C7E0: the background picture: two sprites (256 and 64 wide, both
 * 240 high) from texture pages 0x1D and 0x1F, behind everything. */
void n_results_DrawBackground(void) {
    FRAME(0x40);
    GS(u32, sp + 0x10) = 0;
    u32 b = BUF_INDEX;
    u32 s = SPRITES + b * 0xFA0 + (u32)g_sprites * 0x14;
    u32 m = MODES + b * 0x30 + (u32)g_modes * 0xC;
    u32 ot = OT_TAGS + b * 0x40 + 8;
    GCALL4(F_SET_DRAW_MODE_, m, 0, 0, 0x9D);
    GV(u16, s + 0x10) = 0x100;
    GV(u16, s + 0x12) = 0xF0;
    GV(u16, s + 8) = 0;
    GV(u16, s + 0xA) = 0;
    GV(u8, s + 0xC) = 0;
    GV(u8, s + 0xD) = 0;
    GV(u16, s + 0xE) = 0x7814;
    add_prim(ot, s);
    g_sprites++;
    add_prim(ot, m);
    g_modes++;
    s += 0x14;
    m += 0xC;
    GS(u32, sp + 0x10) = 0;
    GCALL4(F_SET_DRAW_MODE_, m, 0, 0, 0x9F);
    GV(u16, s + 0x10) = 0x40;
    GV(u16, s + 0x12) = 0xF0;
    GV(u16, s + 8) = 0x100;
    GV(u16, s + 0xA) = 0;
    GV(u8, s + 0xC) = 0;
    GV(u8, s + 0xD) = 0;
    GV(u16, s + 0xE) = 0x7814;
    add_prim(ot, s);
    g_sprites++;
    add_prim(ot, m);
    g_modes++;
    V0 = (u32)g_modes;
}

/* 8006CA28: work out the results into the record and the screen's
 * digit arrays: hit average, kill average, clear time, continues, guns
 * found (a star each, gun 7 not counted), and the rank. */
void n_results_Compute(void) {
    FRAME(0x20);
    s32 shots = GV(s32, SHOTS);
    if (shots == 0) GV(u32, g_record + 4) = 0;
    else {
        s32 hits = GV(s32, SHOTS + 4);
        if (!(hits < shots)) GV(u32, g_record + 4) = 100;
        else GV(u32, g_record + 4) = (u32)mips_div((s32)((u32)hits * 100), shots);
    }
    s32 enemies = GV(s32, KILLS), killAvg;
    if (enemies == 0) killAvg = 0;
    else {
        s32 killed = GV(s32, KILLS + 4);
        if (!(killed < enemies)) killAvg = 100;
        else killAvg = mips_div((s32)((u32)killed * 100), enemies);
    }
    u32 r = g_record;
    GV(u32, r + 0x10) = GV(u32, SHOTS + 8);
    if (!(GV(u32, SHOTS + 8) < 10000)) GV(u32, r + 0x10) = 9999;
    GV(u32, r + 0x18) = GV(u32, SHOTS + 0xC);
    u32 flags = g_gunsFound;
    s32 guns = 0;
    for (u32 k = 2;;) {
        if ((flags & (0x80000000u >> k)) && k != 7) guns++;
        if (++k >= 10) break;
        LOOP_POLL(0x8006CB74u);
    }
    guns++;
    GV(u32, g_record + 8) = (u32)guns;
    for (s32 k = 0;;) {
        GV(u16, 0x8006D9D0u + (u32)k * 2) = 0x100;
        if (++k >= guns) break;
        LOOP_POLL(0x8006CBACu);
    }
    GV(u32, g_record + 0xC) = g_continues;
    GCALL2(0x8006D50Cu, 0x8006D9E4u, GV(u32, g_record + 0x10));
    GCALL2(0x8006D62Cu, 0x8006D9F4u, GV(u32, g_record + 4));
    GCALL1(0x8006CE14u, GV(u8, g_record + 4));
    GCALL1(0x8006CCE4u, GV(u32, g_record + 0x18));
    GCALL2(0x8006D50Cu, 0x8006DA18u, GV(u32, g_record + 0xC));
    s32 shown = GV(s32, KILLS + 8);
    GCALL2(0x8006D50Cu, 0x8006DA4Cu, shown < 10000 ? (u32)shown : 9999);
    GCALL2(0x8006D62Cu, 0x8006DA5Cu, (u32)killAvg);
    for (s32 k = 0;;) {
        if (!(k < GV(s32, KILLS + 0x10))) GV(u16, 0x8006DA34u + (u32)k * 2) = 0;
        if (++k >= 9) break;
        LOOP_POLL(0x8006CC98u);
    }
    if (g_rankSeen == 0 && GV(u32, g_record) == 4) g_rankSeen = 1;
}

/* 8006CCE4 (frames): the clear time into its digit array (hours, capped
 * at 99, then minutes and seconds; two digits each). */
void n_results_TimeDigits(void) {
    u32 secs = A0 / 30;
    u32 h = secs / 3600;
    secs -= h * 3600;
    s32 min = (s32)(secs / 60), sec = (s32)(secs - (u32)min * 60);
    s32 hh = (s32)h;
    if (!(hh < 99)) hh = 99;
    const u32 d = 0x8006DA00u;
    s32 t = hh / 10;
    GV(u16, d) = (u16)(GV(u16, d) + t);
    GV(u16, d + 2) = (u16)(GV(u16, d + 2) + (hh - t * 10));
    t = min / 10;
    GV(u16, d + 6) = (u16)(GV(u16, d + 6) + t);
    GV(u16, d + 8) = (u16)(GV(u16, d + 8) + (min - t * 10));
    t = sec / 10;
    GV(u16, d + 0xC) = (u16)(GV(u16, d + 0xC) + t);
    GV(u16, d + 0xE) = (u16)(GV(u16, d + 0xE) + (sec - t * 10));
}

/* 8006CE14 (hit average): the rank. Five scores of up to 100 each (hit
 * average; 100 less 20 a continue... see below; kill average less 0x41;
 * the clear time over 1 hour, 1 point less a minute; 100 less 25 for
 * each of 0x80062DBE), less 50, over 101: rank 0 to 4. Rank 4 is 3 if
 * 0x80062DF8 is 1. Its glyph goes to the screen's text. */
void n_results_Rank(void) {
    s32 a = (s32)(A0 & 0xFF);
    if (!(a < 101)) a = 100;
    s32 b = 100 - (s32)GV(u8, 0x80062E0Cu) * 20;
    if (b < 0) b = 0;
    u32 r = g_record;
    u32 cv = (u32)GV(u16, r + 0x10) - 0x41;
    u32 c;
    if ((s16)cv < 0) c = 0;
    else if ((s16)cv < 101) c = cv;
    else c = 100;
    u32 t = GV(u32, r + 0x18), dv;
    if (0x1A5E0u < t) {
        dv = 100 - (t - 0x1A5E0u) / 1800;
        if ((s16)dv < 0) dv = 0;
    } else {
        dv = 100;
    }
    s32 e = 100 - (s32)g_continues * 25;
    if (e < 0) e = 0;
    s32 sum = (s16)a + (s16)b + (s16)c + (s16)dv + (s16)e - 50;
    s32 rank = sum / 101;
    GV(s32, r) = rank;
    if (rank == 4 && GV(u32, 0x80062DF8u) == 1) GV(s32, r) = 3;
    GV(u16, 0x8006DA14u) = GV(u16, RANK_LETTERS + (u32)GV(s32, g_record) * 2);
}

/* 8006D01C: step the fade (down to 0, where it stops, or up to 0xFF,
 * where it stays drawn) and, while it's drawn, put its tile and draw
 * mode in front. */
void n_results_Fade(void) {
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

/* 8006D198: start: the record, state 0, and the results. */
void n_results_Begin(void) {
    FRAME(0x18);
    GCALL1(0x80011994u, 1);
    GV(u8, 0x80062E1Cu) = 0;
    g_record = RECORD;
    GV(u32, RECORD + 0x14) = 0;
    g_state = 0;
    GCALL0(0x8006CA28u);
}

/* 8006D2B0: build a frame: clear the ordering table, the fade, the texts,
 * the background; the frame is ready for results_Flip. Returns 1. */
void n_results_Frame(void) {
    FRAME(0x18);
    GCALL3(F_GS_CLEAR_OT, 0, 0, OTS + (u32)BUF_INDEX * 0x14);
    GCALL0(0x8006D01Cu);
    g_sprites = 0;
    g_modes = 0;
    GCALL0(0x8006C650u);
    GCALL0(0x8006C7E0u);
    g_frameReady = 1;
    V0 = 1;
}

/* 8006D320: load and upload the pictures: the fonts' file, then the
 * rank's picture (file by rank) to 0x80100000. */
void n_results_Load(void) {
    FRAME(0x28);
    GCALL4(F_FILE_LOAD2, 0x8006C128u, 0x8005856Cu, 0x1D, 0x80100000u);
    GCALL1(0x8006D3B0u, GV(u32, 0x80100004u) + 0x80100000u);
    GCALL1(0x8006D3B0u, GV(u32, 0x80100008u) + 0x80100000u);
    GS(u32, sp + 0x10) = 0x80100000u;
    GCALL4(F_FILE_LOAD, 0x8006C140u, 0x80058570u, 0x8006DC78u, GV(u32, g_record));
    GCALL1(0x8006D3B0u, 0x80100000u);
}

/* 8006D3B0 (tim): upload a TIM's image and (if it has one) its CLUT. */
void n_results_UploadTim(void) {
    FRAME(0x20);
    u32 tim = A0;
    if (GV(u32, tim) != 0x10) { V0 = 0x10; return; }
    GCALL2(F_GS_GET_TIM_INFO, tim + 4, TIM_INFO);
    GCALL2(F_LOAD_IMAGE, TIM_INFO + 4, GV(u32, TIM_INFO + 0xC));
    GCALL1(F_DRAW_SYNC, 0);
    if (((GV(u32, TIM_INFO) >> 3) & 1) && (GV(u16, TIM_INFO + 0x10) | GV(u16, TIM_INFO + 0x12)) != 0)
        GCALL2(F_LOAD_IMAGE, TIM_INFO + 0x10, GV(u32, TIM_INFO + 0x18));
    GCALL1(F_DRAW_SYNC, 0);
}

/* 8006D44C: the fade's tiles (full screen, black, translucent) and draw
 * modes, one per buffer. */
void n_results_InitFade(void) {
    FRAME(0x30);
    for (u32 i = 0;;) {
        u32 t = FADE_TILES + i * 0x10, m = FADE_MODES + i * 0xC;
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
        GCALL4(F_SET_DRAW_MODE_, m, 0, 0, 0x40);
        if (++i >= 2) break;
        LOOP_POLL(0x8006D4E4u);
    }
}

/* 8006D50C / 8006D62C (digits, value): add value's last 6 (or 4) decimal
 * digits to a digit array (most significant first). A 0 digit after the
 * first becomes a blank (0xFFFF) where the digit above it is 0 too, or
 * in the top place: the game's leading-zero blanking (which also blanks
 * zeros inside a number, as 1001 shows). */
static void digits(u32 dst, s32 value, s32 n, u32 poll) {
    for (s32 i = 0;;) {
        s32 q = mips_div(value, (s32)GCALL2(0x8006D8DCu, 10, (u32)i));
        s32 d = q - (q / 10) * 10;
        u32 slot = dst + (u32)(n - 1 - i) * 2;
        GV(u16, slot) = (u16)(GV(u16, slot) + d);
        if (d == 0 && i != 0) {
            if (!(i < n - 1)) GV(u16, slot) = 0xFFFF;
            else {
                s32 q2 = mips_div(value, (s32)GCALL2(0x8006D8DCu, 10, (u32)(i + 1)));
                if (q2 == (q2 / 10) * 10) GV(u16, slot) = 0xFFFF;
            }
        }
        if (++i >= n) break;
        LOOP_POLL(poll);
    }
}

void n_results_Digits6(void) {
    FRAME(0x30);
    digits(A0, (s32)A1, 6, 0x8006D600u);
}

void n_results_Digits4(void) {
    FRAME(0x30);
    digits(A0, (s32)A1, 4, 0x8006D720u);
}

/* 8006D74C (state 0): fade in (speed 8), to state 1. */
void n_results_StateFadeIn(void) {
    FRAME(0x18);
    GCALL1(0x8006D890u, 8);
    V0 = (u32)++g_state;
}

/* 8006D778 (state 1): the pad: start, or a newly pressed 0x20 button
 * (not with pad kind 0x8000), goes on to state 2. */
void n_results_StatePad(void) {
    FRAME(0x20);
    g_padLast = g_pad;
    u32 raw = GCALL1(F_PAD_BUTTONS, 0);
    u32 v = GCALL1(F_MAP_BUTTONS, raw);
    g_pad = v & 0xFFFF;
    if (g_padKind == 0x8000) return;
    if (((v & 0x800) && !(g_padLast & 0x800)) || ((v & 0x20) && !(g_padLast & 0x20))) g_state++;
}

/* 8006D828 (state 2): fade out (speed 8), to state 3. */
void n_results_StateFadeOut(void) {
    FRAME(0x18);
    GCALL1(0x8006D8B0u, 8);
    V0 = (u32)++g_state;
}

/* 8006D854 (state 3): once the fade is all black (flags 6), done. */
void n_results_StateWaitFade(void) {
    FRAME(0x18);
    if ((GCALL0(0x8006D8CCu) & 0xFFFF) == 6) GV(u32, g_record + 0x14) = 1;
}

/* 8006D890 (speed): fade from black: level 0xFF, coming down. */
void n_results_FadeInStart(void) {
    GV(u16, FADE + 2) = (u16)((A0 << 8) | 5);
    GV(u16, FADE) = 0xFF;
    V0 = 0xFF;
}

/* 8006D8B0 (speed): fade to black: level 0, going up. */
void n_results_FadeOutStart(void) {
    GV(u16, FADE + 2) = (u16)((A0 << 8) | 6);
    GV(u16, FADE) = 0;
    V0 = FADE;
}

/* 8006D8CC: the fade's flags. */
void n_results_FadeFlags(void) {
    V0 = GV(u16, FADE + 2);
}

/* 8006D8DC (base, n): base to the power n (n 0 or more). */
void n_results_Pow(void) {
    u32 base = A0;
    s32 n = (s32)A1;
    if (n == 0) { V0 = 1; return; }
    if (n == 1) { V0 = base; return; }
    u32 r = base;
    if (n >= 2) {
        for (s32 k = 2;;) {
            r *= base;
            if (n < ++k) break;
            LOOP_POLL(0x8006D914u);
        }
    }
    V0 = r;
}

/* 8006D1E0: the screen's loop: until the record's done flag (+0x14),
 * the state's handler, the frame and the vertical blank (mode switch). */
void n_results_Loop(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x1C, R[31]);
    MEM_W32(sp + 0x18, R[18]);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 in_s0 = R[16], in_s1 = R[17], in_s2 = R[18];
    if (MEM_R32(GV(u32, 0x8006FD4Cu) + 0x14) == 0) {
        R[18] = 0x8006DCA0u;
        R[17] = 0x80070000u;
        R[16] = 0x80070000u;
        for (;;) {
            gcall_ra(0x8006D238u, GV(u32, 0x8006DCA0u + (GV(u32, 0x8006DC98u) << 2)), A0, A1, A2, A3);
            gcall_ra(0x8006D240u, 0x8006D2B0u, A0, A1, A2, A3);
            gcall_ra(0x8006D248u, 0x8001306Cu, 1, A1, A2, A3);
            if (MEM_R32(GV(u32, 0x8006FD4Cu) + 0x14) != 0) break;
            LOOP_POLL(0x8006D258u);
        }
    }
    R[16] = in_s0;
    R[17] = in_s1;
    R[18] = in_s2;
}

/* 8006D278: back to the kernel: the sound stopped, the frame callbacks
 * cleared, then mode 4's overlay. */
void n_results_Exit(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall_ra(0x8006D288u, 0x80011D48u, 1, A1, A2, A3);
    gcall_ra(0x8006D290u, 0x80011558u, 0, A1, A2, A3);
    gcall_ra(0x8006D298u, 0x80011564u, 0, A1, A2, A3);
    gcall_ra(0x8006D2A0u, 0x80012FF4u, 4, A1, A2, A3);
}

/* 8006D150: SCORE's main: the results worked out, the pictures, the
 * display, the fade, the screen's loop, then back to the kernel. */
void n_results_Main(void) {
    FRAME(0x18);
    MEM_W32(sp + 0x10, R[31]);
    gcall_ra(0x8006D160u, 0x8006D198u, A0, A1, A2, A3);
    gcall_ra(0x8006D168u, 0x8006D320u, A0, A1, A2, A3);
    gcall_ra(0x8006D170u, 0x8006C154u, A0, A1, A2, A3);
    gcall_ra(0x8006D178u, 0x8006D44Cu, A0, A1, A2, A3);
    gcall_ra(0x8006D180u, 0x8006D1E0u, A0, A1, A2, A3);
    gcall_ra(0x8006D188u, 0x8006D278u, A0, A1, A2, A3);
}
