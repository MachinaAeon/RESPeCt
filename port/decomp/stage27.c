/* Stage program 27 (STAGPROG overlay 27: stage 12): the per-frame room
 * hook (rooms 2, 5, 8 and 10) and its "not shooting" test.
 *
 * Rooms 2, 5 and 10 have breakable things (0x5C-byte records from
 * 0x800F5784, the same records numbered on from room to room): while the
 * player shoots, each one not yet broken (its event flag) that the shot
 * reached (stage27_TargetHit) breaks: two sounds, its VRAM patch copied over the
 * intact picture (MoveImage), the flag, debris (F_EN_DRAW) and, for some,
 * its target record's depth changed. Room 2 also sets its light levels by
 * an event flag, room 5 shows a message once two flags are set, room 8
 * cycles a palette.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"
#include "poll.h"

void n_stage27_Room5Clear(void);
void n_stage27_InReach(void);
void n_stage27_TargetHit(void);
void n_stage27_NotFiring(void);
void n_stage27_RoomFrame(void);

#define g_lastButtons   0x800F5960u           /* u16 */
#define g_lightIdx      GV(u16, 0x800F5956)   /* s16, room 8 */
#define g_room5Message  GV(u8, 0x800F576C)
#define TARGETS         0x800F5784u           /* 0x5C-byte records */
#define TARGET_SIZE     0x5C
#define F_TARGET_HIT    0x800F3BE4u           /* stage27_TargetHit (target, target id) */
#define F_NOT_FIRING    0x800F3EB8u
#define F_ROOM8_FRAME   0x800F4978u           /* (0); ovl27 */
#define F_ROOM10_FRAME  0x800F4704u           /* ovl27 */

/* The room's collision lines: count (byte) and records (0x14 bytes: s32
 * x0, z0, x1, z1, depth). */
#define LINE_COUNT      GV(u8, 0x800B60D0)
#define LINE_LIST       GV(u32, 0x800B60D4)
#define EYE_POS         0x800A85D8u           /* where shots come from */
#define F_ROT_STEP      0x8006F7A8u           /* rot_Step (rot, out, n) */
#define F_SEGMENTS_CROSS_XZ 0x8007C290u

static s32 sign_of(s32 x) { return mips_div(x, x < 0 ? -x : x); }

/* 800F37E4 (target): room 5: is the way from the player to the middle of
 * the target's edge (s16 x, z at +0/+4 and +8/+0xC) clear of the room's
 * lines (none crossing it from its far side)? 1 clear, 0 not. */
void n_stage27_Room5Clear(void) {
    FRAME(0x80);
    u32 t = A0;
    const u32 p = sp + 0x10, m = sp + 0x20, a = sp + 0x30, b = sp + 0x40;
    GS(s32, sp + 0x14) = 0;
    GS(u32, p) = GV(u32, PLAYER_POS_ADDR);
    GS(u32, p + 8) = GV(u32, PLAYER_POS_ADDR + 8);
    GS(s32, m + 4) = 0;
    GS(s32, m) = (GV(s16, t) + GV(s16, t + 8)) >> 1;
    u32 line = LINE_LIST;
    u32 n = LINE_COUNT;
    GS(u32, sp + 0x50) = n;
    GS(s32, m + 8) = (GV(s16, t + 4) + GV(s16, t + 0xC)) >> 1;
    for (s32 k = 0; k < (s32)n;) {
        GS(u32, a) = GV(u32, line);
        GS(u32, a + 8) = GV(u32, line + 4);
        GS(u32, b) = GV(u32, line + 8);
        GS(u32, b + 8) = GV(u32, line + 0xC);
        s32 sp_ = (s32)GCALL4(F_LINE_SIDE, a, b, GS(u32, p), GS(u32, p + 8));
        s32 sm = (s32)GCALL4(F_LINE_SIDE, a, b, GS(u32, m), GS(u32, m + 8));
        s32 sa = (s32)GCALL4(F_LINE_SIDE, p, m, GS(u32, a), GS(u32, a + 8));
        s32 sb = (s32)GCALL4(F_LINE_SIDE, p, m, GS(u32, b), GS(u32, b + 8));
        s32 d1 = sign_of(sp_), d2 = sign_of(sm), d3 = sign_of(sa), d4 = sign_of(sb);
        if ((s32)((u32)d1 * (u32)d2) < 0 && (s32)((u32)d3 * (u32)d4) < 0 && sp_ <= 0) {
            V0 = 0;
            return;
        }
        k++;
        line += 0x14;
        if (!(k < (s32)GS(u32, sp + 0x50))) break;
        LOOP_POLL(0x800F39A0u);
    }
    V0 = 1;
}

