/* Quick search: the turn that swings the view onto the nearest target
 * (TEST overlay, 0x80078A58-0x80079064).
 *
 * A quick search is requested with 0x100 in D_800A85B8 (the quick-search
 * button, or a GunCon press); player_Update then calls qs_Update every
 * frame while D_800A85B8 is nonzero. With no target yet (D_800A8670 == 0)
 * it picks one: the nearest live, visible enemy within range, or failing
 * that the nearest of the room's points of interest (the 0x24-byte entries
 * of the trigger list at D_800AB52C, whose count is at +0x1C), set up in a
 * scratch object record (0x800B4E28, copied to 0x800B56F8 when chosen).
 * The previous target (index in D_800B6090) is skipped while others are
 * around. qs_Aim then works out the turn: D_800A85B8 becomes the direction
 * (1 right, 2 left), D_800B4E18 the yaw step per frame, D_800B4E20 the
 * frames left, D_800B4E24 a remainder spread over the last frames,
 * D_800B4E1C the camera pitch step.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "poll.h"

#define g_qsState        GV(s32, 0x800A85B8)   /* 0 off, 0x100 requested, 1/2 turning right/left */
#define g_qsTarget       GV(u32, 0x800A8670)   /* target object (0 = choose one) */
#define g_qsYawStep      GV(s32, 0x800B4E18)
#define g_qsPitchStep    GV(s32, 0x800B4E1C)
#define g_qsFrames       GV(s32, 0x800B4E20)
#define g_qsRemainder    GV(s16, 0x800B4E24)
#define g_qsLastIndex    GV(s32, 0x800B6090)
#define g_qsTargetPoint  GV(u32, 0x800B4DF4)
#define g_qsAutoTarget   GV(s32, 0x800A13D8)   /* GunCon auto aim */
#define g_qsPressGap1    GV(s32, 0x800B4BA4)
#define g_qsPressGap2    GV(s32, 0x800B4BA8)
#define g_triggerList    GV(u32, 0x800AB52C)
#define g_objects        GV(u32, 0x800AB510)
#define g_objectCount    GV(s32, 0x800AF530)
#define g_eventFlags     GV(u16, 0x800A55A2)   /* bit 2: an event is running */
#define SCRATCH_OBJ      0x800B4E28u
#define CHOSEN_POINT     0x800B56F8u
#define OBJ_SIZE         0x8C4u
#define PLAYER_ROT_VEC   0x800A8604u
#define PLAYER_POS_Y     GV(s32, 0x800A85DC)
#define FLAGS_ITEM       0x80062FA0u
#define FLAGS_STORY      0x80062FC0u

#define F_FLAG_TEST      0x8008F64Cu
#define F_RATAN2         0x8002BB48u
#define F_PITCH_ANGLE    0x8002A278u
#define F_DISTANCE       0x8007BDDCu   /* (from, to): distance */
#define F_STEP_TOWARD    0x80078EECu   /* (rot, step, length) */
#define F_CAN_SEE        0x80079728u   /* (object, index, part): visible */

void n_qs_Aim(void);
void n_qs_Target(void);

/* 80078A58 (_, distance, target point, kind): start the turn toward the
 * point; kind 0, 1, 2 sets the speed (0 fastest). No point: stop. */
void n_qs_Aim(void) {
    FRAME(0x40);
    s32 dist = (s32)A1;
    u32 pt = A2;
    s32 kind = (s32)A3;
    if (pt == 0) {
        g_qsState = 0;
        V0 = 0x800B0000u;
        return;
    }
    s32 pitch_div;
    if (kind == 1) { pitch_div = 0x2B; g_qsYawStep = 0x61; g_qsPitchStep = 0x2B; }
    else if (kind == 0) { pitch_div = 0x36; g_qsYawStep = 0x7A; g_qsPitchStep = 0x36; }
    else { pitch_div = 0x20; g_qsYawStep = 0x49; g_qsPitchStep = 0x20; }

    u32 step = sp + 0x10;                    /* local vector */
    GCALL3(F_ROT_STEP, PLAYER_ROT_VEC, step, (u32)-0x50);
    s32 dx = (s16)(MEM_R16(pt) - GV(u16, g_player + 0x18));
    s32 sx = GS(s32, step), sz = GS(s32, step + 8);
    s32 dz = (s16)(MEM_R16(pt + 8) - GV(u16, g_player + 0x20));
    s32 side = (s32)((u32)(sz * dx) - (u32)(sx * dz));
    s32 ahead = (s32)((u32)(sx * dx) + (u32)(sz * dz));
    s32 ang = (s32)GCALL2(F_RATAN2, side, ahead);
    if (ahead == 0) ang = 0x400;
    if (side == 0) ang = 0;
    g_qsState = ang < 0 ? 2 : 1;
    if (ang < 0) ang = -ang;
    s32 per = g_qsYawStep;
    g_qsFrames = ang / per;
    g_qsRemainder = (s16)(ang % per);
    if (g_qsFrames != 0 && g_qsRemainder < 6) g_qsRemainder = 6;

    s32 h = (s32)((u32)PLAYER_POS_Y + GV(u32, CAMERA_OBJ + 0x1C) - MEM_R32(pt + 4) + 0x1F4);
    u32 a = GCALL1(F_PITCH_ANGLE, (u32)mips_div((s32)((u32)h << 12), dist));
    s32 pitch = (s32)(a + (u32)(s32)GV(s16, CAMERA_OBJ + 0x44));
    g_qsPitchStep = pitch;
    if (g_qsRemainder < 0xC) g_qsRemainder = 0;
    s32 frames = (pitch < 0 ? -pitch : pitch) / pitch_div;
    if (g_qsFrames < frames) {
        g_qsFrames = frames;
        g_qsPitchStep = pitch >= 0 ? pitch_div : -pitch_div;
        g_qsYawStep = mips_div(ang, g_qsFrames);
        g_qsRemainder = 0;
    } else if (ang != 0) {
        g_qsPitchStep = mips_div(pitch, g_qsFrames);
    } else {
        g_qsPitchStep = 0;
    }
    D_800B4DF0 = 0;
    g_qsTargetPoint = pt;
    D_800A12E8 = 1;
    V0 = 1;
}

