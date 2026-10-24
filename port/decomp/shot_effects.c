/* Shot and hit effects (TEST overlay): muzzle flashes, sparks, blood and
 * dust, as world sprites (HUD panels flagged 0x400, drawn by
 * hud_draw.c), and the effect image sets they use.
 *
 * Effect images come in sets loaded into 0x800C3E50: set ids listed at
 * 0x800A825C are built in (0x80097C14), the others come from a file;
 * 0x800C3A18 is the set loaded (-1 none). 0x800A8264 maps a stage to its
 * set ({stage, set} byte pairs, 0xFF ends).
 *
 * A world sprite's place: +10 x/4, +12 y, +18 z/16. Sprite definitions
 * for the effects start at 0x800A6F5C (0x14 bytes each).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "hud.h"
#include "game_stack.h"

void n_fx_LoadSet(void);
void n_fx_LoadStageSet(void);
void n_fx_StreamStageSet(void);
void n_fx_ResetSet(void);
void n_fx_LoadCommonSet(void);
void n_fx_FireSpecial(void);
void n_fx_Shot(void);
void n_fx_Muzzle(void);
void n_fx_MuzzleSmoke(void);
void n_fx_Spark(void);
void n_fx_BloodKind(void);
void n_fx_Blood(void);
void n_fx_Dust(void);
void n_fx_Hit(void);
void n_fx_Splatter(void);
void n_fx_DustAt(void);
void n_fx_Flash(void);
void n_hud_StartAnimA(void);

#define g_fxSet         GV(s32, 0x800C3A18)
#define FX_BUFFER       0x800C3E50u
#define FX_SPRITES      0x800A6F5Cu
#define F_UNPROJECT     0x8009B71Cu           /* (sx, sy, depth, out, flag): screen -> world */
#define F_ANIM_ONCE_A   0x8009414Cu
#define F_RAND          0x80011570u
#define F_LOAD_FILE     0x800115C0u

/* Allocate a panel and make it current; 0 if the pool is full. */
static u32 new_panel(void) {
    n_hud_AllocPanel();
    s16 i = (s16)V0;
    if (i == -1) { V0 = (u32)-1; return 0; }
    u32 p = panel_addr(i);
    g_curPanel = p;
    return p;
}

/* Give the current panel a sprite. */
static void set_sprite(u32 p, u32 sprite) {
    A0 = p; A1 = sprite;
    n_hud_SetSprite();
}

static void start_anim(u32 anim) {
    A0 = anim;
    n_hud_StartAnimA();
}

/* Place the current panel at a world position. */
static void place(u32 p, s32 x, u32 y, s32 z) {
    P16(p, 0x10) = (u16)div_pow2(x, 2);
    P16(p, 0x12) = (u16)y;
    P16(p, 0x18) = (u16)div_pow2(z, 4);
}

/* 8009806C (set, buffer): load an effect image set. */
void n_fx_LoadSet(void) {
    FRAME(0x20);
    u32 set = A0, buf = A1;
    for (u32 i = 0;; i++) {
        u8 b = GV(u8, 0x800A825Cu + i);
        if (b == 0xFF) break;
        if (set == b) {
            gcall(0x80097C14u, set, 0xFF, 0x800A825Cu, set);  /* built in (registers as left) */
            return;
        }
    }
    g_fxSet = (s32)set;
    GS(u32, sp + 0x10) = buf;
    GCALL4(F_LOAD_FILE, 0x8006C964u, 0x80058544u, 0x800A6F1Cu, set);
    GCALL2(0x800927D8u, buf, (u32)-1);
}

/* 80098100 (stage, buffer): load the stage's set unless loaded. */
void n_fx_LoadStageSet(void) {
    FRAME(0x18);
    u32 stage = A0, buf = A1;
    if (stage != (u32)g_fxSet) {
        for (u32 e = 0x800A8264u; GV(u8, e) != 0xFF; e += 2) {
            if (GV(u8, e) == stage) {
                A0 = GV(u8, e + 1);
                A1 = buf;
                n_fx_LoadSet();
                break;
            }
        }
    }
}

/* 8009815C (stage, buffer): start reading the stage's set in the
 * background; 1 if started. */
