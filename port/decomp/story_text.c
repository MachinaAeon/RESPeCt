/* The story text before a new game (TYPEW overlay, loaded at 0x8006C118
 * in place of TEST): the typewriter that prints the prologue a character
 * at a time over a picture, with its own screen set-up, fades and frame
 * callback.
 *
 * The overlay's main loop (8006C480, kept translated: it runs for the
 * whole sequence) runs one state handler a frame from the table at
 * 0x8006F424 (state 0x8006FEB0), then draws the frame (8006CFD8):
 *   0  start the fade in from black
 *   1  type: every few frames (the delay of the last character) the next
 *      character; the music volume ramps up; START (or the end of the
 *      text, frame 0x960) ends it
 *   2  once it ends, start the fade out
 *   3  ramp the volume down; the loop ends once the fade has finished
 *
 * The text state is a record at 0x8006FDE0 (pointer at 0x80070848):
 *   +0 the text (u16 codes), +4 frames to the next character, +6 the
 *   picture shown (1-6, by frame), +8/+A the pen position, +C the index
 *   of the current code, +E the current code, +10 set when finished.
 * Codes up to 0x80 are glyphs (0x7F a space, 0x80 a new line); codes with
 * bit 8 are skipped. Each glyph typed is kept (0x8006FEE8, 6 bytes: x, y,
 * u, v) and drawn every frame as an 8x16 sprite.
 *
 * The game was compiled without optimisation here (every local through
 * $fp); the natives keep the original's frames (FRAME) and its return
 * addresses at every call (gcall_ra), as the frame callback (8006D6FC)
 * runs on the interrupt's stack.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "libgs.h"
#include "poll.h"

void n_story_TextCode(void);
void n_story_FadeInStart(void);
void n_story_Typing(void);
void n_story_EndWait(void);
void n_story_FadeOut(void);
void n_story_NextChar(void);
void n_story_PutChar(void);
void n_story_DrawFrame(void);
void n_story_InitScreen(void);
void n_story_FrameCallback(void);
void n_story_LoadData(void);
void n_story_LoadTim(void);
void n_story_InitFade(void);
void n_story_FadeFromBlack(void);
void n_story_FadeToBlack(void);
void n_story_FadeDraw(void);
void n_story_FadeMode(void);
void n_story_CheckSkip(void);
void n_story_DrawPrompt(void);
void n_story_SkipFade(void);
void n_story_InitPromptTiles(void);

/* ---- state ---- */
#define TW_TEXT         GV(u32, 0x80070848)   /* the text state record (address) */
#define TW(off, type)   GV(type, TW_TEXT + (off))
#define TW_CHAR_DELAY   GV(u16, 0x8006F434)   /* frames after the last character */
#define TW_FRAMES       GV(u32, 0x8006F438)   /* frames typed */
#define TW_ENDING       GV(s16, 0x8006F43C)
#define TW_STATE        GV(u32, 0x8006FEB0)
#define TW_VOLUME       GV(s16, 0x8006FEB4)   /* music volume ramp */
#define TW_FLIP         GV(u32, 0x8006FEB8)   /* a frame is ready to show */
#define TW_KEYS         GV(u32, 0x8006F420)   /* typing sound: key, 0-16 */
#define TW_GLYPHS_N     GV(u32, 0x8006FE78)
#define TW_GLYPHS       0x8006FEE8u           /* 6 bytes each: s16 x, y; u8 u, v */
#define TW_SPRITE_N     GV(u32, 0x8006F41C)   /* sprites used this frame */
#define TW_SPRITES      0x80070868u           /* [2 buffers][400] SPRT, 0x14 bytes */
#define TW_GSOT(b)      (0x8006FEC0u + (u32)(b) * 20)   /* GsOT per buffer */
#define TW_OTAG(b)      (0x8006FDF8u + (u32)(b) * 64)   /* its tags */
#define TW_DRMODE(b)    (0x80070850u + (u32)(b) * 12)
#define TW_SOUND_VOL    GV(s16, 0x800637E0)
#define TW_MONO         GV(u32, 0x80062E20)   /* sound output setting: 0 = mono */
/* the fade over everything */
#define TW_FADE_MODE    GV(u16, 0x8006FD1A)   /* bit 0 fading in, 1 out, 2 drawn; speed << 8 */
#define TW_FADE_LEVEL   GV(u16, 0x8006FD18)
#define TW_FADE_TILES   0x8006FD1Cu           /* [2] TILE, 0x10 bytes */
#define TW_FADE_MODES   0x8006FD3Cu           /* [2] DR_MODE, 0xC bytes */
/* the skip prompt (a text box of two tiles) */
#define TW_PAD          GV(u32, 0x8006FDD8)
#define TW_PAD_PREV     GV(u32, 0x8006FDDC)
#define TW_SKIP_ON      GV(u32, 0x8006F418)
#define TW_PROMPT_ON    GV(u32, 0x8006F404)   /* frames to show the prompt text */
#define TW_PROMPT_T     GV(u32, 0x8006F408)
#define TW_PROMPT_TILES 0x8006FD98u           /* [2 buffers][2] TILE, 0x10 bytes */

