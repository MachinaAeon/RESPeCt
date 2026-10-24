/* player_Control: original-behaviour C version (TEST.BIN 0x800771AC).
 *
 * Runs every frame from player_Update. Turns pad input into
 * g_playerMoveFlags (forward/back, turn left/right, walking, aiming), the
 * movement speed and the turn speed, with separate paths for a DualShock in
 * analog mode, a digital pad and a GunCon. Holding aim hands over to
 * player_AimMode and stops movement.
 *
 * Written from the assembly to behave identically; the FPS prototype is
 * built on top of this. */
#include "game.h"

#define SPEED_WALK  (-0x50)
#define SPEED_RUN   (-0x96)
#define SPEED_BACK  0x3C
#define RUN_LIMIT   (-0x8B)     /* speeds below this use the run animation */

/* Switching between running and not running restarts the animation. */
static void track_run_change(void)
{
    if ((D_800B4DFC < RUN_LIMIT) != (g_playerMoveSpeed < RUN_LIMIT))
        D_800B4DE4 = 0;
    D_800B4DFC = (s16)g_playerMoveSpeed;
}

/* Releasing aim while one of the hold-to-fire weapons is charged. */
static void aim_released_with_charge(void)
{
    u16 w = (u16)*g_currentWeapon;

    if ((u16)(w - 13) < 2 && (g_buttonsPrev & BTN_AIM) && g_weaponState == 4) {
        if ((s16)w == 14) {
            func_800805F4(1, 3, 0x7F, 0x7F);
            g_weaponState = 5;
        } else {
            g_weaponState = 8;
        }
    }
}

/* Digital pad section; also runs after the analog stick on a DualShock. */
static void digital_section(void)
{
    s32 b;
    u16 cooldown;

    if (!(g_playerMoveFlags & MOVE_AIM)) {
        if (g_buttons & BTN_AIM) {
            player_AimMode();
        } else {
            aim_released_with_charge();

            if (!(g_playerMoveFlags & (MOVE_TURN_RIGHT | MOVE_TURN_LEFT))) {
                b = g_buttons;
                if (b & BTN_RIGHT) {
                    g_playerMoveFlags |= MOVE_TURN_RIGHT;
                    D_800B4E01 = 0xFF;
                } else if (b & BTN_LEFT) {
                    g_playerMoveFlags |= MOVE_TURN_LEFT;
                    D_800B4E01 = 0;
                }
            }

            cooldown = g_fireCooldown;
            if (cooldown & 0x7FFF) {
                g_fireCooldown = cooldown - 2;
                g_playerMoveFlags &= ~(MOVE_FWD | MOVE_BACK);
            }

            if (!(g_playerMoveFlags & (MOVE_FWD | MOVE_BACK))) {
                g_playerMoveFlags |= MOVE_WALK;
                g_playerMoveSpeed = SPEED_WALK;
                b = g_buttons;
                if (b & BTN_UP) {
                    g_playerMoveFlags |= MOVE_FWD;
                    if (b & BTN_RUN)
                        g_playerMoveSpeed = SPEED_RUN;
                }
                if (g_buttons & BTN_DOWN) {
                    g_playerMoveSpeed = SPEED_BACK;
                    g_playerMoveFlags |= MOVE_BACK;
                }
            }
        }
    }

    if ((g_buttons & BTN_QUICKSEARCH) && !(g_buttonsPrev & BTN_QUICKSEARCH)) {
        D_800A8670 = 0;
        D_800A85B8 |= 0x100;
    }

    track_run_change();
}

static void dualshock_section(void)
{
    u8 v;

    if (g_buttons & BTN_AIM) {
        g_playerMoveFlags |= MOVE_AIM;
        player_AimMode();
        return;
    }

    g_playerMoveFlags |= MOVE_WALK;
    g_playerStick = pad_GetAxes(0);

    v = ((u8 *)&g_playerStick)[0];          /* forward / back */
    if (v >= 0xB1) {
        g_playerMoveSpeed = SPEED_BACK;
        g_playerMoveFlags |= MOVE_BACK;
    } else if (v < 0x50) {
        g_playerMoveFlags |= MOVE_FWD;
        g_playerMoveSpeed = v - 0x96;
    }

    v = D_800B4E01;                          /* turn */
    if (v >= 0xB1)
        g_playerMoveFlags |= MOVE_TURN_RIGHT;
    else if (v < 0x50)
        g_playerMoveFlags |= MOVE_TURN_LEFT;
}

/* The original's GunCon path is left out: the NA build relabels a GunCon as
 * "no controller" (type 0x8000) before this runs, so it is never reached.
 * That frees the space the FPS mode below needs. */

/* ---- FPS prototype: keyboard pad in port 1, PS Mouse in port 2 ----
 * Port 1 stays a normal digital pad (bound to the keyboard), so the title
 * screen and menus work as usual. The mouse is read straight from port 2's
 * receive buffer: X motion turns, Y motion tilts the view (the camera pitch
 * the game uses for quick search), and a held left click presses the game's
 * fire action. W/S move, A/D strafe (player_Update turns the step), Cross
 * (left Shift) sprints. The crosshair stays centred so firing works while
 * moving. */