/* 80078D38 (object, index, distance, part, [kind]): aim at a target: at
 * the given body part of a live enemy, else at the object's position. */
void n_qs_Target(void) {
    u32 obj = A0;
    s32 index = (s32)A1;
    u32 dist = A2;
    s32 part = (s32)A3;
    FRAME(0x18);
    u32 kind = GS(u32, sp + 0x28);           /* argument 5 (caller's sp + 0x10) */
    g_qsTarget = obj;
    u32 pt = 0;
    if (!(index < g_objectCount) || GV(u8, obj + 0x2A) == 0) {
        pt = obj + 0x1B8;
    } else if (GV(s16, obj + 0x392) >= 0) {
        u32 p = obj;
        for (;;) {
            if (GV(s16, p + 0x39A) == part) pt = GV(u32, p + 0x3BC) + 0x38;
            p += 0x5C;
            if (GV(s16, p + 0x392) < 0 || pt != 0) break;
            LOOP_POLL(0x80078DB4);
        }
    }
    A0 = obj; A1 = dist; A2 = pt; A3 = kind;
    n_qs_Aim();
}

/* One of the room's points of interest as a candidate, set up in the
 * scratch record: which ones can be chosen (+0x31E > 0) depends on its
 * kind (+0) and flags; far ones are pulled 8000 toward the player. */
static void setup_point(u32 sp, u32 t, s32 kind, s32 item_found) {
    u32 o = SCRATCH_OBJ;
    s16 flag3 = GV(s16, t + 0x20);
    if (kind == 3 && flag3 != -1) {
        GV(s16, o + 0x31E) = GCALL2(F_FLAG_TEST, FLAGS_STORY, (s32)flag3) ? -1 : 1;
    } else if (kind == 1 && GCALL2(F_FLAG_TEST, FLAGS_ITEM, (s32)GV(s16, t + 2))) {
        GV(s16, o + 0x31E) = -1;
    } else if (kind == 2) {
        GV(s16, o + 0x31E) = -1;
    } else if (item_found && kind != 1) {
        GV(s16, o + 0x31E) = -1;
    } else {
        GV(s16, o + 0x31E) = 1;
    }
    GV(s32, o + 0x1B8) = GV(s32, t + 4);
    GV(s32, o + 0x1BC) = kind == 4 ? GV(s32, t + 8) - 0x3E8 : GV(s32, t + 8);
    GV(s16, o + 0x1C4) = 0;
    GV(s32, o + 0x1C0) = GV(s32, t + 0xC);
    GV(s16, o + 0x1C8) = 0;
    GV(u16, o + 0x1C6) = GV(u16, t + 0x12);
    u32 d = GCALL2(F_DISTANCE, PLAYER_POS_ADDR, o + 0x1B8);
    GV(u32, o + 0x868) = d;
    if (d >= 0x7D00) {
        u32 step = sp + 0x18;                /* qs_Update's local vector */
        GCALL3(F_STEP_TOWARD, o + 0x1C4, step, 0x1F40);
        GV(s32, o + 0x1B8) += GS(s32, step);
        GV(s32, o + 0x1C0) += GS(s32, step + 8);
        GV(u32, o + 0x868) = GCALL2(F_DISTANCE, PLAYER_POS_ADDR, o + 0x1B8);
    }
    if (kind == 0) GV(s16, o + 0x31E) = -1;
}

/* 80079064: once a frame while a quick search runs: choose a target and
 * start the turn, or keep turning (yaw, camera pitch) until done. An
 * event (0x800A55A2 bit 2) stops it. */
