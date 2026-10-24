/* Saved games (a port feature): save at payphones, load with the title
 * screen's LOAD GAME.
 *
 * The game's own memory card save only keeps the weapons collected (it is
 * offered after dying; loading it starts the story over with them). It
 * also keeps a checkpoint of the player's progress in the kernel's memory
 * (func_800717E8 makes it: story flags, the three item lists, the held
 * weapon, health, play time, and the stage / room / entrance in TEST's
 * data), and whenever TEST starts it rebuilds the game from that
 * checkpoint and loads the room (func_80071CF4). The port adds that to a
 * card save:
 *
 *   saving   using a payphone opens the game's own save screen (as after
 *            dying). The checkpoint, with where the player stood, goes
 *            to a file next to the card save written there
 *            (saves/card1-BASLUS-01087-00.sav for the card's first save),
 *            and afterwards the game goes on from the checkpoint instead of
 *            going back to the title screen;
 *   loading  when LOAD GAME loads a card save that has such a file (and
 *            the card save is still the one it was made with), the game
 *            starts there instead of at the beginning: the story text is
 *            skipped, TEST's checkpoint is replaced with the file's when it
 *            restores it, and once the room is up the player is moved to
 *            the saved spot. */
#include "game_vars.h"
#include "savegame.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

void rt_log(const char *fmt, ...);
const char *card_last_file(int written);
int card_file_digest(const char *full, u32 *out);
unsigned card_write_count(void);

#define F_MAKE_CHECKPOINT 0x800717E8u
#define TITLE_START_FLAG  0x800805BCu          /* title loop: != 0 starts the game */
#define MODE_TEST         1
#define MODE_TYPEW        5                    /* the story text before a new game */
#define GAME_TYPE_LOADED  2                    /* D_80062DF4 after LOAD GAME */

/* What makes up a checkpoint (as func_800717E8 writes it). */
static const struct { u32 addr, len; } blobs[] = {
    {0x80062DB0, 1},          /* weapons carried */
    {0x80062DE4, 1},          /* 0x800A8586 */
    {0x80062E30, 1},          /* health */
    {0x80063498, 0x14},       /* play time counters */
    {0x800634B0, 0x98},       /* story flags */
    {0x80063548, 0x221},      /* item lists, held weapon */
    {0x80063770, 0x3D},       /* 0x800B3E68 */
    {0x800A856C, 12},         /* stage, room, entrance */
};
#define NBLOBS (sizeof blobs / sizeof blobs[0])

#define MAGIC "RESURVGM"
#define VERSION 2

typedef struct {
    char magic[8];
    u32 version;
    u32 stage, room;
    s32 hp;
    u32 play_frames;
    s64 saved_at;             /* unix time */
    s32 pos[3];
    s16 yaw, weapon;          /* held weapon id */
    u32 blob_bytes;
    u32 card_digest;          /* of the card save it goes with */
} Header;

typedef struct {
    Header h;
    u8 blob[0x400];
} Snap;

static char save_dir[512] = "saves";
void sg_set_dir(const char *dir) { snprintf(save_dir, sizeof save_dir, "%s", dir); }

/* ------------------------------------------------------------ state */

static int title_active;      /* the title screen's loop is running */
static int load_pending;      /* a load is under way: load below */
static int place_pending;     /* move the player once the room is up */
static Snap load;
static volatile int save_request;  /* (testing) slot to save into */
static int at_phone;          /* standing at a payphone (for the hint) */
static int phone_save;        /* the card save screen is open from a payphone (2: on screen) */
static unsigned phone_writes; /* card writes when it opened */
static Snap phone;            /* where the game goes on afterwards */

int sg_title_active(void) { return title_active && !load_pending; }
int sg_at_payphone(void) { return at_phone; }

static u32 blob_total(void) {
    u32 n = 0;
    for (size_t i = 0; i < NBLOBS; i++) n += blobs[i].len;
    return n;
}

/* ------------------------------------------------------------ files */

