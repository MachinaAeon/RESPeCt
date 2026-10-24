/* The TITLE overlay (TITLE.BIN, loaded at 0x8006C118 like TEST): the
 * title screen, LOAD GAME, and the save screen offered after dying or at
 * a payphone. Shared by title_screen.c, title_sprites.c, title_menu.c,
 * title_text.c and save_screen.c.
 *
 * TITLE's natives are listed under the module "title" in native.txt: the
 * recompiler only routes calls to them while TITLE is the module loaded
 * at that address (rt_call picks the loaded module by its signature), so
 * they never run in place of TEST's or another overlay's code there.
 *
 * Sprites: 72 records of 0x38 bytes at 0x800813E0, run every frame by
 * title_RunSprites (each one's update function, with T_CUR pointing at
 * it) and drawn by title_DrawSprites:
 *   +0 u16 colour mode   +2 u16 texture page bits (+0 << 7 | +2)
 *   +4 u16 u, +6 v       +8 s16 width, +A height (texels)
 *   +C u16 CLUT x, +E CLUT y
 *   +10 s16 x, +12 y (12.4 fixed point)
 *   +14 u16 width, +16 height on screen (8.8 with +22)
 *   +18 s16 ordering table slot
 *   +1A/+1C/+1E u16 red, green, blue (0x80 = as drawn)
 *   +20 u16 flags: 1 free, 2 shown, 4 placed, 8 semi-transparent,
 *       0x10 centred, 0x20/0x40/0x80 drawn by a text routine
 *   +22 u16 scale (0x100 = 1)   +24/+26 the update's own
 *   +30 text (for flags 0x20/0x40/0x80)   +34 update function
 * An image descriptor (16 bytes, copied to +0 by sprite_SetImage) gives
 * the texture fields.
 *
 * Text: two text buffers of 0x26C bytes at 0x80080F00 (window 0, the
 * message) and 0x8008116C (window 1), drawn at 0x800813D8[window]
 * (x, y). Byte codes: 0xFF end, 0xFE new line, 0xFC colour, 0xFA x move,
 * 0xF9 cursor mark, 0xF8 glyph width, 0xF7 line height, 0xF6 separator,
 * 0xF5 y move; below 0x10 a two-byte glyph, otherwise a glyph number + 0x10.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#ifndef TITLE_H
#define TITLE_H

#include "game_vars.h"
#include "poll.h"

/* ---- the title loop ---- */
#define T_START        GV(u32, 0x800805BC)   /* != 0 ends the title loop */
#define T_SCREEN       GV(s32, 0x800805D4)   /* which screen (title_Frame's table) */
#define T_MODE         GV(s32, 0x80080620)   /* how TITLE was entered: 0 title, 2 save screen, 3 ... */
#define T_FADE         GV(s32, 0x80080634)   /* screen brightness 0..0x80 */
#define T_FLASH        GV(s32, 0x8008063C)   /* white flash countdown */
#define T_FRAME_READY  GV(u32, 0x80080640)   /* a frame is drawn: the vsync callback shows it */
#define T_FRAMES       GV(u32, 0x80080630)   /* frame counter */
#define T_PAD          GV(u32, 0x800805CC)   /* buttons pressed (low 16), mouse bits */
#define T_PAD_PREV     GV(u32, 0x80080E48)
#define T_PAD2         GV(u32, 0x80080608)   /* port 2's buttons */
#define T_PAD2_PREV    GV(u32, 0x800805C0)
#define T_PICS         GV(s32, 0x800805C8)   /* first image of the set loaded (title_LoadImages), -1 none */
#define T_OFS_X        GV(s16, 0x80080610)   /* screen offset (doubled when drawn) */
#define T_OFS_Y        GV(s16, 0x80080612)
#define T_VOLUME       GV(s32, 0x8008061C)
#define T_STATE        GV(s32, 0x80080ED8)   /* the screen's own step / counters */
#define T_STATE2       GV(s32, 0x80080EDC)
#define T_STATE3       GV(s32, 0x80080EE0)
#define T_FLAGS        GV(u32, 0x80080ED4)
#define T_TIMER        GV(s32, 0x80080EEC)
#define T_IDLE         GV(s32, 0x80080EF0)   /* frames without input on the main menu */
#define T_MSG          GV(u32, 0x80080E88)   /* message shown in window 0 (address) */
#define T_CUR          GV(u32, 0x80080E50)   /* the sprite being updated */
#define T_TEXT_CLUT    GV(u32, 0x80080EF8)   /* text colour */
#define T_GLYPH_W      GV(s32, 0x80080EFC)
#define T_TEXT_FADE    GV(s32, 0x800823A0)   /* window 0's brightness */
#define T_LINE_H       GV(s32, 0x800823A4)
#define T_SE_SLOT      GV(s32, 0x8007F17C)   /* next sound voice, 10..16 */

