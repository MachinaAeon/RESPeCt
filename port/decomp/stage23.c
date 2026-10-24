/* Stage program 23 (STAGPROG 23, loaded in the stage slot at 0x800F2C50:
 * stage 8): the per-frame room hook, room 0's breakable things and its
 * ambush.
 *
 * Room 0 has twelve breakable things (0x5C-byte records from 0x800F4FEC,
 * event flags 0xB-0x16): while the player fires (by the pad type's own
 * rule), each one not yet broken that the shot reaches (stage23_TargetHit)
 * is broken (stage23_TargetBreak). Its ambush (event flag 0x2A: when the
 * player comes close or shoots) runs a script whose op (stage23_AmbushOp)
 * shatters two more with a flash and wakes the zombies lurking by them
 * (zombie_LurkStep); the music goes back once the room's flags say so.
 * Hanging pieces swing back when shot (stage23_DebrisFrame). Room 2 sets
 * the player's height (y) from the floor; room 3 resets three room flags
 * once, when event flag 0x2E is set.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"
#include "poll.h"

void n_stage23_RoomFrame(void);
void n_stage23_TargetHit(void);
void n_stage23_TargetBreak(void);
void n_stage23_MusicBack(void);
void n_stage23_AmbushCheck(void);
void n_stage23_AmbushOp(void);
void n_stage23_DebrisFrame(void);

#define g_lastButtons    GV(u16, 0x800F543C)   /* D_800AFCB0 last frame */
#define TARGETS          0x800F4FECu           /* 12 x 0x5C-byte records */
#define TARGET_SIZE      0x5C
#define ENEMIES          GV(u32, 0x800AB510)   /* 0x8C4-byte records */
#define ENEMY_COUNT      GV(s32, 0x800AF530)
#define ENEMY_SIZE       0x8C4u
#define TARGET_RANGES    0x800F4FB4u           /* 6 x s16 target ids: used for 0-5 and again for 6-11 */
#define PATCH_PLACES     0x800F4F84u           /* u16 x, y per target: where its broken picture goes */
#define F_TARGET_HIT     0x800F36E8u           /* stage23_TargetHit (target, id) */
#define F_TARGET_BREAK   0x800F39ACu           /* stage23_TargetBreak (index, flag, debris mode) */
#define F_IN_REACH       0x800F34E0u           /* (target): ovl23, not decompiled */
#define F_IN_VIEW        0x800F32DCu           /* (target): ovl23, not decompiled */
#define F_AMBUSH_CHECK   0x800F3ED0u           /* stage23_AmbushCheck */
#define F_MUSIC_BACK     0x800F3B48u           /* stage23_MusicBack */
#define g_targetsBusy    GV(s32, 0x800F4D38)   /* the ambush's music is on */
#define AMBUSH_SCRIPT    0x800F4FC0u
#define g_effectCur      GV(u32, 0x800C39A8)
#define F_RATAN2         0x8002BB48u
#define F_RSIN           0x80029FF8u
#define F_RCOS           0x8002A0C8u
#define F_ISQRT          0x8002A3D8u
#define F_SET_ACTION     0x800802C8u   /* (e, action, anim) */
#define F_SET_ACTION_B   0x8008035Cu   /* (e, action, anim, blend) */
#define F_RANDOM         0x80011570u
#define F_BIOS_RAND      0x800398A8u
#define IDENTITY_MATRIX  0x80065B90u

/* Is the player not firing this frame? By pad type: 0x200000 (the light
 * gun) fires with the sight on screen, DualShock / digital pads by the
 * fire button; anything else never fires. */
static u32 stage23_NotFiring(void) {
    u32 t = g_padTypeBit;
    if (t == 0x200000) {
        if (g_fireRequest == 0) return 1;
        if (g_reticleX < 0) return 1;
        return (u32)g_reticleY >> 31;
    }
    if (t == 0x400000 || t == 0x80000) return g_fireRequest == 0;
    return 1;
}

