/* Hit and burst effects made of HUD panels in the world (TEST overlay):
 * the puff and flying chunks of a hit (fx_Hit kinds 4 and 5), the flame
 * left on an enemy part by a grenade burst, and two rising / falling
 * sprite effects.
 *
 * World sprite panels (flag 0x400) keep their position as x/4, y, z/16
 * (+0x10, +0x12, +0x18); see hud_draw.c.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "hud.h"
#include "libgs.h"
#include "game_stack.h"
#include "poll.h"

void n_fx_HitPuff(void);
void n_fx_HitPuffAt(void);
void n_fx_ChunksUpdate(void);
void n_fx_Chunks(void);
void n_fx_HitChunksAt(void);
void n_fx_PartFlame(void);
void n_fx_PartFlameUpdate(void);
void n_fx_AcidSplash(void);
void n_fx_AcidPuffUpdate(void);
void n_fx_AcidBurnUpdate(void);
void n_stub_ReturnZero(void);
void n_fx_Glint(void);
void n_fx_Rise(void);
void n_fx_RiseUpdate(void);
void n_fx_Shards(void);
void n_fx_ShardsAt(void);
void n_fx_ShardUpdate(void);

#define F_START_ANIM_A   0x800947D4u
#define F_ANIM_ONCE_A    0x8009414Cu
#define F_ANIM_LOOP_A    0x800940C4u
#define F_ANIM_CHAIN     0x800943D0u
#define F_START_CHAIN    0x800948BCu
#define F_APPLY_MATRIX   0x8002A5B8u
#define F_SPARK          0x80098608u
#define F_RANDOM8        0x80011570u
#define g_camYaw         GV(u16, 0x800A8606)

/* A new world sprite panel at (x, y, z) with the given extra flags and
 * sprite; it becomes the current panel. Its index (-1: none free). */
static s16 new_sprite(u32 flags, u32 sprite) {
    n_hud_AllocPanel();
    s16 i = (s16)V0;
    if (i == -1) return i;
    u32 p = panel_addr(i);
    g_curPanel = p;
    P16(p, PANEL_FLAGS) |= (u16)flags;
    A0 = p; A1 = sprite;
    n_hud_SetSprite();
    return i;
}

static void place(u32 p, s32 x, s32 y, s32 z) {
    P16(p, 0x10) = (u16)div_pow2(x, 2);
    P16(p, 0x12) = (u16)y;
    P16(p, 0x18) = (u16)div_pow2(z, 4);
}

/* 80098910 (x, y, z): a puff. */
void n_fx_HitPuff(void) {
    FRAME(0x28);
    s32 x = (s32)A0, y = (s32)A1, z = (s32)A2;
    if (new_sprite(0x41C, 0x800A6F5Cu) == -1) { V0 = (u32)-1; return; }
    u32 p = g_curPanel;
    P16(p, 0xE) = (u16)(P16(p, 0xE) + 2);
    place(p, x, y, z);
    P16(p, 0x34) = (u16)(P16(p, 0x34) + 0x66);
    GCALL1(F_START_ANIM_A, 0x800A7578u);
    P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
    V0 = F_ANIM_ONCE_A;
}

/* 80099724 (obj, dx, dy, dz): a puff at an object's position plus an
 * offset. */
void n_fx_HitPuffAt(void) {
    FRAME(0x18);
    u32 obj = A0;
    s32 dx = (s16)A1, dy = (s16)A2, dz = (s16)A3;
    GCALL3(0x80098910u, GS(u32, obj + 0x18) + (u32)dx, GS(u32, obj + 0x1C) + (u32)dy, GS(u32, obj + 0x20) + (u32)dz);
}

/* 80099A60 (x, y, z): chunks flying off in a random direction around the
 * camera's facing (a chain animation, falling with gravity). */
void n_fx_Chunks(void) {
    FRAME(0x60);
    s32 x = (s32)A0, y = (s32)A1, z = (s32)A2;
    s16 i = new_sprite(0x41C, 0x800A6F70u);
    if (i == -1) { V0 = (u32)(s32)i; return; }
    place(g_curPanel, x, y, z);
    u32 m = sp + 0x28;
    copy_words(m, IDENTITY_MATRIX, 8);
    GS(u32, sp + 0x10) = 0;
    GS(s16, sp + 0x14) = -0x88;
    s32 r = (s32)GCALL0(F_RAND);
    GCALL2(F_ROT_MATRIX_Y, (u32)(g_camYaw + r % 126 - 0x3F) & 0xFFF, m);
    GCALL3(F_APPLY_MATRIX, m, sp + 0x10, sp + 0x18);
    u32 p = g_curPanel;
    P16(p, 0x2E) = GS(u16, sp + 0x18);
    P16(p, 0x30) = (u16)-0x5C;
    P16(p, 0x32) = GS(u16, sp + 0x20);
    GCALL1(F_START_CHAIN, 0x800A771Cu);
    p = g_curPanel;
    u16 f = P16(p, 0x1E);
    P32(p, PANEL_UPDATE) = 0x80099774u;
    P16(p, 0x34) = 0xA0;
    P16(p, 0x2C) = f;
    V0 = (u32)(s32)i;
}

/* 80099774: update the chunks: animate, move the chain by the velocity
 * (+0x2E x, +0x30 y falling, +0x32 z), and on each new frame from the
 * fourth spread the panels along the camera's facing (0x800A82EC). */
