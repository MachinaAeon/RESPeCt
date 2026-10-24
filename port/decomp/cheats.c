/* Weapon and health options of the PC port: manual reload (R), and the
 * cheats in the settings panel. Run from player_Update every game frame,
 * before the frame's shot and weapon-view code, so they never race the
 * game's own ammo handling.
 *
 * The game's ammo model (TEST overlay, see func_800956F8 = reload check):
 *   weapons and items share 8-byte entries at 0x800AF800:
 *     +0 id, +2 count (rounds loaded, or the item's quantity; -1 = no limit),
 *     +4 the id of the ammo item this weapon takes, +6 spare rounds kept on
 *     the weapon itself (0 = use ammo items, negative = no limit)
 *   the inventory (0x800A8880) lists entry indices, how many at +0x40
 *   magazine sizes: 0x800A09C8, 8 bytes per weapon id, size at +4
 *   firing at 0 rounds sets bit 15 of g_fireCooldown ("empty"); while a
 *   weapon is empty the weapon view runs the reload check every frame, which
 *   takes rounds from the spare and starts the reload animation; when that
 *   ends, the rounds (staged at 0x800C39C0) are loaded and the bit cleared.
 */
#include "game_vars.h"
#include <stdlib.h>
#include <string.h>
#include <stdlib.h>

#define ENTRIES        GA(s16, 0x800AF800)
#define INVENTORY      GA(u8, 0x800A8880)
#define INVENTORY_N    GV(s16, 0x800A8880 + 0x40)
#define MAG_SIZE(id)   GV(s16, 0x800A09C8 + (u32)(id) * 8 + 4)
#define PLAYER_HP      GV(s16, 0x800A8584)
#define HP_MAX         200
#define HUD_ROUNDS     GV(s16, 0x800C39C0)       /* rounds shown on the HUD */
#define WEAPON_GRENADE 8

int g_cheat_god, g_cheat_infinite_ammo, g_cheat_no_reload;
volatile int g_reload_request;                    /* set by the platform layer (R) */
/* 1-9: the nth weapon in the inventory; WEAPON_NEXT / WEAPON_PREV (wheel) */
volatile int g_weapon_request;
volatile int g_give_all_request;                  /* cheat: every weapon and ammo */
#define WEAPON_NEXT 10
#define WEAPON_PREV 11

#define ENTRY_EMPTY     0x1E
#define IS_WEAPON(id)   ((id) >= 0 && (id) < 0x24 && (GV(u8, 0x800A09C8 + (u32)(id) * 8 + 6) & 0x10))
#define AMMO_ITEM(id)   GV(u8, 0x800A09C8 + (u32)(id) * 8)
#define F_WEAPON_VIEW   0x80094AE4u               /* (id, 0x44): show the new gun */
#define F_WEAPON_AFTER  0x8008E324u
#define F_WEAPON_GFX    0x80092878u               /* (data, base, id): HUD icon etc. */
#define WEAPON_DATA     0x800C3E50u               /* weapon files, loaded with TEST */

static s16 *weapon(void) { return gp(g_currentWeaponAddr); }

/* The spare ammo for weapon w: a pointer to the count, or NULL if none. */
static s16 *spare_for(s16 *w) {
    if (w[3] != 0) return &w[3];                  /* kept on the weapon */
    if (w[0] == WEAPON_GRENADE) return NULL;
    s32 n = INVENTORY_N;
    for (s32 i = 0; i < n && i < 0x40; i++) {
        s16 *e = &ENTRIES[INVENTORY[i] * 4];
        if (e[0] == w[2]) return &e[1];
    }
    return NULL;
}

static s32 is_gun(s16 *w) { return w[0] >= 0 && w[0] <= WEAPON_GRENADE; }

/* Move up to want rounds from the spare; returns how many. */
static s32 take_spare(s16 *w, s32 want) {
    s16 *sp = spare_for(w);
    if (!sp || *sp == 0) return 0;
    if (*sp < 0) return want;                     /* no limit */
    s32 got = *sp < want ? *sp : want;
    *sp = (s16)(*sp - got);
    return got;
}

static void reload_by_hand(void) {
    s16 *w = weapon();
    if (!is_gun(w)) return;
    s32 mag = MAG_SIZE(w[0]), loaded = w[1];
    if (loaded <= 0 || loaded >= mag) return;     /* empty (reloads anyway) or full */
    if (g_fireCooldown & 0x8000) return;          /* already reloading */
    s16 *sp = spare_for(w);
    if (!sp || *sp == 0) return;                  /* nothing to reload with */
    if (*sp > 0) *sp = (s16)(*sp + loaded);       /* rounds go back with the rest */
    w[1] = 0;
    g_fireCooldown |= 0x8000;                     /* empty: the game reloads it */
}

/* ------------------------------------------------------------ switching */

