/* Shooting (TEST overlay): which enemy body part (or breakable object)
 * a shot at the reticle hits, and the enemies' shadows and hit parts.
 *
 * Each enemy has up to N hit parts, 0x5C-byte records at +0x38C (part k
 * at +0x38C + 0x5C*k, the list ended by +6 = -1):
 *   +0x00..+0x0C  box corners (s16 x0,y0,z0 at 0/2/4, x1,y1,z1 at 8/A/C)
 *   +0x06         part id (-1 ends the list)
 *   +0x0E         body-part zone (<0: can't be hit; 2 = head)
 *   +0x10         matrix (translation at +0x24/+0x28/+0x2C)
 *   +0x30         the bone coordinate it follows
 *   +0x34..+0x53  the 8 corners projected to the screen
 *   +0x54/+0x58   their bounding rectangle (min x,y / max x,y)
 * Breakable objects (0x800BBC68, four 0x18-byte records; their models at
 * 0x800C09D8, 0x44 bytes each) are tested with a box built on the stack
 * the same way.
 *
 * A shot (shot_Update -> shot_Resolve, every frame, with the stack in
 * the scratchpad) lists the targets in reach, nearest first, and for each
 * one tests its parts' screen boxes against the reticle (with a margin by
 * weapon, 0x800A3C2C) and the line of sight, then records the hit
 * (enemy_AddHit). Piercing weapons (stat 1) go on to the next target.
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). */
#include "libgs.h"

void n_enemy_InitHitParts(void);
void n_enemy_InitPartsOne(void);
void n_shot_Resolve(void);
void n_shot_Update(void);
void n_shot_Blood(void);
void n_hitbox_Project(void);
void n_hitbox_ReticleTest(void);
void n_hitbox_Bounds(void);
void n_shadow_Init(void);
void n_shadow_Add(void);
void n_object_InView(void);

#define F_WEAPON_STAT     0x8008DF80u   /* weapon_Stat(k) */
#define F_RECOIL_START    0x80078414u
#define F_LINE_OF_SIGHT   0x800822C4u   /* (from, to): >= 0 blocked */
#define F_ENEMY_STATE     0x800829D8u
#define F_ENEMY_ADD_HIT   0x800710FCu
#define F_OBJECT_TARGETABLE 0x800711A8u
#define F_SHOT_EFFECT     0x80098284u   /* (x, y, hit, 30) */
#define F_UNPROJECT       0x8009B71Cu
#define F_SPAWN_EFFECT    0x80099068u
#define F_HIT_PAUSE       0x8006F5D8u

#define OBJECTS           0x800BBC68u   /* breakable objects */
#define SHOTS_FIRED       GV(u32, 0x80063448)
#define SHOTS_HIT         GV(u32, 0x8006344C)
#define EYE_POS           0x800A85D8u   /* where shots come from */
#define PART(e, k)        ((e) + (u32)(k) * 0x5C)

static u32 weapon_stat(u32 k) { return GCALL1(F_WEAPON_STAT, k); }

/* An enemy's hit parts from its type's part table (0x800BBB80[+0x84E]:
 * 0x14-byte entries, bone index < 0 ends); the end marker's part. */
static u32 init_hit_parts(u32 e) {
    u32 def = GV(u32, 0x800BBB80u + (u32)((s32)GV(s16, e + 0x84E) * 4));
    u32 k = 0;
    for (; GV(s32, def) >= 0; k++, def += 0x14) {
        u32 p = PART(e, k);
        copy_words(p + 0x39C, IDENTITY_MATRIX, 8);
        GV(u32, p + 0x3BC) = GV(u32, e + 0x16C) + (u32)GV(s32, def) * 0x50;   /* bone */
        GV(u16, p + 0x38C) = GV(u16, def + 4);
        GV(u16, p + 0x38E) = GV(u16, def + 6);
        GV(u16, p + 0x390) = GV(u16, def + 8);
        GV(u16, p + 0x394) = GV(u16, def + 0xC);
        GV(u16, p + 0x396) = GV(u16, def + 0xE);
        GV(u16, p + 0x398) = GV(u16, def + 0x10);
        if (GV(s16, def + 0xA) != 0) {
            GV(u8, e + 0x2B) |= 4;
            GV(u16, p + 0x392) = GV(u16, def + 0xA);
        } else {
            GV(u16, p + 0x392) = 0;
        }
        u16 zone = GV(u16, def + 0x12);
        if (zone & 0x8000) {                           /* the enemy's main bone */
            GV(u32, e + 0x388) = GV(u32, p + 0x3BC);
            GV(u16, p + 0x39A) = zone & 0x7FFF;
        } else {
            GV(u16, p + 0x39A) = zone;
        }
    }
    GV(s16, PART(e, k) + 0x392) = -1;
    return PART(e, k);
}

