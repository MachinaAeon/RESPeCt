/* The HUD panel system: shared by hud_panels.c and weapon_view.c. */
#ifndef HUD_H
#define HUD_H
#include "game_vars.h"

/* A panel. Only the fields this file touches are named. */
#define PANELS          0x800C09D8u
#define PANEL_COUNT     0xB4
#define PANEL_SIZE      0x44
#define P16(p, off)     GV(u16, (p) + (off))
#define P32(p, off)     GV(u32, (p) + (off))
#define PANEL_FLAGS     0x1C      /* 1 free, 2 live, 4 busy, 0x80/0x100 hidden, 0x400 paused */
#define PANEL_DRAWPTR   0x38
#define PANEL_UPDATE    0x3C      /* callback, run every frame */

#define g_curPanel      GV(u32, 0x800C39A8)   /* panel being built / updated */
#define g_panelSave     GV(u32, 0x800C09D0)
#define g_panelMode     GV(u16, 0x800C09D4)
#define g_panelList     GA(s16, 0x800C0868)   /* live panels this frame, -1 ends */
#define g_inputLocked   GV(s16, 0x800B3D2C)
#define D_800B3D38      GV(s32, 0x800B3D38)   /* paused: skip panels flagged 0x400 */
#define D_800BBB58      GV(s32, 0x800BBB58)
#define SPRITES         0x800A5BF0u           /* sprite definitions, 0x14 bytes each */

/* The weapon HUD state at 0x800C39C0 */
#define g_hudRounds     GV(s16, 0x800C39C0)   /* rounds shown in the bullet row */
#define g_hudWeaponCtl  GV(s16, 0x800C39EC)   /* panel index of the weapon control */
#define g_hudBullets    GV(s16, 0x800C39EE)   /* panel index of the bullet row */
#define g_weaponViewOn  GV(u32, 0x800C39F4)   /* weapon view handlers for the held gun */
#define g_weaponAction  GV(u32, 0x800C39F8)

#define F_MEMSET        0x80039898u           /* BIOS memset (dst, byte, n) */
#define F_MOVE_IMAGE    0x800356E0u           /* (rect, x, y) */
#define F_DRAW_SYNC     0x80035374u
#define F_WEAPON_FILES  0x80094A30u           /* (buffer): HUD images from CONPAU */
#define F_AMMO_IMAGE    0x80094C60u           /* (ammo item): bullet image */
#define F_ICON_PARTS    0x80094E44u           /* (x, y): the weapon icon's child panels */
#define F_BULLET_ROW    0x80095B90u           /* (x, y, rounds) */
#define F_ACTION_PRE    0x80094454u
#define CB_WEAPON_ICON  0x80094DB4u
#define CB_BULLETS      0x80095480u
#define CB_WEAPON_CTL   0x80095948u
#define F_REMOVE_ITEM   0x8008D678u           /* (inventory slot): an empty ammo box goes */
#define WEAPON_TABLE    0x800A09C8u           /* 8 bytes per weapon id; +4 magazine size */
#define BULLET_LOOKS    0x800A6258u           /* per weapon: bullet sprite u/v offsets, size */
#define g_hudFrame      GV(s16, 0x800C3A00)   /* animation frame for panels */
#define g_iconChanged   GV(s16, 0x800C39B0)
#define g_hudPanelsBullet GA(s16, 0x800C39C4) /* the bullet row's panels, -1 = none */
#define g_reloadBlock   GV(s16, 0x800C39F2)
#define g_inventory     0x800A8880u           /* item slot bytes; count (s16) at +0x40 */
#define ITEMS           0x800AF800u           /* item entries: id, count, ammo id, spare */


void n_hud_AllocPanel(void);
void n_hud_SetSprite(void);
void n_hud_Animate(void);
void n_hud_FreeBullets(void);
void n_hud_BulletRow(void);
void n_hud_NewBullet(void);

static inline u32 panel_addr(s32 i) { return PANELS + (u32)i * PANEL_SIZE; }
static inline s16 panel_index(u32 p) { return (s16)((p - PANELS) / PANEL_SIZE); }

/* Call a panel callback (jalr in the original; a0/a1 as it left them). */
static inline void call_panel(u32 fn, u32 a0, u32 a1) {
    u32 ra = R[31];
    R[4] = a0;
    R[5] = a1;
    g_native_depth++;
    rt_call(fn);
    g_native_depth--;
    R[31] = ra;
}

/* Call game code through a pointer, leaving the argument registers as
 * they are (a jalr). */
static inline u32 call_ptr(u32 fn) {
    u32 ra = R[31];
    g_native_depth++;
    rt_call(fn);
    g_native_depth--;
    R[31] = ra;
    return V0;
}

#endif