void n_fx_ChunksUpdate(void) {
    FRAME(0x58);
    GCALL0(F_ANIM_CHAIN);
    u32 h = g_curPanel;
    if (P16(h, PANEL_FLAGS) & 1) return;
    P16(h, 0x26) = (u16)(P16(h, 0x26) + div_pow2((s16)P16(h, 0x2E), 2));
    s32 vz = (s16)P16(h, 0x32);
    u16 vy = (u16)(P16(h, 0x30) + 0x3C);
    P16(h, 0x30) = vy;
    P16(h, 0x28) = (u16)(P16(h, 0x28) + vy);
    P16(h, 0x2A) = (u16)(P16(h, 0x2A) + div_pow2(vz, 4));
    s32 dy = (s16)P16(h, 0x30);
    for (u32 p = h;;) {
        s32 dx = (s16)P16(p, 0x10) - (s16)P16(p, 0x26);
        P16(p, 0x26) = P16(h, 0x26);
        P16(p, 0x10) = (u16)(P16(h, 0x26) + dx);
        P16(p, 0x28) = P16(h, 0x28);
        P16(p, 0x12) = (u16)(P16(p, 0x12) + dy);
        s32 dz = (s16)P16(p, 0x18) - (s16)P16(p, 0x2A);
        u16 next = P16(p, 0x36);
        P16(p, 0x2A) = P16(h, 0x2A);
        P16(p, 0x18) = (u16)(P16(h, 0x2A) + dz);
        if (next == 0xFFFF) break;
        p = panel_addr(next);
    }
    s16 f = (s16)P16(h, 0x1E);
    if (f == (s16)P16(h, 0x2C)) return;
    P16(h, 0x2C) = (u16)f;
    if (f < 4) return;
    u32 m = sp + 0x28;
    copy_words(m, IDENTITY_MATRIX, 8);
    GS(u32, sp + 0x10) = 0;
    GS(s16, sp + 0x14) = -0x3E8;
    GCALL2(F_ROT_MATRIX_Y, (u32)(s32)GV(s16, 0x800A8606), m);
    GCALL3(F_APPLY_MATRIX, m, sp + 0x10, sp + 0x18);
    u32 c = g_curPanel;
    u32 sc = P16(c, 0x34);
    s32 ux = div_pow2((s32)((u32)GS(s32, sp + 0x18) * sc), 8);
    s32 uz = div_pow2((s32)((u32)GS(s32, sp + 0x20) * sc), 8);
    u32 spread = 0x800A82ECu;
    for (u32 p = c;; spread += 2) {
        s32 k = (s32)((u32)(s32)GV(s16, spread) * (u32)(s32)(s16)P16(c, 0x1E));
        P16(p, 0x10) = (u16)(P16(p, 0x10) + (s32)((u32)ux * (u32)k) / 4000);
        k = (s32)((u32)(s32)GV(s16, spread) * (u32)(s32)(s16)P16(c, 0x1E));
        P16(p, 0x18) = (u16)(P16(p, 0x18) + (s32)((u32)uz * (u32)k) / 16000);
        u16 next = P16(p, 0x36);
        if (next == 0xFFFF) break;
        p = panel_addr(next);
    }
    P16(c, 0x1E) = (u16)(P16(c, 0x1E) + 1);
}

/* 80099C30 (obj, dx, dy, dz): a puff and chunks at an object's position
 * plus an offset. */
void n_fx_HitChunksAt(void) {
    FRAME(0x20);
    u32 obj = A0;
    u32 x = GS(u32, obj + 0x18) + (u32)(s32)(s16)A1;
    u32 y = GS(u32, obj + 0x1C) + (u32)(s32)(s16)A2;
    u32 z = GS(u32, obj + 0x20) + (u32)(s32)(s16)A3;
    GCALL3(0x80098910u, x, y, z);
    GCALL3(0x80099A60u, x, y, z);
}

/* 8009E2D0 (record, pos, part): a flame on an enemy's part (index
 * part / 16 of the enemies, part % 16 its model part): a sprite where the
 * current panel is, and a controller panel (no sprite) that follows the
 * part. The current panel is kept. */
void n_fx_PartFlame(void) {
    FRAME(0x38);
    u32 rec = A0, pos = A1, part = A2;
    n_hud_AllocPanel();
    s16 ctl = (s16)V0;
    if (ctl == -1) return;
    u32 cur = g_curPanel;
    n_hud_AllocPanel();
    s16 spr = (s16)V0;
    if (spr == -1) {
        P16(panel_addr(ctl), PANEL_FLAGS) = 1;
    } else {
        u32 q = panel_addr(spr);
        g_curPanel = q;
        P16(q, PANEL_FLAGS) |= 0x518;
        A0 = q; A1 = 0x800A7038u;
        n_hud_SetSprite();
        q = g_curPanel;
        P16(q, 0x10) = P16(cur, 0x10);
        P16(q, 0x12) = P16(cur, 0x12);
        P16(q, 0x34) = 0x80;
        P16(q, 0x18) = P16(cur, 0x18);
        GCALL1(F_START_ANIM_A, 0x800A7B00u);
        u32 c = panel_addr(ctl);
        P16(c, 0x28) = 0x78;
        P16(c, 0x22) = (u16)spr;
        P16(c, 0x24) = 0;
        P16(c, 0x26) = (u16)part;
        P32(c, 0x38) = rec;
        P32(c, PANEL_UPDATE) = 0x8009E454u;
        P16(c, PANEL_FLAGS) |= 0x400;
        P16(c, 0x10) = GS(u16, pos);
        P16(c, 0x12) = GS(u16, pos + 4);
        g_curPanel = c;
        P16(c, 0x18) = GS(u16, pos + 8);
    }
    g_curPanel = cur;
}