/* 8006F914: set up every active enemy's hit parts (init_hit_parts), and
 * reset the reticle (red quad 0x800B3E28, centred). */
void n_enemy_InitHitParts(void) {
    FRAME(0x20);
    u32 e = ENEMIES;
    GCALL1(F_SET_POLY_F4, 0x800B3E28u);
    GV(u8, 0x800B3E2C) = 0xFF;
    GV(u8, 0x800B3E2D) = 0;
    GV(u8, 0x800B3E2E) = 0;
    for (s32 i = 0; i < ENEMY_COUNT; i++, e += ENEMY_SIZE) {
        if (GV(u8, e + 0x315) >= 0x32) continue;          /* not a hittable type */
        if (GV(u8, e + 0x2A) != 1) continue;
        init_hit_parts(e);
    }
    g_reticleX = 0xA0;
    g_reticleY = 0x78;
}

/* 8006FB5C (record): set up one enemy's hit parts, if it is active (an
 * enemy brought in later, e.g. the final boss's second form). */
void n_enemy_InitPartsOne(void) {
    u32 e = A0;
    if (GV(u8, e + 0x2A) != 1) { V0 = 0x800C0000u; return; }
    V0 = init_hit_parts(e);
}

/* A target's distance: enemy index, or object index | 0x8000. */
static s32 target_dist(u32 t, u32 enemies) {
    if (t & 0x8000) return GV(s32, OBJECTS + (t & 0x7FFF) * 0x18 + 4);
    return GV(s32, enemies + t * ENEMY_SIZE + 0x868);
}