/* ---- game functions ---- */
#define F_TEXT_CODE     0x8006C420u
#define F_NEXT_CHAR     0x8006CAF4u
#define F_PUT_CHAR      0x8006CC04u
#define F_LOAD_TIM      0x8006D994u
#define F_FADE_FROM     0x8006DBD4u
#define F_FADE_TO       0x8006DC20u
#define F_FADE_DRAW     0x8006DC68u
#define F_FADE_MODE     0x8006DE7Cu
#define F_CHECK_SKIP    0x8006E15Cu
#define F_DRAW_PROMPT   0x8006E3C0u
#define F_SKIP_FADE     0x8006EC18u
#define F_SND_MVOL      0x80015484u   /* (l, r, l2, r2): master volume */
#define F_SPU_PLAY_NOTE 0x8001F348u   /* (key, handle, hi, lo, [4], [5], [6], [7]) */
#define F_TW_SET_SPRT   0x80038328u   /* primitive set-up (SPRT) */
#define F_TW_SEMI_TRANS 0x80038248u
#define F_TW_DRAW_MODE  0x800383E8u   /* (p, dfe, dtd, tpage, [tw]) */
#define F_TW_ADD_PRIM   0x800381E8u   /* (ot, prim) */
#define F_TW_LOAD_IMAGE 0x80035620u
#define F_TW_DRAW_SYNC  0x80035374u
#define F_TW_TIM_INFO   0x8002E048u
#define F_TW_INIT_GRAPH 0x8002C7C8u   /* (w, h, mode, dither, [vram]) */
#define F_TW_DEF_DISP   0x8002D6F8u   /* (x0, y0, x1, y1): double buffer */
#define F_TW_RESET      0x8002A350u   /* keeps $ra in a global */
#define F_SET_CALLBACK  0x80011558u   /* (fn): frame callback */
#define F_SET_CALLBACK2 0x80011564u
#define F_PAD_BUTTONS   0x80013A5Cu
#define F_MAP_BUTTONS   0x8001189Cu

#define TW_SPRT(b, i)   (TW_SPRITES + (u32)(b) * 0x1F40 + (u32)(i) * 0x14)

/* addPrim inlined: link prim at the head of the list at tag ot. */
static void link_prim(u32 ot, u32 p) {
    GV(u32, p) = (GV(u32, p) & 0xFF000000u) | (GV(u32, ot) & 0xFFFFFFu);
    GV(u32, ot) = (GV(u32, ot) & 0xFF000000u) | (p & 0xFFFFFFu);
}

/* Music volume for ramp level v: full on the left only in mono, halved
 * on both sides in stereo. */
static void ramp_volume(u32 ra_mono, u32 ra, s16 v) {
    if (TW_MONO == 0) {
        gcall_ra(ra_mono, F_SND_MVOL, (u32)(s32)v, 0, (u32)(s32)v, 0);
    } else {
        s16 h = (s16)(v / 2);
        gcall_ra(ra, F_SND_MVOL, (u32)(s32)h, (u32)(s32)h, (u32)(s32)h, (u32)(s32)h);
    }
}

/* 8006C420 (index): the text code at index. */
void n_story_TextCode(void) {
    FRAME(0x10);
    u16 i = (u16)A0;
    V0 = GV(u16, TW(0, u32) + (u32)i * 2);
}

/* 8006C6D0: state 0: fade in from black (speed 8), on to typing. */
void n_story_FadeInStart(void) {
    FRAME(0x18);
    gcall_ra(0x8006C6ECu, F_FADE_FROM, 8, A1, A2, A3);
    TW_STATE += 1;
}

