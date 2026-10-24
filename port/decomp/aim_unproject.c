/* Aiming (TEST overlay): firing at the reticle, and finding the point in
 * the world behind a spot on the screen.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "libgs.h"

void n_fire_AtScreen(void);
void n_fov_Unproject(void);

#define F_FIRE_SHOT       0x800731BCu   /* (a, b, c, d): the weapon's shot */
#define F_ROT_TRANS_PERS  0x8002A928u   /* (v, sxy, p, flag) */
#define F_ROT_TRANS_PERS3 0x8002A988u   /* (v0, v1, v2, sxy0, [sxy1, sxy2, p, flag]) */
#define F_ROT_MATRIX_YXZ  0x8002AE98u   /* (rot, m) */
#define F_APPLY_MATRIX    0x8002A5B8u
#define F_OBJ_MATRIX2     0x80082760u   /* (coord, scale) */
#define CAM               0x800A8618u   /* the camera object: pos +0x38, rot +0x44 */
#define g_camYaw          GV(u16, 0x800A8606)

extern int g_fov_hold;                   /* gte.c */

/* 8008DF08: fire the held weapon at the reticle (unless firing is held
 * off, 0x800AFCB0 bit 3), with the weapon's shot values (0x800A3D20,
 * 0x800A3D44). */
void n_fire_AtScreen(void) {
    FRAME(0x18);
    if (D_800AFCB0 & 8) return;
    s32 w = GV(s16, g_currentWeaponAddr);
    u32 e = (u32)w * 4;
    GCALL4(F_FIRE_SHOT, GV(u8, 0x800A3D20u + (e | 2)), GV(u8, 0x800A3D44u + (e | 2)),
           GV(u16, 0x800A3D20u + e), GV(u16, 0x800A3D44u + e));
}

/* The camera's rotation (pitch, yaw + the player's facing, roll) as a
 * matrix at m (scratch vector at v). */
static void camera_matrix(u32 v, u32 m) {
    copy_words(m, IDENTITY_MATRIX, 8);
    GS(u16, v) = GV(u16, CAM + 0x44);
    GS(u16, v + 2) = (u16)(g_camYaw + GV(u16, CAM + 0x46));
    GS(u16, v + 4) = GV(u16, CAM + 0x48);
    GCALL2(F_ROT_MATRIX_YXZ, v, m);
}

/* out = the camera's position + m * the vector at v (copied to v + 8). */
static void apply_from_camera(u32 sp, u32 out) {
    GS(u32, sp + 0x48) = GS(u32, sp + 0x40);
    GS(u32, sp + 0x4C) = GS(u32, sp + 0x44);
    GCALL3(F_APPLY_MATRIX, sp + 0x20, sp + 0x40, sp + 0x50);
    GS(s32, out) = GV(s32, CAM + 0x38) + GS(s32, sp + 0x50);
    GS(s32, out + 4) = GV(s32, CAM + 0x3C) + GS(s32, sp + 0x54);
    GS(s32, out + 8) = GV(s32, CAM + 0x40) + GS(s32, sp + 0x58);
}

