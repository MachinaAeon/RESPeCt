/* Stage program 17 (STAGPROG overlay 17: stage 2): the per-frame room
 * hook.
 *
 * Most rooms animate their lights by palette: a row of a CLUT in VRAM is
 * replaced every frame or few by one of the rows below it (F_CLUT_ANIM),
 * flickering or cycling, faster or brighter once an event flag says the
 * power is on. Some rooms also set event flags (and show their message)
 * when the player crosses a line or an event is done, and the last room
 * plays a timed sequence of sounds and flashes after its script starts.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"

void n_stage17_RoomFrame(void);

#define g_lightIdx      GV(u8, 0x800F632C)   /* s8: animation frame */
#define g_lightIdx2     GV(u8, 0x800F632D)   /* s8: a second one */
#define g_lightTimer    GV(u8, 0x800F632E)   /* s8: frames to the next step; room 16: sequence state */
#define g_lightTimer2   GV(u16, 0x800F6330)  /* s16 */
#define g_room7Done     GV(s8, 0x800F6332)
#define g_messageShown  GV(s32, 0x800F63FC)  /* room 9 */
#define D_800A901A      GV(u8, 0x800A901A)
#define D_800A901B      GV(u8, 0x800A901B)
#define F_ROOM7_EVENT   0x800F53C4u          /* ovl17, not decompiled */
#define F_ROOM9_UPDATE  0x800F4488u          /* ovl17, not decompiled */
#define F_EVENT_8D8F4   0x8008D8F4u
#define LIGHT_TABLE     0x800F61A8u          /* room 14: (frames, row) pairs */
#define ROOM16_SCRIPT   0x800F6194u

static s32 idx(void) { return (s8)g_lightIdx; }
static s32 idx2(void) { return (s8)g_lightIdx2; }

/* Count the timer down; when it runs out, restart it at `reload` and
 * toggle the frame between 0 and 1. */
static void flicker(u8 reload) {
    g_lightTimer = (u8)(g_lightTimer - 1);
    if ((s8)g_lightTimer > 0) return;
    g_lightTimer = reload;
    g_lightIdx = idx() == 0;
}

/* The frame + 1, wrapping from `end` back to 1. */
static void cycle(s32 end) {
    g_lightIdx = (u8)(g_lightIdx + 1);
    if (idx() >= end) g_lightIdx = 1;
}

static void set_lights(u8 a, u8 b) {
    GV(u8, LIGHTS) = a;
    GV(u8, LIGHTS + 8) = b;
}

/* Room 16, after the script has started: what each frame of the sequence
 * (counted by g_lightTimer2 from 0x42) does: 1 sound 4, 2 a white flash,
 * 3 sound 3, 4 sound 5. */
static const u8 room16_seq[0x22] = {
    1, 2, 0, 0, 0, 0, 0, 2, 0, 2, 3, 0, 3, 1, 0, 2, 0,
    3, 3, 3, 3, 0, 3, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 4,
};

