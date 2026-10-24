/* The title screen's steps (title_Frame, 8006CF24, calls one per frame
 * by T_SCREEN): the logo fading in, the "press start" wait, building
 * the main menu, the menu wheel (NEW GAME / LOAD GAME / OPTION: turned
 * with left/right, chosen with fire), the LOAD GAME wheel, fading out to
 * start the game; and the menu's button tests.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "title.h"

#define PAD_MOUSE      0x200000u
#define PAD_DUALSHOCK  0x400000u
#define PAD_DIGITAL    0x80000u
#define T_MVOL         GV(s32, 0x80080E84)   /* music volume */
#define T_LOAD_FOUND   GV(u32, 0x80080E8C)   /* a save was found on a card */
#define T_SND_STEP     GV(s32, 0x80082474)
#define MSG_PRESS      0x8007F2F4u           /* "press the button" message */
#define MSG_LOAD       0x8007F334u

/* the press-start message: shown with a mouse, else taken down */
static void show_message(void) {
    if (T_MSG == 0) return;
    if (g_padTypeBit == PAD_MOUSE) {
        if (!GCALL1(F_TEXT_SHOWN, 0)) GCALL4(F_TEXT_SET, T_MSG, 0, 0x10, 0x94);
    } else {
        if (GCALL1(F_TEXT_SHOWN, 0)) GCALL0(F_TEXT_CLEAR);
    }
}

static void set_message(u32 msg) {
    T_MSG = msg;
    if (g_padTypeBit == PAD_MOUSE) GCALL4(F_TEXT_SET, msg, 0, 0x10, 0x94);
}

/* button `mask` newly pressed (title_Pressed on this frame's pads and not
 * on last frame's) */
static int pressed(u32 mask) {
    if (!((u16)GCALL3(0x8006EC1Cu, (u32)(s32)GV(s16, 0x800805CC), T_PAD2, mask))) return 0;
    return !((u16)GCALL3(0x8006EC1Cu, (u32)(s32)GV(s16, 0x80080E48), T_PAD2_PREV, mask));
}

/* The wheel's turn (*turn, 0..0x2FF): T_STATE steps it a quarter
 * (0x40) at a time until on a 0x100 boundary. */
static void wheel_turn(u32 turn_addr) {
    s32 step = T_STATE;
    if (step == 0) return;
    s32 v = (s32)((u32)GV(s32, turn_addr) + (u32)step);
    GV(s32, turn_addr) = v;
    if ((v & 0xFF) == 0) T_STATE = 0;
    if (v < 0) GV(s32, turn_addr) = v + 0x300;
    v = GV(s32, turn_addr);
    if (v >= 0x300) GV(s32, turn_addr) = v - 0x300;
}

/* Left/right on a wheel: start a quarter turn. */
static void wheel_right(u32 turn_addr) {
    T_STATE = 0x40;
    GV(s32, turn_addr) += 0x40;
    GCALL4(F_T_SOUND, 0, 1, 0x7F, 0x7F);
}

static void wheel_left(u32 turn_addr) {
    s32 old = GV(s32, turn_addr);
    s32 v = old - 0x40;
    GV(s32, turn_addr) = v;
    if (v < 0) GV(s32, turn_addr) = old + 0x2C0;
    T_STATE = -0x40;
    GCALL4(F_T_SOUND, 0, 1, 0x7F, 0x7F);
}

static u32 take_sprite(u32 image, u16 flags) {
    s32 k = (s32)GCALL0(F_SPRITE_ALLOC);
    if (k == -1) return 0;
    u32 s = SPR(k);
    SP_U16(s, 0x20) |= flags;
    GCALL2(F_SPRITE_SET_IMAGE, s, image);
    return s;
}

/* 8006DA00: screen 9, building the main menu: fade out, then load the
 * menu images and set up the logo, the wheel (three items with shadows),
 * the pointer, the arrows; then fade in with the press-start message. */