/* 8009E454: update the flame's controller: free both when the part is
 * gone or the flame has faded (from 30 frames left); a spark on the
 * second frame; keep the sprite on the part (the controller's position
 * in the part's coordinates, +0x38 the part's GsCOORDINATE2). */
void n_fx_PartFlameUpdate(void) {
    FRAME(0x98);
    u32 c = g_curPanel;
    s32 part = (s16)P16(c, 0x26);
    s16 spr = (s16)P16(c, 0x22);
    s32 ei = div_pow2(part, 4);
    u32 rec = ENEMIES + (u32)ei * ENEMY_SIZE;
    int gone = !GV(u8, rec + 0x29) || !GV(u8, rec + (u32)(s32)(s16)(part - ei * 16));
    if (!gone) {
        P16(c, 0x28) = (u16)(P16(c, 0x28) - 1);
        if ((s16)P16(c, 0x28) < 0x1E) {
            u32 q = panel_addr(spr);
            P16(q, 0x34) = (u16)(P16(q, 0x34) - 8);
            if (P16(q, 0x34) == 0) gone = 1;
        }
    }
    if (gone) {
        P16(panel_addr(spr), PANEL_FLAGS) = 1;
        P16(g_curPanel, PANEL_FLAGS) = 1;
        return;
    }
    u32 s0 = g_curPanel;
    u32 q = panel_addr((s16)P16(s0, 0x22));
    P16(s0, 0x24) = (u16)(P16(s0, 0x24) + 1);
    g_curPanel = q;
    if ((s16)P16(s0, 0x24) == 2) {
        s32 pi = (s16)P16(s0, 0x26);
        s32 qx = (s16)P16(q, 0x26), qz = (s16)P16(q, 0x2A), qy = (s16)P16(q, 0x28);
        GS(u32, sp + 0x10) = ENEMIES + (u32)div_pow2(pi, 4) * ENEMY_SIZE;
        GCALL4(F_SPARK, (u32)(qx * 4), (u32)qy, (u32)(qz * 16), 0);
        g_curPanel = panel_addr((s16)P16(s0, 0x22));
    }
    P16(g_curPanel, PANEL_FLAGS) |= 4;
    /* a GsCOORDINATE2 at sp+0x18: the controller's position under the part */
    const u32 co = sp + 0x18;
    GS(s32, co + 0x18) = (s16)P16(s0, 0x10);
    GS(s32, co + 0x1C) = (s16)P16(s0, 0x12);
    GS(s32, co + 0x20) = (s16)P16(s0, 0x18);
    GS(u16, co + 0x48) = 0;
    GS(u16, co + 0x46) = 0;
    GS(u16, co + 0x44) = 0;
    GS(u32, co + 0x4C) = P32(s0, 0x38);
    GS(u32, co) = 0;
    GCALL2(F_GS_GET_LS, co, sp + 0x68);
    u32 p = g_curPanel;
    s32 d = (s16)P16(p, 0x10) - (s16)P16(p, 0x26);
    u16 v = (u16)div_pow2(GS(s32, co + 0x38), 2);
    P16(p, 0x26) = v;
    P16(p, 0x10) = (u16)(v + d);
    d = (s16)P16(p, 0x12) - (s16)P16(p, 0x28);
    v = GS(u16, co + 0x3C);
    P16(p, 0x28) = v;
    P16(p, 0x12) = (u16)(v + d);
    d = (s16)P16(p, 0x18) - (s16)P16(p, 0x2A);
    v = (u16)div_pow2(GS(s32, co + 0x40), 4);
    P16(p, 0x2A) = v;
    P16(p, 0x18) = (u16)(v + d);
    GCALL0(F_ANIM_LOOP_A);
    g_curPanel = s0;
}

/* 8009E704 (coordinate, pos, record): an acid grenade bursting on an
 * enemy: (a spark if its attack info has 0x2000) a splash and a puff where
 * pos is in the coordinate's system, and, unless the enemy burns already,
 * a burn (0x8009EB58) on each of its visible model parts (every other
 * one for models of 13 parts or more), starting after 10-19 frames. The
 * current panel is kept (not when it already burns). */