void n_fx_StreamStageSet(void) {
    FRAME(0x18);
    u32 stage = A0, buf = A1;
    V0 = 0;
    if (stage != (u32)g_fxSet) {
        for (u32 e = 0x800A8264u; GV(u8, e) != 0xFF; e += 2) {
            if (GV(u8, e) == stage) {
                GCALL4(0x80076B14u, 0x80058544u, 0x800A6F1Cu, GV(u8, e + 1), buf);
                V0 = 1;
                g_fxSet = (s32)stage;
                break;
            }
        }
    }
}

/* 80098AEC: load the common set; no stage set. */
void n_fx_ResetSet(void) {
    FRAME(0x18);
    A0 = FX_BUFFER;
    n_fx_LoadCommonSet();
    V0 = (u32)-1;
    g_fxSet = -1;
}

/* 80098B18 (buffer): load the common effect images (file 0x29). */
void n_fx_LoadCommonSet(void) {
    FRAME(0x18);
    u32 buf = A0;
    GCALL4(0x800116A4u, 0x8006C9A8u, 0x80058540u, 0x29, buf);
    GCALL2(0x800927D8u, buf, (u32)-1);
}

/* 800981E0: firing a weapon with its own projectile: the rocket launcher
 * (id 7) or the grenade launcher (id 8, by the grenades loaded: 13
 * normal, 14 acid, 15 flame). Returns 1 if handled. */
void n_fx_FireSpecial(void) {
    FRAME(0x18);
    u32 w = g_currentWeaponAddr;
    s16 id = GV(s16, w);
    if (id == 7) {
        gcall(0x8009F0D0u, w, A1, A2, A3);                 /* a0 the weapon, the rest as given */
        V0 = 1;
    } else if (id != 8) {
        V0 = 0;
    } else {
        s16 ammo = GV(s16, w + 4);
        s32 kind = ammo == 0xD ? 0 : ammo == 0xE ? 1 : ammo == 0xF ? 2 : -1;
        if (kind >= 0) gcall(0x8009BBF0u, (u32)kind, A1, A2, A3);
        V0 = 1;
    }
}

/* 80098284 (_, _, flag, event): a shot was fired (event 0x1E): the muzzle
 * flash for the magnum / shotguns (by weapon), else the plain one. */
void n_fx_Shot(void) {
    FRAME(0x18);
    u32 a2 = A2;
    if (A3 == 0x1E) {
        s16 id = GV(s16, g_currentWeaponAddr);
        switch (id) {
        case 4: A0 = 0; n_fx_Muzzle(); break;
        case 5: A0 = 1; n_fx_Muzzle(); break;
        case 6: A0 = 2; n_fx_Muzzle(); break;
        case 13: case 14: A0 = 3; n_fx_Muzzle(); break;
        default: A0 = a2; n_fx_Flash(); break;
        }
    }
}

/* 80098330 (kind): a big muzzle flash at the reticle, two sprites from
 * the table at 0x800A8278 ({anim, s16 sprite, u16 scale, s16 smoke, _,
 * anim 2}, 16 bytes per kind). */
void n_fx_Muzzle(void) {
    FRAME(0x48);
    u32 kind = A0;
    n_hud_AllocPanel();
    s16 i = (s16)V0;
    if (i == -1) { V0 = 1; return; }
    GS(u32, sp + 0x10) = 1;
    GCALL4(F_UNPROJECT, (u32)(g_reticleX - 0xA0), (u32)(g_reticleY - 0x3C), 0x7D0, sp + 0x18);
    u32 p = panel_addr(i);
    P16(p, PANEL_FLAGS) |= 0x141C;
    u32 t = 0x800A8278u + kind * 16;
    g_curPanel = p;
    set_sprite(p, FX_SPRITES + (u32)((s32)GV(s16, t + 4) * 20));
    if (GV(s16, t + 4) == 4) P16(g_curPanel, 0x0E) = (u16)(P16(g_curPanel, 0x0E) + 3);
    p = g_curPanel;
    place(p, GS(s32, sp + 0x18) + 0xA0, (u32)(GS(u16, sp + 0x1C) + 0x78), GS(s32, sp + 0x20));
    P16(p, 0x34) = GV(u16, t + 6);
    start_anim(GV(u32, t));
    P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
    p = new_panel();
    if (!p) { return; }
    P16(p, PANEL_FLAGS) |= 0x141C;
    set_sprite(p, FX_SPRITES + 0x50);
    p = g_curPanel;
    place(p, GS(s32, sp + 0x18) + 0xA0, (u32)(GS(u16, sp + 0x1C) + 0x78), GS(s32, sp + 0x20));
    P16(p, 0x34) = GV(u16, t + 6);
    start_anim(GV(u32, t + 0xC));
    if (GV(s16, t + 8) != 0) {
        p = g_curPanel;
        P32(p, PANEL_UPDATE) = 0x80098580u;
        P16(p, 0x2C) = 0x40;
        V0 = 0x40;
    } else {
        P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
        V0 = g_curPanel;
    }
}

