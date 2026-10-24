/* Message windows: the text boxes of the game (TEST overlay).
 *
 * Text is drawn as 8x14 glyph sprites (libgs GsSPRITE, 0x24 bytes) from a
 * font page of 18 glyphs per row (tpage 0x15, CLUT at x 0x170, the y row
 * picks the colour). Two buffers of 0x100 sprites at 0x800B60E8, one per
 * frame buffer (0x80062D1C), sorted into the ordering table of that buffer
 * (0x800AFC88, or 0x800B4D40 while 0x800BA9F8 says the window is drawn on
 * top of the menu screen).
 *
 * The message box (0x800BA9F4 = 1 opens it, 2 while it shows) lays out up
 * to 8 text entries (0x800BA8E8, 16 bytes each, word 0 non-zero = in use)
 * through 0x8007D034, which counts the sprites in 0x800BA968. Its prompt
 * (0x800BA9FC):
 *   1, 6, 7  a blinking "more" marker (0x800BA978, two spare sprites),
 *            not drawn for 7
 *   3        a two-way choice: left/right toggles 0x800BA9C8 and the
 *            chosen answer's highlight (0x800A16C0 / 0x800A16CA) blinks
 *   4        waits for the confirm button
 *   +100     confirmed: the box closes when the text code sets it to 0
 * 0x800BA9C0 is the blink timer (0..29, shown below 20).
 *
 * The scrolling text window (0x800A8678) has up to 7 lines of u16 glyph
 * codes, 60 bytes apart and ended by 0xFFFE (0 = a blank glyph, 0xFCxx
 * switches to colour xx), with per-line y positions; it's clipped to a
 * band of rows starting at its scroll position.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "poll.h"

void n_font_LoadPage(void);
void n_msg_AddEntry(void);
void n_msg_StopEnemies(void);
void n_msg_Layout(void);
void n_msg_Show(void);
void n_caption_Run(void);
void n_op_Caption(void);
void n_msg_InitSprites(void);
void n_msg_Cancel(void);
void n_msg_DrawText(void);
void n_hint_DrawIcon(void);
void n_msg_Update(void);
void n_textwin_Draw(void);

#define F_FILE_LOAD       0x800115C0u   /* (name, ?, table, file index, [dest]) */
#define F_TEXTURE_UPLOAD  0x8006F370u
#define F_GS_SORT_SPRITE  0x8002C4B8u   /* GsSortSprite(sprite, ot, pri) */
#define F_MSG_OPEN        0x8007CEB8u   /* msg_StopEnemies */
#define F_MSG_CLOSE       0x8007CF08u
#define F_MSG_LAYOUT      0x8007D034u   /* (entry, sprite) -> next free sprite */
#define F_MSG_ADD_ENTRY   0x8007CE4Cu   /* (text, x, y, z, [lines, c, e]) */
#define F_CAPTION_RUN     0x8008AD38u

#define g_frameBuf        GV(u8, 0x80062D1C)    /* drawing buffer 0/1 */
#define GLYPHS            0x800B60E8u           /* 2 x 0x100 GsSPRITEs */
#define GLYPHS_PER_BUF    0x100
#define SPRITE_SIZE       0x24
#define OT_MAIN           0x800AFC88u           /* GsOT per buffer, 0x14 bytes */
#define OT_MENU           0x800B4D40u

#define g_msgState        GV(s32, 0x800BA9F4)   /* 1 open, 2 showing */
#define g_msgOverMenu     GV(s32, 0x800BA9F8)
#define g_msgEntries      0x800BA8E8u           /* 8 x 16 bytes */
#define g_msgSprites      GV(s16, 0x800BA968)
#define MSG_MARKER        0x800BA978u           /* "more" marker sprite per buffer */
#define g_msgPrompt       GV(s16, 0x800BA9FC)
#define g_msgBlink        GV(s32, 0x800BA9C0)
#define g_msgChoice       GV(s32, 0x800BA9C8)
#define g_msgConfirmLock  GV(s16, 0x800BA9D0)
#define g_msgFlag9E0      GV(s32, 0x800BA9E0)
#define g_choiceLit0      GV(s16, 0x800A16C0)
#define g_choiceLit1      GV(s16, 0x800A16CA)

#define PROMPT_CLOSING    100

/* GsSPRITE fields */
#define SP_ATTR   0x00
#define SP_X      0x04
#define SP_Y      0x06
#define SP_W      0x08
#define SP_H      0x0A
#define SP_TPAGE  0x0C
#define SP_U      0x0E
#define SP_V      0x0F
#define SP_CX     0x10
#define SP_CY     0x12
#define SP_R      0x14
#define SP_G      0x15
#define SP_B      0x16
#define SP_MX     0x18
#define SP_MY     0x1A
#define SP_SCALEX 0x1C
#define SP_SCALEY 0x1E
#define SP_ROTATE 0x20