#define g_gameType     GV(u8, 0x80062DF4)    /* 2 loaded game, 3 ... */
#define g_screenX      GV(s16, 0x80062E18)

#define SPRITES        0x800813E0u
#define SPRITE_SIZE    0x38u
#define SPRITE_COUNT   0x48
#define SPR(i)         (SPRITES + (u32)(i) * SPRITE_SIZE)
#define SP_U16(s, o)   GV(u16, (s) + (o))
#define SP_S16(s, o)   GV(s16, (s) + (o))
#define SF_FREE        0x1
#define SF_SHOWN       0x2

#define TEXT_BUF       0x80080F00u           /* two of 0x26C bytes */
#define TEXT_POS       0x800813D8u           /* (x, y) per window */

#define OT_TITLE       0x800805D8u           /* GsOT (20 bytes) per buffer */
#define OT_PRIMS       0x80080648u           /* ordering table, 0x400 bytes per buffer */
#define BUF            GV(u8, 0x80062D1C)    /* double buffer index */

/* ---- TITLE's own functions ---- */
#define F_T_FRAME_END   0x8006C6E4u
#define F_T_SOUND       0x8006CC78u   /* (bank, sound, volume l, volume r) */
#define F_T_LOAD_PICS   0x8006CE20u   /* (which) */
#define F_T_LOAD_IMAGES 0x8006F2CCu   /* (first, count): unpack images from the loaded file */
#define F_T_UPLOAD_TIM  0x8006F56Cu   /* (data, compressed) */
#define F_T_SHADOW      0x8006F6D0u   /* (sprite): a copy drawn behind it */
#define F_T_MENU_SOUNDS 0x8006FA64u
#define F_SPRITE_ALLOC  0x8006FB00u   /* () -> index or -1 */
#define F_T_DRAW_SPRITES 0x8006FB64u
#define F_T_DRAW_TEXT   0x80070148u
#define F_T_TEXT        0x80070A7Cu
#define F_T_GLYPH       0x80071078u
#define F_SPRITES_CLEAR 0x80071464u
#define F_SPRITE_SET_IMAGE 0x80071490u /* (sprite, image descriptor) */
#define F_TEXT_CLEAR    0x80071528u
#define F_TEXT_SET      0x8007154Cu   /* (text, window, x, y) */
#define F_TEXT_SHOWN    0x800715D0u   /* (window) -> has text */
#define F_TEXT_CURSOR   0x80071604u   /* (item, window, skip header) */
#define F_TEXT_DRAW_WINDOW 0x800717BCu

/* ---- kernel ---- */
#define F_LOAD_FILE     0x800115C0u   /* (name, file, table, index, [dest]) */
#define F_DECOMPRESS    0x80016F78u   /* (src, dst) */
#define F_SPU_PLAY_NOTE 0x8001F348u
#define F_STREAM_PLAY   0x800135A8u   /* (0, channel, a, b) */
#define F_STORE_IMAGE   0x80035680u
#define F_LOAD_IMAGE    0x80035620u
#define F_DRAW_SYNC     0x80035374u
#define F_CLEAR_IMAGE   0x800354F8u
#define F_GET_TIM_INFO  0x8002E048u
#define F_SET_SEMI      0x80038248u
#define F_SET_SHADE_TEX 0x80038278u
#define F_ADD_PRIM      0x800381E8u
#define F_SET_POLY_FT4  0x800382C8u
#define F_DR_MODE       0x800383E8u   /* (p, dfe, dtd, tpage, [tw]) */
#define F_CLEAR_OT      0x8002DED8u   /* GsClearOt (offset, point, ot) */
#define F_SET_MASTER_VOL 0x8001D358u

#endif