/* 8006FCF8: resolve this frame's shot (g_fireRequest). */
void n_shot_Resolve(void) {
    FRAME(0x100);
    u32 fp = sp;
    GS(u32, fp + 0xD0) = 0;                                /* hit pause done */
    /* the target list, on the stack below the frame */
    u32 sz = (((((u32)(ENEMY_COUNT + 5) << 5) >> 3) + 7) >> 3) << 3;
    u32 enemies = ENEMIES;
    R[29] -= sz;
    u32 list = R[29] + 0x10;
    if (!g_fireRequest) goto out;
    {
        u32 rate = weapon_stat(2);
        g_fireCooldown = (u16)((g_fireCooldown & 0x8000) | rate);
    }
    if (g_reticleX < 0 || g_reticleY < 0) goto out;
    {
        u32 w = g_currentWeaponAddr;
        if (GV(s16, w + 2) <= 0) {                          /* empty */
            D_800AFCB0 |= 2;
            goto out;
        }
        u16 left = (u16)(GV(u16, w + 2) - 1);
        GV(u16, w + 2) = left;
        if (left == 0) g_fireCooldown = 0x8000;
        D_800AFCB0 |= 1;
    }
    if (SHOTS_FIRED < 0xFFFFFFFFu) SHOTS_FIRED++;
    GCALL0(F_RECOIL_START);
    if (GCALL0(0x800981E0u)) goto out;

    /* enemies in reach */
    u32 n = 0;
    for (s32 i = 0; i < ENEMY_COUNT; i++) {
        u32 e = enemies + (u32)i * ENEMY_SIZE;
        if (GV(u8, e + 0x29) == 0 || GV(u8, e + 0x315) >= 0x32) continue;
        u32 f = GV(u32, e + 0x320);
        if ((f & 2) || !(f & 0x40) || (GV(u8, e + 0x2B) & 1)) continue;
        if (weapon_stat(1) && weapon_stat(0) < GV(u32, e + 0x868)) continue;   /* out of range */
        if (GV(u8, e + 0x2A) != 1) continue;
        GS(u32, list + n * 4) = (u32)i;
        n++;
    }
    /* breakable objects */
    for (u32 k = 0; k < 4; k++) {
        u32 o = OBJECTS + k * 0x18;
        if (GV(s32, o) < 0) break;
        if (GV(s16, o + 0xE) >= 0 && GCALL1(F_OBJECT_TARGETABLE, k)) {
            GS(u32, list + n * 4) = k | 0x8000;
            n++;
        }
    }
    enemies = ENEMIES;
    GS(s32, list + n * 4) = -1;
    /* nearest first */
    for (u32 i = 0; n >= 2 && (s32)n >= 2; i++, n--) {
        for (u32 j = i + 1; j < i + n; j++) {
            u32 a = GS(u32, list + i * 4), b = GS(u32, list + j * 4);
            if (target_dist(b, enemies) < target_dist(a, enemies)) {
                GS(u32, list + i * 4) = b;
                GS(u32, list + j * 4) = a;
            }
        }
    }

    for (u32 t = list; GS(s32, t) != -1; t += 4) {
        u32 target = GS(u32, t);
        u32 idx = (u32)((t - list) >> 2);
        if (target & 0x8000) {
            /* an object: a coordinate (fp+0x20) and a hit box (fp+0x70) */
            u32 rec = OBJECTS + (target & 0x7FFF) * 0x18;
            u32 obj = 0x800C09D8u + (u32)GV(s32, rec) * 0x44;
            GV(u32, 0x800C39A8) = obj;
            copy_words(fp + 0x80, IDENTITY_MATRIX, 8);
            copy_words(fp + 0x24, IDENTITY_MATRIX, 8);
            GS(s32, fp + 0x38) = (s32)GV(s16, obj + 0x10) << 2;
            GS(s32, fp + 0x3C) = GV(s16, obj + 0x12);
            GS(s32, fp + 0x40) = (s32)GV(s16, obj + 0x18) << 4;
            GS(u16, fp + 0x64) = GV(u16, obj + 0x24);
            GS(u16, fp + 0x66) = GV(u16, obj + 0x26);
            GS(u32, fp + 0x6C) = 0;
            GS(u32, fp + 0x20) = 0;
            GS(u32, fp + 0xA0) = fp + 0x20;
            GS(u16, fp + 0x68) = GV(u16, obj + 0x28);
            if ((s32)GCALL2(F_LINE_OF_SIGHT, EYE_POS, fp + 0x38) >= 0) continue;
            GS(u16, fp + 0x70) = GV(u16, rec + 0x08);
            GS(u16, fp + 0x72) = GV(u16, rec + 0x0A);
            GS(u16, fp + 0x74) = GV(u16, rec + 0x0C);
            GS(u16, fp + 0x78) = GV(u16, rec + 0x10);
            GS(u16, fp + 0x7A) = GV(u16, rec + 0x12);
            GS(u16, fp + 0x7C) = GV(u16, rec + 0x14);
            GCALL3(0x80070930u, fp + 0x70, 0, 0);
            if (GCALL1(0x80070CF4u, fp + 0x70) == 0) continue;
            obj = GV(u32, 0x800C39A8);
            GV(u16, obj + 0x1C) |= 0x4000;                  /* hit */
            if (weapon_stat(1) == 0) goto shot_done;
            continue;
        }
        /* an enemy: its parts */
        u32 e = enemies + target * ENEMY_SIZE;
        if (GV(s16, e + 0x392) < 0) continue;
        for (u32 k = 0; GV(s16, PART(enemies + GS(u32, t) * ENEMY_SIZE, k) + 0x392) >= 0; k++) {
            e = enemies + GS(u32, t) * ENEMY_SIZE;
            u32 off = k * 0x5C;
            if (GV(s16, e + off + 0x39A) < 0) continue;
            GCALL3(0x80070930u, e + off + 0x38C, k, idx);
            u32 hit = GCALL1(0x80070CF4u, enemies + GS(u32, t) * ENEMY_SIZE + off + 0x38C);
            if (hit == 0) continue;
            e = enemies + GS(u32, t) * ENEMY_SIZE;
            GS(u32, fp + 0x10) = GV(u32, GV(u32, e + off + 0x3BC) + 0x38);
            GS(u32, fp + 0x14) = GV(u32, GV(u32, enemies + GS(u32, t) * ENEMY_SIZE + off + 0x3BC) + 0x3C);
            GS(u32, fp + 0x18) = GV(u32, GV(u32, enemies + GS(u32, t) * ENEMY_SIZE + off + 0x3BC) + 0x40);
            if ((s32)GCALL2(F_LINE_OF_SIGHT, EYE_POS, fp + 0x10) >= 0) continue;
            if (GCALL2(F_ENEMY_STATE, enemies + GS(u32, t) * ENEMY_SIZE, 9) & 0x2000)
                GCALL2(0x80070628u, GS(u32, t), k);          /* shot_Blood */
            if (GCALL4(F_ENEMY_ADD_HIT, enemies + GS(u32, t) * ENEMY_SIZE, 5, k, hit) == 0) continue;
            if (SHOTS_HIT < 0xFFFFFFFFu && GS(u32, fp + 0xD0) == 0) {
                GCALL0(F_HIT_PAUSE);
                GS(u32, fp + 0xD0) = 1;
                SHOTS_HIT += 1;
            }
            if (weapon_stat(1) == 0) goto shot_done;
        }
    }
shot_done:
    GCALL4(F_SHOT_EFFECT, (u16)g_reticleX, (u16)g_reticleY, GS(u32, fp + 0xD0), 0x1E);
out:
    R[29] = fp;
}