static inline u32 glyph_buffer(void) { return GLYPHS + (u32)g_frameBuf * (GLYPHS_PER_BUF * SPRITE_SIZE); }
static inline u32 msg_ot(void) { return (g_msgOverMenu == 1 ? OT_MAIN : OT_MENU) + (u32)g_frameBuf * 0x14; }

/* 8007CC74 (dest, which): load a font page file (0x188, 0x186 or 0x187
 * for 0-2) to dest and upload it. Any other value loads the file number
 * the caller left in a3. */
void n_font_LoadPage(void) {
    FRAME(0x20);
    u32 dest = A0;
    u32 file = A3;
    switch ((s32)A1) {
    case 0: file = 0x188; break;
    case 1: file = 0x186; break;
    case 2: file = 0x187; break;
    }
    GS(u32, sp + 0x10) = dest;                          /* argument 5 */
    GCALL4(F_FILE_LOAD, 0x8006C32C, 0x80058534, 0x800A195C, file);
    GCALL1(F_TEXTURE_UPLOAD, dest);
}

static void init_glyph(u32 s, u16 h, u16 tpage, u8 v) {
    GV(u32, s + SP_ATTR) = 0;
    GV(s16, s + SP_X) = 0;  GV(s16, s + SP_Y) = 0;
    GV(u16, s + SP_W) = 8;  GV(u16, s + SP_H) = h;
    GV(u16, s + SP_TPAGE) = tpage;
    GV(u8, s + SP_U) = 0;   GV(u8, s + SP_V) = v;
    GV(s16, s + SP_CX) = 0x170; GV(s16, s + SP_CY) = 0x1E3;
    GV(u8, s + SP_R) = 0x80; GV(u8, s + SP_G) = 0x80; GV(u8, s + SP_B) = 0x80;
    GV(s16, s + SP_MX) = 0; GV(s16, s + SP_MY) = 0;
    GV(s16, s + SP_SCALEX) = 0; GV(s16, s + SP_SCALEY) = 0;
    GV(s32, s + SP_ROTATE) = 0;
}

/* 8007CD04: set up the glyph sprites, the "more" markers (16x8 at u 0x98
 * on tpage 0x15, positioned at y 0x3E) and clear the message entries. */
void n_msg_InitSprites(void) {
    for (u32 i = 0; i < 2 * GLYPHS_PER_BUF; i++) init_glyph(GLYPHS + i * SPRITE_SIZE, 0xE, 0x15, 0);
    for (u32 k = 0; k < 2; k++) {
        u32 s = MSG_MARKER + k * SPRITE_SIZE;     /* only these fields */
        GV(s16, s + SP_X) = 8;  GV(s16, s + SP_Y) = 0x3E;
        GV(u16, s + SP_W) = 0x10; GV(u16, s + SP_H) = 8;
        GV(u16, s + SP_TPAGE) = 0x15;
        GV(u8, s + SP_U) = 0x98; GV(u8, s + SP_V) = 0;
        GV(s16, s + SP_CX) = 0x170; GV(s16, s + SP_CY) = 0x1E3;
        GV(u8, s + SP_R) = 0x80; GV(u8, s + SP_G) = 0x80; GV(u8, s + SP_B) = 0x80;
    }
    for (int i = 7; i >= 0; i--) GV(u32, g_msgEntries + (u32)i * 0x10) = 0;
    g_msgOverMenu = 0;
    g_msgState = 0;
    g_msgFlag9E0 = 0;
    V0 = 0x800C0000u;
}

/* 8007D7D0: drop the message box if one is up. */
void n_msg_Cancel(void) {
    if (g_msgState != 0) {
        g_msgBlink = 0;
        GV(u32, g_msgEntries) = 0;
        g_msgState = 0;
    }
    V0 = 0x800C0000u;
}

static int pressed(s32 mask) { return (g_buttons & mask) && !(g_buttonsPrev & mask); }

/* The two-way choice: left/right (the buttons depend on the controller)
 * switch the answer. */
static void choice_step(void) {
    u32 pad = g_padTypeBit;
    s32 mask;
    if (pad == 0x200000) mask = 0x840;
    else if (pad == 0x80000 || pad == 0x400000) mask = BTN_LEFT | BTN_RIGHT;
    else mask = 0;
    if (mask && pressed(mask)) {
        g_msgBlink = 0;
        g_choiceLit1 = 0;
        g_choiceLit0 = 0;
        g_msgChoice = (g_msgChoice - 1) & 1;
    }
    if (g_msgBlink < 20) {
        if (g_msgChoice == 0) g_choiceLit0 = 2;
        else g_choiceLit1 = 2;
    } else {
        g_choiceLit1 = 0;
        g_choiceLit0 = 0;
    }
}