/* The checkpoint and where the player stands, now. */
static void capture(Snap *s) {
    GCALL0(F_MAKE_CHECKPOINT);
    memset(s, 0, sizeof *s);
    Header *h = &s->h;
    memcpy(h->magic, MAGIC, 8);
    h->version = VERSION;
    h->stage = GV(u32, 0x800B4BA0);
    h->room = GV(u32, 0x800B3E20);
    h->hp = GV(s16, 0x800A8584);
    h->play_frames = GV(u32, 0x80063460);
    h->saved_at = (s64)time(NULL);
    for (int i = 0; i < 3; i++) h->pos[i] = PLAYER_POS[i];
    h->yaw = PLAYER_YAW;
    h->weapon = GV(s16, g_currentWeaponAddr);
    h->blob_bytes = blob_total();
    u32 o = 0;
    for (size_t i = 0; i < NBLOBS; i++) {
        memcpy(s->blob + o, gp(blobs[i].addr), blobs[i].len);
        /* The record (shots, hits, kills, play time, ...) is kept in two
         * parts: the stage's so far (0x80063448, five words, cleared when
         * TEST starts) and the totals (0x80063460, which the checkpoint
         * copies). The game adds the stage's part in only at the end of a
         * stage, so put it into the saved totals here, or a loaded game
         * would lose everything since the stage began. */
        if (blobs[i].addr == 0x80063498)
            for (u32 k = 0; k < 5; k++) {
                u32 t;
                memcpy(&t, s->blob + o + k * 4, 4);
                t += GV(u32, 0x80063448 + k * 4);
                memcpy(s->blob + o + k * 4, &t, 4);
            }
        o += blobs[i].len;
    }
    s->h.play_frames = GV(u32, 0x80063460 + 12) + GV(u32, 0x80063448 + 12);
}

static int write_snap(const char *path, const Snap *s) {
    char tmp[640];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    fwrite(&s->h, sizeof s->h, 1, f);
    fwrite(s->blob, s->h.blob_bytes, 1, f);
    int ok = !ferror(f);
    ok &= fclose(f) == 0;
    if (!ok) { remove(tmp); return 0; }
    remove(path);
    if (rename(tmp, path) != 0) return 0;
    rt_log("savegame: saved %s (stage %u room %u)", path, s->h.stage, s->h.room);
    return 1;
}

static int read_snap(const char *path, Snap *s) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    int ok = fread(&s->h, sizeof s->h, 1, f) == 1 && !memcmp(s->h.magic, MAGIC, 8) &&
             s->h.version == VERSION && s->h.blob_bytes == blob_total() &&
             fread(s->blob, s->h.blob_bytes, 1, f) == 1;
    fclose(f);
    return ok;
}

/* "bu00:BASLUS-01087-00" -> saves/card1-BASLUS-01087-00.sav */
static void card_companion(const char *card_file, char *out, size_t n) {
    const char *name = strchr(card_file, ':');
    snprintf(out, n, "%s/card%c-%s.sav", save_dir, card_file[2] == '1' ? '2' : '1',
             name ? name + 1 : card_file);
}

static void start_load(const Snap *s) {
    load = *s;
    load_pending = 1;
}

/* (testing) game<slot>.sav */
static void slot_file(int slot, char *out, size_t n) { snprintf(out, n, "%s/game%d.sav", save_dir, slot); }

int sg_request_load(int slot) {
    char path[600];
    Snap s;
    slot_file(slot, path, sizeof path);
    if (!read_snap(path, &s)) return 0;
    start_load(&s);
    GV(u32, TITLE_START_FLAG) = 1;            /* as if New Game was chosen */
    rt_log("savegame: loading %s", path);
    return 1;
}

void sg_request_save(int slot) { save_request = slot; }

/* ------------------------------------------------------------ hooks */

/* The payphone's save screen closed: keep the progress with the card save
 * (if one was written) and go on playing from it. */
static void phone_save_done(void) {
    phone_save = 0;
    const char *card = card_last_file(1);
    u32 digest;
    if (card_write_count() != phone_writes && *card && card_file_digest(card, &digest)) {
        char path[600];
        card_companion(card, path, sizeof path);
        phone.h.card_digest = digest;
        sg_save_done(write_snap(path, &phone));
    }
    start_load(&phone);
}