/* 800705D8: every frame: resolve the shot with the stack in the
 * scratchpad (unless the game has the input locked). */
void n_shot_Update(void) {
    FRAME(0x18);
    if (g_inputLocked == 0) {
        u32 saved = GCALL0(F_GET_SP);
        GCALL1(F_SET_SP, SCRATCH_STACK);
        n_shot_Resolve();
        GCALL1(F_SET_SP, saved);
    }
}

/* 80070628 (enemy, part): the blood spurt where a shot hit: for enemy
 * type 0x12 (first enemy's type) the point on the part under the
 * reticle, else a point in front of the eye at the enemy's distance. */
void n_shot_Blood(void) {
    u32 idx = A0, part = A1;
    FRAME(0xD0);
    u32 base = ENEMIES;
    u32 e = base + idx * ENEMY_SIZE;
    if (GV(u8, base + 0x315) == 0x12) {
        u32 p = e + part * 0x5C + 0x38C;
        s32 a = GV(s16, p) + GV(s16, p + 4);
        s32 cx = (a + (s32)((u32)a >> 31)) >> 1;
        GS(u16, sp + 0xA0) = (u16)cx;
        s32 b = GV(s16, p + 8) + GV(s16, p + 0xC);
        s32 cx1 = (b + (s32)((u32)b >> 31)) >> 1;
        GS(u16, sp + 0xA2) = (u16)cx1;
        s32 num = (s32)((u32)(s32)(s16)(GV(u16, 0x80062DDC) - GV(u16, p + 0x54)) *
                        (u32)((s32)(s16)cx1 - (s32)(s16)cx));
        s32 q = mips_div(num, (s16)(GV(u16, p + 0x58) - GV(u16, p + 0x54)));
        u32 m = sp + 0x30;
        copy_words(m, IDENTITY_MATRIX, 8);
        GCALL2(F_ROT_MATRIX_Y, (s32)GV(s16, g_player + 0x46), m);
        u32 x = (u32)cx + (u32)q;
        GCALL1(F_SET_ROT_MATRIX, m);
        GCALL1(F_SET_TRANS_MATRIX, m);
        GS(u16, sp + 0x18) = (u16)x;
        s32 num2 = (s32)((u32)(s32)(s16)(GV(u16, 0x80062DE0) - GV(u16, p + 0x56)) *
                         (u32)((s32)GV(s16, p + 0xA) - (s32)GV(s16, p + 2)));
        s32 q2 = mips_div(num2, (s16)(GV(u16, p + 0x5A) - GV(u16, p + 0x56)));
        GS(u16, sp + 0x1C) = (u16)-0x14;
        GS(u16, sp + 0x1A) = (u16)(GV(u16, p + 2) + q2);
        GCALL3(F_ROT_TRANS, sp + 0x18, sp + 0x20, sp + 0xA8);
        u32 pa = e + part * 0x5C;
        GS(u32, sp + 0x68) = GV(u32, GV(u32, pa + 0x3BC) + 0x38) + GS(u32, sp + 0x20);
        GS(u32, sp + 0x6C) = GV(u32, GV(u32, pa + 0x3BC) + 0x3C) + GS(u32, sp + 0x24);
        GS(u32, sp + 0x70) = GV(u32, GV(u32, pa + 0x3BC) + 0x40) + GS(u32, sp + 0x28);
    } else {
        u32 d5 = GV(u32, e + 0x868) * 5;
        u32 hi = (u32)(((uint64_t)d5 * 0xAAAAAAABull) >> 32);
        s32 depth = (s32)(hi << 14) >> 16;
        GS(u32, sp + 0x10) = 1;
        GCALL4(F_UNPROJECT, (u32)(g_reticleX - 0xA0), (u32)(g_reticleY - 0x3C), (u32)depth, sp + 0x20);
        GS(u32, sp + 0x68) = GS(u32, sp + 0x20);
        GS(u32, sp + 0x6C) = GS(u32, sp + 0x24);
        GS(u32, sp + 0x70) = GS(u32, sp + 0x28);
    }
    GS(u32, sp + 0x10) = 0x1F;
    GCALL4(F_SPAWN_EFFECT, sp + 0x50, e, 0, 0);
}

