/* Camera and movement helpers called from player_Update (TEST overlay):
 * levelling the view, the weapon's recoil kick, the floor height under the
 * player, and stepping up or down stairs. Decompiled from the game's
 * code; they work on its RAM exactly as the originals did. */
#include "game_vars.h"

void n_camera_LevelPitch(void);
void n_camera_RecoilStart(void);
void n_camera_Recoil(void);
void n_player_SavePrevPos(void);
void n_player_UpdateFloor(void);
void n_player_StepUpDown(void);

#define CAMERA_PITCH_U   GV(u16, CAMERA_OBJ + 0x44)   /* = D_800A865C */
#define D_800B4E08_ADDR  0x800B4E08u                  /* last position: x, z */
#define D_800B4E08       GA(s32, D_800B4E08_ADDR)
#define D_800B4E10       GV(s32, 0x800B4E10)          /* stair: target height */
#define D_800B4E14       GV(s32, 0x800B4E14)          /* stair: height per frame */
#define D_800BB194       GV(s32, 0x800BB194)          /* floor height */
#define D_800B60C8       0x800B60C8u                  /* room's floor and stair zones */
#define D_800A1300       GA(u16, 0x800A1300)          /* recoil per weapon: kick, remaining */
#define D_800A1324       GV(s16, 0x800A1324)          /* recoil phase */
#define D_800A1326       GV(s16, 0x800A1326)          /* recoil frame counter */
#define D_800A1328       GV(s16, 0x800A1328)          /* recoil: weapon */
#define F_ZONE_CROSSED   0x8007C0D4u                  /* (from, to, a, b): moved across edge a-b */
#define F_ZONE_SIDE      0x8007C084u                  /* (a, b, p): which side of a-b p is on */

static s32 half(s32 v) { return (v + (v < 0)) >> 1; }  /* v / 2, toward zero */

/* 800789F8: ease the camera pitch 16 units toward level; at level it
 * marks the view levelled. */
void n_camera_LevelPitch(void) {
    s16 p = D_800A865C;
    D_800A12E8 = 0;
    s16 n = (s16)(p > 0 ? p - 16 : p + 16);
    D_800A865C = n;
    if (p > 0 ? n >= 0 : n <= 0) return;
    D_800A12E8 = 1;
    D_800A865C = 0;
    D_800B4DF0 = 0;
}

/* 80078414: a shot was fired: start the current weapon's recoil. */
void n_camera_RecoilStart(void) {
    u16 *w = gp(g_currentWeaponAddr);
    D_800A1324 = 0;
    D_800A1326 = 0;
    u16 *e = &D_800A1300[(s16)w[0] * 2];
    D_800A1328 = (s16)w[0];
    e[1] = (u16)(e[0] - e[1]);
}

/* 80078460: recoil, every frame: kick the view up (half the amount for
 * two frames), then let it settle back down at up to 10 units a frame. */
void n_camera_Recoil(void) {
    u16 *e = &D_800A1300[D_800A1328 * 2];
    switch (D_800A1324) {
    case 0:
        CAMERA_PITCH_U = (u16)(CAMERA_PITCH_U - half((s16)e[1]));
        if (D_800A1326 <= 0) {
            D_800A1326 = (s16)(D_800A1326 + 1);
            return;
        }
        D_800A1324 = (s16)(D_800A1324 + 1);
        e[1] = (u16)(half((s16)e[0]) & 0xFFFE);
        return;
    case 1: {
        s16 left = (s16)e[1];
        if (left == 0) { D_800A1324 = (s16)(D_800A1324 + 1); return; }
        s32 step = left < 11 ? 1 : 10;
        CAMERA_PITCH_U = (u16)(CAMERA_PITCH_U + step);
        u16 rest = (u16)(e[1] - step);
        e[1] = rest;
        if ((s16)rest >= 0) return;
        CAMERA_PITCH_U = (u16)(CAMERA_PITCH_U + rest);      /* overshot: take it back */
        e[1] = 0;
        return;
    }
    }
}

/* 80077F68: remember where the player stood (x, z) before moving. */
void n_player_SavePrevPos(void) {
    D_800B4E08[0] = PLAYER_POS[0];
    D_800B4E08[1] = PLAYER_POS[2];
}

/* For each zone edge the step from the last position to now crossed,
 * found(edge, side) is called; side >= 0 is the front. Zones are 20 bytes:
 * two points (8 bytes each), then the height in front and behind. If the
 * player didn't move nothing is checked. Returns 1 if stop was set. */
typedef int (*ZoneFn)(u8 *zone, s32 side);
static void scan_zones(u32 zones, s32 count, ZoneFn found) {
    GameStack gs;
    u32 pos = gstack_alloc(&gs, 8);
    s32 *p = gp(pos);
    p[0] = PLAYER_POS[0];
    p[1] = PLAYER_POS[2];
    u32 z = zones;
    for (s32 i = 0; i < count; i++) {
        if (D_800B4E08[0] == p[0] && D_800B4E08[1] == p[1]) continue;
        if (GCALL4(F_ZONE_CROSSED, D_800B4E08_ADDR, pos, z, z + 8)) {
            s32 side = (s32)GCALL3(F_ZONE_SIDE, z, z + 8, pos);
            if (found(gp(z), side)) i = count;
        }
        z += 0x14;
    }
    gstack_free(&gs);
}

static int set_floor(u8 *z, s32 side) {
    D_800BB194 = side >= 0 ? *(s16 *)(z + 0x10) : *(s16 *)(z + 0x12);
    return 0;
}

/* 800780E4: the floor height changes where the player crossed into another
 * floor zone. */
void n_player_UpdateFloor(void) {
    scan_zones(GV(u32, D_800B60C8 + 0x14), GV(u8, D_800B60C8 + 0x10), set_floor);
}

static int start_stairs(u8 *z, s32 side) {
    s16 a = *(s16 *)(z + 0x10), b = *(s16 *)(z + 0x12);
    D_800B6084 = side >= 0 ? 2 : 1;
    D_800B4E10 = side >= 0 ? a : b;
    s32 d = (a < 0 ? -a : a) - (b < 0 ? -b : b);
    D_800B4E14 = (d < 0 ? d + 3 : d) >> 2;
    return 1;
}

/* 80078208: crossing a stair edge moves the player's height to the other
 * level over a few frames (1 = down, 2 = up the y axis), then plays a step. */
void n_player_StepUpDown(void) {
    if (D_800B6084 == 0)
        scan_zones(GV(u32, D_800B60C8 + 0x1C), GV(u8, D_800B60C8 + 0x18), start_stairs);
    s32 *y = &PLAYER_POS[1];
    s32 target = D_800B4E10;
    if (D_800B6084 == 1) {
        *y += D_800B4E14;
        if (*y < target) return;
    } else if (D_800B6084 == 2) {
        *y -= D_800B4E14;
        if (target < *y) return;
    } else {
        return;
    }
    *y = target;
    D_800B6084 = 0;
    D_800B4DE8 = 0;
    GCALL0(F_STEP_SOUND);
}