static void unproject(void) {
    FRAME(0x100);
    s32 dx = (s32)A0, dy = (s32)A1, depth = (s32)A2;
    u32 out = A3;
    GS(u32, sp + 0x100) = A0;
    GS(u32, sp + 0x104) = A1;
    GS(u32, sp + 0x108) = A2;
    u32 refine = GS(u32, sp + 0x110);              /* argument 5 */
    /* the screen scale: where (1000, 1000, depth) projects */
    copy_words(sp + 0x20, IDENTITY_MATRIX, 8);
    GCALL1(F_SET_ROT_MATRIX, sp + 0x20);
    GCALL1(F_SET_TRANS_MATRIX, sp + 0x20);
    GS(u16, sp + 0x40) = 0x3E8;
    GS(u16, sp + 0x42) = 0x3E8;
    GS(u16, sp + 0x44) = (u16)depth;
    GCALL4(F_ROT_TRANS_PERS, sp + 0x40, sp + 0x60, sp + 0xC8, sp + 0xC8);
    u16 sx = GS(u16, sp + 0x60), sy = GS(u16, sp + 0x62);
    GS(u16, sp + 0xD0) = sx;
    camera_matrix(sp + 0x40, sp + 0x20);
    if (dx == 0) GS(s16, sp + 0x40) = -1;
    else GS(s16, sp + 0x40) = (s16)mips_div((s32)((u32)-dx * 1000u), (s16)GS(u16, sp + 0xD0));
    GS(s16, sp + 0x44) = (s16)-depth;
    GS(s16, sp + 0x42) = (s16)mips_div((s32)((u32)dy * 1000u), (s16)sy);
    apply_from_camera(sp, out);
    if (refine == 0) return;
    /* correct it: project the point found and move by the error */
    s32 passes = GV(s16, CAM + 0x46) ? 8 : 4;
    for (s32 i = 0; i < passes; i++) {
        GS(u32, sp + 0x80) = GS(u32, out);
        GS(u32, sp + 0x84) = GS(u32, out + 4);
        GS(u32, sp + 0x88) = GS(u32, out + 8);
        GS(u16, sp + 0xB0) = 0;
        GS(u16, sp + 0xAE) = 0;
        GS(u16, sp + 0xAC) = 0;
        GS(u32, sp + 0xB4) = 0;
        GCALL2(F_OBJ_MATRIX2, sp + 0x68, 0);
        GCALL2(F_GS_GET_LS, sp + 0x68, sp + 0x20);
        GCALL1(F_GS_SET_LS_MATRIX, sp + 0x20);
        GS(u16, sp + 0x44) = 0;
        GS(u16, sp + 0x42) = 0;
        GS(u16, sp + 0x40) = 0;
        GS(u16, sp + 0xBC) = 0;
        GS(u16, sp + 0xBA) = 0;
        GS(u16, sp + 0xB8) = 0;
        GS(u32, sp + 0x10) = sp + 0xC0;
        GS(u32, sp + 0x14) = sp + 0xC0;
        GS(u32, sp + 0x18) = sp + 0xC8;
        GS(u32, sp + 0x1C) = sp + 0xC8;
        GCALL4(F_ROT_TRANS_PERS3, sp + 0x40, sp + 0xB8, sp + 0xB8, sp + 0x60);
        camera_matrix(sp + 0x40, sp + 0x20);
        s32 ex = (s32)((u32)((s16)GS(u16, sp + 0x60) - GS(s32, sp + 0x100)) * 1000u);
        u16 nx = (u16)(GS(u16, sp + 0x48) + mips_div(ex, (s16)GS(u16, sp + 0xD0) * 2));
        GS(u16, sp + 0x40) = nx;
        if (nx == 0) GS(s16, sp + 0x40) = -1;
        s32 ey = (s32)((u32)(GS(s32, sp + 0x104) - ((s16)GS(u16, sp + 0x62) + 0x3C)) * 1000u);
        s32 qy = mips_div(ey, (s16)sy * 2);
        GS(s16, sp + 0x44) = (s16)-GS(s32, sp + 0x108);
        GS(u16, sp + 0x42) = (u16)(GS(u16, sp + 0x4A) + qy);
        apply_from_camera(sp, out);
    }
}

/* 8009B71C (dx, dy, depth, out, refine): the point at that depth behind a
 * spot on the screen (dx, dy from the aiming centre), where a shot's
 * flash and impact go; refine: correct it by projecting it back (4
 * passes, 8 with the camera turned). It finds the screen scale by
 * projecting a test vector, so with the port's field of view setting it
 * would aim along a different line than the shot: it runs with the
 * game's own projection. */
void n_fov_Unproject(void) {
    g_fov_hold++;
    unproject();
    g_fov_hold--;
}