/* 8007D7FC: the message box, once a frame. */
void n_msg_Update(void) {
    FRAME(0x28);
    if (g_msgState == 1) {
        if (g_msgOverMenu == 0) {
            GCALL0(F_MSG_OPEN);
            g_playerFrozen = 1;
        }
        D_800A85B8 = 0;
        g_msgState++;
    } else if (g_msgState != 2) {
        return;
    }

    /* lay out and draw the text */
    g_msgSprites = 0;
    u32 s = glyph_buffer();
    for (u32 i = 0; i < 8; i++) {
        u32 e = g_msgEntries + i * 0x10;
        if (GV(u32, e) != 0) s = GCALL2(F_MSG_LAYOUT, e, s);
    }
    s = glyph_buffer();
    for (s32 i = 0; i < g_msgSprites; i++, s += SPRITE_SIZE)
        GCALL3(F_GS_SORT_SPRITE, s, msg_ot(), 0);

    switch (g_msgPrompt) {
    case 1: case 6: case 7:
        if (g_msgBlink < 20 && g_msgPrompt != 7)
            GCALL3(F_GS_SORT_SPRITE, MSG_MARKER + (u32)g_frameBuf * SPRITE_SIZE, msg_ot(), 0);
        break;
    case 3:
        choice_step();
        break;
    case 4:
        if (pressed(BTN_FIRE) && g_msgPrompt < PROMPT_CLOSING) {
            g_msgPrompt += PROMPT_CLOSING;
            g_msgConfirmLock = 0;
        }
        break;
    }

    if (g_msgPrompt != 0) {
        /* confirm: prompts other than the choice close at once; prompt 4
         * only while not locked */
        if (pressed(BTN_FIRE) && g_msgPrompt != 4) {
            if (g_msgPrompt < PROMPT_CLOSING) g_msgPrompt += PROMPT_CLOSING;
        } else if (g_msgPrompt == 4 && g_msgConfirmLock == 0) {
            if (g_msgPrompt < PROMPT_CLOSING) g_msgPrompt += PROMPT_CLOSING;
        }
        if (++g_msgBlink >= 30) g_msgBlink = 0;
        if (g_msgPrompt != 0) return;
    }
    if (g_msgOverMenu == 0) {
        GCALL0(F_MSG_CLOSE);
        g_playerFrozen = 0;
    }
    g_msgState = 0;
}

/* 8007DD24: draw the scrolling text window. */
void n_textwin_Draw(void) {
    FRAME(0x38);
    u32 w = 0x800A8678u;
    if (GV(s8, w + 0x1AA) == 0) { V0 = 0; return; }   /* hidden */
    u32 attr = GV(u32, w + 0x1A4);
    u16 top = GV(u16, w + 0x1BC);                       /* first visible row */
    u32 bottom = (u32)top + GV(u8, w + 0x1A8);          /* + visible height */
    u8 shade = GV(u8, w + 0x1AB);
    u32 s = glyph_buffer();
    s16 cy = 0x1E3;                                     /* CLUT row: colour */
    s32 n = 0;
    for (s16 line = 0; line < 7; line++) {
        s32 y = GV(s16, w + 0x1AC + line * 2);
        if (!((s16)top < y + 14)) continue;             /* scrolled out above */
        if (!(y < (s16)bottom)) break;                  /* below the window */
        u32 c = w + (u32)line * 60;
        u16 x = GV(u16, w + 0x1BA);
        u32 gy = (u32)y;
        s32 h = 14;                                     /* < 0: clipped at the bottom */
        if (y < (s16)top) { h = 14 - (s32)((u32)top - (u32)y); gy = top; }
        else if ((s16)bottom < y + 14) h = (s32)((u32)y - bottom);
        u16 ch = GV(u16, c);
        if (ch == 0xFFFE) continue;
        do {
            if ((u16)(ch + 0x400) < 0x100) {            /* 0xFCxx: colour */
                cy = (s16)((ch & 0xFF) + 0x1E3);
            } else {
                if (GV(u16, c) != 0) {
                    u16 g = GV(u16, c);
                    u16 row = g / 18;
                    GV(u16, s + SP_X) = x;
                    GV(u16, s + SP_Y) = (u16)gy;
                    GV(u16, s + SP_TPAGE) = 0x15;
                    GV(s16, s + SP_CX) = 0x170;
                    GV(s16, s + SP_CY) = cy;
                    GV(u8, s + SP_U) = (u8)((u16)(g - row * 18) << 3);
                    if ((s16)h >= 0) {
                        GV(u16, s + SP_H) = (u16)h;
                        GV(u8, s + SP_V) = (u8)(row * 14 + 14 - h);   /* skip the clipped rows */
                    } else {
                        GV(u16, s + SP_H) = (u16)-h;
                        GV(u8, s + SP_V) = (u8)(row * 14);
                    }
                    if (GV(u16, c) >= 0x120) {            /* second font page */
                        GV(u8, s + SP_V) = (u8)(GV(u8, s + SP_V) + 0x20);
                        GV(u16, s + SP_TPAGE) = (u16)(GV(u16, s + SP_TPAGE) + 1);
                    }
                    s += SPRITE_SIZE;
                    n++;
                }
                x += 8;
            }
            c += 2;
            ch = GV(u16, c);
        } while (ch != 0xFFFE);
    }
    s = glyph_buffer();
    if ((s16)n <= 0) { V0 = (u32)n << 16; return; }
    for (s16 i = 0; i < (s16)n; i++, s += SPRITE_SIZE) {
        GV(u8, s + SP_B) = shade;
        GV(u8, s + SP_G) = shade;
        GV(u8, s + SP_R) = shade;
        GV(u32, s + SP_ATTR) = attr;
        GCALL3(F_GS_SORT_SPRITE, s, OT_MAIN + (u32)g_frameBuf * 0x14, 0);
    }
    V0 = 0;
}