void n_fx_AcidSplash(void) {
    FRAME(0xB0);
    u32 coord = A0, pos = A1, rec = A2;
    u32 keep = g_curPanel;
    if (GCALL2(0x800829D8u, rec, 9) & 0x2000) {
        u32 p = g_curPanel;
        GS(u32, sp + 0x10) = rec;
        GCALL4(F_SPARK, (u32)((s32)(s16)P16(p, 0x10) << 2), (u32)(s32)(s16)P16(p, 0x12),
               (u32)((s32)(s16)P16(p, 0x18) << 4), 0);
    }
    /* a GsCOORDINATE2 at sp+0x18: pos under the coordinate */
    const u32 co = sp + 0x18;
    GS(u32, co + 0x18) = GS(u32, pos);
    GS(u32, co + 0x1C) = GS(u32, pos + 4);
    GS(u32, co + 0x4C) = coord;
    GS(u16, co + 0x48) = 0;
    GS(u16, co + 0x46) = 0;
    GS(u16, co + 0x44) = 0;
    GS(u32, co) = 0;
    GS(u32, co + 0x20) = GS(u32, pos + 8);
    GCALL2(F_GS_GET_LS, co, sp + 0x68);
    s32 wx = GS(s32, co + 0x38), wz = GS(s32, co + 0x40);
    n_hud_AllocPanel();
    s16 i = (s16)V0;
    if (i != -1) {
        u32 q = panel_addr(i);
        g_curPanel = q;
        P16(q, PANEL_FLAGS) |= 0x41C;
        A0 = q; A1 = 0x800A6FACu;
        n_hud_SetSprite();
        q = g_curPanel;
        P16(q, 0x10) = (u16)div_pow2(GS(s32, co + 0x38), 2);
        P16(q, 0x12) = GS(u16, co + 0x3C);
        P16(q, 0x18) = (u16)div_pow2(GS(s32, co + 0x40), 4);
        GCALL1(F_START_ANIM_A, 0x800A7E78u);
        P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
    }
    n_hud_AllocPanel();
    i = (s16)V0;
    if (i != -1) {
        u32 q = panel_addr(i);
        g_curPanel = q;
        P16(q, PANEL_FLAGS) |= 0x41C;
        A0 = q; A1 = 0x800A6FACu;
        n_hud_SetSprite();
        q = g_curPanel;
        P16(q, 0x14) = 0x30;
        P16(q, 0x8) = 0x30;
        P16(q, 0x16) = 0x20;
        P16(q, 0xA) = 0x20;
        s32 back = (s16)P16(keep, 0x2E) / 50;
        P16(q, 0x10) = (u16)div_pow2(wx - back, 2);
        P16(q, 0x12) = GS(u16, co + 0x3C);
        back = (s16)P16(keep, 0x2E) / 50;
        P16(q, 0x18) = (u16)div_pow2(wz - back, 4);
        P16(q, 0x2C) = 0;
        P32(q, PANEL_UPDATE) = 0x8009EB1Cu;
        P16(q, 0xE) = (u16)(P16(q, 0xE) + 3);
    }
    u8 slot = GV(u8, rec + 0x314);
    for (s32 k = 0;;) {                              /* burning already? */
        u32 q = panel_addr(k);
        if ((P16(q, PANEL_FLAGS) & 1) == 0 && P32(q, PANEL_UPDATE) == 0x8009EB58u &&
            (s16)P16(q, 0x2A) == slot)
            return;
        if (++k >= PANEL_COUNT) break;
        LOOP_POLL(0x8009E9D0u);
    }
    u32 part = rec + 0x2C;
    u32 every_other = !(GV(u8, rec + 0x28) < 0xD);
    if (GV(u8, rec + 0x28) != 0) {
        for (s32 k = 0;;) {
            if (GV(u32, part + 4) != 0 && GV(u8, rec + (u32)k) != 0 && ((u32)k & every_other) == 0 &&
                GV(u32, part) != 0) {
                n_hud_AllocPanel();
                s16 j = (s16)V0;
                if (j == -1) break;
                u32 q = panel_addr(j);
                P16(q, PANEL_FLAGS) |= 0x400;
                P32(q, PANEL_DRAWPTR) = GV(u32, part);
                g_curPanel = q;
                s32 r = (s32)GCALL0(F_RAND);
                q = g_curPanel;
                P16(q, 0x22) = (u16)(r % 10 + 10);
                P16(q, 0x2C) = (u16)k;
                P32(q, PANEL_UPDATE) = 0x8009EB58u;
                P16(q, 0x2A) = GV(u8, rec + 0x314);
            }
            k++;
            part += 8;
            if (!(k < (s32)GV(u8, rec + 0x28))) break;
            LOOP_POLL(0x8009EAE0u);
        }
    }
    g_curPanel = keep;
}

/* 8009EB1C: the acid puff: gone after 9 frames. */
void n_fx_AcidPuffUpdate(void) {
    u32 p = g_curPanel;
    u16 t = (u16)(P16(p, 0x2C) + 1);
    P16(p, 0x2C) = t;
    if ((s16)t >= 9) P16(p, PANEL_FLAGS) = 1;
    V0 = 1;
}

/* 8009EB58: an acid burn on an enemy part (+0x2A the enemy's slot, +0x2C
 * the part, +0x38 its GsCOORDINATE2): after its delay (+0x22) a flame
 * sprite (index at +0x24) plays once, kept on the part a little towards
 * the camera. Both go with the part, or when the flame is over. */
