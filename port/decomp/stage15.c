/* Stage program 15 (STAGPROG overlay 15: stage 0, the hospital): the
 * per-frame room hook (rooms 2, 4 and 6).
 *
 * Room 2 has four breakable things (0x5C-byte records from 0x800F5270):
 * while the player shoots, each one not yet broken (event flags 7-10)
 * that the shot reached (0x800F3948) breaks (0x800F3BF8: sounds, its VRAM
 * patch, the flag, debris). Its music (track 1) fades out once three of
 * the room's enemies are dead and then goes back to track 0. Rooms 4 and
 * 6 stop or swap the music by event and room flags.
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "stagprog.h"

void n_stage15_RoomFrame(void);

#define g_lastButtons   0x800F53E0u           /* u16 */
#define g_musicState    GV(s32, 0x800F51F8)   /* room 2: -1 track 1 on, -2 fading; room 4: 1 stopped */
#define g_room6Music    GV(s32, 0x800F51FC)   /* room 6: music swapped */
#define g_enemies       GV(u32, 0x800AB510)   /* 0x8C4-byte records; +0x31E s16 health */
#define g_lightObj      GV(u32, 0x800B60CC)
#define TARGETS         0x800F5270u           /* 0x5C-byte records */
#define TARGET_SIZE     0x5C
#define F_TARGET_HIT    0x800F3948u           /* (target, i); ovl15, not decompiled */
#define F_TARGET_BREAK  0x800F3BF8u           /* (i, flag, debris mode); ovl15 */
#define F_ROOM4_FRAME   0x800F4134u           /* (0); ovl15 */
#define F_MUSIC_FADE    0x80011EC4u           /* (track, speed) */
#define F_MUSIC_FADING  0x80011FE8u           /* nonzero while a fade runs */

/* 800F3D34: every frame, by room. Returns 0. */
void n_stage15_RoomFrame(void) {
    FRAME(0x20);
    switch (g_room) {
    case 2:
        if (stage_NotFiring(g_lastButtons) == 0) {
            for (u32 i = 0; i < 4; i++) {
                if (flag_test(EVENT_FLAGS, 7 + i)) continue;
                if (GCALL2(F_TARGET_HIT, TARGETS + i * TARGET_SIZE, i) == 0) continue;
                GCALL3(F_TARGET_BREAK, i, 7 + i, 0xE);
            }
        }
        if (g_musicTrack == 1) {
            if (g_musicState == -1) {
                s32 dead = 0;
                u32 e = g_enemies;
                for (s32 i = 0; i < 3; i++, e += 0x8C4)
                    if (GV(s16, e + 0x31E) <= 0) dead++;
                if (dead >= 3) {
                    g_musicState = -2;
                    GCALL2(F_MUSIC_FADE, (u32)(s32)g_musicTrack, 0x100);
                }
            } else if (g_musicState == -2) {
                if (GCALL0(F_MUSIC_FADING) == 0) {
                    GCALL1(F_MUSIC_STOP, 1);
                    g_musicTrack = 0;
                    GCALL1(F_MUSIC_PLAY, 0);
                }
            }
        }
        if (flag_test(EVENT_FLAGS, 0x23)) GV(u32, g_lightObj + 0xC8) = 0xD00;
        break;
    case 4:
        if (!flag_test(EVENT_FLAGS, 0x28)) flag_set(EVENT_FLAGS, 0x28);
        if (flag_test(EVENT_FLAGS, 4)) {
            if (g_musicState != 0) break;
            GCALL1(F_MUSIC_STOP, (u32)(s32)g_musicTrack);
            g_musicState = 1;
        } else {
            GCALL1(F_ROOM4_FRAME, 0);
        }
        break;
    case 6:
        if (g_room6Music != 0) break;
        if (!flag_test(ROOM_FLAGS, 0x1F)) break;
        if (!flag_test(ROOM_FLAGS, 0x20)) break;
        if (!flag_test(ROOM_FLAGS, 0x21)) break;
        GCALL1(F_MUSIC_STOP, (u32)(s32)g_musicTrack);
        g_musicTrack = 0;
        GCALL1(F_MUSIC_PLAY, 0);
        g_room6Music = 1;
        break;
    }
    V0 = 0;
}