/* 8007E064 (buffer): draw the text of the message in 0x800BA9E8 (text,
 * x, y) as glyph sprites of that buffer, while 0x800BA9E0 is 2 or more:
 * 0xFFFE starts a new line (14 down), 0xFFFF ends, 0 is a space. */
void n_msg_DrawText(void) {
    FRAME(0x28);
    u32 buf = A0;
    s32 state = g_msgFlag9E0;
    if (state == 0 || state == 1) return;
    u32 s = GLYPHS + buf * (GLYPHS_PER_BUF * SPRITE_SIZE);
    u32 text = GV(u32, 0x800BA9E8);
    s32 x0 = GV(s32, 0x800BA9EC), y0 = GV(s32, 0x800BA9F0);
    u32 dx = 0, dy = 0;
    s32 n = 0;
    for (int done = 0; !done; text += 2) {
        u16 ch = GV(u16, text);
        if (ch == 0xFFFE) { dx = 0; dy += 14; continue; }
        if (ch == 0xFFFF) { done = 1; continue; }
        if (ch != 0) {
            u16 row = ch / 18;
            GV(u16, s + SP_X) = (u16)(x0 + dx);
            GV(u16, s + SP_Y) = (u16)(y0 + dy);
            GV(u16, s + SP_W) = 8;
            GV(u16, s + SP_H) = 0xE;
            GV(u16, s + SP_TPAGE) = 0x15;
            GV(s16, s + SP_CX) = 0x170;
            GV(s16, s + SP_CY) = 0x1E3;
            GV(u8, s + SP_U) = (u8)((u16)(ch - row * 18) << 3);
            GV(u8, s + SP_V) = (u8)(row * 14);
            GV(u32, s + SP_ATTR) = 0;
            GV(u8, s + SP_B) = 0x80;
            GV(u8, s + SP_G) = 0x80;
            GV(u8, s + SP_R) = 0x80;
            if (ch >= 0x120) {                      /* second font page */
                GV(u8, s + SP_V) = (u8)(GV(u8, s + SP_V) + 0x20);
                GV(u16, s + SP_TPAGE) = (u16)(GV(u16, s + SP_TPAGE) + 1);
            }
            s += SPRITE_SIZE;
            n++;
        }
        dx += 8;
    }
    s = GLYPHS + buf * (GLYPHS_PER_BUF * SPRITE_SIZE);
    for (s32 i = 0; i < n; i++, s += SPRITE_SIZE)
        GCALL3(F_GS_SORT_SPRITE, s, OT_MAIN + buf * 0x14, 0);
}

/* 8007F16C: on alternate 32 frames, draw the controller's button hint
 * sprite (one for 0x200000 pads, another for the others) on the menu. */
void n_hint_DrawIcon(void) {
    FRAME(0x18);
    if (GV(u32, 0x80062E10) & 0x20) return;
    u32 pad = GCALL1(0x80013B90u, 0);
    u32 sprite;
    if (pad == 0x200000) sprite = 0x800BAFC8u;
    else if (pad == 0x400000 || pad == 0x80000) sprite = 0x800BAFF0u;
    else return;
    GCALL3(F_GS_SORT_SPRITE, sprite, OT_MENU + (u32)g_frameBuf * 0x14, 0);
}