void n_title_MenuBuild(void) {
    FRAME(0x40);
    s32 st = T_STATE;
    if (st == 0) {
        s32 v = T_FADE - 4;
        T_FADE = v;
        if (v < 0) {
            T_STATE = 1;
            T_FADE = 0;
            T_STATE2 = 0;
            T_FLAGS |= 0x10;
        }
        T_MSG = 0;
        return;
    }
    if (st == 1) {
        T_STATE = 2;
        T_STATE2 = 0;
        return;
    }
    if (st == 2) {
        s32 n = T_STATE2 + 1;
        T_STATE2 = n;
        if (n != 3) return;
        T_PICS = 1;
        for (u32 i = 1;; i++) {
            GCALL2(F_T_UPLOAD_TIM, 0x80138000u + GV(u32, 0x80138000u + i * 4 + 4), 1);
            if (i >= 3) break;
            LOOP_POLL(0x8006DAF8);
        }
        u32 s;
        s = take_sprite(0x8007F1E4u, 4);           /* the logo */
        if (s) {
            SP_U16(s, 0x10) = 0x180;
            SP_U16(s, 0x14) = 0xFF;
            SP_U16(s, 8) = 0xFF;
            SP_U16(s, 0x18) = 4;
            SP_U16(s, 0x12) = 0;
            GV(u32, s + 0x34) = 0x8006F874u;
        }
        s = take_sprite(0x8007F1E4u, 4);
        if (s) {
            SP_U16(s, 0x10) = 0x1170;
            SP_U16(s, 0x18) = 4;
            GV(u32, s + 0x34) = 0x8006F874u;
            u16 t = SP_U16(s, 2), w = SP_U16(s, 8);
            SP_U16(s, 4) = 0;
            SP_U16(s, 0x12) = 0;
            SP_U16(s, 2) = (u16)(t + 2);
            SP_U16(s, 8) = (u16)(w - 0x100);
            SP_U16(s, 0x14) = (u16)(w - 0x100);
        }
        s32 k = (s32)GCALL0(F_SPRITE_ALLOC);           /* the pointer */
        if (k != -1) {
            s = SPR(k);
            SP_U16(s, 0x20) |= 6;
            GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F1D0u);
            u16 u = SP_U16(s, 4), v = SP_U16(s, 6);
            SP_U16(s, 0x16) = 0x10;
            SP_U16(s, 0xA) = 0x10;
            SP_U16(s, 0x24) = u;
            SP_U16(s, 0x26) = v;
            if (g_padTypeBit == PAD_MOUSE) {
                SP_U16(s, 0x10) = 0x200;
            } else {
                SP_U16(s, 0x14) = 0xC0;
                SP_U16(s, 8) = 0xC0;
                SP_U16(s, 0x10) = 0x400;
                SP_U16(s, 4) = (u16)(SP_U16(s, 4) + 0x20);
                SP_U16(s, 6) = (u16)(SP_U16(s, 6) + 0xE8);
            }
            SP_U16(s, 0x12) = 0x780;
            SP_U16(s, 0x18) = 1;
            GV(u32, s + 0x34) = 0x8006E2B8u;
            GCALL1(F_T_SHADOW, (u32)k);
        }
        u16 ang = 0x24, v = 0x10;
        for (s32 i = 0; i < 3; i++) {                 /* the wheel */
            k = (s32)GCALL0(F_SPRITE_ALLOC);
            if (k != -1) {
                s = SPR(k);
                SP_U16(s, 0x20) |= 4;
                GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F1D0u);
                SP_U16(s, 0x12) = 0x780;
                SP_U16(s, 6) = v;
                SP_U16(s, 0x16) = 0x10;
                SP_U16(s, 0xA) = 0x10;
                SP_U16(s, 0x10) = 0;
                SP_U16(s, 0x18) = 3;
                SP_U16(s, 0x24) = ang;
                SP_U16(s, 0x1E) = 0;
                SP_U16(s, 0x1C) = 0;
                SP_U16(s, 0x1A) = 0;
                GV(u32, s + 0x34) = 0x8006E120u;
                GCALL1(F_T_SHADOW, (u32)k);
            }
            ang += 0x100;
            v += 0x10;
            if (i + 1 < 3) LOOP_POLL(0x8006DD68);
        }
        s = take_sprite(0x8007F1D0u, 6);
        if (s) {
            SP_U16(s, 0x16) = 0x18;
            SP_U16(s, 0xA) = 0x18;
            SP_U16(s, 0x12) = 0xCC0;
            SP_U16(s, 0x14) = 0xFF;
            SP_U16(s, 8) = 0xFF;
            SP_U16(s, 0x10) = 0x200;
            SP_U16(s, 0x18) = 1;
            u16 vv = SP_U16(s, 6);
            GV(u32, s + 0x34) = 0x8006F8D8u;
            SP_U16(s, 6) = (u16)(vv + 0x58);
        }
        u32 found = GCALL0(0x8007B340u);              /* a save on a card? */
        T_LOAD_FOUND = found;
        T_STATE2 = found ? 0x200 : 0;
        T_FLAGS &= ~5u;
        GCALL0(F_TEXT_CLEAR);
        GS(u16, sp + 0x10) = 0x17E;
        GS(u16, sp + 0x12) = 0x1E3;
        GS(u16, sp + 0x14) = 2;
        GS(u16, sp + 0x16) = 4;
        GCALL2(F_LOAD_IMAGE, sp + 0x10, 0x8007F3C0u);
        for (s32 i = 0; i < 2; i++) {                 /* the arrows */
            k = (s32)GCALL0(F_SPRITE_ALLOC);
            if (k != -1) {
                s = SPR(k);
                u16 f = SP_U16(s, 0x20);
                SP_U16(s, 0x20) = f | 4;
                if (i == 0) SP_U16(s, 0x20) = f | 0x14;
                GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F1BCu);
                u16 u = SP_U16(s, 4), cy = SP_U16(s, 0xE);
                SP_U16(s, 0x14) = 0xE;
                SP_U16(s, 0xA) = 0xE;
                SP_U16(s, 0x16) = 0xE;
                SP_U16(s, 8) = 0xE;
                SP_U16(s, 4) = (u16)(u + 0x1C);
                SP_U16(s, 0x24) = cy;
                SP_U16(s, 0x10) = i ? 0xD80 : 0x600;
                SP_U16(s, 0x12) = 0x790;
                SP_U16(s, 0x18) = 0;
                GV(u32, s + 0x34) = 0x8006F7D0u;
            }
            if (i + 1 < 2) LOOP_POLL(0x8006DF28);
        }
        for (s32 i = 0; i < 2; i++) {                 /* the mouse's arrows */
            k = (s32)GCALL0(F_SPRITE_ALLOC);
            if (k != -1) {
                s = SPR(k);
                SP_U16(s, 0x20) |= 4;
                GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F1F8u);
                SP_U16(s, 4) = (u16)(SP_U16(s, 4) + (i ? 0x10 : 8));
                u16 vv = SP_U16(s, 6), cy = SP_U16(s, 0xE);
                SP_U16(s, 0x14) = 8;
                SP_U16(s, 0xA) = 8;
                SP_U16(s, 0x16) = 8;
                SP_U16(s, 8) = 8;
                SP_U16(s, 6) = (u16)(vv + 0xD0);
                SP_U16(s, 0x24) = cy;
                SP_U16(s, 0x10) = i ? 0xDB0 : 0x630;
                SP_U16(s, 0x12) = 0x710;
                SP_U16(s, 0x18) = 0;
                GV(u32, s + 0x34) = 0x8006E4B0u;
            }
            if (i + 1 < 2) LOOP_POLL(0x8006DFF8);
        }
        T_STATE++;
        return;
    }
    GCALL0(0x8006F93Cu);
    if ((T_FLAGS & 4) && !GCALL1(F_TEXT_SHOWN, 0)) set_message(MSG_PRESS);
    u32 flags = T_FLAGS;
    s32 v = T_FADE + ((flags & 0x10) ? 2 : 8);
    T_FADE = v;
    if (v >= 0x80) {
        T_FADE = 0x80;
        T_SCREEN = 0xA;
        GV(u32, 0x80080EE4) = 0;
        T_STATE = 0;
        GV(u32, 0x80080EE8) = 8;
        T_FLAGS = flags & ~0x10u;
        T_IDLE = 0;
    }
}

