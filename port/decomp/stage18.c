/* Stage program 18 (STAGPROG overlay 18: stage 3): the per-frame room
 * hook (rooms 11-16).
 *
 * Room 11 starts an alarm when the player moves or shoots (a flash, the
 * second music track, a countdown of flashes and sounds); the music goes
 * back once four room flags are set. Room 13 runs a short scripted
 * sequence (stage18_Room13Sequence). Rooms 15 and 16 set event flags (a
 * message, a script once the player crosses a line).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"

void n_stage18_Room13Sequence(void);
void n_stage18_RoomFrame(void);

#define g_seqStep       GV(u16, 0x800F4864)   /* s16: room 13's step; 100 = done */
#define g_seqWait       GV(u16, 0x800F4866)
#define g_musicSwapped  GV(s32, 0x800F4814)
#define g_alarmTimer    GV(s32, 0x800F4818)   /* -1 once over */
#define F_SEQ_WAIT      0x800F3338u           /* (kind): nonzero while waiting; ovl18, not decompiled */
#define F_ALARM_START   0x800925C4u           /* (0x800A8FF0) */
#define LIGHT_8         GV(u8, 0x800B60D8)

/* 800F3474: room 13, every frame: step through a sequence (each step
 * waits on 0x800F3338) until step 100; then the player model's state and
 * two of the room's distances follow event flag 0x7A. */
void n_stage18_Room13Sequence(void) {
    FRAME(0x18);
    if ((s16)g_seqStep < 100 && flag_test(EVENT_FLAGS, 0x7A)) {
        switch ((u32)(s32)(s16)g_seqStep) {
        case 0:
            g_seqWait = 3;
            g_seqStep = (u16)(g_seqStep + 1);
            /* fall through */
        case 1:
            if ((s16)GCALL1(F_SEQ_WAIT, 0) != 0) return;
            g_seqWait = 5;
            g_seqStep = (u16)(g_seqStep + 1);
            return;
        case 2:
        case 3:
            if ((s16)GCALL1(F_SEQ_WAIT, g_seqStep == 2 ? 1 : 0) != 0) return;
            g_seqWait = 2;
            g_seqStep = (u16)(g_seqStep + 1);
            return;
        case 4:
            if ((s16)GCALL1(F_SEQ_WAIT, 1) != 0) return;
            g_seqStep = (u16)(g_seqStep + 1);
            return;
        case 5:
            GCALL1(F_SEQ_WAIT, 0);
            g_seqStep = 100;
            return;
        }
        return;
    }
    u32 dist;
    if (flag_test(EVENT_FLAGS, 0x7A)) {
        GV(u16, g_playerObj + 0x24) = 3;
        dist = 0x1C20;
    } else {
        GV(u16, g_playerObj + 0x24) = 2;
        dist = 0x18E0;
    }
    GV(u32, GV(u32, LIGHTS + 0xC) + 0x3C) = dist;
    GV(u32, GV(u32, LIGHTS + 0xC) + 0x44) = dist;
}

/* The music back to track 0 (stopping the track playing). */
static void music_back(u32 stop) {
    GCALL1(F_MUSIC_STOP, stop);
    g_musicTrack = 0;
    GCALL1(F_MUSIC_PLAY, 0);
}

/* 800F360C: every frame, by room (11-16). Returns 0. */
void n_stage18_RoomFrame(void) {
    FRAME(0x20);
    switch (g_room) {
    case 11:
        if (!flag_test(EVENT_FLAGS, 0x85)) {
            if ((g_playerMoveFlags & 3) != 0 ||
                (g_fireRequest != 0 && g_reticleX >= 0 && g_reticleY >= 0)) {
                GCALL1(F_ALARM_START, 0x800A8FF0u);
                LIGHT_8 = 8;
                flag_set(EVENT_FLAGS, 0x85);
                screen_flash();
                GCALL1(F_MUSIC_STOP, 0);
                g_musicTrack = 1;
                GCALL1(F_MUSIC_PLAY, 1);
                g_musicSwapped = 0;
                g_alarmTimer = 0;
            }
        }
        if (g_musicSwapped == 0 && flag_test(ROOM_FLAGS, 0x51) && flag_test(ROOM_FLAGS, 0x52) &&
            flag_test(ROOM_FLAGS, 0x53) && flag_test(ROOM_FLAGS, 0x54)) {
            music_back((u32)(s32)g_musicTrack);
            g_musicSwapped = 1;
        }
        if (g_alarmTimer < 0) break;
        g_alarmTimer = g_alarmTimer + 1;
        if (g_alarmTimer == 20) flag_set(EVENT_FLAGS, 0xB8);
        if (g_alarmTimer == 0x24) {
            screen_flash();
            sound_at(6, 4);
        }
        if (g_alarmTimer == 0x2B) {
            screen_flash();
            sound_at(6, 4);
            g_alarmTimer = -1;
        }
        break;
    case 12:
        if (g_musicSwapped != 0) break;
        if (!flag_test(ROOM_FLAGS, 0x55)) break;
        if (!flag_test(ROOM_FLAGS, 0x56)) break;
        music_back(1);
        g_musicSwapped = 1;
        break;
    case 13:
        n_stage18_Room13Sequence();
        break;
    case 15:
        if (!flag_test(FLAGS_2FA0, 0xD)) break;
        if (flag_test(EVENT_FLAGS, 0x8B)) break;
        GCALL2(F_MESSAGE, 0, 1);
        flag_set(EVENT_FLAGS, 0x8B);
        break;
    case 16:
        if (!flag_test(EVENT_FLAGS, 0x35) && player_line_side(0x800F4824u, 0x800F4834u) >= 0 &&
            GCALL2(F_EVENT_SCRIPT, 1, 0x800F481Cu) != 0) {
            flag_set(EVENT_FLAGS, 0x35);
            break;
        }
        if (g_musicSwapped == 0) break;
        if (!flag_test(ROOM_FLAGS, 0x5D)) break;
        GCALL1(F_MUSIC_STOP, (u32)(s32)g_musicTrack);
        g_musicSwapped = 0;
        break;
    }
    V0 = 0;
}