/* Entry indices of the weapons in the inventory, in its order. */
static s32 weapon_list(s32 *out) {
    s32 n = 0, count = INVENTORY_N;
    for (s32 i = 0; i < count && i < 0x40; i++) {
        s32 e = INVENTORY[i];
        if (IS_WEAPON(ENTRIES[e * 4])) out[n++] = e;
    }
    return n;
}

/* Equipping: what the inventory screen does (conpau 800C7888), then what
 * the game does when play resumes after it (func_8009493C, the weapon HUD
 * and weapon view setup) without its disc loads: the weapon images come
 * from hud.c instead. Without the second half the new gun's fire and reload
 * handlers aren't installed. The HUD panels are freed and rebuilt as after
 * the inventory screen (func_80092DD0, func_80093444): the bullet panel
 * and the weapon view keep per-weapon state (and child panels), and stale
 * ones corrupt the panel list after switching from e.g. the grenade
 * launcher. */
void hud_show_weapon(int id);               /* hud.c */
void hud_weapon_done(void);
void hud_show_ammo(int item);
#define D_800C39C0_ADDR 0x800C39C0u
#define F_HUD_RESET     0x80092DD0u
#define F_HUD_BUILD     0x80093444u         /* (panel table) */
#define HUD_PANELS      0x800A65C8u

/* Each weapon has its own sound bank (VAB) in sound slot 1: bank file
 * id + 1. The game loads it (func_8008E324 -> func_80011CB4) by reading the
 * file into RAM at 0x800E5E48 - free while the inventory screen is up, but
 * part of the running stage otherwise - so here it's read on the PC side.
 * Every weapon bank has the same layout (one program, a 44272-byte body),
 * so the bank already open is updated in place: the new header goes into
 * slot 1's header buffer with the sample addresses worked out the way the
 * VAB open (func_80024868) does, and the body is written into the SPU RAM
 * block the bank already owns. */
int cd_read_sector_raw(uint32_t lba, uint8_t out[2352]);
void spu_write_ram(uint32_t addr, const uint8_t *data, uint32_t n);
#define SND_SLOT1        0x800637CCu      /* {header buffer, words, vab id, file} */
#define VAB_SPU_ADDR     0x800657C0u
#define VAB_BODY_SIZE    0x80065778u
#define BCD(x) (((x) >> 4) * 10 + ((x) & 15))

static void load_weapon_sounds(s32 file) {
    const u8 *loc = gp(0x80058510);
    s32 base = (BCD(loc[0]) * 60 + BCD(loc[1])) * 75 + BCD(loc[2]) - 150;
    s32 first = GV(s32, 0x80058D2C + (u32)file * 4), last = GV(s32, 0x80058D2C + (u32)file * 4 + 4);
    s16 vab = GV(s16, SND_SLOT1 + 8);
    if (base <= 0 || last <= first || last - first > 64 || vab < 0 || vab > 15) return;
    size_t n = (size_t)(last - first) * 2048;
    uint8_t *f = malloc(n), raw[2352];
    for (s32 i = 0; i < last - first; i++) {
        if (!cd_read_sector_raw((uint32_t)(base + first + i), raw)) { free(f); return; }
        memcpy(f + (size_t)i * 2048, raw + 24, 2048);
    }
    uint32_t h0 = f[4] | f[5] << 8 | f[6] << 16 | (uint32_t)f[7] << 24;
    uint32_t h1 = f[8] | f[9] << 8 | f[10] << 16 | (uint32_t)f[11] << 24;
    uint32_t words = (h1 - h0) / 4;
    u32 hdr = GV(u32, SND_SLOT1);
    if (h0 < 12 || h1 <= h0 || h1 >= n || words != GV(u32, SND_SLOT1 + 4)) { free(f); return; }
    const uint8_t *nh = f + h0;
    /* same shape as the bank already open: magic, version, program count */
    if (memcmp(nh, gp(hdr), 8) || nh[0x12] != GV(u8, hdr + 0x12) || nh[0x13] != GV(u8, hdr + 0x13)) { free(f); return; }

    /* What the open (func_80024868) works out from a header: */
    u32 version = nh[4] | nh[5] << 8 | nh[6] << 16 | (uint32_t)nh[7] << 24;
    s32 ps = nh[0x12] | nh[0x13] << 8, vs = nh[0x16];
    s32 max_progs = GV(s16, 0x80065722);
    const uint8_t *vag = nh + 0x20 + (u32)max_progs * 16 + (u32)ps * 512;
    u32 sizes[256], total = 0;
    for (s32 i = 0; i <= vs && i < 256; i++) {
        u32 v = vag[2 * i] | vag[2 * i + 1] << 8;
        sizes[i] = version >= 5 ? v << 3 : v << 2;
        total += sizes[i];
    }
    total = (total + 0x3F) & ~0x3Fu;
    u32 spu = GV(u32, VAB_SPU_ADDR + (u32)vab * 4);
    /* the block bank 1 already has in SPU RAM must hold the new sounds */
    u32 list = GV(u32, 0x8005983C), room = 0;
    for (int i = 0; i < 32; i++) {
        u32 e = GV(u32, list + 8 * (u32)i);
        if ((e & 0x0FFFFFFF) == spu) { room = GV(u32, list + 8 * (u32)i + 4); break; }
        if (e & 0x40000000) break;
    }
    if (total > room || spu + total > 0x80000 || h1 + total > n + 0x800) { free(f); return; }

    memcpy(gp(hdr), nh, words * 4);
    u32 progs = hdr + 0x20;
    for (s32 i = 0, k = 0; i < max_progs; i++)
        if (GV(u8, progs + (u32)i * 16)) GV(s32, progs + (u32)i * 16 + 8) = k++;
        else GV(s32, progs + (u32)i * 16 + 8) = k;
    u32 acc = 0;
    for (s32 i = 0; i <= vs && i < 256; i++) {
        acc += sizes[i];
        GV(u16, progs + (u32)(i / 2) * 16 + (i & 1 ? 0xE : 0xC)) = (u16)((spu + acc) >> 3);
    }
    GV(u32, VAB_BODY_SIZE + (u32)vab * 4) = acc;
    u32 body = acc;
    if (body > n - h1) body = (u32)(n - h1);
    spu_write_ram(spu, f + h1, body);
    GV(s16, SND_SLOT1 + 10) = (s16)file;
    free(f);
}