/* 80070930 (part): project a hit part's box to the screen: its eight
 * corners (+0x34) and their bounding rectangle (+0x54, +0x58). Only
 * while the 3D scene is up. The part may be on a caller's stack. */
void n_hitbox_Project(void) {
    u32 p = A0;
    FRAME(0xD0);
    if (D_800A8610) {
        GCALL2(F_GS_GET_LS, GS(u32, p + 0x30), sp + 0x28);
        GCALL1(F_GS_SET_LS_MATRIX, sp + 0x28);
        u16 tx = GS(u16, p + 0x24), ty = GS(u16, p + 0x28), tz = GS(u16, p + 0x2C);
        GS(u16, sp + 0x98) = tx;
        GS(u16, sp + 0x90) = tz;
        /* corner (i, j, k) at sp + 0x48 + 8 * (4i + j + 2k): x0/x1, y0/y1, z0/z1 */
        for (u32 c = 0; c < 8; c++) {
            u32 i = c >> 2, j = c & 1, k = (c >> 1) & 1;
            u32 v = sp + 0x48 + c * 8;
            GS(u16, v) = (u16)(tx + GS(u16, p + i * 8));
            GS(u16, v + 2) = (u16)(ty + GS(u16, p + 2 + j * 8));
            GS(u16, v + 4) = (u16)(tz + GS(u16, p + 4 + k * 8));
        }
        GS(u32, sp + 0xA0) = (u32)tx + GS(u16, p);
        GS(u32, sp + 0x10) = p + 0x34;
        GS(u32, sp + 0x14) = p + 0x38;
        GS(u32, sp + 0x18) = p + 0x3C;
        GS(u32, sp + 0x1C) = p + 0x40;
        GS(u32, sp + 0x20) = sp + 0x88;
        GS(u32, sp + 0x24) = sp + 0x8C;
        GCALL4(F_ROT_TRANS_PERS4, sp + 0x48, sp + 0x50, sp + 0x60, sp + 0x58);
        GS(u32, sp + 0x10) = p + 0x44;
        GS(u32, sp + 0x14) = p + 0x48;
        GS(u32, sp + 0x18) = p + 0x4C;
        GS(u32, sp + 0x1C) = p + 0x50;
        GS(u32, sp + 0x20) = sp + 0x88;
        GS(u32, sp + 0x24) = sp + 0x8C;
        GCALL4(F_ROT_TRANS_PERS4, sp + 0x68, sp + 0x70, sp + 0x80, sp + 0x78);
        GCALL4(0x80071010u, p + 0x34, p + 0x54, 0, 8);
        GCALL4(0x80071010u, p + 0x34, p + 0x58, 1, 8);
    }
}

/* 80070CF4 (part): does the reticle hit the part's screen rectangle?
 * Off-screen rectangles miss; on-screen ones are clamped to the screen
 * (and moved to 0,0 at the top left); the reticle is tried as squares
 * of up to three sizes for the held weapon. Returns 1-3 (the size that
 * hit), 0 for a miss. */