/* 8006C71C: state 1: typing. The volume ramps up (by 7, to 0x102); the
 * next character when its delay is up; START, or frame 0x960, ends it
 * (state 2, music off). */
void n_story_Typing(void) {
    FRAME(0x18);
    if (TW_VOLUME < 0x103) {
        ramp_volume(0x8006C780u, 0x8006C828u, TW_VOLUME);
        TW_VOLUME = (s16)(TW_VOLUME + 7);
    }
    if (TW(4, u16) == 0) {
        gcall_ra(0x8006C864u, F_NEXT_CHAR, A0, A1, A2, A3);
        TW(4, u16) = TW_CHAR_DELAY;
    } else {
        TW(4, u16) = (u16)(TW(4, u16) - 1);
    }
    if (gcall_ra(0x8006C8C0u, F_CHECK_SKIP, 0, 0, A2, A3) != 0 || TW_FRAMES == 0x960) {
        TW_STATE = 2;
        TW_ENDING = 1;
        TW_VOLUME = 0;
        gcall_ra(0x8006C91Cu, F_SND_MVOL, 0, 0, 0, 0);
    }
}

/* 8006C934: state 2: once ending, fade to black (speed 8), on to 3. */
void n_story_EndWait(void) {
    FRAME(0x18);
    if (TW_ENDING != 0) {
        gcall_ra(0x8006C964u, F_FADE_TO, 8, A1, A2, A3);
        TW_STATE += 1;
    }
}

/* 8006C994: state 3: the fade out. Done (+10) once the fade is full;
 * the volume ramps down by 0x11 while above 0. */
void n_story_FadeOut(void) {
    FRAME(0x18);
    if ((gcall_ra(0x8006C9ACu, F_FADE_MODE, A0, A1, A2, A3) & 0xFFFF) == 6) TW(0x10, u16) = 1;
    if (TW_VOLUME > 0) {
        ramp_volume(0x8006CA1Cu, 0x8006CAC4u, TW_VOLUME);
        TW_VOLUME = (s16)(TW_VOLUME - 0x11);
    }
}

/* 8006CAF4: type the next character: a glyph code (up to 0x80) is put;
 * codes with bit 8 are passed over. */
void n_story_NextChar(void) {
    FRAME(0x20);
    for (;;) {
        u16 c = TW(0xE, u16);
        if (c < 0x81) {
            gcall_ra(0x8006CB48u, F_PUT_CHAR, c, A1, A2, A3);
            return;
        }
        if (!(c & 0x100)) return;
        TW(0xC, u16) = (u16)(TW(0xC, u16) + 1);
        TW(0xE, u16) = (u16)gcall_ra(0x8006CBB4u, F_TEXT_CODE, TW(0xC, u16), A1, A2, A3);
        LOOP_POLL(0x8006CBC4);
    }
}

/* 8006CC04 (code): put one code: 0x7F a space, 0x80 a new line, the rest
 * glyphs (with the key sound; a glyph's width and the delay after it
 * from the table at 0x8006C11C). Then on to the next code. */