/* LOAD GAME loaded a card save: its progress, if it has some. */
static void card_load(void) {
    const char *card = card_last_file(0);
    if (!*card) return;
    char path[600];
    card_companion(card, path, sizeof path);
    Snap s;
    if (!read_snap(path, &s)) return;
    u32 digest;
    if (!card_file_digest(card, &digest) || digest != s.h.card_digest) {
        rt_log("savegame: %s is for another save on the card", path);
        return;
    }
    rt_log("savegame: loading %s", path);
    start_load(&s);
}

/* kernel 80012FF4 (mode): an overlay mode starts. */
void sg_hook_mode_switch(void) {
    title_active = 0;
    rt_log("savegame: mode switch to %u (game type %d)", R[4], D_80062DF4);
    if (R[4] == MODE_TEST && D_80062DF4 == GAME_TYPE_LOADED && !load_pending) {
        card_load();
    }
    if (load_pending && R[4] == MODE_TYPEW) R[4] = MODE_TEST;   /* no story text */
    if (load_pending) {                        /* where TEST starts */
        GV(u32, 0x80062DB8) = load.h.stage;
        GV(u32, 0x80062D9C) = load.h.room;
        /* the live story flags too: TEST checks some (the opening) before
         * it restores the checkpoint */
        u32 o = 0;
        for (size_t i = 0; i < NBLOBS; i++) {
            if (blobs[i].addr == 0x800634B0) memcpy(gp(0x80062F60), load.blob + o, 0x94);
            o += blobs[i].len;
        }
    }
}

/* title 8006C244: the title screen's loop (also the save screen's). */
void sg_hook_title(void) { title_active = 1; }

/* title 8006CF24: every frame of it, by its screen (D_800805D4: 17 the
 * save screen, then a fade to the title). When the payphone's save screen
 * is done, start the game again at once, from where it was. */
#define TITLE_SCREEN      0x800805D4u
#define SCREEN_SAVE       17
void sg_hook_title_frame(void) {
    if (phone_save == 1 && GV(u32, TITLE_SCREEN) == SCREEN_SAVE) phone_save = 2;
    if (phone_save == 2 && GV(u32, TITLE_SCREEN) != SCREEN_SAVE) {
        phone_save_done();
        GV(u32, TITLE_START_FLAG) = 1;        /* as if New Game was chosen */
    }
}

/* test 80071CF4: TEST rebuilds the game from the checkpoint. */
void sg_hook_restore(void) {
    if (!load_pending) return;
    load_pending = 0;
    u32 o = 0;
    for (size_t i = 0; i < NBLOBS; i++) {
        memcpy(gp(blobs[i].addr), load.blob + o, blobs[i].len);
        o += blobs[i].len;
    }
    D_80062DF4 = 1;                           /* "continuing": restore, not a new game */
    /* the restore keeps holding the weapon with the id held now */
    /* (at the start of TEST the pointer is still 0: point it at the first
     * item entry, which the restore rewrites anyway) */
    g_currentWeaponAddr = 0x800AF800u;
    GV(s16, 0x800AF800) = load.h.weapon;
    /* (the came-from stage, 0x800A866C, stays -1: the room load runs the
     * stage's set-up only when it differs from the stage entered) */
    place_pending = 1;
    rt_log("savegame: restored stage %u room %u (weapon %d)", load.h.stage, load.h.room, load.h.weapon);
}

/* test 8008F3BC (line, frames): TEST's start-up shows the opening lines
 * ("My head aches...") between its loading steps; the last one drawn stays
 * up while the next step loads. When loading a saved game (or going on
 * after a save), don't show them. */
void f_test_8008F3BC(void);
void n_sg_Monologue(void) {
    if (load_pending || place_pending) return;
    f_test_8008F3BC();
}

/* kernel 80039C58 (pattern, dirent): libapi's firstfile. It looks the
 * device up in the BIOS's device table in low RAM (0x150) to patch its
 * search function before calling the BIOS; the port's BIOS has no such
 * table, so it found no device and every search came back empty (the
 * title's LOAD GAME said "No save data"). Call the BIOS directly. */
void bios_call(u32 table, u32 fn);
void n_card_FirstFile(void) {
    u32 src = R[4], dst = 0x8006BCF8u;            /* the device name, as the original keeps it */
    while ((s8)GV(u8, src) >= 0x3B) GV(u8, dst++) = GV(u8, src++);
    GV(u8, dst) = 0;
    bios_call(0xB0, 0x42);
}