/* 800F38B0: every frame, by room (2-16). Returns 0. */
void n_stage17_RoomFrame(void) {
    FRAME(0x18);
    switch (g_room) {
    case 2:
        flag_test(EVENT_FLAGS, 0x6A);
        break;
    case 3:
        if (player_line_side(0x800F61D4u, 0x800F61E4u) >= 0) break;
        if (flag_test(EVENT_FLAGS, 0x91)) break;
        flag_set(EVENT_FLAGS, 0x91);
        GCALL2(F_MESSAGE, 0, 2);
        break;
    case 6:
        flicker(2);
        if (flag_test(EVENT_FLAGS, 0x6A) && flag_test(EVENT_FLAGS, 0x68)) {
            set_lights(9, 9);
            clut_anim(0x240, 0x1F0, s16v(idx() + 3));
        } else {
            clut_anim(0x240, 0x1F0, s16v(idx() + 1));
        }
        clut_anim(0x240, 0x1F5, s16v(idx() + (flag_test(EVENT_FLAGS, 0x69) ? 3 : 1)));
        break;
    case 7:
        if (flag_test(EVENT_FLAGS, 0x6A)) {
            clut_anim(0x230, 0x1E0, idx());
            cycle(3);
            if (g_room7Done == 0) GCALL0(F_ROOM7_EVENT);
            if (g_eventBusy == 0) D_800A901B = 1;
        }
        if (flag_test(EVENT_FLAGS, 0xA6)) {
            D_800A901A = 0;
            set_lights(0x17, 0x32);
        }
        break;
    case 8:
        cycle(3);
        clut_anim(0x240, 0x1F0, idx());
        clut_anim(0x240, 0x1F3, idx());
        clut_anim(0x240, 0x1F6, idx());
        break;
    case 9: {
        u32 on = idx() == 0;
        g_lightIdx = (u8)on;
        clut_anim(0x1E0, 0x1E0, (s32)on + 1);
        GCALL0(F_ROOM9_UPDATE);
        if (g_messageShown != 0) break;
        if (!flag_test(FLAGS_2FA0, 0x40)) break;
        g_messageShown = 1;
        GCALL2(F_MESSAGE, 0, 0);
        break;
    }
    case 10:
        flicker(2);
        clut_anim(0x2C0, 0xCD, s16v(idx() + 1));
        if (flag_test(EVENT_FLAGS, 0x71)) {
            if (g_eventBusy == 0) {
                D_800A901B = 1;
                set_lights(8, 8);
            }
            clut_anim(0x2C0, 0xC8, s16v(idx() + 3));
        } else {
            clut_anim(0x2C0, 0xC8, s16v(idx() + 1));
        }
        break;
    case 11:
        g_lightTimer = (u8)(g_lightTimer - 1);
        if ((s8)g_lightTimer <= 0) {
            g_lightTimer = 7;
            cycle(13);
            /* 1..7 up, then 8..12 back down (6..2) */
            u8 f = g_lightIdx;
            s32 i = (s8)f;
            if (i >= 8) f = (u8)(7 - i % 7);
            clut_anim(0x240, 0x1F0, f);
            clut_anim(0x240, 0x1F8, f);
        }
        if (g_eventMode != 1) break;
        if (!flag_test(ROOM_FLAGS, 0x33)) break;
        if (flag_test(EVENT_FLAGS, 0x98)) break;
        flag_set(EVENT_FLAGS, 0x98);
        GCALL2(F_MESSAGE, 0, 4);
        break;
    case 12:
        if (g_eventMode != 1) break;
        if (!flag_test(ROOM_FLAGS, 0xF5)) break;
        if (flag_test(EVENT_FLAGS, 0xAF)) break;
        GCALL2(F_EVENT_8D8F4, 3, 0);
        flag_set(EVENT_FLAGS, 0xAF);
        GCALL2(F_MESSAGE, 0, 7);
        break;
    case 13:
        if (flag_test(EVENT_FLAGS, 0x6E)) set_lights(8, 8);
        if (!flag_test(FLAGS_2FA0, 0x3C)) break;
        if (player_line_side(0x800F6284u, 0x800F6274u) < 0) break;
        if (flag_test(EVENT_FLAGS, 0x86)) {
            if (!flag_test(EVENT_FLAGS, 0x8E)) {
                if (flag_test(EVENT_FLAGS, 0xA9)) break;
                flag_set(EVENT_FLAGS, 0xA9);
                GCALL2(F_MESSAGE, 0, 6);
            } else {
                if (flag_test(EVENT_FLAGS, 0xAA)) break;
                flag_set(EVENT_FLAGS, 0xAA);
                GCALL2(F_MESSAGE, 0, 0xA);
            }
        } else {
            if (flag_test(EVENT_FLAGS, 0xA8)) break;
            flag_set(EVENT_FLAGS, 0xA8);
            GCALL2(F_MESSAGE, 0, 2);
        }
        break;
    case 14:
        if (flag_test(EVENT_FLAGS, 0x70)) {
            g_lightTimer = (u8)(g_lightTimer - 1);
            if ((s8)g_lightTimer <= 0)
                stage_ClutTableStep(0x800F632Cu, 0x800F632Eu, LIGHT_TABLE, 0x350, 0xF7);
        }
        if (flag_test(FLAGS_2FA0, 0x3C)) flag_set(EVENT_FLAGS, 0xAB);
        break;
    case 15:
        g_lightTimer = (u8)(g_lightTimer - 1);
        if ((s8)g_lightTimer <= 0) {
            g_lightTimer = 2;
            cycle(3);
            clut_anim(0x1C0, 0x1E0, idx());
        }
        if (flag_test(EVENT_FLAGS, 0x70)) {
            g_lightTimer2 = (u16)(g_lightTimer2 - 1);
            if ((s16)g_lightTimer2 > 0) break;
            g_lightTimer2 = 8;
            g_lightIdx2 = (u8)(g_lightIdx2 + 1);
            if (idx2() >= 9) g_lightIdx2 = 1;
            clut_anim(0x2F0, 0x40, idx2());
        } else {
            clut_anim(0x1B0, 0x1E0, idx());
            g_lightTimer2 = (u16)(g_lightTimer2 - 1);
            if ((s16)g_lightTimer2 > 0) break;
            g_lightTimer2 = 2;
            g_lightIdx2 = (u8)(g_lightIdx2 + 1);
            if (idx2() >= 4) g_lightIdx2 = 1;
            clut_anim(0x2F0, 0x50, idx2());
        }
        break;
    case 16:
        cycle(5);
        clut_anim(0x230, 0x1E0, s16v((s8)(idx() / 3) + 1));
        if (!flag_test(EVENT_FLAGS, 0x72)) {
            s8 state = (s8)g_lightTimer;
            if (state == 0) {
                GCALL2(F_EVENT_SCRIPT, 0, ROOM16_SCRIPT);
                g_lightTimer = (u8)(g_lightTimer + 1);
            } else if (state == 1) {
                u16 t = g_lightTimer2;
                g_lightTimer2 = (u16)(t + 1);
                u32 k = (u32)(s32)(s16)(t - 0x42);
                if (k < 0x22) {
                    switch (room16_seq[k]) {
                    case 1: sound_at(6, 4); break;
                    case 2: screen_flash(); break;
                    case 3: sound_at(6, 3); break;
                    case 4: sound_at(6, 5); break;
                    }
                }
            }
        }
        if ((s8)g_lightTimer >= 100) g_lightTimer = 100;
        break;
    }
    V0 = 0;
}
