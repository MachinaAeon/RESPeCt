/* Stage program 19 (STAGPROG overlay 19: stage 4): the per-frame room
 * hook (rooms 2 and 5).
 *
 * Room 2 turns one of its room objects (object 2: held at height -1500,
 * spinning about x). In room 5, once event flag 0x40 is set its event
 * (0x800F3584) runs once with a white flash (flag 0x5F); once room flags
 * 0x63 and 0x64 are both set the music stops (once) and event flag 0xA0
 * is set.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"

void n_stage19_RoomFrame(void);

#define g_musicStopped  GV(s32, 0x800F3808)   /* room 5 (reset at room start) */
#define ROOM_OBJ2       (0x800A89C0u + 2 * 0x318 + 0x180)   /* its 3D object */
#define F_ROOM5_EVENT   0x800F3584u           /* ovl19, not decompiled */
#define F_COORD_SET_MATRIX 0x8006F69Cu        /* coord_SetMatrix (object, 0) */

/* 800F30D4: every frame, by room. Returns 0. */
void n_stage19_RoomFrame(void) {
    FRAME(0x20);
    switch (g_room) {
    case 2:
        GV(s32, ROOM_OBJ2 + 0x1C) = -0x5DC;
        GV(u16, ROOM_OBJ2 + 0x44) = (u16)(GV(u16, ROOM_OBJ2 + 0x44) + 8);
        GCALL2(F_COORD_SET_MATRIX, ROOM_OBJ2, 0);
        break;
    case 5:
        if (flag_test(EVENT_FLAGS, 0x40) && !flag_test(EVENT_FLAGS, 0x5F)) {
            GCALL0(F_ROOM5_EVENT);
            screen_flash();
            flag_set(EVENT_FLAGS, 0x5F);
        }
        if (g_musicStopped == 0 && flag_test(ROOM_FLAGS, 0x63) && flag_test(ROOM_FLAGS, 0x64)) {
            GCALL1(F_MUSIC_STOP, 0);
            g_musicStopped = 1;
        }
        if (flag_test(ROOM_FLAGS, 0x63) && flag_test(ROOM_FLAGS, 0x64) &&
            !flag_test(EVENT_FLAGS, 0xA0))
            flag_set(EVENT_FLAGS, 0xA0);
        break;
    }
    V0 = 0;
}