void n_story_PutChar(void) {
    FRAME(0x30);
    u16 c = (u16)A0;
    u32 k = TW_KEYS + 1;
    TW_KEYS = k;
    if ((s32)k >= 0x11) TW_KEYS = 0;
    if (c == 0x7F) {
        TW_CHAR_DELAY = 3;
        TW(8, u16) = (u16)(TW(8, u16) + 8);
    } else if (c == 0x80) {
        GS(u32, sp + 0x10) = 0x3C;
        GS(u32, sp + 0x14) = 0x7F;
        GS(u32, sp + 0x18) = 0x30;
        GS(u32, sp + 0x1C) = 0x30;
        gcall_ra(0x8006CCF0u, F_SPU_PLAY_NOTE, (u32)(s32)GV(s16, 0x8006F420), (u32)(s32)TW_SOUND_VOL, 0, 0);
        TW(8, u16) = 0x10;
        TW(0xA, u16) = (u16)(TW(0xA, u16) + 0x10);
        TW_CHAR_DELAY = 0xC;
    } else {
        GS(u32, sp + 0x10) = 0x3C;
        GS(u32, sp + 0x14) = 0x7F;
        GS(u32, sp + 0x18) = 0x30;
        GS(u32, sp + 0x1C) = 0x30;
        gcall_ra(0x8006CD80u, F_SPU_PLAY_NOTE, (u32)(s32)GV(s16, 0x8006F420), (u32)(s32)TW_SOUND_VOL, 0, 0);
        TW_CHAR_DELAY = 5;
        u32 g = TW_GLYPHS + TW_GLYPHS_N * 6;
        GV(u16, g) = TW(8, u16);
        GV(u16, g + 2) = TW(0xA, u16);
        GV(u8, g + 4) = (u8)((c & 0x1F) << 3);
        GV(u8, g + 5) = (u8)(((c >> 5) << 4) - 0x40);
        TW_GLYPHS_N += 1;
        /* the advance: 8, narrower for some glyphs (a jump table on
         * codes 2-0x3F); the last two (0x3E, 0x3F) pause longer */
        u32 w = 8;
        if (c >= 2 && c < 0x40) {
            LOOP_POLL(0x8006CEC4);                         /* the jump table */
            switch (c) {
            case 0x02: case 0x04: case 0x09: case 0x12: case 0x13: case 0x22: case 0x28: case 0x2A:
                w = 7; break;
            case 0x08: case 0x0B:
                w = 6; break;
            case 0x3E: case 0x3F:
                TW_CHAR_DELAY = 0x14; break;
            }
        }
        TW(8, u16) = (u16)(TW(8, u16) + w);
    }
    TW(0xC, u16) = (u16)(TW(0xC, u16) + 1);
    TW(0xE, u16) = (u16)gcall_ra(0x8006CFB0u, F_TEXT_CODE, TW(0xC, u16), A1, A2, A3);
}

/* 8006CFD8: build the frame: clear its table, the fade, a sprite per
 * glyph typed, the picture's sprite and draw mode (tpage 0x16), then
 * flag it ready for the frame callback. The picture follows the frame
 * count: 1 from frame 0, 2 at 0x168, 3 at 0x3C0, 4 at 0x438, 5 at 0x4EC,
 * 6 at 0x654. */
void n_story_DrawFrame(void) {
    FRAME(0x30);
    u32 b = BUF_INDEX;
    gcall_ra(0x8006D024u, F_GS_CLEAR_OT, 0, 0, TW_GSOT(b), A3);
    gcall_ra(0x8006D02Cu, F_FADE_DRAW, A0, A1, A2, A3);
    TW_SPRITE_N = 0;
    for (s32 i = 0; i < (s32)TW_GLYPHS_N; i++) {
        b = BUF_INDEX;
        u32 s = TW_SPRT(b, TW_SPRITE_N);
        u32 g = TW_GLYPHS + (u32)i * 6;
        GV(u16, s + 8) = GV(u16, g);
        GV(u16, s + 0xA) = GV(u16, g + 2);
        GV(u8, s + 0xC) = GV(u8, g + 4);
        GV(u8, s + 0xD) = GV(u8, g + 5);
        GV(u16, s + 0x10) = 8;
        GV(u16, s + 0x12) = 0x10;
        GV(u16, s + 0xE) = 0x7917;
        link_prim(TW_OTAG(BUF_INDEX), s);
        TW_SPRITE_N += 1;
        LOOP_POLL(0x8006D254);
    }
    switch (TW_FRAMES) {
    case 0:     TW(6, u16) = 1; break;
    case 0x168: TW(6, u16) = 2; break;
    case 0x3C0: TW(6, u16) = 3; break;
    case 0x438: TW(6, u16) = 4; break;
    case 0x4EC: TW(6, u16) = 5; break;
    case 0x654: TW(6, u16) = 6; break;
    }
    b = BUF_INDEX;
    u32 s = TW_SPRT(b, TW_SPRITE_N);
    GV(u16, s + 0x10) = 0x100;
    GV(u16, s + 0x12) = 0x20;
    GV(u16, s + 8) = 0x20;
    GV(u16, s + 0xA) = 0xC0;
    GV(u8, s + 0xC) = 0;
    GV(u8, s + 0xD) = (u8)((TW(6, u8) - 1) << 5);
    GV(u16, s + 0xE) = 0x78D7;
    u32 dm = TW_DRMODE(BUF_INDEX);
    GS(u32, sp + 0x10) = 0;
    gcall_ra(0x8006D474u, F_TW_DRAW_MODE, dm, 0, 0, 0x16);
    link_prim(TW_OTAG(BUF_INDEX), dm);
    TW_FLIP = 1;
}

