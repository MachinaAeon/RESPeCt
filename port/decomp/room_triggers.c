/* Room event triggers and per-room properties (TEST overlay).
 *
 * A room's trigger list (pointer at D_800AB52C; the count is the s16 at
 * +0x1C of the first entry) holds 0x24-byte entries: +0 s16 kind (index
 * into the handler table at 0x800A31B0), +4/+8/+0xC s32 position, +0x12
 * s16 the heading the player must face (+-0x200, or anything when +0x18
 * is 0x800), +0x18 s16 the half-width of the facing cone, +0x1E s16
 * radius. Walking into one (within the radius, facing it) runs its
 * handler once; D_800A132C remembers which trigger has fired until the
 * player leaves it (-1 none).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"
#include "poll.h"

#define g_triggerList    GV(u32, 0x800AB52C)
#define g_triggerActive  GV(s32, 0x800A132C)
#define TRIGGER_HANDLERS 0x800A31B0u
#define g_triggersOff    GV(u32, 0x800BBB58)
#define g_inputLocked    GV(s16, 0x800B3D2C)
#define g_stage          GV(s32, 0x800B4BA0)
#define g_room           GV(s32, 0x800B3E20)
#define g_floorSound     GV(s32, 0x800BB194)   /* footstep surface (read by 0x80084B04) */
#define F_SQRT           0x8002A3D8u
#define F_LINE_SIDE      0x8007C04Cu   /* (a, b, x, z): which side of the line a-b */

void n_trigger_Facing(void);
void n_trigger_Outside(void);

/* 80079A18: forget the fired trigger. */
void n_trigger_Reset(void) {
    g_triggerActive = -1;
    V0 = (u32)-1;
}

/* 80079C18 (heading): 1 unless the player faces away from the heading
 * (difference 0x601-0x9FF, i.e. facing it within 0x200). */
void n_trigger_Facing(void) {
    s32 h = (s32)((A0 + 0x800) & 0xFFF);
    s32 d = GV(s16, g_player + 0x46) - h;
    if (d < 0) d = -d;
    V0 = ((u32)((d - 0x601) & 0xFFFF) < 0x3FF) ^ 1;
}

/* 80079C4C (trigger): 0 when the player is facing into the trigger
 * (within the cone: two points 100 ahead, turned +-half-width, on the
 * right sides of the trigger's line), else 1. */
void n_trigger_Outside(void) {
    FRAME(0x50);
    u32 t = A0;
    if (GV(s16, t + 0x18) == 0x800) { V0 = 0; return; }
    A0 = (u32)(s32)GV(s16, t + 0x12);
    n_trigger_Facing();
    if (V0 != 0) { V0 = 1; return; }

    u32 right = sp + 0x10, left = sp + 0x20, rot = sp + 0x30;   /* local vectors */
    GS(u16, rot) = GV(u16, g_player + 0x44);
    GS(u16, rot + 2) = GV(u16, g_player + 0x46);
    GS(u16, rot + 4) = GV(u16, g_player + 0x48);
    GS(u16, rot + 2) = (u16)(GV(u16, g_player + 0x46) + GV(u16, t + 0x18));
    /* a3 as the original leaves it (it can reach the trigger's handler) */
    gcall(F_ROT_STEP, rot, left, 0x64, A3);
    GS(s32, left) += PLAYER_POS[0];
    GS(s32, left + 8) += PLAYER_POS[2];
    GS(u16, rot + 2) = (u16)(GS(u16, rot + 2) - GV(s16, t + 0x18) * 2);
    gcall(F_ROT_STEP, rot, right, 0x64, (u32)PLAYER_POS[0]);
    GS(s32, right) += PLAYER_POS[0];
    GS(s32, right + 8) += PLAYER_POS[2];
    s32 v = (s32)GCALL4(F_LINE_SIDE, PLAYER_POS_ADDR, left, GV(u32, t + 4), GV(u32, t + 0xC));
    if (v > 0) {
        v = (s32)GCALL4(F_LINE_SIDE, PLAYER_POS_ADDR, right, GV(u32, t + 4), GV(u32, t + 0xC));
        if (v < 0) { V0 = 0; return; }
    }
    V0 = 1;
}

