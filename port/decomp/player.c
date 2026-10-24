/* Player control: decompiled to native C (TEST.BIN 0x800771AC, 0x800779DC,
 * 0x800785C0), with the PC port's FPS controls built in.
 *
 *   n_player_Update   per frame: control, heading, walking step, camera
 *   n_player_Control  pad input -> movement flags, speed, turn
 *   n_player_AimMode  aiming: reticle, firing per weapon
 *
 * Original behaviour for a digital pad or a DualShock; FPS mode when a
 * PlayStation Mouse is in port 2 (the PC port always provides one): the
 * mouse turns and tilts the view, left click fires, W/S move, A/D strafe,
 * sprint is Cross. This replaces the MIPS versions in src/mods (which the
 * DuckStation patch still uses). */
#include "game_vars.h"
#include "savegame.h"
#include "savestate.h"

extern int16_t g_fps_pitch_limit;          /* port/src/rt.c, from resurv.ini */

#define SPEED_WALK  (-0x50)
#define SPEED_RUN   (-0x96)
#define SPEED_BACK  0x3C
#define RUN_LIMIT   (-0x8B)                 /* speeds below this use the run animation */
#define PAD_CROSS   0x40                    /* raw pad buffer [3] bit */

void n_player_AimMode(void);
void n_player_Control(void);

/* ------------------------------------------------------------ aiming */

#define RET_CENTRE_X 0xA0
#define RET_CENTRE_Y 0x78
#define RET_MIN_X    0x40
#define RET_MAX_X    0x100
#define RET_MIN_Y    0x10
#define RET_MAX_Y    0xDF

/* Reticle pushed past the right/left limit: clamp and turn the view. */
static void push_right(void) { g_reticleX = RET_MAX_X; D_800B4E01 = 0xFF; g_playerMoveFlags |= MOVE_TURN_RIGHT; }
static void push_left(void) { g_reticleX = RET_MIN_X; D_800B4E01 = 0; g_playerMoveFlags |= MOVE_TURN_LEFT; }

static void stick_moves_reticle(void) {
    s32 v = g_inputAxes[0];                 /* vertical */
    if (v >= 0xB1) {
        g_reticleY += (v - 0x80) / 10;
        if (g_reticleY > RET_MAX_Y) g_reticleY = RET_MAX_Y;
    } else if (v < 0x50) {
        g_reticleY -= (0x80 - v) / 10;
        if (g_reticleY < RET_MIN_Y) g_reticleY = RET_MIN_Y;
    }
    v = g_inputAxes[1];                     /* horizontal */
    if (v >= 0xB1) {
        g_reticleX += (v - 0x80) / 8;
        if (g_reticleX > RET_MAX_X) push_right();
    } else if (v < 0x50) {
        g_reticleX -= (0x80 - v) / 8;
        if (g_reticleX < RET_MIN_X) push_left();
    }
}

static void dpad_moves_reticle(void) {
    s32 b = g_buttons;
    if (b & BTN_RIGHT) { g_reticleX += 8; if (g_reticleX > RET_MAX_X) push_right(); }
    if (b & BTN_LEFT) { g_reticleX -= 8; if (g_reticleX < RET_MIN_X) push_left(); }
    if (b & BTN_DOWN) { g_reticleY += 8; if (g_reticleY > RET_MAX_Y) g_reticleY = RET_MAX_Y; }
    if (b & BTN_UP) { g_reticleY -= 8; if (g_reticleY < RET_MIN_Y) g_reticleY = RET_MIN_Y; }
}

static void draw_reticle(void) {
    s32 x = (u16)g_reticleX, y = (u16)g_reticleY;
    D_800B3E28[4] = (s16)(x - 0xA2);  D_800B3E28[5] = (s16)(y - 0x7A);
    D_800B3E28[6] = (s16)(x - 0x9E);  D_800B3E28[7] = (s16)(y - 0x7A);
    D_800B3E28[8] = (s16)(x - 0xA2);  D_800B3E28[9] = (s16)(y - 0x76);
    D_800B3E28[10] = (s16)(x - 0x9E); D_800B3E28[11] = (s16)(y - 0x76);
    D_800BBB98[2] = (s16)(x - 0xA8);
    D_800BBB98[3] = (s16)(y - 0x80);
}