/* ---- laying out the message box text ---- */

#define TW_WAIT       GV(u8, 0x800BA9DC)    /* typewriter: frames to the next glyph */
#define TW_SPEED      GV(u8, 0x800BA9DD)    /* frames per glyph (0: all at once) */
#define TW_SHOWN      GV(u8, 0x800BA9DE)    /* glyphs revealed so far */
#define TW_COUNT      GV(u8, 0x800BA9DF)    /* glyphs counted this frame */
#define g_msgWait     GV(s16, 0x800BA9D0)   /* 0xFExx: frames left to wait */
#define g_msgWaitCode GV(u16, 0x800BA9D2)   /* the 0xFExx code waited for */
#define g_msgSavedHw  GV(u16, 0x800BA9D4)   /* text halfword replaced by a stop */
#define g_msgSavedAt  GV(u32, 0x800BA9D8)   /* (address after it) */
#define g_choiceOn    GV(s32, 0x800BA9C4)
#define g_choiceText  GV(u32, 0x800BA9CC)   /* where the text continues after a choice */
#define g_choiceAnswer GV(s32, 0x800BAA00)
#define CHOICE_TEXT   0x800A16BEu

/* 8007CE4C (text, x, y, z, [lines, c, e]): put a text in the first free
 * message entry; 0 if there was one, 1 if all 8 are in use. */
void n_msg_AddEntry(void) {
    u32 sp = R[29];
    s32 lines = (s32)GS(u32, sp + 0x10);
    u16 c = GS(u16, sp + 0x14), d = GS(u16, sp + 0x18);
    for (u32 i = 0;; i++) {
        u32 e = g_msgEntries + i * 0x10;
        if (GV(u32, e) == 0) {
            GV(u32, e) = A0;
            GV(u16, e + 4) = (u16)A1;
            GV(u16, e + 6) = (u16)A2;
            GV(u16, e + 8) = (u16)A3;
            GV(u16, e + 0xA) = (u16)(lines - 1);
            GV(u16, e + 0xC) = c;
            GV(u16, e + 0xE) = d;
            V0 = 0;
            return;
        }
        if (i + 1 >= 8) break;
        LOOP_POLL(0x8007CEA8);
    }
    V0 = 1;
}

/* 8007CEB8: while a message is up, stop every enemy of kind 1 (byte 0
 * cleared). */
void n_msg_StopEnemies(void) {
    u32 e = GV(u32, 0x800AB510);   /* the enemy records */
    if (GV(s32, 0x800AF530) <= 0) { V0 = (u32)GV(s32, 0x800AF530); return; }
    for (s32 i = 0;; i++, e += 0x8C4) {
        if (GV(u8, e + 0x2A) == 1) GV(u8, e) = 0;
        if (!(i + 1 < GV(s32, 0x800AF530))) break;
        LOOP_POLL(0x8007CEF8);
    }
    V0 = 0;
}

/* Move the sprites from first up to s one line (14) up. */
static void scroll_up(u32 first, u32 s, u32 pc) {
    for (u32 p = first;;) {
        GV(u16, p + SP_Y) = (u16)(GV(u16, p + SP_Y) - 0xE);
        p += SPRITE_SIZE;
        if (p == s) break;
        LOOP_POLL(pc);
    }
}

/* 8007D034 (entry, sprite): lay out a message entry's text as glyph
 * sprites from sprite on; returns the next free sprite.
 *
 * Codes in the text: 0 a space; 0xFCxx colour xx; 0xFBxx (first) a
 * typewriter speed; 0xFExx wait xx frames then prompt 4; 0xFFFE a new
 * line (or, past the entry's line limit, stop with prompt 6); 0xFF00
 * stop with prompt 1 (more), 0xFF01 prompt 7, 0xFFFF the end (prompt 2,
 * or 3 when a choice was shown); 0xFE00 ends the message. 0xFD00/0xFD01
 * show the two-way choice (the text at 0x800A16C0, first or second answer
 * lit) and 0xFD02 continues after it.
 *
 * A confirmed prompt (100 + n, from msg_Update) first moves the text on:
 * 101 past the 0xFF00, 102/103 ends the message (103 keeps the choice's
 * answer in 0x800BAA00), 104 past the 0xFExx waited for (an 0xFF01 right
 * after it is put in its place, remembering the halfword it replaced),
 * 106 past two lines, 107 past the 0xFF01 (restoring that halfword). */