void n_qs_Update(void) {
    FRAME(0x70);
    if (g_qsTarget == 0) {
        s32 max_dist = GV(s32, 0x800A866C) == 6 ? 0x34BC : 0x7D00;
        s32 kind = -1;
        s16 part = -1;
        u32 best_dist = (u32)-1;      /* -2 once the previous target is chosen again */
        s32 best = 0, found = 0, item_found = 0;
        u32 t = g_triggerList;
        u32 o = SCRATCH_OBJ;
        GV(u8, o + 0x2A) = 1;
        GV(u8, o + 0x29) = 1;
        GV(s32, o + 0x170) = 0x1000;
        s32 limit = g_objectCount + GV(s16, t + 0x1C);
        u32 obj = g_objects;
        for (s32 i = 0; i < limit; obj += OBJ_SIZE) {
            if (i >= g_objectCount) {             /* point of interest */
                obj = SCRATCH_OBJ;
                kind = GV(s16, t);
                part = (s16)GV(u16, t + 0x18);
                setup_point(sp, t, kind, item_found);
                t += 0x24;
            } else if (GV(u8, obj + 0x2A) != 1) {
                goto next;
            }
            u8 type = GV(u8, obj + 0x315);
            if (type >= 0x5A || GV(s16, obj + 0x31E) <= 0 || type >= 0x32 ||
                GV(u8, obj + 0x29) == 0 || GV(s32, obj + 0x170) == 0)
                goto next;
            if (GCALL3(F_CAN_SEE, obj, i, (s32)part) == 0) goto next;
            if (!item_found && kind == 1) {      /* kind-1 points (pickups) come first */
                best_dist = (u32)-1;
                found = 0;
                item_found = 1;
            }
            if (best_dist != (u32)-1 && i == g_qsLastIndex) goto next;
            s32 d = GV(s32, obj + 0x868);
            if (!((d < 0 ? -d : d) < max_dist)) goto next;
            if (best_dist < (u32)d) goto next;
            if (i >= g_objectCount) {             /* keep the chosen point */
                copy_record_polled(CHOSEN_POINT, SCRATCH_OBJ, 0x800793F8);
                obj = CHOSEN_POINT;
            } else {
                limit = g_objectCount;            /* an enemy beats every point */
            }
            best_dist = (u32)-2;
            g_qsTarget = obj;
            best = i;
            D_800B4DEC = obj;
            if (i != g_qsLastIndex) best_dist = GV(u32, obj + 0x868);
            found++;
        next:
            if (!(++i < limit)) break;
            LOOP_POLL(0x8007945C);
        }
        if (best_dist == (u32)-1) {
            g_qsState = 0;
            g_qsTarget = 0;
            g_qsPressGap1 = 0;
            g_qsPressGap2 = 0;
            return;
        }
        u32 target = g_qsTarget;
        g_qsLastIndex = best;
        if (found == 1) best_dist = GV(u32, target + 0x868);
        GS(u32, sp + 0x10) = 0;                  /* argument 5: kind 0 */
        A0 = target; A1 = (u32)best; A2 = best_dist; A3 = 2;
        n_qs_Target();
        return;
    }

    if (g_qsFrames != 0) {                        /* turning */
        g_qsFrames--;
        GV(u16, CAMERA_OBJ + 0x44) -= (u16)g_qsPitchStep;
        if (g_qsState == 1) PLAYER_YAW = (s16)(PLAYER_YAW + (u16)g_qsYawStep);
        else PLAYER_YAW = (s16)(PLAYER_YAW - (u16)g_qsYawStep);
    } else {                                      /* the remainder */
        s16 r = g_qsRemainder;
        u16 ru = (u16)r;
        if (r >= 7) {
            if (g_qsState == 1) PLAYER_YAW = (s16)(ru + (u16)(PLAYER_YAW - 6));
            else PLAYER_YAW = (s16)((u16)(PLAYER_YAW + 6) - ru);
            g_qsRemainder = 6;
        } else if (r != 0) {
            if (g_qsState == 1) PLAYER_YAW = (s16)(PLAYER_YAW + ru);
            else PLAYER_YAW = (s16)(PLAYER_YAW - ru);
            g_qsRemainder = (s16)(ru - 1);
        } else {                                  /* done */
            s32 autoaim = g_qsAutoTarget;
            g_qsState = 0;
            g_qsPressGap2 = 0;
            g_qsTarget = 0;
            if (autoaim) g_qsAutoTarget = 0;
        }
    }
    if (g_eventFlags & 4) {
        s32 autoaim = g_qsAutoTarget;
        g_qsTarget = 0;
        g_qsState = 0;
        g_qsPressGap2 = 0;
        if (autoaim) g_qsAutoTarget = 0;
    }
}

void n_qs_AimBack(void);

/* 80078EB4: turn back to the view kept at 0x800B56F8 (the target object
 * it becomes; the point at +0x1B8, distance 3000, fastest). After picking
 * up an item. */
void n_qs_AimBack(void) {
    FRAME(0x18);
    g_qsTarget = 0x800B56F8u;
    GCALL4(0x80078A58u, 0x800B56F8u, 0xBB8, 0x800B56F8u + 0x1B8, 0);
}
