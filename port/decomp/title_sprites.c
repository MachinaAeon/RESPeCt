/* The title's sprite updates (each sprite's +0x34, run by
 * title_RunSprites with T_CUR the sprite): the menu items, the cursor
 * and the mouse pointer, the scrolling menu wheel, shadows, and
 * the save screen's card icons. See title.h for the sprite record.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "title.h"

#define SHOW(s)  (SP_U16(s, 0x20) |= SF_SHOWN)
#define HIDE(s)  (SP_U16(s, 0x20) &= 0xFFFD)
#define PAD_MOUSE 0x200000u                  /* g_padTypeBit: a mouse */

static void set_rgb(u32 s, u16 c) {
    SP_U16(s, 0x1E) = c;
    SP_U16(s, 0x1C) = c;
    SP_U16(s, 0x1A) = c;
}

/* 8006D3DC: a title menu item: when the image set changes (T_PICS), take
 * that set's image and colour; hidden while no set is loaded. +0x26: the
 * second (shadow-like) copy, drawn dimmer and offset. */
void n_title_ItemUpdate(void) {
    FRAME(0x18);
    s32 pics = T_PICS;
    if (pics != -1) {
        u32 s = T_CUR;
        if ((u32)SP_U16(s, 0x24) != (u32)pics) {
            SP_U16(s, 0x24) = GV(u16, 0x800805C8);
            if (pics == 0x1E) {
                GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F234u);
                s = T_CUR;
                if (SP_U16(s, 0x26) == 0) {
                    SP_U16(s, 0x14) = 0xFF;
                    SP_U16(s, 8) = 0xFF;
                } else {
                    SP_U16(s, 0x14) = 0x3F;
                    SP_U16(s, 8) = 0x3F;
                    SP_U16(s, 4) = 0;
                    SP_U16(s, 0x10) = 0xFF0;
                    SP_U16(s, 2) += 4;
                }
                s = T_CUR;
                SP_U16(s, 0x12) = 0;
                set_rgb(s, 0x80);
            } else if (pics == 1) {
                GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F194u);
                s = T_CUR;
                if (SP_U16(s, 0x26) == 0) {
                    SP_U16(s, 0x14) = 0xBF;
                    SP_U16(s, 8) = 0xBF;
                } else {
                    SP_U16(s, 0x14) = 0x81;
                    SP_U16(s, 8) = 0x81;
                    SP_U16(s, 4) = 0;
                    SP_U16(s, 0x10) = 0xBF0;
                    SP_U16(s, 2) += 2;
                }
                s = T_CUR;
                SP_U16(s, 0x12) = 0x100;
                set_rgb(s, 0x40);
            } else {
                u16 c;
                if (pics == 6) {
                    GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F220u);
                    c = 0x20;
                } else {
                    GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F180u);
                    c = 0x80;
                }
                set_rgb(T_CUR, c);
                s = T_CUR;
                if (SP_U16(s, 0x26) == 0) {
                    SP_U16(s, 0x14) = 0xFF;
                    SP_U16(s, 8) = 0xFF;
                } else {
                    SP_U16(s, 0x14) = 0x40;
                    SP_U16(s, 8) = 0x40;
                    SP_U16(s, 0x10) = 0xFF0;
                    SP_U16(s, 2) += 2;
                }
                SP_U16(T_CUR, 0x12) = 0;
            }
        }
    }
    if (T_SCREEN == 8) set_rgb(T_CUR, 0x80);
    if (T_PICS == -1) HIDE(T_CUR);
    else SHOW(T_CUR);
}

/* 8006E120: a LOAD GAME / main menu entry on the wheel: hidden until the
 * menu is up (T_FLAGS 4); fades with the screen; placed on the wheel by
 * its angle (+0x24 plus the wheel's turn T_STATE2). */
