/* The checkpoint (TEST overlay): the copy of the player's progress the
 * game keeps in memory, and rebuilding the game from it when TEST starts.
 *
 * checkpoint_Make (also used by savegame.c through gcall) writes:
 *   0x80063548  item list B ids (one byte each, 0x1E = end/empty)
 *   0x80063598  item list C ids
 *   0x800635E8  the carried items, 6 bytes each: id, byte +4, count,
 *               extra (u16 +6); 0x80063768 = index of the held one
 *   0x800634B0  the story flags (0x94 bytes, live copy at 0x80062F60)
 *   0x80063498  the record totals (5 words from 0x80063460)
 *   0x80063770  0x3D bytes from 0x800B3E68
 *   0x800A856C  stage, room (0x800A8570), entrance (0x800A8574)
 *   0x80062E30 / 0x80062DE4  health (bytes of 0x800A8584 / 0x800A8586)
 *   0x80062DB0  number of weapons carried
 * Item entries (8 bytes, see inventory.c): s16 id, s16 count, byte +4,
 * s16 +6 (for id 8, spare rounds).
 *
 * checkpoint_Restore runs as TEST starts: it empties the item lists,
 * resets health and the player, then by game type (0x80062DF4):
 * new game / demo: the starting inventory; continuing (1): items, flags
 * and room from the checkpoint; loaded (2): items and some flags from it.
 * The port's savegame.c hooks it (sg_hook_restore) to put a saved
 * checkpoint in place first.
 *
 * Decompiled from the game's code; RAM-identical to the originals
 * (checked with tools/eq). */
#include "libgs.h"

void n_checkpoint_NewGameItems(void);
void n_checkpoint_Make(void);
void n_checkpoint_Restore(void);
void sg_hook_restore(void);

#define GAME_TYPE      GV(u8, 0x80062DF4)
#define DIFFICULTY     GV(u32, 0x80062DF8)    /* 1: the easier setting (more ammo) */
#define ITEM_EMPTY     0x1E
#define WEAPON_SPARE   8                      /* item id with spare rounds at +6 */

#define LIST_B         0x800B3DA0u            /* index bytes; count s16 at +0x50 */
#define ITEMS_B        0x800AF580u
#define LIST_C         0x800B4BE8u
#define ITEMS_C        0x800AFA00u
#define LIST_CARRIED   0x800A8880u            /* count s16 at +0x40 */
#define ITEMS_CARRIED  0x800AF800u

#define CP_ITEMS_B     0x80063548u
#define CP_ITEMS_C     0x80063598u
#define CP_CARRIED     0x800635E8u
#define CP_HELD        GV(u8, 0x80063768)
#define CP_FLAGS       0x800634B0u
#define CP_RECORD      0x80063498u
#define CP_MISC        0x80063770u
#define LIVE_FLAGS     0x80062F60u
#define RECORD         0x80063460u
#define MISC           0x800B3E68u

#define F_FLAG_TEST    0x8008F64Cu   /* (flags, bit) */
#define F_FLAG_SET     0x8008F5F8u
#define F_ITEM_GIVE    0x80071C0Cu   /* (checkpoint carried end, item kind): returns new end */

/* 80071738: the starting inventory: item 0x11 (10, held), item 10
 * (unlimited), and on the easier setting two of item 0x14 in list B;
 * health bytes and record cleared. */
void n_checkpoint_NewGameItems(void) {
    if (DIFFICULTY == 1) {
        GV(u16, ITEMS_B) = 0x14;
        GV(u16, ITEMS_B + 8) = 0x14;
        GV(u16, LIST_B + 0x50) = 2;
    }
    GV(u16, ITEMS_CARRIED) = 0;
    GV(u16, ITEMS_CARRIED + 2) = 0x11;
    GV(u32, ITEMS_CARRIED + 4) = 0xA;
    GV(u16, ITEMS_CARRIED + 8) = 0xA;
    GV(s16, ITEMS_CARRIED + 0xA) = -1;
    GV(u16, LIST_CARRIED + 0x40) = 2;
    g_currentWeaponAddr = ITEMS_CARRIED;
    for (u32 i = 0; i < 5; i++) GV(u32, RECORD + i * 4) = 0;
    GV(u8, 0x80062DBE) = 0;
    GV(u8, MISC) = 0xFF;
    GV(u8, 0x80062D3C) = 0;
    V0 = 0x80060000u;
}