/* 80098580: the muzzle smoke: play once; at its last frame hold it and
 * fade it out (+0x2C) as a lit sprite. */
void n_fx_MuzzleSmoke(void) {
    FRAME(0x18);
    gcall(F_ANIM_ONCE_A, A0, A1, A2, A3);              /* registers as given */
    u32 p = g_curPanel;
    u32 e = GV(u32, P32(p, PANEL_DRAWPTR)) + (u32)((s32)(s16)P16(p, 0x1E) * 4);
    V0 = (u32)-1;
    if (GV(s16, e + 4) == -1) {
        P16(p, 0x1A) = 2;
        P16(p, 0x02) |= 0x20;
        u16 l = (u16)(P16(p, 0x2C) - 0x10);
        P16(p, 0x2C) = l;
        P16(p, PANEL_FLAGS) |= 0x200;
        V0 = P16(p, PANEL_FLAGS);
        if ((s16)l <= 0) { P16(p, PANEL_FLAGS) = 1; V0 = 1; }
    }
}

/* 80098608 (x, y, z, opaque, enemy): a hit spark (or blood, in the
 * enemy's colour). */
void n_fx_Spark(void) {
    FRAME(0x28);
    u32 enemy = GS(u32, sp + 0x28 + 0x10);              /* argument 5 */
    s32 x = (s32)A0;
    u32 y = A1;
    s32 z = (s32)A2;
    u32 opaque = A3;
    u32 p = new_panel();
    if (!p) { return; }
    P16(p, PANEL_FLAGS) |= opaque ? 0x40C : 0x41C;
    set_sprite(g_curPanel, FX_SPRITES);
    if (enemy) {
        A0 = enemy;
        n_fx_BloodKind();
        P16(g_curPanel, 0x0E) = (u16)(P16(g_curPanel, 0x0E) + V0);
    }
    p = g_curPanel;
    place(p, x, y + 0x14, z);
    P16(p, 0x34) = 0xAA;
    start_anim(0x800A7578u);
    P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
    V0 = F_ANIM_ONCE_A;
}

/* 80098740 (enemy): the colour of its blood: 0 red, 1 or 2 by type. */
void n_fx_BloodKind(void) {
    u32 e = A0;
    u8 type = GV(u8, e + 0x315);
    if (type < 0x32) {
        s16 k = GV(s16, 0x800BB020u + (u32)((s32)GV(s16, e + 0x84E) * 4));
        switch (k) {
        case 0x1C: case 0x10: case 0x1E: case 0x1A: case 0x16: case 0x1F: case 0x20: case 0x21:
            V0 = 2;
            return;
        }
        V0 = (u32)(k - 0x13) < 2;
        return;
    }
    if ((u8)(type / 10) == 11) V0 = (type != 0x6F) << 1;
    else V0 = 0;
}

/* 800987F8 (x, y, z, enemy): a blood spray. */
void n_fx_Blood(void) {
    FRAME(0x28);
    s32 x = (s32)A0;
    u32 y = A1;
    s32 z = (s32)A2;
    u32 enemy = A3;
    u32 p = new_panel();
    if (!p) { return; }
    P16(p, PANEL_FLAGS) |= 0x41C;
    set_sprite(p, FX_SPRITES);
    if (enemy) {
        A0 = enemy;
        n_fx_BloodKind();
        P16(g_curPanel, 0x0E) = (u16)(P16(g_curPanel, 0x0E) + V0);
    }
    p = g_curPanel;
    place(p, x, y + 0x14, z);
    P16(p, 0x34) = 0xAA;
    start_anim(0x800A7580u);
    P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
    V0 = F_ANIM_ONCE_A;
}