/* 800F3BD4: every frame. Returns 0. */
void n_stage23_RoomFrame(void) {
    FRAME(0x28);
    switch (g_room) {
    case 0: {
        u32 idle = stage23_NotFiring();
        s16 weapon = GV(s16, g_currentWeaponAddr);
        if (weapon < 9 && weapon >= 7) {
            idle = g_weaponFiring == -1;
        } else {
            if (g_lastButtons & 4) idle = 1;
            g_lastButtons = D_800AFCB0;
        }
        if (idle == 0) {
            for (s32 i = 0; i < 12; i++) {
                u32 flag = (u32)i + 0xB;
                if (GCALL2(F_FLAG_TEST, EVENT_FLAGS, flag) != 0) continue;
                s32 k = i < 6 ? i : i - 6;
                if (GCALL2(F_TARGET_HIT, TARGETS + (u32)i * 0x5C, (s32)GV(s16, TARGET_RANGES + (u32)k * 2)) == 0)
                    continue;
                GCALL3(F_TARGET_BREAK, i, flag, 0xE);
            }
        }
        GCALL0(F_AMBUSH_CHECK);
        if (g_targetsBusy == 0) GCALL0(F_MUSIC_BACK);
        break;
    }
    case 2:
        GV(s32, 0x800A85DC) = GV(s16, 0x800AF56A) != 0 ? (s32)GV(s16, 0x800B4D6A) : 0;
        break;
    case 3:
        if (GCALL2(F_FLAG_TEST, EVENT_FLAGS, 0x29) != 0) break;
        if (GCALL2(F_FLAG_TEST, EVENT_FLAGS - 0x20, 0x2E) == 0) break;
        GCALL2(F_FLAG_CLEAR, EVENT_FLAGS - 0x60, 0x77);
        GCALL2(F_FLAG_CLEAR, EVENT_FLAGS - 0x60, 0x78);
        GCALL2(F_FLAG_CLEAR, EVENT_FLAGS - 0x60, 0x79);
        GCALL2(F_FLAG_SET, EVENT_FLAGS, 0x29);
        break;
    }
    V0 = 0;
}

/* 800F36E8 (target, id): did the shot reach breakable thing `target`?
 * Weapons 7 and 8: a hit mark on its line `id` within its height. Else in
 * reach (0x800F34E0), in view (0x800F32DC) and the reticle on its box.
 * 1 or 0. */
void n_stage23_TargetHit(void) {
    FRAME(0x98);
    u32 t = A0;
    s32 id = (s16)A1;
    s16 weapon = GV(s16, g_currentWeaponAddr);
    if (weapon < 9 && weapon >= 7) {
        V0 = stage_HitMarkOn(t, id, 0x800F37A4u);
        return;
    }
    if (GCALL1(F_IN_REACH, t) == 0) { V0 = 0; return; }
    if (GCALL1(F_IN_VIEW, t) == 0) { V0 = 0; return; }
    V0 = stage_ReticleOnBox(sp, t, 0x800F3808u);
}

/* 800F39AC (i, flag, mode): break thing i: two sounds, its broken picture
 * (targets 0-5 from (0x360, 0xE0) 15 x 32, 6-11 from (0x370, 0) 15 x 96)
 * to its place, its flag, debris (mode); once both of its pair (i and
 * i + 6) are broken, its target record's depth is cleared. */
void n_stage23_TargetBreak(void) {
    FRAME(0x40);
    u32 i = A0, flag = A1, mode = A2;
    u32 list = g_targetList;
    u32 r = sp + 0x18;
    if ((s32)i < 6) stage_Rect(r, 0x360, 0xE0, 0xF, 0x20);
    else stage_Rect(r, 0x370, 0, 0xF, 0x60);
    sound_at(6, 1);
    sound_at(6, 2);
    GCALL3(F_MOVE_IMAGE, r, GV(u16, PATCH_PLACES + i * 4), GV(u16, PATCH_PLACES + (i * 2 + 1) * 2));
    GCALL1(F_DRAW_SYNC, 0);
    flag_set(EVENT_FLAGS, flag);
    GS(u32, sp + 0x10) = mode;
    GCALL4(F_EN_DRAW, TARGETS + i * TARGET_SIZE, 0, 0, 0);
    u32 j = (s32)i < 6 ? i + 6 : i;
    if (!flag_test(EVENT_FLAGS, j + 0xB)) return;
    if (!flag_test(EVENT_FLAGS, j + 5)) return;
    GV(s32, list + (u32)(s32)GV(s16, TARGET_RANGES + (j - 6) * 2) * 0x14 + 0x10) = 0;
}

/* 800F3B48: once room flags 0x72-0x75 are all set, back to music track 0
 * (g_targetsBusy = 1). */
