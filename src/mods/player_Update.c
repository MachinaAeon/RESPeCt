/* player_Update (TEST.BIN 0x800785C0): per-frame player movement.
 *
 * Runs player_Control, applies the turn to the heading, then moves the
 * player along the heading: the speed and rotation give a step
 * (func_8006F7A8), the walk/run/backstep animation table adds its offset,
 * and move_Collide checks the step against the room (on a hit it returns
 * a corrected position to slide to). The walking step itself
 * (player_MoveStep) lives in KERNEL's unused GunCon routine because this
 * slot is too small for the C version.
 *
 * Original behaviour, plus the FPS prototype (mouse in port 2):
 *  - A/D strafe and W/S + A/D diagonals: the same walking step with the
 *    heading turned, so the walk bob and footsteps play as usual.
 *  - Mouse look up/down: player_Control sets the camera pitch; the game
 *    would ease it back to level (after quick search, when turning or
 *    walking), so it is put back before the camera matrix is built. */
#include "game.h"

#define RUN_LIMIT (-0x8B)

static s32 abs32(s32 v)
{
    return v < 0 ? -v : v;
}

/* FPS: heading offset for the step from the held direction keys.
 * Positive = to the right (4096 = 360 degrees). A back step moves against
 * its heading, so a back diagonal turns the other way. */
static s32 fps_step_turn(void)
{
    s32 b = g_buttons;
    s32 side = b & BTN_RIGHT ? 0x200 : b & BTN_LEFT ? -0x200 : 0;

    if (g_playerMoveFlags & MOVE_BACK)
        return -side;
    if (b & BTN_UP)
        return side;
    return side * 2;                        /* pure strafe */
}

void player_Update(void)
{
    s16 yv;
    s16 rot[4];
    s16 *dir;
    s32 pitch;
    s32 fps;

    if (!g_playerFrozen) {
        player_Control();
        fps = player_FpsActive() && D_80062DEC;
        pitch = D_800A865C;
        if (fps) {
            /* FPS: mouse Y tilts the view (mouse down = look down). */
            u8 *m = g_padBuf2Ptr;
            s32 dy = m[0] == 0 ? (s8)m[5] : 0;
            s32 limit = PORT_CONFIG->pitch_limit;
            pitch += dy * PORT_CONFIG->pitch_scale;
            if (pitch > limit) pitch = limit;
            else if (pitch < -limit) pitch = -limit;
            D_800A865C = pitch;
        }

        if (D_800A12E8 == 0
            || (D_800A85B8 == 0 && D_800A865C != 0 && abs32(D_800B4DF0) >= 0x1F4))
            func_800789F8();

        yv = g_playerYawVel;
        if (yv) {
            PLAYER_YAW += yv;
            if (D_800A865C) {
                D_800B4DF0 += yv;
                if (*(s16 *)(D_800B4DEC + 0x31E) <= 0)
                    func_800789F8();
            }
        }
        PLAYER_YAW &= 0xFFF;

        if (D_800A85B8)
            func_80079064(D_800A85B8);
        func_80078460();

        if (g_playerMoveFlags & MOVE_WALK) {
            if (!D_800B4DE4) {
                D_800B4DE8 = 0;
                D_800B4DE4 = g_playerMoveSpeed > 0 ? D_800A12CC
                           : g_playerMoveSpeed >= RUN_LIMIT ? D_800A128C
                           : D_800A12B4;
            }
            if (D_800B6084) {
                func_80078208();
            } else if (g_playerMoveFlags & (MOVE_FWD | MOVE_BACK)) {
                dir = PLAYER_ROT;
                if (fps) {                  /* FPS prototype: strafe/diagonal */
                    rot[0] = dir[0];
                    rot[1] = dir[1] + fps_step_turn();
                    rot[2] = dir[2];
                    dir = rot;
                }
                player_MoveStep(dir);
            } else {
                D_800B4DE4 = 0;
                D_800B4DF8 = 0;
            }
        } else {
            D_800B4DF8 = 0;
            D_800B4DE4 = 0;
        }

        if (g_playerMoveFlags & MOVE_FWD)
            func_80079A28();

        /* FPS prototype: keep the mouse pitch, except while quick search
         * is turning the view toward something (then it stays there). */
        if (fps && !D_800A85B8)
            D_800A865C = pitch;
    }

    /* Area-specific rule: heading kept between 0x400 and 0x800 (the same
     * check as player_Control's, shared for space). */
    if (player_MovementLockedArea()) {
        if (PLAYER_YAW < 0x401)
            PLAYER_YAW = 0x400;
        else if (PLAYER_YAW >= 0x800)
            PLAYER_YAW = 0x800;
    }

    func_8006F69C(g_player, 0);
    func_8006F69C(D_800A8618, 0);
    func_80082760(D_800BB790, 0);
    func_80033FD8(D_800B3EE8);
}