void n_hitbox_ReticleTest(void) {
    u32 p = A0;
    FRAME(8);
    V0 = 0;
    s16 x0 = GS(s16, p + 0x54), x1 = GS(s16, p + 0x58);
    s16 y0 = GS(s16, p + 0x56), y1 = GS(s16, p + 0x5A);
    if ((x0 < -0xA0 && x1 < -0xA0) || (x0 > 0xA0 && x1 > 0xA0) ||
        (y0 < -0x78 && y1 < -0x78) || (y0 > 0x78 && y1 > 0x78))
        goto done;
    u32 sizes = GV(u32, 0x800A3C2Cu + (u32)((s32)GV(s16, g_currentWeaponAddr) * 4));
    static const struct { u32 off; u16 add, max; } clamp[4] = {
        {0x54, 0xA0, 0x13F}, {0x58, 0xA0, 0x13F}, {0x56, 0x78, 0xEF}, {0x5A, 0x78, 0xEF}};
    for (int i = 0; i < 4; i++) {
        u16 v = (u16)(GS(u16, p + clamp[i].off) + clamp[i].add);
        GS(u16, p + clamp[i].off) = v;
        if ((s16)v < 0) GS(u16, p + clamp[i].off) = 0;
        if (GS(s16, p + clamp[i].off) >= (s16)(clamp[i].max + 1)) GS(u16, p + clamp[i].off) = clamp[i].max;
    }
    u16 rx = (u16)g_reticleX, ry = (u16)g_reticleY;
    for (int i = 0; i < 3; i++) {
        s16 r = GV(s16, sizes + (u32)i * 2);
        if (r <= 0) break;
        s16 ax = (s16)(rx - r), ay = (s16)(ry - r), bx = (s16)(rx + r), by = (s16)(ry + r);
        GS(s16, sp) = ax;                              /* the square, as the original keeps it */
        GS(s16, sp + 2) = ay;
        GS(s16, sp + 4) = bx;
        GS(s16, sp + 6) = by;
        s16 minx = GS(s16, p + 0x54), maxx = GS(s16, p + 0x58);
        s16 miny = GS(s16, p + 0x56), maxy = GS(s16, p + 0x5A);
        if (ax < minx && bx < minx) continue;
        if (maxx < ax && maxx < bx) continue;
        if (ay < miny && by < miny) continue;
        if (maxy < ay && maxy < by) continue;
        V0 = (u32)(i + 1);
        break;
    }
done:
}

/* 80071010 (points, out, axis, n): out = the minimum (mode 0) or
 * maximum (mode 1) x and y of n screen points. */
void n_hitbox_Bounds(void) {
    u32 pts = A0, out = A1, mode = A2;
    s32 n = (s32)A3;
    GS(u16, out) = GS(u16, pts);
    V0 = GS(u16, pts + 2);
    GS(u16, out + 2) = (u16)V0;
    if (mode != 0 && mode != 1) { V0 = 1; return; }
    if (mode == 1) V0 = 1;
    for (s32 i = 0; i < n; i++, pts += 4) {
        for (u32 c = 0; c < 4; c += 2) {
            s16 v = GS(s16, pts + c), o = GS(s16, out + c);
            int take = mode == 0 ? v < o : o < v;
            V0 = (u32)take;
            if (take) GS(u16, out + c) = (u16)v;
        }
    }
}

/* An enemy's shadow record (0xDC bytes, from the heap 0x800B4D78): two
 * semi-transparent quads, a coordinate, and the bone it follows: the
 * root's, or (if search_head) the head part's when it has one. */