void n_stage23_MusicBack(void) {
    FRAME(0x18);
    for (u32 f = 0x72; f <= 0x75; f++)
        if (!flag_test(ROOM_FLAGS, f)) return;
    GCALL1(F_MUSIC_STOP, (s32)g_musicTrack);
    g_musicTrack = 0;
    GCALL1(F_MUSIC_PLAY, 0);
    g_targetsBusy = 1;
}

/* 800F3ED0: room 0's ambush (event flag 0x2A, once): when the player
 * comes within 2000 of (-3000, 500), or shoots with the sight on screen,
 * the ambush script (0x800F4FC0) starts: on script slot 1 once an enemy
 * is dead, else on slot 0. */
void n_stage23_AmbushCheck(void) {
    FRAME(0x38);
    u32 e = ENEMIES;
    if (flag_test(EVENT_FLAGS, 0x2A)) return;
    u32 dx = (u32)-0xBB8 - (u32)PLAYER_POS[0];
    u32 dz = 0x1F4u - (u32)PLAYER_POS[2];
    GS(s32, sp + 0x10) = -0xBB8;
    GS(s32, sp + 0x14) = 0;
    GS(s32, sp + 0x18) = 0x1F4;
    if ((s32)GCALL1(F_ISQRT, dx * dx + dz * dz) >= 0x7D0) {
        if (g_fireRequest == 0) return;
        if (g_reticleX < 0) return;
        if (g_reticleY < 0) return;
    }
    if (flag_test(EVENT_FLAGS, 0x2A)) return;
    for (s32 i = 0; i < ENEMY_COUNT; i++, e += ENEMY_SIZE) {
        if (i) LOOP_POLL(0x800F3FECu);
        if (GV(s16, e + 0x31E) > 0) continue;
        if (GCALL2(F_EVENT_SCRIPT, 1, AMBUSH_SCRIPT) != 0) goto start;
    }
    if (GCALL2(F_EVENT_SCRIPT, 0, AMBUSH_SCRIPT) == 0) return;
start:
    flag_set(EVENT_FLAGS, 0x2A);
}

/* 800F403C (op): the ambush script's op, by its step (+4): 0, the flash:
 * unless both are broken, things 8 and 9 (event flags 0x13, 0x14, noted
 * at +0xA, +0xC) shatter with a white flash, music track 1; 1, the
 * enemies of room ids 0x72 / 0x73 wake (action 4, health 200, a random
 * wait 150-405), the animation by whether their thing was already broken;
 * 2, done (the script on 4 bytes, returns 0). Returns 1 while running. */
void n_stage23_AmbushOp(void) {
    FRAME(0x48);
    u32 op = A0;
    u32 e = ENEMIES;
    s16 step = GV(s16, op + 4);
    if (step == 0) {
        u32 r = sp + 0x18;
        GS(u16, r + 0) = 0x370;
        GS(u16, r + 4) = 0xF;
        GS(u16, r + 2) = 0;
        GS(u16, r + 6) = 0x60;
        GV(u16, op + 0xA) = (u16)flag_test(EVENT_FLAGS, 0x13);
        GV(u16, op + 0xC) = (u16)flag_test(EVENT_FLAGS, 0x14);
        if (!flag_test(EVENT_FLAGS, 0x13) || !flag_test(EVENT_FLAGS, 0x14)) {
            sound_at(6, 1);
            sound_at(6, 2);
            screen_flash();
            for (u32 k = 0; k < 2; k++) {
                if (k) LOOP_POLL(0x800F41D8u);
                u32 f = 8 + k + 0xB;
                if (flag_test(EVENT_FLAGS, f)) continue;
                GCALL3(F_MOVE_IMAGE, r, GV(u16, PATCH_PLACES + 0x20 + k * 4), GV(u16, PATCH_PLACES + 0x22 + k * 4));
                GCALL1(F_DRAW_SYNC, 0);
                flag_set(EVENT_FLAGS, f);
                GS(u32, sp + 0x10) = 0x11;
                GCALL4(F_EN_DRAW, TARGETS + (8 + k) * TARGET_SIZE, 0, 0, 0);
            }
        }
        GCALL1(F_MUSIC_STOP, 0);
        g_musicTrack = 1;
        GCALL1(F_MUSIC_PLAY, 1);
        g_targetsBusy = 0;
    } else if (step == 1) {
        for (s32 i = 0; i < ENEMY_COUNT; i++, e += ENEMY_SIZE) {
            if (i) LOOP_POLL(0x800F42E4u);
            if ((u32)(GV(u16, e + 0x850) - 0x72) >= 2) continue;
            s16 id = GV(s16, e + 0x850);
            GV(u16, e + 0x316) = 1;
            if (id == 0x72) {
                if (GV(s16, op + 0xA) != 0) GCALL3(F_SET_ACTION, e, 4, 2);
                else GCALL4(F_SET_ACTION_B, e, 4, 0xB, 2);
            } else if (id == 0x73) {
                if (GV(s16, op + 0xC) != 0) GCALL3(F_SET_ACTION, e, 4, 2);
                else GCALL4(F_SET_ACTION_B, e, 4, 0xA, 2);
            }
            GV(u16, e + 0x31E) = 0xC8;
            GV(u16, e + 0x852) = 0;
            GV(u32, e + 0x370) = (GCALL0(F_RANDOM) & 0xFF) + 0x96;
        }
    } else if (step == 2) {
        GV(u32, op + 0) += 4;
        V0 = 0;
        return;
    } else {
        V0 = 1;
        return;
    }
    GV(u16, op + 4) = (u16)(GV(u16, op + 4) + 1);
    V0 = 1;
}