static s32 mouse_in_port2(void)
{
    return (g_padState2 & PADSTATE_TYPE_MASK) == PADTYPE_MOUSE;
}

/* FPS mode: digital pad in port 1 and a PS Mouse in port 2. */
s32 player_FpsActive(void)
{
    return g_padTypeBit == 0x80000 && mouse_in_port2();
}

static void fps_section(void)
{
    u8 *m = g_padBuf2Ptr;                   /* mouse receive buffer */
    u8 *p = g_padBuf1Ptr;                   /* port 1 pad receive buffer */
    s32 ok = m[0] == 0;
    s32 dx = 0;
    s32 b = g_buttons;
    s32 sprint = p[0] == 0 && !(p[3] & PAD_CROSS);

    if (ok)
        dx = (s8)m[4];                      /* X motion this poll */

    /* Only the mouse fires: the keyboard's fire buttons are ignored (Cross,
     * left Shift, is sprint here). */
    b &= ~BTN_FIRE;
    /* Left click = fire. It stays in g_buttons for the rest of the frame,
     * so next frame's g_buttonsPrev sees it and a held click counts once. */
    if (ok && !(m[3] & MOUSE_LEFT))
        b |= BTN_FIRE;

    g_playerYawVel = (s16)(dx * PORT_CONFIG->yaw_scale);

    /* Looking up and down is done in player_Update (more room there). */

    /* W forward (sprint with Shift), S back, A/D alone strafe at walking
     * speed. player_Update turns the step for strafes and diagonals. */
    g_playerMoveFlags |= MOVE_WALK;
    g_playerMoveSpeed = SPEED_WALK;
    if (b & BTN_UP) {
        g_playerMoveFlags |= MOVE_FWD;
        if (sprint)
            g_playerMoveSpeed = SPEED_RUN;
    } else if (b & BTN_DOWN) {
        g_playerMoveFlags |= MOVE_BACK;
        g_playerMoveSpeed = SPEED_BACK;
    } else if (b & (BTN_LEFT | BTN_RIGHT)) {
        g_playerMoveFlags |= MOVE_FWD;
    }

    /* Crosshair always centred; player_AimMode draws it and handles firing.
     * It would also move the reticle with the d-pad (W/A/S/D here), so hide
     * the direction keys from it for this call. */
    g_reticleX = 0xA0;
    g_reticleY = 0x78;
    g_buttons = b & ~(BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT);
    player_AimMode();
    g_buttons = b;

    track_run_change();
}

/* Area-specific rule (same check as the original): forward/back movement is
 * blocked here, and player_Update pins the heading. */
s32 player_MovementLockedArea(void)
{
    return D_800BB19C != 3 && D_800A866C == 2 && D_800B3F08 == 0xC
        && func_8008F64C(D_80062F60, 0xF5) == 0;
}

static void update_turn_speed(void)
{
    s32 a, q;

    if (g_playerMoveFlags & (MOVE_TURN_RIGHT | MOVE_TURN_LEFT)) {
        a = D_800B4E01 - 0x80;
        if (a < 0)
            a = -a;
        a >>= 1;
        if (!(g_playerMoveFlags & MOVE_TURN_RIGHT))
            a = -a;                          /* turning left */
        q = (s16)((s16)a * 7 / 10);
        g_playerYawVel = (s16)(q * g_playerTurnRamp / 10);
        if (g_playerTurnRamp < 10)
            g_playerTurnRamp++;
    } else if (g_playerYawVel >= 7) {
        g_playerYawVel = 6;
    } else if (g_playerYawVel < -6) {
        g_playerYawVel = -6;
    } else if (g_playerYawVel > 0) {
        g_playerYawVel--;
    } else if (g_playerYawVel < 0) {
        g_playerYawVel++;
    } else {
        g_playerTurnRamp = 0;
    }

    /* Reversing direction stops the turn at once. */
    if ((D_800B4DFE > 0 && g_playerYawVel < 0) || (D_800B4DFE < 0 && g_playerYawVel > 0)) {
        g_playerYawVel = 0;
        g_playerTurnRamp = 0;
    }
    D_800B4DFE = g_playerYawVel;
}

void player_Control(void)
{
    s32 fps = player_FpsActive();

    g_playerMoveFlags = 0;
    func_8007BC7C();

    switch (g_padTypeBit) {
    case 0x400000:                           /* DualShock, analog mode */
        if (D_80062DEC) {
            dualshock_section();
            if (D_80062DEC)
                digital_section();
        }
        break;
    case 0x80000:                            /* digital pad */
        if (D_80062DEC) {
            if (fps)
                fps_section();               /* FPS prototype */
            else
                digital_section();
        } else if (fps) {
            g_playerYawVel = 0;              /* no control (e.g. cutscene) */
        }
        break;
    }

    /* Area-specific rule: no forward/back movement under this condition. */
    if (player_MovementLockedArea())
        g_playerMoveFlags &= ~(MOVE_FWD | MOVE_BACK);

    if (fps) {
        /* Mouse sets the turn directly: no ramp or decay. */
        g_playerTurnRamp = 0;
        D_800B4DFE = g_playerYawVel;
    } else {
        update_turn_speed();
    }
}