void n_fx_AcidBurnUpdate(void) {
    FRAME(0x60);
    u32 c = g_curPanel;
    u32 rec = ENEMIES + (u32)(s32)(s16)P16(c, 0x2A) * ENEMY_SIZE;
    if (GV(u8, rec + (u32)(s32)(s16)P16(c, 0x2C)) == 0) {      /* the part is gone */
        if ((s16)P16(c, 0x22) == 0) P16(panel_addr((s16)P16(c, 0x24)), PANEL_FLAGS) = 1;
        P16(g_curPanel, PANEL_FLAGS) = 1;
        return;
    }
    u32 co = P32(c, PANEL_DRAWPTR);
    if ((s16)P16(c, 0x22) == 0) {
        g_curPanel = panel_addr((s16)P16(c, 0x24));
        GCALL0(F_ANIM_ONCE_A);
        if (P16(g_curPanel, PANEL_FLAGS) & 1) {
            P16(c, PANEL_FLAGS) = 1;
        } else {
            const u32 m = sp + 0x28, v = sp + 0x10, out = sp + 0x18;
            s32 yaw = (s16)g_camYaw;
            for (u32 k = 0; k < 0x20; k += 4) GS(u32, m + k) = GV(u32, IDENTITY_MATRIX + k);
            GCALL2(F_ROT_MATRIX_Y, (u32)yaw, m);
            GS(u32, v) = 0;
            GS(u16, v + 4) = 0x64;
            GCALL3(F_APPLY_MATRIX, m, v, out);
            u32 q = g_curPanel;
            s32 rx = (s16)P16(q, 0x10) - (s16)P16(q, 0x26);
            u16 bx = (u16)div_pow2((s32)(GV(u32, co + 0x38) + GS(u32, out)), 2);
            P16(q, 0x26) = bx;
            P16(q, 0x10) = (u16)(bx + rx);
            s32 ry = (s16)P16(q, 0x12) - (s16)P16(q, 0x28);
            u16 by = GV(u16, co + 0x3C);
            P16(q, 0x28) = by;
            P16(q, 0x12) = (u16)(by + ry);
            s32 rz = (s16)P16(q, 0x18) - (s16)P16(q, 0x2A);
            u16 bz = (u16)div_pow2((s32)(GV(u32, co + 0x40) + GS(u32, out + 8)), 4);
            P16(q, 0x2A) = bz;
            P16(q, 0x18) = (u16)(bz + rz);
        }
        g_curPanel = c;
        return;
    }
    u16 d = (u16)(P16(c, 0x22) - 1);
    P16(c, 0x22) = d;
    if (d == 0) {
        n_hud_AllocPanel();
        s16 i = (s16)V0;
        if (i == -1) {
            P16(g_curPanel, PANEL_FLAGS) = 1;
        } else {
            u32 q = panel_addr(i);
            c = g_curPanel;
            P16(c, 0x24) = (u16)i;
            g_curPanel = q;
            P16(q, PANEL_FLAGS) |= 0x51C;
            A0 = q; A1 = 0x800A6FACu;
            n_hud_SetSprite();
            q = g_curPanel;
            P16(q, 0x10) = (u16)div_pow2(GV(s32, co + 0x38), 2);
            P16(q, 0x12) = GV(u16, co + 0x3C);
            P16(q, 0x18) = (u16)div_pow2(GV(s32, co + 0x40), 4);
            GCALL1(F_START_ANIM_A, 0x800A8024u);
        }
    }
    g_curPanel = c;
}

/* 8009FE14: returns 0 (called by enemy programs 39 and 55). */
void n_stub_ReturnZero(void) { V0 = 0; }

/* 800A0158 (x, y, z): a glint of random size. */
void n_fx_Glint(void) {
    FRAME(0x28);
    s32 x = (s32)A0, y = (s32)A1, z = (s32)A2;
    if (new_sprite(0x41C, 0x800A6FACu) == -1) return;
    u32 p = g_curPanel;
    P16(p, 0xE) = (u16)(P16(p, 0xE) + 1);
    place(p, x, y, z);
    s32 r = (s32)GCALL0(F_RANDOM8);
    P16(g_curPanel, 0x34) = (u16)(r % 128 + 0x80);
    GCALL1(F_START_ANIM_A, 0x800A7BE0u);
    P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
}

/* 800A0268 (x, y, z): a sprite rising with random speed and size, freed
 * when it's above 0x800A8258. */
void n_fx_Rise(void) {
    FRAME(0x28);
    s32 x = (s32)A0, y = (s32)A1, z = (s32)A2;
    if (new_sprite(0x40C, 0x800A7074u) == -1) return;
    place(g_curPanel, x, y, z);
    s32 r = (s32)GCALL0(F_RANDOM8);
    P16(g_curPanel, 0x30) = (u16)(r % 80 + 0x28);
    r = (s32)GCALL0(F_RANDOM8);
    P16(g_curPanel, 0x34) = (u16)(r % 64 + 0x60);
    GCALL1(F_START_ANIM_A, 0x800A81D0u);
    P32(g_curPanel, PANEL_UPDATE) = 0x800A03A4u;
}

/* 800A03A4: update the rising sprite. */
void n_fx_RiseUpdate(void) {
    FRAME(0x18);
    GCALL0(F_ANIM_ONCE_A);
    u32 p = g_curPanel;
    if (P16(p, PANEL_FLAGS) & 1) return;
    u16 v = (u16)(P16(p, 0x30) + 4);
    s32 d = (s16)P16(p, 0x12) - (s16)P16(p, 0x28);
    u16 b = (u16)(P16(p, 0x28) + v);
    P16(p, 0x28) = b;
    P16(p, 0x12) = (u16)(b + d);
    P16(p, 0x30) = v;
    if (!((s16)P16(p, 0x12) < GV(s32, 0x800A8258))) P16(p, PANEL_FLAGS) = 1;
}

#define MODEL_SLOTS      0x800C3A98u   /* 0xEC bytes each: +2 drawn in the world */
#define F_BIOS_RAND      0x800398A8u
#define g_shardsTimer    GV(s32, 0x800C3A1C)

/* 80099EB8 (coord, world, yaw): twelve shards (world panels drawing model
 * slot 0 or 1 at random; `world` noted in the slot) bursting from the
 * coordinate's position (+0x18, +0x1C, +0x20): spread +-1000 sideways of
 * yaw, up to 1128 above, at random size, thrown along yaw +-30 with a
 * random spin, falling (fx_ShardUpdate). The current panel is kept. */
