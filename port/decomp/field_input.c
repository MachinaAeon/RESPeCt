/* Field input outside the player's own control (TEST overlay): opening
 * the status screen and the map, the GunCon's movement and quick search,
 * and running the player's update on the scratchpad stack.
 *
 *   Start (or a full push of the right stick on a DualShock) asks for the
 *   status screen (D_800B3D38 = 1, player control off); Select asks for
 *   the map (D_800B3D20). With a GunCon (buttons D_80062DC4, last frame
 *   D_80062DA0: 0x20 trigger, 0x40 B, 0x800 A): the trigger pulled with
 *   the gun pointing off screen walks (held 20 frames: runs; twice within
 *   20 frames: steps back), A / B start a quick search to the right/left
 *   (pressed again within 16 frames: a new target), A+B held asks for the
 *   status screen, A+B+trigger for 10 frames the map.
 *
 * Neither works while 0x800AFCB0 or 0x800A55A2 has bit 2 set.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"

#define g_menuInputOff   GV(u32, 0x800BBB60)
#define g_inputLocked    GV(s16, 0x800B3D2C)
#define g_inputAxesPrev  GA(u8, 0x800B3DF4)
#define g_statusRequest  GV(s32, 0x800B3D38)
#define g_mapRequest     GV(s32, 0x800B3D20)
#define g_gunButtons     GV(u32, 0x80062DC4)
#define g_gunButtonsPrev GV(u32, 0x80062DA0)
#define g_gunWalkFrames  GV(s32, 0x800B5FBC)
#define g_gunBackWindow  GV(s32, 0x800B3D18)
#define g_gunRightGap    GV(s32, 0x800B4BA4)
#define g_gunLeftGap     GV(s32, 0x800B4BA8)
#define g_gunABFrames    GV(s32, 0x800A8838)
#define g_gunMapFrames   GV(s32, 0x800B5FC0)
#define g_qsState        GV(s32, 0x800A85B8)
#define g_qsTarget       GV(u32, 0x800A8670)
#define g_menusOff       (D_800AFCB0 & 4)
#define g_eventRunning   (GV(u16, 0x800A55A2) & 4)
#define D_80062E2C       GV(u8, 0x80062E2C)

#define BTN_SELECT 0x100
#define BTN_START  0x800
#define GUN_TRIGGER 0x20
#define GUN_B       0x40
#define GUN_A       0x800

#define F_PLAYER_UPDATE 0x800785C0u

/* 80078580: player_Update, with the stack in the scratchpad. The original
 * keeps the old stack pointer in s0 while player_Update runs (player_Update
 * saves s0 in its own frame, so the value is visible to the game). */
void n_player_UpdateOnScratchpad(void) {
    FRAME(0x18);
    u32 s0 = R[16];
    R[16] = sp;                             /* the old stack, kept in s0 */
    R[29] = 0x1F800400;
    GCALL0(F_PLAYER_UPDATE);
    R[29] = sp;
    R[16] = s0;
    V0 = 0x1F800400;                        /* the stack pointer the switch back returned */
}

static s32 stick_off_centre(u8 v) { s32 d = v - 0x80; return (d < 0 ? -d : d) >= 0x61; }

/* s0/s1: what the original holds in them during the sound call (the
 * callee saves them where the game can see them); 0 = left unchanged. */
static void open_status(u32 s0, u32 s1) {
    u32 o0 = R[16], o1 = R[17];
    if (s0) R[16] = s0;
    if (s1) R[17] = s1;
    GCALL4(F_SOUND_CUE, 0, 2, 0x7F, 0x7F);
    R[16] = o0; R[17] = o1;
    D_80062DEC = 0;
    g_statusRequest = 1;
}

/* Select: the map. */
static void check_select(void) {
    if (g_mapRequest != 0) return;
    if (!(g_buttons & BTN_SELECT) || (g_buttonsPrev & BTN_SELECT)) return;
    if (g_menusOff || g_eventRunning) return;
    g_mapRequest = 1;
}

static void gun_quick_search(u32 button, s32 dir, u32 gap_addr) {
    s32 *gap = (s32 *)(g_ram + (gap_addr & 0x1FFFFF));
    if ((g_gunButtons & button) && !(g_gunButtonsPrev & button)) {
        if (g_qsState == dir) g_qsState = 0;
        if (*gap == 0) *gap = 0x10;
        else { g_qsTarget = 0; g_qsState |= 0x100; }
    }
    if (*gap != 0) (*gap)--;
}