/* The flags block copy, in the original's four runs (16, 8, 8, 5 words). */
static void copy_flags(u32 dst, u32 src) {
    static const u8 runs[4][2] = {{0, 16}, {0x40, 8}, {0x60, 8}, {0x80, 5}};
    for (int r = 0; r < 4; r++)
        for (u32 i = 0; i < runs[r][1]; i++)
            GV(u32, dst + runs[r][0] + i * 4) = GV(u32, src + runs[r][0] + i * 4);
}

/* 800717E8: make the checkpoint from the current game. */
void n_checkpoint_Make(void) {
    FRAME(0x20);
    GV(u8, 0x80062DB0) = 0;
    GCALL3(F_MEMSET, CP_ITEMS_B, ITEM_EMPTY, 0x220);
    for (s16 i = 0; i < GV(s16, LIST_B + 0x50); i++)
        GV(u8, CP_ITEMS_B + (u32)i) = GV(u8, ITEMS_B + (u32)GV(u8, LIST_B + (u32)i) * 8);
    for (s16 i = 0; i < GV(s16, LIST_C + 0x50); i++)
        GV(u8, CP_ITEMS_C + (u32)i) = GV(u8, ITEMS_C + (u32)GV(u8, LIST_C + (u32)i) * 8);
    u32 held = 0;
    u32 out = CP_CARRIED;
    for (s16 i = 0; i < GV(s16, LIST_CARRIED + 0x40); i++, out += 6) {
        u32 it = ITEMS_CARRIED + (u32)GV(u8, LIST_CARRIED + (u32)i) * 8;
        /* an empty gun with spare rounds: load one */
        if (GV(s16, it) == WEAPON_SPARE && GV(s16, it + 2) == 0 && GV(s16, it + 6) > 0) {
            GV(u16, it + 2) = 1;
            GV(u16, it + 6) = (u16)(GV(u16, it + 6) - 1);
        }
        if (GV(s16, it) < 9) GV(u8, 0x80062DB0) += 1;      /* a weapon */
        if (it == g_currentWeaponAddr) held = (u32)i;
        GV(u8, out) = GV(u8, it);
        GV(u8, out + 1) = GV(u8, it + 4);
        GV(u16, out + 2) = GV(u16, it + 2);
        GV(u16, out + 4) = GV(u16, it + 6);
    }
    CP_HELD = (u8)held;
    GV(u32, 0x800A856C) = GV(u32, 0x800B4BA0);
    GV(u32, 0x800A8570) = GV(u32, 0x800B3E20);
    GV(s32, 0x800A8574) = GV(s16, 0x800B3D34);
    copy_flags(CP_FLAGS, LIVE_FLAGS);
    for (u32 i = 0; i < 5; i++) GV(u32, CP_RECORD + i * 4) = GV(u32, RECORD + i * 4);
    for (u32 i = 0; i < 0x3D; i++) GV(u8, CP_MISC + i) = GV(u8, MISC + i);
    GV(u8, 0x80062E30) = GV(u8, 0x800A8584);
    GV(u8, 0x80062DE4) = GV(u8, 0x800A8586);
    V0 = 0x80060000u;
}

/* Empty an item list: n entries (id ITEM_EMPTY), the list in order. */
static void clear_items(u32 items, u32 list, int n) {
    for (int i = 0; i < n; i++) {
        GV(u16, items + (u32)i * 8) = ITEM_EMPTY;
        GV(u16, items + (u32)i * 8 + 2) = 0;
        GV(u32, items + (u32)i * 8 + 4) = 0;
        GV(u8, list + (u32)i) = (u8)i;
    }
}

/* A held weapon's magazine after a restore: by weapon id 0-3. */
static void reload_held(void) {
    u32 w = g_currentWeaponAddr;
    switch (GV(s16, w)) {
    case 0: GV(u16, w + 2) = 0x11; break;
    case 1: GV(u16, w + 2) = 0xF; break;
    case 2: GV(u16, w + 2) = 0xF; break;
    case 3: GV(u16, w + 2) = 8; break;
    }
}