static s32 shot_allowed(u16 cooldown) { return !(cooldown & 0x8000) || (D_800AFCB0 & 8); }

#define WEAPON_HOLD_A 13
#define WEAPON_HOLD_B 14

void n_player_AimMode(void) {
    if (!(g_buttonsPrev & BTN_AIM)) {       /* just started aiming */
        g_reticleX = RET_CENTRE_X;
        g_reticleY = RET_CENTRE_Y;
    }
    D_800A0CB8 = 1;

    if (g_padTypeBit == 0x400000) {          /* DualShock, analog mode */
        if (D_80062DF4 != 3) stick_moves_reticle();
        dpad_moves_reticle();
    } else if (g_padTypeBit == 0x80000) {    /* digital pad */
        dpad_moves_reticle();
    }
    draw_reticle();

    u16 cooldown = g_fireCooldown;
    if (cooldown & 0x7FFF) {
        g_fireCooldown = (u16)(cooldown - 2);
        return;
    }
    s32 fire = g_buttons & BTN_FIRE, fire_prev = g_buttonsPrev & BTN_FIRE;
    switch (GV(s16, g_currentWeaponAddr)) {
    case WEAPON_HOLD_A:
        if (!fire) {
            g_weaponState = 8;
        } else if (!fire_prev) {
            if (shot_allowed(cooldown)) { g_fireRequest = 1; g_weaponState = 9; }
        } else if (g_weaponState == 4 && shot_allowed(cooldown)) {
            g_fireRequest = 1;
        }
        break;
    case WEAPON_HOLD_B:
        if (fire && !fire_prev) {
            g_weaponState = g_weaponState == 6 ? 7 : 1;
        } else if (g_weaponState == 4) {
            if (fire && shot_allowed(cooldown)) {
                g_fireRequest = 1;
            } else {
                GCALL4(F_SOUND_CUE, 1, 3, 0x7F, 0x7F);
                g_weaponState = 5;
            }
        }
        break;
    default:
        if (fire && !fire_prev && shot_allowed(cooldown)) {
            s32 x = (u16)g_reticleX, y = (u16)g_reticleY;
            g_fireRequest = 1;
            g_weaponState = 0;
            GCALL4(F_FIRE_AT_SCREEN, y - 0x80, cooldown, x - 0x9E, x - 0xA2);
        }
        break;
    }
}

/* ------------------------------------------------------------ control */

/* Switching between running and not running restarts the animation. */
static void track_run_change(void) {
    if ((D_800B4DFC < RUN_LIMIT) != (g_playerMoveSpeed < RUN_LIMIT)) D_800B4DE4 = 0;
    D_800B4DFC = (s16)g_playerMoveSpeed;
}

/* Releasing aim while one of the hold-to-fire weapons is charged. */
static void aim_released_with_charge(void) {
    u16 w = (u16)GV(s16, g_currentWeaponAddr);
    if ((u16)(w - 13) < 2 && (g_buttonsPrev & BTN_AIM) && g_weaponState == 4) {
        if ((s16)w == 14) {
            GCALL4(F_SOUND_CUE, 1, 3, 0x7F, 0x7F);
            g_weaponState = 5;
        } else {
            g_weaponState = 8;
        }
    }
}

