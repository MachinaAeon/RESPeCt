/* Item lists and weapon helpers (TEST overlay).
 *
 * Items live in 8-byte entries (s16 id, s16 count (-1 = unlimited), s16
 * ammo item / extra, s16 spare); a list is a byte array of entry indices
 * with an s16 count after it. Three lists: what the player carries
 * (0x800A8880, entries at 0x800AF800, count at +0x40) and two more (0x800B3DA0
 * -> 0x800AF580 and 0x800B4BE8 -> 0x800AFA00, counts at +0x50).
 *
 * Decompiled from the game's code; RAM-identical to the originals (checked
 * with tools/eq). */
#include "game_vars.h"

void n_item_RemoveCarried(void);
void n_item_RemoveB(void);
void n_item_RemoveC(void);
void n_weapon_LoadSounds(void);
void n_weapon_ShotSound(void);
void n_shots_Clear(void);

#define ITEM_EMPTY      0x1E
#define F_SOUND_LOAD    0x80011CB4u   /* (slot, bank file, buffer) */
#define F_SOUND_AT      0x80080524u   /* (bank, sound, pitch?, vol, left, right) */

/* Remove the item in slot of a list: its entry becomes empty and moves
 * past the end, the others close up. */
static void remove_item(u32 list, u32 entries, u32 count_off, u32 slot) {
    s16 s = (s16)slot;
    u8 e = GV(u8, list + (u32)(s32)s);
    u32 ent = entries + (u32)e * 8;
    GV(s16, ent) = ITEM_EMPTY;
    GV(s16, ent + 2) = 0;
    GV(u32, ent + 4) = 0;
    u32 i = slot;
    if (s < GV(s16, list + count_off) - 1) {
        do {
            s16 k = (s16)i;
            i++;
            GV(u8, list + (u32)(s32)k) = GV(u8, list + (u32)(s32)k + 1);
        } while ((s16)i < GV(s16, list + count_off) - 1);
    }
    GV(u8, list + (u32)(s32)(s16)i) = e;
    GV(u16, list + count_off) = (u16)(GV(u16, list + count_off) - 1);
    V0 = list;
}

/* 8008D678 (slot): from what the player carries (an emptied ammo box). */
void n_item_RemoveCarried(void) { remove_item(0x800A8880u, 0x800AF800u, 0x40, A0); }
/* 8008D734 (slot), 8008D7F0 (slot): the same for the other two lists. */
void n_item_RemoveB(void) { remove_item(0x800B3DA0u, 0x800AF580u, 0x50, A0); }
void n_item_RemoveC(void) { remove_item(0x800B4BE8u, 0x800AFA00u, 0x50, A0); }

/* 8008E324: load the held weapon's sound bank (file id + 1) into sound
 * slot 1, through the buffer at 0x22000 past the stage's data. */
void n_weapon_LoadSounds(void) {
    s16 id = GV(s16, g_currentWeaponAddr);
    GCALL3(F_SOUND_LOAD, 1, (s32)id + 1, GV(u32, 0x80010050) + 0x22000);
}

/* 8008E368: the held weapon's shot. Handguns (ids 0-3) play it at their
 * own pitch (0x800A3D68) and the sound volume setting; the others use
 * the plain cue. */
void n_weapon_ShotSound(void) {
    s16 id = GV(s16, g_currentWeaponAddr);
    if (id >= 4) {
        GCALL4(F_SOUND_CUE, 1, 5, 0x7F, 0x7F);
        return;
    }
    s32 v = GV(s16, 0x80062DB6) * 127 / 100;          /* volume setting, 0-100 -> 0-127 */
    s32 pitch = GV(s16, 0x800A3D68 + (u32)id * 2);
    GameStack st;
    u32 args = gstack_alloc(&st, 8);                  /* arguments 5 and 6, at sp + 0x10 */
    GS(s32, args) = v;                                /* left */
    GS(s32, args + 4) = v;                            /* right */
    GCALL4(F_SOUND_AT, 1, 5, pitch, 0x7F);
    gstack_free(&st);
}

/* 8008E4A0: clear the four shot records at 0x800BBC18. */
void n_shots_Clear(void) {
    for (u32 i = 0; i < 4; i++) {
        u32 r = 0x800BBC18u + i * 0x14;
        GV(s16, r) = -1;
        GV(s16, r + 2) = -1;
        GV(u32, r + 4) = 0;
        GV(u32, r + 8) = 0;
        GV(u32, r + 12) = 0;
    }
}