/* 80079A28: walk-into triggers, while the player walks forward. */
void n_trigger_Check(void) {
    FRAME(0x38);
    if (g_triggersOff != 0 || g_inputLocked != 0) return;
    u32 t = g_triggerList;
    s32 n = GV(s16, t + 0x1C);
    for (s32 i = 0; i < n; t += 0x24) {
        s32 dx = (s32)((u32)PLAYER_POS[0] - GV(u32, t + 4));
        s32 ax = dx < 0 ? -dx : dx;
        s32 dz = 0, az = 0;
        s32 near = ax < 0x3E9;
        if (near) {
            dz = (s32)((u32)PLAYER_POS[2] - GV(u32, t + 0xC));
            az = dz < 0 ? -dz : dz;
            near = az < 0x3E9;
        }
        if (near) {
            /* a stage-specific tighter box */
            if (GV(s32, 0x800A866C) == 5 && D_800B3F08 >= 0 && D_800B3F08 < 3 &&
                !(ax < 0x258 && az < 0x258))
                goto next;
            /* a1-a3 hold dz, |dz|, |dx| in the original (left for the handler) */
            s32 d = (s32)gcall(F_SQRT, (u32)(dx * dx) + (u32)(dz * dz), (u32)dz, (u32)az, (u32)ax);
            near = d < GV(s16, t + 0x1E);
        }
        if (!near) {
            if (g_triggerActive == i) g_triggerActive = -1;
            goto next;
        }
        A0 = t;
        n_trigger_Outside();
        if (V0 == 0) {
            if (g_triggerActive != -1) return;
            g_triggerActive = i;
            gcall(GV(u32, TRIGGER_HANDLERS + (u32)(s32)GV(s16, t) * 4), t, A1, A2, A3);
        } else if (g_triggerActive == i) {
            g_triggerActive = -1;
        }
        return;
    next:
        if (++i >= n) break;
        LOOP_POLL(0x80079BE4);
    }
}

/* 80077F8C: the floor's footstep sound for rooms with a special floor
 * (by stage and room). */
void n_room_SetFloorSound(void) {
    /* Polls as the original: its jump tables, and its branches back to
     * the shared return. */
    s32 room;
    g_floorSound = 0;
    u32 st = (u32)(g_stage - 1);
    if (st >= 0x11) return;
    LOOP_POLL(0x80077FC0);
    switch (st + 1) {
    case 1:
        room = g_room;
        if (room == 2) g_floorSound = 2;
        else if (room == 3) g_floorSound = 4;
        break;
    case 2: {
        u32 r = (u32)(g_room - 4);
        if (r >= 0xD) { LOOP_POLL(0x80078018); break; }
        LOOP_POLL(0x80078034);
        if (r == 0) g_floorSound = 2;
        else if (r == 2 || r == 5 || r == 9 || r == 12) g_floorSound = 1;   /* rooms 6, 9, 13, 16 */
        break;
    }
    case 4:
        if (g_room != 3) { LOOP_POLL(0x80078058); break; }
        LOOP_POLL(0x80078060);
        g_floorSound = 1;
        break;
    case 7:
        if (g_room != 4) { LOOP_POLL(0x80078074); break; }
        LOOP_POLL(0x8007807C);
        g_floorSound = 1;
        break;
    case 11:
        if (g_room != 2) { LOOP_POLL(0x80078090); break; }
        LOOP_POLL(0x80078098);
        g_floorSound = 1;
        break;
    case 16:
        if (g_room != 10) { LOOP_POLL(0x800780AC); break; }
        g_floorSound = 2;
        break;
    case 17:
        if (g_room == 0) g_floorSound = 2;
        break;
    }
}