#define KEY_FLAGS      0x80062FA0u   /* live flags, +0x40 group */
#define CP_KEY_FLAGS   0x800634F0u   /* the checkpoint's */

/* Key flags 1-22 (not 13) set now, set them in dst too. */
static void merge_key_flags(u32 dst, u32 src) {
    for (u32 k = 1; k <= 0x16; k++) {
        if (k == 0xD) continue;
        if (GCALL2(F_FLAG_TEST, src, k)) GCALL2(F_FLAG_SET, dst, k);
    }
}

/* 80071CF4: rebuild the game from the checkpoint as TEST starts. */
void n_checkpoint_Restore(void) {
    sg_hook_restore();
    FRAME(0x20);
    GCALL0(0x8009250Cu);
    u32 held_id = 0;
    GV(s16, 0x800A8588) = -1;
    GV(u32, 0x800BB198) = 0;
    GV(u32, 0x800A28D0) = 0;
    clear_items(ITEMS_B, LIST_B, 0x50);
    GV(u16, LIST_B + 0x50) = 0;
    clear_items(ITEMS_C, LIST_C, 0x50);
    GV(u16, LIST_C + 0x50) = 0;
    if (GAME_TYPE != 1) {
        GV(u8, 0x80062E0C) = 0;
    } else {
        /* items picked up since the checkpoint (key flags 1-6, 8, 9 set
         * now but not in it) are added to its carried list */
        u32 end = CP_CARRIED;
        held_id = GV(u16, g_currentWeaponAddr);
        while (GV(u8, end) != ITEM_EMPTY) end += 6;
        static const u8 keys[8] = {1, 2, 3, 4, 5, 6, 8, 9};
        for (int i = 0; i < 8; i++)
            if (GCALL2(F_FLAG_TEST, KEY_FLAGS, keys[i]) && !GCALL2(F_FLAG_TEST, CP_KEY_FLAGS, keys[i]))
                end = GCALL2(F_ITEM_GIVE, end, keys[i] - 1);
    }
    clear_items(ITEMS_CARRIED, LIST_CARRIED, 0x40);
    GV(u16, LIST_CARRIED + 0x40) = 0;
    GV(u16, 0x800A8584) = 0xC8;                /* health 200 */
    GV(u8, 0x800A8587) = 0;
    GV(u8, 0x800A8586) = 0;
    GV(s32, g_player + 0x18) = 0x16A8;
    GV(s32, g_player + 0x20) = 0x3E8;
    GV(s32, g_player + 0x1C) = 0;
    GV(u16, g_player + 0x44) = 0;
    GV(u16, g_player + 0x46) = 0x400;
    GV(u16, g_player + 0x48) = 0;

    switch (GAME_TYPE) {
    case 0:
        n_checkpoint_NewGameItems();
        break;
    case 3:
        n_checkpoint_NewGameItems();
        GCALL0(0x80071514u);
        break;
    case 1: {
        /* continuing */
        u32 n = 0, items = ITEMS_B;
        for (u32 cp = CP_ITEMS_B; GV(u8, cp) != ITEM_EMPTY; cp++, items += 8, n++)
            GV(u16, items) = GV(u8, cp);
        GV(u16, LIST_B + 0x50) = (u16)n;
        n = 0; items = ITEMS_C;
        for (u32 cp = CP_ITEMS_C; GV(u8, cp) != ITEM_EMPTY; cp++, items += 8, n++) {
            u32 id = GV(u8, cp);
            GV(u16, items) = (u16)id;
            GV(u32, items + 4) = GV(u32, 0x800A09C8u + id * 8);
        }
        GV(u16, LIST_C + 0x50) = (u16)n;
        n = 0; items = ITEMS_CARRIED;
        for (u32 cp = CP_CARRIED; GV(u8, cp) != ITEM_EMPTY; cp += 6, items += 8, n++) {
            GV(u16, items) = GV(u8, cp);
            GV(u8, items + 4) = GV(u8, cp + 1);
            GV(u16, items + 2) = GV(u16, cp + 2);
            GV(u16, items + 6) = GV(u16, cp + 4);
            if (GV(s16, items) == (s16)held_id) g_currentWeaponAddr = items;
        }
        GV(u16, LIST_CARRIED + 0x40) = (u16)n;
        merge_key_flags(CP_KEY_FLAGS, KEY_FLAGS);
        copy_flags(LIVE_FLAGS, CP_FLAGS);
        u32 stage = GV(u32, 0x800A856C);
        GV(u32, 0x800B4BA0) = stage;
        u32 room = GV(u32, 0x800A8570);
        GV(u32, 0x800B3E20) = room;
        /* the camera object to follow (0x800A8614) by stage / room */
        if (stage == 5) {
            GV(u32, 0x800A8614) = 2;
        } else if (stage == 1) {
            static const u8 follow[5] = {0, 0xC, 0x11, 0x12, 0x13};
            if (room < 5) GV(u32, 0x800A8614) = follow[room];
        } else {
            if (stage == 6) {
                GV(s32, g_player + 0x18) = 0x2AF8;
                GV(s16, 0x800B4D6A) = -0x2EE0;
                GV(s32, g_player + 0x1C) = -0x2EE0;
                GV(s32, g_player + 0x20) = 0x14820;
            }
            GV(u32, 0x800A8614) = 0;
        }
        GV(u16, 0x800B3D34) = GV(u16, 0x800A8574);   /* entrance */
        reload_held();
        D_800AFCB0 &= 0xFFF7;
        g_fireCooldown &= 0x7FFF;
        break;
    }
    case 2: {
        /* a loaded game */
        merge_key_flags(KEY_FLAGS, CP_KEY_FLAGS);
        u32 n = 0, items = ITEMS_CARRIED;
        for (u32 cp = CP_CARRIED; GV(u8, cp) != ITEM_EMPTY; cp += 6, items += 8, n++) {
            GV(u16, items) = GV(u8, cp);
            GV(u8, items + 4) = GV(u8, cp + 1);
            GV(u16, items + 2) = GV(u16, cp + 2);
            GV(u16, items + 6) = GV(u16, cp + 4);
            s16 id = GV(s16, items);
            if (id < 8) {
                /* guns come back full */
                GV(u16, items + 2) = GV(u16, 0x800A09C8u + (u32)((s32)id * 8) + 4);
            } else if (id == WEAPON_SPARE) {
                if (GV(s16, items + 2) == 0) {
                    GV(u16, items + 2) = 1;
                    GV(u16, items + 6) = (u16)(GV(u16, items + 6) - 1);
                }
                s16 min = DIFFICULTY == 1 ? 0xB : 5;
                if (GV(s16, items + 6) < min) GV(u16, items + 6) = (u16)min;
            }
        }
        GV(u16, LIST_CARRIED + 0x40) = (u16)n;
        if (GV(u8, 0x80062D3C) && !GCALL2(F_FLAG_TEST, CP_KEY_FLAGS, 8)) {
            GCALL2(F_FLAG_SET, KEY_FLAGS, 8);
            GV(u16, items) = 7;
            GV(u16, items + 2) = 1;
            GV(u32, items + 4) = 0xFFFF0010u;
            GV(u16, LIST_CARRIED + 0x40) += 1;
        }
        g_currentWeaponAddr = ITEMS_CARRIED;
        for (u32 i = 0; i < 5; i++) GV(u32, RECORD + i * 4) = GV(u32, CP_RECORD + i * 4);
        for (u32 i = 0; i < 0x3D; i++) GV(u8, MISC + i) = GV(u8, CP_MISC + i);
        GV(u8, 0x80062DBE) = 0;
        if (DIFFICULTY == 1) {
            /* at least two of item 0x14 in list B (the original only
             * looks at the first entry's id, for every entry) */
            s16 cnt = GV(s16, LIST_B + 0x50);
            s16 first = GV(s16, ITEMS_B);
            s32 have = 0;
            for (s16 i = 0; i < cnt; i++)
                if (first == 0x14) have++;
            if (have < 2) {
                s32 add = 2 - have;
                u32 it = ITEMS_B + (u32)((s32)GV(s16, LIST_B + 0x50) * 8);
                for (s16 i = 0; i < add; i++, it += 8) {
                    GV(u16, it) = 0x14;
                    GV(u16, LIST_B + 0x50) += 1;
                }
            }
        }
        reload_held();
        break;
    }
    }
    n_checkpoint_Make();
}