/* Digital pad; also runs after the analog stick on a DualShock. */
static void digital_section(void) {
    if (!(g_playerMoveFlags & MOVE_AIM)) {
        if (g_buttons & BTN_AIM) {
            n_player_AimMode();
        } else {
            aim_released_with_charge();
            if (!(g_playerMoveFlags & (MOVE_TURN_RIGHT | MOVE_TURN_LEFT))) {
                if (g_buttons & BTN_RIGHT) { g_playerMoveFlags |= MOVE_TURN_RIGHT; D_800B4E01 = 0xFF; }
                else if (g_buttons & BTN_LEFT) { g_playerMoveFlags |= MOVE_TURN_LEFT; D_800B4E01 = 0; }
            }
            u16 cooldown = g_fireCooldown;
            if (cooldown & 0x7FFF) {
                g_fireCooldown = (u16)(cooldown - 2);
                g_playerMoveFlags &= ~(MOVE_FWD | MOVE_BACK);
            }
            if (!(g_playerMoveFlags & (MOVE_FWD | MOVE_BACK))) {
                g_playerMoveFlags |= MOVE_WALK;
                g_playerMoveSpeed = SPEED_WALK;
                if (g_buttons & BTN_UP) {
                    g_playerMoveFlags |= MOVE_FWD;
                    if (g_buttons & BTN_RUN) g_playerMoveSpeed = SPEED_RUN;
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

static void dualshock_section(void) {
    if (g_buttons & BTN_AIM) {
        g_playerMoveFlags |= MOVE_AIM;
        n_player_AimMode();
        return;
    }
    g_playerMoveFlags |= MOVE_WALK;
    g_playerStick = (s32)GCALL1(F_PAD_GET_AXES, 0);
    u8 v = (u8)g_playerStick;               /* forward / back */
    if (v >= 0xB1) {
        g_playerMoveSpeed = SPEED_BACK;
        g_playerMoveFlags |= MOVE_BACK;
    } else if (v < 0x50) {
        g_playerMoveFlags |= MOVE_FWD;
        g_playerMoveSpeed = v - 0x96;
    }
    v = D_800B4E01;                          /* turn */
    if (v >= 0xB1) g_playerMoveFlags |= MOVE_TURN_RIGHT;
    else if (v < 0x50) g_playerMoveFlags |= MOVE_TURN_LEFT;
}

/* FPS mode: digital pad in port 1 and a PlayStation Mouse in port 2. */
static s32 fps_active(void) {
    return g_padTypeBit == 0x80000 && (g_padState2 & PADSTATE_TYPE_MASK) == PADTYPE_MOUSE;
}

static void fps_section(void) {
    const u8 *m = gp(g_padBuf2Addr);        /* mouse receive buffer */
    const u8 *p = gp(g_padBuf1Addr);        /* pad receive buffer */
    s32 ok = m[0] == 0;
    s32 b = g_buttons;
    s32 sprint = p[0] == 0 && !(p[3] & PAD_CROSS);

    /* Only the mouse fires; Cross is sprint. A held click stays in
     * g_buttons for the frame, so it counts once. */
    b &= ~BTN_FIRE;
    if (ok && !(m[3] & MOUSE_LEFT) && !sg_at_payphone()) b |= BTN_FIRE;   /* no shots at a payphone */

    g_playerYawVel = (s16)(ok ? (s8)m[4] : 0);   /* the PC side scales by sensitivity */

    /* W forward (sprint with Shift), S back, A/D alone strafe; player_Update
     * turns the step for strafes and diagonals. */
    g_playerMoveFlags |= MOVE_WALK;
    g_playerMoveSpeed = SPEED_WALK;
    if (b & BTN_UP) {
        g_playerMoveFlags |= MOVE_FWD;
        if (sprint) g_playerMoveSpeed = SPEED_RUN;
    } else if (b & BTN_DOWN) {
        g_playerMoveFlags |= MOVE_BACK;
        g_playerMoveSpeed = SPEED_BACK;
    } else if (b & (BTN_LEFT | BTN_RIGHT)) {
        g_playerMoveFlags |= MOVE_FWD;
    }

    /* Crosshair always centred; the aim code draws it and fires. It would
     * move the reticle with the d-pad (WASD), so hide those from it. */
    g_reticleX = RET_CENTRE_X;
    g_reticleY = RET_CENTRE_Y;
    g_buttons = b & ~(BTN_UP | BTN_DOWN | BTN_LEFT | BTN_RIGHT);
    n_player_AimMode();
    g_buttons = b;
    track_run_change();
}

/* Area-specific rule: forward/back blocked here and the heading pinned. */
static s32 movement_locked_area(void) {
    return D_800BB19C != 3 && D_800A866C == 2 && D_800B3F08 == 0xC &&
           GCALL2(F_AREA_CHECK, D_80062F60_ADDR, 0xF5) == 0;
}

static void update_turn_speed(void) {
    if (g_playerMoveFlags & (MOVE_TURN_RIGHT | MOVE_TURN_LEFT)) {
        s32 a = D_800B4E01 - 0x80;
        if (a < 0) a = -a;
        a >>= 1;
        if (!(g_playerMoveFlags & MOVE_TURN_RIGHT)) a = -a;
        s32 q = (s16)((s16)a * 7 / 10);
        g_playerYawVel = (s16)(q * g_playerTurnRamp / 10);
        if (g_playerTurnRamp < 10) g_playerTurnRamp++;
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

void n_player_Control(void) {
    s32 fps = fps_active();
    g_playerMoveFlags = 0;
    GCALL0(F_CONTROL_PRE);
    switch (g_padTypeBit) {
    case 0x400000:                           /* DualShock, analog mode */
        if (D_80062DEC) {
            dualshock_section();
            if (D_80062DEC) digital_section();
        }
        break;
    case 0x80000:                            /* digital pad */
        if (D_80062DEC) {
            if (fps) fps_section();
            else digital_section();
        } else if (fps) {
            g_playerYawVel = 0;              /* no control (e.g. cutscene) */
        }
        break;
    }
    if (movement_locked_area()) g_playerMoveFlags &= ~(MOVE_FWD | MOVE_BACK);
    if (fps) {
        g_playerTurnRamp = 0;                /* the mouse sets the turn directly */
        D_800B4DFE = g_playerYawVel;
    } else {
        update_turn_speed();
    }
}

/* ------------------------------------------------------------ update */

/* Move the player by step (a game address) through collision: if clear add
 * it, otherwise move_Collide put the corrected position in step x/z. */
static void apply_step(u32 step) {
    s32 *s = gp(step);
    if (GCALL3(F_MOVE_COLLIDE, PLAYER_POS_ADDR, step, 0) == 0) {
        PLAYER_POS[0] += s[0];
        PLAYER_POS[2] += s[2];
    } else {
        PLAYER_POS[0] = s[0];
        PLAYER_POS[2] = s[2];
    }
}

/* The walking step along rot (a game address of a rotation vector): step at
 * the move speed, advance the walk/run/backstep animation (bob and
 * footsteps), add the animation's offset, move through collision. */
static void move_step(u32 rot) {
    GameStack gs;
    u32 frame = gstack_alloc(&gs, 32);
    u32 step = frame, adjust = frame + 16;
    s32 *st = gp(step), *ad = gp(adjust);

    GCALL0(F_MOVE_PRE);
    GCALL3(F_ROT_STEP, rot, step, g_playerMoveSpeed);

    const s16 *tbl = gp(D_800B4DE4);
    s32 i = D_800B4DE8;
    D_800B4DE8 = i + 1;
    D_800A8634 = tbl[i] - 0x640;
    if (tbl[i + 1] == -1) {                  /* end of the animation cycle */
        D_800B4DE8 = 0;
        if (g_playerMoveSpeed < 0) D_800B4DF8 = (s16)(D_800B4DF8 < 3 ? D_800B4DF8 + 1 : 0x64);
        GCALL0(F_STEP_SOUND);
    }
    GCALL2(F_ANIM_OFFSET, adjust, step);
    st[0] += ad[0];
    st[1] += ad[1];
    st[2] += ad[2];
    apply_step(step);

    GCALL0(F_MOVE_POST);
    GCALL0(F_MOVE_ANIM_END);
    if (D_800A865C) GCALL0(F_PITCH_LEVEL);
    gstack_free(&gs);
}

/* FPS: heading offset for the step from the held direction keys
 * (positive = right; a back step moves against its heading). */
static s32 fps_step_turn(void) {
    s32 b = g_buttons;
    s32 side = b & BTN_RIGHT ? 0x200 : b & BTN_LEFT ? -0x200 : 0;
    if (g_playerMoveFlags & MOVE_BACK) return -side;
    if (b & BTN_UP) return side;
    return side * 2;                         /* pure strafe */
}

static s32 qs_pressed;                      /* quick search started by the key */

void decomp_state(StateIO *io) {
    ss_module(io, "decomp");
    SS(io, qs_pressed);
}

void port_weapon_frame(void);                /* cheats.c */

void n_player_Update(void) {
    port_weapon_frame();
    sg_game_frame(g_sg_use);
    g_sg_use = 0;
    if (!g_playerFrozen) {
        n_player_Control();
        s32 fps = fps_active() && D_80062DEC;
        s32 pitch = D_800A865C;
        if (fps) {
            /* mouse Y tilts the view (mouse down = look down) */
            const u8 *m = gp(g_padBuf2Addr);
            s32 dy = m[0] == 0 ? (s8)m[5] : 0;
            s32 limit = g_fps_pitch_limit;
            pitch += dy;
            if (pitch > limit) pitch = limit;
            else if (pitch < -limit) pitch = -limit;
            D_800A865C = (s16)pitch;
        }

        s32 since = D_800B4DF0 < 0 ? -D_800B4DF0 : D_800B4DF0;
        if (D_800A12E8 == 0 || (D_800A85B8 == 0 && D_800A865C != 0 && since >= 0x1F4))
            GCALL0(F_PITCH_LEVEL);

        s16 yv = g_playerYawVel;
        if (yv) {
            PLAYER_YAW = (s16)(PLAYER_YAW + yv);
            if (D_800A865C) {
                D_800B4DF0 += yv;
                if (*(s16 *)gp(D_800B4DEC + 0x31E) <= 0) GCALL0(F_PITCH_LEVEL);
            }
        }
        PLAYER_YAW &= 0xFFF;

        /* The game turns the view onto a target when your shot hits one
         * (and in a few other cases) through the quick-search turn. A
         * pressed quick search starts with a request (0x100); in FPS mode
         * turns that didn't are cancelled, so the aim stays yours. */
        if (D_800A85B8 & 0x100) qs_pressed = 1;
        else if (D_800A85B8 == 0) qs_pressed = 0;
        if (fps && D_800A85B8 && !qs_pressed) D_800A85B8 = 0;
        if (D_800A85B8) GCALL1(F_QUICK_SEARCH, D_800A85B8);
        GCALL0(F_UPDATE_PRE);

        if (g_playerMoveFlags & MOVE_WALK) {
            if (!D_800B4DE4) {
                D_800B4DE8 = 0;
                D_800B4DE4 = g_playerMoveSpeed > 0 ? ANIM_BACK : g_playerMoveSpeed >= RUN_LIMIT ? ANIM_WALK : ANIM_RUN;
            }
            if (D_800B6084) {
                GCALL0(F_MOVE_ANIM_END);
            } else if (g_playerMoveFlags & (MOVE_FWD | MOVE_BACK)) {
                if (fps) {                   /* strafe / diagonal: a turned copy of the heading */
                    GameStack gs;
                    u32 rot = gstack_alloc(&gs, 8);
                    s16 *r = gp(rot);
                    r[0] = PLAYER_ROT[0];
                    r[1] = (s16)(PLAYER_ROT[1] + fps_step_turn());
                    r[2] = PLAYER_ROT[2];
                    r[3] = 0;
                    move_step(rot);
                    gstack_free(&gs);
                } else {
                    move_step(PLAYER_ROT_ADDR);
                }
            } else {
                D_800B4DE4 = 0;
                D_800B4DF8 = 0;
            }
        } else {
            D_800B4DF8 = 0;
            D_800B4DE4 = 0;
        }

        if (g_playerMoveFlags & MOVE_FWD) GCALL0(F_TRIGGERS);

        /* keep the mouse pitch, except while quick search turns the view */
        if (fps && !D_800A85B8) D_800A865C = (s16)pitch;
    }

    if (movement_locked_area()) {            /* heading kept between 0x400 and 0x800 */
        if (PLAYER_YAW < 0x401) PLAYER_YAW = 0x400;
        else if (PLAYER_YAW >= 0x800) PLAYER_YAW = 0x800;
    }

    GCALL2(F_OBJ_MATRIX, g_player, 0);
    GCALL2(F_OBJ_MATRIX, CAMERA_OBJ, 0);
    GCALL2(F_UPDATE_TAIL1, D_800BB790_ADDR, 0);
    GCALL1(F_UPDATE_TAIL2, D_800B3EE8_ADDR);
}