void n_fx_Shards(void) {
    FRAME(0x78);
    u32 co = A0, world = A1;
    s32 yaw = (s16)A2;
    u32 keep = g_curPanel;
    u32 m = sp + 0x28, v = sp + 0x10, out = sp + 0x18;
    for (s32 n = 0xC;;) {
        s16 i = (s16)GCALL0(0x80092E3Cu);
        if (i == -1) break;
        u32 p = panel_addr(i);
        g_curPanel = p;
        P16(p, 0x34) = 0x100;
        P16(p, PANEL_FLAGS) |= 0x424;
        u32 slot = (GCALL0(F_BIOS_RAND) >> 3) & 1;
        P16(g_curPanel, 0x22) = (u16)slot;
        GV(u8, MODEL_SLOTS + slot * 0xEC + 2) = (u8)world;
        copy_words(m, IDENTITY_MATRIX, 8);
        s32 r = (s32)GCALL0(F_RANDOM8);
        GS(u16, v + 2) = 0;
        GS(u16, v + 4) = 0;
        GS(u16, v + 0) = (u16)(r % 2000 - 0x3E8);
        GCALL2(F_ROT_MATRIX_Y, (u32)yaw, m);
        GCALL3(F_APPLY_MATRIX, m, v, out);
        P16(g_curPanel, 0x10) = (u16)((s32)(GS(u32, co + 0x18) + GS(u32, out + 0)) / 4);
        r = (s32)GCALL0(F_BIOS_RAND);
        p = g_curPanel;
        P16(p, 0x12) = (u16)(GS(u16, co + 0x1C) + 0x230 - (u32)(r % 1128));
        P16(p, 0x18) = (u16)((s32)(GS(u32, co + 0x20) + GS(u32, out + 8)) / 16);
        r = (s32)GCALL0(F_BIOS_RAND);
        P16(g_curPanel, 0x34) = (u16)(r % 56 + 0xC0);
        r = (s32)GCALL0(F_BIOS_RAND);
        copy_words(m, IDENTITY_MATRIX, 8);
        GS(u32, v + 0) = 0;
        GS(u16, v + 4) = (u16)(-(r % 74) - 0x17);
        r = (s32)GCALL0(F_BIOS_RAND);
        GCALL2(F_ROT_MATRIX_Y, (u32)(yaw + r % 60 - 0x1E) & 0xFFF, m);
        GCALL3(F_APPLY_MATRIX, m, v, out);
        p = g_curPanel;
        P16(p, 0x2E) = GS(u16, out + 0);
        P16(p, 0x32) = GS(u16, out + 8);
        r = (s32)GCALL0(F_BIOS_RAND);
        p = g_curPanel;
        P32(p, PANEL_UPDATE) = 0x8009A7D4u;
        P16(p, 0x30) = (u16)(r % 180 - 0x78);
        r = (s32)GCALL0(F_BIOS_RAND);
        P16(g_curPanel, 0x24) = (u16)((r >> 2) & 0xFFF);
        r = (s32)GCALL0(F_BIOS_RAND);
        P16(g_curPanel, 0x26) = (u16)((r >> 2) & 0xFFF);
        r = (s32)GCALL0(F_BIOS_RAND);
        n--;
        P16(g_curPanel, 0x2C) = (u16)((r % 7) << 1);
        if (n <= 0) break;
        LOOP_POLL(0x8009A2CCu);
    }
    g_curPanel = keep;
}

/* 8009A310 (box, side): shards from the middle of a box's two corners
 * (s16 x, y, z at +0 and +8, in the coordinates at +0x30), thrown across
 * its longer side away from the player (side: the other way, and not in
 * the world); the shards' timer 16. */
void n_fx_ShardsAt(void) {
    FRAME(0xE0);
    u32 b = A0, side = A1;
    u32 c1 = sp + 0x10, c2 = sp + 0x60, ls = sp + 0xB0;
    GS(s32, c1 + 0x18) = GV(s16, b + 0);
    GS(s32, c1 + 0x1C) = GV(s16, b + 2);
    GS(u16, c1 + 0x48) = 0;
    GS(u16, c1 + 0x46) = 0;
    GS(u16, c1 + 0x44) = 0;
    GS(s32, c1 + 0x20) = GV(s16, b + 4);
    GS(u32, c1 + 0) = 0;
    GS(u32, c1 + 0x4C) = GV(u32, b + 0x30);
    GCALL2(F_GS_GET_LS, c1, ls);
    GS(s32, c2 + 0x18) = GV(s16, b + 8);
    GS(s32, c2 + 0x1C) = GV(s16, b + 0xA);
    GS(u16, c2 + 0x48) = 0;
    GS(u16, c2 + 0x46) = 0;
    GS(u16, c2 + 0x44) = 0;
    GS(s32, c2 + 0x20) = GV(s16, b + 0xC);
    GS(u32, c2 + 0) = 0;
    GS(u32, c2 + 0x4C) = GV(u32, b + 0x30);
    GCALL2(F_GS_GET_LS, c2, ls);
    s32 mx = (s32)(GS(u32, sp + 0x48) + GS(u32, sp + 0x98)) / 2;
    GS(s32, sp + 0x28) = mx;
    GS(s32, sp + 0x2C) = (s32)(GS(u32, sp + 0x4C) + GS(u32, sp + 0x9C)) / 2;
    s32 mz = (s32)(GS(u32, sp + 0x50) + GS(u32, sp + 0xA0)) / 2;
    GS(s32, sp + 0x30) = mz;
    s32 dx = GV(s16, b + 0) - GV(s16, b + 8);
    if (dx < 0) dx = -dx;
    s32 dz = GV(s16, b + 4) - GV(s16, b + 0xC);
    if (dz < 0) dz = -dz;
    u32 yaw;
    if (dx < dz) yaw = mx < GV(s32, 0x800A8650) ? 0x400 : 0xC00;
    else yaw = (u32)!(mz < GV(s32, 0x800A8658)) << 11;
    u32 world = 1;
    if (side != 0) { world = 0; yaw ^= 0x800; }
    GCALL4(0x80099EB8u, c1, world, yaw, 0);
    g_shardsTimer = 0x10;
}