void n_msg_Layout(void) {
    u32 e = A0, s = A1, first = A1;
    u32 t = GV(u32, e);
    s32 dx = 0, dy = 0, lines = 0, done = 0;
    u16 cy = 0x1E3;
    u32 k = (u32)(s32)(s16)(u16)(g_msgPrompt - 0x65);
    if (k < 7) {
        LOOP_POLL(0x8007D0B4);
        switch (k) {
        case 0:                                         /* 101 */
            t += 2;
            for (;;) {
                u16 c = GV(u16, t);
                t += 2;
                if (c == 0xFF00) break;
                LOOP_POLL(0x8007D0CC);
            }
            GV(u32, e) = t;
            g_msgPrompt = 0;
            break;
        case 2:                                         /* 103 */
            g_choiceAnswer = g_msgChoice;
            /* fall through */
        case 1:                                         /* 102 */
            g_msgPrompt = 0;
            GV(u32, e) = 0;
            TW_SPEED = 0;
            V0 = s;
            return;
        case 3: {                                       /* 104 */
            u16 code = g_msgWaitCode;
            t += 2;
            for (;;) {
                u16 c = GV(u16, t);
                t += 2;
                if (c == code) break;
                LOOP_POLL(0x8007D120);
            }
            if (GV(u16, t) != 0xFF01) {
                GV(u32, e) = t;
                g_msgPrompt = 0;
            } else {
                g_msgSavedAt = t;
                g_msgSavedHw = GV(u16, t - 2);
                GV(u16, t - 2) = GV(u16, t);
                t = GV(u32, e);
            }
            break;
        }
        case 4:                                         /* 105 */
            break;
        case 5:                                         /* 106 */
            for (;;) {
                if (GV(u16, t) == 0xFFFE) lines++;
                t += 2;
                if (!(lines < 2)) break;
                LOOP_POLL(0x8007D188);
            }
            lines = 0;
            GV(u32, e) = t;
            TW_SHOWN = 0;
            break;
        case 6: {                                       /* 107 */
            t += 2;
            for (;;) {
                u16 c = GV(u16, t);
                t += 2;
                if (c == 0xFF01) break;
                LOOP_POLL(0x8007D1B4);
            }
            u16 saved = g_msgSavedHw;
            if (saved != 0) {
                GV(u16, g_msgSavedAt - 2) = saved;
                g_msgSavedHw = 0;
                g_msgSavedAt = 0;
            }
            t += 2;
            GV(u32, e) = t;
            g_msgPrompt = 0;
            break;
        }
        }
    }

    if ((u16)(GV(u16, t) + 0x4FF) < 0xFF) {             /* 0xFBxx: typewriter */
        u8 speed = GV(u8, t);
        t += 2;
        TW_SHOWN = 0;
        TW_SPEED = speed;
        TW_WAIT = speed;
        GV(u32, e) = t;
    }
    TW_COUNT = 0;

    if (g_msgSprites < 0x100) {
        for (;;) {
            if (TW_SPEED != 0) {
                g_msgPrompt = 5;
                u8 cnt = TW_COUNT;
                if (TW_SHOWN == cnt) {                  /* revealed so far: stop here */
                    u8 w = TW_WAIT;
                    if (w != 0) { TW_WAIT = (u8)(w - 1); goto tail; }
                    TW_WAIT = TW_SPEED;                 /* reveal the next glyph */
                    for (;;) {
                        TW_SHOWN = (u8)(TW_SHOWN + 1);
                        u16 c = GV(u16, GV(u32, e) + (u32)TW_SHOWN * 2);
                        if (c == 0) { LOOP_POLL(0x8007D2DC); continue; }
                        if (c == 0xFFFE) { LOOP_POLL(0x8007D2E4); continue; }
                        break;
                    }
                    goto tail;
                }
                TW_COUNT = (u8)(cnt + 1);
            }
            u16 c = GV(u16, t);
            if ((u16)(c + 0x400) < 0x100) {             /* 0xFCxx: colour */
                cy = (u16)((c & 0xFF) + 0x1E3);
                t += 2;
            }
            c = GV(u16, t);
            if ((u16)(c + 0x1FF) < 0xFF) {              /* 0xFExx: wait */
                if (g_msgWait == 0) {
                    g_msgWaitCode = c;
                    g_msgWait = GV(u8, t);
                } else {
                    g_msgWait = (s16)(g_msgWait - 1);
                }
                g_msgPrompt = 4;
                goto tail;
            }
            switch (c) {
            case 0xFE00:                                /* end of the message */
                done = 1;
                GV(u32, e) = 0;
                g_msgPrompt = 0;
                TW_SPEED = 0;
                break;
            case 0xFF01:
                g_msgPrompt = 7;
                done = 1;
                break;
            case 0xFF00:
                g_msgPrompt = 1;
                done = 1;
                break;
            case 0xFFFF:
                g_msgPrompt = g_choiceOn != 0 ? 3 : 2;
                done = 1;
                break;
            case 0xFFFE: {                              /* new line */
                s16 max = GV(s16, e + 0xA);
                if (!(lines < max)) { g_msgPrompt = 6; done = 1; break; }
                if (s == first) break;
                lines++;
                dx = 0;
                if (max >= 3) dy += 0xE;
                else scroll_up(first, s, 0x8007D4F0);
                break;
            }
            case 0xFD00:
            case 0xFD01:                                /* the choice */
                if (g_choiceOn == 0) {
                    g_msgChoice = c == 0xFD00 ? 0 : 1;
                    g_choiceOn = 1;
                    if (c == 0xFD00) g_choiceLit0 = 2;
                    else g_choiceLit1 = 2;
                }
                g_choiceText = t;
                t = CHOICE_TEXT;
                break;
            case 0xFD02:                                /* back from the choice */
                t = g_choiceText;
                break;
            case 0:
                dx += 8;
                break;
            default: {                                  /* a glyph */
                u16 g = GV(u16, t);
                GV(u16, s + SP_X) = (u16)(GV(u16, e + 4) + dx);
                GV(u16, s + SP_H) = 0xE;
                GV(u16, s + SP_Y) = (u16)(GV(u16, e + 6) + dy);
                GV(u8, s + SP_U) = (u8)((g % 18) << 3);
                GV(s16, s + SP_CX) = 0x170;
                GV(u16, s + SP_CY) = cy;
                GV(u16, s + SP_TPAGE) = 0x15;
                GV(u8, s + SP_V) = (u8)((g / 18) * 14);
                GV(u32, s + SP_ATTR) = 0;
                GV(u8, s + SP_B) = 0x80;
                GV(u8, s + SP_G) = 0x80;
                GV(u8, s + SP_R) = 0x80;
                if (GV(u16, t) >= 0x120) {              /* second font page */
                    GV(u8, s + SP_V) = (u8)(GV(u8, s + SP_V) + 0x20);
                    GV(u16, s + SP_TPAGE) = (u16)(GV(u16, s + SP_TPAGE) + 1);
                }
                s += SPRITE_SIZE;
                dx += 8;
                g_msgSprites = (s16)(g_msgSprites + 1);
                break;
            }
            }
            t += 2;
            if (done || !(g_msgSprites < 0x100)) break;
            LOOP_POLL(0x8007D620);
        }
    }
tail:
    if (TW_SPEED != 0 && lines < GV(s16, e + 0xA) && s != first)
        scroll_up(first, s, 0x8007D670);
    V0 = s;
}