static u32 shadow_fill(u32 e, int search_head) {
    u32 s = GV(u32, 0x800B4D78);
    GV(u32, e + 0x8BC) = s;
    GV(u16, s + 0xD4) = 0;
    GV(u32, s + 0xB0) = 0;
    u32 q = s + 0x28;
    GCALL1(F_SET_POLY_FT4, s);
    GCALL1(F_SET_POLY_FT4, q);
    GCALL2(F_SET_SEMI_TRANS, s, 1);
    GCALL2(F_SET_SEMI_TRANS, q, 1);
    for (u32 j = 0; j < 2; j++) {
        u32 pq = j ? q : s;
        GV(u8, pq + 6) = 0x20;
        GV(u8, pq + 5) = 0x20;
        GV(u8, pq + 4) = 0x20;
    }
    for (u32 j = 0; j < 2; j++) {
        u32 pq = j ? q : s;
        GV(u8, pq + 0xC) = 0xE0;  GV(u8, pq + 0xD) = 0x70;
        GV(u8, pq + 0x14) = 0xF7; GV(u8, pq + 0x15) = 0x70;
        GV(u8, pq + 0x1C) = 0xE0; GV(u8, pq + 0x1D) = 0x87;
        GV(u8, pq + 0x24) = 0xF7; GV(u8, pq + 0x25) = 0x87;
    }
    GV(u16, s + 0xE) = (u16)GCALL2(F_GET_CLUT, 0x190, 0x1EB);
    GV(u16, q + 0xE) = (u16)GCALL2(F_GET_CLUT, 0x190, 0x1EB);
    GV(u16, s + 0x16) = (u16)GCALL4(F_GET_TPAGE, 0, 0, 0x278, 0x170);
    GV(u16, q + 0x16) = (u16)GCALL4(F_GET_TPAGE, 0, 0, 0x278, 0x170);
    GV(u32, s + 0x50) = 0;
    copy_words(s + 0x54, IDENTITY_MATRIX, 8);
    copy_words(s + 0x74, IDENTITY_MATRIX, 8);
    GV(u16, s + 0x94) = 0;
    GV(u16, s + 0x96) = 0;
    GV(u16, s + 0x98) = 0;
    GV(u32, s + 0x9C) = 0;
    GV(u32, s + 0xA8) = 0x1000;
    GV(u32, s + 0xA4) = 0x1000;
    GV(u32, s + 0xA0) = 0x1000;
    GV(u32, s + 0xD8) = GV(u32, e + 0x16C);              /* the root bone */
    if (search_head && GV(s16, e + 0x392) >= 0) {
        for (u32 k = 0; GV(s16, PART(e, k) + 0x392) >= 0; k++)
            if (GV(s16, PART(e, k) + 0x39A) == 2) {        /* the head */
                GV(u32, s + 0xD8) = GV(u32, PART(e, k) + 0x3BC);
                break;
            }
    }
    return s;
}

/* The shadow's size by enemy type (0x800A0EC4 / 0x800A0F90). */
static void shadow_size(u32 s, s32 type) {
    u32 size = type >= 0x32 ? 0x800A0F90u + (u32)((type - 0x32) / 10) * 8
                            : 0x800A0EC4u + (u32)(type * 6);
    u16 w = GV(u16, size), d = GV(u16, size + 2);
    GV(u16, s + 0xB4) = (u16)-w; GV(u16, s + 0xB8) = (u16)-d;
    GV(u16, s + 0xBC) = w;       GV(u16, s + 0xC0) = (u16)-d;
    GV(u16, s + 0xC4) = (u16)-w; GV(u16, s + 0xC8) = d;
    GV(u16, s + 0xCC) = w;       GV(u16, s + 0xD0) = d;
    GV(u16, s + 0xCE) = 0; GV(u16, s + 0xC6) = 0; GV(u16, s + 0xBE) = 0; GV(u16, s + 0xB6) = 0;
    GV(u16, s + 0xD6) = GV(u16, size + 4);
}

/* 80073758: per room: load the shadow texture (once) and the effect
 * textures asked for (0x800B4D80), then give each active enemy a shadow
 * record (0xDC bytes from the heap 0x800B4D78): two semi-transparent
 * quads, a coordinate, the bone it follows (the head part's, for some
 * enemy types) and its size by enemy type (0x800A0EC4 / 0x800A0F90). */