/* 8006E568: screen 0xA, the main menu. Idle too long: the demo (screen
 * 0x12). Before start is pressed: wait for it. Then the wheel: left and
 * right turn it, fire chooses: NEW GAME, OPTION (screen 0x10) or LOAD
 * GAME (its wheel of cards: screen 0xB). */
void n_title_MainMenu(void) {
    FRAME(0x30);
    s32 idle = T_IDLE + 1;
    T_IDLE = idle;
    if (idle >= 0x709) {
        T_SCREEN = 0x12;
        T_STATE = 0;
        return;
    }
    show_message();
    u32 flags = T_FLAGS;
    if (!(flags & 4)) {
        u32 t = g_padTypeBit, start;
        if (t == PAD_MOUSE) start = T_PAD & 0x860;
        else if ((s32)t > (s32)PAD_MOUSE) start = t == PAD_DUALSHOCK ? T_PAD & 0x800 : 0;
        else start = t == PAD_DIGITAL ? T_PAD & 0x800 : 0;
        if (!start) return;
        T_FLAGS |= 4;
        GCALL4(F_T_SOUND, 0, 2, 0x7F, 0x7F);
        set_message(MSG_PRESS);
        T_IDLE = 0;
        return;
    }
    if (!(flags & 8)) return;
    if (T_STATE2 & 0xFF) { wheel_turn(0x80080EDCu); return; }
    if (pressed(0x800)) { wheel_right(0x80080EDCu); T_IDLE = 0; return; }
    if (pressed(0x40)) { wheel_left(0x80080EDCu); T_IDLE = 0; return; }
    if (!pressed(0x20)) return;
    s32 item = T_STATE2;
    if (item == 0) {                                  /* LOAD GAME */
        T_SCREEN = 0xB;
        T_STATE = 0;
        T_STATE3 = 0x200;
        set_message(MSG_LOAD);
        u32 tab = 0x8007F3D0u;
        for (s32 i = 0; i < 3; i++) {
            s32 k = (s32)GCALL0(F_SPRITE_ALLOC);
            if (k != -1) {
                u32 s = SPR(k);
                SP_U16(s, 0x20) |= 0xC;
                GCALL2(F_SPRITE_SET_IMAGE, s, 0x8007F1D0u);
                SP_U16(s, 4) = (u16)(SP_U16(s, 4) + GV(u16, tab));
                SP_U16(s, 6) = (u16)(SP_U16(s, 6) + GV(u16, tab + 2));
                u16 w = GV(u16, tab + 4);
                SP_U16(s, 0x16) = 0x10;
                SP_U16(s, 0xA) = 0x10;
                SP_U16(s, 0x12) = 0x780;
                SP_U16(s, 0x18) = 1;
                SP_U16(s, 0x14) = w;
                SP_U16(s, 8) = w;
                s16 ws = (s16)GV(u16, tab + 4);
                GV(u32, s + 0x34) = 0x8006EB60u;
                SP_U16(s, 0x24) = (u16)((i << 8) - ws / 2 + 0xA4);
                s32 k2 = (s32)GCALL0(F_SPRITE_ALLOC);
                if (k2 != -1) {                       /* its shadow, as sprite_AddShadow */
                    u32 d = SPR(k2);
                    for (u32 o = 0;; o += 0x10) {
                        for (u32 j = 0; j < 0x10; j += 4) GV(u32, d + o + j) = GV(u32, s + o + j);
                        if (o + 0x10 == 0x30) break;
                        LOOP_POLL(0x8006E9E8);
                    }
                    GV(u32, d + 0x30) = GV(u32, s + 0x30);
                    GV(u32, d + 0x34) = GV(u32, s + 0x34);
                    SP_U16(d, 0xC) = 0x140;
                    SP_U16(d, 0xE) = 0xFF;
                    u16 f = SP_U16(d, 0x20), t = SP_U16(d, 2);
                    SP_U16(d, 0x24) = (u16)k;
                    SP_U16(d, 0x20) = f | 0xC;
                    SP_U16(d, 2) = t | 0x40;
                    SP_U16(d, 0x12) = SP_U16(s, 0x12);
                    GV(u32, d + 0x34) = 0x8006E394u;
                    SP_U16(d, 0x18) = (u16)(SP_U16(s, 0x18) + 1);
                }
                SP_U16(s, 2) |= 0x20;
            }
            tab += 6;
            if (i + 1 < 3) LOOP_POLL(0x8006EA68);
        }
    } else if (item == 0x200) {                       /* NEW GAME */
        T_SCREEN = 0xF;
        T_SND_STEP = 0;
        GCALL4(F_STREAM_PLAY, 0, 0, 8, 0xFF);
        GCALL4(F_STREAM_PLAY, 0, 1, 8, 0xFF);
        GCALL0(F_T_MENU_SOUNDS);
        GV(u32, 0x80080ED0) = 0;
    } else {                                          /* OPTION */
        T_SCREEN = 0x10;
        GCALL0(0x80073FD8u);
    }
    GCALL4(F_T_SOUND, 0, 2, 0x7F, 0x7F);
}