/* 8007D6E0 (text, kind): open the message box with a text (unless one is
 * up): kind picks the position and line count (0-4; 4 is drawn over the
 * menu). */
void n_msg_Show(void) {
    FRAME(0x28);
    u32 text = A0, kind = A1;
    if (g_msgState != 0) { V0 = (u32)g_msgState; return; }
    if (kind >= 5) { V0 = 0; return; }
    LOOP_POLL(0x8007D71C);
    s32 x, y, z, n;
    switch (kind) {
    case 0: x = -0x60; y = 0x30; z = 0x10; n = 2; break;
    case 1: x = -0x38; y = 0x2A; z = 0xC; n = 4; break;
    case 2: x = -0x7E; y = 0x4E; z = 0x12; n = 2; break;
    case 3: x = -0x7E; y = 0x3E; z = 0x12; n = 3; break;
    default: x = -0x41; y = 0x30; z = 0x10; n = 2; kind = 1; break;
    }
    GS(u32, sp + 0x10) = (u32)n;
    GS(u32, sp + 0x14) = 0;
    GS(u32, sp + 0x18) = 0;
    GCALL4(F_MSG_ADD_ENTRY, text, x, y, z);
    g_msgState = 1;
    g_msgOverMenu = (s32)kind;
    V0 = 0x800C0000u;
}

/* ---- captions (the script's subtitles, drawn by msg_DrawText) ---- */

#define CAPTION_TEXT   0x800BB570u          /* 0x100 halfwords */
#define SCRIPT_TEXTS   GV(u32, 0x800BBB7C)  /* the room's texts: s16 count, then offsets */
#define g_captionState GV(s32, 0x800BA9E0)
#define g_captionText  GV(u32, 0x800BA9E8)
#define g_captionX     GV(s32, 0x800BA9EC)
#define g_captionY     GV(s32, 0x800BA9F0)

