/* Stage program 32 (STAGPROG overlay 32: stage 17): the per-frame room
 * hook (rooms 0 and 2).
 *
 * Room 0 cycles its lights by palette (brighter once event flag 0x74 is
 * set); room 2 swaps the music back once event flag 0xAD is set, steps a
 * palette animation from a table, and sets event flags with their
 * messages.
 *
 * Also one of its script ops (stage32_OpBlank).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"
#include "libgs.h"
#include "poll.h"

void n_stage32_RoomFrame(void);
void n_stage32_OpBlank(void);
void n_stage32_OpModel(void);

#define LIGHT_IDX       0x800F3AB8u          /* s8 */
#define LIGHT_TIMER     0x800F3AB9u          /* s8 */
#define g_lightIdx      GV(u8, LIGHT_IDX)
#define g_lightTimer    GV(u8, LIGHT_TIMER)
#define g_musicSwapped  GV(s32, 0x800F3ABC)
#define LIGHT_TABLE     0x800F3A70u          /* (frames, row) pairs */

/* The light frame for this step: (s8)(idx / 3) + k, as 16 bits. */
static s32 frame3(s32 k) { return s16v((s8)((s8)g_lightIdx / 3) + k); }

/* 800F2F80: every frame, by room. Returns 0. */
void n_stage32_RoomFrame(void) {
    FRAME(0x20);
    switch (g_room) {
    case 0:
        g_lightIdx = (u8)(g_lightIdx + 1);
        if ((s8)g_lightIdx >= 5) g_lightIdx = 1;
        clut_anim(0x240, 0x1F8, frame3(1));
        clut_anim(0x240, 0x1F5, frame3(1));
        clut_anim(0x240, 0x1F0, frame3(flag_test(EVENT_FLAGS, 0x74) ? 3 : 1));
        break;
    case 2:
        if (g_musicSwapped == 0 && flag_test(EVENT_FLAGS, 0xAD)) {
            GCALL1(F_MUSIC_STOP, 1);
            g_musicTrack = 0;
            GCALL1(F_MUSIC_PLAY, 0);
            g_musicSwapped = 1;
        }
        g_lightTimer = (u8)(g_lightTimer - 1);
        if ((s8)g_lightTimer <= 0)
            stage_ClutTableStep(LIGHT_IDX, LIGHT_TIMER, LIGHT_TABLE, 0x240, 0x1F0);
        if (flag_test(ROOM_FLAGS, 0xF4) && !flag_test(EVENT_FLAGS, 0xB6)) {
            GCALL2(F_MESSAGE, 0, 9);
            flag_set(EVENT_FLAGS, 0xB6);
        }
        if (flag_test(EVENT_FLAGS, 0x9A)) break;
        if (!flag_test(ROOM_FLAGS, 0x119)) break;
        flag_set(EVENT_FLAGS, 0x9A);
        GCALL2(F_MESSAGE, 0, 0);
        break;
    }
    V0 = 0;
}

/* 800F3404 (thread): script op: when it starts, the display goes off
 * (SetDispMask 0) and the game flow goes to handler 3 (g_flowHandler
 * 0x800BB19C), step 0x11 (g_flowStep 0x800A28CC). Moves on (pc + 4).
 * Returns 0. */
void n_stage32_OpBlank(void) {
    FRAME(0x18);
    u32 th = A0;
    if (GV(s16, th + 4) == 0) {
        GCALL1(0x800352DCu, 0);
        GV(s32, 0x800BB19C) = 3;
        GV(s32, 0x800A28CC) = 0x11;
    }
    GV(u32, th) += 4;
    V0 = 0;
}

/* 800F3464 (script thread): a script op in steps (+4; the table at
 * 0x800F2C6C): wait for button 4, then blank the display; wait out a
 * timer (0x800A0CBC = 0x17) drawing a frame at a time; set up the model
 * of the object +0xA (the last but two loaded): its parts' animation
 * pointers and states; the display back on and a fade; then wait for
 * button 4 to be released and go on to the next op (0). 1 while the op
 * isn't done. The timer's wait polls where a save state can be made (in
 * the translation's frame and registers). */