static void guncon(void) {
    if (!D_80062DEC) {
        g_gunWalkFrames = 0;
        g_gunBackWindow = 0;
        g_gunRightGap = 0;
        g_gunLeftGap = 0;
        g_playerMoveSpeed = -0x50;
        return;
    }
    /* walking */
    if ((g_gunButtons & GUN_TRIGGER) && !(g_gunButtonsPrev & GUN_TRIGGER) && g_reticleX <= 0 && g_reticleY <= 0) {
        if (g_gunWalkFrames >= 0x14) g_playerMoveSpeed = -0x96;
        if (g_gunBackWindow == 0) g_gunBackWindow = 0x14;
        else g_playerMoveSpeed = 0x3C;
        g_gunWalkFrames = 0;
    } else if (g_gunButtons & GUN_TRIGGER) {
        if (g_playerMoveSpeed != -0x50) g_gunWalkFrames = 0;
        else if (g_gunWalkFrames < 0x1E) g_gunWalkFrames++;
    } else {
        if (g_gunWalkFrames != 0) g_gunWalkFrames--;
        if (g_gunBackWindow == 0 && g_playerMoveSpeed != -0x50) g_playerMoveSpeed = -0x50;
    }
    if (g_gunBackWindow != 0) g_gunBackWindow--;

    gun_quick_search(GUN_A, 1, 0x800B4BA4);
    gun_quick_search(GUN_B, 2, 0x800B4BA8);

    if (g_inputLocked != 0 || D_80062E2C != 0) return;
    if (g_statusRequest == 0) {
        if ((g_gunButtons & GUN_A) || (g_gunButtons & GUN_B)) g_gunABFrames++;
        else g_gunABFrames = 0;
        u32 b = g_gunButtons;
        if ((b & (GUN_A | GUN_B)) == (GUN_A | GUN_B) &&
            (g_buttonsPrev & (GUN_A | GUN_B)) != (GUN_A | GUN_B) && g_gunABFrames < 4 &&
            !(b & GUN_TRIGGER) && !g_menusOff && !g_eventRunning) {
            g_buttons = (s32)b;
            GCALL4(F_SOUND_CUE, 0, 2, 0x7F, 0x7F);
            g_statusRequest = 1;
            D_80062DEC = 0;
        }
    }
    if (g_mapRequest != 0) return;
    if ((g_gunButtons & (GUN_A | GUN_B | GUN_TRIGGER)) == (GUN_A | GUN_B | GUN_TRIGGER)) g_gunMapFrames++;
    else g_gunMapFrames = 0;
    if (g_gunMapFrames < 0xA || g_menusOff || g_eventRunning) return;
    g_mapRequest = 1;
    g_statusRequest = 0;
}

/* 80079D90: once a frame in the field: menu buttons per controller type. */
void n_field_MenuButtons(void) {
    FRAME(0x20);
    /* The original's register saves. This runs from a per-frame callback
     * (0x8006F1E4, installed through 0x80011558 at 0x80072F38) in the
     * interrupt handler, on its stack in main RAM (0x8005Bxxx): the game
     * can see that stack (the equivalence test compares it), so the frame
     * must hold what the original's does. */
    GS(u32, sp + 0x18) = R[31];
    GS(u32, sp + 0x14) = R[17];
    GS(u32, sp + 0x10) = R[16];
    if (g_menuInputOff != 0) return;
    s32 pad = (s32)g_padTypeBit;
    if (pad == 0x200000) { guncon(); return; }
    if (pad != 0x400000 && pad != 0x80000) return;
    if (!D_80062DEC || g_inputLocked != 0) return;
    if (g_statusRequest == 0) {
        s32 open = 0;
        if ((g_buttons & BTN_START) && !(g_buttonsPrev & BTN_START)) open = 1;
        else if (pad == 0x400000) {
            /* right stick pushed fully, from centred */
            s32 was_centred = !stick_off_centre(g_inputAxesPrev[3]) && !stick_off_centre(g_inputAxesPrev[2]);
            if ((stick_off_centre(g_inputAxes[3]) || stick_off_centre(g_inputAxes[2])) && was_centred) open = 1;
        }
        /* the digital pad's path keeps two addresses in s0/s1 */
        if (open && !g_menusOff && !g_eventRunning)
            open_status(pad == 0x80000 ? 0x800B0000u : 0, pad == 0x80000 ? 0x80060000u : 0);
    }
    check_select();
}