/* 8006EC1C: title_Pressed(pad, pad 2, button): is `button` (0x800 next,
 * 0x40 previous, 0x20 choose, 0x7FF back) down in `pad`, by the pad type
 * (the mouse: its two buttons, swapped by the option 0x80062DD6). The
 * second pad isn't looked at. */
void n_title_Pressed(void) {
    u32 pad = A0;
    s32 m = (s16)A2;
    u32 t = g_padTypeBit;
    if (t == PAD_MOUSE) {
        if (m == 0x40) {
            if (GV(s16, 0x80062DD6)) { LOOP_POLL(0x8006EDD4); V0 = (u16)(pad & 0x800); }
            else V0 = pad & 0x40;
        } else if (m < 0x41) {
            V0 = m == 0x20 ? pad & 0x20 : 0;
        } else if (m == 0x7FF) {
            V0 = (pad & 0x840) == 0x840;
        } else if (m == 0x800) {
            if (GV(s16, 0x80062DD6)) V0 = pad & 0x40;
            else { LOOP_POLL(0x8006EDC0); V0 = (u16)(pad & 0x800); }
        } else V0 = 0;
        return;
    }
    if ((s32)t > (s32)PAD_MOUSE ? t != PAD_DUALSHOCK : t != PAD_DIGITAL) { V0 = 0; return; }
    if (m == 0x40) {
        if (T_SCREEN != 0xF) { LOOP_POLL(0x8006ED00); V0 = (u16)(pad & 0x2000); return; }
        s32 st = T_SND_STEP;
        if (st == 5) { LOOP_POLL(0x8006ED14); V0 = (u16)(pad & 0x4000); }
        else if (st != 7) { LOOP_POLL(0x8006ED30); V0 = (u16)(pad & 0x2000); }
        else { LOOP_POLL(0x8006ED28); V0 = (u16)(pad & 0x4000); }
        return;
    }
    if (m < 0x41) {
        if (m != 0x20) { V0 = 0; return; }
        s32 scr = T_SCREEN;
        V0 = (scr == 0xA || scr == 0xC) ? pad & 0x820 : pad & 0x20;
        return;
    }
    if (m == 0x7FF) { V0 = pad & 0x40; return; }
    if (m != 0x800) { V0 = 0; return; }
    if (T_SCREEN == 0xF) {
        s32 st = T_SND_STEP;
        if (st == 5 || st == 7) { V0 = (u16)(pad & 0x1000); return; }
    }
    V0 = (u32)(s32)(s16)(pad & 0xFFFF8000u);
}