void n_title_WheelItemUpdate(void) {
    u32 flags = T_FLAGS;
    s32 scr;
    if (!(flags & 4)) goto hide;
    scr = T_SCREEN;
    if (scr == 0xB || scr == 0xD) {
        u32 s = T_CUR;
        set_rgb(s, (u16)(0x80 - GV(u16, 0x80080ED8)));
        SP_U16(s, 0x20) |= 0xA;
        SP_U16(s, 2) |= 0x20;
        return;
    }
    if (scr == 0xC) { LOOP_POLL(0x8006E1B0); goto hide; }
    if (scr == 0xE) { LOOP_POLL(0x8006E1B8); goto hide; }
    if ((u32)(scr - 0xF) < 2) {
        if (flags & 1) { LOOP_POLL(0x8006E1CC); goto hide; }
        SHOW(T_CUR);
        return;
    }
    {
        u32 s = T_CUR;
        if (SP_U16(s, 0x1A) != 0x80) {
            SP_U16(s, 0x20) |= 0xA;
            SP_U16(s, 2) |= 0x20;
            u16 c = (u16)(SP_U16(s, 0x1A) + 0x10);
            SP_U16(s, 0x1A) = c;
            if (c >= 0x80) {
                SP_U16(s, 0x1A) = 0x80;
                T_FLAGS = flags | 8;
            }
            s = T_CUR;
            u16 r = SP_U16(s, 0x1A);
            SP_U16(s, 0x1E) = r;
            SP_U16(s, 0x1C) = r;
        } else {
            SP_U16(s, 0x20) = (u16)((SP_U16(s, 0x20) & 0xFFF7) | 2);
            SP_U16(s, 2) &= 0xFF9F;
        }
        s = T_CUR;
        s32 a = (s32)((u32)T_STATE2 + SP_U16(s, 0x24));
        if (a >= 0x201) a -= 0x300;
        SP_U16(s, 0x10) = (u16)(a << 4);
    }
    return;
hide:
    HIDE(T_CUR);
}

/* 8006E2B8: the pointer: the mouse arrow (a mouse) or the hand cursor,
 * at (+0x24, +0x26); fades out once the menu is chosen (T_FLAGS 4). */
void n_title_PointerUpdate(void) {
    u32 s = T_CUR;
    if (g_padTypeBit == PAD_MOUSE) {
        u16 x = SP_U16(s, 0x24), y = SP_U16(s, 0x26);
        SP_U16(s, 0x14) = 0xFF;
        SP_U16(s, 8) = 0xFF;
        SP_U16(s, 0x10) = 0x200;
        SP_U16(s, 4) = x;
        SP_U16(s, 6) = y;
    } else {
        SP_U16(s, 0x14) = 0xC0;
        SP_U16(s, 8) = 0xC0;
        SP_U16(s, 0x10) = 0x400;
        SP_U16(s, 4) = (u16)(SP_U16(s, 0x24) + 0x20);
        SP_U16(s, 6) = (u16)(SP_U16(s, 0x26) + 0xE8);
    }
    if (T_FLAGS & 4) {
        s = T_CUR;
        SP_U16(s, 0x20) |= 8;
        u16 c = (u16)(SP_U16(s, 0x1A) - 0x10);
        SP_U16(s, 0x1A) = c;
        SP_U16(s, 2) |= 0x20;
        if (c == 0) {
            SP_U16(s, 0x20) = SF_FREE;
            return;
        }
        SP_U16(s, 0x1E) = c;
        SP_U16(s, 0x1C) = c;
    }
}

/* 8006E394: a shadow (sprite_AddShadow): follows its sprite (+0x24) while
 * that is a wheel item or the pointer; freed with it. */