/* One axis of a debris piece's bounce: the position (p) moves back by the
 * speed (v) / 16; the speed changes by the damping (d) towards the rest
 * point, and once the piece is past it the bounce shrinks (d = the speed
 * left / 16) or ends. `odd` is the first axis's quirk: ending a bounce
 * from the positive side clears the third axis's speed (+0x32), not its
 * own. */
static void stage23_bounce(u32 r, u32 p, u32 v, u32 d, int odd) {
    u16 du = GV(u16, r + d);
    if ((s16)du == 0) {
        GV(u16, r + p) = 0;
        GV(u16, r + v) = 0;
        return;
    }
    if (GV(s16, r + p) >= 0) {
        u16 nv = (u16)(GV(u16, r + v) - du);
        GV(u16, r + v) = nv;
        if (GV(s16, r + p) + ((s16)nv >> 4) > 0) return;
        nv = (u16)(nv + (u16)(GV(u16, r + d) << 1));
        GV(u16, r + v) = nv;
        if ((s16)nv < 0) {
            GV(u16, r + d) = (u16)(0 - ((s16)nv >> 4));
            return;
        }
        GV(u16, r + (odd ? 0x32 : v)) = 0;
        GV(u16, r + p) = 0;
        GV(u16, r + d) = 0;
    } else {
        u16 nv = (u16)(GV(u16, r + v) + du);
        GV(u16, r + v) = nv;
        if (GV(s16, r + p) + ((s16)nv >> 4) < 0) return;
        nv = (u16)(nv - (u16)(GV(u16, r + d) << 1));
        GV(u16, r + v) = nv;
        if ((s16)nv > 0) {
            GV(u16, r + d) = (u16)((s16)nv >> 4);
            return;
        }
        GV(u16, r + v) = 0;
        GV(u16, r + p) = 0;
        GV(u16, r + d) = 0;
    }
}

/* A kick on one axis (speed v, damping d, at most `lim`): the speed by
 * dv, the damping by dd, the excess over lim taken back off the speed. */
static void stage23_kick(u32 r, u32 v, u32 d, s32 dv, s32 dd, s32 lim) {
    if (GV(s16, r + d) >= lim) return;
    GV(u16, r + v) = (u16)(GV(u16, r + v) + (u32)dv);
    GV(u16, r + d) = (u16)(GV(u16, r + d) + (u32)dd);
    s32 n = GV(s16, r + d);
    if (n < lim) return;
    s32 k = n - lim;
    if (dv > 0) GV(u16, r + v) = (u16)(GV(u16, r + v) - ((u32)k << 4));
    else GV(u16, r + v) = (u16)(GV(u16, r + v) + ((u32)k << 4));
}

/* 800F4434: a hanging piece's frame (the effect record g_effectCur): its
 * offset (+0x24, +0x26, +0x28) swings back by its speeds (+0x2E, +0x30,
 * +0x32) with damping (+0x2A, +0x1E, +0x2C). When shot (+0x1C bit 14):
 * the reticle tested against six slices of its box, the lowest hit (or
 * the last) setting the strength, and it is kicked away from the player
 * (sideways at random). */