/* Free every HUD panel and build the HUD again for weapon id (the second
 * half of equipping). */
static void hud_rebuild(s16 id) {
    GCALL0(F_HUD_RESET);                    /* func_80092DD0: free all HUD panels */
    /* func_8009493C */
    GV(s16, 0x800C39B0) = 0;
    GV(s16, 0x800C3A00) = 0;
    GV(u8, 0x800C39FC) = 0;
    GV(s16, 0x800C39F2) = 0;
    for (u32 i = 0; i < 0x11; i++) GV(s16, D_800C39C0_ADDR + 4 + i * 2) = -1;
    hud_show_weapon(id);
    hud_show_ammo(GV(s16, g_currentWeaponAddr + 4));             /* func_80094C60 */
    GCALL2(F_WEAPON_VIEW, (u32)(s32)id, 0x44);       /* copies the image into the HUD */
    hud_weapon_done();
    GV(u32, 0x800C39F4) = GV(u32, 0x800A62A0 + (u32)id * 4);   /* weapon view handlers */
    GV(u32, 0x800C39F8) = GV(u32, 0x800A62E8 + (u32)id * 4);
    GCALL1(F_HUD_BUILD, HUD_PANELS);        /* rebuild them for the new weapon */
}

static void equip(s32 e) {
    u32 addr = 0x800AF800u + (u32)e * 8;
    if (addr == g_currentWeaponAddr) return;
    s16 id = ENTRIES[e * 4];
    GCALL4(F_SOUND_CUE, 0, 2, 0x7F, 0x7F);
    g_currentWeaponAddr = addr;
    D_800AFCB0 &= (u16)~8;
    g_fireCooldown &= 0x7FFF;
    if (GV(s16, SND_SLOT1 + 10) != id + 1) load_weapon_sounds(id + 1);   /* func_8008E324 */

    hud_rebuild(id);
}

static void switch_weapon(int req) {
    s32 list[0x40];
    s32 n = weapon_list(list);
    if (!n) return;
    if (req >= 1 && req <= 9) {
        if (req <= n) equip(list[req - 1]);
        return;
    }
    s32 cur = 0;
    for (s32 i = 0; i < n; i++)
        if (0x800AF800u + (u32)list[i] * 8 == g_currentWeaponAddr) cur = i;
    cur = req == WEAPON_NEXT ? (cur + 1) % n : (cur + n - 1) % n;
    equip(list[cur]);
}

/* ------------------------------------------------------------ give all */

static s32 find_entry(s16 id) {
    s32 count = INVENTORY_N;
    for (s32 i = 0; i < count && i < 0x40; i++)
        if (ENTRIES[INVENTORY[i] * 4] == id) return INVENTORY[i];
    return -1;
}

/* A table entry no inventory slot uses, or -1. */
static s32 free_entry(void) {
    s32 count = INVENTORY_N;
    for (s32 e = 0; e < 0x40; e++) {
        if (ENTRIES[e * 4] != ENTRY_EMPTY) continue;
        s32 used = 0;
        for (s32 i = 0; i < count && i < 0x40; i++) if (INVENTORY[i] == e) used = 1;
        if (!used) return e;
    }
    return -1;
}