/* 8006D530: screen set-up: display off, graphics reset, 320x240
 * double buffered, the two ordering tables (4 tags each, 0x8006FDF8 and
 * 0x8006FE38), the frame callback (8006D6FC), and every glyph sprite
 * initialised (grey, opaque). */
void n_story_InitScreen(void) {
    FRAME(0x30);
    gcall_ra(0x8006D54Cu, F_SET_DISP_MASK, 0, A1, A2, A3);
    gcall_ra(0x8006D554u, F_TW_RESET, A0, A1, A2, A3);
    GS(u32, sp + 0x10) = 0;
    gcall_ra(0x8006D570u, F_TW_INIT_GRAPH, 0x140, 0xF0, 4, 1);
    gcall_ra(0x8006D588u, F_TW_DEF_DISP, 0, 0, 0, 0x100);
    GV(u32, 0x8006FED4) = 4;
    GV(u32, 0x8006FEC0) = 4;
    GV(u32, 0x8006FEC4) = 0x8006FDF8u;
    GV(u32, 0x8006FED8) = 0x8006FE38u;
    gcall_ra(0x8006D5D0u, F_SET_CALLBACK, 0x8006D6FCu, A1, A2, A3);
    gcall_ra(0x8006D5DCu, F_SET_CALLBACK2, 0, A1, A2, A3);
    for (s32 b = 0; b < 2; b++) {
        u32 s = TW_SPRT(b, 0);
        for (s32 i = 0; i < 400; i++) {
            gcall_ra(0x8006D658u, F_TW_SET_SPRT, s, A1, A2, A3);
            GV(u8, s + 4) = 0x80;
            GV(u8, s + 5) = 0x80;
            GV(u8, s + 6) = 0x80;
            gcall_ra(0x8006D68Cu, F_TW_SEMI_TRANS, s, 0, A2, A3);
            s += 0x14;
            LOOP_POLL(0x8006D6AC);
        }
        LOOP_POLL(0x8006D6C4);
    }
    TW_SPRITE_N = 0;
    TW_GLYPHS_N = 0;
    TW_FLIP = 0;
}

/* 8006D6FC: the frame callback: when a frame is ready, swap buffers, set
 * the draw offsets and clip for the new one (screen offset
 * 0x80062E18/1A), clear to black and draw its table.
 * It runs on the interrupt's stack, in compared RAM: the frame is the
 * original's ($ra and $fp saved, $fp = $sp, the clip rectangle at
 * sp + 0x10). */
void n_story_FrameCallback(void) {
    FRAME(0x20);
    u32 ra = R[31], fp = R[30];
    GS(u32, sp + 0x1C) = ra;
    GS(u32, sp + 0x18) = fp;
    R[30] = sp;
    if (TW_FLIP != 0) {
        gcall_ra(0x8006D728u, F_GS_SWAP_DISP_BUFF, A0, A1, A2, A3);
        if (BUF_INDEX == 0)
            gcall_ra(0x8006D764u, F_GS_SET_DRAW_OFFSETS, GV(u16, 0x80062E18), GV(u16, 0x80062E1A), 0, 0x100);
        else
            gcall_ra(0x8006D7A0u, F_GS_SET_DRAW_OFFSETS, 0, 0, GV(u16, 0x80062E18),
                     (u16)(GV(u16, 0x80062E1A) + 0x100));
        u16 ox = GV(u16, 0x80062E18);
        GS(u16, sp + 0x10) = ox;
        GS(u16, sp + 0x14) = GV(s16, 0x80062E18) < 0 ? (u16)((GV(s16, 0x80062E18) >> 1) + 0x140)
                                                     : (u16)(0x140 - GV(u16, 0x80062E18));
        GS(u16, sp + 0x12) = GV(u16, 0x80062E1A);
        GS(u16, sp + 0x16) = GV(s16, 0x80062E1A) < 0 ? (u16)((GV(s16, 0x80062E1A) >> 1) + 0xF0)
                                                     : (u16)(0xF0 - GV(u16, 0x80062E1A));
        gcall_ra(0x8006D864u, F_GS_SET_CLIP, sp + 0x10, A1, A2, A3);
        gcall_ra(0x8006D8A4u, F_GS_SORT_CLEAR, 0, 0, 0, TW_GSOT(BUF_INDEX));
        gcall_ra(0x8006D8D8u, F_GS_DRAW_OT, TW_GSOT(BUF_INDEX), A1, A2, A3);
        u32 v = gcall_ra(0x8006D8E0u, F_GS_GET_ACTIVE_BUFF, A0, A1, A2, A3);
        BUF_INDEX = (u8)v;
        TW_FLIP = 0;
        V0 = v;
    } else {
        V0 = 0;
    }
    R[30] = fp;
    R[31] = ra;
}