/* 800F39DC (target): is the target's edge in reach of the player: either
 * of two rays from the player (yaw +0x155 and -0x155, 10000 units long)
 * crossing it, or its first end between them as seen from the player, or
 * its second end between them as seen from the eye (0x800A85D8). */
void n_stage27_InReach(void) {
    FRAME(0x70);
    u32 t = A0;
    const u32 rot = sp + 0x50, r1 = sp + 0x20, r2 = sp + 0x10, e1 = sp + 0x30, e2 = sp + 0x40;
    GS(u16, rot + 4) = 0;
    GS(u16, rot) = 0;
    GS(u16, rot + 2) = (u16)(GV(u16, g_player + 0x46) + 0x155);
    GCALL3(F_ROT_STEP, rot, r1, (u32)-0x64);
    GS(u32, r1) = GV(u32, PLAYER_POS_ADDR) + GS(u32, r1) * 100;
    GS(u32, r1 + 8) = GV(u32, PLAYER_POS_ADDR + 8) + GS(u32, r1 + 8) * 100;
    GS(u16, rot + 2) = (u16)(GS(u16, rot + 2) - 0x2AA);
    GCALL3(F_ROT_STEP, rot, r2, (u32)-0x64);
    GS(u32, r2) = GV(u32, PLAYER_POS_ADDR) + GS(u32, r2) * 100;
    GS(u32, r2 + 8) = GV(u32, PLAYER_POS_ADDR + 8) + GS(u32, r2 + 8) * 100;
    GS(s32, e1 + 4) = 0;
    GS(s32, e1) = GV(s16, t);
    GS(s32, e1 + 8) = GV(s16, t + 4);
    GS(s32, e2 + 4) = 0;
    GS(s32, e2) = GV(s16, t + 8);
    GS(s32, e2 + 8) = GV(s16, t + 0xC);
    u32 pos = PLAYER_POS_ADDR;
    if (GCALL4(F_SEGMENTS_CROSS_XZ, pos, r1, e1, e2)) { V0 = 1; return; }
    if (GCALL4(F_SEGMENTS_CROSS_XZ, pos, r2, e1, e2)) { V0 = 1; return; }
    u32 x = (u32)(s32)GV(s16, t), z = (u32)(s32)GV(s16, t + 4);
    if ((s32)GCALL4(F_LINE_SIDE, pos, r1, x, z) < 0 &&
        (s32)GCALL4(F_LINE_SIDE, pos, r2, x, z) > 0) { V0 = 1; return; }
    x = (u32)(s32)GV(s16, t + 8);
    z = (u32)(s32)GV(s16, t + 0xC);
    if ((s32)GCALL4(F_LINE_SIDE, EYE_POS, r1, x, z) < 0 &&
        (s32)GCALL4(F_LINE_SIDE, EYE_POS, r2, x, z) > 0) { V0 = 1; return; }
    V0 = 0;
}

/* 800F3BE4 (target, id): did the shot reach the breakable thing? With
 * weapons 7 and 8: a hit mark on its line (id) within its height. Else:
 * in reach (and in room 5 with a clear way), and the reticle on one of
 * the 27 boxes its box (s16 x0, y0, z0, -, x1, y1, z1 from +0x10 of a
 * copy of the record) splits into, 3 by 3 by 3. 1 or 0. */
void n_stage27_TargetHit(void) {
    FRAME(0x98);
    u32 t = A0;
    s32 id = (s16)A1;
    s16 weapon = GV(s16, g_currentWeaponAddr);
    if (weapon < 9 && weapon >= 7) {
        V0 = stage_HitMarkOn(t, id, 0x800F3CA0u);
        return;
    }
    if (GCALL1(0x800F39DCu, t) == 0) { V0 = 0; return; }
    if (g_room == 5 && GCALL1(0x800F37E4u, t) == 0) { V0 = 0; return; }
    V0 = stage_ReticleOnBox(sp, t, 0x800F3D14u);
}

/* 800F3EB8 (result): *result = is the player not shooting
 * (stage_NotFiring, last buttons at 0x800F5960). */
void n_stage27_NotFiring(void) {
    V0 = stage_NotFiringResult(A0, g_lastButtons);
}

/* Breakable thing n (its event flag, target id at `id`, patch place at
 * `place`: u16 x, y): if not broken and the shot reached it, break it
 * (sounds in bank b and b+1, patch `rect`). 1 if it broke. */