/* 8006EE08: screen 0xC, the LOAD GAME wheel: left/right turn it, back
 * (or choosing "back") returns to the main menu (screen 0xD), choosing a
 * card starts loading (screen 0xE). */
void n_title_LoadMenu(void) {
    FRAME(0x18);
    show_message();
    if (T_STATE3 & 0xFF) { wheel_turn(0x80080EE0u); return; }
    if (pressed(0x7FF)) {
        T_STATE3 = 0x100;
        goto back;
    }
    if (pressed(0x800)) { wheel_right(0x80080EE0u); return; }
    if (pressed(0x40)) { wheel_left(0x80080EE0u); return; }
    if (!pressed(0x20)) return;
    {
        s32 item = T_STATE3;
        if (item == 0x100) goto back;
        GV(u32, 0x80062DF8) = item == 0x200 ? 0 : 1;   /* which card */
        GCALL4(F_STREAM_PLAY, 0, 0, 8, 0xFF);
        GCALL4(F_STREAM_PLAY, 0, 1, 8, 0xFF);
        T_SCREEN = 0xE;
        g_gameType = 0;
        GCALL0(F_T_MENU_SOUNDS);
        return;
    }
back:
    T_SCREEN = 0xD;
    T_STATE = 0x80;
    T_STATE2 = 0;
    T_STATE3 = 0x100;
    GCALL4(F_T_SOUND, 0, 3, 0x7F, 0x7F);
    set_message(MSG_PRESS);
    GV(u32, 0x80080EE8) = 8;
    GV(u32, 0x80080EE4) = 8;
}

/* 8006F180: screen 0xE, leaving the title: after the flash, fade out
 * (every other frame), then end the title loop; for a new game, restore
 * the display settings saved on entry. The music fades with it. */
