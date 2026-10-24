/* Shooting things: weapon stats, where a shot hits an enemy, and how much
 * damage it does (TEST overlay).
 *
 * An enemy record keeps up to four hits per frame: count at +0x324, then
 * per hit (1-4) the body part (+0x326), the side it came from (+0x32B),
 * the damage (+0x330, s16), the shot's +0x33A value and its damage class
 * (+0x344). Its distance to the player is at +0x868, its type at +0x315.
 *
 * Damage = base (per shot kind, 0x800A3CDC) x class/type factor
 * (0x800A3D88) / 100 x body-part factor / 100, then scaled by distance:
 * the magnum keeps full damage to 2000 and falls to a quarter past 10000;
 * other guns do 1.5x point blank (< 1000) and 0.7x past 5000.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"

void n_weapon_Stat(void);
void n_weapon_ShotKind(void);
void n_weapon_Damage(void);
void n_enemy_HitSide(void);
void n_enemy_HitFactor(void);
void n_enemy_AddHit(void);

#define WEAPON_ID       GV(s16, g_currentWeaponAddr)
#define WEAPON_AMMO     GV(s16, g_currentWeaponAddr + 4)

/* 8008E430: the shot's kind: the weapon id, except for the grenade
 * launcher, where it depends on the grenades loaded (items 13-15). */
void n_weapon_ShotKind(void) {
    s16 id = WEAPON_ID;
    if (id != 8) { V0 = (u32)(s32)id; return; }
    s16 ammo = WEAPON_AMMO;
    if (ammo == 0xE) V0 = 0xA;
    else if (ammo < 0xF) V0 = ammo == 0xD ? 9 : 0xD;
    else V0 = ammo == 0xF ? 8 : 0xF;
}

/* 8008DF80 (stat): a number for the held weapon: 0 and 1 from tables at
 * 0x800A3C58 / 0x800A3C84, 2 the fire rate (0x800A3CB0, doubled; ids 13
 * and 14 depend on the controller type), 5 the shot's damage class. */
void n_weapon_Stat(void) {
    s32 k = (s32)A0;
    s16 id = WEAPON_ID;
    switch (k) {
    case 0: V0 = GV(u32, 0x800A3C58 + (u32)(s32)id * 4); return;
    case 1: V0 = GV(u32, 0x800A3C84 + (u32)(s32)id * 4); return;
    case 2: {
        u16 uid = (u16)id;
        if ((u16)(uid - 0xD) < 2) {
            u32 pad = g_padTypeBit;
            if (pad != 0x200000) {
                if (pad != 0x80000 && pad != 0x400000) { V0 = 0; return; }
                V0 = (GV(u32, 0x800A3CB0 + (u32)(s32)id * 4) << 1) + 2;
                return;
            }
        }
        V0 = GV(u32, 0x800A3CB0 + (u32)((s32)(s16)uid << 2)) << 1;
        return;
    }
    case 5:
        n_weapon_ShotKind();
        V0 = (u32)(s32)GV(s16, 0x800A3D70 + (u32)((s32)V0 << 1));
        return;
    default: V0 = 0; return;
    }
}

/* 8008216C (enemy): which of eight sides the player faces the enemy from
 * (the player's yaw against the enemy's, 0x200 = 45 degrees). */
void n_enemy_HitSide(void) {
    s32 d = ((GV(u16, 0x800A8606) & 0xFFF) - (GV(u16, A0 + 0x1C6) & 0xFFF)) & 0xFFF;
    static const u8 side[8] = {3, 2, 1, 0, 7, 6, 5, 4};
    int k = 0;
    while (k < 7 && d > 0x200 * (k + 1)) k++;
    V0 = side[k];
}

/* 80082B14 (enemy, hit): the damage factor (percent) for the body part
 * hit, from the enemy's part table (+0x840 -> +0x18): row by the part's
 * base (+0x39A of its 0x5C-byte record), column by weapon (grenades by
 * kind). */
void n_enemy_HitFactor(void) {
    u32 e = A0;
    u8 part = GV(u8, e + A1 + 0x326);
    s16 base = GV(s16, e + (u32)part * 0x5C + 0x39A);
    s16 id = WEAPON_ID;
    u32 col = A3;                                 /* unknown grenades: as left in a3 */
    if (id != 8) col = (u32)((s32)id * 10 + base);
    else {
        s16 ammo = WEAPON_AMMO;
        if (ammo == 0xE) col = (u32)(base + 0x64);
        else if (ammo == 0xD) col = (u32)(base + 0x5A);
        else if (ammo == 0xF) col = (u32)(base + 0x50);
    }
    V0 = (u32)(s32)GV(s16, GV(u32, e + 0x840) + (col << 1) + 0x18);
}

/* 8008E0E8 (enemy, hit): damage of a hit. */
void n_weapon_Damage(void) {
    u32 e = A0, hit = A1;
    n_weapon_ShotKind();
    s32 kind = (s32)V0;
    A0 = 5;
    n_weapon_Stat();
    s32 cls = (s32)V0;
    s32 d = GV(s16, 0x800A3CDC + (u32)(kind * 6)) *
            GV(s16, 0x800A3D88 + (u32)((cls + GV(u8, e + 0x315) * 5) << 1)) / 100;
    A0 = e; A1 = hit;
    n_enemy_HitFactor();
    s32 dmg = (s16)d * (s16)V0 / 100;
    s16 v = (s16)dmg;
    u32 dist = GV(u32, e + 0x868);
    s32 r;
    if (WEAPON_ID == 4) {                         /* magnum */
        if (dist < 2000) r = v;
        else if (dist < 5000) r = (s16)(v * 6 / 10);
        else if (dist < 10000) r = (s16)(v * 4 / 10);
        else { V0 = (u32)(v / 4); return; }
    } else {
        if (dist < 1000) {
            s32 t = v * 3;
            t += (s32)((u32)t >> 31);
            r = (s32)((u32)t << 15) >> 16;
        } else if (dist < 2000) r = v;
        else if (dist < 5000) r = (s16)(v * 4 / 5);
        else r = (s16)(v * 7 / 10);
    }
    V0 = (u32)(s32)(s16)r;
}

/* 800710FC (enemy, _, part, value): record a hit on an enemy (at most
 * four a frame): the body part, the side, the damage. Returns 1. */
void n_enemy_AddHit(void) {
    u32 e = A0, part = A2, value = A3;
    s16 n = GV(s16, e + 0x324);
    if (n >= 4) { V0 = 1; return; }
    n++;
    GV(s16, e + 0x324) = n;
    GV(u8, e + (u32)(s32)n + 0x326) = (u8)part;
    A0 = e;
    n_enemy_HitSide();
    GV(u8, e + (u32)(s32)n + 0x32B) = (u8)V0;
    A0 = 5;
    n_weapon_Stat();
    u32 h = e + (u32)((s32)n * 2);
    GV(u16, h + 0x344) = (u16)V0;
    GV(u16, h + 0x33A) = (u16)value;
    A0 = e; A1 = (u32)(s32)n;
    n_weapon_Damage();
    GV(u16, h + 0x330) = (u16)V0;
    GV(u32, e + 0x320) |= 0x10;
    V0 = 1;
}