void n_shadow_Init(void) {
    FRAME(0x40);
    if (GV(u32, 0x800A0FE8) == 0) {
        GV(u32, 0x800A0FE8) = 1;
        GS(u32, sp + 0x10) = GV(u32, 0x800B4D78);
        GCALL4(F_FILE_LOAD, 0x8006C230u, 0x80058530u, 0x800A0CD8u, 0x6F);
        GCALL1(0x8006F370u, GV(u32, 0x800B4D78));         /* tim_Load */
    }
    u32 req = GV(u32, 0x800B4D80);
    if (req != 0) {
        if (req == 1) {
            GS(u32, sp + 0x10) = PACKET_AREA;
            GCALL4(F_FILE_LOAD, 0x8006C21Cu, 0x80058534u, 0x800A195Cu, 1);
        } else if (req == 2) {
            GS(u32, sp + 0x10) = PACKET_AREA;
            GCALL4(F_FILE_LOAD, 0x8006C230u, 0x80058530u, 0x800A0CD8u, 0x28);
        }
        GCALL1(0x8006F370u, PACKET_AREA);
        GV(u32, 0x800B4D80) = 0;
    }
    /* the previous enemy's type (the original keeps it in a register:
     * for the first one, whatever the caller had there) */
    s32 prev_type = (s32)R[19];
    u32 e = ENEMIES;
    for (s32 i = 0; i < ENEMY_COUNT; i++, e += ENEMY_SIZE) {
        if (GV(u8, e + 0x2A) != 1) {
            GV(u32, e + 0x8BC) = 0;
            continue;
        }
        u32 s = shadow_fill(e, prev_type < 0x32);
        prev_type = GV(s32, GV(u32, e + 0x88C));
        GV(u32, 0x800B4D78) += 0xDC;
        shadow_size(s, prev_type);
    }
    GV(u32, 0x800AB524) = 0;
}

/* 80073C5C (record): give one enemy a shadow record (as shadow_Init does,
 * for an enemy brought in later). */
void n_shadow_Add(void) {
    FRAME(0x20);
    u32 e = A0;
    u32 s = shadow_fill(e, 1);
    s32 type = GV(s32, GV(u32, e + 0x88C));
    GV(u32, 0x800B4D78) += 0xDC;
    shadow_size(s, type);
}

/* 800711A8 (k): is breakable object k (its edge: s16 x, z at +8/+0xC and
 * +0x10/+0x14 of its record) in front of the player, within the wedge of
 * two rays from the player at yaw +-0x226, 10000 units long: either ray
 * crosses the edge, or an end lies between the rays. 1 or 0. The stack
 * can be in the scratchpad (shot_Resolve's). */
#define F_SEGMENTS_CROSS_XZ 0x8007C290u
#define F_SIDE_OF_LINE      0x8007C04Cu
void n_object_InView(void) {
    FRAME(0x78);
    u32 o = OBJECTS + A0 * 0x18;
    u32 rot = sp + 0x30, r1 = sp + 0x20, r2 = sp + 0x10, e1 = sp + 0x38, e2 = sp + 0x48;
    u32 pos = PLAYER_POS_ADDR;
    GS(u16, rot) = GV(u16, PLAYER_ROT_ADDR);
    GS(u16, rot + 2) = (u16)(GV(u16, PLAYER_ROT_ADDR + 2) + 0x226);
    GS(u16, rot + 4) = GV(u16, PLAYER_ROT_ADDR + 4);
    GCALL3(F_ROT_STEP, rot, r1, (u32)-100);
    GS(u32, r1) = GV(u32, pos) + GS(u32, r1) * 100;
    GS(u32, r1 + 8) = GV(u32, pos + 8) + GS(u32, r1 + 8) * 100;
    GS(u16, rot + 2) = (u16)(GS(u16, rot + 2) - 0x44C);
    GCALL3(F_ROT_STEP, rot, r2, (u32)-100);
    GS(u32, r2) = GV(u32, pos) + GS(u32, r2) * 100;
    GS(u32, r2 + 8) = GV(u32, pos + 8) + GS(u32, r2 + 8) * 100;
    GS(s32, e1) = GV(s16, o + 8);
    GS(s32, e1 + 4) = 0;
    GS(s32, e1 + 8) = GV(s16, o + 0xC);
    GS(s32, e2) = GV(s16, o + 0x10);
    GS(s32, e2 + 4) = 0;
    GS(s32, e2 + 8) = GV(s16, o + 0x14);
    if (GCALL4(F_SEGMENTS_CROSS_XZ, pos, r1, e1, e2) || GCALL4(F_SEGMENTS_CROSS_XZ, pos, r2, e1, e2)) {
        V0 = 1;
        return;
    }
    for (u32 k = 8; k <= 0x10; k += 8) {
        u32 x = (u32)(s32)GV(s16, o + k), z = (u32)(s32)GV(s16, o + k + 4);
        if ((s32)GCALL4(F_SIDE_OF_LINE, pos, r1, x, z) < 0 && (s32)GCALL4(F_SIDE_OF_LINE, pos, r2, x, z) > 0) {
            V0 = 1;
            return;
        }
    }
    V0 = 0;
}