void n_stage32_OpModel(void) {
    FRAME(0x20);
    MEM_W32(sp + 0x14, R[17]);
    MEM_W32(sp + 0x1C, R[31]);
    MEM_W32(sp + 0x18, R[18]);
    MEM_W32(sp + 0x10, R[16]);
    const u32 in_s0 = R[16], in_s1 = R[17], in_s2 = R[18];
    const u32 th = A0;
    R[17] = th;
    u32 ret = 1;
    const u32 step = (u32)(s32)GV(s16, th + 4);
    if (step < 7) {
        const u32 t = GV(u32, 0x800F2C6Cu + (step << 2));
        switch (t) {
        case 0x800F34ACu:
            if (GV(s16, 0x800A55A2u) != 4) goto out;
            gcall_ra(0x800F34C8u, 0x800352DCu, 0, A1, A2, A3);
            goto next;
        case 0x800F34D0u:
            GV(u32, 0x800A0CBCu) = 0x17;
            GV(u16, th + 0xA) = (u16)(GV(u16, 0x800AF530u) - 3);
            GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
            /* fall through */
        case 0x800F34F8u:
            if (GV(u32, 0x800A0CBCu) != 0) {
                R[18] = 0x800B0000u;
                R[16] = 0x800A0000u;
                for (;;) {
                    gcall_ra(0x800F351Cu, 0x8007672Cu, A0, A1, A2, A3);
                    gcall_ra(0x800F3524u, 0x80076B80u, 0x800B3E40u, A1, A2, A3);
                    gcall_ra(0x800F352Cu, 0x800255F8u, 0, A1, A2, A3);
                    if (GV(u32, 0x800A0CBCu) == 0) break;
                    SAVE_POLL(0x800F3534u);
                }
            }
            GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
            /* fall through */
        case 0x800F354Cu: {
            gcall_ra(0x800F3558u, 0x80076784u, (u32)(s32)GV(s16, th + 0xA), 0xC, A2, A3);
            u32 obj = GV(u32, 0x800AB510u) + (u32)(s32)GV(s16, th + 0xA) * 0x8C4;
            const s32 n = GV(s32, obj + 0x1D0);
            if (n > 0) {
                for (s32 j = 0;;) {
                    const u32 pp = obj + 0x1D4 + (u32)j * 4, ap = obj + 0x274 + (u32)j * 4;
                    GV(u32, ap) = GV(u32, pp) + 0x18;
                    u32 q = GV(u32, pp);
                    GV(u16, q + 0xA) = 0xFFFF;
                    GV(u16, q + 8) = 0xFFFF;
                    GV(u8, q + 0xD) = 0x10;
                    u32 e = GV(u32, ap) + ((u32)(s32)GV(s16, obj + 0x860) << 2);
                    GV(u16, GV(u32, pp) + 0x16) = (u16)(GV(u16, e) + 1);
                    e = GV(u32, ap) + ((u32)(s32)GV(s16, obj + 0x860) << 2);
                    GV(u8, GV(u32, pp) + 0xC) = GV(u8, e + 2);
                    j++;
                    if (!(j < n)) break;
                    LOOP_POLL(0x800F361Cu);
                }
            }
            R[16] = 0x800B0000u;
            obj = GV(u32, 0x800AB510u) + (u32)(s32)GV(s16, th + 0xA) * 0x8C4;
            gcall_ra(0x800F364Cu, 0x8006FB5Cu, obj, A1, A2, A3);
            obj = GV(u32, 0x800AB510u) + (u32)(s32)GV(s16, th + 0xA) * 0x8C4;
            gcall_ra(0x800F3674u, 0x80073C5Cu, obj, A1, A2, A3);
            goto next;
        }
        case 0x800F367Cu:
            gcall_ra(0x800F3684u, 0x800255F8u, 0, A1, A2, A3);
            gcall_ra(0x800F368Cu, 0x800352DCu, 1, A1, A2, A3);
            GV(u32, 0x800A8954u) = 0;
            gcall_ra(0x800F36A0u, 0x80090B90u, 0, 0x10, A2, A3);
            /* fall through */
        case 0x800F36A0u:
        next:
            GV(u16, th + 4) = (u16)(GV(u16, th + 4) + 1);
            break;
        case 0x800F36B4u:
            if (GV(u16, 0x800A55A2u) & 4) goto out;
            GV(u32, th) += 4;
            ret = 0;
            break;
        default:
            rt_badjump(0x800F34A4u, t);
            return;
        }
    }
out:
    R[16] = in_s0;
    R[17] = in_s1;
    R[18] = in_s2;
    V0 = ret;
}