/* 8006D908: load the sequence's data file into 0x80138000 and upload the
 * picture (a TIM at the offset in its second word). */
void n_story_LoadData(void) {
    FRAME(0x28);
    GS(u32, sp + 0x10) = 0x80138000u;
    gcall_ra(0x8006D948u, F_FILE_LOAD, 0x8006C214u, 0x80058568u, 0x8006F40Cu, 0);
    gcall_ra(0x8006D97Cu, F_LOAD_TIM, GV(u32, 0x80138004u) + 0x80138000u, A1, A2, A3);
}

/* 8006D994 (TIM): upload a TIM (magic 0x10) to VRAM: the image, then its
 * palette when it has one. */
#define TW_TIM_INFO 0x8006F4E8u   /* GsIMAGE: pmode, x,y,w,h, pixels, cx,cy,cw,ch, clut */
void n_story_LoadTim(void) {
    FRAME(0x20);
    u32 p = A0;
    if (GV(u32, p) != 0x10) return;
    gcall_ra(0x8006D9FCu, F_TW_TIM_INFO, p + 4, TW_TIM_INFO, A2, A3);
    gcall_ra(0x8006DA14u, F_TW_LOAD_IMAGE, TW_TIM_INFO + 4, GV(u32, TW_TIM_INFO + 0xC), A2, A3);
    gcall_ra(0x8006DA20u, F_TW_DRAW_SYNC, 0, A1, A2, A3);
    if ((GV(u32, TW_TIM_INFO) >> 3) & 1) {
        if ((s16)(GV(u16, TW_TIM_INFO + 0x10) | GV(u16, TW_TIM_INFO + 0x12))) {
            gcall_ra(0x8006DA7Cu, F_TW_LOAD_IMAGE, TW_TIM_INFO + 0x10, GV(u32, TW_TIM_INFO + 0x18), A2, A3);
            gcall_ra(0x8006DA88u, F_TW_DRAW_SYNC, 0, A1, A2, A3);
        }
    }
}

/* 8006DAA0: the fade's primitives: a full-screen black tile per buffer,
 * subtractive, with its draw mode. */
void n_story_InitFade(void) {
    FRAME(0x30);
    u32 t = TW_FADE_TILES, dm = TW_FADE_MODES;
    for (s32 i = 0; i < 2; i++) {
        GV(u8, t + 3) = 3;
        GV(u8, t + 7) = 0x60;
        GV(u16, t + 8) = 0;
        GV(u16, t + 0xA) = 0;
        GV(u16, t + 0xC) = 0x140;
        GV(u16, t + 0xE) = 0xF0;
        gcall_ra(0x8006DB44u, F_TW_SEMI_TRANS, t, 1, A2, A3);
        GV(u8, t + 4) = 0;
        GV(u8, t + 5) = 0;
        GV(u8, t + 6) = 0;
        GS(u32, sp + 0x10) = 0;
        gcall_ra(0x8006DB84u, F_TW_DRAW_MODE, dm, 0, 0, 0x40);
        dm += 0xC;
        t += 0x10;
        LOOP_POLL(0x8006DBB4);
    }
}

/* 8006DBD4 (speed): start fading in from black. */
void n_story_FadeFromBlack(void) {
    FRAME(0x8);
    TW_FADE_MODE = (u16)(((u16)A0 << 8) | 5);
    TW_FADE_LEVEL = 0xFF;
    V0 = 0xFF;
}

/* 8006DC20 (speed): start fading to black. */
void n_story_FadeToBlack(void) {
    FRAME(0x8);
    u32 v = (u32)(u16)A0 << 8;
    TW_FADE_MODE = (u16)(v | 6);
    TW_FADE_LEVEL = 0;
    V0 = v;
}