void n_stage23_DebrisFrame(void) {
    FRAME(0xE0);
    u32 r = g_effectCur;
    GV(u16, r + 0x24) += (u16)((s16)GV(u16, r + 0x2E) >> 4);
    s32 a0 = (s16)GV(u16, r + 0x30) >> 4;
    GV(u16, r + 0x26) += (u16)a0;
    GV(u16, r + 0x28) += (u16)((s16)GV(u16, r + 0x32) >> 4);
    stage23_bounce(r, 0x24, 0x2E, 0x2A, 1);
    stage23_bounce(g_effectCur, 0x26, 0x30, 0x1E, 0);
    stage23_bounce(g_effectCur, 0x28, 0x32, 0x2C, 0);
    r = g_effectCur;
    if (GV(u16, r + 0x1C) & 0x4000) {
        for (u32 k = 0; k < 0x20; k += 4) GS(u32, sp + 0x20 + k) = GV(u32, IDENTITY_MATRIX + k);
        for (u32 k = 0; k < 0x20; k += 4) GS(u32, sp + 0x74 + k) = GV(u32, IDENTITY_MATRIX + k);
        GS(s32, sp + 0x88) = (s32)GV(s16, r + 0x10) << 2;
        GS(s32, sp + 0x8C) = GV(s16, r + 0x12);
        GS(s32, sp + 0x90) = (s32)GV(s16, r + 0x18) << 4;
        GS(u16, sp + 0xB4) = GV(u16, r + 0x24);
        GS(u16, sp + 0xB6) = GV(u16, r + 0x26);
        u16 z = GV(u16, r + 0x28);
        GS(u32, sp + 0x40) = sp + 0x70;
        GS(s16, sp + 0x10) = -0x1BF;
        GS(s16, sp + 0x18) = 0x1BB;
        GS(s16, sp + 0x1A) = 0x19B;
        GS(s16, sp + 0x14) = -0x195;
        GS(u32, sp + 0xBC) = 0;
        GS(u32, sp + 0x70) = 0;
        GS(u16, sp + 0x12) = 0;
        GS(s16, sp + 0x1C) = 0x1A3;
        GS(u16, sp + 0xB8) = z;
        s32 n = 0;
        for (;;) {
            gcall(F_HITBOX_PROJECT, sp + 0x10, 0, 0, 0);
            if (GCALL1(F_HITBOX_RETICLE, sp + 0x10) != 0) break;
            n++;
            u16 y = GS(u16, sp + 0x1A);
            GS(u16, sp + 0x12) = y;
            GS(u16, sp + 0x1A) = (u16)(y + 0x19B);
            if (n >= 6) break;
            LOOP_POLL(0x800F48ACu);
        }
        if (n == 6) n = 5;
        u32 c = g_effectCur;
        u32 ay = ((u32)(s32)GV(s16, c + 0x18) * 16 - (u32)GV(s32, 0x800A8658)) << 12;
        u32 ax = ((u32)GV(s32, 0x800A8650) - (u32)(s32)GV(s16, c + 0x10) * 4) << 12;
        s32 ang = (s32)GCALL2(F_RATAN2, ay, ax);
        s32 str = (s32)((u32)(n + 1) << 7) / 6;
        if (ang < 0) ang = -ang;
        s32 a = (s16)ang;
        s32 sv = (s32)GCALL1(F_RSIN, a);
        s32 dx = -((s32)((u32)str * (u32)sv) >> 12);
        s32 ddx = dx >> 4;
        if (ddx < 0) ddx = -ddx;
        s32 cv = (s32)GCALL1(F_RCOS, a);
        s32 m = (s32)((u32)str * (u32)cv);
        s32 dz = m >> 12;
        s32 ddz = m >> 16;
        if (ddz < 0) ddz = -ddz;
        s32 rnd = (s32)GCALL0(F_BIOS_RAND);
        s32 dy = (s32)((u32)(rnd % str) << 1) - str;
        s32 ddy = dy >> 4;
        if (ddy < 0) ddy = -ddy;
        stage23_kick(g_effectCur, 0x2E, 0x2A, dx, ddx, 0x10);
        stage23_kick(g_effectCur, 0x32, 0x2C, dz, ddz, 0x10);
        stage23_kick(g_effectCur, 0x30, 0x1E, dy, ddy, 8);
    }
    r = g_effectCur;
    GV(u16, r + 0x1C) &= 0xBFFF;
    V0 = GV(u16, r + 0x1C);
}