/* ------------------------------------------------------------ payphones */

/* Where the save points are (payphones, a desk phone, a game console, a
 * computer): the stage and room, a spot in front of the phone
 * and how far from it (and how squarely facing it) the player may stand,
 * and the story flag after which it can be used (the first one rings and
 * plays a scene first). */
static const struct {
    int stage, room;
    s32 x, z, radius;
    s16 yaw;                  /* facing the phone */
    u32 flags;                /* flag array, bit; 0 = always */
    int bit;
} phones[] = {
    {7, 1, 12030, 200, 700, 2048, 0x80062FC0u, 43},     /* street, first phone */
    {7, 3, 14286, 1520, 500, 2024, 0, 0},              /* payphone */
    {3, 4, 800, -1708, 500, 2516, 0, 0},               /* office desk telephone */
    {16, 6, 1600, -906, 500, 3150, 0, 0},              /* television and game console */
    {2, 8, 7936, 1856, 500, 3877, 0, 0},               /* computer terminal */
    {2, 14, -1376, -144, 500, 1066, 0, 0},             /* lab computer terminals */
    {12, 0, -40, 8194, 700, 2035, 0, 0},               /* hospital lobby payphones */
};

static int find_phone(void) {
    int stage = GV(s32, 0x800B4BA0), room = GV(s32, 0x800B3E20);
    for (size_t i = 0; i < sizeof phones / sizeof phones[0]; i++) {
        if (phones[i].stage != stage || phones[i].room != room) continue;
        s64 dx = PLAYER_POS[0] - phones[i].x, dz = PLAYER_POS[2] - phones[i].z;
        if (dx * dx + dz * dz > (s64)phones[i].radius * phones[i].radius) continue;
        int d = ((PLAYER_YAW - phones[i].yaw) & 0xFFF);
        if (d > 0x800) d = 0x1000 - d;
        if (d > 0x2AA) continue;                         /* within 60 degrees */
        if (phones[i].flags) {
            u32 w = GV(u32, phones[i].flags + (u32)(phones[i].bit >> 5) * 4);
            if (!(w & (0x80000000u >> (phones[i].bit & 31)))) continue;
        }
        return (int)i;
    }
    return -1;
}

volatile int g_sg_use;

/* Open the game's save screen, as after dying: the game-over handler (3)
 * at its step 15 (make the checkpoint, TITLE's save screen). */
static void open_save_screen(void) {
    capture(&phone);
    phone_save = 1;
    phone_writes = card_write_count();
    GV(u32, 0x800BB19C) = 3;
    GV(u32, 0x800A28CC) = 15;
    rt_log("savegame: payphone: opening the save screen");
}

/* Every frame from player_Update (game thread, a safe place to call game
 * code). use = the player pressed the action button this frame. */
void sg_game_frame(int use) {
    if (place_pending && D_80062DEC == 1 && GV(s16, 0x800B3D2C) == 0) {
        place_pending = 0;
        /* the record: TEST's start-up clears the totals after the restore
         * (as for a new stage), so put the saved ones back now */
        u32 o = 0;
        for (size_t i = 0; i < NBLOBS; i++) {
            if (blobs[i].addr == 0x80063498) memcpy(gp(0x80063460), load.blob + o, 0x14);
            o += blobs[i].len;
        }
        if ((u32)GV(s32, 0x800B4BA0) == load.h.stage && (u32)GV(s32, 0x800B3E20) == load.h.room) {
            for (int i = 0; i < 3; i++) PLAYER_POS[i] = load.h.pos[i];
            PLAYER_YAW = load.h.yaw;
            GV(s32, 0x800B4E08) = load.h.pos[0];          /* last position, for collisions */
            GV(s32, 0x800B4E0C) = load.h.pos[2];
        }
    }
    int slot = save_request;
    if (slot) {
        save_request = 0;
        Snap s;
        char path[600];
        capture(&s);
        slot_file(slot, path, sizeof path);
        sg_save_done(write_snap(path, &s));
    }
    int can_act = D_80062DEC == 1 && GV(s16, 0x800B3D2C) == 0 && GV(u32, 0x800BB19C) != 3;
    at_phone = can_act && find_phone() >= 0;
    if (at_phone && use && !phone_save) open_save_screen();
}