/* 8006DC68: step the fade and, while it shows, add its tile (shade =
 * level) and draw mode to this frame. */
void n_story_FadeDraw(void) {
    FRAME(0x28);
    u16 mode = TW_FADE_MODE, lvl = TW_FADE_LEVEL;
    if (mode & 1) {
        lvl = (u16)(lvl - ((mode & 0xFF00) >> 8));
        if ((s16)lvl <= 0) { lvl = 0; mode = 0; }
    } else if (mode & 2) {
        lvl = (u16)(lvl + ((mode & 0xFF00) >> 8));
        if ((s16)lvl >= 0xFF) { lvl = 0xFF; mode = 6; }
    }
    TW_FADE_MODE = mode;
    TW_FADE_LEVEL = lvl;
    if (mode & 4) {
        u32 t = TW_FADE_TILES + (u32)BUF_INDEX * 16;
        u32 dm = TW_FADE_MODES + (u32)BUF_INDEX * 12;
        GV(u8, t + 4) = (u8)lvl;
        GV(u8, t + 5) = (u8)lvl;
        GV(u8, t + 6) = (u8)lvl;
        gcall_ra(0x8006DE34u, F_TW_ADD_PRIM, TW_OTAG(BUF_INDEX), t, A2, A3);
        gcall_ra(0x8006DE64u, F_TW_ADD_PRIM, TW_OTAG(BUF_INDEX), dm, A2, A3);
    }
}

/* 8006DE7C: the fade's mode (6: faded to black). */
void n_story_FadeMode(void) {
    FRAME(0x8);
    V0 = TW_FADE_MODE;
}

/* Pressed this frame (not last). */
#define NEWLY(bit) ((TW_PAD & (bit)) && !(TW_PAD_PREV & (bit)))

/* 8006E15C (buffer?, prompt buffer): read the pad; 1 if START was just
 * pressed (with a mouse-type pad (0x200000), also the right button). With
 * skipping on (0x8006F418), when it wasn't, the prompt is stepped and
 * drawn (8006EC18, 8006E3C0) and 0 returned. */
void n_story_CheckSkip(void) {
    FRAME(0x18);
    u32 a1 = A1;
    TW_PAD_PREV = TW_PAD;
    u32 raw = gcall_ra(0x8006E190u, F_PAD_BUTTONS, 0, A1, A2, A3);
    TW_PAD = gcall_ra(0x8006E19Cu, F_MAP_BUTTONS, raw, A1, A2, A3) & 0xFFFF;
    u32 pt = GV(u32, 0x80062E38);
    int start;
    if (pt == 0x8000) start = 0;
    else if (pt == 0x200000) start = NEWLY(0x40) && NEWLY(0x800);
    else start = NEWLY(0x800);
    if (start) { V0 = 1; return; }
    if (TW_SKIP_ON != 0) {
        gcall_ra(0x8006E2B0u, F_SKIP_FADE, A0, A1, A2, A3);
        gcall_ra(0x8006E2BCu, F_DRAW_PROMPT, a1, A1, A2, A3);
    }
    V0 = 0;
}

/* 8006E3C0 (buffer): draw the skip prompt's frame: its two tiles; while
 * 0x8006F404 counts down, also its text (a string of codes at
 * 0x8006F3F8, set out in columns from 0x8006F3FC/400: 0 = next line,
 * 0xFFFE = next column, 0xFFFF = end; 14x14 glyphs from tpage 0x15/0x16). */