void n_title_FadeOut(void) {
    FRAME(0x18);
    if (T_FLASH != 0) return;
    if (!(T_FRAMES & 1)) {
        s32 v = T_FADE - 1;
        T_FADE = v;
        if (v <= 0) {
            T_FADE = 0;
            u8 type = g_gameType;
            T_START = 1;
            if (type == 0 || type == 3) {
                GV(u16, 0x80062E18) = GV(u16, 0x80080610);
                GV(u16, 0x80062E1A) = GV(u16, 0x80080612);
                GV(u16, 0x80062DD4) = GV(u16, 0x80080624);
                GV(u16, 0x80062DD6) = GV(u16, 0x80080628);
                GV(u16, 0x80062DAC) = GV(u16, 0x80080618);
                GV(u16, 0x80062DAE) = (u16)(GV(u16, 0x8008061A) - 4);
                GV(u32, 0x80062E20) = GV(u32, 0x80080614);
                GV(u16, 0x80062DB4) = GV(u16, 0x8008062C);
                GV(u16, 0x80062DB6) = GV(u16, 0x8008061C);
            }
        }
    }
    if (T_FADE < 0x40) {
        s32 v = T_MVOL - 1;
        T_MVOL = v;
        if (v < 0) T_MVOL = 0;
        s16 m = GV(s16, 0x80080E84);
        GCALL2(F_SET_MASTER_VOL, (u32)(s32)m, (u32)(s32)m);
    }
}

/* 8006F35C: screen 3: fade in. */
void n_title_FadeIn(void) {
    s32 v = T_FADE + 8;
    T_FADE = v;
    if (v >= 0x81) {
        T_FADE = 0x80;
        T_TIMER = 0;
        T_SCREEN = 4;
        T_STATE = 0;
    }
}

/* 8006F3A4: screen 4: load the next pictures (third frame), wait a
 * second. */
void n_title_Wait1(void) {
    FRAME(0x18);
    s32 n = T_STATE + 1;
    T_STATE = n;
    if (n == 3) GCALL1(F_T_LOAD_PICS, 5);
    n = T_TIMER + 1;
    T_TIMER = n;
    if (n >= 0x3D) T_SCREEN = 5;
}

/* 8006F408: screen 5: fade the message out. */
void n_title_TextFadeOut(void) {
    s32 v = T_TEXT_FADE - 4;
    T_TEXT_FADE = v;
    if (v <= 0) {
        T_TEXT_FADE = 0;
        T_TIMER = 0;
        T_SCREEN = 6;
    }
}

/* 8006F440: screen 6: wait 5 seconds, then the menu (with a mouse via
 * screen 7). */
void n_title_Wait2(void) {
    s32 n = T_TIMER + 1;
    T_TIMER = n;
    if (n >= 0x12D) {
        T_SCREEN = g_padTypeBit == PAD_MOUSE ? 7 : 9;
        T_STATE = 0;
        T_STATE2 = 0;
    }
}

/* 8006F4F0: screen 0xB: the LOAD GAME wheel fading in. */
void n_title_LoadFadeIn(void) {
    s32 v = T_STATE + 8;
    T_STATE = v;
    if (v >= 0x80) {
        T_STATE = 0x80;
        T_SCREEN = 0xC;
    }
}

/* 8006F93C: keep the press-start message up with a mouse, down without. */
void n_title_KeepMessage(void) {
    FRAME(0x18);
    show_message();
}

/* 8006F9BC: read the pad; start newly pressed (with a mouse: the left
 * button; never with no pad). */
void n_title_StartPressed(void) {
    FRAME(0x20);
    T_PAD_PREV = T_PAD;
    u32 pad = GCALL1(0x80013A5Cu, 0);
    u32 t = g_padTypeBit;
    T_PAD = pad;
    if (t == 0x8000) V0 = 0;
    else if (t == PAD_MOUSE) V0 = (pad & 0x20) && !(T_PAD_PREV & 0x20);
    else V0 = (pad & 0x800) && !(T_PAD_PREV & 0x800);
}

/* 8007ECF4: fade out, then back to the start (screen 0). */
void n_title_FadeToStart(void) {
    FRAME(0x18);
    s32 v = T_FADE;
    if (v <= 0) {
        T_SCREEN = 0;
        GCALL0(F_SPRITES_CLEAR);
        return;
    }
    v -= 8;
    T_FADE = v;
    if (v < 0) T_FADE = 0;
}