/* 80098A04 (x, y, z): a puff of dust. */
void n_fx_Dust(void) {
    FRAME(0x28);
    s32 x = (s32)A0;
    u32 y = A1;
    s32 z = (s32)A2;
    u32 p = new_panel();
    if (!p) { return; }
    P16(p, PANEL_FLAGS) |= 0x41C;
    set_sprite(p, 0x800A6F98u);
    p = g_curPanel;
    P16(p, 0x02) |= 0x20;
    place(p, x, y, z);
    start_anim(0x800A7780u);
    P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
    V0 = F_ANIM_ONCE_A;
}

/* 80099068 (obj, a, b, c, kind): an effect where something was hit; obj
 * has its position at +0x18. Kind 0x1F: a random spark/blood mix around
 * it; 2-17: the effect handlers (2 the room's splatter, 6 dust, ...). */
void n_fx_Hit(void) {
    FRAME(0x30);
    s32 kind = GS(s32, sp + 0x30 + 0x10);               /* argument 5 */
    u32 obj = A0, a1 = A1, a2 = A2, a3 = A3;
    if (kind >= 0x1E) {
        s32 k = kind - 0x1E;
        V0 = 1;
        if (k != 1) { return; }
        s32 r = (s32)GCALL0(0x800398A8u);                   /* rand */
        s32 x = GS(s32, obj + 0x18), y = GS(s32, obj + 0x1C), z = GS(s32, obj + 0x20);
        switch ((s16)(r % 3)) {
        case 1:
            GS(u32, sp + 0x10) = a1;
            GCALL4(0x80098608u, (u32)(x - 8), (u32)(y - 8), (u32)(z - 8), 0);
            GCALL4(0x800987F8u, (u32)x, (u32)y, (u32)z, a1);
            break;
        case 0:
            GS(u32, sp + 0x10) = a1;
            GCALL4(0x80098608u, (u32)x, (u32)y, (u32)z, 0);
            break;
        case 2:
            GCALL4(0x800987F8u, (u32)(x - 8), (u32)(y - 8), (u32)(z - 8), a1);
            GCALL4(0x800987F8u, (u32)x, (u32)y, (u32)z, a1);
            break;
        }
        return;
    }
    u32 s1 = (u32)(s32)(s16)a1, s2 = (u32)(s32)(s16)a2, s3 = (u32)(s32)(s16)a3;
    switch (kind) {
    case 2:  GCALL4(0x80099304u, obj, s1, s2, s3); break;
    case 3:  GCALL4(0x80099554u, obj, s1, s2, s3); break;
    case 4:  GCALL4(0x80099724u, obj, s1, s2, s3); break;
    case 5:  GCALL4(0x80099C30u, obj, s1, s2, s3); break;
    case 6:  GCALL4(0x80099CB0u, obj, s1, s2, s3); break;
    case 9:  GCALL4(0x8009A944u, obj, s1, s2, s3); break;
    case 11: { u32 r1 = R[5], r2 = R[6], r3 = R[7]; gcall(0x8009B0CCu, obj, r1, r2, r3); break; }
    case 14: gcall(0x8009A310u, obj, 0, a2, a3); break;
    case 15: gcall(0x8009FA40u, a1, a2, a3, a3); break;
    case 17: gcall(0x8009A310u, obj, 1, a2, a3); break;
    case 7: case 8: case 10: case 12: case 13: case 16:
        V0 = 0x800992E4u;                                   /* the jump table's target */
        break;
    default:
        V0 = 0;
        break;
    }
}

/* 80099304 (obj, kind, _, _): a splatter of drops where obj (+0x198 x,
 * +0x1A0 z of its matrix) is, if the stage/room has one (0x800A82B8:
 * {s16 stage, s16 room, u16 y, u16 frame offset}, -1 ends). Kinds at
 * 0x800A82CC: {s16 spread, s16 size range, s16 period (-1 every time),
 * s16 drops}. */