/* 8009A7D4: a shard's frame: falls (gravity 0x16 on +0x30), moves by its
 * speed (x / 4, z / 16) and spins (table 0x800A82FC by +0x2C); on the
 * floor (y above 0) it becomes a still sprite (template 0x800A6FD4, 2/5
 * of its size, animation 0x800A77F0, drawn in the world if its slot
 * says so). */
void n_fx_ShardUpdate(void) {
    FRAME(0x28);
    u32 p = g_curPanel;
    P16(p, 0x30) = (u16)(P16(p, 0x30) + 0x16);
    s32 vx = (s16)P16(p, 0x2E) / 4;
    s32 vz = (s16)P16(p, 0x32);
    P16(p, 0x10) = (u16)(P16(p, 0x10) + (u32)vx);
    P16(p, 0x12) = (u16)(P16(p, 0x12) + P16(p, 0x30));
    P16(p, 0x18) = (u16)(P16(p, 0x18) + (u32)(vz / 16));
    u32 spin = 0x800A82FCu + (u32)((s32)(s16)P16(p, 0x2C) * 2);
    P16(p, 0x24) = (u16)((P16(p, 0x24) + GV(u16, spin)) & 0xFFF);
    P16(p, 0x26) = (u16)((P16(p, 0x26) + GV(u16, spin + 2)) & 0xFFF);
    if ((s16)P16(p, 0x12) <= 0) return;
    u16 f = (u16)((P16(p, PANEL_FLAGS) | 0x41C) & 0xFFDF);
    P16(p, PANEL_FLAGS) = f;
    if (GV(u8, MODEL_SLOTS + (u32)((s32)(s16)P16(p, 0x22) * 0xEC) + 2) != 0) P16(p, PANEL_FLAGS) = f | 0x800;
    u32 q = g_curPanel;
    u32 size = P16(q, 0x34);
    GCALL2(0x800945B4u, q, 0x800A6FD4u);
    q = g_curPanel;
    P16(q, 0x12) = 0;
    P16(q, 0x34) = (u16)((s32)(size * 2) / 5);
    GCALL1(F_START_ANIM_A, 0x800A77F0u);
    P32(g_curPanel, PANEL_UPDATE) = F_ANIM_ONCE_A;
}

/* ---- feathers (fx_Hit kind 11: a bird hit) ---- */
void n_fx_Feathers(void);
void n_fx_FeatherSway(void);
void n_fx_FeatherUpdate(void);

#define F_RSIN           0x80029FF8u
#define F_RCOS           0x8002A0C8u
#define FEATHER_SPRITE   0x800A6FE8u
#define FEATHER_FRAMES_DOWN 0x800A8318u        /* swing frame by angle / 256 */
#define FEATHER_FRAMES_UP   0x800A8328u
#define SPRITE_FRAMES    0x800A77F8u           /* 12 bytes: u, v, _, _, w, h */

/* 8009B3F8 (out, angle): the feather's sway offset for its swing angle:
 * (cos, sin) x 75/1024 turned to the camera's facing, into the vector
 * out. */
void n_fx_FeatherSway(void) {
    FRAME(0x48);
    u32 out = A0, ang = A1;
    u32 m = sp + 0x10, v = sp + 0x30;
    copy_words(m, IDENTITY_MATRIX, 8);
    GCALL2(F_ROT_MATRIX_Y, (u32)(s32)GV(s16, 0x800A8606), m);
    u32 c = GCALL1(F_RCOS, ang);
    GS(u16, v + 0) = (u16)((c * 75) >> 10);
    u32 s = GCALL1(F_RSIN, ang);
    GS(u16, v + 2) = (u16)((s * 75) >> 10);
    GS(u16, v + 4) = 0;
    GCALL3(F_APPLY_MATRIX, m, v, out);
}

/* 8009B0CC (obj, ...): 3-6 feathers at obj's position (+0x18), each
 * swinging (+0x2A angle, +0x2C direction, +0x38 speed) and drifting
 * (+0x2E, +0x30, +0x32) down; fx_FeatherUpdate moves them. */