/* A text of the room's text table (index). */
static u32 script_text(s16 idx) {
    u32 tab = SCRIPT_TEXTS;
    return tab + (u32)((s32)GV(s16, tab + (u32)(s32)idx * 4 + 4) / 2 * 2);
}

/* 8008AD38 (thread, state): script op: a caption (text pc[1], frames
 * pc[4]) shown page by page (two lines a page) for its time; the caption
 * state becomes state + 1. Thread fields: +0xA frames left, +0xC total,
 * +0xE pages. */
void n_caption_Run(void) {
    FRAME(8);
    u32 th = A0;
    u32 pc = GV(u32, th);
    if (GV(s16, SCRIPT_TEXTS) < GV(s16, pc + 2)) {     /* no such text */
        GV(u32, th) = pc + 0xA;
        V0 = 0;
        return;
    }
    s16 step = GV(s16, th + 4);
    if (step == 0) {
        for (s32 i = 0xFF;; i--) {
            GV(u16, CAPTION_TEXT + (u32)i * 2) = 0;
            if (i - 1 < 0) break;
            LOOP_POLL(0x8008ADC4);
        }
        u32 dst = CAPTION_TEXT;
        u32 src = script_text(GV(s16, pc + 2));
        s32 pages = 1, nl = 0;
        u16 c = GV(u16, src);
        if (c != 0xFFFF) {
            for (;;) {
                GV(u16, dst) = c;
                if (c == 0xFFFE && !(++nl < 2)) {       /* every second line: a page */
                    GV(u16, dst) = 0xFFFF;
                    pages++;
                    nl = 0;
                }
                src += 2;
                c = GV(u16, src);
                dst += 2;
                if (c == 0xFFFF) break;
                LOOP_POLL(0x8008AE5C);
            }
        }
        GV(u16, dst) = GV(u16, src);
        u16 frames = GV(u16, GV(u32, th) + 8);
        GV(u16, th + 0xE) = (u16)pages;
        GV(u16, th + 4) = (u16)(step + 1);
        GV(u16, th + 0xC) = frames;
        GV(u16, th + 0xA) = frames;
        V0 = 1;
        return;
    }
    if (step == 2) { GV(u16, th + 4) = (u16)(step + 1); V0 = 1; return; }
    if (step != 1) { GV(u32, th) = pc + 0xA; V0 = 0; return; }

    /* step 1: show the page for the time elapsed */
    s32 total = GV(s16, th + 0xC);
    s32 per = mips_div(total, GV(s16, th + 0xE));
    s32 page = mips_div((s32)((u32)total - (u32)(s32)GV(s16, th + 0xA)), per);
    u32 p = CAPTION_TEXT;
    if (page > 0) {
        s32 n = 0;
        for (;;) {
            if (GV(u16, p) == 0xFFFF) n++;
            p += 2;
            if (n == page) break;
            LOOP_POLL(0x8008AEE8);
        }
    }
    /* y: 0x4D, or 0x3F for a page of two lines */
    s32 y = 0x4D;
    if (GV(u16, p) != 0xFFFF) {
        u32 q = p;
        u16 c = GV(u16, q);
        for (;;) {
            if (c == 0xFFFE) {
                if (GV(u16, q + 2) != 0xFFFF) y = 0x3F;
                break;
            }
            q += 2;
            c = GV(u16, q);
            if (c == 0xFFFF) break;
            LOOP_POLL(0x8008AF40);
        }
    }
    /* the longer line's glyph count (counted on the game's stack, as the
     * original does) */
    GS(u32, sp + 4) = 0;
    GS(u32, sp + 0) = 0;
    if (GV(u16, p) != 0xFFFF) {
        u32 q = p, cnt = sp;
        u16 c = GV(u16, q);
        for (;;) {
            if (c == 0xFFFE) cnt += 4;
            else GS(u32, cnt) = GS(u32, cnt) + 1;
            q += 2;
            c = GV(u16, q);
            if (c == 0xFFFF) break;
            LOOP_POLL(0x8008AFA0);
        }
    }
    s32 n0 = (s32)GS(u32, sp), n1 = (s32)GS(u32, sp + 4);
    s32 w = n1 < n0 ? n0 * 4 : n1 * 4;
    g_captionText = p;
    g_captionX = -w;
    g_captionY = y;
    g_captionState = (s32)(A1 + 1);
    u16 left = (u16)(GV(u16, th + 0xA) - 1);
    GV(u16, th + 0xA) = left;
    if ((s16)left < 3) GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
    V0 = 1;
}

/* 8008B038 (thread): script op: caption, state 1. */
void n_op_Caption(void) {
    FRAME(0x18);
    V0 = GCALL2(F_CAPTION_RUN, A0, 0);
}