/* Into the inventory: reuse an empty slot in it, or add one. */
static s32 add_entry(s16 id, s16 count, s16 ammo_item, s16 spare) {
    s32 n = INVENTORY_N, e = -1;
    for (s32 i = 0; i < n && i < 0x40; i++)
        if (ENTRIES[INVENTORY[i] * 4] == ENTRY_EMPTY) { e = INVENTORY[i]; break; }
    if (e < 0) {
        if (n >= 0x40 || (e = free_entry()) < 0) return -1;
        INVENTORY[n] = (u8)e;
        INVENTORY_N = (s16)(n + 1);
    }
    s16 *x = &ENTRIES[e * 4];
    x[0] = id; x[1] = count; x[2] = ammo_item; x[3] = spare;
    return e;
}

static void give_all(void) {
    for (s16 id = 0; id <= WEAPON_GRENADE; id++) {
        if (!IS_WEAPON(id) || find_entry(id) >= 0) continue;
        s16 mag = MAG_SIZE(id);
        /* the grenade launcher keeps its rounds on itself, the rest use items */
        add_entry(id, mag, AMMO_ITEM(id), id == WEAPON_GRENADE ? 30 : 0);
    }
    /* a good stack of each ammo type that isn't unlimited already */
    for (s16 id = 0; id <= WEAPON_GRENADE; id++) {
        s16 item = AMMO_ITEM(id);
        if (id == WEAPON_GRENADE || item == ENTRY_EMPTY) continue;
        s32 e = find_entry(item);
        if (e < 0) add_entry(item, 99, 0, 0);
        else if (ENTRIES[e * 4 + 1] >= 0 && ENTRIES[e * 4 + 1] < 99) ENTRIES[e * 4 + 1] = 99;
    }
}

/* The weapon got rounds outside the game's reload: rebuild the HUD's bullet
 * row for them, as the game does when the weapon changes (free the round
 * panels, lay out the row: 80095CDC, 80095B90, which also sets the rounds
 * shown). Just setting the count left the row's panels out of step: the
 * old ones stayed on screen, and with the cheat off later the row never
 * emptied. */
static void refill(s16 *w, s32 rounds) {
    w[1] = (s16)rounds;
    GCALL0(0x80095CDCu);
    GCALL3(0x80095B90u, 0x62, 0xD4, rounds);
}

/* Bullet-row panels left over from the cheats' old refills (or anything
 * else): live round panels (flags 0x306, as 80097B48 makes them) that the
 * row no longer lists stay on screen for good, a full row of rounds over
 * the real one. When there are more of them than the row lists (allowing
 * for rounds still flying off after a shot), the HUD is built again. */
static void heal_bullet_row(void) {
    if (!D_80062DEC || (g_fireCooldown & 0x8000) || GV(s16, 0x800B3D2C)) return;
    s16 *w = weapon();
    if (!is_gun(w)) return;
    s32 listed = 0, live = 0;
    for (int i = 0; i < 0x11; i++) if (GV(s16, 0x800C39C4u + (u32)i * 2) != -1) listed++;
    for (u32 i = 0; i < 0xB4; i++) if (GV(u16, 0x800C09D8u + i * 0x44 + 0x1C) == 0x306) live++;
    if (live > listed + 4) hud_rebuild(w[0]);
}

/* Called at the start of every player_Update. */
void port_weapon_frame(void) {
    if (g_cheat_god && PLAYER_HP > 0 && PLAYER_HP < HP_MAX) PLAYER_HP = HP_MAX;

    if (g_give_all_request) { g_give_all_request = 0; give_all(); }
    heal_bullet_row();
    int wreq = g_weapon_request;
    g_weapon_request = 0;
    if (wreq && D_80062DEC) switch_weapon(wreq);

    s16 *w = weapon();
    int reload = g_reload_request;
    g_reload_request = 0;
    if (!is_gun(w)) return;
    s32 mag = MAG_SIZE(w[0]);
    if (mag <= 0) return;
    if (g_fireCooldown & 0x8000) {
        /* Empty with nothing to reload from ("out of ammo", flag 8): a cheat
         * switched on now brings the gun back. Otherwise it's mid-reload:
         * leave it to the game. */
        if (!(D_800AFCB0 & 8) || w[1] != 0) return;
        s32 got = g_cheat_infinite_ammo ? mag : g_cheat_no_reload ? take_spare(w, mag) : 0;
        if (!got) return;
        refill(w, got);
        g_fireCooldown &= 0x7FFF;
        D_800AFCB0 &= (u16)~8;
        return;
    }

    if (g_cheat_infinite_ammo) {
        if (w[1] < mag) refill(w, mag);
        return;
    }
    if (g_cheat_no_reload) {
        if (w[1] < mag) {
            s32 got = take_spare(w, mag - w[1]);
            if (got) refill(w, w[1] + got);
        }
        return;
    }
    if (reload) reload_by_hand();
}
