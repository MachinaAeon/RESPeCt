/* player_AimMode: original-behaviour C version (TEST.BIN 0x800779DC).
 *
 * Runs every frame while the aim button is held. Moves a 2D reticle on
 * screen with the analog stick (DualShock in analog mode) and the d-pad,
 * turns the view when the reticle is pushed against the left/right edge,
 * draws the reticle, and handles firing per weapon type.
 *
 * Written from the assembly to behave identically; this is the starting
 * point for the FPS-style aiming. */
#include "game.h"

#define RET_CENTRE_X 0xA0
#define RET_CENTRE_Y 0x78
#define RET_MIN_X    0x40
#define RET_MAX_X    0x100
#define RET_MIN_Y    0x10
#define RET_MAX_Y    0xDF

#define WEAPON_HOLD_A 13
#define WEAPON_HOLD_B 14

/* Reticle pushed past the right/left limit: clamp and turn the view. */
static void push_right(void)
{
    g_reticleX = RET_MAX_X;
    D_800B4E01 = 0xFF;
    g_playerMoveFlags |= MOVE_TURN_RIGHT;
}

static void push_left(void)
{
    g_reticleX = RET_MIN_X;
    D_800B4E01 = 0;
    g_playerMoveFlags |= MOVE_TURN_LEFT;
}

static void stick_moves_reticle(void)
{
    s32 v;

    v = g_inputAxes[0];                     /* vertical */
    if (v >= 0xB1) {
        g_reticleY += (v - 0x80) / 10;
        if (g_reticleY > RET_MAX_Y)
            g_reticleY = RET_MAX_Y;
    } else if (v < 0x50) {
        g_reticleY -= (0x80 - v) / 10;
        if (g_reticleY < RET_MIN_Y)
            g_reticleY = RET_MIN_Y;
    }

    v = g_inputAxes[1];                     /* horizontal */
    if (v >= 0xB1) {
        g_reticleX += (v - 0x80) / 8;
        if (g_reticleX > RET_MAX_X)
            push_right();
    } else if (v < 0x50) {
        g_reticleX -= (0x80 - v) / 8;
        if (g_reticleX < RET_MIN_X)
            push_left();
    }
}

static void dpad_moves_reticle(void)
{
    s32 b = g_buttons;      /* nothing below changes g_buttons */

    if (b & BTN_RIGHT) {
        g_reticleX += 8;
        if (g_reticleX > RET_MAX_X)
            push_right();
    }
    if (b & BTN_LEFT) {
        g_reticleX -= 8;
        if (g_reticleX < RET_MIN_X)
            push_left();
    }
    if (b & BTN_DOWN) {
        g_reticleY += 8;
        if (g_reticleY > RET_MAX_Y)
            g_reticleY = RET_MAX_Y;
    }
    if (b & BTN_UP) {
        g_reticleY -= 8;
        if (g_reticleY < RET_MIN_Y)
            g_reticleY = RET_MIN_Y;
    }
}

static void draw_reticle(void)
{
    s32 x = (u16)g_reticleX;
    s32 y = (u16)g_reticleY;

    D_800B3E28[4]  = x - 0xA2;  D_800B3E28[5]  = y - 0x7A;
    D_800B3E28[6]  = x - 0x9E;  D_800B3E28[7]  = y - 0x7A;
    D_800B3E28[8]  = x - 0xA2;  D_800B3E28[9]  = y - 0x76;
    D_800B3E28[10] = x - 0x9E;  D_800B3E28[11] = y - 0x76;
    D_800BBB98[2] = x - 0xA8;
    D_800BBB98[3] = y - 0x80;
}

static s32 shot_allowed(u16 cooldown)
{
    return !(cooldown & 0x8000) || (D_800AFCB0 & 8);
}

void player_AimMode(void)
{
    u16 cooldown;
    s32 fire, fire_prev;

    if (!(g_buttonsPrev & BTN_AIM)) {       /* just started aiming */
        g_reticleX = RET_CENTRE_X;
        g_reticleY = RET_CENTRE_Y;
    }
    D_800A0CB8 = 1;

    if (g_padTypeBit == 0x400000) {          /* DualShock, analog mode */
        if (D_80062DF4 != 3)
            stick_moves_reticle();
        dpad_moves_reticle();
    } else if (g_padTypeBit == 0x80000) {    /* digital pad */
        dpad_moves_reticle();
    }

    draw_reticle();

    cooldown = g_fireCooldown;
    if (cooldown & 0x7FFF) {
        g_fireCooldown = cooldown - 2;
        return;
    }

    fire = g_buttons & BTN_FIRE;
    fire_prev = g_buttonsPrev & BTN_FIRE;

    switch (*g_currentWeapon) {
    case WEAPON_HOLD_A:
        if (!fire) {
            g_weaponState = 8;
        } else if (!fire_prev) {
            if (shot_allowed(cooldown)) {
                g_fireRequest = 1;
                g_weaponState = 9;
            }
        } else if (g_weaponState == 4 && shot_allowed(cooldown)) {
            g_fireRequest = 1;
        }
        break;

    case WEAPON_HOLD_B:
        if (fire && !fire_prev) {
            g_weaponState = (g_weaponState == 6) ? 7 : 1;
        } else if (g_weaponState == 4) {
            if (fire && shot_allowed(cooldown)) {
                g_fireRequest = 1;
            } else {
                func_800805F4(1, 3, 0x7F, 0x7F);
                g_weaponState = 5;
            }
        }
        break;

    default:
        if (fire && !fire_prev && shot_allowed(cooldown)) {
            s32 x = (u16)g_reticleX;
            s32 y = (u16)g_reticleY;
            g_fireRequest = 1;
            g_weaponState = 0;
            fire_AtScreen(y - 0x80, cooldown, x - 0x9E, x - 0xA2);
        }
        break;
    }
}