void n_fx_Feathers(void) {
    FRAME(0x70);
    u32 obj = A0;
    s32 n = (s32)GCALL0(F_RAND) % 4 + 3;
    s32 x = div_pow2((s32)GS(u32, obj + 0x18), 2);
    u32 y = GS(u32, obj + 0x1C);
    s32 z = div_pow2((s32)GS(u32, obj + 0x20), 4);
    u32 m = sp + 0x10, d = sp + 0x38;
    if (n <= 0) return;
    for (;;) {
        n_hud_AllocPanel();
        s16 i = (s16)V0;
        if (i == -1) { V0 = (u32)-1; return; }
        g_curPanel = panel_addr(i);
        A0 = g_curPanel; A1 = FEATHER_SPRITE;
        n_hud_SetSprite();
        u32 p = g_curPanel;
        u16 fl = P16(p, PANEL_FLAGS);
        P16(p, 0x34) = 0x40;
        P16(p, 0x22) = (u16)x; P16(p, 0x10) = (u16)x;
        P16(p, 0x24) = (u16)y; P16(p, 0x12) = (u16)y;
        P16(p, 0x26) = (u16)z; P16(p, 0x18) = (u16)z;
        P16(p, PANEL_FLAGS) = fl | 0x418;
        s32 r = (s32)GCALL0(F_RAND);
        p = g_curPanel;
        s32 a = r % 0x800;
        P16(p, 0x2A) = (u16)a;
        s32 sn = (s32)GCALL1(F_RSIN, (u32)(s32)(s16)a);
        p = g_curPanel;
        P16(p, 0x24) = (u16)(P16(p, 0x24) - (u16)((s32)((u32)sn * 800) >> 13));
        r = (s32)GCALL0(F_RAND);
        p = g_curPanel;
        P16(p, 0x2C) = (u16)((r >> 5) & 1);
        P32(p, PANEL_UPDATE) = 0x8009B4DCu;
        P16(p, 0x28) = 0xFFFF;
        copy_words(m, IDENTITY_MATRIX, 8);
        r = (s32)GCALL0(F_RAND);
        GCALL2(F_ROT_MATRIX_Y, (u32)(r % 0x1000), m);
        GS(u32, sp + 0x30) = 0;
        GS(u16, sp + 0x34) = 0x1E;
        GCALL3(F_APPLY_MATRIX, m, sp + 0x30, d);
        p = g_curPanel;
        P16(p, 0x2E) = GS(u16, d);
        r = (s32)GCALL0(F_RAND);
        p = g_curPanel;
        P16(p, 0x30) = (u16)(r % 20 + 0x14);
        P16(p, 0x32) = GS(u16, d + 8);
        r = (s32)GCALL0(F_RAND);
        p = g_curPanel;
        P32(p, 0x38) = (u32)(r % 48 + 0x48);
        A0 = d; A1 = (u32)(s32)(s16)P16(p, 0x2A);
        n_fx_FeatherSway();
        p = g_curPanel;
        P16(p, 0x22) = (u16)(P16(p, 0x22) - (u16)div_pow2((s32)GS(u32, d), 2));
        P16(p, 0x24) = (u16)(P16(p, 0x24) - GS(u16, d + 4));
        u32 nz = (u32)P16(p, 0x26) - (u32)div_pow2((s32)GS(u32, d + 8), 4);
        P16(p, 0x26) = (u16)nz;
        if (--n <= 0) { V0 = nz; return; }
        RT_POLL_NOSAVE(0x8009B3C4);
    }
}

/* 8009B4DC: a feather, every frame: swing (the angle +0x2A by +0x38 down
 * to 0, then up to 0x800, and so on), drift down by +0x30 (freed once
 * below y 0), its sprite frame by the angle, and its position the drift
 * plus the sway. */
void n_fx_FeatherUpdate(void) {
    FRAME(0x30);
    u32 p = g_curPanel;
    s32 a = (s16)P16(p, 0x2A);
    u32 frames;
    P16(p, PANEL_FLAGS) |= 4;
    if ((s16)P16(p, 0x2C) == 0) {
        a = (s32)((u32)a - P32(p, 0x38));
        if (a <= 0) { a = 0; P16(p, 0x2C) = 1; }
        frames = FEATHER_FRAMES_DOWN;
    } else {
        a = (s32)((u32)a + P32(p, 0x38));
        if (a >= 0x800) { a = 0x800; P16(p, 0x2C) = 0; }
        frames = FEATHER_FRAMES_UP;
    }
    P16(p, 0x2A) = (u16)a;
    u32 sway = sp + 0x10;
    A0 = sway; A1 = (u32)a;
    n_fx_FeatherSway();
    p = g_curPanel;
    u16 y = (u16)(P16(p, 0x24) + P16(p, 0x30));
    P16(p, 0x24) = y;
    y = (u16)(y + GS(u16, sway + 4));
    P16(p, 0x12) = y;
    if ((s16)y >= 0) { P16(p, PANEL_FLAGS) = 1; V0 = 1; return; }
    s32 k = (a == 0x800 ? 0x7FF : a) >> 8;
    u32 ent = frames + (u32)k * 2;
    s16 f = GV(s16, ent);
    if (f != (s16)P16(p, 0x28)) {
        P16(p, 0x28) = GV(u16, ent);
        u32 fr = SPRITE_FRAMES + (u32)(s32)f * 12;
        P16(p, 0x4) = (u16)(GV(u16, fr + 0) + ((GV(u16, 0x800A6F5Cu + 0x90) & 0x3F) << 2));
        P16(p, 0x6) = (u16)(GV(u16, 0x800A6F5Cu + 0x92) + GV(u16, fr + 2));
        u16 w = GV(u16, fr + 8), h = GV(u16, fr + 0xA);
        u32 scale = P16(p, 0x34);
        P16(p, 0x8) = w;
        P16(p, 0xA) = h;
        P16(p, 0x14) = (u16)((s32)((u32)w * scale) >> 8);
        P16(p, 0x16) = (u16)((s32)((u32)h * scale) >> 8);
    }
    u16 px = (u16)(P16(p, 0x22) + (u16)div_pow2((s16)P16(p, 0x2E), 2));
    P16(p, 0x22) = px;
    P16(p, 0x10) = (u16)(px + (u16)div_pow2((s32)GS(u32, sway), 2));
    u32 pz = (u32)P16(p, 0x26) + (u32)div_pow2((s16)P16(p, 0x32), 4);
    P16(p, 0x26) = (u16)pz;
    V0 = pz + (u32)div_pow2((s32)GS(u32, sway + 8), 4);
    P16(p, 0x18) = (u16)V0;
}
