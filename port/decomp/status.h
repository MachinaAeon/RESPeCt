/* The status screen (CONPAU_E overlay): its state, the game functions it
 * calls and the helpers its natives share (status_screen.c, file_viewer.c,
 * item_combine.c, item_check.c, map_screen.c). */
#ifndef STATUS_H
#define STATUS_H
#include "hud.h"
#include "libgs.h"
#include "poll.h"
#include "game_stack.h"

/* ---- the screen's state ---- */
#define ST_STATE        GV(u8, 0x800CF668)    /* state handler index */
#define ST_ITEMBOX      GV(u8, 0x800CF669)    /* the item box is open (not the status screen) */
#define ST_SCROLLED     GV(u8, 0x800CF66A)
#define ST_SORT_OLD     GV(u8, 0x800CF66B)    /* also sort the other buffer's tables */
#define ST_OLD_BUF      GV(s16, 0x800CF66C)
#define ST_SOUND        GV(u32, 0x800CF68C)   /* music to play / playing */
#define ST_SOUND_ON     GV(u32, 0x800CF690)
#define ST_CANCEL       GV(s16, 0x800CF5CC)   /* cancel requested (mouse) */
#define ST_OBJ_ON       GV(u32, 0x800CF8BC)   /* sort the 3D objects */
#define ST_OBJ_MODE     GV(s32, 0x800CF8C0)
#define ST_PADTYPE      GV(u32, 0x800CF79C)   /* pad type the prompts were made for */
#define COUNT_SLOTS     0x800CF698u           /* [3][3] digit panels */
#define OBJECTS         0x800CF7C8u           /* u8 count, u8 on[] (+2), objects at +0xC (8 bytes) */
#define RVIEW           0x800CF7A8u           /* GsRVIEW2 */
#define HANDLERS        0x800CF594u
#define ACTIONS         0x800CF5A8u           /* menu actions (panel callbacks) */
/* the command menu */
#define MENU            0x800CF678u
#define MENU_ID         GV(s16, MENU)
#define MENU_COUNT      GV(s16, MENU + 2)
#define MENU_CURSOR     GV(s16, MENU + 6)
#define MENU_CURPANEL   GV(s16, MENU + 8)
#define MENU_PANEL(i)   GV(s16, MENU + 0xA + (u32)(s32)(i) * 2)
#define MENU_REPEAT     GV(u16, 0x800CF68A)   /* auto-repeat delay */
#define MENU_HINT       GV(u8, 0x800CF6B1)    /* hint panel, 0xFF none */
#define MENU_DEFS       0x800CF064u           /* 12 bytes per menu */
#define MENU_LOOKS      0x800CF088u           /* 8 bytes: u, v, w, h */
#define ITEM_LOOKS      0x800CF0F8u
#define DIGITS          0x800CF098u           /* u, v per digit; +0x2C/+0x2E the infinity mark */
/* the item list */
#define LIST            0x800CF6B8u
#define LIST_COUNT      GV(s16, LIST + 0xE0)
#define LIST_CURSOR     GV(s16, LIST + 0xE2)
#define INVP            0x800CF6A8u           /* the list's panels, 0xFF none */
#define INV_PANEL(i)    GV(u8, INVP + (i))
#define NAMES           0x800A8678u           /* item names box: +0x1A4.., lines at +0x1AC */
#define ITEM_LOOK_TAB   0x800A6D5Cu           /* name text per item id */

/* ---- game functions ---- */
#define F_SET_PROJECTION 0x8002D818u
#define F_SET_REF_VIEW2  0x80033FD8u
#define F_SET_AMBIENT    0x8002DE58u
#define F_HUD_UPDATE     0x8009358Cu
#define F_HUD_SORT       0x80092E94u
#define F_ALLOC_PANEL    0x80092E3Cu
#define F_SET_SPRITE     0x800945B4u
#define F_HUD_LOAD       0x80092878u
#define F_HUD_BUILD      0x80093444u
#define F_SOUND_STOP     0x8007D7D0u
#define F_SOUND_PLAY     0x8007D6E0u
#define F_HUD_FREE       0x800944F0u
#define F_EQUIP_ICON     0x80094AE4u
#define F_LOAD_SOUNDS    0x8008E324u
#define SPR_COUNT        0x800A608Cu   /* sprite: count digit */
#define SPR_MENU         0x800A6064u
#define SPR_ITEMS        0x800A60B4u

#define ITEMS_B          0x800AF580u   /* item entries (8 bytes) of the lists here */

/* jal: the call with $ra as the original's jal sets it. */
#define JAL(ra, f, a0, a1, a2, a3) gcall_ra((ra), (f), (u32)(a0), (u32)(a1), (u32)(a2), (u32)(a3))
/* jalr with $ra as the original sets it, argument registers as they are. */
static inline u32 jalr_ra(u32 ra, u32 fn) {
    u32 old = R[31];
    R[31] = ra;
    g_native_depth++;
    rt_call(fn);
    g_native_depth--;
    R[31] = old;
    return R[2];
}

static inline u32 button(u32 ra, u32 a0, u32 a1) { return JAL(ra, 0x800CBF10u, a0, a1, A2, A3); }
#define BTN(ra, mask)      ((button((ra), (u32)(s32)(s16)g_buttons, (mask)) & 0xFFFF) != 0)
#define BTN_PREV(ra, mask) ((button((ra), (u32)(s32)(s16)g_buttonsPrev, (mask)) & 0xFFFF) != 0)
static inline void sound(u32 ra, u32 id) { JAL(ra, F_SOUND_CUE, 0, id, 0x7F, 0x7F); }

static inline u32 pnl(u32 i) { return PANELS + (i & 0xFFFF) * PANEL_SIZE; }    /* by a u8/u16 number */
static inline u32 pnl_s(s32 i) { return PANELS + (u32)i * PANEL_SIZE; }         /* by an s16 number */

/* Store the caller's $ra / s registers in the frame where the original's
 * prologue does (sp + off). */
#define SAVE_RA(off)      (GS(u32, sp + (off)) = R[31])
#define SAVE_SREG(n, off) (GS(u32, sp + (off)) = R[16 + (n)])

#endif
