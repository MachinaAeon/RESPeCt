/* KERNEL 0x800110D8: helper space for player_Update.
 *
 * The original function here is GunCon-only. Its single caller runs it only
 * when the port-1 controller type is GunCon, and the NA build relabels a
 * GunCon as "no controller" (0x8000) just before that check, so it never
 * runs. Its 0x1C0 bytes now hold player_Update's walking step, which is
 * only called from TEST.BIN (gameplay), so
 * reaching into TEST.BIN code and data from here is fine. The entry itself
 * does nothing. */
#include "game.h"

void func_800110D8(void)
{
}

/* player_Update's walking step (original behaviour): step along rot (the
 * player's heading, or a turned copy for FPS strafes) at g_playerMoveSpeed, advance the walk/run/backstep animation (its table
 * also gives the vertical bob), add the animation's position offset, then
 * move through collision. */
void player_MoveStep(s16 *rot)
{
    s32 step[4];
    s32 adjust[4];
    s32 i;

    func_80077F68();
    func_8006F7A8(rot, step, g_playerMoveSpeed);

    i = D_800B4DE8;
    D_800B4DE8 = i + 1;
    D_800A8634 = D_800B4DE4[i] - 0x640;
    if (D_800B4DE4[i + 1] == -1) {          /* end of the animation cycle */
        D_800B4DE8 = 0;
        if (g_playerMoveSpeed < 0)
            D_800B4DF8 = D_800B4DF8 < 3 ? D_800B4DF8 + 1 : 0x64;
        func_80084B04();
    }

    func_80081C80(adjust, step);
    step[0] += adjust[0];
    step[1] += adjust[1];
    step[2] += adjust[2];
    player_ApplyStep(step);   /* inline, game.h */

    func_800780E4();
    func_80078208();
    if (D_800A865C)
        func_800789F8();
}