void n_story_DrawPrompt(void) {
    FRAME(0x58);
    u32 b = A0;
    gcall_ra(0x8006E40Cu, F_GS_CLEAR_OT, 0, 0, TW_GSOT(b), A3);
    u32 ot = TW_OTAG(b);
    link_prim(ot, TW_PROMPT_TILES + b * 16);
    link_prim(ot, TW_PROMPT_TILES + 0x20 + b * 16);
    TW_PROMPT_T += 1;
    if (TW_PROMPT_ON == 0) {
        gcall_ra(0x8006E5B0u, F_GS_DRAW_OT, TW_GSOT(b), A1, A2, A3);
        return;
    }
    TW_PROMPT_ON -= 1;
    u32 s = TW_SPRT(b, 0);
    u32 p = GV(u32, 0x8006F3F8);
    u32 bx = GV(u32, 0x8006F3FC), by = GV(u32, 0x8006F400);
    u32 dx = 0, dy = 0;
    const u32 tpx = 0x170, tpy = 0x1E3;
    for (;;) {
        u16 c = GV(u16, p);
        int done = 0;
        if (c == 0xFFFE) {
            dx -= 0xE;
            dy = 0;
        } else if (c == 0xFFFF) {
            done = 1;
        } else if (c == 0) {
            dy += 0xE;
        } else {
            u16 g = c;
            if (g >= 0x9E && g < 0xA7) g = (u16)(g + 0x82);
            if (g >= 0xEE && g < 0xF7) g = (u16)(g + 0x3B);
            if (g == 1) g = 0x132;
            if (g >= 3 && g < 0xB) g = (u16)(g + 0x130);
            if (g >= 0x17 && g < 0x1A) g = (u16)(g + 0x124);
            if (g >= 0x3A && g < 0x3C) g = (u16)(g + 0x104);
            GV(u16, s + 8) = (u16)((u16)bx + (u16)dx);
            GV(u16, s + 0xA) = (u16)((u16)by + (u16)dy);
            GV(u16, s + 0x10) = 0xE;
            GV(u16, s + 0x12) = 0xE;
            GV(u8, s + 0xC) = (u8)((u16)(g % 18) * 14);
            GV(u8, s + 0xD) = (u8)((u16)(g / 18) * 14);
            GV(u16, s + 0xE) = (u16)((tpy << 6) | ((tpx >> 4) & 0x3F));
            if (g >= 0x120) {
                GV(u8, s + 0xD) = (u8)(GV(u8, s + 0xD) + 0x20);
                link_prim(ot + 4, s);
            } else {
                link_prim(ot, s);
            }
            s += 0x14;
            dy += 0xE;
        }
        p += 2;
        if (done) break;
        LOOP_POLL(0x8006EA6C);
    }
    u32 d1 = 0x8006FE80u + b * 24, d2 = 0x8006FE8Cu + b * 24;
    GS(u32, sp + 0x10) = 0;
    gcall_ra(0x8006EAE0u, F_TW_DRAW_MODE, d1, 0, 0, 0x15);
    link_prim(ot, d1);
    GS(u32, sp + 0x10) = 0;
    gcall_ra(0x8006EB60u, F_TW_DRAW_MODE, d2, 0, 0, 0x16);
    link_prim(ot + 4, d2);
    gcall_ra(0x8006EC00u, F_GS_DRAW_OT, TW_GSOT(b), A1, A2, A3);
}

/* 8006EC18: while skipping: the prompt text off, and the music volume
 * ramps up (by 0x11, to 0x10E). (The code after this, picking a prompt
 * from the table at 0x8006F484 by time, is never reached.) */
void n_story_SkipFade(void) {
    FRAME(0x20);
    TW_PROMPT_ON = 0;
    if (TW_VOLUME < 0x10F) {
        ramp_volume(0x8006EC84u, 0x8006ED2Cu, TW_VOLUME);
        TW_VOLUME = (s16)(TW_VOLUME + 0x11);
    }
}

/* 8006EEDC: the prompt's two tiles per buffer: black, opaque, 320x24 at
 * y 0 and 216. */
void n_story_InitPromptTiles(void) {
    FRAME(0x10);
    for (s32 i = 0; i < 2; i++) {
        u32 r = TW_PROMPT_TILES + (u32)i * 32;
        GV(u8, r + 3) = 3;
        GV(u8, r + 7) = 0x60;
        GV(u8, r + 7) = GV(u8, r + 7) & 0xFD;
        GV(u16, r + 0xC) = 0x140;
        GV(u16, r + 0xE) = 0x18;
        GV(u16, r + 8) = 0;
        GV(u16, r + 0xA) = (u16)(i * 216);
        GV(u8, r + 4) = 0;
        GV(u8, r + 5) = 0;
        GV(u8, r + 6) = 0;
        u32 w0 = GV(u32, r), w1 = GV(u32, r + 4), w2 = GV(u32, r + 8), w3 = GV(u32, r + 12);
        GV(u32, r + 0x10) = w0;
        GV(u32, r + 0x14) = w1;
        GV(u32, r + 0x18) = w2;
        GV(u32, r + 0x1C) = w3;
        LOOP_POLL(0x8006F10C);
    }
}