void n_title_ShadowUpdate(void) {
    u32 me = T_CUR;
    u32 s = SPR(SP_U16(me, 0x24));
    if (!(SP_U16(s, 0x20) & SF_FREE)) {
        u32 fn = GV(u32, s + 0x34);
        if (fn == 0x8006E120u || fn == 0x8006EB60u || fn == 0x8006E2B8u) {
            if ((SP_U16(s, 0x20) & SF_SHOWN) && SP_U16(s, 0x1A) != 0) {
                u32 d = T_CUR;
                SP_U16(d, 0x20) |= SF_SHOWN;
                SP_U16(d, 0x10) = SP_U16(s, 0x10);
                set_rgb(d, SP_U16(s, 0x1A));
                SP_U16(d, 4) = SP_U16(s, 4);
                SP_U16(d, 6) = SP_U16(s, 6);
                SP_U16(d, 8) = SP_U16(s, 8);
                SP_U16(d, 0x14) = SP_U16(s, 0x14);
            } else {
                HIDE(T_CUR);
            }
            return;
        }
    }
    SP_U16(me, 0x20) = SF_FREE;
}

/* The blinking arrow: shown once the wheel is up (T_FLAGS 8), blinking
 * between images +0x24+1 and +0x24+3 (CLUT rows), hidden on the
 * confirming screens or while the wheel turns. */
static void blink_arrow(void) {
    u32 s = T_CUR;
    u32 blink = T_FRAMES & 0x20;
    SP_U16(s, 0x20) |= SF_SHOWN;
    SP_U16(s, 0xE) = (u16)(SP_U16(s, 0x24) + (blink ? 1 : 3));
    if ((u32)(T_SCREEN - 0xE) < 3 || GV(u8, 0x80080EDC) != 0) HIDE(T_CUR);
}

/* 8006E4B0: the wheel's arrows with a mouse. */
void n_title_MouseArrowUpdate(void) {
    if (g_padTypeBit != PAD_MOUSE) { HIDE(T_CUR); return; }
    if (!(T_FLAGS & 8)) return;
    blink_arrow();
}

/* 8006EB60: a LOAD GAME list entry on the wheel (angle +0x24 plus
 * T_STATE3), fading with the screen; freed on leaving (screen 10). */
void n_title_LoadItemUpdate(void) {
    u32 s = T_CUR;
    s32 a = (s32)((u32)T_STATE3 + SP_U16(s, 0x24));
    if (a >= 0x201) a -= 0x300;
    SP_U16(s, 0x10) = (u16)(a << 4);
    s32 scr = T_SCREEN;
    if (scr == 0xB || scr == 0xD) {
        SP_U16(s, 0x20) |= 0xA;
        set_rgb(s, GV(u16, 0x80080ED8));
        return;
    }
    if (scr == 0xC) {
        set_rgb(s, 0x80);
        SP_U16(s, 0x20) &= 0xFFF7;
        SP_U16(s, 2) &= 0xFF9F;
        return;
    }
    if (scr == 0xA) SP_U16(s, 0x20) = SF_FREE;
}

/* 8006F7D0: the wheel's arrows. */
void n_title_ArrowUpdate(void) {
    if (!(T_FLAGS & 8)) return;
    blink_arrow();
}

/* 8006F874: shown with image set 1 while the menu isn't locked. */
void n_title_Set1Update(void) {
    if (T_PICS == 1 && !(T_FLAGS & 1)) SHOW(T_CUR);
    else HIDE(T_CUR);
}

/* 8006F8D8: hidden while the menu is locked or on screen 0x10. */
void n_title_UnlockedUpdate(void) {
    if ((T_FLAGS & 1) || T_SCREEN == 0x10) HIDE(T_CUR);
    else SHOW(T_CUR);
}

/* 8007EBBC: a save screen sprite shown with a mouse; freed when the save
 * screen's sprites go (0x80082620 = 0). */
void n_save_MouseSpriteUpdate(void) {
    if (GV(u32, 0x80082620) == 0) { SP_U16(T_CUR, 0x20) = SF_FREE; return; }
    if (g_padTypeBit == PAD_MOUSE) SHOW(T_CUR);
    else HIDE(T_CUR);
}