static u32 target_try(u32 sp, u32 n, u32 flag, u32 id, u32 rect, u32 place, u32 bank) {
    u32 t = TARGETS + n * TARGET_SIZE;
    if (flag_test(EVENT_FLAGS, flag)) return 0;
    if (GCALL2(F_TARGET_HIT, t, (u32)(s32)GV(s16, id)) == 0) return 0;
    sound_at(6, bank);
    sound_at(6, bank + 1);
    GCALL3(F_MOVE_IMAGE, rect, GV(u16, place), GV(u16, place + 2));
    GCALL1(F_DRAW_SYNC, 0);
    flag_set(EVENT_FLAGS, flag);
    GS(u32, sp + 0x10) = 0xE;
    GCALL4(F_EN_DRAW, t, 0, 0, 0);
    return 1;
}

/* Its target record's depth (+0x10 of the 0x14-byte record). */
static void target_depth(u32 list, u32 id, s32 depth) {
    GV(s32, list + (u32)(s32)GV(s16, id) * 0x14 + 0x10) = depth;
}

/* 800F3FC4: every frame, by room. Returns 0. */
void n_stage27_RoomFrame(void) {
    FRAME(0x60);
    u32 idle = sp + 0x30;
    switch (g_room) {
    case 2:
        GCALL1(F_NOT_FIRING, idle);
        if (GS(u32, idle) == 0) {
            u32 list = g_targetList;
            stage_Rect(sp + 0x18, 0x300, 0x68, 0xF, 0x3F);
            for (u32 i = 0; i < 5; i++) {
                u32 id = 0x800F56D4u + i * 2;
                if (target_try(sp, i, 0x19 + i, id, sp + 0x18, 0x800F56C0u + i * 4, 2))
                    target_depth(list, id, -0x4F7);
            }
        }
        {
            u8 l = flag_test(EVENT_FLAGS, 0x96) ? 0x1B : 0x18;
            GV(u8, LIGHTS) = l;
            GV(u8, LIGHTS + 8) = l;
        }
        break;
    case 5:
        GCALL1(F_NOT_FIRING, idle);
        if (GS(u32, idle) == 0) {
            u32 list = g_targetList;
            u32 rect = sp + 0x20;
            stage_Rect(rect, 0x370, 0x40, 0x10, 0x40);
            target_try(sp, 0, 0x1E, 0x800F5730u, rect, 0x800F5720u, 1);
            stage_Rect(rect, 0x354, 0x80, 0x14, 0x50);
            for (u32 i = 1; i < 3; i++)
                target_try(sp, i, 0x1E + i, 0x800F5730u + i * 2, rect, 0x800F5720u + i * 4, 1);
            stage_Rect(rect, 0x370, 0x40, 0x10, 0x40);
            if (!flag_test(EVENT_FLAGS, 0x94)) {
                u32 id = 0x800F5730u + 3 * 2;
                if (GCALL2(F_TARGET_HIT, TARGETS + 3 * TARGET_SIZE, (u32)(s32)GV(s16, id)) != 0) {
                    sound_at(6, 1);
                    sound_at(6, 2);
                    GCALL3(F_MOVE_IMAGE, rect, GV(u16, 0x800F5720u + 3 * 4), GV(u16, 0x800F5722u + 3 * 4));
                    GCALL1(F_DRAW_SYNC, 0);
                    flag_set(EVENT_FLAGS, 0x94);
                    GS(u32, sp + 0x10) = 0xE;
                    GCALL4(F_EN_DRAW, TARGETS + 3 * TARGET_SIZE, 0, 0, 0);
                    target_depth(list, id, 0);
                }
            }
        }
        if (!flag_test(EVENT_FLAGS, 0x8A)) break;
        if (!flag_test(ROOM_FLAGS, 0xB3)) break;
        if (g_room5Message != 0) break;
        g_room5Message = 1;
        GV(u32, 0x800A85B8) = 0;
        GCALL2(F_MESSAGE, 0, 1);
        break;
    case 8:
        g_lightIdx = (u16)(g_lightIdx + 1);
        if ((s16)g_lightIdx >= 9) g_lightIdx = 1;
        clut_anim(0x2E0, 0xF7, (s16)g_lightIdx);
        GCALL1(F_ROOM8_FRAME, 0);
        break;
    case 10:
        GCALL0(F_ROOM10_FRAME);
        GCALL1(F_NOT_FIRING, idle);
        if (GS(u32, idle) != 0) break;
        {
            u32 list = g_targetList;
            stage_Rect(sp + 0x28, 0x2A4, 0, 0x11, 0x7F);
            for (u32 i = 0; i < 2; i++) {
                u32 id = 0x800F5760u + i * 2;
                if (target_try(sp, i, 0x21 + i, id, sp + 0x28, 0x800F5758u + i * 4, 2))
                    target_depth(list, id, -0x45D);
            }
        }
        break;
    }
    V0 = 0;
}