void n_fx_Splatter(void) {
    FRAME(0x38);
    u32 obj = A0;
    s32 kind = (s16)A1;
    u32 room = 0x800A82B8u;
    for (;; room += 8) {
        s16 st = GV(s16, room);
        if (st == -1) { V0 = (u32)-1; return; }
        if (GV(s32, 0x800A866C) == st && GV(s32, 0x800B3F08) == GV(s16, room + 2)) break;
    }
    u32 y = GV(u16, room + 4);
    u32 n = GV(u32, 0x800C3A94) + 1;
    GV(u32, 0x800C3A94) = n;
    u32 k = 0x800A82CCu + (u32)(kind * 8);
    s32 period = GV(s16, k + 4);
    if (period != -1) {
        s32 m = mips_rem((s32)(n << 4), period);
        u32 v = GV(u32, 0x800C3A90) & (u32)period;
        if (v != (u32)m) { V0 = v; return; }
    }
    u32 mat = obj + 0x180;
    s32 i = 0;
    V0 = (u32)(s32)GV(s16, k + 6);
    if (GV(s16, k + 6) <= 0) { return; }
    do {
        u32 p = new_panel();
        if (p) {
            P16(p, PANEL_FLAGS) |= 0x41C;
            set_sprite(p, FX_SPRITES);
            p = g_curPanel;
            P16(p, 0x0E) = (u16)(P16(p, 0x0E) + GV(u16, room + 6));
            s32 r = (s32)GCALL0(F_RAND);
            s32 x = GS(s32, mat + 0x18) + mips_rem(r, GV(s16, k));
            P16(g_curPanel, 0x10) = (u16)div_pow2(x, 2);
            P16(g_curPanel, 0x12) = (u16)y;
            r = (s32)GCALL0(F_RAND);
            s32 z = GS(s32, mat + 0x20) + mips_rem(r, GV(s16, k));
            P16(g_curPanel, 0x18) = (u16)div_pow2(z, 4);
            r = (s32)GCALL0(F_RAND);
            P16(g_curPanel, 0x34) = (u16)(mips_rem(r, GV(s16, k + 2)) + 0x60);
            start_anim(0x800A7578u);
            P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
        }
        i++;
    } while (i < GV(s16, k + 6));
    V0 = 0;
}

/* 80099CB0 (obj, dx, dy, dz): dust at an offset from obj's position. */
void n_fx_DustAt(void) {
    FRAME(0x18);
    u32 obj = A0;
    s32 dx = (s16)A1, dy = (s16)A2, dz = (s16)A3;
    A0 = GS(u32, obj + 0x18) + (u32)dx;
    A1 = GS(u32, obj + 0x1C) + (u32)dy;
    A2 = GS(u32, obj + 0x20) + (u32)dz;
    n_fx_Dust();
}

/* 80099D00 (handgun): a small muzzle flash at the reticle, by handgun
 * (ids 0-3), or the generic one (0). */
void n_fx_Flash(void) {
    FRAME(0x38);
    u32 handgun = A0;
    n_hud_AllocPanel();
    s16 i = (s16)V0;
    if (i == -1) { V0 = (u32)-1; return; }
    GS(u32, sp + 0x10) = 1;
    GCALL4(F_UNPROJECT, (u32)(g_reticleX - 0xA0), (u32)(g_reticleY - 0x3C), 0x7D0, sp + 0x18);
    u32 p = panel_addr(i);
    g_curPanel = p;
    P16(p, PANEL_FLAGS) |= 0x140C;
    set_sprite(p, 0x800A704Cu);
    p = g_curPanel;
    P16(p, 0x0E) = (u16)(P16(p, 0x0E) + 2);
    place(p, GS(s32, sp + 0x18) + 0xA0, (u32)(GS(u16, sp + 0x1C) + 0x78), GS(s32, sp + 0x20));
    P16(p, 0x34) = 0x60;
    u32 anim = 0;
    if (!handgun) anim = 0x800A7224u;
    else {
        switch (GV(s16, g_currentWeaponAddr)) {
        case 0: anim = 0x800A7134u; break;
        case 1: anim = 0x800A7184u; break;
        case 2: anim = 0x800A70E4u; break;
        case 3: anim = 0x800A71D4u; break;
        }
    }
    if (anim) start_anim(anim);
    P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
    V0 = g_curPanel;
}
