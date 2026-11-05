/* Mods: files that take the place of the disc's (the mods folder next to
 * the exe; RT_MODS for the headless runner).
 *
 * mods/<name>/ holds files by the disc's own paths (B/STGBG7.BIN), or by
 * the PC version's PSX_DATA paths (stage/STAGE07/0700/0700_A.hmd): those
 * are the very files packed in the PS1 disc's archives (modfs_pc.h), and
 * go in at their place there. Mods apply in the order mods/order.txt lists
 * them (one folder per line), else by name; a later one wins.
 *
 * The game reads the disc by sector (its own tables give each file's
 * place). A whole file can use up to its last sector; a file inside an
 * archive its original size (the rest of its place is left as zeros), or
 * more in the stages' archives (B/STGBGn.BIN: the rooms): the room's block
 * is laid out again with it, the archive moves past the end of the disc
 * (its directory entry says where) and the game's table of its blocks
 * (in TEST.BIN, 0x800A5B90) is changed to match, up to what the room's
 * object file can take in memory (0x80108000-0x80150000, or more with the
 * rooms' memory moved: room_window_place). Files that hold
 * game code (the game runs translated, from what it read) can't be
 * replaced, nor streams whole; their sound goes by channel instead:
 * X/BGM_32.STR/05.wav is what the game plays from that file's channel 5
 * (music, voices, a movie's sound; the PC version's wav/Scene05_1.wav and
 * the like by their PC names, modfs_wav.h), sample for sample in step
 * with the disc's, so loops, cuts and the end stay the game's; and their
 * pictures by frame: M/OPENING.STR/0042.png is shown when the game shows
 * that stream's frame 42. The sound banks go by sound (NOISE/012/03.wav),
 * the rooms' music by sequence (NOISE/236/music1.seq, or .wav for a
 * recording). What is and isn't used is in the log. */
#ifdef _WIN32
#include <windows.h>
#endif
#include "rt.h"
#include "disc_ids.h"
#include "modfs_pc.h"
#include "modfs_bmp.h"
#include "modfs_pack.h"
#include "modfs_boss.h"
#include "modfs_wav.h"
#include "modfs_pc_bgm.h"
#include "modfs_pc_sfx.h"
#include "modfs_pc_sfx_events.h"
#include "modfs_pc_noise.h"
#include "modmem.h"
#include "modfs_memory.h"
#include "modio.h"
#define MODIO_BOOT_STAGING 1

#include <ctype.h>
#include <stdarg.h>
#include <math.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(p) _mkdir(p)
#else
#define mkdir(p) mkdir(p, 0777)
#endif

#include "stb/stb_image.h"

char g_mods_dir[1024];                           /* set by the platform; "" = none */
char g_movie_dump[1024];                         /* set by the platform: write the movies' pictures there; "" = don't */

typedef struct { uint32_t lba, nsec; uint8_t *data; const char *name; } Touched;
static Touched touched[128];
static int ntouched;
static int disc_only;                            /* reading the disc's own sectors */
unsigned long mz_crc32(unsigned long crc, const unsigned char *ptr, size_t buf_len);   /* miniz */
static int nused, nmods, nskipped;
static int mod_memory_models;

/* A line in the log; what a mod gives that isn't used is counted (the
 * player is told how many on start). */
static void note(const char *fmt, ...) {
    char b[1600];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    rt_log("%s", b);
    if (strstr(b, "not used") || strstr(b, "can't be replaced") || strstr(b, "couldn't") || strstr(b, "not shown") ||
        strstr(b, "no such folder"))
        nskipped++;
}

/* Files inside an archive that grow (done once every mod is in). */
typedef struct { int fi; uint32_t offset, size, orig, cap; uint8_t *data; char what[300]; } Grow;   /* cap: its own limit (0: its kind's) */
static Grow grows[256];
static int ngrows;
static uint32_t vnext;                           /* sectors used past the end of the disc */
uint32_t disc_image_sectors(void);               /* discimg.c */

static int disc_index(const char *name) {
    for (size_t i = 0; i < sizeof disc_files / sizeof disc_files[0]; i++)
        if (!strcasecmp(disc_files[i].name, name)) return (int)i;
    return -1;
}

/* A whole/raw archive may contain model data even when it did not pass
 * through the NAB/NAC converter. Keep the opt-in heap safe for every
 * accepted entry point into these archives. */
static void memory_mark_file(int fi) {
    if (fi >= 0 && (!strcmp(disc_files[fi].name, "OBJ.DAT") ||
                   !strcmp(disc_files[fi].name, "EV_OBJ.DAT") ||
                   !strcmp(disc_files[fi].name, "ITEMDAT.DAT"))) mod_memory_models = 1;
}

/* The disc file's copy to change (read from the image the first time). */
static Touched *touch(int fi) {
    if (fi < 0) return NULL;
    for (int i = 0; i < ntouched; i++)
        if (touched[i].lba == disc_files[fi].lba) return &touched[i];
    if (ntouched == (int)(sizeof touched / sizeof touched[0])) return NULL;
    Touched *t = &touched[ntouched];
    t->lba = disc_files[fi].lba;
    t->nsec = (disc_files[fi].size + 2047) / 2048;
    t->name = disc_files[fi].name;
    t->data = malloc((size_t)t->nsec * 2048);
    if (!t->data) return NULL;
    uint8_t s[2352];
    disc_only = 1;
    for (uint32_t k = 0; k < t->nsec; k++) {
        cd_read_sector_raw(t->lba + k, s);
        memcpy(t->data + (size_t)k * 2048, s + 24, 2048);
    }
    disc_only = 0;
    ntouched++;
    return t;
}

/* Sectors to change that aren't a file of the list (a directory's). */
static Touched *touch_lba(uint32_t lba, uint32_t nsec, const char *name) {
    for (int i = 0; i < ntouched; i++)
        if (touched[i].lba == lba) return &touched[i];
    if (ntouched == (int)(sizeof touched / sizeof touched[0])) return NULL;
    Touched *t = &touched[ntouched];
    t->lba = lba;
    t->nsec = nsec;
    t->name = name;
    t->data = malloc((size_t)nsec * 2048);
    if (!t->data) return NULL;
    uint8_t s[2352];
    disc_only = 1;
    for (uint32_t k = 0; k < nsec; k++) {
        cd_read_sector_raw(lba + k, s);
        memcpy(t->data + (size_t)k * 2048, s + 24, 2048);
    }
    disc_only = 0;
    ntouched++;
    return t;
}

static int sound_apply(const char *mod, const char *rel, const char *up, const char *path);
static int sfx_apply(const char *mod, const char *rel, const char *up, const char *path);
static int pc_noise_apply(const char *mod, const char *rel, const char *up, const char *path);
static int text_apply(const char *mod, const char *rel, const char *up, const char *path);
static int pc_bitmaps_source(const char *mod, const char *up);
static void pc_bitmaps_load(const char *mod, const char *root);
static int diary_apply(const char *mod, const char *rel, const char *up, const char *path);
static void pc_diary_disable(unsigned doc);
static int own_picture_apply(const char *mod, const char *rel, const char *up, const char *path);
static void texts_done(void);
static int data_apply(const char *mod, const char *rel, const char *up, const char *path);
static void data_done(void);
static int write_out(const char *rel, const uint8_t *d, size_t n);
static void sfx_done(void);
static int movie_apply(const char *mod, const char *rel, const char *up, const char *path);
static int bmp_apply(const char *mod, const char *rel, const char *up, const char *path);
static int picture_apply(const char *mod, const char *rel, const char *up, const char *path);
static int enemy_apply(const char *mod, const char *rel, const char *up, const char *path);
static void enemies_done(void);
static int pack_apply(const char *mod, const char *rel, const char *up, const char *path);
static void packs_done(void);

static uint8_t *load(const char *path, long *n) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    *n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *d = malloc((size_t)(*n > 0 ? *n : 1));
    if (d && fread(d, 1, (size_t)*n, f) != (size_t)*n) { free(d); d = NULL; }
    fclose(f);
    return d;
}

/* The archives whose files can grow, besides the stages' (B/STGBGn.BIN):
 * where KERNEL.BIN keeps their place on the disc (CdlLOC) and TEST.BIN
 * their block table (the first sector of each block, then the end; the
 * sound banks', NOISE.BIN's, is in KERNEL.BIN). A block can grow to the
 * biggest of its kind the game itself loads (kinds: the block's number
 * modulo `kinds`), or to `cap`: the doors' are by turns a door's model,
 * loaded below 0x80150000 (room_change.c), and its data at 0x80150000;
 * the effects' blocks (shot_effects.c) all go to one buffer; an enemy's
 * block (OBJ.DAT, type 30's in EV_OBJ.DAT) is read into 0x800F8000 and
 * unpacked from there (room_loader.c), as big as the biggest of them
 * (type 18's, 93 sectors); a room's sound set (NOISE.BIN: its bank and
 * music) is read where the room's block is (room_sound.c). OBJ.DAT's
 * table has copies in many of the game's modules (the overlays each have
 * theirs): `copies` has every copy found in them changed too. */
static const struct { const char *name; uint32_t loc, tab, kinds, cap; int copies; } archives[] = {
    {"AEX.DAT", 0x80058538u, 0x800A2B3Cu, 1, 0, 0},
    {"B/DOORANM.BIN", 0x8005857Cu, 0x800A42F4u, 2, 0, 0},
    {"EFFECTX.BIN", 0x80058544u, 0x800A6F1Cu, 1, 0, 0},
    {"OBJ.DAT", 0x80058530u, 0x800A0CD8u, 1, 93u * 2048, 1},
    {"EV_OBJ.DAT", 0x80058534u, 0x800A195Cu, 1, 93u * 2048, 1},
    {"ITEMDAT.DAT", 0x8005855Cu, 0x800A3EDCu, 1, 0, 1},
    {"NOISE.BIN", 0x80058510u, 0x80058D2Cu, 1, 142u * 2048, 0},
    {"ROOMENV.DAT", 0x80058550u, 0x800A32F0u, 1, 0, 0},
};

static int can_grow(const char *disc) {
    if (!strcasecmp(disc,"AEX.DAT")) return modmem_enabled();
    if (!strncasecmp(disc, "B/STGBG", 7)) return 1;
    for (size_t k = 0; k < sizeof archives / sizeof archives[0]; k++)
        if (!strcasecmp(disc, archives[k].name)) return 1;
    return 0;
}

/* Save-state compatibility: content and actual application order,
 * independent of absolute root paths. This is a change detector, not
 * an authenticity check. Include source files conservatively, even when
 * an individual file is rejected by a format-specific loader. */
static uint64_t mod_signature = UINT64_C(14695981039346656037), mod_file_hash;
static uint32_t mod_file_count;
static uint64_t mod_hash(uint64_t h, const void *data, size_t n) {
    const uint8_t *p = data;
    for (size_t i = 0; i < n; i++) { h ^= p[i]; h *= UINT64_C(1099511628211); }
    return h;
}
/* A mod's file in the mods' identity (quick saves): its name and size,
 * and its bytes: all of them up to 1 MB; for a bigger one (a stream's
 * WAV, a whole archive) its time of change, the first and the last 64 KB
 * (reading every big file again took longer than the mods themselves). */
static void mod_hash_file(const char *up, const char *path) {
    uint64_t h = mod_hash(UINT64_C(14695981039346656037), up, strlen(up) + 1);
    struct stat st;
    FILE *f = stat(path, &st) ? NULL : fopen(path, "rb");
    if (!f) return;
    int64_t meta[2] = {(int64_t)st.st_size, st.st_size > (1 << 20) ? (int64_t)st.st_mtime : 0};
    h = mod_hash(h, meta, sizeof meta);
    static uint8_t buf[1 << 16];
    size_t n;
    if (st.st_size <= (1 << 20))
        while ((n = fread(buf, 1, sizeof buf, f)) != 0) h = mod_hash(h, buf, n);
    else {
        n = fread(buf, 1, sizeof buf, f);
        h = mod_hash(h, buf, n);
        fseek(f, -(long)sizeof buf, SEEK_END);
        n = fread(buf, 1, sizeof buf, f);
        h = mod_hash(h, buf, n);
    }
    fclose(f);
    uint8_t digest[8];
    for (int i = 0; i < 8; i++) digest[i] = (uint8_t)(h >> (i * 8));
    mod_file_hash = mod_hash(mod_file_hash, digest, sizeof digest);
    mod_file_count++;
}
uint64_t modfs_signature(void) { return nused ? (mod_signature ? mod_signature : 1) : 0; }

/* A PC version file with nothing like it in the PS1 game (checked against
 * the disc: those are other resources, the PC's own screens and sounds),
 * or one of its developers' leftovers: say what it is, and what a mod
 * gives instead. 1 if it was one. */
static int pc_only_note(const char *mod, const char *rel, const char *up) {
    static const char *const sources[] = {".RSD", ".PLY", ".MAT", ".GRP", ".COD", ".BAT", ".ORG", ".TXT", ".CFG", ".EXE", ".LA", ".HM"};
    const char *dot = strrchr(up, '.'), *slash = strrchr(up, '/');
    if (dot && (!slash || dot > slash))
        for (size_t i = 0; i < sizeof sources / sizeof sources[0]; i++)
            if (!strcmp(dot, sources[i])) {
                note("mods: %s/%s: ignored (a source file of the PC version's makers: no game reads it)", mod, rel);
                return 1;
            }
    static const struct { const char *prefix, *what; } kinds[] = {
        {"MESSAGE/", "the PC's picture of a message; RESPeCt shows the PS1's messages as text: change them in text/ (written by extract)"},
        {"EVENTMSG/", "the PC's picture of a message; RESPeCt shows the PS1's messages as text: change them in text/ (written by extract)"},
        {"WAV/", "a PC sound effect that isn't one of the PS1's recordings (modfs_pc_sfx.h has those): give the PS1's sound it stands for as NOISE/<bank>/<n>.wav (written by extract)"},
        {"ITEM/WAV/", "a PC sound effect that isn't one of the PS1's recordings (modfs_pc_sfx.h has those): give the PS1's sound it stands for as NOISE/<bank>/<n>.wav (written by extract)"},
        {"MENU/", "a picture of the PC's own menus, which the PS1 draws its own way"},
        {"ANM/", "an animation of the PC's own screens (the PS1 draws them its own way)"},
        {"ITEM/ITEM_", "the PC's own 3D model of an item (the PS1 shows its pictures, in BIGTIM)"},
        {"ITEM/PIC/", "a picture of the PC's own screens"},
        {"PEOPLE/", "a PC-only model (nothing like it on the PS1's disc)"},
        {"DOORNEW/", "a PC-only door model (the PS1's door files are DOOR_xx.HMD and DOOR_xx.BIN)"},
        {"EFFECT/", "a PC-only effect model (nothing like it on the PS1's disc)"},
        {"MAP/", "a picture of the PC's own map screen"},
        {"RESULT/", "a picture of the PC's own results screen"},
        {"AVI/", "a picture of the PC's own movie screens"},
        {"RESOURCE/DEMO_", "the PC's own recording of the attract demo"},
        {"RESOURCE/", "a PC-only file (the PS1's files are different: an older build's, or the PC's own)"},
        {"ITEM/", "a PC-only item file (nothing like it on the PS1's disc)"},
        {"STAGE/", "a file of a room's folder that the PS1's rooms don't have (the PC version's leftovers)"},
    };
    for (size_t i = 0; i < sizeof kinds / sizeof kinds[0]; i++)
        if (!strncmp(up, kinds[i].prefix, strlen(kinds[i].prefix))) {
            note("mods: %s/%s: %s: not used", mod, rel, kinds[i].what);
            return 1;
        }
    return 0;
}

static void apply(const char *mod, const char *rel, const char *path) {
    char up[512];
    size_t len = strlen(rel), component = 0;
    if (!len || len >= sizeof up || rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':')) {
        note("mods: %s/%s: unsafe or overlong resource path: not used", mod, rel);
        return;
    }
    for (size_t k = 0; k <= len; k++) {
        char c = rel[k] == '\\' ? '/' : (char)toupper((unsigned char)rel[k]);
        up[k] = c;
        if (!c || c == '/') {
            size_t n = k - component;
            if (!n || (n == 1 && up[component] == '.') ||
                (n == 2 && up[component] == '.' && up[component + 1] == '.')) {
                note("mods: %s/%s: unsafe resource path component: not used", mod, rel);
                return;
            }
            component = k + 1;
        }
    }
    /* Keep the supplied name in state identity and manifest-source exclusion.
     * Only dispatch strips the PC installation's verified PSX_DATA wrapper. */
    mod_hash_file(up, path);
    {   /* plugins/x.dll: native code, loaded after every file (modplug.c) */
        int modplug_add(const char *mod, const char *mod_dir, const char *up, const char *path), modplug_enabled(void);
        if (modplug_add(mod, NULL, up, path)) {
            if (modplug_enabled()) nused++;
            else note("mods: %s/%s: a plugin, and plugins are switched off (mod_plugins = 1 in resurv.ini): not used", mod, rel);
            return;
        }
    }
    if (pc_bitmaps_source(mod, up)) return;
    if (!strncmp(up, "PSX_DATA/", 9)) memmove(up, up + 9, strlen(up + 9) + 1);
    if (!strcmp(up, "STAGE/STAGE02/020E/RELOAD/TARAN20E.BIN")) strcpy(up, "TARAN20E.BIN");   /* (the PC keeps it there; the same file) */
    long n = 0;
    int fi = disc_index(up);
    if (fi >= 0) {                                /* a disc file, whole */
        if (disc_files[fi].hash || strstr(up, ".STR")) {
            note("mods: %s/%s: game code or a stream, can't be replaced", mod, rel);
            return;
        }
        uint8_t *d = load(path, &n);
        Touched *t = d ? touch(fi) : NULL;
        if (!t) { free(d); note("mods: %s/%s: couldn't read it", mod, rel); return; }
        if ((uint32_t)n > t->nsec * 2048) {
            note("mods: %s/%s: %ld bytes, %u more than its place on the disc: not used", mod, rel, n,
                   (unsigned)((uint32_t)n - t->nsec * 2048));
            free(d);
            return;
        }
        memset(t->data, 0, (size_t)t->nsec * 2048);
        memcpy(t->data, d, (size_t)n);
        free(d);
        note("mods: %s/%s: in place of the disc's (%ld bytes)", mod, rel, n);
        memory_mark_file(fi);
        if (!strcmp(up, "CONFFILE.BIN")) for (unsigned doc=0;doc<13;doc++) pc_diary_disable(doc);
        nused++;
        return;
    }
    if (pc_noise_apply(mod, rel, up, path)) return;
    for (size_t i = 0; i < sizeof modfs_pc / sizeof modfs_pc[0]; i++) {
        if (strcmp(modfs_pc[i].pc, up)) continue;
        int di = disc_index(modfs_pc[i].disc);
        uint8_t *d = di >= 0 ? load(path, &n) : NULL;
        Touched *t = d ? touch(di) : NULL;
        if (!t) { free(d); note("mods: %s/%s: couldn't read it", mod, rel); return; }
        uint8_t *at = t->data + modfs_pc[i].offset;
        static int checked[sizeof modfs_pc / sizeof modfs_pc[0]];
        if (!checked[i]) {                       /* the disc's original is there */
            uint8_t s[2352];
            uint8_t *orig = malloc(modfs_pc[i].size + 4096);
            uint32_t first = modfs_pc[i].offset / 2048, last = (modfs_pc[i].offset + modfs_pc[i].size - 1) / 2048;
            disc_only = 1;
            for (uint32_t k2 = first; k2 <= last; k2++) {
                cd_read_sector_raw(t->lba + k2, s);
                memcpy(orig + (size_t)(k2 - first) * 2048, s + 24, 2048);
            }
            disc_only = 0;
            uint32_t crc = (uint32_t)mz_crc32(0, orig + modfs_pc[i].offset % 2048, modfs_pc[i].size);
            free(orig);
            checked[i] = crc == modfs_pc[i].crc ? 1 : -1;
        }
        if (checked[i] < 0) {
            note("mods: %s/%s: the disc's %s isn't the one expected there: not used", mod, rel, modfs_pc[i].disc);
            free(d);
            return;
        }
        if ((uint32_t)n > modfs_pc[i].size) {
            if (can_grow(modfs_pc[i].disc) && ngrows < (int)(sizeof grows / sizeof grows[0])) {
                Grow *g = &grows[ngrows++];       /* its block is laid out again (grow_all) */
                g->fi = di;
                g->offset = modfs_pc[i].offset;
                g->size = (uint32_t)n;
                g->orig = modfs_pc[i].size;
                g->cap = 0;
                g->data = d;
                snprintf(g->what, sizeof g->what, "%s/%s", mod, rel);
                nused++;
                return;
            }
            note("mods: %s/%s: %ld bytes, %u more than the original's place in %s: not used", mod, rel, n,
                   (unsigned)((uint32_t)n - modfs_pc[i].size), modfs_pc[i].disc);
            free(d);
            return;
        }
        memset(at, 0, modfs_pc[i].size);
        memcpy(at, d, (size_t)n);
        free(d);
        note("mods: %s/%s: into %s at %u (%ld bytes)", mod, rel, modfs_pc[i].disc, modfs_pc[i].offset, n);
        memory_mark_file(di);
        nused++;
        return;
    }
    if (data_apply(mod, rel, up, path)) return;
    if (text_apply(mod, rel, up, path)) return;
    if (diary_apply(mod, rel, up, path)) return;
    if (own_picture_apply(mod, rel, up, path)) return;
    if (picture_apply(mod, rel, up, path)) return;
    if (bmp_apply(mod, rel, up, path)) return;
    if (enemy_apply(mod, rel, up, path)) return;
    if (pack_apply(mod, rel, up, path)) return;
    if (sfx_apply(mod, rel, up, path)) return;
    if (sound_apply(mod, rel, up, path)) return;
    if (movie_apply(mod, rel, up, path)) return;
    if (!strncmp(up, "BGM/", 4) && strlen(up) > 4 && !strcmp(up + strlen(up) - 4, ".WAV")) {
        note("mods: %s/%s: one of the PC's music or sound files that none of the PS1's sequences clearly is (modfs_pc_bgm.h);"
             " give it as NOISE/<block>/music1.wav for a room's music (the log says which plays in a room)"
             " or NOISE/<bank>/<n>.wav for a sound: not used", mod, rel);
        return;
    }
    if (pc_only_note(mod, rel, up)) return;
    note("mods: %s/%s: not a file RESPeCt knows (neither the disc's nor the PC version's): not used", mod, rel);
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static void wr16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }
static void wr32be(uint8_t *p, uint32_t v) { p[3] = (uint8_t)v; p[2] = (uint8_t)(v >> 8); p[1] = (uint8_t)(v >> 16); p[0] = (uint8_t)(v >> 24); }

/* Where a file's ISO 9660 directory record is (its sector and offset). */
static int iso_record(const char *path, uint32_t *sec, uint32_t *off) {
    uint8_t s[2352];
    disc_only = 1;
    int ok = cd_read_sector_raw(16, s) && !memcmp(s + 25, "CD001", 5);
    uint32_t dlba = 0, dlen = 0;
    if (ok) { dlba = rd32(s + 24 + 156 + 2); dlen = rd32(s + 24 + 156 + 10); }
    const char *name = path;
    while (ok) {
        const char *slash = strchr(name, '/');
        size_t part = slash ? (size_t)(slash - name) : strlen(name);
        int found = 0;
        for (uint32_t k = 0; k * 2048 < dlen && !found; k++) {
            if (!cd_read_sector_raw(dlba + k, s)) { ok = 0; break; }
            const uint8_t *d = s + 24;
            for (int o = 0; o < 2048 && d[o]; o += d[o]) {
                int nlen = d[o + 32], flen = nlen;
                for (int i = 0; i < nlen; i++) if (d[o + 33 + i] == ';') { flen = i; break; }
                if ((size_t)flen != part || strncasecmp((const char *)d + o + 33, name, part)) continue;
                if (!slash) { *sec = dlba + k; *off = (uint32_t)o; disc_only = 0; return 1; }
                dlba = rd32(d + o + 2);
                dlen = rd32(d + o + 10);
                found = 1;
                break;
            }
        }
        if (!found) break;
        name = slash + 1;
    }
    disc_only = 0;
    return 0;
}

#define TEST_BASE   0x8006C118u                  /* where TEST.BIN is loaded */
#define STAGE_NAMES 0x800A5B00u                  /* the stages' files: path, */
#define STAGE_LOCS  0x800A5B48u                  /* place on the disc (CdlLOC, in KERNEL.BIN's data), */
#define STAGE_SECS  0x800A5B90u                  /* block table (first sector of each, then the end) */
/* The room's block in memory (room_load.c: from 0x80108000, below
 * 0x80150000): as big as the biggest the game itself loads there (a room
 * in STGBG2, 142 sectors). */
#define OBJ_ROOM    (142u * 2048)

/* The rooms' memory. The game keeps the room's object file (its block of
 * B/STGBGn.BIN, read by room_load.c; in stages 1, 5 and 6 the scenery's
 * slots, read by 80090DC4 and its kin) from 0x80108000, and puts more after
 * it, past 0x800B3D54 (the next room's file while a door opens, the room's
 * sound set, a script's image or music), up to 0x80150000. A mod's room
 * bigger than the biggest the game has moves all that to extended RAM,
 * bigger by as much: the native readers use g_room_win_base/end, the game's
 * own code in TEST.BIN that builds those two addresses is changed (room_code).
 * What only borrows the old place for a moment (the enemies' files, the
 * sound banks, a door's model at its top, the title's and the typewriter's
 * pictures) stays there, now out of the room's way. Off unless a room
 * needs it: then the PS1's layout, byte for byte. */
uint32_t g_room_win_base = 0x80108000u, g_room_win_end = 0x80150000u;
static uint32_t ext_used;                        /* (with ext_alloc) */
static void ram_fix(uint32_t addr, const uint8_t *was, const uint8_t *now, uint32_t n);
#define ROOM_OLD     0x80108000u
#define ROOM_OLD_END 0x80150000u
#define ROOM_MAX     (1024u * 1024u)             /* the biggest room block a mod can bring */
static uint32_t room_need;                       /* the biggest room block past OBJ_ROOM (grow_all) */
static uint32_t room_bytes;                      /* the moved window's size (0: not moved) */
/* TEST.BIN's code (run from RAM) that builds the window's start (lui,ori)
 * or end (lui), as the disc has it. */
static const struct { uint32_t addr, word; } room_code[] = {
    {0x80090DDCu, 0x3C148010u}, {0x80090E10u, 0x36948000u},   /* the scenery's slots */
    {0x800911ACu, 0x3C028010u}, {0x800911B0u, 0x34428000u},   /* ...and 0x800B3D54 past them */
    {0x80091854u, 0x3C048010u}, {0x80091858u, 0x34848000u},   /* a scenery slot's file */
    {0x80091C38u, 0x3C048010u}, {0x80091C3Cu, 0x34848000u},
    {0x80091EDCu, 0x3C118010u}, {0x80091EE0u, 0x36318000u},
    {0x8008D8ACu, 0x3C038010u}, {0x8008D8B0u, 0x34638000u},   /* scripts: a block of the room's file */
    {0x8008D8F4u, 0x3C038010u}, {0x8008D8F8u, 0x34638000u},
    {0x80088DF0u, 0x3C028015u},                                /* the end, for what goes past 0x800B3D54 */
    {0x80092BE4u, 0x3C028015u},                                /* ...the room's sound set */
};

static int room_code_found(const Touched *test) {
    for (size_t i = 0; i < sizeof room_code / sizeof room_code[0]; i++) {
        uint32_t o = room_code[i].addr - TEST_BASE;
        if (!test || o + 4 > test->nsec * 2048 || rd32(test->data + o) != room_code[i].word) return 0;
    }
    return 1;
}

/* How big a room's block can be: the PS1's, or with the window moved, as
 * much as extended RAM leaves (after the mods' pictures, and some for the
 * enemies' models when those can grow too). */
static uint32_t room_cap(const Touched *test) {
    if (!room_code_found(test)) return OBJ_ROOM;
    uint64_t left = (uint64_t)(RAM_SIZE - RAM_GAME) - (((uint64_t)ext_used + 0xFFFFu) & ~UINT64_C(0xFFFF));
    uint64_t keep = 0x48000u + 0x10000u + (modmem_enabled() ? 0x200000u : 0);
    uint64_t cap = left > keep ? (left - keep) & ~UINT64_C(2047) : 0;
    if (cap > ROOM_MAX) cap = ROOM_MAX;
    return cap > OBJ_ROOM ? (uint32_t)cap : OBJ_ROOM;
}

/* The scenery of stages 1, 5 and 6: their blocks go in slots from the
 * rooms' window, one after the other (TEST 80090DC4 for the ones kept,
 * 80091554/80091938/80091E10 for the ones read as the player moves), as
 * many as the stage's area record says (byte 6, plus two) and as big as
 * it says (byte 7, sectors). The record is in the stage's program
 * (STAGPROG.BIN, loaded at 0x800F2C50), the pointer to it at 0x800A89B0.
 * A mod's block bigger than its slot makes the slots of that stage bigger
 * (byte 7) and moves the rooms' window, bigger by as much. */
static struct { const char *file; uint32_t first, last, sector, addr; uint8_t count, stride, want; } scenery[] = {
    {"B/STGBG1.BIN", 0, 11, 5, 0x800F3060u, 3, 20, 0},   /* (stage 1 has two areas) */
    {"B/STGBG1.BIN", 12, 16, 5, 0x800F3068u, 3, 20, 0},
    {"B/STGBG5.BIN", 2, 55, 19, 0x800F3668u, 5, 12, 0},
    {"B/STGBG6.BIN", 0, 13, 22, 0x800F328Cu, 3, 23, 0},
};
static uint32_t scenery_extra;                   /* what the bigger slots take more (room_window_place) */
/* The area record in STAGPROG.BIN, if it's the one expected. */
static uint8_t *scenery_record(int k) {
    Touched *sp = touch(disc_index("STAGPROG.BIN"));
    uint64_t o = (uint64_t)scenery[k].sector * 2048 + (scenery[k].addr - 0x800F2C50u);
    if (!sp || o + 8 > (uint64_t)sp->nsec * 2048) return NULL;
    uint8_t *r = sp->data + o;
    uint32_t ptr = rd32(r);
    if (ptr < 0x800F2C50u || ptr >= 0x800F8000u || r[6] != scenery[k].count || r[7] != scenery[k].stride) return NULL;
    return r;
}
static int scenery_of(int fi, uint32_t block) {
    for (int k = 0; k < (int)(sizeof scenery / sizeof scenery[0]); k++)
        if (!strcmp(disc_files[fi].name, scenery[k].file) && block >= scenery[k].first && block <= scenery[k].last) return k;
    return -1;
}
/* The original slot's size, 0 for a block not in one. */
static uint32_t scenery_resident_cap(int fi, uint32_t block) {
    int k = scenery_of(fi, block);
    return k < 0 ? 0 : scenery[k].stride * 2048u;
}
/* How big a scenery block can be: its slot, or with the slots made bigger,
 * as much as extended RAM leaves for all of them (up to 255 sectors). */
static uint32_t scenery_cap(int fi, uint32_t block, const Touched *test) {
    int k = scenery_of(fi, block);
    if (k < 0) return 0;
    uint32_t slot = scenery[k].stride * 2048u, n = scenery[k].count + 2u;
    uint32_t most = room_cap(test);              /* (what the window can grow by) */
    if (!scenery_record(k) || most <= OBJ_ROOM) return slot;
    uint64_t cap = ((uint64_t)most - OBJ_ROOM + (uint64_t)n * slot) / n;
    if (cap > 255u * 2048) cap = 255u * 2048;
    cap &= ~UINT64_C(2047);
    return cap > slot ? (uint32_t)cap : slot;
}
/* Every mod in: a room bigger than the PS1's moves the window (modfs_open). */
static void room_window_place(void) {
    const char *force = getenv("RT_ROOM_WINDOW");    /* (tests: moved with no room needing it) */
    if (!room_need && force && atoi(force)) room_need = OBJ_ROOM;
    const char *more_slots = getenv("RT_SCENERY_SLOTS");   /* (tests: every stage's slots this many sectors bigger) */
    for (int k = 0; k < (int)(sizeof scenery / sizeof scenery[0]); k++) {
        if (more_slots && atoi(more_slots) > 0 && scenery[k].want < scenery[k].stride + atoi(more_slots))
            scenery[k].want = (uint8_t)(scenery[k].stride + atoi(more_slots));
        if (scenery[k].want <= scenery[k].stride) continue;
        uint8_t *r = scenery_record(k), was[8], now[8];
        if (!r) rt_fatal("mods: %s's scenery slots can't be made bigger after all (STAGPROG.BIN changed)", scenery[k].file);
        memcpy(was, r, 8);
        r[7] = scenery[k].want;                  /* the stage's slots, bigger */
        memcpy(now, r, 8);
        ram_fix(scenery[k].addr, was, now, 8);
        uint32_t more = (scenery[k].count + 2u) * (uint32_t)(scenery[k].want - scenery[k].stride) * 2048u;
        if (more > scenery_extra) scenery_extra = more;
        note("mods: %s's scenery slots %u -> %u sectors (the stage's program, STAGPROG.BIN)", scenery[k].file, scenery[k].stride, scenery[k].want);
    }
    if (!room_need && !scenery_extra) return;
    Touched *test = touch(disc_index("TEST.BIN"));
    uint32_t base = (0x80200000u + ext_used + 0xFFFFu) & ~0xFFFFu;
    uint32_t bytes = ((room_need > scenery_extra ? room_need : scenery_extra) + 0x48000u + 0xFFFFu) & ~0xFFFFu;
    if (!room_code_found(test) || (uint64_t)base + bytes > 0x80000000u + RAM_SIZE)
        rt_fatal("mods: the rooms' memory can't be moved after all (TEST.BIN changed, or no extended RAM left)");
    for (size_t i = 0; i < sizeof room_code / sizeof room_code[0]; i++) {
        uint8_t *p = test->data + room_code[i].addr - TEST_BASE, was[4], now[4];
        uint32_t w = room_code[i].word, hi = (w & 0xFFFFu) == 0x8015u ? (base + bytes) >> 16 : base >> 16;
        memcpy(was, p, 4);
        wr32(p, w >> 26 == 15 ? (w & 0xFFFF0000u) | hi : w & 0xFFFF0000u);   /* lui: the new high half; ori: 0 */
        memcpy(now, p, 4);
        ram_fix(room_code[i].addr, was, now, 4);
    }
    g_room_win_base = base;
    g_room_win_end = base + bytes;
    room_bytes = bytes;
    g_ram_ext_used = 1;
    note("mods: the rooms' memory moved to %08X-%08X (the biggest room %u bytes, the PS1 has room for %u; scenery slots %u bytes more)",
         base, base + bytes, room_need, OBJ_ROOM, scenery_extra);
}

/* savestate.c: a quick save made with the rooms' memory where it isn't
 * now (its room still in the other place) can't go on. */
int modfs_room_window_state_ok(uint32_t obj_end) {
    const char *force = getenv("RT_ROOM_WINDOW");    /* (tests: 2 lets one in, its room stays where it was) */
    if (force && atoi(force) == 2) return 1;
    if (room_bytes) return obj_end < ROOM_OLD || obj_end > ROOM_OLD_END;
    return obj_end < 0x80200000u;
}

/* A sector's place as the CD's position (BCD minute, second, frame). */
static void bcd_msf(uint32_t lba, uint8_t msf[3]) {
    uint32_t a = lba + 150;
    msf[0] = (uint8_t)(a / 4500 / 10 * 16 + a / 4500 % 10);
    msf[1] = (uint8_t)(a / 75 % 60 / 10 * 16 + a / 75 % 60 % 10);
    msf[2] = (uint8_t)(a % 75 / 10 * 16 + a % 75 % 10);
}

/* What the mods change in the game's memory once it has read it (tables,
 * where files are): a savestate made before them has the old in memory,
 * so loading one puts the new back (modfs_state_loaded). At an address,
 * or (0) wherever in RAM the old is found (the tables' copies in the
 * overlays, loaded where the game puts them). */
typedef struct { uint32_t addr, n; uint8_t *was, *now; } RamFix;
static RamFix ramfixes[128];
static int nramfix;

static void ram_fix(uint32_t addr, const uint8_t *was, const uint8_t *now, uint32_t n) {
    if (nramfix == (int)(sizeof ramfixes / sizeof ramfixes[0]) || !memcmp(was, now, n)) return;
    RamFix *f = &ramfixes[nramfix++];
    f->addr = addr;
    f->n = n;
    f->was = malloc(n);
    f->now = malloc(n);
    memcpy(f->was, was, n);
    memcpy(f->now, now, n);
}

static uint8_t *ext_image;                       /* (texts_done) */
typedef struct { uint16_t x, y, w, h; uint16_t *was, *now; } VramFix;   /* (vram_fix_add) */
static VramFix *vfix;
static int nvfix;
static uint32_t ext_image_n;
typedef struct { uint16_t x, y, w, h; uint32_t hash; } ModHdRect;
static ModHdRect *mod_hd_rects;
static int nmod_hd_rects;

/* savestate.c: a savestate was loaded. */
void modfs_state_loaded(void) {
    int fixed = 0;
    for (int i = 0; i < nramfix; i++) {
        RamFix *f = &ramfixes[i];
        if (f->addr) {
            uint8_t *p = ram_ptr(f->addr);
            if (!memcmp(p, f->was, f->n)) { memcpy(p, f->now, f->n); fixed++; }
            continue;
        }
        for (uint32_t o = 0; o + f->n <= 0x200000; o += 4)
            if (g_ram[o] == f->was[0] && !memcmp(g_ram + o, f->was, f->n)) { memcpy(g_ram + o, f->now, f->n); fixed++; }
    }
    if (ext_image && !modio_static_copy(g_ram + RAM_GAME,ext_image,ext_image_n))
        rt_fatal("mods: static image/font staging span is outside the saved prefix");   /* what the mods put past the game's 2 MB */
    for (int i = 0; i < nvfix; i++) {            /* pictures in VRAM as they were before the mods */
        VramFix *v = &vfix[i];
        int same = 1;
        for (uint32_t r = 0; r < v->h && same; r++) same = !memcmp(&g_vram[v->y + r][v->x], v->was + (size_t)r * v->w, (size_t)v->w * 2);
        if (!same) continue;
        for (uint32_t r = 0; r < v->h; r++) memcpy(&g_vram[v->y + r][v->x], v->now + (size_t)r * v->w, (size_t)v->w * 2);
        fixed++;
    }
    /* A state can already contain the native TIM without its HD upload
     * history (including byte-identical replacements). Re-register only
     * rectangles whose complete current contents match a mod's TIM. */
    for (int i = 0; i < nmod_hd_rects; i++) {
        ModHdRect *r = &mod_hd_rects[i];
        uint32_t hash = 0;
        for (uint32_t y = 0; y < r->h; y++) hash = (uint32_t)mz_crc32(hash, (const uint8_t *)&g_vram[r->y + y][r->x], (size_t)r->w * 2);
        if (hash == r->hash) {
            void texpack_upload(int x, int y, int w, int h);
            texpack_upload(r->x, r->y, r->w, r->h);
        }
    }
    if (fixed) note("mods: the save was made without some of these mods: %d places in memory brought up to date", fixed);
}

/* The archives that have files growing: each block they're in laid out
 * again (a room's or an effect set's header's offsets, then its files,
 * packed as they were; or the one file a door's block is), the archive
 * moved past the end of the disc if it outgrew its place, and TEST.BIN's
 * block table changed to match. */
/* Only whole split-model blocks consumed by the native room loader gain a
 * larger packed budget. Never infer this from the archive name alone. */
static uint32_t sync_file_table(int fi) {
    if(fi<0)return 0;
    const char *name=disc_files[fi].name;
    return !strcmp(name,"EV_OBJ.DAT")?0x800A195Cu:!strcmp(name,"AEX.DAT")?0x800A2B3Cu:0;
}
static uint32_t sync_growth_cap(int fi,uint32_t block,const uint8_t *b,uint32_t bytes) {
    uint32_t original=modio_original_size(sync_file_table(fi),(int)block);
    uint64_t need=((uint64_t)bytes+2047)&~UINT64_C(2047);
    if(!original || need<=original || !modmem_enabled() || need>modio_capacity() || !modio_tim_valid(b,bytes))return 0;
    return modio_capacity();
}
static uint32_t split_model_cap(int fi,uint32_t block,const uint8_t *b,uint32_t n) {
    uint32_t table=0;
    if (!modmem_enabled() || fi<0) return 0;
    if (!strcmp(disc_files[fi].name,"OBJ.DAT")) table=0x800A0CD8u;
    else if (!strcmp(disc_files[fi].name,"EV_OBJ.DAT")) table=0x800A195Cu;
    if (!modmem_split_identity(table,block) || n<16 || rd32(b)!=3 || rd32(b+4)!=16) return 0;
    uint32_t off[4]={16,rd32(b+8),rd32(b+12),n};
    for (int k=0;k<3;k++)
        if (off[k]>n || off[k+1]<off[k] || off[k+1]-off[k]<4 ||
            !rd32(b+off[k]) || rd32(b+off[k])>RAM_SIZE-RAM_GAME) return 0;
    return RAM_SIZE-RAM_GAME;
}
static void grow_all(void) {
    if (!ngrows) return;
    Touched *test = touch(disc_index("TEST.BIN"));
    if (!test) return;
    uint32_t vbase = disc_image_sectors() > DISC_SECTORS ? disc_image_sectors() : DISC_SECTORS;
    for (int gi = 0; gi < ngrows; gi++) {
        int fi = grows[gi].fi;
        if (fi < 0) continue;                    /* done with another */
        Touched *t = touch(fi);
        if (!t) continue;
        const char *base = strrchr(disc_files[fi].name, '/');
        base = base ? base + 1 : disc_files[fi].name;
        /* its block table, from TEST.BIN's list of the stages' files */
        uint32_t tab = 0, nb = 0, loc = 0, kinds = 1, fixed = 0, tbase = TEST_BASE;
        int copies = 0;
        static uint32_t v[1024];
        Touched *tf = test;                      /* the file the table is in */
        for (size_t k = 0; k < sizeof archives / sizeof archives[0]; k++)
            if (!strcasecmp(disc_files[fi].name, archives[k].name)) {
                if (archives[k].tab < TEST_BASE && (tf = touch(disc_index("KERNEL.BIN")))) tbase = rd32(tf->data + 0x18) - 0x800;
                if (!tf) tf = test;
                tab = archives[k].tab - tbase;
                loc = archives[k].loc;
                kinds = archives[k].kinds;
                fixed = archives[k].cap;
                copies = archives[k].copies;
            }
        for (uint32_t k = 0; k < 18 && !tab; k++) {
            uint32_t np = rd32(test->data + STAGE_NAMES + 4 * k - TEST_BASE) - TEST_BASE;
            if (np >= test->nsec * 2048 - 32) continue;
            const char *nm = (const char *)test->data + np;
            size_t l = strnlen(nm, 32), bl = strlen(base);
            if (l >= bl && !strncasecmp(nm + l - bl, base, bl)) {
                tab = rd32(test->data + STAGE_SECS + 4 * k - TEST_BASE) - TEST_BASE;
                loc = rd32(test->data + STAGE_LOCS + 4 * k - TEST_BASE);
            }
        }
        if (tab && tab < tf->nsec * 2048 - 4096) {
            for (nb = 0; nb < 1023; nb++) {
                v[nb] = rd32(tf->data + tab + 4 * nb);
                if (nb && v[nb] < v[nb - 1]) { nb = 0; break; }
                if (nb && v[nb] == t->nsec) break;
            }
        }
        if (!tab || !nb || v[0] != 0 || v[nb] != t->nsec) {
            for (int g = gi; g < ngrows; g++)
                if (grows[g].fi == fi) {
                    note("mods: %s: bigger than the original, and %s's block table wasn't found: not used", grows[g].what, disc_files[fi].name);
                    grows[g].fi = -1;
                    nused--;
                }
            continue;
        }
        /* how big each kind of block can be */
        int rooms = !fixed && kinds == 1 && !strncasecmp(disc_files[fi].name, "B/STGBG", 7);
        uint32_t rmax = rooms ? room_cap(test) : OBJ_ROOM;   /* (more with the rooms' memory moved) */
        uint32_t cap[4] = {rmax, rmax, rmax, rmax};
        uint64_t extra = 0;
        if (fixed)
            cap[0] = cap[1] = cap[2] = cap[3] = fixed;
        else if (kinds != 1 || strncasecmp(disc_files[fi].name, "B/STGBG", 7)) {
            memset(cap, 0, sizeof cap);
            for (uint32_t b = 0; b < nb; b++)
                if ((v[b + 1] - v[b]) * 2048 > cap[b % kinds]) cap[b % kinds] = (v[b + 1] - v[b]) * 2048;
        }
        uint32_t maxcap = 0;
        for (int k = 0; k < 4; k++) if (cap[k] > maxcap) maxcap = cap[k];
        for (int g = gi; g < ngrows; g++) {      /* include a whole block's own capacity */
            if (grows[g].fi != fi) continue;
            uint64_t reserve=grows[g].cap > maxcap ? grows[g].cap : maxcap;
            for (uint32_t b=0;b<nb;b++) {
                uint32_t len=(v[b+1]-v[b])*2048;
                if (grows[g].offset!=v[b]*2048 || ((uint64_t)grows[g].orig+2047)/2048*2048!=len) continue;
                uint32_t cap=split_model_cap(fi,b,grows[g].data,grows[g].size);
                if(!cap)cap=sync_growth_cap(fi,b,grows[g].data,grows[g].size);
                uint64_t want=((uint64_t)grows[g].size+2047)&~UINT64_C(2047);
                if (cap && want<=cap && want>reserve) reserve=want;
                break;
            }
            if (rmax > OBJ_ROOM) {                /* what a file can add: itself, padded */
                uint64_t most = ((((uint64_t)grows[g].size + 3) & ~UINT64_C(3)) + 2047 + 2048) & ~UINT64_C(2047);
                if (reserve > most) reserve = most;
            }
            extra+=reserve;
        }
        uint64_t total = (uint64_t)t->nsec * 2048 + extra;
        uint8_t *out = total <= UINT32_MAX && total <= SIZE_MAX ? malloc((size_t)total) : NULL;
        if (!out) {
            note("mods: %s: couldn't allocate the rebuilt archive: bigger files not used", disc_files[fi].name);
            for (int g = gi; g < ngrows; g++)
                if (grows[g].fi == fi) { free(grows[g].data); grows[g].fi = -1; nused--; }
            continue;
        }
        static uint32_t nv[1024];
        uint32_t cur = 0;
        int accepted = 0;
        nv[0] = 0;
        for (uint32_t b = 0; b < nb; b++) {
            uint32_t start = v[b] * 2048, len = (v[b + 1] - v[b]) * 2048, room = cap[b % kinds];
            uint32_t resident = scenery_resident_cap(fi, b), slot_cap = resident ? scenery_cap(fi, b, test) : 0;
            if (resident && room > slot_cap) room = slot_cap;
            uint32_t sync_original=modio_original_size(sync_file_table(fi),(int)b);
            if(sync_original && room>sync_original)room=sync_original;
            const uint8_t *blk = t->data + start;
            int any = 0;
            for (int g = gi; g < ngrows; g++)
                if (grows[g].fi == fi && grows[g].offset - start < len) any = 1;
            uint32_t c = len >= 4 ? rd32(blk) : 0, off[260];
            int ok = any && c && c < 256 && 4 + 4 * c <= len;
            for (uint32_t i = 0; ok && i < c; i++) {
                off[i] = rd32(blk + 4 + 4 * i);
                if (off[i] < 4 + 4 * c || off[i] >= len || (i && off[i] < off[i - 1])) ok = 0;
            }
            uint32_t newlen = len;
            const Grow *whole = NULL;                /* a block that's one file */
            for (int g = gi; g < ngrows; g++)       /* (the last mod wins) */
                if (grows[g].fi == fi && grows[g].offset == start && ((uint64_t)grows[g].orig + 2047) / 2048 * 2048 == len) whole = &grows[g];
            if (whole) {
                if (whole->cap) room = whole->cap;
                else {
                    uint32_t dynamic_cap=split_model_cap(fi,b,whole->data,whole->size);
                    if (dynamic_cap) room=dynamic_cap;
                }
                /* An authored whole-block capacity cannot override a live slot. */
                if (resident && room > slot_cap) room = slot_cap;
                if(sync_original) {
                    uint32_t expanded=sync_growth_cap(fi,b,whole->data,whole->size);
                    room=expanded?expanded:sync_original;
                }
                uint64_t want = ((uint64_t)whole->size + 2047) & ~UINT64_C(2047);
                if (want < len) want = len;      /* as it was, if it fits */
                if (want > room) {
                    note("mods: %s block %u would be %llu bytes, more than the %u the game has for it: not used",
                           disc_files[fi].name, b, (unsigned long long)want, room);
                    newlen = len;
                    whole = NULL;
                    ok = 0;
                } else {
                    newlen = (uint32_t)want;
                    memcpy(out + cur, whole->data, whole->size);
                    memset(out + cur + whole->size, 0, newlen - whole->size);
                    ok = 1;
                }
            } else if (ok) {                     /* measure every part before writing any */
                const uint8_t *src[256];
                uint32_t size[256];
                uint64_t want = off[0];
                for (uint32_t i = 0; i < c; i++) {
                    size[i] = (i + 1 < c ? off[i + 1] : len) - off[i];
                    src[i] = blk + off[i];
                    for (int g = gi; g < ngrows; g++)  /* the last mod wins */
                        if (grows[g].fi == fi && grows[g].offset == start + off[i]) { src[i] = grows[g].data; size[i] = grows[g].size; }
                    want += ((uint64_t)size[i] + 3) & ~UINT64_C(3);
                }
                want = (want + 2047) & ~UINT64_C(2047);
                if (want > room) {
                    note("mods: %s block %u would be %llu bytes, more than the %u the game has for it: not used",
                           disc_files[fi].name, b, (unsigned long long)want, room);
                    ok = 0;
                    newlen = len;
                } else {
                    newlen = (uint32_t)want;
                    uint8_t *o = out + cur;
                    memset(o, 0, newlen);        /* deterministic part and sector padding */
                    memcpy(o, blk, off[0]);
                    uint32_t at = off[0];
                    for (uint32_t i = 0; i < c; i++) {
                        wr32(o + 4 + 4 * i, at);
                        memcpy(o + at, src[i], size[i]);
                        at += (size[i] + 3) & ~3u;
                    }
                }
            } else if (any)
                note("mods: %s block %u: its layout isn't one this knows: not used", disc_files[fi].name, b);
            if (!ok) memcpy(out + cur, blk, len);
            if (ok && rooms && !resident && newlen > OBJ_ROOM && newlen > room_need) room_need = newlen;
            if (ok && resident && newlen > resident) {           /* the stage's slots must grow */
                int k = scenery_of(fi, b);
                if (k >= 0 && newlen / 2048 > scenery[k].want) scenery[k].want = (uint8_t)(newlen / 2048);
            }
            for (int g = gi; g < ngrows; g++)
                if (grows[g].fi == fi && grows[g].offset - start < len) {
                    if (ok) {
                        accepted++;
                        note("mods: %s: prepared for %s block %u (%u bytes; the block %u -> %u bytes)", grows[g].what, disc_files[fi].name, b, grows[g].size, len, newlen);
                    } else nused--;
                }
            cur += newlen;
            nv[b + 1] = cur / 2048;
        }
        for (int g = gi; g < ngrows; g++)
            if (grows[g].fi == fi) { free(grows[g].data); grows[g].fi = -1; }
        uint32_t nsec = cur / 2048;
        if (nsec == t->nsec && !memcmp(out, t->data, cur)) { free(out); continue; }
        /* past the end of the disc: its place in KERNEL.BIN's table and its directory entry told (checked first) */
        uint32_t sec = 0, doff = 0, ko = 0;
        Touched *dir = NULL, *kern = NULL;
        if (nsec > t->nsec) {
            dir = iso_record(disc_files[fi].name, &sec, &doff) ? touch_lba(sec, 1, "directory") : NULL;
            kern = touch(disc_index("KERNEL.BIN"));
            ko = kern ? loc - rd32(kern->data + 0x18) + 0x800 : 0;
            uint8_t want[3];
            bcd_msf(t->lba, want);
            if (!dir || !kern || ko + 4 > kern->nsec * 2048 || memcmp(kern->data + ko, want, 3)) {
                note("mods: %s: where the game finds it wasn't found: the bigger files in it not used", disc_files[fi].name);
                free(out);
                nused -= accepted;
                continue;
            }
        }
        uint8_t was[4096], now[4096];               /* the table, for saves made before (modfs_state_loaded) */
        uint32_t tn = 4 * (nb + 1) <= sizeof was ? 4 * (nb + 1) : 0;
        if (tn) memcpy(was, tf->data + tab, tn);
        if (copies) {                            /* the table's copies in the game's modules */
            int found = 0;
            for (size_t m = 0; m < sizeof disc_files / sizeof disc_files[0]; m++) {
                if (!disc_files[m].hash || (int)m == disc_index("TEST.BIN")) continue;
                Touched *mt = touch((int)m);
                for (uint32_t o = 0; mt && o + 4 * (nb + 1) <= mt->nsec * 2048; o += 4) {
                    if (memcmp(mt->data + o, test->data + tab, 4 * (nb + 1))) continue;
                    if (o % 2048 < 32) continue;        /* where an overlay is told apart by its first bytes */
                    for (uint32_t b = 0; b <= nb; b++) wr32(mt->data + o + 4 * b, nv[b]);
                    found++;
                }
            }
            for (uint32_t o = 0; o + 4 * (nb + 1) <= test->nsec * 2048; o += 4)
                if (o != tab && !memcmp(test->data + o, test->data + tab, 4 * (nb + 1))) {
                    for (uint32_t b = 0; b <= nb; b++) wr32(test->data + o + 4 * b, nv[b]);
                    found++;
                }
            if (memcmp(v, nv, 4 * (nb + 1)))
                note("mods: %s: its block table changed in TEST.BIN and %d copies", disc_files[fi].name, found);
        }
        for (uint32_t b = 0; b <= nb; b++) wr32(tf->data + tab + 4 * b, nv[b]);   /* the block table */
        if (tn) {
            memcpy(now, tf->data + tab, tn);
            ram_fix(tbase + tab, was, now, tn);
            if (copies) ram_fix(0, was, now, tn);
        }
        free(t->data);
        t->data = out;
        if (accepted) memory_mark_file(fi);
        if (nsec > t->nsec) {
            uint8_t old[3], msf[3];
            memcpy(old, kern->data + ko, 3);
            t->lba = vbase + vnext;
            vnext += nsec + 16;
            bcd_msf(t->lba, msf);
            memcpy(kern->data + ko, msf, 3);
            ram_fix(loc, old, msf, 3);
            wr32(dir->data + doff + 2, t->lba);
            wr32be(dir->data + doff + 6, t->lba);
            wr32(dir->data + doff + 10, nsec * 2048);
            wr32be(dir->data + doff + 14, nsec * 2048);
        }
        t->nsec = nsec;
    }
}

/* ------------------------------------------------------------ sound */

/* A stream's channel played from a mod's WAV: each of the channel's
 * sectors on the disc numbered in order (index), the WAV in stereo at the
 * channel's rate. */
typedef struct {
    uint32_t lba, nsec, chan, rate, per;          /* per: frames a sector holds */
    int32_t *index;                               /* the sector's place in the channel, -1 another's */
    int16_t *pcm;                                 /* stereo */
    uint32_t frames;
} Track;
static Track tracks[128];
static int ntracks;


/* PCM WAV, 16 bits, mono or stereo, to stereo at rate (linear). */
static int16_t *wav_load(const uint8_t *d, long n, uint32_t rate, uint32_t *frames, const char **why) {
    if (n < 44 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { *why = "not a WAV file"; return NULL; }
    const uint8_t *fmt = NULL, *data = NULL;
    uint32_t dlen = 0;
    for (size_t o = 12; o + 8 <= (size_t)n;) {
        uint32_t len = rd32(d + o + 4);
        if (len > (size_t)n - o - 8) { *why = "truncated WAV chunk"; return NULL; }
        if (!memcmp(d + o, "fmt ", 4)) {
            if (len < 16) { *why = "truncated WAV format"; return NULL; }
            fmt = d + o + 8;
        }
        if (!memcmp(d + o, "data", 4)) { data = d + o + 8; dlen = len; }
        o += 8 + (size_t)len + (len & 1);
    }
    if (!fmt || !data) { *why = "no format or no samples"; return NULL; }
    uint32_t chans = rd16(fmt + 2), srate = rd32(fmt + 4), bits = rd16(fmt + 14);
    if (rd16(fmt) != 1 || bits != 16 || chans < 1 || chans > 2 || !srate) { *why = "not 16-bit PCM, mono or stereo"; return NULL; }
    if (!dlen || dlen % (2 * chans) || !rate) { *why = "no complete PCM frames or invalid target rate"; return NULL; }
    uint32_t in = dlen / (2 * chans);
    uint64_t count = (uint64_t)in * rate / srate;
    if (!count || count > UINT32_MAX || count > SIZE_MAX / 4) { *why = "resampled WAV size out of range"; return NULL; }
    uint32_t out = (uint32_t)count;
    int16_t *pcm = malloc((size_t)out * 4);
    if (!pcm) { *why = "out of memory"; return NULL; }
    for (uint32_t i = 0; i < out; i++) {
        uint64_t pos = (uint64_t)i * srate;
        uint32_t a = (uint32_t)(pos / rate), f = (uint32_t)((pos % rate) * 65536 / rate), b = a + 1 < in ? a + 1 : a;
        for (int c = 0; c < 2; c++) {
            uint32_t cc = chans == 2 ? (uint32_t)c : 0;
            int32_t va = (int16_t)rd16(data + (a * chans + cc) * 2), vb = (int16_t)rd16(data + (b * chans + cc) * 2);
            pcm[2 * i + c] = (int16_t)(va + (((vb - va) * (int32_t)f) >> 16));
        }
    }
    *frames = out;
    return pcm;
}

/* X/BGM_32.STR/05.wav: channel 5 of that stream; wav/Scene05_1.wav (the
 * PC's name): the channel it is. */
static int sound_apply(const char *mod, const char *rel, const char *up, const char *path) {
    const char *sl = strrchr(up, '/');
    if (!sl || strcasecmp(up + strlen(up) - 4, ".WAV")) return 0;
    char stream[512];
    long chan = -1;
    for (size_t i = 0; i < sizeof modfs_wav / sizeof modfs_wav[0]; i++)
        if (!strcmp(modfs_wav[i].pc, up)) {
            snprintf(stream, sizeof stream, "%s", modfs_wav[i].stream);
            chan = modfs_wav[i].chan;
        }
    if (chan < 0) snprintf(stream, sizeof stream, "%.*s", (int)(sl - up), up);
    int fi = disc_index(stream);
    if (fi < 0 || !strstr(stream, ".STR")) return 0;
    char *end;
    if (chan < 0 && ((chan = strtol(sl + 1, &end, 10)), end == sl + 1 || strcasecmp(end, ".WAV") || chan < 0 || chan > 31)) {
        note("mods: %s/%s: name it by the channel's number (00.wav to 31.wav): not used", mod, rel);
        return 1;
    }
    /* the channel's sectors, in order, and how they're coded (each stream read once: which channel
     * each of its sectors is, 0xFF not sound, and its coding byte) */
    static struct { int fi; uint8_t *chan, *coding; } scans[16];
    static int nscans;
    uint32_t lba = disc_files[fi].lba, nsec = (disc_files[fi].size + 2047) / 2048, count = 0, rate = 0, per = 0;
    int si = 0;
    while (si < nscans && scans[si].fi != fi) si++;
    if (si == nscans) {
        if (nscans == 16) { free(scans[0].chan); free(scans[0].coding); memmove(scans, scans + 1, sizeof scans[0] * 15); nscans--; si = nscans; }
        scans[si].fi = fi;
        scans[si].chan = malloc(nsec);
        scans[si].coding = malloc(nsec);
        uint8_t sec[2352];
        disc_only = 1;
        for (uint32_t k = 0; scans[si].chan && scans[si].coding && k < nsec; k++) {
            scans[si].chan[k] = 0xFF;
            if (!cd_read_sector_raw(lba + k, sec) || sec[15] != 2 || !(sec[18] & 0x04) || sec[17] >= 32) continue;
            scans[si].chan[k] = sec[17];
            scans[si].coding[k] = sec[19];
        }
        disc_only = 0;
        nscans++;
    }
    int32_t *index = malloc(nsec * sizeof *index);
    for (uint32_t k = 0; index && scans[si].chan && k < nsec; k++) {
        index[k] = -1;
        if (scans[si].chan[k] != chan) continue;
        index[k] = (int32_t)count++;
        if (!rate) {
            uint8_t c = scans[si].coding[k];
            rate = (c >> 2) & 1 ? 18900 : 37800;
            per = (((c >> 4) & 3) == 1 ? 4 : 8) * 28 * 18 / ((c & 3) == 1 ? 2 : 1);
        }
    }
    if (!count) {
        note("mods: %s/%s: %s has no sound on channel %ld: not used", mod, rel, stream, chan);
        free(index);
        return 1;
    }
    long n = 0;
    uint8_t *d = load(path, &n);
    const char *why = "couldn't read it";
    uint32_t frames = 0;
    int16_t *pcm = d ? wav_load(d, n, rate, &frames, &why) : NULL;
    free(d);
    if (!pcm || ntracks == (int)(sizeof tracks / sizeof tracks[0])) {
        note("mods: %s/%s: %s: not used", mod, rel, pcm ? "too many sounds" : why);
        free(pcm);
        free(index);
        return 1;
    }
    for (int i = 0; i < ntracks; i++)             /* a later mod wins */
        if (tracks[i].lba == lba && tracks[i].chan == (uint32_t)chan) {
            free(tracks[i].pcm);
            free(tracks[i].index);
            tracks[i] = tracks[--ntracks];
            break;
        }
    Track *t = &tracks[ntracks++];
    *t = (Track){lba, nsec, (uint32_t)chan, rate, per, index, pcm, frames};
    double secs = (double)count * per / rate;
    note("mods: %s/%s: in place of %s channel %ld (%.1f s; the WAV %.1f s%s)", mod, rel, stream, chan, secs,
           (double)frames / rate, frames < count * per ? ", silence after it" : frames > count * per ? ", cut to fit" : "");
    nused++;
    return 1;
}

/* cdrom.c: an XA sound sector the game is playing: a mod's in its place (1). */
int modfs_xa(uint32_t lba, const uint8_t *s) {
    for (int i = 0; i < ntracks; i++) {
        Track *t = &tracks[i];
        if (lba - t->lba >= t->nsec || s[17] != t->chan) continue;
        int32_t k = t->index[lba - t->lba];
        if (k < 0) return 0;
        static int16_t buf[4032 * 2];
        uint64_t at = (uint64_t)k * t->per;
        for (uint32_t f = 0; f < t->per && f < 4032; f++) {
            uint64_t j = at + f;
            buf[2 * f] = j < t->frames ? t->pcm[2 * j] : 0;
            buf[2 * f + 1] = j < t->frames ? t->pcm[2 * j + 1] : 0;
        }
        void spu_xa_pcm(const int16_t *lr, int frames, int rate);
        spu_xa_pcm(buf, (int)t->per, (int)t->rate);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ sound effects */

/* The sound effects (and the instruments the music plays) are the sound
 * banks in NOISE.BIN. Its blocks (KERNEL.BIN's table at 0x80058D2C: the
 * first sector of each, then the end) are each [n, n offsets]: a bank's
 * attributes (VAB header) at the first, its waveforms (SPU ADPCM, one
 * after another, their sizes in the header) at the last, a room's music
 * (sequences) in between. NOISE/012/03.wav is waveform 3 of block 12,
 * and of every other bank that has the very same one (many share their
 * sounds), at the rate its tones play it at note 60 (centre note C, fine
 * tune s: 44100 * 2^((60 - C + s/128) / 12)). A bank goes whole into the
 * sound chip's memory (512 KB, shared with the other banks open), so it
 * can't grow: a sound can be longer than the original by what the mods'
 * other sounds for that bank leave free. It ends, or repeats, as the
 * original did (from the WAV's own loop start if it has one, a smpl
 * chunk; else from the original's). */
#define NOISE_TABLE 0x80058D2Cu

typedef struct { uint32_t block, vag; uint8_t *wav; long n; int places, cut, bad, rank; double secs; uint32_t rate; char what[600];
                 int16_t *whole; uint32_t whole_frames, whole_rate; } Sfx;   /* whole: the WAV, mono at its own rate (spu.c plays it) */   /* cut: in how many banks */
static Sfx *sfxs;
static int nsfx;

/* A bank in NOISE.BIN: its block (at, n bytes), header and body (offsets in the block), programs, waveforms, their size table. */
typedef struct { uint32_t at, n, vh, body, ps, vs, tab; } Bank;

static int bank_at(const uint8_t *d, uint32_t at, uint32_t n, Bank *b) {
    if (n < 16) return 0;
    uint32_t c = rd32(d + at);
    if (c < 2 || c > 8 || 4 + 4 * c > n) return 0;
    uint32_t vh = rd32(d + at + 4), body = rd32(d + at + 4 * c);
    if (vh != 4 + 4 * c || vh + 32 > n || memcmp(d + at + vh, "pBAV", 4) || body > n) return 0;
    uint32_t ps = rd16(d + at + vh + 18), vs = rd16(d + at + vh + 22), tab = vh + 32 + 2048 + 512 * ps;
    if (ps > 128 || vs > 255 || tab + 512 > body) return 0;
    uint64_t sum = 0;
    for (uint32_t i = 1; i <= vs; i++) sum += rd16(d + at + tab + 2 * i) * 8u;
    if (body + sum > n) return 0;
    *b = (Bank){at, n, vh, body, ps, vs, tab};
    return 1;
}

static uint32_t vag_size(const uint8_t *d, const Bank *b, uint32_t v) { return rd16(d + b->at + b->tab + 2 * v) * 8u; }
static uint32_t vag_off(const uint8_t *d, const Bank *b, uint32_t v) {   /* in NOISE.BIN */
    uint32_t o = b->at + b->body;
    for (uint32_t i = 1; i < v; i++) o += vag_size(d, b, i);
    return o;
}

/* The rate waveform v plays at, by the first tone that uses it. */
static uint32_t vag_rate(const uint8_t *d, const Bank *b, uint32_t v) {
    const uint8_t *tone = d + b->at + b->vh + 32 + 2048;
    for (uint32_t k = 0; k < 16 * b->ps; k++, tone += 32)
        if (rd16(tone + 22) == v) return (uint32_t)(44100.0 * pow(2.0, (60.0 - tone[4] + tone[5] / 128.0) / 12.0) + 0.5);
    return 22050;
}

/* A waveform's blocks (16 bytes, 28 samples each): a silent one before
 * the sound (lead), the sound (its last flagged "end", 1; "repeat", 2, if
 * it loops, its loop start flagged 4, from), and what's after (tail: the
 * silent block a sound that doesn't loop ends in). */
typedef struct { uint32_t lead, data, tail, loop, from; } Shape;
static Shape vag_shape(const uint8_t *v, uint32_t nb) {
    static const uint8_t zero[16];
    Shape s = {0, 0, 0, 0, 0};
    s.lead = nb > 1 && !memcmp(v, zero, 16);
    uint32_t j = s.lead;
    while (j + 1 < nb && !(v[16 * j + 1] & 1)) j++;
    s.data = j + 1 - s.lead;
    s.tail = nb - 1 - j;
    s.loop = !s.tail && (v[16 * j + 1] & 2);
    for (uint32_t k = s.lead; k <= j; k++)
        if (v[16 * k + 1] & 4) { s.from = k - s.lead; break; }
    return s;
}

static int16_t *vag_decode(const uint8_t *v, uint32_t nb) {   /* every block's 28 samples */
    static const int pos[5] = {0, 60, 115, 98, 122}, neg[5] = {0, 0, -52, -55, -60};
    int16_t *o = malloc((size_t)nb * 28 * 2 + 2);
    int32_t h0 = 0, h1 = 0;
    for (uint32_t k = 0; o && k < nb; k++) {
        const uint8_t *b = v + 16 * k;
        int shift = b[0] & 15, f = (b[0] >> 4) & 7;
        if (f > 4) f = 0;
        if (shift > 12) shift = 9;
        for (int i = 0; i < 28; i++) {
            int n = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
            int32_t s = (((n & 8 ? n - 16 : n) * 4096) >> shift) + ((h0 * pos[f] + h1 * neg[f] + 32) >> 6);
            s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
            h1 = h0;
            h0 = s;
            o[28 * k + i] = (int16_t)s;
        }
    }
    return o;
}

/* 28 samples as one block (flags left 0): of the five filters and the
 * steps that fit, the one closest to them as the SPU decodes it; h: the
 * last two samples decoded, moved on. */
static void adpcm_block(const int16_t *x, int32_t h[2], uint8_t out[16]) {
    static const int pos[5] = {0, 60, 115, 98, 122}, neg[5] = {0, 0, -52, -55, -60};
    int64_t best = -1;
    int32_t b0 = h[0], b1 = h[1];
    for (int f = 0; f < 5; f++) {
        int32_t p0 = h[0], p1 = h[1], peak = 0;
        for (int i = 0; i < 28; i++) {
            int32_t r = x[i] - ((p0 * pos[f] + p1 * neg[f] + 32) >> 6);
            if (r < 0) r = -r;
            if (r > peak) peak = r;
            p1 = p0;
            p0 = x[i];
        }
        int s0 = 0;
        while (s0 < 12 && peak > (7 << s0)) s0++;
        for (int s = s0; s <= s0 + 1 && s <= 12; s++) {
            uint8_t t[16] = {(uint8_t)(f << 4 | (12 - s)), 0};
            int32_t g0 = h[0], g1 = h[1];
            int64_t err = 0;
            for (int i = 0; i < 28; i++) {
                int32_t pr = (g0 * pos[f] + g1 * neg[f] + 32) >> 6, q = (x[i] - pr + ((1 << s) >> 1)) >> s;
                q = q < -8 ? -8 : q > 7 ? 7 : q;
                int32_t v = q * (1 << s) + pr;
                v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
                err += (int64_t)(x[i] - v) * (x[i] - v);
                g1 = g0;
                g0 = v;
                t[2 + i / 2] |= (uint8_t)((q & 15) << ((i & 1) * 4));
            }
            if (best < 0 || err < best) { best = err; memcpy(out, t, 16); b0 = g0; b1 = g1; }
        }
    }
    h[0] = b0;
    h[1] = b1;
}

/* A WAV's rate and its first loop's start (a smpl chunk), -1 none. */
static int64_t wav_loop(const uint8_t *d, long n, uint32_t *rate) {
    int64_t from = -1;
    *rate = 0;
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return -1;
    for (size_t o = 12; o + 8 <= (size_t)n;) {
        uint32_t len = rd32(d + o + 4);
        if (len > (size_t)n - o - 8 || (!memcmp(d + o, "fmt ", 4) && len < 16)) { *rate = 0; return -1; }
        if (!memcmp(d + o, "fmt ", 4)) *rate = rd32(d + o + 12);
        if (!memcmp(d + o, "smpl", 4) && len >= 60 && rd32(d + o + 8 + 28)) from = rd32(d + o + 8 + 44);
        o += 8 + (size_t)len + (len & 1);
    }
    return from;
}

static uint32_t noise_banks(Touched *t, uint32_t *v, Bank **bk);
static int music_apply(const char *mod, const char *rel, unsigned blk, unsigned part, const char *path, int alt);

/* NOISE/012/music1.seq: a room's sound set's first sequence (its music),
 * as the disc has it (pQES), in its place; bigger, its block laid out
 * again (grow_all). */
static int seq_apply(const char *mod, const char *rel, unsigned blk, unsigned part, const char *path) {
    int fi = disc_index("NOISE.BIN");
    Touched *t = touch(fi);
    static uint32_t v[1024];
    Bank *bk = NULL;
    uint32_t nb = noise_banks(t, v, &bk), c = 0, at = 0, len = 0;
    if (blk < nb && bk[blk].vs) {
        at = v[blk] * 2048;
        c = rd32(t->data + at);
    }
    free(bk);
    if (part < 1 || part + 2 > c) {
        note("mods: %s/%s: NOISE.BIN's block %u has no music %u: not used", mod, rel, blk, part);
        return 1;
    }
    uint32_t off = rd32(t->data + at + 4 + 4 * part);
    len = rd32(t->data + at + 8 + 4 * part) - off;
    long n = 0;
    uint8_t *d = load(path, &n);
    if (!d || n < 16 || memcmp(d, "pQES", 4)) {
        note("mods: %s/%s: %s: not used", mod, rel, d ? "not a sequence (pQES)" : "couldn't read it");
        free(d);
        return 1;
    }
    if ((uint32_t)n <= len) {
        memset(t->data + at + off, 0, len);
        memcpy(t->data + at + off, d, (size_t)n);
        free(d);
        note("mods: %s/%s: into NOISE.BIN block %u (%ld bytes)", mod, rel, blk, n);
        nused++;
        return 1;
    }
    if (ngrows == (int)(sizeof grows / sizeof grows[0])) {
        note("mods: %s/%s: too many files growing: not used", mod, rel);
        free(d);
        return 1;
    }
    Grow *g = &grows[ngrows++];
    *g = (Grow){fi, at + off, (uint32_t)n, len, 0, d, ""};
    snprintf(g->what, sizeof g->what, "%s/%s", mod, rel);
    nused++;
    return 1;
}

/* NOISE/012/03.wav: waveform 3 of NOISE.BIN's block 12 (done once every mod is in). */
static int sfx_apply(const char *mod, const char *rel, const char *up, const char *path) {
    char alias[64];
    int sets = 0, was = nused;
    for (size_t i = 0; i < sizeof modfs_pc_bgm / sizeof modfs_pc_bgm[0]; i++)   /* the PC's music by its name (modfs_pc_bgm.h), */
        if (!strcmp(up, modfs_pc_bgm[i].pc)) {                                  /* in each set that has that sequence */
            music_apply(mod, rel, (unsigned)modfs_pc_bgm[i].block, (unsigned)modfs_pc_bgm[i].music, path, modfs_pc_bgm[i].alt);
            sets++;
        }
    if (sets) {                                  /* one file: it counts once, and one it took the place of (all its */
        nused = was + (nused - was == sets);     /* sets: the same name, or one of the same sequence) no more */
        return 1;
    }
    int rank = 0;
    for (size_t i = 0; i < sizeof modfs_pc_sfx / sizeof modfs_pc_sfx[0]; i++)   /* the PS1's own recordings */
        if (!strcmp(up, modfs_pc_sfx[i].pc)) {
            snprintf(alias, sizeof alias, "NOISE/%u/%u.WAV", modfs_pc_sfx[i].block, modfs_pc_sfx[i].vag);
            up = alias;
            break;
        }
    for (size_t i = 0; up != alias && i < sizeof modfs_pc_sfx_events / sizeof modfs_pc_sfx_events[0]; i++)   /* by their event */
        if (!strcmp(up, modfs_pc_sfx_events[i].pc)) {
            snprintf(alias, sizeof alias, "NOISE/%u/%u.WAV", modfs_pc_sfx_events[i].block, modfs_pc_sfx_events[i].vag);
            up = alias;
            rank = (int)modfs_pc_sfx_events[i].rank;
            break;
        }
    if (strncmp(up, "NOISE/", 6)) return 0;
    unsigned blk, vag;
    char rest[8] = "";
    long n = 0;
    if (sscanf(up + 6, "%u/MUSIC%u%7s", &blk, &vag, rest) == 3 && !strcmp(rest, ".SEQ")) return seq_apply(mod, rel, blk, vag, path);
    if (sscanf(up + 6, "%u/MUSIC%u%7s", &blk, &vag, rest) == 3 && !strcmp(rest, ".WAV")) return music_apply(mod, rel, blk, vag, path, 0);
    if (sscanf(up + 6, "%u/%u%7s", &blk, &vag, rest) != 3 || strcmp(rest, ".WAV")) {
        note("mods: %s/%s: name it by the bank's block and the sound's number (NOISE/012/03.wav), or a room's music (NOISE/240/music1.seq or .wav): not used", mod, rel);
        return 1;
    }
    uint8_t *d = load(path, &n);
    Sfx *s = d ? realloc(sfxs, (size_t)(nsfx + 1) * sizeof *sfxs) : NULL;
    if (!s) { free(d); note("mods: %s/%s: couldn't read it", mod, rel); return 1; }
    sfxs = s;
    s = &sfxs[nsfx++];
    memset(s, 0, sizeof *s);
    s->block = blk;
    s->vag = vag;
    s->rank = rank;
    s->wav = d;
    s->n = n;
    snprintf(s->what, sizeof s->what, "%s/%s", mod, rel);
    nused++;
    return 1;
}

/* A waveform anew from sound s (mono, at its rate), in at most room
 * bytes, shaped as the original (onb blocks); the bytes written. */
static uint32_t sfx_encode(Sfx *s, const int16_t *pcm, uint32_t frames, uint32_t rate, const uint8_t *orig, uint32_t onb, uint32_t room, uint8_t *out) {
    Shape sh = vag_shape(orig, onb);
    uint32_t want = frames ? (frames + 27) / 28 : 1, cap = room / 16 > sh.lead + sh.tail ? room / 16 - sh.lead - sh.tail : 1;
    if (want > cap) { want = cap; s->cut++; }
    uint32_t rs;
    int64_t from = wav_loop(s->wav, s->n, &rs);
    uint32_t lf = from >= 0 && rs ? (uint32_t)((uint64_t)from * rate / rs / 28) : sh.from;
    if (lf >= want) lf = 0;
    uint8_t *o = out;
    if (sh.lead) { memset(o, 0, 16); o += 16; }
    int32_t h[2] = {0, 0};
    int16_t x[28];
    for (uint32_t k = 0; k < want; k++, o += 16) {
        for (uint32_t i = 0; i < 28; i++) x[i] = 28 * k + i < frames ? pcm[28 * k + i] : 0;
        adpcm_block(x, h, o);
        o[1] = sh.loop ? 2 : 0;
        if (k == (sh.loop ? lf : 0)) o[1] |= 4;
        if (k == want - 1) o[1] |= 1;
    }
    memcpy(o, orig + 16 * (onb - sh.tail), 16 * sh.tail);
    o += 16 * sh.tail;
    double secs = (double)(want * 28 < frames ? want * 28 : frames) / rate;
    if (secs > s->secs) s->secs = secs;
    if (!s->rate) s->rate = rate;
    return (uint32_t)(o - out);
}

/* The SPU plays sound s whole, at the WAV's own rate and length, where the
 * game starts the waveform just written (out, n bytes; its original's
 * shape orig, onb blocks, at rate): spu.c finds it by the CRC of its blocks
 * up to the end flag. */
static void sfx_whole(Sfx *s, const uint8_t *out, uint32_t n, const uint8_t *orig, uint32_t onb, uint32_t rate) {
    void spu_hd_add(uint32_t crc, uint32_t len, const int16_t *pcm, uint32_t frames, uint32_t wav_rate, uint32_t rate, int64_t loop, uint32_t lead);
    uint32_t rs;
    int64_t from = wav_loop(s->wav, s->n, &rs);
    if (!s->whole && rs) {
        const char *why = "";
        uint32_t fr = 0;
        int16_t *lr = wav_load(s->wav, s->n, rs, &fr, &why);
        if ((s->whole = lr ? malloc((size_t)fr * 2 + 2) : NULL)) {
            for (uint32_t k = 0; k < fr; k++) s->whole[k] = (int16_t)((lr[2 * k] + lr[2 * k + 1]) / 2);
            s->whole_frames = fr;
            s->whole_rate = rs;
        }
        free(lr);
    }
    if (!s->whole || !s->whole_frames) return;
    uint32_t len = 16;
    while (len < n && !(out[len - 16 + 1] & 1)) len += 16;
    Shape sh = vag_shape(orig, onb);
    int64_t loop = !sh.loop ? -1 : from >= 0 ? from : (int64_t)((uint64_t)sh.from * 28 * s->whole_rate / rate);
    spu_hd_add((uint32_t)mz_crc32(0, out, len), len, s->whole, s->whole_frames, s->whole_rate, rate, loop, 28 * sh.lead);
}

/* NOISE.BIN's table of blocks (KERNEL.BIN's), and its banks: the number of blocks, 0 if not found. */
static uint32_t noise_banks(Touched *t, uint32_t *v, Bank **bk) {
    Touched *kern = touch(disc_index("KERNEL.BIN"));
    uint32_t nb = 0, ko = kern ? NOISE_TABLE - rd32(kern->data + 0x18) + 0x800 : 0;
    for (; t && kern && nb < 1023 && ko + 4 * nb + 4 <= kern->nsec * 2048; nb++) {
        v[nb] = rd32(kern->data + ko + 4 * nb);
        if (nb && v[nb] < v[nb - 1]) break;
        if (nb && v[nb] == t->nsec) break;
    }
    if (!t || !kern || !nb || v[0] || v[nb] != t->nsec || !(*bk = calloc(nb, sizeof **bk))) return 0;
    for (uint32_t b = 0; b < nb; b++)
        if (v[b + 1] == v[b] || !bank_at(t->data, v[b] * 2048, (v[b + 1] - v[b]) * 2048, &(*bk)[b])) (*bk)[b].vs = 0;
    return nb;
}

#include "modfs_pc_noise.inc"

/* The sounds into their banks. */
static void sfx_done(void) {
    if (!nsfx) return;
    Touched *t = touch(disc_index("NOISE.BIN"));
    static uint32_t v[1024];
    Bank *bk = NULL;
    uint32_t nb = noise_banks(t, v, &bk);
    uint32_t *base = nb ? calloc(nb + 1, sizeof *base) : NULL;
    for (uint32_t b = 0; base && b < nb; b++) base[b + 1] = base[b] + bk[b].vs + 1;
    int *who = base ? malloc(sizeof *who * (base[nb] + 1)) : NULL;
    if (!who) {
        for (int i = 0; i < nsfx; i++) { note("mods: %s: NOISE.BIN's table of banks wasn't found: not used", sfxs[i].what); nused--; }
        nb = 0;
    }
    uint8_t *d = t ? t->data : NULL;
    for (uint32_t k = 0; who && k <= base[nb]; k++) who[k] = -1;
    /* which sound each waveform gets: the copies of the one a mod names, then the one it names (a later mod
     * wins; inside a mod, of the PC files of one waveform the lowest ranked, modfs_pc_sfx_events.h: they go
     * highest rank first) */
    int *ord = malloc(sizeof *ord * (size_t)nsfx), no = 0;
    for (int i = 0, j; ord && i < nsfx; i = j) {
        size_t ml = strcspn(sfxs[i].what, "/");
        int top = 0;
        for (j = i; j < nsfx && strcspn(sfxs[j].what, "/") == ml && !strncmp(sfxs[j].what, sfxs[i].what, ml); j++)
            if (sfxs[j].rank > top) top = sfxs[j].rank;
        for (int r = top; r >= 0; r--)
            for (int k = i; k < j; k++)
                if (sfxs[k].rank == r) ord[no++] = k;
    }
    for (int pass = 0; who && ord && pass < 2; pass++)
        for (int oi = 0; oi < nsfx; oi++) {
            int i = ord[oi];
            Sfx *s = &sfxs[i];
            if (s->block >= nb || !bk[s->block].vs || s->vag < 1 || s->vag > bk[s->block].vs) {
                if (pass) { note("mods: %s: NOISE.BIN has no bank in block %u with a sound %u: not used", s->what, s->block, s->vag); nused--; }
                continue;
            }
            const uint8_t *was = d + vag_off(d, &bk[s->block], s->vag);
            uint32_t sz = vag_size(d, &bk[s->block], s->vag);
            if (pass) {
                if (s->places >= 0) who[base[s->block] + s->vag] = i;
                continue;
            }
            /* the very sound that's there (the originals written out, given back): nothing to do */
            uint32_t rate = vag_rate(d, &bk[s->block], s->vag), rs, frames = 0;
            wav_loop(s->wav, s->n, &rs);
            Shape sh = vag_shape(was, sz / 16);
            const char *why = "";
            int16_t *pcm = rs == rate ? wav_load(s->wav, s->n, rate, &frames, &why) : NULL, *dec = pcm ? vag_decode(was, sz / 16) : NULL;
            int same = dec && frames == sh.data * 28;
            for (uint32_t k = 0; same && k < frames; k++) same = pcm[2 * k] == dec[28 * sh.lead + k];
            free(pcm);
            free(dec);
            if (same) { s->places = -1; continue; }
            for (uint32_t b = 0; b < nb; b++)
                for (uint32_t j = 1; j <= bk[b].vs; j++)
                    if (vag_size(d, &bk[b], j) == sz && !memcmp(d + vag_off(d, &bk[b], j), was, sz)) who[base[b] + j] = i;
        }
    /* each bank with sounds to change: its waveforms again, in what its body was */
    for (uint32_t b = 0; who && b < nb; b++) {
        Bank *B = &bk[b];
        int any = 0;
        for (uint32_t j = 1; j <= B->vs; j++) any |= who[base[b] + j] >= 0;
        if (!any) continue;
        uint32_t total = 0, keep = 0, give[256] = {0}, want[256] = {0}, frames[256] = {0}, rate[256] = {0};
        int16_t *mono[256] = {0};
        for (uint32_t j = 1; j <= B->vs; j++) {
            uint32_t sz = vag_size(d, B, j);
            total += sz;
            int i = who[base[b] + j];
            if (i < 0) { keep += sz; continue; }
            Sfx *s = &sfxs[i];
            const char *why = "couldn't read it";
            rate[j] = vag_rate(d, B, j);
            int16_t *lr = wav_load(s->wav, s->n, rate[j], &frames[j], &why);
            if (!lr) {
                if (!s->bad) { note("mods: %s: %s: not used", s->what, why); nused--; }
                s->bad = 1;
                keep += sz;
                continue;
            }
            mono[j] = malloc((size_t)frames[j] * 2 + 2);
            for (uint32_t k = 0; mono[j] && k < frames[j]; k++) mono[j][k] = (int16_t)((lr[2 * k] + lr[2 * k + 1]) / 2);
            free(lr);
            if (!mono[j]) { keep += sz; continue; }
            Shape sh = vag_shape(d + vag_off(d, B, j), sz / 16);
            want[j] = 16 * (sh.lead + sh.tail + (frames[j] ? (frames[j] + 27) / 28 : 1));
            give[j] = want[j] < sz ? want[j] : sz;
        }
        /* what the shorter ones leave, to the longer ones, in order */
        uint32_t left = total - keep;
        for (uint32_t j = 1; j <= B->vs; j++) if (mono[j]) left -= give[j];
        for (uint32_t j = 1; j <= B->vs; j++)
            if (mono[j] && want[j] > give[j]) {
                uint32_t more = want[j] - give[j] < left ? want[j] - give[j] : left;
                give[j] += more;
                left -= more;
            }
        uint8_t *body = calloc(1, total + 16), *o = body;
        uint32_t at = B->at + B->body, now[256] = {0};
        for (uint32_t j = 1; body && j <= B->vs; j++) {
            uint32_t sz = vag_size(d, B, j);
            now[j] = sz;
            if (mono[j]) {
                Sfx *s = &sfxs[who[base[b] + j]];
                now[j] = sfx_encode(s, mono[j], frames[j], rate[j], d + at, sz / 16, give[j], o);
                sfx_whole(s, o, now[j], d + at, sz / 16, rate[j]);
                s->places++;
            } else
                memcpy(o, d + at, sz);
            o += now[j];
            at += sz;
        }
        if (body) {
            memcpy(d + B->at + B->body, body, total);   /* (zeros after, if it's shorter) */
            for (uint32_t j = 1; j <= B->vs; j++) wr16(d + B->at + B->tab + 2 * j, (uint16_t)(now[j] / 8));
        }
        free(body);
        for (uint32_t j = 1; j <= B->vs; j++) free(mono[j]);
    }
    for (int i = 0; i < nsfx; i++) {
        Sfx *s = &sfxs[i];
        if (s->places > 0) {
            char cut[64] = "";
            if (s->cut) snprintf(cut, sizeof cut, "; %d of its banks keep less of it, RESPeCt plays it whole", s->cut);
            note("mods: %s: in place of NOISE.BIN's sound %u of block %u%s (%d bank%s; %.2f s at %u Hz%s)", s->what, s->vag, s->block,
                 s->places > 1 ? " and its copies" : "", s->places, s->places > 1 ? "s" : "", s->secs, s->rate, cut);
        } else if (s->places == 0 && !s->bad && who && s->block < nb && s->vag >= 1 && s->vag <= bk[s->block].vs) {
            int w = who[base[s->block] + s->vag];
            note("mods: %s: NOISE.BIN's sound %u of block %u is %s's (the same sound%s): not used", s->what, s->vag, s->block,
                 w >= 0 && w != i ? sfxs[w].what : "another file", s->rank ? "; the PS1 plays one sound where the PC has more" : "");
            nused--;
        }
        free(s->wav);
    }
    free(sfxs);
    sfxs = NULL;
    nsfx = 0;
    free(ord);
    free(who);
    free(bk);
    free(base);
}

/* ------------------------------------------------------------ music */

/* A room's music is a sequence (NOISE/240/music1.seq, played with the
 * room's sound bank) that the game starts, stops and fades (kernel_sound.c,
 * room_sound.c), and whose place in it times the music's events. With
 * NOISE/240/music1.wav a mod gives it as a recording instead, as the PC
 * version has its music: the sequence still plays, every note at velocity
 * 0 (silent: what the game times by is the same), and spu.c mixes the WAV
 * in while it plays, at its volume (fades, the options' music volume),
 * from the start when it starts again, repeating from the WAV's loop
 * start (a smpl chunk) or else from the beginning. Sound sets: block 230
 * is set 0 (room_sound.c: 0xE6 + the room's set); sequences 0 and 1 of the
 * set are music1 and music2. */
#define SND_SET_NOW   0x80062E7Cu                /* s16: the sound set loaded, -1 none */
#define SND_SEQ_IDS   0x80062E70u                /* s16 x 2: its sequences' ids */
#define SS_SCORE      0x80064678u                /* libsnd: per id, its sequence's state */

typedef struct { uint32_t block, part, frames, loop; int alt; int16_t *pcm; uint8_t *wav; long n; char what[600]; } Music;
static Music musics[64];
static int nmusics;

/* Every note-on of a sequence (after its 15-byte header) at velocity 0; 0 if it isn't one this reads to its end. */
static int seq_mute(uint8_t *d, uint32_t n) {
    uint32_t o = 15;
    uint8_t st = 0;
    if (n < 16 || memcmp(d, "pQES", 4)) return 0;
    while (o < n) {
        while (o < n && d[o] & 0x80) o++;    /* the time before it */
        if (++o >= n) return 0;
        if (d[o] & 0x80) st = d[o++];
        if (!st || o >= n) return 0;
        if (st == 0xFF) {
            uint8_t t = d[o++];
            if (t == 0x2F) return 1;
            if (t != 0x51) return 0;
            o += 3;                              /* the tempo */
            continue;
        }
        uint32_t k = (st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0 ? 1 : 2;
        if (o + k > n) return 0;
        if ((st & 0xF0) == 0x90) d[o + 1] = 0;
        o += k;
    }
    return 0;
}

/* alt: another PC room's WAV of a sequence (modfs_pc_bgm.h): in one mod, the
 * WAV that matches the sequence best (alt 0) is used for it. */
static int music_apply(const char *mod, const char *rel, unsigned blk, unsigned part, const char *path, int alt) {
    size_t ml = strlen(mod);
    for (int i = 0; i < nmusics; i++)
        if (musics[i].block == blk && musics[i].part == part && alt && !musics[i].alt
            && !strncmp(musics[i].what, mod, ml) && musics[i].what[ml] == '/') {
            note("mods: %s/%s: block %u's music %u is %s's (the same music, that one matches it better): not used there",
                 mod, rel, blk, part, musics[i].what);
            return 1;
        }
    long n = 0;
    uint8_t *d = load(path, &n);
    if (!d) { note("mods: %s/%s: couldn't read it", mod, rel); return 1; }
    for (int i = 0; i < nmusics; i++)            /* a later mod wins */
        if (musics[i].block == blk && musics[i].part == part) {
            note("mods: %s/%s: block %u's music %u, in place of %s", mod, rel, blk, part, musics[i].what);
            free(musics[i].wav);
            musics[i] = musics[--nmusics];
            nused--;
            break;
        }
    if (nmusics == (int)(sizeof musics / sizeof musics[0])) { free(d); note("mods: %s/%s: too many: not used", mod, rel); return 1; }
    Music *m = &musics[nmusics++];
    memset(m, 0, sizeof *m);
    m->block = blk;
    m->part = part;
    m->alt = alt;
    m->wav = d;
    m->n = n;
    snprintf(m->what, sizeof m->what, "%s/%s", mod, rel);
    nused++;
    return 1;
}

/* The music WAVs: their sequences silenced, the WAVs read. */
static void music_done(void) {
    if (!nmusics) return;
    int fi = disc_index("NOISE.BIN");
    Touched *t = touch(fi);
    static uint32_t v[1024];
    Bank *bk = NULL;
    uint32_t nb = noise_banks(t, v, &bk);
    for (int i = 0; i < nmusics; i++) {
        Music *m = &musics[i];
        uint32_t at = m->block < nb && bk[m->block].vs ? v[m->block] * 2048 : 0, c = at ? rd32(t->data + at) : 0;
        const char *why = "couldn't read it";
        if (m->part < 1 || m->part + 2 > c) why = "NOISE.BIN has no such music (a room's sound set: blocks 230 and up)";
        else {
            uint32_t off = at + rd32(t->data + at + 4 + 4 * m->part), len = rd32(t->data + at + 8 + 4 * m->part) - (off - at);
            uint8_t *seq = t->data + off;
            for (int g = 0; g < ngrows; g++)     /* (a mod's own sequence there) */
                if (grows[g].fi == fi && grows[g].offset == off) { seq = grows[g].data; len = grows[g].size; }
            uint32_t rs;
            int64_t from = wav_loop(m->wav, m->n, &rs);
            if (!seq_mute(seq, len)) why = "its sequence isn't one this can silence";
            else if ((m->pcm = wav_load(m->wav, m->n, 44100, &m->frames, &why)) && m->frames) {
                m->loop = from >= 0 && rs ? (uint32_t)((uint64_t)from * 44100 / rs) : 0;
                if (m->loop >= m->frames) m->loop = 0;
                note("mods: %s: in place of NOISE.BIN block %u's music %u (%.1f s%s)", m->what, m->block, m->part, m->frames / 44100.0,
                     from >= 0 ? ", repeating from its loop start" : "");
                free(m->wav);
                m->wav = NULL;
                continue;
            }
        }
        note("mods: %s: %s: not used", m->what, why);
        nused--;
        free(m->wav);
        free(m->pcm);
        musics[i--] = musics[--nmusics];
    }
    free(bk);
}

/* spu.c, every sample (44.1 kHz): the music WAVs playing, stereo, at full
 * scale; what the game's sequences do is looked at every 512 samples. */
void modfs_music(int32_t *l, int32_t *r) {
    static struct { int m, playing; uint32_t pos; int32_t vl, vr; } st[2] = {{-1, 0, 0, 0, 0}, {-1, 0, 0, 0, 0}};
    static int poll;
    *l = *r = 0;
    if (!g_mods_dir[0]) return;
    if (--poll <= 0) {
        poll = 512;
        int16_t set = (int16_t)rd16(ram_ptr(SND_SET_NOW));
        for (int k = 0; k < 2; k++) {
            int16_t id = (int16_t)rd16(ram_ptr(SND_SEQ_IDS + 2 * (uint32_t)k));
            uint32_t p = id >= 0 && id < 32 ? rd32(ram_ptr(SS_SCORE + 4 * (uint32_t)id)) : 0;
            int m = -1;
            if ((p >> 21) != 0x80000000u >> 21) p = 0;
            for (int i = 0; p && set >= 0 && i < nmusics; i++)
                if (musics[i].block == 0xE6u + (uint32_t)set && musics[i].part == (uint32_t)k + 1) m = i;
            if (m != st[k].m) { st[k].m = m; st[k].playing = 0; st[k].pos = 0; }
            if (!p) continue;
            uint32_t rd = rd32(ram_ptr(p)), start = rd32(ram_ptr(p + 4));
            int playing = rd32(ram_ptr(p + 0x98)) & 1;
            if (playing && !st[k].playing && rd - start < 64) {   /* (started, not resumed) */
                st[k].pos = 0;
                rt_log("music: NOISE/%03d/music%d%s", 0xE6 + set, k + 1, m >= 0 ? ".wav (a mod's)" : ".seq");
            }
            st[k].playing = playing;
            st[k].vl = (int16_t)rd16(ram_ptr(p + 0x58));
            st[k].vr = (int16_t)rd16(ram_ptr(p + 0x5A));
        }
    }
    for (int k = 0; k < 2; k++) {
        if (st[k].m < 0 || !st[k].playing) continue;
        const Music *m = &musics[st[k].m];
        if (st[k].pos >= m->frames) st[k].pos = m->loop;
        *l += m->pcm[2 * st[k].pos] * st[k].vl / 127;
        *r += m->pcm[2 * st[k].pos + 1] * st[k].vr / 127;
        st[k].pos++;
    }
}

/* ------------------------------------------------------------ pictures */

static int is_picture(const char *ext) {
    return !strcasecmp(ext, ".PNG") || !strcasecmp(ext, ".BMP") || !strcasecmp(ext, ".JPG") || !strcasecmp(ext, ".JPEG") ||
           !strcasecmp(ext, ".TGA");
}

/* The PC version's BMPs that are the disc's TIMs (modfs_bmp.h): the
 * picture (BMP, PNG, JPG, TGA; a PNG's alpha is transparency) goes into
 * the TIM's place in the TIM's own format and size (scaled to it if it's
 * another, keeping the PS1's look: bigger pictures belong in a texture
 * pack). Paletted TIMs get a palette made for the picture (median cut);
 * a TIM with several palettes (a font's colours) keeps them, the picture
 * drawn with the first one's colours. Black where the original was
 * transparent stays transparent; other black is the darkest blue, which
 * the PS1 doesn't treat as see-through. */
static uint16_t to15(uint32_t rgba) {
    uint16_t v = (uint16_t)(((rgba >> 3) & 31) | ((rgba >> 11) & 31) << 5 | ((rgba >> 19) & 31) << 10);
    return v ? v : 0x0400;
}

static int qch;                                  /* the channel boxes are sorted on */
static int cmp_ch(const void *a, const void *b) {
    int x = (*(const uint16_t *)a >> (5 * qch)) & 31, y = (*(const uint16_t *)b >> (5 * qch)) & 31;
    return x - y;
}

/* Up to n colours for the 15-bit colours c[0..count) (sorted in place). */
static int median_cut(uint16_t *c, size_t count, uint16_t *pal, int n) {
    struct { size_t at, len; } box[256];
    int nb = 1;
    box[0].at = 0;
    box[0].len = count;
    while (nb < n) {
        int pick = -1, pch = 0, best = 0;
        for (int i = 0; i < nb; i++) {           /* the box with the widest spread */
            if (box[i].len < 2) continue;
            for (int ch = 0; ch < 3; ch++) {
                int lo = 31, hi = 0;
                for (size_t k = 0; k < box[i].len; k++) {
                    int v = (c[box[i].at + k] >> (5 * ch)) & 31;
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
                if (hi - lo > best) { best = hi - lo; pick = i; pch = ch; }
            }
        }
        if (pick < 0) break;
        qch = pch;
        qsort(c + box[pick].at, box[pick].len, sizeof *c, cmp_ch);
        size_t half = box[pick].len / 2;
        box[nb].at = box[pick].at + half;
        box[nb].len = box[pick].len - half;
        box[pick].len = half;
        nb++;
    }
    for (int i = 0; i < nb; i++) {
        uint32_t s[3] = {0, 0, 0};
        for (size_t k = 0; k < box[i].len; k++)
            for (int ch = 0; ch < 3; ch++) s[ch] += (c[box[i].at + k] >> (5 * ch)) & 31;
        uint32_t m = box[i].len ? (uint32_t)box[i].len : 1;
        pal[i] = (uint16_t)((s[0] + m / 2) / m | ((s[1] + m / 2) / m) << 5 | ((s[2] + m / 2) / m) << 10);
        if (!pal[i]) pal[i] = 0x0400;
    }
    return nb;
}

static int nearest(const uint16_t *pal, int n, uint16_t v) {
    /* remembered for the palette last asked about (a picture asks for each of its pixels) */
    static uint16_t last[256];
    static int lastn = -1;
    static int16_t memo[32768];
    if (n > 256) n = 256;
    if (n != lastn || memcmp(last, pal, (size_t)n * 2)) {
        memcpy(last, pal, (size_t)n * 2);
        lastn = n;
        memset(memo, 0xFF, sizeof memo);
    }
    if (memo[v & 0x7FFF] >= 0) return memo[v & 0x7FFF];
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < n; i++) {
        int dr = (int)(pal[i] & 31) - (v & 31), dg = (int)((pal[i] >> 5) & 31) - ((v >> 5) & 31);
        int db = (int)((pal[i] >> 10) & 31) - ((v >> 10) & 31);
        int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (pal[i] && d < bd) { bd = d; best = i; }
    }
    memo[v & 0x7FFF] = (int16_t)best;
    return best;
}

/* A picture (RGBA, w x h) into a TIM in the TIM's own format and size;
 * NULL, or why not. */
static void tim_size(const uint8_t *tim, uint32_t *w, uint32_t *h) {
    uint32_t flag = rd32(tim + 4), bpp = (uint32_t[]){4, 8, 16, 24}[flag & 3];
    const uint8_t *pix = flag & 8 ? tim + 8 + rd32(tim + 8) : tim + 8;
    *w = rd16(pix + 8) * 16 / bpp;
    *h = rd16(pix + 10);
}

static int tim_keep_palette;                    /* (picture_apply: the palette's place is another picture's too) */
static const char *tim_from_picture(uint8_t *tim, const uint8_t *img, uint32_t w, uint32_t h, const char **how) {
    /* the TIM: its palette (if any), then its pixels */
    uint32_t flag = rd32(tim + 4), bpp = (uint32_t[]){4, 8, 16, 24}[flag & 3];
    uint8_t *clut = flag & 8 ? tim + 8 : NULL;
    uint32_t cw = clut ? rd16(clut + 8) : 0, chs = clut ? rd16(clut + 10) : 0;
    uint8_t *pix = clut ? clut + rd32(clut) : tim + 8;
    uint32_t tw = rd16(pix + 8) * 16 / bpp, th = rd16(pix + 10);
    uint8_t *px = pix + 12;
    if (bpp == 24 || (bpp < 16 && (!clut || cw < (1u << bpp)))) return "the TIM's format isn't one this writes";
    /* the picture at the TIM's size (nearest pixel when it's the same) */
    uint32_t *rgba = malloc((size_t)tw * th * 4);
    for (uint32_t y = 0; y < th; y++)
        for (uint32_t x = 0; x < tw; x++) {
            uint32_t x0 = x * (uint32_t)w / tw, x1 = (x + 1) * (uint32_t)w / tw, y0 = y * (uint32_t)h / th, y1 = (y + 1) * (uint32_t)h / th;
            if (x1 <= x0) x1 = x0 + 1;
            if (y1 <= y0) y1 = y0 + 1;
            uint32_t s[4] = {0, 0, 0, 0}, m = 0;
            for (uint32_t yy = y0; yy < y1; yy++)
                for (uint32_t xx = x0; xx < x1; xx++, m++)
                    for (int k = 0; k < 4; k++) s[k] += img[((size_t)yy * w + xx) * 4 + k];
            rgba[y * tw + x] = (s[0] / m) | (s[1] / m) << 8 | (s[2] / m) << 16 | (s[3] / m) << 24;
        }
    /* what the original shows at each pixel (its 15-bit colour; 0: see-through) */
    uint8_t *row0 = clut ? clut + 12 : NULL;
    uint16_t *was = malloc((size_t)tw * th * 2);
    for (uint32_t k = 0; k < tw * th; k++) {
        uint32_t v;
        if (bpp == 16) v = rd16(px + k * 2);
        else if (bpp == 8) v = rd16(row0 + px[k] * 2);
        else v = rd16(row0 + ((px[k / 2] >> (k & 1) * 4) & 15) * 2);
        was[k] = (uint16_t)v;
    }
    uint16_t *col = malloc((size_t)tw * th * 2);  /* the new picture, 15-bit; 0 see-through */
    size_t opaque = 0;
    int same = 1;
    for (uint32_t k = 0; k < tw * th; k++) {
        uint32_t p = rgba[k];
        int clear = (p >> 24) < 128 || ((p & 0xFFFFFF) == 0 && was[k] == 0);
        col[k] = clear ? 0 : to15(p);
        opaque += !clear;
        if (same && !(col[k] == (was[k] & 0x7FFF) || (col[k] == 0x0400 && was[k] && !(was[k] & 0x7FFF)))) same = 0;
    }
    free(rgba);
    if (same) {                                  /* the picture as the disc has it: left as it is */
        free(col);
        free(was);
        *how = "as on the disc";
        return NULL;
    }
    uint16_t stp = 0;                            /* the original palette's semi-transparency bit, if all its colours have it */
    if (clut) {
        int with = 0, without = 0;
        for (uint32_t k = 0; k < cw; k++) {
            uint16_t v = rd16(row0 + k * 2);
            if (v & 0x7FFF) { if (v & 0x8000) with++; else without++; }
        }
        if (with && !without) stp = 0x8000;
    } else {
        int with = 0, without = 0;
        for (uint32_t k = 0; k < tw * th; k++)
            if (was[k] & 0x7FFF) { if (was[k] & 0x8000) with++; else without++; }
        if (with && !without) stp = 0x8000;
    }
    if (bpp == 16) {
        for (uint32_t k = 0; k < tw * th; k++) {     /* each pixel keeps its own semi-transparency bit */
            uint16_t v = col[k] == (was[k] & 0x7FFF) ? was[k] : col[k] ? (uint16_t)(col[k] | (was[k] ? was[k] & 0x8000 : stp)) : 0;
            wr16(px + k * 2, v);
        }
        *how = "16-bit";
    } else {
        int ncol = 1 << bpp;
        uint16_t pal[256];
        if (chs == 1 && !tim_keep_palette) {     /* a palette for this picture: entry 0 see-through */
            uint16_t *list = malloc((opaque ? opaque : 1) * 2);
            size_t m = 0;
            for (uint32_t k = 0; k < tw * th; k++) if (col[k]) list[m++] = col[k];
            int got = m ? median_cut(list, m, pal + 1, ncol - 1) : 0;
            free(list);
            pal[0] = 0;
            for (int k = 1 + got; k < ncol; k++) pal[k] = 0;
            for (int k = 0; k < ncol; k++) wr16(row0 + k * 2, pal[k] ? (uint16_t)(pal[k] | stp) : 0);
            *how = bpp == 8 ? "256 colours" : "16 colours";
        } else {                                 /* the TIM's own palettes kept */
            for (int k = 0; k < ncol; k++) pal[k] = rd16(row0 + k * 2) & 0x7FFF;
            *how = "its own palettes";
        }
        int clear = -1;
        for (int k = 0; k < ncol && clear < 0; k++) if (!pal[k]) clear = k;
        for (uint32_t k = 0; k < tw * th; k++) {
            int ix = col[k] ? nearest(pal, ncol, col[k]) : (clear >= 0 ? clear : nearest(pal, ncol, 0x0400));
            if (bpp == 8) px[k] = (uint8_t)ix;
            else px[k / 2] = (uint8_t)((px[k / 2] & (k & 1 ? 0x0F : 0xF0)) | ix << (k & 1) * 4);
        }
    }
    free(col);
    free(was);
    return NULL;
}

/* TIMs a mod changed, as they'd be in VRAM: a quick save made before the
 * mod has the old pixels there (a font loaded once a game), so loading it
 * puts the new ones in where the old still are (modfs_state_loaded). */
static uint32_t tim_len(const uint8_t *tim) {
    uint32_t flag = rd32(tim + 4), n = 8;
    if (flag & 8) n += rd32(tim + 8);
    return n + rd32(tim + n);
}

/* The TIM's blocks (its palette, its pixels) before and after. */
static void vram_fix_add(const uint8_t *before, const uint8_t *after) {
    uint32_t flag = rd32(after + 4), o = 8;
    for (int b = 0; b < ((flag & 8) ? 2 : 1); b++) {
        uint32_t len = rd32(after + o), x = rd16(after + o + 4), y = rd16(after + o + 6), w = rd16(after + o + 8), h = rd16(after + o + 10);
        if (len == rd32(before + o) && w && h && x + w <= 1024 && y + h <= 512 && memcmp(before + o + 12, after + o + 12, (size_t)w * h * 2)) {
            vfix = realloc(vfix, (size_t)(nvfix + 1) * sizeof *vfix);
            VramFix *f = &vfix[nvfix++];
            *f = (VramFix){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, malloc((size_t)w * h * 2), malloc((size_t)w * h * 2)};
            memcpy(f->was, before + o + 12, (size_t)w * h * 2);
            memcpy(f->now, after + o + 12, (size_t)w * h * 2);
        }
        o += len;
    }
}

static int modfs_hd_add(const uint8_t *tim, const uint8_t *img, uint32_t w, uint32_t h);

static int bmp_apply(const char *mod, const char *rel, const char *up, const char *path) {
    size_t i = 0;                                /* by name: map/MAP00_1.bmp, or .png, .jpg, .tga */
    const char *dot = strrchr(up, '.');
    size_t stem = dot ? (size_t)(dot - up) : strlen(up);
    if (!dot || !is_picture(dot)) return 0;
    for (; i < sizeof modfs_bmp / sizeof modfs_bmp[0]; i++)
        if (!strncmp(modfs_bmp[i].pc, up, stem) && !strcmp(modfs_bmp[i].pc + stem, ".BMP")) break;
    if (i == sizeof modfs_bmp / sizeof modfs_bmp[0]) return 0;
    int di = disc_index(modfs_bmp[i].disc);
    Touched *t = di >= 0 ? touch(di) : NULL;
    uint8_t *tim = t ? t->data + modfs_bmp[i].offset : NULL;
    if (!tim || (uint32_t)mz_crc32(0, tim, modfs_bmp[i].size) != modfs_bmp[i].crc) {
        /* it may be a picture an earlier mod changed: then the TIM's header is still the one expected */
        if (!tim || rd32(tim) != 0x10) {
            note("mods: %s/%s: the disc's %s isn't the one expected there: not used", mod, rel, modfs_bmp[i].disc);
            return 1;
        }
    }
    long n = 0;
    uint8_t *d = load(path, &n);
    int w, h, c;
    uint8_t *img = d ? stbi_load_from_memory(d, (int)n, &w, &h, &c, 4) : NULL;
    free(d);
    if (!img) {
        note("mods: %s/%s: couldn't read it as a picture: not used", mod, rel);
        return 1;
    }
    uint32_t tl = tim_len(tim);
    uint8_t *before = malloc(tl);
    if (before) memcpy(before, tim, tl);
    const char *how = "", *why = tim_from_picture(tim, img, (uint32_t)w, (uint32_t)h, &how);
    if (!why && before) vram_fix_add(before, tim);
    free(before);
    int hd = !why && modfs_hd_add(tim, img, (uint32_t)w, (uint32_t)h);
    stbi_image_free(img);
    if (why) {
        note("mods: %s/%s: %s: not used", mod, rel, why);
        return 1;
    }
    uint32_t tw = 0, th = 0;
    tim_size(tim, &tw, &th);
    note("mods: %s/%s: into %s's picture at %u (%ux%u, %s%s)", mod, rel, modfs_bmp[i].disc, modfs_bmp[i].offset, tw, th, how,
           hd ? ", with an HD replacement" : (uint32_t)w != tw || (uint32_t)h != th ? ", scaled to the PS1's size" : "");
    nused++;
    return 1;
}

/* Every picture of a disc file (TIMs as the game loads them: 4 or 8 bits
 * with a palette, or 16 bits), in order: PICTURES/<file>/<n>.png names
 * the n-th (extract writes them all). A picture in a packed block isn't
 * one of these (its block has its own names). */
static uint32_t tim_at(const uint8_t *d, uint32_t n, uint32_t o) {   /* its length, 0 if none there */
    if ((uint64_t)o + 20 > n || rd32(d + o) != 0x10) return 0;
    uint32_t flag = rd32(d + o + 4), p = o + 8;
    if (flag != 8 && flag != 9 && flag != 2) return 0;
    for (int part = flag & 8 ? 0 : 1; part < 2; part++) {
        if ((uint64_t)p + 12 > n) return 0;
        uint32_t len = rd32(d + p), x = rd16(d + p + 4), y = rd16(d + p + 6), w = rd16(d + p + 8), h = rd16(d + p + 10);
        if (!w || !h || x >= 1024 || y >= 512 || len != 12 + w * h * 2 || len > (part ? 0x100000u : 0x20000u) || (uint64_t)p + len > n) return 0;
        p += len;
    }
    return p - o;
}
static int file_pictures(const uint8_t *d, uint32_t n, uint32_t *off, int max) {
    int k = 0;
    for (uint32_t o = 0; o + 20 <= n && k < max;) {
        uint32_t len = tim_at(d, n, o);
        if (len) { off[k++] = o; o += (len + 3) & ~3u; }
        else o += 4;
    }
    return k;
}
static int picture_apply(const char *mod, const char *rel, const char *up, const char *path) {
    if (strncmp(up, "PICTURES/", 9)) return 0;
    const char *slash = strrchr(up, '/'), *dot = strrchr(up, '.');
    char file[256];
    unsigned idx;
    char rest[8] = "";
    if (!dot || !is_picture(dot) || slash - (up + 9) <= 0 || slash - (up + 9) >= (int)sizeof file ||
        sscanf(slash + 1, "%u%7s", &idx, rest) != 2 || strcasecmp(rest, dot)) {
        note("mods: %s/%s: name it PICTURES/<disc file>/<number>.png, as extract does: not used", mod, rel);
        return 1;
    }
    memcpy(file, up + 9, (size_t)(slash - (up + 9)));
    file[slash - (up + 9)] = 0;
    int fi = disc_index(file);
    Touched *t = fi >= 0 ? touch(fi) : NULL;
    static uint32_t off[4096];
    int count = t ? file_pictures(t->data, t->nsec * 2048, off, 4096) : 0;
    if (!t || (int)idx >= count) {
        note("mods: %s/%s: %s has no picture %u (extract shows its pictures): not used", mod, rel, file, idx);
        return 1;
    }
    uint8_t *tim = t->data + off[idx];
    int shared = -1;                             /* another picture whose palette goes to the same place */
    if (rd32(tim + 4) & 8)
        for (int k = 0; k < count && shared < 0; k++)
            if (k != (int)idx && (rd32(t->data + off[k] + 4) & 8) && rd32(t->data + off[k] + 12) == rd32(tim + 12)) shared = k;
    long n = 0;
    uint8_t *d = load(path, &n);
    int w, h, c;
    uint8_t *img = d ? stbi_load_from_memory(d, (int)n, &w, &h, &c, 4) : NULL;
    free(d);
    if (!img) { note("mods: %s/%s: couldn't read it as a picture: not used", mod, rel); return 1; }
    uint32_t tl = tim_len(tim);
    uint8_t *before = malloc(tl);
    if (before) memcpy(before, tim, tl);
    tim_keep_palette = shared >= 0;
    const char *how = "", *why = tim_from_picture(tim, img, (uint32_t)w, (uint32_t)h, &how);
    tim_keep_palette = 0;
    if (!why && before) vram_fix_add(before, tim);
    free(before);
    int hd = !why && modfs_hd_add(tim, img, (uint32_t)w, (uint32_t)h);
    stbi_image_free(img);
    if (why) { note("mods: %s/%s: %s: not used", mod, rel, why); return 1; }
    uint32_t tw = 0, th = 0;
    tim_size(tim, &tw, &th);
    char shares[160] = "";
    if (shared >= 0 && !hd)
        snprintf(shares, sizeof shares, "; its palette is picture %d's too, so its colours stay: give it at 2x or more for any colour", shared);
    note("mods: %s/%s: into %s's picture %u (%ux%u, %s%s%s)", mod, rel, file, idx, tw, th, how,
         hd ? ", with an HD replacement" : (uint32_t)w != tw || (uint32_t)h != th ? ", scaled to the PS1's size" : "", shares);
    nused++;
    return 1;
}

/* HD keeps the native upload and palette identities. With multiple
 * CLUT rows, the authored picture represents row zero: retain its palette
 * indices at high resolution and map them through each original row.
 * A green/red UI font must not reuse the row-zero colour image. */
static int modfs_hd_add_banks(const uint8_t *tim, const uint8_t *img, uint32_t w, uint32_t h, const uint8_t *extra, uint32_t extra_rows) {
    uint32_t flag = rd32(tim + 4), bpp = (uint32_t[]){4, 8, 16, 24}[flag & 3];
    const uint8_t *clut = flag & 8 ? tim + 8 : NULL;
    if (bpp == 24 || (bpp < 16 && !clut)) return 0;
    const uint8_t *pix = clut ? clut + rd32(clut) : tim + 8;
    uint32_t tw = rd16(pix + 8) * 16 / bpp, th = rd16(pix + 10), ww = rd16(pix + 8);
    uint32_t own_rows = bpp < 16 ? rd16(clut + 10) : 1, cw = clut ? rd16(clut + 8) : 0;
    if (bpp == 16) extra_rows = 0;
    uint32_t npal = own_rows + extra_rows;
    uint32_t colors = bpp == 4 ? 16 : 256;
    if (!tw || !th || !npal || (bpp < 16 && cw < colors)) return 0;
    uint32_t k = w / tw < h / th ? w / tw : h / th;
    if (k < 2) return 0;
    uint32_t W = k * tw, H = k * th, x = rd16(pix + 4), y = rd16(pix + 6);
    extern size_t g_hd_budget;
    uint64_t pixels = (uint64_t)W * H;
    if (W > INT32_MAX || H > INT32_MAX || pixels > SIZE_MAX / 4 / npal || pixels > g_hd_budget / 4 / npal || x + ww > 1024 || y + th > 512) return 0;
    uint32_t **images = calloc(npal, sizeof *images), *keys = calloc(npal, sizeof *keys);
    if (!images || !keys) { free(images); free(keys); return 0; }
    uint16_t source_pal[256];
    if (bpp < 16) for (uint32_t i = 0; i < colors; i++) source_pal[i] = rd16(clut + 12 + i * 2);
    for (uint32_t row = 0; row < npal; row++) {
        const uint8_t *pal = bpp < 16 ? (row < own_rows ? clut + 12 + (size_t)row * cw * 2 : extra + (size_t)(row - own_rows) * colors * 2) : NULL;
        keys[row] = pal ? (uint32_t)mz_crc32(0, pal, (size_t)colors * 2) : 0;
        int duplicate = 0;
        for (uint32_t j = 0; j < row; j++) if (keys[j] == keys[row]) duplicate = 1;
        if (duplicate) continue;
        uint32_t *px = images[row] = malloc((size_t)W * H * 4);
        if (!px) { for (uint32_t j = 0; j < row; j++) free(images[j]); free(images); free(keys); return 0; }
        for (uint32_t yy = 0; yy < H; yy++) for (uint32_t xx = 0; xx < W; xx++) {
            uint32_t sx = (uint32_t)((uint64_t)xx * w / W), sy = (uint32_t)((uint64_t)yy * h / H), tx = xx / k, ty = yy / k, v;
            const uint8_t *p = img + ((size_t)sy * w + sx) * 4;
            uint32_t rgba = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
            if (npal > 1) {
                uint32_t ni = bpp == 8 ? pix[12 + (size_t)ty * tw + tx]
                    : (pix[12 + ((size_t)ty * tw + tx) / 2] >> (tx & 1) * 4) & 15;
                uint16_t native = source_pal[ni], colour = to15(rgba);
                /* Match the native converter's transparency rule. Keep an
                 * unchanged native index: two equal colours in row zero may
                 * have different colours in another row. STP black is opaque. */
                int clear = p[3] < 128 || (!rgba && !native);
                int index = colour == (native & 0x7FFF) || (!rgba && native == 0x8000)
                    ? (int)ni : nearest(source_pal, (int)colors, colour);
                v = clear ? 0 : rd16(pal + index * 2);
                uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
                rgba = (r | r >> 5) | (g | g >> 5) << 8 | (b | b >> 5) << 16;
            } else if (bpp == 16) v = rd16(pix + 12 + 2 * ((size_t)ty * tw + tx));
            else if (bpp == 8) v = rd16(pal + 2 * pix[12 + (size_t)ty * tw + tx]);
            else v = rd16(pal + 2 * ((pix[12 + ((size_t)ty * tw + tx) / 2] >> (tx & 1) * 4) & 15));
            px[(size_t)yy * W + xx] = rgba | (!v ? 0 : (uint32_t)p[3] << 24);
        }
    }
    uint32_t hash = (uint32_t)mz_crc32(0, pix + 12, (size_t)ww * th * 2);
    int found = 0;
    for (int i = 0; i < nmod_hd_rects; i++) {
        ModHdRect *r = &mod_hd_rects[i];
        if (r->x == x && r->y == y && r->w == ww && r->h == th && r->hash == hash) found = 1;
    }
    if (!found) {
        ModHdRect *r = realloc(mod_hd_rects, (size_t)(nmod_hd_rects + 1) * sizeof *r);
        if (!r) { for (uint32_t row = 0; row < npal; row++) free(images[row]); free(images); free(keys); return 0; }
        mod_hd_rects = r;
        mod_hd_rects[nmod_hd_rects++] = (ModHdRect){(uint16_t)x, (uint16_t)y, (uint16_t)ww, (uint16_t)th, hash};
    }
    void texpack_add_image(uint32_t hash, uint32_t pal, int w, int h, uint32_t *px);
    for (uint32_t row = 0; row < npal; row++) if (images[row]) texpack_add_image(hash, keys[row], (int)W, (int)H, images[row]);
    free(images); free(keys);
    return 1;
}

static int modfs_hd_add(const uint8_t *tim, const uint8_t *img, uint32_t w, uint32_t h) {
    return modfs_hd_add_banks(tim, img, w, h, NULL, 0);
}

/* ------------------------------------------------------------ texts */

/* The game's texts are 16-bit glyph codes (message.c): up to 0x8F a glyph
 * of the font (18 a row, 8x14, in EV_OBJ.DAT's blocks 0x186-0x188: as
 * pictures FONT/FONT186.png...), 0 a space, and control codes: 0xFFFE a
 * new line, 0xFFFF the end, 0xFF00/0xFF01 a "more" prompt, 0xFExx wait
 * xx frames (0xFE00 ends the message), 0xFCxx a colour, 0xFBxx the typing
 * speed, 0xFD00-0xFD02 a two-way choice. A mod gives them as UTF-8 text,
 * each text after its "@n" line, the codes as {tags}:
 *   text/ROOMS.txt  the rooms' messages (ROOMENV.DAT, a block a room:
 *                   @block.n), which grow with their room's block;
 *   text/GAME.txt   the items' names and the inventory's lines (TEST.BIN,
 *                   a pointer each), moved past the game's 2 MB of RAM
 *                   when they no longer fit in their place;
 *   text/ITEMS.txt  the items' descriptions (B/BIGTIM.BIN, after each
 *                   item's picture: room for some 700 more letters each).
 * text/CHARS.txt maps a character (or a {name}) to a glyph: the font's own
 * are known, and a letter drawn in the font's free cells (128-143) gets a
 * line "ì 128". */
#define ROOMENV_TABLE 0x800A32F0u                /* TEST.BIN: ROOMENV.DAT's blocks */
#define EVOBJ_TABLE   0x800A195Cu                /* TEST.BIN: EV_OBJ.DAT's blocks */
#define GAME_TEXTS    0x3A518u                   /* TEST.BIN: the items' names... (file offsets) */
#define GAME_TEXTS_END 0x3AC44u
#define GAME_PTRS_END 0x3AE04u                   /* their pointers, after them */

static const char *const glyph_name[144] = {
    " ", "{_}", "▸", "{L2}", "{R2}", "{L1}", "{R1}", "△", "○", "×", "□", "▾",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ";", ",", "\"", "!", "?", "⁉",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X",
    "Y", "Z", "(", "/", ")", "'", "-", "·",
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r", "s", "t", "u", "v", "w", "x",
    "y", "z", "Ä", "ä", "Ö", "ö", "Ü", "ü", "ß", "À", "à", "Â", "â", "È", "è", "É", "é", "Ê", "ê", "Ï", "ï", "Î", "î",
    "Ô", "ô", "Ù", "ù", "Û", "û", "Ç", "ç", "{S.}", "{T.}", "{A.}", "{R.}", "“", ".", "…", "{dash1}", "{dash2}", "+", "=", "&",
};
static const struct { const char *tok; uint16_t code; } glyph_alias[] = {
    {"”", 25}, {"’", 58}, {"‘", 58}, {"–", 59}, {"{!?}", 28},
};

typedef struct { char *tok; uint16_t code; } CharMap;
static CharMap *charmap;                         /* the mods' CHARS.txt */
static int ncharmap;

typedef struct { int kind; uint8_t *d; long n; char what[600]; } TextFile;   /* kind: 0 rooms, 1 game, 2 items, 3 chars, 4 UI, 5 list */
static TextFile *textfiles;
static int ntextfiles;

/* Codes to text (until the end code), the control codes as tags. */
static void text_decode(const uint8_t *p, uint32_t n, char *out, size_t cap, int *ended) {
    size_t o = 0;
    out[0] = 0;
    *ended = 0;
#define PUT(...) do { if (o < cap) o += (size_t)snprintf(out + o, cap - o, __VA_ARGS__); } while (0)
    for (uint32_t k = 0; k + 1 < n; k += 2) {
        uint16_t c = rd16(p + k), next = k + 3 < n ? rd16(p + k + 2) : 0xFFFF;
        if (c == 0xFFFF) { *ended = 1; break; }
        if (c == 0xFFFE) PUT(next == 0xFFFF ? "{n}" : "\n");
        else if (c == 0 && (next == 0xFFFE || next == 0xFFFF)) PUT("{sp}");
        else if (c < 144 && glyph_name[c]) PUT("%s", glyph_name[c]);
        else if (c < 0x8000) PUT("{g%u}", c);
        else if (c == 0xFF00) PUT("{more}");
        else if (c == 0xFF01) PUT("{more2}");
        else if (c == 0xFE00) PUT("{stop}");
        else if (c >> 8 == 0xFE) PUT("{wait %u}", c & 0xFF);
        else if (c >> 8 == 0xFC) PUT("{color %u}", c & 0xFF);
        else if (c >> 8 == 0xFB) PUT("{speed %u}", c & 0xFF);
        else if (c >= 0xFD00 && c <= 0xFD02) PUT("{%s}", c == 0xFD00 ? "choice1" : c == 0xFD01 ? "choice2" : "choiced");
        else PUT("{x%04X}", c);
    }
    if (!*ended) PUT("{noend}");
#undef PUT
}

/* Text to codes (the end code added unless {noend}); the number, or -1 and why. */
static int text_encode(const char *s, uint16_t *out, int cap, char *why, size_t wn) {
    int n = 0, end = 1;
    while (*s) {
        uint32_t c = 0xFFFFFFFFu;
        size_t used = 0;
        if (*s == '\r') { s++; continue; }
        if (*s == '\n') { c = 0xFFFE; used = 1; }
        if (c == 0xFFFFFFFFu) for (int i = 0; i < ncharmap; i++) {   /* longest match; equal length keeps the later mod's first entry */
            size_t l = strlen(charmap[i].tok);
            if (l > used && !strncmp(s, charmap[i].tok, l)) { c = charmap[i].code; used = l; }
        }
        if (c == 0xFFFFFFFFu && *s == '{') {
            const char *e = strchr(s, '}');
            unsigned a = 0;
            char t[24] = "";
            if (e && e - s < 20) {
                memcpy(t, s + 1, (size_t)(e - s - 1));
                t[e - s - 1] = 0;
                used = (size_t)(e - s + 1);
                if (!strcmp(t, "n")) c = 0xFFFE;
                else if (!strcmp(t, "sp")) c = 0;
                else if (!strcmp(t, "more")) c = 0xFF00;
                else if (!strcmp(t, "more2")) c = 0xFF01;
                else if (!strcmp(t, "stop")) c = 0xFE00;
                else if (!strcmp(t, "choice1")) c = 0xFD00;
                else if (!strcmp(t, "choice2")) c = 0xFD01;
                else if (!strcmp(t, "choiced")) c = 0xFD02;
                else if (!strcmp(t, "noend")) { end = 0; s += used; continue; }
                else if (sscanf(t, "wait %u", &a) == 1 && a < 256) c = 0xFE00 | a;
                else if (sscanf(t, "color %u", &a) == 1 && a < 256) c = 0xFC00 | a;
                else if (sscanf(t, "speed %u", &a) == 1 && a < 256) c = 0xFB00 | a;
                else if (t[0] == 'g' && sscanf(t + 1, "%u", &a) == 1 && a < 0x8000) c = a;
                else if (t[0] == 'x' && sscanf(t + 1, "%x", &a) == 1 && a < 0x10000) c = a;
            }
        }
        if (c == 0xFFFFFFFFu) {                  /* the font's own glyphs, longest name first */
            for (int g = 0; g < 144; g++) {
                size_t l = glyph_name[g] ? strlen(glyph_name[g]) : 0;
                if (l > used && !strncmp(s, glyph_name[g], l)) { c = (uint32_t)g; used = l; }
            }
            for (size_t i = 0; i < sizeof glyph_alias / sizeof glyph_alias[0]; i++) {
                size_t l = strlen(glyph_alias[i].tok);
                if (l > used && !strncmp(s, glyph_alias[i].tok, l)) { c = glyph_alias[i].code; used = l; }
            }
        }
        if (c == 0xFFFFFFFFu) {
            size_t l = 1;
            while (l < 4 && ((unsigned char)s[l] & 0xC0) == 0x80) l++;
            snprintf(why, wn, "\"%.*s\" isn't a letter of the font (add it to text/CHARS.txt) or a tag", *s == '{' ? 12 : (int)l, s);
            return -1;
        }
        if (n + 2 > cap) { snprintf(why, wn, "too long"); return -1; }
        out[n++] = (uint16_t)c;
        s += used;
    }
    if (end) out[n++] = 0xFFFF;
    return n;
}

/* A text file's entries, in order: the "@" line's name and the text after
 * it (in *buf, which the caller frees with the entries). */
typedef struct { char name[32]; char *text; } TextEntry;
static int text_entries(const uint8_t *d, long n, TextEntry **out, char **buf) {
    char *s = malloc((size_t)n + 1);
    memcpy(s, d, (size_t)n);
    s[n] = 0;
    if (!strncmp(s, "\xEF\xBB\xBF", 3)) memmove(s, s + 3, (size_t)n - 2);   /* (a BOM) */
    int cnt = 0, cap = 0;
    TextEntry *e = NULL;
    for (char *p = s; *p;) {
        char *eol = strchr(p, '\n');
        char *next = eol ? eol + 1 : p + strlen(p);
        if (*p == '@') {
            if (cnt) {                           /* the one before ends here (its last line break isn't part of it) */
                char *t = e[cnt - 1].text;
                *(p - 1) = 0;
                if (p - 2 >= t && *(p - 2) == '\r') *(p - 2) = 0;
            }
            if (cnt == cap) { cap = cap ? cap * 2 : 256; e = realloc(e, (size_t)cap * sizeof *e); }
            size_t l = strcspn(p + 1, " \t\r\n");
            snprintf(e[cnt].name, sizeof e[cnt].name, "%.*s", (int)(l < 31 ? l : 31), p + 1);
            e[cnt].text = next;
            cnt++;
        }
        p = next;
    }
    if (cnt) {                                   /* the last: no line breaks at the file's end */
        char *t = e[cnt - 1].text;
        size_t l = strlen(t);
        while (l && (t[l - 1] == '\n' || t[l - 1] == '\r')) t[--l] = 0;
    }
    *out = e;
    *buf = s;
    return cnt;
}

static int text_apply(const char *mod, const char *rel, const char *up, const char *path) {
    int kind = !strcmp(up, "TEXT/ROOMS.TXT") ? 0 : !strcmp(up, "TEXT/GAME.TXT") ? 1 : !strcmp(up, "TEXT/ITEMS.TXT") ? 2 :
               !strcmp(up, "TEXT/CHARS.TXT") ? 3 : !strcmp(up, "TEXT/UI.TXT") ? 4 : !strcmp(up, "TEXT/LIST.TXT") ? 5 : -1;
    if (kind < 0) return 0;
    long n = 0;
    uint8_t *d = load(path, &n);
    TextFile *f = d ? realloc(textfiles, (size_t)(ntextfiles + 1) * sizeof *textfiles) : NULL;
    if (!f) { free(d); note("mods: %s/%s: couldn't read it", mod, rel); return 1; }
    textfiles = f;
    f = &textfiles[ntextfiles++];
    f->kind = kind;
    f->d = d;
    f->n = n;
    snprintf(f->what, sizeof f->what, "%s/%s", mod, rel);
    nused++;
    return 1;
}

/* The rooms' texts: ROOMENV.DAT's table of blocks (0 if not found). */
static uint32_t roomenv_blocks(Touched *test, Touched *t, uint32_t *v) {
    uint32_t nb = 0, tab = ROOMENV_TABLE - TEST_BASE;
    for (; test && t && nb < 1023; nb++) {
        v[nb] = rd32(test->data + tab + 4 * nb);
        if (nb && v[nb] < v[nb - 1]) return 0;
        if (nb && v[nb] == t->nsec) return nb;
    }
    return 0;
}

/* A block's text part: where (in the block), how big, how many texts; 0 if it has none. */
static int roomenv_part(const uint8_t *d, uint32_t at, uint32_t len, uint32_t *po, uint32_t *pl, uint32_t *cnt) {
    if (len < 28 || rd32(d + at) != 6) return 0;
    uint32_t a = rd32(d + at + 4 + 4 * 4), b = rd32(d + at + 4 + 4 * 5);
    if (a < 28 || b < a || b > len || b - a < 4) return 0;
    uint32_t c = rd16(d + at + a);
    if (c > 255 || 4 + 4 * c > b - a) return 0;
    *po = a;
    *pl = b - a;
    *cnt = c;
    return c > 0;
}

/* Text n of a part: its offset in the part and how far it may run. */
static void roomenv_text(const uint8_t *part, uint32_t pl, uint32_t cnt, uint32_t n, uint32_t *o, uint32_t *end) {
    *o = (uint32_t)((int16_t)rd16(part + 4 + 4 * n) / 2 * 2);
    *end = pl;
    for (uint32_t k = 0; k < cnt; k++) {
        uint32_t ok = (uint32_t)((int16_t)rd16(part + 4 + 4 * k) / 2 * 2);
        if (ok > *o && ok < *end) *end = ok;
    }
    if (*o > pl) *o = *end = pl;
}

/* The items' descriptions: B/BIGTIM.BIN's blocks (a picture, then the text), found by their headers. */
static int bigtim_blocks(const Touched *t, uint32_t *at, uint32_t *end, int max) {
    int n = 0;
    for (uint32_t o = 0; t && o + 16 <= t->nsec * 2048 && n < max; o += 2048) {
        uint32_t c = rd32(t->data + o);
        if ((c != 2 && c != 3) || rd32(t->data + o + 4) != 4 + 4 * c) continue;
        if (n) end[n - 1] = o;
        at[n++] = o;
    }
    if (n) end[n - 1] = t->nsec * 2048;
    return n;
}

/* The items' names...: TEST.BIN's strings from GAME_TEXTS, each at a 4-byte boundary; their offsets. */
static int game_texts(const uint8_t *test, uint32_t *off, int max) {
    int n = 0;
    for (uint32_t o = GAME_TEXTS; o < GAME_TEXTS_END && n < max;) {
        off[n++] = o;
        while (o < GAME_TEXTS_END && rd16(test + o) != 0xFFFF) o += 2;
        o = (o + 2 + 3) & ~3u;
    }
    return n;
}

/* Past the game's 2 MB of RAM: what mods put there (rewritten when a save is loaded). */
static uint32_t ext_used;
static uint32_t ext_alloc(uint32_t n) {
    n = (n + 3) & ~3u;
    if (RAM_GAME + ext_used + n > RAM_SIZE) return 0;
    uint32_t a = 0x80000000u + RAM_GAME + ext_used;
    ext_used += n;
    g_ram_ext_used = 1;
    return a;
}

/* These inline status labels have no pointer table to relocate. Only
 * the native display consumers use this optional resource lookup; no
 * instructions, state transitions or original strings are overwritten. */
static struct { const char *name; uint32_t original, replacement; int max; } ui_texts[] = {
    {"RETURN", 0x800CF2ECu, 0, 28}, {"NO_ITEM", 0x800CF300u, 0, 28}, {"FULLY_LOADED", 0x800CF310u, 0, 28},
    /* the doors' messages (TEST.BIN, room_change.c n_op_Door) */
    {"DOOR_LOCKED", 0x800A2ECCu, 0, 120}, {"DOOR_LOCKED_OTHER_SIDE", 0x800A2E84u, 0, 120},
    {"DOOR_USED", 0x800A2EF4u, 0, 60}, {"DOOR_UNLOCKED", 0x800A2F14u, 0, 120},
};
uint32_t modfs_ui_text(uint32_t original) {
    for (size_t i = 0; i < sizeof ui_texts / sizeof ui_texts[0]; i++)
        if (ui_texts[i].original == original && ui_texts[i].replacement) return ui_texts[i].replacement;
    return original;
}
static void texts_ui_done(void) {
    for (int i = 0; i < ntextfiles; i++) {
        if (textfiles[i].kind != 4) continue;
        TextEntry *entries = NULL;
        char *buf = NULL, why[160];
        int count = text_entries(textfiles[i].d, textfiles[i].n, &entries, &buf);
        for (int k = 0; k < count; k++) {
            size_t u = 0;
            for (; u < sizeof ui_texts / sizeof ui_texts[0]; u++) if (!strcmp(entries[k].name, ui_texts[u].name)) break;
            if (u == sizeof ui_texts / sizeof ui_texts[0]) { note("mods: %s: @%s: unknown UI label: not used", textfiles[i].what, entries[k].name); continue; }
            uint16_t codes[128];
            int n = text_encode(entries[k].text, codes, 128, why, sizeof why);
            if (n >= 0 && (n < 1 || n > ui_texts[u].max || codes[n - 1] != 0xFFFF))
                { snprintf(why, sizeof why, "this label needs an end tag and at most %d codes", ui_texts[u].max - 1); n = -1; }
            if (n < 0) { note("mods: %s: @%s: %s: not used", textfiles[i].what, entries[k].name, why); continue; }
            uint32_t addr = ext_alloc((uint32_t)n * 2);
            if (!addr) { note("mods: %s: @%s: no free mod RAM: not used", textfiles[i].what, entries[k].name); continue; }
            for (int c = 0; c < n; c++) wr16(ram_ptr(addr) + c * 2, codes[c]);
            ui_texts[u].replacement = addr;
            note("mods: %s: @%s: UI label in mod RAM", textfiles[i].what, entries[k].name);
        }
        free(entries); free(buf);
    }
}

/* Optional compact names for the 144-pixel inventory list (18 fixed
 * 8-pixel glyphs, status_screen.c/message.c). Full names remain intact. */
static struct { uint32_t original, current, display; } list_texts[45];
uint32_t modfs_list_text(uint32_t original) {
    for (size_t i = 0; i < sizeof list_texts / sizeof list_texts[0]; i++)
        /* Only the current pointer table is authoritative. An old address
         * can belong to a different name after in-place repacking. */
        if (list_texts[i].display && original == list_texts[i].current) return list_texts[i].display;
    return original;
}
static void texts_list_done(void) {
    for (int i = 0; i < ntextfiles; i++) {
        if (textfiles[i].kind != 5) continue;
        TextEntry *entries = NULL;
        char *buf = NULL, why[160];
        int count = text_entries(textfiles[i].d, textfiles[i].n, &entries, &buf);
        for (int k = 0; k < count; k++) {
            unsigned x;
            char tail;
            if (sscanf(entries[k].name, "%u%c", &x, &tail) != 1 || x >= 45 || !list_texts[x].original) {
                note("mods: %s: @%s: no such inventory name (0 to 44): not used", textfiles[i].what, entries[k].name); continue;
            }
            uint16_t codes[64];
            int n = text_encode(entries[k].text, codes, 64, why, sizeof why), glyphs = 0, bad = 0;
            for (int c = 0; c + 1 < n; c++) {
                if (codes[c] < 0x8000) glyphs++;
                else if ((codes[c] & 0xFF00) != 0xFC00) bad = 1;
            }
            if (n >= 0 && (n < 1 || n > 28 || codes[n - 1] != 0xFFFF || glyphs > 18 || bad)) {
                snprintf(why, sizeof why, "compact names need at most 18 glyphs, colours and an end tag"); n = -1;
            }
            if (n < 0) { note("mods: %s: @%s: %s: not used", textfiles[i].what, entries[k].name, why); continue; }
            uint32_t addr = ext_alloc((uint32_t)n * 2);
            if (!addr) { note("mods: %s: @%s: no free mod RAM: not used", textfiles[i].what, entries[k].name); continue; }
            for (int c = 0; c < n; c++) wr16(ram_ptr(addr) + c * 2, codes[c]);
            list_texts[x].display = addr;
            note("mods: %s: @%s: compact inventory name in mod RAM", textfiles[i].what, entries[k].name);
        }
        free(entries); free(buf);
    }
}

static void texts_done(void) {
    if (!ntextfiles) return;
    /* the mods' characters first */
    for (int i = 0; i < ntextfiles; i++) {
        if (textfiles[i].kind != 3) continue;
        char *s = malloc((size_t)textfiles[i].n + 1);
        memcpy(s, textfiles[i].d, (size_t)textfiles[i].n);
        s[textfiles[i].n] = 0;
        int got = 0;
        for (char *line = strtok(s, "\r\n"); line; line = strtok(NULL, "\r\n")) {
            char tok[32];
            unsigned code;
            if (line[0] == '#' || sscanf(line, "%31s %u", tok, &code) != 2 || code >= 0x8000) continue;
            charmap = realloc(charmap, (size_t)(ncharmap + 1) * sizeof *charmap);
            memmove(charmap + 1, charmap, (size_t)ncharmap * sizeof *charmap);   /* (a later mod's first) */
            charmap[0].tok = strdup(tok);
            charmap[0].code = (uint16_t)code;
            ncharmap++;
            got++;
        }
        free(s);
        note("mods: %s: %d characters", textfiles[i].what, got);
    }
    texts_ui_done();
    Touched *test = touch(disc_index("TEST.BIN"));
    static uint16_t codes[4096];
    char why[160];
    /* the rooms': each block's texts laid out again when any changed */
    {
        int fi = disc_index("ROOMENV.DAT");
        Touched *t = touch(fi);
        static uint32_t v[1024];
        uint32_t nb = roomenv_blocks(test, t, v);
        static struct { uint16_t *c; int n; } *set;   /* per block and text: the mods' codes */
        set = calloc((size_t)nb * 256, sizeof *set);
        int changed = 0, bad = 0, entries = 0;
        for (int i = 0; set && i < ntextfiles; i++) {
            if (textfiles[i].kind != 0) continue;
            TextEntry *e = NULL;
            char *buf = NULL;
            int ne = text_entries(textfiles[i].d, textfiles[i].n, &e, &buf);
            for (int k = 0; k < ne; k++) {
                unsigned b, x;
                uint32_t po, pl, cnt;
                if (sscanf(e[k].name, "%u.%u", &b, &x) != 2 || b >= nb ||
                    !roomenv_part(t->data, v[b] * 2048, (v[b + 1] - v[b]) * 2048, &po, &pl, &cnt) || x >= cnt) {
                    note("mods: %s: @%s: no such text (ROOMENV.DAT block.text): not used", textfiles[i].what, e[k].name);
                    bad++;
                    continue;
                }
                int n = text_encode(e[k].text, codes, 4096, why, sizeof why);
                if (n < 0) { note("mods: %s: @%s: %s: not used", textfiles[i].what, e[k].name, why); bad++; continue; }
                free(set[b * 256 + x].c);
                set[b * 256 + x].c = malloc((size_t)n * 2);
                memcpy(set[b * 256 + x].c, codes, (size_t)n * 2);
                set[b * 256 + x].n = n;
                entries++;
            }
            free(buf);
            free(e);
        }
        for (uint32_t b = 0; set && b < nb; b++) {
            uint32_t at = v[b] * 2048, po, pl, cnt;
            if (!roomenv_part(t->data, at, (v[b + 1] - v[b]) * 2048, &po, &pl, &cnt)) continue;
            const uint8_t *part = t->data + at + po;
            int any = 0;
            for (uint32_t x = 0; x < cnt && !any; x++) {
                if (!set[b * 256 + x].c) continue;
                uint32_t o, end;
                roomenv_text(part, pl, cnt, x, &o, &end);
                uint32_t len = 0;
                while (o + len + 2 <= end && rd16(part + o + len) != 0xFFFF) len += 2;
                int hasend = o + len + 2 <= end;
                if (set[b * 256 + x].n * 2u != len + (hasend ? 2u : 0u) || memcmp(set[b * 256 + x].c, part + o, len)) any = 1;
            }
            if (!any) continue;
            uint32_t cap = 4 + 4 * cnt;
            for (uint32_t x = 0; x < cnt; x++) {
                uint32_t o, end;
                roomenv_text(part, pl, cnt, x, &o, &end);
                cap += set[b * 256 + x].c ? (uint32_t)set[b * 256 + x].n * 2 : end - o;
            }
            /* The original reader sign-extends each 16-bit byte offset. */
            if (cap > 32764) {
                note("mods: ROOMENV.DAT block %u: text offsets exceed the original signed 16-bit range: not used", b);
                continue;
            }
            uint8_t *np = calloc(1, cap + 8);
            if (!np) { note("mods: ROOMENV.DAT block %u: couldn't allocate the texts: not used", b); continue; }
            memcpy(np, part, 4 + 4 * cnt);
            uint32_t w = 4 + 4 * cnt;
            for (uint32_t x = 0; x < cnt; x++) {
                uint32_t o, end;
                roomenv_text(part, pl, cnt, x, &o, &end);
                wr16(np + 4 + 4 * x, (uint16_t)w);
                if (set[b * 256 + x].c) {
                    memcpy(np + w, set[b * 256 + x].c, (size_t)set[b * 256 + x].n * 2);
                    w += (uint32_t)set[b * 256 + x].n * 2;
                } else {
                    memcpy(np + w, part + o, end - o);
                    w += end - o;
                }
            }
            w = (w + 3) & ~3u;
            if (w <= pl) {
                memset(t->data + at + po, 0, pl);
                memcpy(t->data + at + po, np, w);
                free(np);
            } else if (ngrows < (int)(sizeof grows / sizeof grows[0])) {
                Grow *g = &grows[ngrows++];   /* the block laid out again (grow_all), its table changed */
                *g = (Grow){fi, at + po, w, pl, 0, np, ""};
                snprintf(g->what, sizeof g->what, "the texts of ROOMENV.DAT block %u", b);
            } else free(np);
            changed++;
        }
        for (uint32_t k = 0; set && k < nb * 256; k++) free(set[k].c);
        free(set);
        if (entries || bad) note("mods: rooms' texts: %d given, %d blocks changed%s", entries, changed, bad ? "; some skipped (above)" : "");
    }
    /* the items' descriptions: in their place, up to the block's end */
    {
        Touched *t = touch(disc_index("B/BIGTIM.BIN"));
        uint32_t at[64], end[64];
        int nb = bigtim_blocks(t, at, end, 64), done = 0;
        for (int i = 0; i < ntextfiles; i++) {
            if (textfiles[i].kind != 2) continue;
            TextEntry *e = NULL;
            char *buf = NULL;
            int ne = text_entries(textfiles[i].d, textfiles[i].n, &e, &buf);
            for (int k = 0; k < ne; k++) {
                unsigned b;
                if (sscanf(e[k].name, "%u", &b) != 1 || (int)b >= nb) {
                    note("mods: %s: @%s: no such description (0 to %d): not used", textfiles[i].what, e[k].name, nb - 1);
                    continue;
                }
                int n = text_encode(e[k].text, codes, 4096, why, sizeof why);
                uint32_t o = at[b] + rd32(t->data + at[b] + 8);
                if (n >= 0 && o + (uint32_t)n * 2 > end[b]) { snprintf(why, sizeof why, "%u letters more than there's room for", (unsigned)(o + (uint32_t)n * 2 - end[b]) / 2); n = -1; }
                if (n < 0) { note("mods: %s: @%s: %s: not used", textfiles[i].what, e[k].name, why); continue; }
                memset(t->data + o, 0, end[b] - o);
                memcpy(t->data + o, codes, (size_t)n * 2);
                done++;
            }
            free(buf);
            free(e);
        }
        if (done) note("mods: items' descriptions: %d given", done);
    }
    /* the items' names...: in their place if they fit, else past the game's 2 MB; their pointers changed */
    {
        uint32_t off[128];
        int nt = test ? game_texts(test->data, off, 128) : 0, given = 0;
        for (int x = 0; x < nt && x < 45; x++) list_texts[x].original = list_texts[x].current = TEST_BASE + off[x];
        static uint16_t *mine[128];
        static int nmine[128];
        for (int i = 0; i < ntextfiles; i++) {
            if (textfiles[i].kind != 1) continue;
            TextEntry *e = NULL;
            char *buf = NULL;
            int ne = text_entries(textfiles[i].d, textfiles[i].n, &e, &buf);
            for (int k = 0; k < ne; k++) {
                unsigned x;
                if (sscanf(e[k].name, "%u", &x) != 1 || (int)x >= nt) {
                    note("mods: %s: @%s: no such text (0 to %d): not used", textfiles[i].what, e[k].name, nt - 1);
                    continue;
                }
                int n = text_encode(e[k].text, codes, 4096, why, sizeof why);
                /* ItemNames copies into 60-byte rows, adding two colour
                 * codes and a line end for the selected row. A missing
                 * terminator or more than 27 codes would overwrite it. */
                if (n >= 0 && x < 45 && (n < 1 || n > 28 || codes[n - 1] != 0xFFFF)) {
                    snprintf(why, sizeof why, "an inventory name needs an end tag and at most 27 codes (the original row capacity)"); n = -1;
                }
                if (n < 0) { note("mods: %s: @%s: %s: not used", textfiles[i].what, e[k].name, why); continue; }
                if (x < 45 && n > 19) note("mods: %s: @%s: long inventory name: check the list box layout in game", textfiles[i].what, e[k].name);
                free(mine[x]);
                mine[x] = malloc((size_t)n * 2);
                memcpy(mine[x], codes, (size_t)n * 2);
                nmine[x] = n;
                given++;
            }
            free(buf);
            free(e);
        }
        int any = 0;
        uint32_t total = 0;
        for (int x = 0; x < nt; x++) {
            uint32_t len = 0;
            while (rd16(test->data + off[x] + len) != 0xFFFF) len += 2;
            if (mine[x] && ((uint32_t)nmine[x] * 2 != len + 2 || memcmp(mine[x], test->data + off[x], len))) any = 1;
            total += ((mine[x] ? (uint32_t)nmine[x] * 2 : len + 2) + 3) & ~3u;
        }
        if (any) {
            uint8_t *buf = calloc(1, total + 4);
            uint32_t newoff[128], w = 0;
            for (int x = 0; x < nt; x++) {
                uint32_t len = 0;
                while (rd16(test->data + off[x] + len) != 0xFFFF) len += 2;
                newoff[x] = w;
                if (mine[x]) memcpy(buf + w, mine[x], (size_t)nmine[x] * 2), w += (uint32_t)nmine[x] * 2;
                else memcpy(buf + w, test->data + off[x], len + 2), w += len + 2;
                w = (w + 3) & ~3u;
            }
            uint32_t base = 0;
            const char *where = "in their place";
            if (total <= GAME_TEXTS_END - GAME_TEXTS) {
                memset(test->data + GAME_TEXTS, 0, GAME_TEXTS_END - GAME_TEXTS);
                memcpy(test->data + GAME_TEXTS, buf, total);
                base = TEST_BASE + GAME_TEXTS;
            } else if ((base = ext_alloc(total))) {
                memcpy(ram_ptr(base), buf, total);
                where = "past the game's 2 MB of RAM";
            }
            if (base) {
                uint8_t was[GAME_PTRS_END - GAME_TEXTS_END];
                memcpy(was, test->data + GAME_TEXTS_END, sizeof was);
                for (uint32_t p = GAME_TEXTS_END; p + 4 <= GAME_PTRS_END; p += 4)
                    for (int x = 0; x < nt; x++)
                        if (rd32(was + p - GAME_TEXTS_END) == TEST_BASE + off[x]) wr32(test->data + p, base + newoff[x]);
                ram_fix(TEST_BASE + GAME_TEXTS_END, was, test->data + GAME_TEXTS_END, sizeof was);
                for (int x = 0; x < nt && x < 45; x++) list_texts[x].current = base + newoff[x];
                note("mods: items' names and the inventory's lines: %d given, %s (%u bytes)", given, where, total);
            } else
                note("mods: items' names: not enough memory for them: not used");
            free(buf);
        } else if (given)
            note("mods: items' names and the inventory's lines: %d given, as on the disc", given);
        for (int x = 0; x < nt; x++) { free(mine[x]); mine[x] = NULL; }
    }
    texts_list_done();
    for (int i = 0; i < ntextfiles; i++) free(textfiles[i].d);
    free(textfiles);
    textfiles = NULL;
    ntextfiles = 0;
}

/* The originals as text files, with the font's characters. */
static int texts_extract(void) {
    int files = 0;
    size_t cap = 4 << 20, o;
    char *out = malloc(cap), line[8192];
    int ended;
    Touched *test = touch(disc_index("TEST.BIN"));
#define ADD(...) do { if (o < cap) o += (size_t)snprintf(out + o, cap - o, __VA_ARGS__); } while (0)
    {
        Touched *t = touch(disc_index("ROOMENV.DAT"));
        static uint32_t v[1024];
        uint32_t nb = roomenv_blocks(test, t, v);
        o = 0;
        ADD("# The rooms' messages (ROOMENV.DAT: block = stage * 30 + room). Change the text after\n"
            "# an @ line, keep the line itself. A line break is a line break in the game; {more}\n"
            "# waits for a button, {color n} changes colour; see MODDING.md. Texts left out stay.\n");
        for (uint32_t b = 0; b < nb; b++) {
            uint32_t at = v[b] * 2048, po, pl, cnt;
            if (!roomenv_part(t->data, at, (v[b + 1] - v[b]) * 2048, &po, &pl, &cnt)) continue;
            for (uint32_t x = 0; x < cnt; x++) {
                uint32_t to, end;
                roomenv_text(t->data + at + po, pl, cnt, x, &to, &end);
                text_decode(t->data + at + po + to, end - to, line, sizeof line, &ended);
                ADD("@%u.%u\n%s\n", b, x, line);
            }
        }
        files += write_out("text/ROOMS.txt", (const uint8_t *)out, o);
    }
    {
        uint32_t off[128];
        int nt = test ? game_texts(test->data, off, 128) : 0;
        o = 0;
        ADD("# Item names and inventory lines (TEST.BIN). See MODDING.md for row capacity;\n# text/LIST.txt supplies optional shorter names for the inventory only.\n");
        for (int x = 0; x < nt; x++) {
            text_decode(test->data + off[x], GAME_TEXTS_END - off[x], line, sizeof line, &ended);
            ADD("@%d\n%s\n", x, line);
        }
        files += write_out("text/GAME.txt", (const uint8_t *)out, o);
    }
    {
        Touched *t = touch(disc_index("B/BIGTIM.BIN"));
        uint32_t at[64], end[64];
        int nb = bigtim_blocks(t, at, end, 64);
        o = 0;
        ADD("# The items' descriptions (B/BIGTIM.BIN, with the item's picture BIGTIM/n.png).\n");
        for (int b = 0; b < nb; b++) {
            uint32_t to = at[b] + rd32(t->data + at[b] + 8);
            text_decode(t->data + to, end[b] - to, line, sizeof line, &ended);
            ADD("@%d\n%s\n", b, line);
        }
        files += write_out("text/ITEMS.txt", (const uint8_t *)out, o);
    }
    o = 0;
    ADD("# The font's glyphs (FONT/FONT188.png...: 18 a row, 8x14 each, numbered from 0 at the top\n"
        "# left): a character and its glyph. Draw a new letter in a free cell (128 to 143) and add\n"
        "# a line for it, e.g. \"ì 128\", in your mod's text/CHARS.txt.\n");
    for (int g = 0; g < 144; g++)
        if (glyph_name[g]) ADD("%s %d\n", g == 0 ? "{sp}" : glyph_name[g], g);
    files += write_out("text/CHARS.txt", (const uint8_t *)out, o);
    const char *compact = "# Optional shorter inventory names. Use GAME identifiers 0-44 and at most\n# 18 visible glyphs; leave this empty to keep full names. See MODDING.md.\n";
    files += write_out("text/LIST.txt", (const uint8_t *)compact, strlen(compact));
#undef ADD
    free(out);
    return files;
}

/* ------------------------------------------------------------ game data */

/* Numbers of the game a mod gives as text (decimal; # starts a comment):
 *   data/COMBAT.txt   in TEST.BIN (combat.c, enemy.c): per weapon (its item id) its
 *                     fire rate (the game doubles it), "rate <id> <n>"; per shot
 *                     kind (the weapon, grenades by the ammo loaded) its
 *                     base damage, three values, and damage class,
 *                     "shot <kind> <d1> <d2> <d3> <class>"; per enemy type
 *                     the damage factor in percent for each of the five
 *                     classes, "factor <type> <c0> <c1> <c2> <c3> <c4>";
 *                     the hit points of the small enemies (types 0-10) by
 *                     their placement's hit points value, "hp <n> <points>"
 *                     (n 0-14), and, for a value of 15 and up, the
 *                     cumulative chances (percent) of each, "chance <n> <p>";
 *                     the bigger enemies' (11-33) four hit points (their
 *                     placement's value 0-3; 4 and up one of them at
 *                     random), "enemy <type> <h0> <h1> <h2> <h3>", and every
 *                     enemy program's body-part factors, a row per shot
 *                     kind, "part <type> <kind> <p0> ... <p9>": in the
 *                     program's own table (+0x840, enemy.c and combat.c)
 *                     in ENEMPROG.BIN, both of its copies (the two
 *                     program slots), from the next time it's loaded;
 *   data/ROOMS.txt    a room's enemies and pickups (ROOMENV.DAT: block =
 *                     stage * 30 + room, room_loader.c), "@<block>" then
 *                     "enemy <type> <flag> <x> <y> <z> <yaw> <pose>
 *                     <animation> <hit points value>" and "item <type> <flag> <x> <y>
 *                     <z> <r0> <r1> <r2> <extra>"; a room given lists all
 *                     of them (others stay). The game makes room for at
 *                     most 7 objects (enemies and pickups) in a room
 *                     loaded at once, 10 at all: no more than that, or the
 *                     room's own number if it has more than 7.
 * A save made before the mod gets the weapons' numbers when it's loaded. */
#define W_RATE    0x800A3CB0u                    /* u32 x 11 */
#define W_SHOT    0x800A3CDCu                    /* s16 x 3 per kind, 11 kinds */
#define W_CLASS   0x800A3D70u                    /* s16 per kind, 12 */
#define W_FACTOR  0x800A3D88u                    /* s16 x 5 per enemy type, 34 */
#define W_HP      0x800A23ACu                    /* s16 x 15: small enemies' hit points (enemy.c) */
#define W_CHANCE  0x800A23CCu                    /* s32 x 15: cumulative chances, percent */
#define ITEM_NAMES 0x800A6D5Cu                   /* the items' names (pointers) */

#define EN_PROGS  0x800A31C4u                    /* ENEMPROG.BIN's block table: type, type + 0x28 (enemy_spawn.c) */
/* Each enemy program's table (+0x840): s16 x 12 (2-5 the hit points of
 * types 11 and up, n_enemy_AttackInfo), then s16 x 10 body parts per
 * shot kind, 11 kinds (n_enemy_HitFactor). Where it is in the program's
 * block (the same in both slots); the block is the type's, or the one
 * whose program it shares. */
#define EN_TABLE_N (12 + 110)
static const struct { uint8_t type, block; uint16_t off; const char *name; } enemy_tables[] = {
    {0, 0, 0x5728, "zombies (types 0-10)"}, {11, 11, 0x3FA4, "Cerberus"}, {12, 12, 0x4208, "crows"},
    {13, 13, 0x7368, "licker"}, {14, 13, 0x745C, "licker (type 14)"}, {15, 15, 0x2CCC, "giant cockroach"},
    {16, 16, 0x5938, "giant spider (16 and 17)"}, {18, 18, 0x1E00, "alligator"}, {19, 19, 0x27A8, ""},
    {20, 20, 0x27A8, ""}, {22, 22, 0x58F8, "Tyrant (22 and 23)"}, {26, 26, 0x251C, "giant moth"},
    {28, 28, 0x58DC, "Hunter"}, {30, 30, 0x61AC, "Lott and Lily"}, {31, 31, 0x3154, ""},
    {32, 32, 0x31DC, "final boss"}, {33, 33, 0x4620, "final boss, second form"},
};
/* The table's two copies in ENEMPROG.BIN (byte offsets), if they look like one. */
static int enemy_table_at(const Touched *test, const Touched *ep, int k, uint32_t at[2]) {
    if (!test || !ep) return 0;
    for (int slot = 0; slot < 2; slot++) {
        uint32_t b = enemy_tables[k].block + (slot ? 0x28u : 0), t = EN_PROGS - TEST_BASE + 4 * b;
        if (t + 8 > test->nsec * 2048) return 0;
        uint32_t s0 = rd32(test->data + t), s1 = rd32(test->data + t + 4);
        uint64_t o = (uint64_t)s0 * 2048 + enemy_tables[k].off;
        if (s1 <= s0 || s1 > ep->nsec || o + 2 * EN_TABLE_N > (uint64_t)s1 * 2048) return 0;
        for (int i = 2; i < 6; i++) if ((int16_t)rd16(ep->data + o + 2 * i) <= 0) return 0;
        for (int i = 12; i < EN_TABLE_N; i++) if ((int16_t)rd16(ep->data + o + 2 * i) < 0) return 0;
        at[slot] = (uint32_t)o;
    }
    return 1;
}
static int enemy_table(long type) {
    for (int k = 0; k < (int)(sizeof enemy_tables / sizeof enemy_tables[0]); k++) if (enemy_tables[k].type == type) return k;
    return -1;
}

typedef struct { int kind; uint8_t *d; long n; char what[600]; } DataFile;   /* kind: 0 combat, 1 rooms */
static DataFile *datafiles;
static int ndatafiles;

static int data_apply(const char *mod, const char *rel, const char *up, const char *path) {
    int kind = !strcmp(up, "DATA/COMBAT.TXT") ? 0 : !strcmp(up, "DATA/ROOMS.TXT") ? 1 : -1;
    if (kind < 0) return 0;
    long n = 0;
    uint8_t *d = load(path, &n);
    DataFile *f = d ? realloc(datafiles, (size_t)(ndatafiles + 1) * sizeof *datafiles) : NULL;
    if (!f) { free(d); note("mods: %s/%s: couldn't read it", mod, rel); return 1; }
    datafiles = f;
    f = &datafiles[ndatafiles++];
    f->kind = kind;
    f->d = d;
    f->n = n;
    snprintf(f->what, sizeof f->what, "%s/%s", mod, rel);
    nused++;
    return 1;
}

/* An item's name (for the comments), as letters. */
static void item_name(const uint8_t *test, uint32_t id, char *out, size_t cap) {
    out[0] = 0;
    uint32_t p = id < 0x60 ? rd32(test + ITEM_NAMES - TEST_BASE + 4 * id) : 0;
    if (p < TEST_BASE || p >= TEST_BASE + 0x57000) return;
    int ended;
    text_decode(test + (p - TEST_BASE), 120, out, cap, &ended);
    for (char *c = out; *c; c++) if (*c == '\n' || *c == '{') { *c = 0; break; }
}

/* A block's part k (0 zones, 1 setup: enemies from +4, 2 pickups): where and how long. */
static int roomenv_list(const uint8_t *d, uint32_t at, uint32_t len, int k, uint32_t *po, uint32_t *pl) {
    if (len < 28 || rd32(d + at) != 6) return 0;
    uint32_t a = rd32(d + at + 4 + 4 * (uint32_t)k), b = rd32(d + at + 8 + 4 * (uint32_t)k);
    if (a < 28 || b < a || b > len) return 0;
    *po = a;
    *pl = b - a;
    return 1;
}
/* The entries of a list (0x18 bytes each) before its end mark. */
static uint32_t list_count(const uint8_t *p, uint32_t len, int setup) {
    uint32_t n = 0;
    for (uint32_t o = setup ? 4 : 0; o + 2 <= len; o += 0x18, n++) {
        uint16_t t = rd16(p + o);
        if (setup ? (t & 0xF000) == 0xF000 : t == 0xFFFF) return n;
        if (o + 0x18 > len) break;
    }
    return 0xFFFFFFFFu;                          /* (no end mark) */
}

static void data_done(void) {
    if (!ndatafiles) return;
    Touched *test = touch(disc_index("TEST.BIN"));
    if (!test) return;
    for (int i = 0; i < ndatafiles; i++) {
        DataFile *f = &datafiles[i];
        char *s = malloc((size_t)f->n + 1);
        memcpy(s, f->d, (size_t)f->n);
        s[f->n] = 0;
        if (f->kind == 0) {                      /* the weapons: in TEST.BIN's data, and the RAM of older saves */
            static uint8_t was[W_FACTOR + 340 - W_RATE], washp[W_CHANCE + 60 - W_HP];
            memcpy(was, test->data + W_RATE - TEST_BASE, sizeof was);
            memcpy(washp, test->data + W_HP - TEST_BASE, sizeof washp);
            int lines = 0, bad = 0, ln = 0;
            Touched *ep = NULL;
            for (char *line = strtok(s, "\n"); line; line = strtok(NULL, "\n")) {
                ln++;
                char *h = strchr(line, '#');
                if (h) *h = 0;
                char word[16];
                long a[14];
                int n = 0, used = 0;
                if (sscanf(line, "%15s%n", word, &used) != 1) continue;
                for (char *q = line + used, *e; n < 14; q = e) {
                    long v = strtol(q, &e, 10);
                    if (e == q) break;
                    a[n++] = v;
                }
                int got = n + 1;                 /* (the word and its numbers) */
                uint8_t *at = NULL;
                int pt = !strcmp(word, "part");
                if ((!strcmp(word, "enemy") && got == 6) || (pt && got == 13)) {   /* an enemy program's table */
                    int k = enemy_table(a[0]), ok = k >= 0 && (pt || a[0] >= 11);
                    uint32_t where[2];
                    if (ok && !ep) ep = touch(disc_index("ENEMPROG.BIN"));
                    if (ok && pt) ok = a[1] >= 0 && a[1] < 11;
                    for (int i = pt ? 2 : 1; ok && i < n; i++) ok = a[i] >= (pt ? 0 : 1) && a[i] < 32768;
                    if (ok && enemy_table_at(test, ep, k, where)) {
                        for (int slot = 0; slot < 2; slot++) {
                            uint8_t *t = ep->data + where[slot];
                            if (!pt) for (int i = 0; i < 4; i++) wr16(t + 4 + 2 * i, (uint16_t)a[1 + i]);
                            else for (int i = 0; i < 10; i++) wr16(t + 0x18 + 20 * a[1] + 2 * i, (uint16_t)a[2 + i]);
                        }
                        at = ep->data;
                    }
                }
                if (!strcmp(word, "rate") && got == 3 && a[0] >= 0 && a[0] < 11 && a[1] >= 0 && a[1] < 1000) {
                    at = test->data + W_RATE - TEST_BASE + 4 * a[0];
                    wr32(at, (uint32_t)a[1]);
                } else if (!strcmp(word, "shot") && got == 6 && a[0] >= 0 && a[0] < 11 && a[4] >= 0 && a[4] < 5) {
                    at = test->data + W_SHOT - TEST_BASE + 6 * a[0];
                    for (int k = 0; k < 3; k++) wr16(at + 2 * k, (uint16_t)(int16_t)(a[1 + k] < -32768 ? -32768 : a[1 + k] > 32767 ? 32767 : a[1 + k]));
                    wr16(test->data + W_CLASS - TEST_BASE + 2 * a[0], (uint16_t)a[4]);
                } else if (!strcmp(word, "factor") && got == 7 && a[0] >= 0 && a[0] < 34) {
                    at = test->data + W_FACTOR - TEST_BASE + 10 * a[0];
                    for (int k = 0; k < 5; k++) wr16(at + 2 * k, (uint16_t)(int16_t)(a[1 + k] < 0 ? 0 : a[1 + k] > 32767 ? 32767 : a[1 + k]));
                } else if (!strcmp(word, "hp") && got == 3 && a[0] >= 0 && a[0] < 15 && a[1] > 0 && a[1] < 32768) {
                    at = test->data + W_HP - TEST_BASE + 2 * a[0];
                    wr16(at, (uint16_t)a[1]);
                } else if (!strcmp(word, "chance") && got == 3 && a[0] >= 0 && a[0] < 15 && a[1] >= 0 && a[1] <= 100) {
                    at = test->data + W_CHANCE - TEST_BASE + 4 * a[0];
                    wr32(at, (uint32_t)a[1]);
                }
                if (at) lines++;
                else { note("mods: %s: line %d isn't one of rate/shot/factor/hp/chance/enemy/part with its numbers: not used", f->what, ln); bad++; }
            }
            ram_fix(W_RATE, was, test->data + W_RATE - TEST_BASE, sizeof was);
            ram_fix(W_HP, washp, test->data + W_HP - TEST_BASE, sizeof washp);
            note("mods: %s: %d weapon and damage numbers%s", f->what, lines, bad ? ", some lines skipped (above)" : "");
        } else {                                 /* the rooms' enemies and pickups */
            int fi = disc_index("ROOMENV.DAT");
            Touched *t = touch(fi);
            static uint32_t v[1024];
            uint32_t nb = roomenv_blocks(test, t, v);
            int rooms = 0, ln = 0;
            char *save = NULL;
            uint8_t *en = malloc(0x18 * 64), *it = malloc(0x18 * 64);
            int ne = 0, ni = 0, cur = -1, bad = 0;
            for (char *line = strtok_r(s, "\n", &save);; line = strtok_r(NULL, "\n", &save)) {
                char *h = line ? strchr(line, '#') : NULL;
                if (h) *h = 0;
                if (line) ln++;
                unsigned b;
                int head = line && sscanf(line, " @%u", &b) == 1;
                if ((head || !line) && cur >= 0 && !bad) {   /* the room before: its two lists in again */
                    uint32_t at = v[cur] * 2048, len = (v[cur + 1] - v[cur]) * 2048, so, sl, io, il;
                    roomenv_list(t->data, at, len, 1, &so, &sl);
                    roomenv_list(t->data, at, len, 2, &io, &il);
                    uint32_t oe = list_count(t->data + at + so, sl, 1), oi = list_count(t->data + at + io, il, 0);
                    uint32_t cap = oe + oi > 7 ? oe + oi : 7;
                    if (cap > 10) cap = 10;
                    if ((uint32_t)(ne + ni) > cap) note("mods: %s: @%d: %d enemies and pickups, more than the %u the game has room for there: not used", f->what, cur, ne + ni, cap);
                    else {
                        for (int part = 1; part <= 2; part++) {
                            uint32_t po = part == 1 ? so : io, pl = part == 1 ? sl : il, old = part == 1 ? 4 + oe * 0x18 : oi * 0x18, cnt = part == 1 ? (uint32_t)ne : (uint32_t)ni;
                            const uint8_t *p = t->data + at + po;
                            uint8_t *np = calloc(1, pl + cnt * 0x18 + 0x18);
                            uint32_t w = 0;
                            if (part == 1) { memcpy(np, p, 4); w = 4; }
                            memcpy(np + w, part == 1 ? en : it, cnt * 0x18);
                            w += cnt * 0x18;
                            memcpy(np + w, p + old, pl - old);       /* its end mark and what follows */
                            w += pl - old;
                            if (w == pl && !memcmp(np, p, pl)) { free(np); continue; }
                            if (w <= pl) { memset(t->data + at + po, 0, pl); memcpy(t->data + at + po, np, w); free(np); }
                            else if (ngrows < (int)(sizeof grows / sizeof grows[0])) {
                                Grow *g = &grows[ngrows++];
                                *g = (Grow){fi, at + po, w, pl, 0, np, ""};
                                snprintf(g->what, sizeof g->what, "the %s of ROOMENV.DAT block %d", part == 1 ? "enemies" : "pickups", cur);
                            } else free(np);
                        }
                        rooms++;
                    }
                }
                if (!line) break;
                if (head) {
                    cur = -1;
                    bad = 0;
                    ne = ni = 0;
                    uint32_t so, sl, io, il;
                    if (b >= nb || !roomenv_list(t->data, v[b] * 2048, (v[b + 1] - v[b]) * 2048, 1, &so, &sl) ||
                        !roomenv_list(t->data, v[b] * 2048, (v[b + 1] - v[b]) * 2048, 2, &io, &il) ||
                        list_count(t->data + v[b] * 2048 + so, sl, 1) == 0xFFFFFFFFu || list_count(t->data + v[b] * 2048 + io, il, 0) == 0xFFFFFFFFu) {
                        note("mods: %s: @%u: no such room (ROOMENV.DAT block): not used", f->what, b);
                        continue;
                    }
                    cur = (int)b;
                    continue;
                }
                char word[8];
                long a[10];
                int got = sscanf(line, "%7s %ld %ld %ld %ld %ld %ld %ld %ld %ld", word, &a[0], &a[1], &a[2], &a[3], &a[4], &a[5], &a[6], &a[7], &a[8]);
                if (got <= 0) continue;
                if (cur < 0) continue;
                int is_e = !strcmp(word, "enemy"), is_i = !strcmp(word, "item");
                if ((!is_e && !is_i) || got != 10 || (is_e ? ne : ni) >= 64 || (is_e && (a[0] & 0xF000) == 0xF000) || (is_i && a[0] < 0)) {
                    note("mods: %s: line %d isn't an enemy or item line with its nine numbers: room %d not used", f->what, ln, cur);
                    bad = 1;
                    continue;
                }
                uint8_t *e = (is_e ? en + 0x18 * ne++ : it + 0x18 * ni++);
                wr16(e, (uint16_t)a[0]);
                wr16(e + 2, (uint16_t)a[1]);
                wr32(e + 4, (uint32_t)a[2]);
                wr32(e + 8, (uint32_t)a[3]);
                wr32(e + 12, (uint32_t)a[4]);
                for (int k = 0; k < 4; k++) wr16(e + 16 + 2 * k, (uint16_t)a[5 + k]);
            }
            free(en);
            free(it);
            note("mods: %s: %d rooms' enemies and pickups", f->what, rooms);
        }
        free(s);
        free(f->d);
    }
    free(datafiles);
    datafiles = NULL;
    ndatafiles = 0;
}

/* The originals as data/COMBAT.txt and data/ROOMS.txt. */
static int data_extract(void) {
    int files = 0;
    Touched *test = touch(disc_index("TEST.BIN")), *t = touch(disc_index("ROOMENV.DAT"));
    if (!test || !t) return 0;
    size_t cap = 2 << 20, o = 0;
    char *out = malloc(cap), name[200];
#define ADD(...) do { if (o < cap) o += (size_t)snprintf(out + o, cap - o, __VA_ARGS__); } while (0)
    const uint8_t *T = test->data - TEST_BASE;
    ADD("# The weapons, their damage and the enemies' hit points (TEST.BIN, see combat.c and enemy.c). Damage of a hit = shot base damage\n"
        "# x factor (for the enemy's type and the shot's class) / 100 x the body part's factor / 100,\n"
        "# then by distance. A weapon is its item (the number of its name in text/GAME.txt, minus 1).\n"
        "# rate <weapon> <fire rate: the game uses it doubled>\n");
    for (uint32_t k = 0; k < 11; k++) { item_name(test->data, k, name, sizeof name); ADD("rate %u %u    # %s\n", k, rd32(T + W_RATE + 4 * k), name); }
    ADD("# shot <kind> <base damage: three values> <class 0-4>: the kind is the weapon; the grenade gun's\n"
        "# are 8, 9 and 10 by the grenades loaded\n");
    for (uint32_t k = 0; k < 11; k++)
        ADD("shot %u %d %d %d %d\n", k, (int16_t)rd16(T + W_SHOT + 6 * k), (int16_t)rd16(T + W_SHOT + 6 * k + 2), (int16_t)rd16(T + W_SHOT + 6 * k + 4),
            (int16_t)rd16(T + W_CLASS + 2 * k));
    ADD("# factor <enemy type> <percent for class 0> <1> <2> <3> <4>\n");
    for (uint32_t k = 0; k < 34; k++) {
        ADD("factor %u", k);
        for (int c = 0; c < 5; c++) ADD(" %d", (int16_t)rd16(T + W_FACTOR + 10 * k + 2 * c));
        ADD("\n");
    }
    ADD("# hp <n> <hit points>: the small enemies' (types 0-10) by their placement's last number\n"
        "# (data/ROOMS.txt); 15 and up picks one at random, by the cumulative chances in percent:\n");
    for (uint32_t k = 0; k < 15; k++) ADD("hp %u %d\n", k, (int16_t)rd16(T + W_HP + 2 * k));
    for (uint32_t k = 0; k < 15; k++) ADD("chance %u %d\n", k, (int32_t)rd32(T + W_CHANCE + 4 * k));
    Touched *ep = touch(disc_index("ENEMPROG.BIN"));
    ADD("# enemy <type> <hit points 0> <1> <2> <3>: a bigger enemy's (types 11-33), by its placement's last\n"
        "# number (0-3; 4 and up: one of the four at random). In its program (ENEMPROG.BIN), from the next room.\n");
    for (int k = 0; k < (int)(sizeof enemy_tables / sizeof enemy_tables[0]); k++) {
        uint32_t at[2];
        if (!enemy_tables[k].type || !enemy_table_at(test, ep, k, at)) continue;
        ADD("enemy %u", enemy_tables[k].type);
        for (int i = 0; i < 4; i++) ADD(" %d", (int16_t)rd16(ep->data + at[0] + 4 + 2 * i));
        if (enemy_tables[k].name[0]) ADD("    # %s", enemy_tables[k].name);
        ADD("\n");
    }
    ADD("# part <type> <shot kind 0-10> <percent for body part 0> <1> ... <9>: the body-part factor of a hit,\n"
        "# by the shot's kind (as in shot above: the weapon, the grenade gun's 8-10 by its grenades).\n");
    for (int k = 0; k < (int)(sizeof enemy_tables / sizeof enemy_tables[0]); k++) {
        uint32_t at[2];
        if (!enemy_table_at(test, ep, k, at)) continue;
        ADD("# type %u%s%s\n", enemy_tables[k].type, enemy_tables[k].name[0] ? ": " : "", enemy_tables[k].name);
        for (int r = 0; r < 11; r++) {
            ADD("part %u %d", enemy_tables[k].type, r);
            for (int i = 0; i < 10; i++) ADD(" %d", (int16_t)rd16(ep->data + at[0] + 0x18 + 20 * r + 2 * i));
            ADD("\n");
        }
    }
    files += write_out("data/COMBAT.txt", (const uint8_t *)out, o);
    o = 0;
    ADD("# The rooms' enemies and pickups (ROOMENV.DAT: @block, block = stage * 30 + room; see room_loader.c).\n"
        "# enemy <type> <game flag: not placed when set> <x> <y> <z> <yaw> <pose> <animation (+32768: starts hidden)>\n"
        "#       <hit points: for types 0-10 a row of hp in data/COMBAT.txt (15 and up: at random)>\n"
        "# item <item> <game flag: taken> <x> <y> <z> <rotation x> <y> <z> <extra>\n"
        "# A room you list has exactly these; at most 7 enemies and pickups together (10 where the room had more).\n");
    static uint32_t v[1024];
    uint32_t nb = roomenv_blocks(test, t, v);
    for (uint32_t b = 0; b < nb; b++) {
        uint32_t at = v[b] * 2048, len = (v[b + 1] - v[b]) * 2048, so, sl, io, il;
        if (v[b + 1] == v[b] || !roomenv_list(t->data, at, len, 1, &so, &sl) || !roomenv_list(t->data, at, len, 2, &io, &il)) continue;
        uint32_t ne = list_count(t->data + at + so, sl, 1), ni = list_count(t->data + at + io, il, 0);
        if (ne == 0xFFFFFFFFu || ni == 0xFFFFFFFFu) continue;
        ADD("@%u    # stage %u, room %u\n", b, b / 30, b % 30);
        for (uint32_t k = 0; k < ne; k++) {
            const uint8_t *e = t->data + at + so + 4 + 0x18 * k;
            ADD("enemy %d %d %d %d %d %u %d %u %u\n", (int16_t)rd16(e), (int16_t)rd16(e + 2), (int32_t)rd32(e + 4), (int32_t)rd32(e + 8),
                (int32_t)rd32(e + 12), rd16(e + 16), (int16_t)rd16(e + 18), rd16(e + 20), rd16(e + 22));
        }
        for (uint32_t k = 0; k < ni; k++) {
            const uint8_t *e = t->data + at + io + 0x18 * k;
            item_name(test->data, rd16(e), name, sizeof name);
            ADD("item %d %d %d %d %d %u %u %u %u    # %s\n", (int16_t)rd16(e), (int16_t)rd16(e + 2), (int32_t)rd32(e + 4), (int32_t)rd32(e + 8),
                (int32_t)rd32(e + 12), rd16(e + 16), rd16(e + 18), rd16(e + 20), rd16(e + 22), name);
        }
    }
    files += write_out("data/ROOMS.txt", (const uint8_t *)out, o);
#undef ADD
    free(out);
    return files;
}

/* Pictures of the disc that aren't the PC version's BMPs, by names of
 * their own: FONT/FONT186.png to FONT188.png (the text's font pages,
 * EV_OBJ.DAT's blocks 0x186-0x188: 18 glyphs a row, 8x14) and BIGTIM/n.png
 * (item n's picture when it's looked at, B/BIGTIM.BIN; PC item/PIC instead
 * contains pickup messages). A bigger picture uses HD textures, see
 * modfs_hd). Where the TIM is, NULL if not one of these. */
static uint8_t *own_picture(const char *up, char *name, size_t nn) {
    unsigned k;
    char rest[8] = "";
    if (sscanf(up, "FONT/FONT%x%7s", &k, rest) == 2 && k >= 0x186 && k <= 0x188 && is_picture(rest)) {
        Touched *test = touch(disc_index("TEST.BIN")), *t = touch(disc_index("EV_OBJ.DAT"));
        uint32_t at = test && t ? rd32(test->data + EVOBJ_TABLE - TEST_BASE + 4 * k) * 2048 : 0;
        snprintf(name, nn, "EV_OBJ.DAT block 0x%X", k);
        return t && at + 64 <= t->nsec * 2048 && rd32(t->data + at) == 0x10 ? t->data + at : NULL;
    }
    if (sscanf(up, "BIGTIM/%u%7s", &k, rest) == 2 && is_picture(rest)) {
        Touched *t = touch(disc_index("B/BIGTIM.BIN"));
        uint32_t at[64], end[64];
        int nb = bigtim_blocks(t, at, end, 64);
        snprintf(name, nn, "B/BIGTIM.BIN block %u", k);
        return (int)k < nb && rd32(t->data + at[k] + rd32(t->data + at[k] + 4)) == 0x10 ? t->data + at[k] + rd32(t->data + at[k] + 4) : NULL;
    }
    return NULL;
}

/* The three font pages share CLUT VRAM: e.g. the inventory uses the
 * FONT187 palette for glyphs stored in FONT188. Register these verified
 * bank alternatives against the modified sheet's pixel upload too. */
static int font_hd_add(const char *up, const uint8_t *tim, const uint8_t *img, uint32_t w, uint32_t h) {
    uint8_t banks[32 * 16 * 2];
    uint32_t rows = 0;
    if (rd32(tim + 4) != 8 || rd16(tim + 16) != 16) return modfs_hd_add(tim, img, w, h);
    for (unsigned k = 0x186; k <= 0x188; k++) {
        char path[48], name[64];
        snprintf(path, sizeof path, "FONT/FONT%X.PNG", k);
        if (!strcasecmp(path, up)) continue;
        uint8_t *t = own_picture(path, name, sizeof name);
        if (!t || rd32(t + 4) != 8 || rd16(t + 16) != 16) continue;
        uint32_t n = rd16(t + 18);
        if (n > 32 - rows) return 0;
        memcpy(banks + (size_t)rows * 32, t + 20, (size_t)n * 32);
        rows += n;
    }
    return modfs_hd_add_banks(tim, img, w, h, banks, rows);
}

static int own_picture_apply(const char *mod, const char *rel, const char *up, const char *path) {
    char name[64];
    uint8_t *tim = own_picture(up, name, sizeof name);
    if (!tim) return 0;
    long n = 0;
    uint8_t *d = load(path, &n);
    int w, h, c;
    uint8_t *img = d ? stbi_load_from_memory(d, (int)n, &w, &h, &c, 4) : NULL;
    free(d);
    if (!img) { note("mods: %s/%s: couldn't read it as a picture: not used", mod, rel); return 1; }
    uint32_t tl = tim_len(tim);
    uint8_t *before = malloc(tl);
    if (before) memcpy(before, tim, tl);
    const char *how = "", *why = tim_from_picture(tim, img, (uint32_t)w, (uint32_t)h, &how);
    if (!why && before) vram_fix_add(before, tim);
    free(before);
    uint32_t tw = 0, th = 0;
    tim_size(tim, &tw, &th);
    int hd = !why && (!strncmp(up, "FONT/", 5) ? font_hd_add(up, tim, img, (uint32_t)w, (uint32_t)h) : modfs_hd_add(tim, img, (uint32_t)w, (uint32_t)h));
    stbi_image_free(img);
    if (why) { note("mods: %s/%s: %s: not used", mod, rel, why); return 1; }
    note("mods: %s/%s: into %s (%ux%u, %s%s)", mod, rel, name, tw, th, how,
         hd ? ", with an HD replacement" : (uint32_t)w != tw || (uint32_t)h != th ? ", scaled to the PS1's size" : "");
    nused++;
    return 1;
}

/* ------------------------------------------------------------ movies */

/* A movie's pictures from a mod: M/OPENING.STR/0042.png is the whole
 * picture the game shows while it shows that stream's frame 42 (the
 * number in the frame's own header, from 1), at any size.
 *
 * Which frame the screen shows is followed as the game plays it: a frame
 * read whole from the disc (its last sector delivered) waits for the
 * decoder; the next picture decoded is that frame; its first slice
 * written to VRAM says which buffer it went to; and the screen shows
 * whatever buffer it shows. So it stays the game's own timing, skipping
 * and stopping included. */
typedef struct { int fi; uint32_t frame, seq; char *path; } Pic;
static Pic *pics;
static int npics, cpics;

static int cmp_pic(const void *a, const void *b) {
    const Pic *x = a, *y = b;
    if (x->fi != y->fi) return x->fi < y->fi ? -1 : 1;
    if (x->frame != y->frame) return x->frame < y->frame ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* A movie as a video file (modvideo.c, FFmpeg): M/OPENING.STR/video.mp4 (any
 * format FFmpeg reads), or the PC version's avi/Opening.avi for the stream
 * of the same name. Its frames go over the stream's in proportion (the
 * first to the first, the last to the last), so the game's own timing
 * stays; a frame given as a picture wins over the video's. */
typedef struct Video Video;
Video *modvideo_open(const char *path, const char **why);
int modvideo_frames(Video *v);
const uint32_t *modvideo_frame(Video *v, int n, int *w, int *h);
typedef struct { int fi; char *path; char what[600]; Video *v; int bad, frames, str_frames; } Vid;
static Vid vids[32];
static int nvids;

static int is_video(const char *ext) {
    static const char *const e[] = {".AVI", ".MPG", ".MPEG", ".MP4", ".MKV", ".WEBM", ".MOV", ".WMV", ".M4V"};
    for (size_t i = 0; i < sizeof e / sizeof e[0]; i++)
        if (!strcasecmp(ext, e[i])) return 1;
    return 0;
}

static int video_apply(const char *mod, const char *rel, const char *up, const char *path) {
    const char *sl = strrchr(up, '/'), *dot = strrchr(up, '.');
    if (!sl || !dot || dot < sl || !is_video(dot)) return 0;
    char stream[512];
    if (!strncmp(up, "AVI/", 4) && !strchr(up + 4, '/'))           /* the PC's: avi/Opening.avi */
        snprintf(stream, sizeof stream, "M/%.*s.STR", (int)(dot - up - 4), up + 4);
    else if (!strncmp(sl, "/VIDEO.", 7))
        snprintf(stream, sizeof stream, "%.*s", (int)(sl - up), up);
    else
        return 0;
    int fi = disc_index(stream);
    if (fi < 0 || !strstr(stream, ".STR")) {
        note("mods: %s/%s: a video, but the disc has no movie %s: not used", mod, rel, stream);
        return 1;
    }
    int k = 0;
    while (k < nvids && vids[k].fi != fi) k++;   /* a later mod wins */
    if (k == nvids) {
        if (nvids == (int)(sizeof vids / sizeof vids[0])) { note("mods: %s/%s: too many videos: not used", mod, rel); return 1; }
        nvids++;
    } else {
        free(vids[k].path);
        nused--;
    }
    vids[k] = (Vid){fi, strdup(path), "", NULL, 0, 0, 0};
    snprintf(vids[k].what, sizeof vids[k].what, "%s/%s", mod, rel);
    note("mods: %s/%s: the video for %s (read when it plays)", mod, rel, stream);
    nused++;
    return 1;
}

/* How many frames a stream has (its last frame's number in the sectors' headers). */
static int str_frames(int fi) {
    uint8_t s[2352];
    uint32_t n = (disc_files[fi].size + 2047) / 2048, most = 0;
    disc_only = 1;
    for (uint32_t k = 0; k < n; k++) {
        if (!cd_read_sector_raw(disc_files[fi].lba + k, s)) continue;
        if (rd32(s + 24) == 0x80010160u && rd32(s + 32) > most) most = rd32(s + 32);
    }
    disc_only = 0;
    return (int)most;
}

/* The video's picture for frame (from 1) of stream fi, or NULL. */
static const uint32_t *video_picture(int fi, uint32_t frame, int *w, int *h) {
    Vid *v = NULL;
    for (int i = 0; i < nvids; i++)
        if (vids[i].fi == fi) v = &vids[i];
    if (!v || v->bad) return NULL;
    if (!v->v) {
        const char *why = "";
        if (!(v->v = modvideo_open(v->path, &why))) {
            note("mods: %s: %s: not shown", v->what, why);
            v->bad = 1;
            return NULL;
        }
        v->frames = modvideo_frames(v->v);
        v->str_frames = str_frames(fi);
        note("mods: %s: %d frames over the movie's %d", v->what, v->frames, v->str_frames);
        if (v->frames < 1 || v->str_frames < 1) { v->bad = 1; return NULL; }
    }
    int n = v->str_frames > 1 ? (int)(((int64_t)(frame - 1) * (v->frames - 1) + (v->str_frames - 1) / 2) / (v->str_frames - 1)) : 0;
    return modvideo_frame(v->v, n < v->frames ? n : v->frames - 1, w, h);
}

/* M/OPENING.STR/0042.png: frame 42 of that stream. */
static int movie_apply(const char *mod, const char *rel, const char *up, const char *path) {
    if (video_apply(mod, rel, up, path)) return 1;
    const char *sl = strrchr(up, '/'), *dot = strrchr(up, '.');
    if (!sl || !dot || dot < sl) return 0;
    if (strcasecmp(dot, ".PNG") && strcasecmp(dot, ".JPG") && strcasecmp(dot, ".JPEG") && strcasecmp(dot, ".BMP") &&
        strcasecmp(dot, ".TGA"))
        return 0;
    char stream[512];
    snprintf(stream, sizeof stream, "%.*s", (int)(sl - up), up);
    int fi = disc_index(stream);
    if (fi < 0 || !strstr(stream, ".STR")) return 0;
    char *end;
    long frame = strtol(sl + 1, &end, 10);
    if (end == sl + 1 || end != dot || frame < 1) {
        note("mods: %s/%s: name it by the frame's number (0001.png, 0002.png...): not used", mod, rel);
        return 1;
    }
    if (npics == cpics) {
        cpics = cpics ? cpics * 2 : 1024;
        pics = realloc(pics, (size_t)cpics * sizeof *pics);
    }
    pics[npics] = (Pic){fi, (uint32_t)frame, (uint32_t)npics, strdup(path)};
    npics++;
    nused++;
    return 1;
}

/* Once every mod is in: sorted, a later mod's picture in place of an
 * earlier one's, and a line in the log for each stream. */
static void movie_done(void) {
    if (!npics) return;
    qsort(pics, (size_t)npics, sizeof *pics, cmp_pic);
    int n = 0;
    for (int i = 0; i < npics; i++) {
        if (n && pics[n - 1].fi == pics[i].fi && pics[n - 1].frame == pics[i].frame) {
            free(pics[n - 1].path);
            nused--;
            pics[n - 1] = pics[i];
        } else
            pics[n++] = pics[i];
    }
    npics = n;
    for (int i = 0; i < npics;) {
        int j = i;
        while (j < npics && pics[j].fi == pics[i].fi) j++;
        note("mods: %s: pictures for %d frames (%u to %u)", disc_files[pics[i].fi].name, j - i, pics[i].frame,
               pics[j - 1].frame);
        i = j;
    }
}

static const Pic *pic_find(int fi, uint32_t frame) {
    int lo = 0, hi = npics - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        const Pic *p = &pics[m];
        if (p->fi == fi && p->frame == frame) return p;
        if (p->fi < fi || (p->fi == fi && p->frame < frame)) lo = m + 1;
        else hi = m - 1;
    }
    return NULL;
}

/* The stream a sector belongs to (-1: none). */
static int stream_at(uint32_t lba) {
    static int strs[64], nstrs = -1;
    if (nstrs < 0) {
        nstrs = 0;
        for (size_t i = 0; i < sizeof disc_files / sizeof disc_files[0] && nstrs < 64; i++)
            if (strstr(disc_files[i].name, ".STR")) strs[nstrs++] = (int)i;
    }
    for (int i = 0; i < nstrs; i++)
        if (lba - disc_files[strs[i]].lba < (disc_files[strs[i]].size + 2047) / 2048) return strs[i];
    return -1;
}

typedef struct { int fi; uint32_t frame; } Shown;
static Shown readq[8];                           /* frames read whole, not decoded yet */
static int nreadq;
static Shown decoding = {-1, 0};                 /* the picture the decoder made last */
static uint32_t decoded_at;
static int placed;                               /* its first slice is in VRAM */
static struct { int x, y; Shown s; uint32_t seq; } bufs[4];   /* where pictures went */
static uint32_t nplaced;
static Shown now = {-1, 0};                      /* what the screen shows */

/* cdrom.c: a data sector the game is given. */
void modfs_data(uint32_t lba, const uint8_t *s) {
    int fi = stream_at(lba);
    if (fi < 0) { nreadq = 0; return; }
    const uint8_t *d = s + 24;
    if (rd32(d) != 0x80010160u || rd16(d + 4) + 1 != rd16(d + 6)) return;   /* not a frame's last sector */
    if (nreadq && readq[nreadq - 1].fi != fi) nreadq = 0;
    if (nreadq == 8) memmove(readq, readq + 1, sizeof readq - sizeof readq[0]), nreadq--;
    readq[nreadq++] = (Shown){fi, rd32(d + 8)};
}

/* mdec.c: a picture starts decoding. */
void modfs_mdec(void) {
    if (nreadq) {
        decoding = readq[0];
        memmove(readq, readq + 1, (size_t)--nreadq * sizeof readq[0]);
    } else
        decoding.fi = -1;
    decoded_at = g_frame;
    placed = 0;
}

/* gpu.c: pixels written to VRAM from the CPU, at x,y. */
void modfs_vram_write(int x, int y) {
    if (placed || decoding.fi < 0 || g_frame - decoded_at > 4) return;
    placed = 1;
    int k = 0;
    for (int i = 0; i < 4; i++) {
        if (bufs[i].x == x && bufs[i].y == y && bufs[i].seq) { k = i; break; }
        if (bufs[i].seq < bufs[k].seq) k = i;
    }
    bufs[k].x = x;
    bufs[k].y = y;
    bufs[k].s = decoding;
    bufs[k].seq = ++nplaced;
}

/* An RGB picture as a PNG file, not compressed (the deflate it needs
 * isn't built in; any program reads it, and saves it smaller). */
static void png_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
    uint8_t b[8] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n};
    memcpy(b + 4, type, 4);
    fwrite(b, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t crc = (uint32_t)mz_crc32(mz_crc32(0, b + 4, 4), d, n);
    uint8_t c[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
    fwrite(c, 1, 4, f);
}

static void png_write(const char *path, const uint8_t *rgb, int w, int h) {
    size_t row = (size_t)w * 3 + 1, raw = row * h, blocks = (raw + 65534) / 65535;
    uint8_t *z = malloc(2 + raw + blocks * 5 + 4), *o = z;
    FILE *f = z ? fopen(path, "wb") : NULL;
    if (!f) { free(z); return; }
    *o++ = 0x78;                                 /* zlib: deflate, stored blocks */
    *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw;) {
        size_t n = raw - i < 65535 ? raw - i : 65535;
        *o++ = i + n == raw;
        *o++ = (uint8_t)n; *o++ = (uint8_t)(n >> 8); *o++ = (uint8_t)~n; *o++ = (uint8_t)(~n >> 8);
        for (size_t k = 0; k < n; k++, i++) {
            uint8_t v = i % row ? rgb[(i / row) * w * 3 + i % row - 1] : 0;
            *o++ = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
    }
    uint32_t ad = b << 16 | a;
    *o++ = (uint8_t)(ad >> 24); *o++ = (uint8_t)(ad >> 16); *o++ = (uint8_t)(ad >> 8); *o++ = (uint8_t)ad;
    uint8_t ihdr[13] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                        (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0};
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)(o - z));
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
}

/* Once a frame: with enemy models a mod made bigger, a line in the log if
 * a room's enemies fill the memory they're loaded into (0x80150000 up,
 * below the stacks from 0x801F0000) nearly to its end. */
static int models_grown;
static void memory_watch(void) {
    static int told;
    if (!models_grown || told) return;
    uint32_t top = rd32(ram_ptr(0x800B4D78));
    if (top > 0x801E8000u && top < 0x801F0000u) {
        told = 1;
        note("mods: this room's enemies take the memory they load into nearly to its end (up to %08X): bigger models than these are not safe", top);
    } else if (top >= 0x801F0000u && top < 0x80200000u) {
        told = 1;
        note("mods: this room's enemies don't fit the memory they load into (up to %08X): the game may break; make the bigger models lighter", top);
    }
}

/* display.c, once a frame: which frame the screen shows; written out
 * when dumping (g_movie_dump/M/OPENING.STR/0042.png, the picture as the
 * PS1 shows it). */
void modfs_movie_vblank(void) {
    memory_watch();
    now.fi = -1;
    if (g_frame - g_mdec_frame >= 8) return;     /* no movie playing */
    GpuDisplay d;
    gpu_display(&d);
    int dw = d.rgb24 ? d.w * 3 / 2 : d.w;
    uint32_t best = 0;
    for (int i = 0; i < 4; i++)
        if (bufs[i].seq > best && bufs[i].x - d.x >= 0 && bufs[i].x - d.x < dw && bufs[i].y - d.y >= 0 &&
            bufs[i].y - d.y < d.h) {
            best = bufs[i].seq;
            now = bufs[i].s;
        }
    static Shown last = {-1, 0};
    if (!g_movie_dump[0] || now.fi < 0 || (now.fi == last.fi && now.frame == last.frame)) return;
    last = now;
    if (d.w > 640 || d.h > 512) return;
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", g_movie_dump, disc_files[now.fi].name);
    for (char *c = path + 1; *c; c++)
        if (*c == '/') { *c = 0; mkdir(path); *c = '/'; }
    mkdir(path);
    snprintf(path + strlen(path), sizeof path - strlen(path), "/%04u.png", now.frame);
    static uint8_t rgb[640 * 512 * 3];
    int w, h;
    display_grab_rgb(rgb, &w, &h);
    png_write(path, rgb, w, h);
}

/* display.c: a mod's picture for what the screen shows now (BGRX, its
 * size), or NULL. The last one stays decoded. */
const uint32_t *modfs_movie_picture(int *w, int *h) {
    if (now.fi < 0 || g_frame - g_mdec_frame >= 8) return NULL;
    const Pic *p = pic_find(now.fi, now.frame);
    if (!p) return video_picture(now.fi, now.frame, w, h);
    static const Pic *have;
    static uint32_t *px;
    static int pw, ph;
    if (p != have) {
        have = p;
        free(px);
        px = NULL;
        long n = 0;
        uint8_t *d = load(p->path, &n);
        int c;
        uint8_t *img = d ? stbi_load_from_memory(d, (int)n, &pw, &ph, &c, 4) : NULL;
        free(d);
        if (!img || pw > 4096 || ph > 4096) {
            note("mods: %s: %s: not shown", p->path, img ? "bigger than 4096x4096" : "couldn't read it as a picture");
            stbi_image_free(img);
            return NULL;
        }
        px = malloc((size_t)pw * ph * 4);
        for (size_t i = 0; i < (size_t)pw * ph; i++)
            px[i] = img[i * 4 + 2] | (uint32_t)img[i * 4 + 1] << 8 | (uint32_t)img[i * 4] << 16;
        stbi_image_free(img);
    }
    if (!px) return NULL;
    *w = pw;
    *h = ph;
    return px;
}

/* ------------------------------------------------------------ enemies */

/* The PC version's enemies (its nab folder: ENEMnn.NAB, the model, and
 * ENEMnn.NAC, its animations, nn the enemy type) are the PS1's own data
 * read out into floats: the NAC's commands are the PS1's animation
 * script word for word and its keys the PS1's 20-byte keys (s32
 * translation x,y,z, 0x7FFFFFFF for "unchanged", which the PC turns
 * into 0; s16 rotation x,y,z; a zero), the NAB's textures the PS1's
 * 16-bit TIMs as A1R5G5B5 (192-wide ones stretched to 256, nearest
 * pixel). On the disc a type's data is one block of OBJ.DAT (type 30:
 * EV_OBJ.DAT 0x196): a header (3, then where each part starts) and three
 * parts packed with the game's LZSS: textures, animations (2: script,
 * keys), model (HMD). A mod's NAB/NAC is written back into that: the
 * script and keys, the textures; the model's shape (the NAB's polygons)
 * isn't taken yet. What the PC loses (the 0x7FFFFFFF keys, the
 * textures' semi-transparency bit) comes from the disc's. */

/* The game's LZSS (kernel_files.c, 0x80016F78): a 32-bit size, then a
 * flag byte per 8 items (bit set: a literal byte; clear: 16 bits,
 * distance in the top 11, length - 3 in the low 5). */
static uint8_t *lz_unpack_size(const uint8_t *s, uint32_t avail, uint32_t *n, uint32_t *consumed) {
    if (avail < 4) return NULL;
    uint32_t size = rd32(s), i = 4, o = 0, flags = 0;
    if (size > 16u << 20) return NULL;
    uint8_t *d = malloc(size + 64);
    while (d && o < size) {
        if (!(flags & 0x100)) {
            if (i >= avail) break;
            flags = s[i++] | 0xFF00u;
        }
        if (flags & 1) {
            if (i >= avail) break;
            d[o++] = s[i++];
        } else {
            if (i + 2 > avail) break;
            uint32_t w = s[i] | s[i + 1] << 8, dist = w >> 5, len = (w & 31) + 3;
            i += 2;
            if (!dist) { free(d); return NULL; }   /* a self-reference has no initialized source byte */
            for (uint32_t k = 0; k < len && o < size + 34; k++, o++) d[o] = o >= dist ? d[o - dist] : 0;
        }
        flags >>= 1;
    }
    if (d && o < size) { free(d); return NULL; }
    *n = size;
    if (consumed) *consumed = i;
    return d;
}

static uint8_t *lz_unpack(const uint8_t *s, uint32_t avail, uint32_t *n) {
    return lz_unpack_size(s, avail, n, NULL);
}

#define HASH(p) ((((uint32_t)in[p] << 8) ^ ((uint32_t)in[(p) + 1] << 4) ^ in[(p) + 2]) & 4095)
static void lz_match(const uint8_t *in, uint32_t n, uint32_t i, const int32_t *head, const int32_t *prev, uint32_t *best,
                     uint32_t *bdist) {
    *best = 0;
    if (i + 3 > n) return;
    int32_t c = head[HASH(i)];
    for (int tries = 0; c >= 0 && i - (uint32_t)c <= 2047 && tries < 128; tries++, c = prev[c]) {
        if ((uint32_t)c >= i) continue;
        uint32_t l = 0, max = n - i < 34 ? n - i : 34;
        while (l < max && in[c + l] == in[i + l]) l++;
        if (l > *best) { *best = l; *bdist = i - (uint32_t)c; if (l == 34) break; }
    }
}

static uint8_t *lz_pack(const uint8_t *in, uint32_t n, uint32_t *outn) {
    uint8_t *o = malloc(4 + (size_t)n + n / 8 + 16);
    int32_t *head = malloc(4096 * sizeof *head), *prev = malloc(((size_t)n + 1) * sizeof *prev);
    if (!o || !head || !prev) { free(o); free(head); free(prev); return NULL; }
    for (int k = 0; k < 4096; k++) head[k] = -1;
    wr32(o, n);
    uint32_t w = 4, flagat = 0, item = 8, i = 0;
    while (i < n) {
        if (item == 8) { flagat = w++; o[flagat] = 0; item = 0; }
        uint32_t best = 0, bdist = 0, next = 0, ndist = 0;
        lz_match(in, n, i, head, prev, &best, &bdist);
        if (best >= 3 && best < 34 && i + 1 < n) {   /* a longer one a byte on: a literal first */
            uint32_t h = HASH(i);                    /* (i itself in reach for it) */
            prev[i] = head[h];
            head[h] = (int32_t)i;
            lz_match(in, n, i + 1, head, prev, &next, &ndist);
            head[h] = prev[i];
            if (next > best + 1) best = 0;
        }
        uint32_t step = best >= 3 ? best : 1;
        if (best >= 3) {
            uint32_t v = bdist << 5 | (best - 3);
            o[w++] = (uint8_t)v;
            o[w++] = (uint8_t)(v >> 8);
        } else {
            o[flagat] |= (uint8_t)(1 << item);
            o[w++] = in[i];
        }
        item++;
        for (uint32_t k = 0; k < step; k++, i++)
            if (i + 3 <= n) { uint32_t h = HASH(i); prev[i] = head[h]; head[h] = (int32_t)i; }
    }
#undef HASH
    free(head);
    free(prev);
    *outn = w;
    return o;
}

#include "modfs_diary.inc"
#include "modfs_pc_text.inc"
#include "modfs_pc_diary.inc"

static int ui_extract(void) {
    Touched *t;
    const uint8_t *tab, *pages;
    if (!diary_info(&t, &tab, &pages)) return 0;
    char output[4096] = "", text[512];
    size_t n = 0;
    Touched *test = touch(disc_index("TEST.BIN"));
    for (size_t i = 0; i < sizeof ui_texts / sizeof ui_texts[0]; i++) {
        int ended;
        if (ui_texts[i].original >= 0x800C3E48u) text_decode(diary_meta + ui_texts[i].original - 0x800C3E48u, 56, text, sizeof text, &ended);
        else if (test) text_decode(test->data + ui_texts[i].original - TEST_BASE, 240, text, sizeof text, &ended);   /* (the doors') */
        else continue;
        if (!ended) return 0;
        n += (size_t)snprintf(output + n, sizeof output - n, "@%s\n%s\n", ui_texts[i].name, text);
    }
    return write_out("text/UI.txt", (const uint8_t *)output, n);
}

/* Blocks of the packed archives (OBJ.DAT, EV_OBJ.DAT, ITEMDAT.DAT) that
 * mods change: unpacked once, changed by every mod that has something
 * for them, packed again at the end (a part nothing changed keeps the
 * disc's packing). A block is either a header (3, then where each part
 * starts) and three packed parts, or packed whole (part 0 here). */
typedef struct {
    int fi;
    uint32_t block, start, len, off[4];
    int split, changed;                          /* changed: a bit per part */
    uint8_t *part[3], *was[3];
    uint32_t plen[3], was_len[3];
    char what[400];
} Packed;
static Packed packs[64];
static int npacks;

static uint32_t archive_table(int fi) {          /* its block table's address in TEST.BIN, or 0 */
    for (size_t k = 0; k < sizeof archives / sizeof archives[0]; k++)
        if (fi >= 0 && !strcasecmp(disc_files[fi].name, archives[k].name)) return archives[k].tab;
    return 0;
}

static Packed *packed_get(int fi, uint32_t block) {
    for (int i = 0; i < npacks; i++)
        if (packs[i].fi == fi && packs[i].block == block) return &packs[i];
    Touched *test = touch(disc_index("TEST.BIN")), *t = fi >= 0 ? touch(fi) : NULL;
    uint32_t tab = archive_table(fi);
    if (!test || !t || !tab || npacks == (int)(sizeof packs / sizeof packs[0])) return NULL;
    tab -= TEST_BASE;
    uint32_t start = rd32(test->data + tab + 4 * block) * 2048, end = rd32(test->data + tab + 4 * block + 4) * 2048;
    if (end <= start || end > t->nsec * 2048) return NULL;
    Packed *p = &packs[npacks];
    memset(p, 0, sizeof *p);
    p->fi = fi;
    p->block = block;
    p->start = start;
    p->len = end - start;
    const uint8_t *b = t->data + start;
    p->split = rd32(b) == 3 && rd32(b + 4) == 16;
    if (p->split) {
        for (int k = 0; k < 3; k++) p->off[k] = rd32(b + 4 + 4 * k);
        p->off[3] = p->len;
        if (p->off[0] > p->off[1] || p->off[1] > p->off[2] || p->off[2] > p->len) return NULL;
    } else {
        p->off[0] = 0;
        p->off[1] = p->len;
    }
    npacks++;
    return p;
}

/* Part k of a block, unpacked (NULL if it can't be). */
static uint8_t *packed_part(Packed *p, int k, uint32_t *n) {
    if (!p || k < 0 || k > (p->split ? 2 : 0)) return NULL;
    if (!p->part[k]) {
        Touched *t = touch(p->fi);
        p->part[k] = lz_unpack(t->data + p->start + p->off[k], p->off[k + 1] - p->off[k], &p->plen[k]);
        if (p->part[k] && (p->was[k] = malloc(p->plen[k]))) {
            p->was_len[k] = p->plen[k];
            memcpy(p->was[k], p->part[k], p->plen[k]);
        }
    }
    *n = p->plen[k];
    return p->part[k];
}

static void packed_note(Packed *p, int k, const char *what) {
    if (k > 0 && p->fi >= 0 && (!strcmp(disc_files[p->fi].name, "OBJ.DAT") ||
                               !strcmp(disc_files[p->fi].name, "EV_OBJ.DAT"))) mod_memory_models = 1;
    if (p->split && p->fi>=0 && modmem_split_identity(archive_table(p->fi),p->block)) mod_memory_models=1;
    p->changed |= 1 << k;
    size_t l = strlen(p->what);
    if (!strstr(p->what, what)) snprintf(p->what + l, sizeof p->what - l, "%s%s", l ? ", " : "", what);
}

/* Every changed block packed again, to grow with the archive's other
 * files that grow (grow_all). */
static void packs_done(void) {
    for (int i = 0; i < npacks; i++) {
        Packed *p = &packs[i];
        if (p->changed) {
            Touched *t = touch(p->fi);
            const uint8_t *b = t->data + p->start;
            int np = p->split ? 3 : 1;
            uint8_t *out = malloc((size_t)p->len * 2 + p->plen[0] + p->plen[1] + p->plen[2] + 64);
            uint32_t at = p->split ? 16 : 0;
            if (p->split) wr32(out, 3);
            for (int k = 0; k < np; k++) {
                if (p->split) wr32(out + 4 + 4 * k, at);
                uint32_t pn = 0;
                int same = !p->part[k] || (p->was[k] && p->was_len[k] == p->plen[k] && !memcmp(p->was[k], p->part[k], p->plen[k]));   /* the disc's packing kept */
                uint8_t *pk = same ? NULL : lz_pack(p->part[k], p->plen[k], &pn);
                if (pk) { memcpy(out + at, pk, pn); free(pk); }
                else { pn = p->off[k + 1] - p->off[k]; memcpy(out + at, b + p->off[k], pn); }
                at += (pn + 3) & ~3u;
            }
            if (ngrows < (int)(sizeof grows / sizeof grows[0])) {
                Grow *g = &grows[ngrows++];
                g->fi = p->fi;
                g->offset = p->start;
                g->size = at;
                g->orig = p->len;
                g->cap = 0;
                g->data = out;
                snprintf(g->what, sizeof g->what, "%s", p->what);
            } else
                free(out);
        }
        for (int k = 0; k < 3; k++) { free(p->part[k]); free(p->was[k]); }
    }
    npacks = 0;
}

/* A PC version's file that is inside a packed block (modfs_pack.h): in
 * its place there (every place, when the disc has it more than once). */
static int pack_apply(const char *mod, const char *rel, const char *up, const char *path) {
    long n = -1;
    uint8_t *d = NULL;
    int any = 0, done = 0;
    char what[400];
    snprintf(what, sizeof what, "%s/%s", mod, rel);
    for (size_t i = 0; i < sizeof modfs_pack / sizeof modfs_pack[0]; i++) {
        if (strcmp(modfs_pack[i].pc, up)) continue;
        any = 1;
        if (n < 0 && !(d = load(path, &n))) { note("mods: %s: couldn't read it", what); return 1; }
        if ((uint32_t)n > modfs_pack[i].size) {
            note("mods: %s: %ld bytes, %u more than the original: not used (inside a packed block a file keeps its size)", what, n,
                   (unsigned)((uint32_t)n - modfs_pack[i].size));
            break;
        }
        int fi = disc_index(modfs_pack[i].disc);
        uint8_t *at = NULL;
        Packed *p = NULL;
        if (modfs_pack[i].part == -2) {          /* the block as it is */
            Touched *test = touch(disc_index("TEST.BIN")), *t = touch(fi);
            uint32_t tab = archive_table(fi);
            if (test && t && tab) at = t->data + rd32(test->data + tab - TEST_BASE + 4 * modfs_pack[i].block) * 2048 + modfs_pack[i].offset;
        } else {
            uint32_t pn = 0;
            p = packed_get(fi, modfs_pack[i].block);
            uint8_t *u = packed_part(p, modfs_pack[i].part < 0 ? 0 : modfs_pack[i].part, &pn);
            if (u && modfs_pack[i].offset + modfs_pack[i].size <= pn) at = u + modfs_pack[i].offset;
        }
        if (!at) { note("mods: %s: %s block %u couldn't be read: not used there", what, modfs_pack[i].disc, modfs_pack[i].block); continue; }
        const uint8_t *orig = p ? p->was[modfs_pack[i].part < 0 ? 0 : modfs_pack[i].part] + modfs_pack[i].offset : at;
        static uint8_t *seen[256];               /* raw places a mod already changed (their original was checked then) */
        int again = 0;
        for (int s2 = 0; s2 < 256 && seen[s2]; s2++) if (seen[s2] == at) again = 1;
        if (!again && (uint32_t)mz_crc32(0, orig, modfs_pack[i].size) != modfs_pack[i].crc) {
            note("mods: %s: the disc's %s isn't the one expected there: not used", what, modfs_pack[i].disc);
            continue;
        }
        if (!p && !again) for (int s2 = 0; s2 < 256; s2++) if (!seen[s2]) { seen[s2] = at; break; }
        memset(at, 0, modfs_pack[i].size);
        memcpy(at, d, (size_t)n);
        if (p) packed_note(p, modfs_pack[i].part < 0 ? 0 : modfs_pack[i].part, what);
        else memory_mark_file(fi);
        done++;
    }
    if (!any) return 0;
    free(d);
    if (done) {
        note("mods: %s: into %d place%s of the disc's packed files", what, done, done > 1 ? "s" : "");
        nused++;
    }
    return 1;
}

/* What mods give for an enemy type: [0] its NAB, [1] its NAC, [2] a
 * picture for its texture (nab/ENEM00.png). */
typedef struct { int type; uint8_t *data[3]; long n[3]; char what[3][300]; } Enemy;
static Enemy enemies[64];
static int nenemies;

/* nab/ENEM00.NAB, nab/ENEM00.NAC, or nab/ENEM00.png (the texture as a picture) */
static int enemy_apply(const char *mod, const char *rel, const char *up, const char *path) {
    if (strncmp(up, "NAB/ENEM", 8) || !isdigit((unsigned char)up[8]) || !isdigit((unsigned char)up[9]) || up[10] != '.') return 0;
    int kind = !strcmp(up + 11, "NAB") ? 0 : !strcmp(up + 11, "NAC") ? 1 : is_picture(up + 10) ? 2 : -1;
    if (kind < 0) return 0;
    int type = (up[8] - '0') * 10 + (up[9] - '0');
    Enemy *e = NULL;
    for (int i = 0; i < nenemies; i++) if (enemies[i].type == type) e = &enemies[i];
    if (!e && nenemies < (int)(sizeof enemies / sizeof enemies[0])) {
        e = &enemies[nenemies++];
        memset(e, 0, sizeof *e);
        e->type = type;
    }
    long n = 0;
    uint8_t *d = e ? load(path, &n) : NULL;
    if (!d) { note("mods: %s/%s: couldn't read it", mod, rel); return 1; }
    if (!e->data[kind]) nused++;
    free(e->data[kind]);                         /* a later mod wins */
    e->data[kind] = d;
    e->n[kind] = n;
    snprintf(e->what[kind], sizeof e->what[kind], "%s/%s", mod, rel);
    return 1;
}

/* The NAC's animations into the PS1's (A: 2, script, keys). */
/* How much bigger an enemy type's animations (loaded once for all of its
 * kind, in the same memory as the models) can get. */
#define ANIM_GROWTH (modmem_enabled() ? (RAM_SIZE - RAM_GAME) : (64u * 1024))

/* The NAC's animations into the PS1's (A: 2, script, keys; *A replaced
 * when the NAC has another number of commands or keys). A command that
 * takes a key has the key's word offset (index x 5) in its low 16 bits:
 * from the NAC's own key index, so commands and keys can be added (up to
 * 13107 keys). NULL, or why not. */
static const char *nac_into(const uint8_t *nac, long n, uint8_t **Ap, uint32_t *nap, int *rebuilt) {
    uint8_t *A = *Ap;
    uint32_t na = *nap;
    *rebuilt = 0;
    if (n < 8 || na < 12 || rd32(A) != 2) return "the disc's animations aren't laid out as expected";
    uint32_t a0 = rd32(A + 4), a1 = rd32(A + 8);
    if (a0 > a1 || a1 > na) return "the disc's animations aren't laid out as expected";
    uint32_t ncmd = (a1 - a0) / 4, nkey = (na - a1) / 20, nk = rd32(nac);
    if (!nk || (uint64_t)nk * 112 + 8 > (uint64_t)n) return "it's cut short";
    uint32_t nc = rd32(nac + 4 + (size_t)nk * 112);
    if (4 + (uint64_t)nk * 112 + 4 + (uint64_t)nc * 8 != (uint64_t)n) return "it's cut short or has more after it";
    uint32_t keys = nk - 1;                      /* (the PC's NAC has a key more than the PS1's, at its end) */
    if (keys > 13107) return "it has more than 13107 keys, more than the PS1's commands can point at";
    uint8_t *old = A;
    uint32_t oa0 = a0, oa1 = a1;
    if (nc != ncmd || keys != nkey) {            /* laid out again: 2, 12, the script's end, then the script and the keys */
        uint32_t nn = 12 + 4 * nc + 20 * keys;
        if (nn > na + ANIM_GROWTH) {
            static char b[160];
            snprintf(b, sizeof b, "its animations would grow by %u bytes, more than the %u the game's memory has room for", nn - na, ANIM_GROWTH);
            return b;
        }
        A = calloc(nn, 1);
        if (!A) return "out of memory";
        wr32(A, 2);
        wr32(A + 4, 12);
        wr32(A + 8, 12 + 4 * nc);
        a0 = 12;
        a1 = 12 + 4 * nc;
        *rebuilt = 1;
        *nap = nn;
    }
    /* laid out again, keys may have moved: each found by what it holds among the disc's (as the PC
     * shows them), to know which of its channels were "unchanged" there */
    uint32_t *find = NULL;
    if (*rebuilt && (find = malloc(32768 * 4))) {
        memset(find, 0xFF, 32768 * 4);
        for (uint32_t k = 0; k < nkey; k++) {
            const uint8_t *q = old + oa1 + 20 * k;
            uint32_t hsh = 0;
            for (int c = 0; c < 3; c++) hsh = hsh * 31 + (rd32(q + 4 * c) == 0x7FFFFFFFu ? 0 : rd32(q + 4 * c));
            for (int c = 0; c < 3; c++) hsh = hsh * 31 + rd16(q + 12 + 2 * c);
            hsh &= 32767;
            while (find[hsh] != 0xFFFFFFFFu) hsh = (hsh + 1) & 32767;
            find[hsh] = k;
        }
    }
    const uint8_t *cmd = nac + 4 + (size_t)nk * 112 + 4;
    for (uint32_t i = 0; i < nc; i++) {
        uint32_t w = rd32(cmd + 8 * i), key = rd32(cmd + 8 * i + 4);
        if (i && key != 0xFFFFFFFFu) {
            if (key >= keys) { if (*rebuilt) free(A); free(find); return "a command takes a key it doesn't have"; }
            w = (w & 0xFFFF0000u) | (key * 5);
        }
        wr32(A + a0 + 4 * i, w);
    }
    for (uint32_t k = 0; k < keys; k++) {
        const uint8_t *f = nac + 4 + (size_t)k * 112;
        uint8_t *p = A + a1 + 20 * k;
        const uint8_t *was = !*rebuilt && k < nkey ? old + oa1 + 20 * k : NULL;
        float v[12];
        memcpy(v, f, sizeof v);
        if (find) {                              /* the disc's key with the same values, if there is one */
            int32_t t3[3];
            uint16_t r3[3];
            uint32_t hsh = 0;
            for (int c = 0; c < 3; c++) { t3[c] = (int32_t)lrintf(v[9 + c]); hsh = hsh * 31 + (uint32_t)t3[c]; }
            for (int c = 0; c < 3; c++) { r3[c] = (uint16_t)(int16_t)lrintf(v[5 + c]); hsh = hsh * 31 + r3[c]; }
            for (hsh &= 32767; find[hsh] != 0xFFFFFFFFu; hsh = (hsh + 1) & 32767) {
                const uint8_t *q = old + oa1 + 20 * find[hsh];
                int same = 1;
                for (int c = 0; c < 3 && same; c++)
                    same = (rd32(q + 4 * c) == 0x7FFFFFFFu ? 0 : (int32_t)rd32(q + 4 * c)) == t3[c] && rd16(q + 12 + 2 * c) == r3[c];
                if (same) { was = q; break; }
            }
        }
        for (int c = 0; c < 3; c++) {
            int32_t t = (int32_t)lrintf(v[9 + c]);
            if (was && rd32(was + 4 * c) == 0x7FFFFFFFu && t == 0) t = 0x7FFFFFFF;   /* "unchanged", which the PC turns into 0 */
            wr32(p + 4 * c, (uint32_t)t);
            wr16(p + 12 + 2 * c, (uint16_t)(int16_t)lrintf(v[5 + c]));
        }
        wr16(p + 18, 0);
    }
    (void)oa0;
    free(find);
    if (*rebuilt) *Ap = A;
    return NULL;
}

/* The NAB's animation tables (for each bone, where each of its animations
 * starts in the script: control << 16 | command) into the HMD's
 * animation unit (B). NULL, or why not. */
static const char *nab_seqs(const uint8_t *nab, long n, uint8_t *B, uint32_t nb) {
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;  /* past the vertices, materials and groups */
    if (n < 8 || at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nm = rd32(nab + at);
    at += 4;
    for (uint32_t m = 0; m < nm && m < 64; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        uint32_t ntx = rd32(nab + at + 8);
        at += 352;
        for (uint32_t t = 0; t < ntx && at + 16 <= (uint64_t)n; t++) at += 16 + (uint64_t)rd32(nab + at);
    }
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t ng = rd32(nab + at);
    at += 4;
    for (uint32_t g = 0; g < ng && g < 4096; g++) {
        if (at + 24 > (uint64_t)n) return "it's cut short";
        at += 24 + (rd32(nab + at) ? 4 : 0) + 4;
    }
    at += (uint64_t)rd32(nab) * 60 + (uint64_t)rd32(nab + 4) * 20;   /* per-vertex and per-face data */
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nq = rd32(nab + at);
    at += 4;
    /* the HMD's animation unit (B: the model, 2 parts; or nb's top bit set: B is the HMD itself) */
    uint32_t o1 = 0;
    if (nb & 0x80000000u) nb &= 0x7FFFFFFF;
    else {
        if (nb < 12 || rd32(B) != 2) return "the disc's model isn't laid out as expected";
        o1 = rd32(B + 8);
        if (o1 > nb) return "the disc's model isn't laid out as expected";
    }
    uint32_t hw = (nb - o1) / 4, unit = 0;
    const uint8_t *h = B + o1;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    for (uint32_t bi = 0; bi < HW(3) && bi < 64 && !unit; bi++)   /* (following each block's chain of units) */
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64 && !unit; p = HW(p), guard++)
            if (HW(p + 3) == 0x3800000u) unit = p;
    if (!unit) return "the disc's model has no animation table";
    uint32_t pos = unit + 5;
    for (uint32_t q = 0; q < nq; q++) {
        if (at + 96 > (uint64_t)n) return "it's cut short";
        uint32_t cnt = rd32(nab + at + 12), w1 = HW(pos + 1);
        if (w1 == 0xFFFFFFFFu || (w1 >> 16) != cnt) return "its animation tables aren't the size of the PS1's (as many animations for each bone)";
        if (at + 96 + (uint64_t)cnt * 8 > (uint64_t)n) return "it's cut short";
        for (uint32_t k = 0; k < cnt; k++) {
            uint32_t ctl = rd32(nab + at + 96 + 8 * k), start = rd32(nab + at + 100 + 8 * k);
            if (start > 0xFFFF || ctl > 0xFFFF) return "an animation starts past what the PS1 can point at";
            wr32((uint8_t *)h + 4 * (size_t)(pos + 6 + k), ctl << 16 | start);
        }
        at += 96 + (uint64_t)cnt * 8;
        pos += w1 & 0xFFFF;
    }
#undef HW
    return NULL;
}

static int16_t sat16(float f) {
    long v = lrintf(f);
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* The NAB's shape into the PS1's model (B: 2, then the vertices and
 * normals, then the HMD). The NAB's triangles are the HMD's textured
 * polygons (GT3; GT4 as two: corners 0,1,2 and 3,2,1) ordered by
 * material (the NAB's textures, each on its VRAM texture page), then by
 * block, then as in the HMD; untextured ones come last and aren't
 * shapes anyone edits. A corner gives its vertex's position (the PC's z
 * is the PS1's -z), its normal and its UV (in the material's texture,
 * the polygon's page counted from it). Only what differs from the disc's
 * is written, so a vertex shared by many corners takes the one that was
 * moved. NULL, or why not. */
static const char *nab_shape(const uint8_t *nab, long n, uint8_t *B, uint32_t nb, uint32_t timw, int *ntri, int *moved) {
    if (n < 8 || nb < 12 || rd32(B) != 2) return "the disc's model isn't laid out as expected";
    uint32_t nv = rd32(nab), o0 = rd32(B + 4), o1 = rd32(B + 8);
    if (o0 + 8 > o1 || o1 + 16 > nb) return "the disc's model isn't laid out as expected";
    uint32_t vb = o0 + rd32(B + o0) * 4, nbase = o0 + rd32(B + o0 + 4) * 4;
    if (vb > nbase || nbase > o1) return "the disc's model isn't laid out as expected";
    uint32_t nvert = (nbase - vb) / 8, nnorm = (o1 - nbase) / 8;
    /* the NAB's materials: texture page and size */
    struct { uint32_t page, w, h; } mat[8];
    uint64_t at = 8 + (uint64_t)nv * 36;
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nm = rd32(nab + at);
    at += 4;
    if (nm > 8) return "it has more materials than this reads";
    for (uint32_t m = 0; m < nm; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        uint32_t ntx = rd32(nab + at + 8);
        mat[m].page = rd32(nab + at + 4) & 0x1F;
        mat[m].w = mat[m].h = 0;
        at += 352;
        for (uint32_t t = 0; t < ntx; t++) {
            if (at + 16 > (uint64_t)n) return "it's cut short";
            if (!mat[m].w) { mat[m].w = rd32(nab + at + 8); mat[m].h = rd32(nab + at + 12); }
            at += 16 + (uint64_t)rd32(nab + at);
        }
    }
    uint8_t *was = malloc(nb);                   /* the disc's, to tell what the NAB changes */
    if (!was) return "out of memory";
    memcpy(was, B, nb);
    const uint8_t *h = B + o1;
    uint32_t hw = (nb - o1) / 4;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    /* the textured polygons, in the NAB's order */
    uint32_t tri = 0, nblk = HW(3);
    *moved = 0;
    for (uint32_t m = 0; m < nm; m++)
        for (uint32_t bi = 0; bi < nblk && bi < 256; bi++)
            for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 4096; p = HW(p), guard++) {
                uint32_t nt = HW(p + 2) & 0xFFFF, base = HW(HW(p + 1) + 1) & 0x7FFFFFFF;
                for (uint32_t t = 0; t < nt && t < 4096; t++) {
                    uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
                    if (ty & 0xFF000000u) break;
                    if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
                    int quad = (ty & 0x10) != 0;
                    for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF); k++) {
                        uint32_t w0 = base + idx + k * (quad ? 7 : 6);
                        if (w0 + 7 > hw) { free(was); return "the disc's model isn't laid out as expected"; }
                        uint8_t *a = (uint8_t *)h + 4 * w0;
                        uint32_t pg = (rd32(a + 4) >> 16) & 0x1F, best = nm;
                        for (uint32_t q = 0; q < nm; q++)   /* the material whose page it is on */
                            if ((mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF) &&
                                (best == nm || (mat[q].page & 0xF) > (mat[best].page & 0xF)))
                                best = q;
                        if (best != m) continue;
                        static const int corners[2][3] = {{0, 1, 2}, {3, 2, 1}};
                        for (int half = 0; half < (quad ? 2 : 1); half++, tri++) {
                            if (tri >= nv / 3) { free(was); return "it has fewer polygons than the PS1's model (they can be moved, not added or taken away)"; }
                            for (int c = 0; c < 3; c++) {
                                int corner = corners[half][c];
                                uint32_t ni, vi;
                                if (!quad) { ni = rd16(a + 12 + 4 * corner); vi = rd16(a + 14 + 4 * corner); }
                                else if (!corner) { ni = rd16(a + 10); vi = rd16(a + 14); }
                                else { ni = rd16(a + 16 + 4 * (corner - 1)); vi = rd16(a + 18 + 4 * (corner - 1)); }
                                if (vi >= nvert || ni >= nnorm) { free(was); return "the disc's model isn't laid out as expected"; }
                                float f[9];
                                memcpy(f, nab + 8 + (size_t)(3 * tri + c) * 36, sizeof f);
                                int16_t pos[3] = {sat16(f[0]), sat16(f[1]), sat16(-f[2])}, nor[3] = {sat16(f[3]), sat16(f[4]), sat16(-f[5])};
                                uint32_t ov = vb + 8 * vi, on = nbase + 8 * ni;
                                for (int x = 0; x < 3; x++) {
                                    if ((int16_t)rd16(was + ov + 2 * x) != pos[x]) { wr16(B + ov + 2 * x, (uint16_t)pos[x]); (*moved)++; }
                                    if ((int16_t)rd16(was + on + 2 * x) != nor[x]) wr16(B + on + 2 * x, (uint16_t)nor[x]);
                                }
                                uint32_t wm = !m && timw ? timw : mat[m].w, hm = mat[m].h ? mat[m].h : 256;
                                long u = lrintf(f[7] * (float)wm) - 64 * (long)((pg & 0xF) - (mat[m].page & 0xF)), v = lrintf(f[8] * (float)hm);
                                uint8_t nu = (uint8_t)(u < 0 ? 0 : u > 255 ? 255 : u), nv8 = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                                size_t ua = (size_t)(a - B) + 4 * (size_t)corner;
                                if (was[ua] != nu) a[4 * corner] = nu;
                                if (was[ua + 1] != nv8) a[4 * corner + 1] = nv8;
                            }
                        }
                    }
                }
            }
#undef HW
    free(was);
    *ntri = (int)tri;
    return NULL;
}

/* A NAB whose polygons aren't the PS1 model's (some added or taken away,
 * or moved to other body parts): the model built again from it. The
 * HMD stays as it is (its skeleton, animation, coordinates and untextured
 * polygons), and gets after its end a new unit for every block (body
 * part) with its textured polygons, the NAB's triangles (GT3, each on the
 * texture page its UVs need), the block pointing to it; the vertices and
 * normals they need are added after the disc's (shared ones once). The
 * model, the HMD part copied for each enemy, grows by the new triangles'
 * size: up to MODEL_GROWTH, so a room full of them still fits the
 * memory the game loads enemies into (0x80150000 up; the fullest room
 * measured leaves 213 KB). */
#define MODEL_GROWTH (modmem_enabled() ? (RAM_SIZE - RAM_GAME) : (48u * 1024))

typedef struct { uint32_t page, w, h, per; } NabMat;   /* per: pixels a texture page is wide (64 at 16 bits, 128 at 8) */
typedef struct { uint32_t start, count, node, mat; } NabGroup;   /* mat ~0u: untextured */

/* The NAB's materials and groups; NULL, or why not. */
static const char *nab_layout(const uint8_t *nab, long n, NabMat *mat, uint32_t *nm, NabGroup *grp, uint32_t *ng) {
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;
    if (n < 8 || at + 4 > (uint64_t)n) return "it's cut short";
    *nm = rd32(nab + at);
    at += 4;
    if (*nm > 8) return "it has more materials than this reads";
    for (uint32_t m = 0; m < *nm; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        uint32_t ntx = rd32(nab + at + 8);
        mat[m].page = rd32(nab + at + 4) & 0x1F;
        { uint32_t d = nab[at + 92]; mat[m].per = d == 8 ? 128 : d == 4 ? 256 : 64; }   /* (its first texture's depth) */
        mat[m].w = mat[m].h = 0;
        at += 352;
        for (uint32_t t = 0; t < ntx; t++) {
            if (at + 16 > (uint64_t)n) return "it's cut short";
            if (!mat[m].w) { mat[m].w = rd32(nab + at + 8); mat[m].h = rd32(nab + at + 12); }
            at += 16 + (uint64_t)rd32(nab + at);
        }
    }
    if (at + 4 > (uint64_t)n) return "it's cut short";
    *ng = rd32(nab + at);
    at += 4;
    if (*ng > 256) return "it has more groups than this reads";
    for (uint32_t g = 0; g < *ng; g++) {
        if (at + 24 > (uint64_t)n) return "it's cut short";
        uint32_t has = rd32(nab + at);
        grp[g].start = rd32(nab + at + 8);
        grp[g].count = rd32(nab + at + 12);
        at += 24;
        grp[g].mat = ~0u;
        if (has) { grp[g].mat = rd32(nab + at); at += 4; }
        grp[g].node = rd32(nab + at);
        at += 4;
        if ((uint64_t)grp[g].start + grp[g].count > rd32(nab) / 3) return "a group has triangles past its end";
        if (grp[g].mat != ~0u && grp[g].mat >= *nm) return "a group's material isn't one of its own";
    }
    return NULL;
}

/* The model built again from the NAB; the new model (malloc'd) and its
 * size, or NULL and why. */
static uint8_t *nab_rebuild(const uint8_t *nab, long n, const uint8_t *B, uint32_t nb, uint32_t timw, uint32_t *outn, int *ntri,
                            const char **why) {
    static NabMat mat[8];
    static NabGroup grp[256];
    uint32_t nm = 0, ng = 0;
    if ((*why = nab_layout(nab, n, mat, &nm, grp, &ng))) return NULL;
    *why = "the disc's model isn't laid out as expected";
    if (nb < 12 || rd32(B) != 2) return NULL;
    uint32_t o0 = rd32(B + 4), o1 = rd32(B + 8);
    if (o0 + 8 > o1 || o1 + 16 > nb) return NULL;
    uint32_t vb = o0 + rd32(B + o0) * 4, nbase = o0 + rd32(B + o0 + 4) * 4;
    if (vb > nbase || nbase > o1) return NULL;
    uint32_t nS = (nbase - vb) / 8, nN = (o1 - nbase) / 8, hw = (nb - o1) / 4;
    const uint8_t *h = B + o1;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    uint32_t nblk = HW(3), hdr = 0, base = 0;
    if (nblk > 64) return NULL;
    /* each material's texture window bits (from a polygon of the disc's that uses it) */
    uint32_t tsb[8] = {0}, cba[8] = {0};
    int seen[8] = {0};
    for (uint32_t bi = 0; bi < nblk; bi++) {
        uint32_t p = HW(4 + bi);
        if (!p || p == 0xFFFFFFFFu) continue;
        uint32_t nt = HW(p + 2) & 0xFFFF, b0 = HW(HW(p + 1) + 1) & 0x7FFFFFFF;
        for (uint32_t t = 0; t < nt && t < 4096; t++) {
            uint32_t ty = HW(p + 3 + 3 * t), idx = HW(p + 5 + 3 * t);
            if (ty & 0xFF000000u) break;
            if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
            hdr = HW(p + 1);
            base = b0;
            uint32_t w1 = HW(b0 + idx + 1), pg = (w1 >> 16) & 0x1F;
            for (uint32_t q = 0; q < nm; q++)
                if (!seen[q] && (mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF)) {
                    seen[q] = 1;
                    tsb[q] = w1 >> 16;
                    cba[q] = HW(b0 + idx) >> 16;
                }
        }
    }
    if (!hdr) return NULL;
    for (uint32_t q = 0; q < nm; q++)
        if (!seen[q]) tsb[q] = (tsb[0] & ~0x1Fu) | mat[q].page;   /* (a material none of the disc's polygons used) */
    /* the new vertices and normals after the disc's, and the new HMD after the disc's */
    uint32_t total = 0;
    for (uint32_t g = 0; g < ng; g++) if (grp[g].mat != ~0u) total += grp[g].count;
    size_t cap = (size_t)nb + (size_t)total * 32 + (size_t)nblk * 12 + (size_t)total * 12 + 4096;
    uint8_t *S = malloc(((size_t)nS + total * 3) * 8), *N = malloc(((size_t)nN + total * 3) * 8), *H = malloc(cap);
    uint32_t *hash = malloc(2 * 65536 * 4);
    if (!S || !N || !H || !hash) { free(S); free(N); free(H); free(hash); *why = "out of memory"; return NULL; }
    memcpy(S, B + vb, (size_t)nS * 8);
    memcpy(N, B + nbase, (size_t)nN * 8);
    uint32_t cs = nS, cn = nN;
    memset(hash, 0xFF, 2 * 65536 * 4);
    /* hash[0..65535]: vertices, [65536..]: normals; slot = 1 + index, linear probing */
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < (pass ? nN : nS); i++) {
            const uint8_t *s = (pass ? N : S) + 8 * i;
            uint32_t k = (rd16(s) * 73856093u ^ rd16(s + 2) * 19349663u ^ rd16(s + 4) * 83492791u) & 65535;
            while (hash[pass * 65536 + k] != 0xFFFFFFFFu) k = (k + 1) & 65535;
            hash[pass * 65536 + k] = i;
        }
    memcpy(H, h, (size_t)hw * 4);
    uint32_t hn = hw;                            /* words in the new HMD */
    /* the words the disc's textured polygons take are free to use again (not their units' words: a
     * unit put there isn't drawn) */
    uint8_t *fw = calloc(hw + 1, 1);
    uint32_t (*ent)[3] = malloc(sizeof *ent * 8192);
    if (!fw || !ent) { free(S); free(N); free(H); free(hash); free(fw); free(ent); *why = "out of memory"; return NULL; }
    for (uint32_t bi = 0; bi < nblk; bi++) {
        uint32_t p = HW(4 + bi);
        if (!p || p == 0xFFFFFFFFu || HW(p) != 0xFFFFFFFFu) continue;   /* (one unit to a block, as the disc's are) */
        uint32_t nt = HW(p + 2) & 0xFFFF, b0 = HW(HW(p + 1) + 1) & 0x7FFFFFFF, other = 0;
        for (uint32_t t = 0; t < nt && t < 4096; t++) {
            uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
            if (ty & 0xFF000000u) { other = 1; break; }
            if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
            uint32_t pw = ty & 0x10 ? 7 : 6;
            for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF) * pw; k++)
                if (b0 + idx + k < hw) fw[b0 + idx + k] = 1;
        }
        (void)other;
    }
    *ntri = 0;
    for (uint32_t bi = 0; bi < nblk; bi++) {
        uint32_t p = HW(4 + bi), keep[64][3], nk = 0, flag = 0;
        int had = 0;
        if (p && p != 0xFFFFFFFFu) {
            uint32_t nt = HW(p + 2) & 0xFFFF;
            for (uint32_t t = 0; t < nt && t < 4096; t++) {
                uint32_t ty = HW(p + 3 + 3 * t);
                if (ty & 0xFF000000u) { nk = 99; break; }    /* not a unit of polygons: left as it is */
                if ((ty & 0xFF) == 0x0D || (ty & 0xFF) == 0x15) { had = 1; flag |= ty & 0x800000; continue; }
                if (nk < 64) { keep[nk][0] = ty; keep[nk][1] = HW(p + 4 + 3 * t); keep[nk][2] = HW(p + 5 + 3 * t); nk++; }
            }
        }
        if (nk == 99) continue;
        uint32_t count = 0, ne = 0;
        for (uint32_t g = 0; g < ng; g++) if (grp[g].mat != ~0u && grp[g].node == bi) count += grp[g].count;
        if (!count && !had) continue;
        /* the polygons: two triangles that were a quad (corners a,b,c and d,c,b) a GT4 again, the rest GT3 */
        for (uint32_t g = 0; g < ng; g++) {
            if (grp[g].mat == ~0u || grp[g].node != bi) continue;
            const NabMat *mm = &mat[grp[g].mat];
            uint32_t wm = !grp[g].mat && timw ? timw : mm->w, hm2 = mm->h ? mm->h : 256;
            for (uint32_t t = grp[g].start; t < grp[g].start + grp[g].count;) {
                const uint8_t *ta = nab + 8 + (size_t)3 * t * 36, *tb = ta + 3 * 36;
                int quad = t + 1 < grp[g].start + grp[g].count && !memcmp(tb + 36, ta + 72, 36) && !memcmp(tb + 72, ta + 36, 36);
                int nc = quad ? 4 : 3;
                const uint8_t *cor[4] = {ta, ta + 36, ta + 72, tb};
                long u[4], v[4], lo = 1 << 30;
                uint32_t vi[4], ni[4];
                for (int c = 0; c < nc; c++) {
                    float f[9];
                    memcpy(f, cor[c], sizeof f);
                    int16_t pos[3] = {sat16(f[0]), sat16(f[1]), sat16(-f[2])}, nor[3] = {sat16(f[3]), sat16(f[4]), sat16(-f[5])};
                    for (int pass = 0; pass < 2; pass++) {
                        const int16_t *x = pass ? nor : pos;
                        uint8_t *arr = pass ? N : S;
                        uint32_t *cnt = pass ? &cn : &cs;
                        uint32_t k = ((uint16_t)x[0] * 73856093u ^ (uint16_t)x[1] * 19349663u ^ (uint16_t)x[2] * 83492791u) & 65535, found = ~0u;
                        while (hash[pass * 65536 + k] != 0xFFFFFFFFu) {
                            const uint8_t *q = arr + 8 * hash[pass * 65536 + k];
                            if ((int16_t)rd16(q) == x[0] && (int16_t)rd16(q + 2) == x[1] && (int16_t)rd16(q + 4) == x[2]) { found = hash[pass * 65536 + k]; break; }
                            k = (k + 1) & 65535;
                        }
                        if (found == ~0u) {
                            if (*cnt >= 65535) { free(S); free(N); free(hash); free(H); free(fw); free(ent); *why = "too many vertices"; return NULL; }
                            found = (*cnt)++;
                            uint8_t *q = arr + 8 * found;
                            wr16(q, (uint16_t)x[0]); wr16(q + 2, (uint16_t)x[1]); wr16(q + 4, (uint16_t)x[2]); wr16(q + 6, 0);
                            hash[pass * 65536 + k] = found;
                        }
                        (pass ? ni : vi)[c] = found;
                    }
                    u[c] = lrintf(f[7] * (float)wm);
                    v[c] = lrintf(f[8] * (float)hm2);
                    if (u[c] < lo) lo = u[c];
                }
                long off = lo < 0 ? 0 : lo / (long)mm->per;   /* the texture page from where its UVs start */
                if ((mm->page & 0xF) + off > 15) off = 15 - (mm->page & 0xF);
                /* its place: the first free run it fits in, else after the end */
                uint32_t pw = quad ? 7 : 6, at = 0, run = 0;
                for (uint32_t w = base; w < hw && run < pw; w++) {   /* (after the polygons' start: their index is counted from it) */
                    if (!fw[w]) { run = 0; continue; }
                    if (!run) at = w;
                    run++;
                }
                if (run < pw) { at = hn; hn += pw; }
                else memset(fw + at, 0, pw);
                uint8_t *pk = H + 4 * (size_t)at;
                memset(pk, 0, 4 * pw);
                for (int c = 0; c < nc; c++) {
                    long uu = u[c] - (long)mm->per * off, vv = v[c];
                    pk[4 * c] = (uint8_t)(uu < 0 ? 0 : uu > 255 ? 255 : uu);
                    pk[4 * c + 1] = (uint8_t)(vv < 0 ? 0 : vv > 255 ? 255 : vv);
                }
                wr16(pk + 2, (uint16_t)cba[grp[g].mat]);
                wr16(pk + 6, (uint16_t)((tsb[grp[g].mat] & ~0x1Fu) | (mm->page & 0x10) | ((mm->page & 0xF) + (uint32_t)off)));
                if (quad) {
                    wr16(pk + 10, (uint16_t)ni[0]);
                    wr16(pk + 14, (uint16_t)vi[0]);
                    for (int c = 1; c < 4; c++) { wr16(pk + 16 + 4 * (c - 1), (uint16_t)ni[c]); wr16(pk + 18 + 4 * (c - 1), (uint16_t)vi[c]); }
                } else
                    for (int c = 0; c < 3; c++) { wr16(pk + 12 + 4 * c, (uint16_t)ni[c]); wr16(pk + 14 + 4 * c, (uint16_t)vi[c]); }
                uint32_t ty = quad ? 0x15 : 0x0D;     /* the type's entry: a run of packets one after the other */
                if (ne && ent[ne - 1][0] == ty && ent[ne - 1][1] + ent[ne - 1][2] * pw == at) ent[ne - 1][2]++;
                else if (ne < 8192) { ent[ne][0] = ty; ent[ne][1] = at; ent[ne][2] = 1; ne++; }
                *ntri += quad ? 2 : 1;
                t += quad ? 2 : 1;
            }
        }
        /* the block's new unit, after the end */
        uint32_t nt = nk + ne;
        if (!nt) { wr32(H + 4 * (4 + bi), 0); continue; }
        uint32_t uw = 3 + 3 * nt, unit = hn;
        hn += uw;
        uint8_t *u8 = H + 4 * (size_t)unit;
        wr32(u8, p && p != 0xFFFFFFFFu ? HW(p) : 0xFFFFFFFFu);   /* (the block's next unit, if it had one) */
        wr32(u8 + 4, p && p != 0xFFFFFFFFu ? HW(p + 1) : hdr);
        wr32(u8 + 8, 0x80000000u | nt);
        uint32_t e = 12;
        for (uint32_t k = 0; k < ne; k++, e += 12) {
            wr32(u8 + e, ent[k][0] | (k ? 0 : flag));
            wr32(u8 + e + 4, 0x80000002u | ent[k][2] << 16);
            wr32(u8 + e + 8, ent[k][1] - base);
        }
        for (uint32_t k = 0; k < nk; k++, e += 12) { wr32(u8 + e, keep[k][0]); wr32(u8 + e + 4, keep[k][1]); wr32(u8 + e + 8, keep[k][2]); }
        wr32(H + 4 * (4 + bi), unit);
    }
    free(fw);
    free(ent);
#undef HW
    free(hash);
    if ((hn - hw) * 4 > MODEL_GROWTH) {
        free(S); free(N); free(H);
        static char b[160];
        snprintf(b, sizeof b, "the model would grow by %u bytes, more than the %u each enemy of it can take in memory", (hn - hw) * 4, MODEL_GROWTH);
        *why = b;
        return NULL;
    }
    /* B: 2, then the vertices and normals (their header: where each starts), then the HMD */
    uint32_t b0n = 8 + cs * 8 + cn * 8;
    size_t total_n = 12 + (size_t)b0n + (size_t)hn * 4;
    uint8_t *o = malloc(total_n);
    if (!o) { free(S); free(N); free(H); *why = "out of memory"; return NULL; }
    wr32(o, 2);
    wr32(o + 4, 12);
    wr32(o + 8, 12 + b0n);
    wr32(o + 12, 2);
    wr32(o + 16, 2 + cs * 2);
    memcpy(o + 20, S, (size_t)cs * 8);
    memcpy(o + 20 + (size_t)cs * 8, N, (size_t)cn * 8);
    memcpy(o + 12 + b0n, H, (size_t)hn * 4);
    free(S); free(N); free(H);
    *outn = (uint32_t)total_n;
    *why = NULL;
    return o;
}

/* Whether the NAB has the PS1 model's textured polygons, block by block
 * and material by material (then it goes in place, nab_shape). */
static int nab_same_layout(const uint8_t *nab, long n, const uint8_t *B, uint32_t nb) {
    static NabMat mat[8];
    static NabGroup grp[256];
    uint32_t nm = 0, ng = 0;
    if (nab_layout(nab, n, mat, &nm, grp, &ng) || nb < 12 || rd32(B) != 2) return 0;
    uint32_t o1 = rd32(B + 8);
    if (o1 + 16 > nb) return 0;
    const uint8_t *h = B + o1;
    uint32_t hw = (nb - o1) / 4;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    static uint32_t ps1[64][8], pc[64][8];
    memset(ps1, 0, sizeof ps1);
    memset(pc, 0, sizeof pc);
    uint32_t nblk = HW(3);
    if (nblk > 64) return 0;
    for (uint32_t bi = 0; bi < nblk; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 4096; p = HW(p), guard++) {
            uint32_t nt = HW(p + 2) & 0xFFFF, base = HW(HW(p + 1) + 1) & 0x7FFFFFFF;
            for (uint32_t t = 0; t < nt && t < 4096; t++) {
                uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
                if (ty & 0xFF000000u) break;
                if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
                int quad = (ty & 0x10) != 0;
                for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF); k++) {
                    uint32_t pg = (HW(base + idx + k * (quad ? 7 : 6) + 1) >> 16) & 0x1F, best = nm;
                    for (uint32_t q = 0; q < nm; q++)
                        if ((mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF) &&
                            (best == nm || (mat[q].page & 0xF) > (mat[best].page & 0xF)))
                            best = q;
                    if (best < nm) ps1[bi][best] += quad ? 2 : 1;
                }
            }
        }
#undef HW
    for (uint32_t g = 0; g < ng; g++)
        if (grp[g].mat != ~0u) {
            if (grp[g].node >= 64) return 0;
            pc[grp[g].node][grp[g].mat] += grp[g].count;
        }
    return !memcmp(ps1, pc, sizeof ps1);
}

/* The NAB's textures into the PS1's 16-bit TIMs (in order, same height). */
static const char *nab_into(const uint8_t *nab, long n, uint8_t *tex, uint32_t nt, int *ntex) {
    if (n < 8) return "it's cut short";
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nm = rd32(nab + at);
    at += 4;
    uint32_t used = 0;                           /* TIMs taken, by offset order */
    for (uint32_t m = 0; m < nm; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        uint32_t ntx = rd32(nab + at + 8);
        at += 352;
        for (uint32_t t = 0; t < ntx; t++) {
            if (at + 16 > (uint64_t)n) return "it's cut short";
            uint32_t size = rd32(nab + at), W = rd32(nab + at + 8), H = rd32(nab + at + 12);
            const uint8_t *px = nab + at + 16;
            at += 16 + (uint64_t)size;
            if (at > (uint64_t)n || (uint64_t)W * H * 2 > size) return "it's cut short";
            uint32_t o = 0, k = 0;               /* the next 16-bit TIM of that height */
            uint8_t *dst = NULL;
            uint32_t w = 0;
            for (; o + 20 <= nt && rd32(tex + o) == 0x10; k++) {
                uint32_t flag = rd32(tex + o + 4), j = o + 8;
                if (flag & 8) j += rd32(tex + j);
                if (j + 12 > nt) break;
                uint32_t il = rd32(tex + j), iw = rd16(tex + j + 8), ih = rd16(tex + j + 10);
                if (!dst && k >= used && (flag & 3) == 2 && ih == H && iw <= W && iw && j + 12 + iw * ih * 2 <= nt) {
                    dst = tex + j + 12;
                    w = iw;
                    used = k + 1;
                }
                o = j + il;
            }
            if (!dst) continue;                  /* a texture the PS1 has elsewhere */
            for (uint32_t y = 0; y < H; y++)
                for (uint32_t x = 0; x < w; x++) {
                    uint16_t p = (uint16_t)rd16(px + 2 * ((size_t)y * W + (x * W + w - 1) / w));
                    uint16_t was = (uint16_t)rd16(dst + 2 * ((size_t)y * w + x));
                    uint16_t v = (uint16_t)(((p >> 10) & 31) | ((p >> 5) & 31) << 5 | (p & 31) << 10);
                    if (v == (was & 0x7FFF)) v = was;                            /* as it was */
                    else if (!(p & 0x8000)) v = 0;                               /* see-through */
                    else v = (uint16_t)((v ? v : 0x0400) | (was & 0x8000));
                    wr16(dst + 2 * ((size_t)y * w + x), v);
                }
            (*ntex)++;
        }
    }
    return NULL;
}

/* The first 16-bit TIM of an enemy's textures (its skin), or NULL. */
static uint8_t *enemy_skin(uint8_t *tex, uint32_t nt) {
    for (uint32_t o = 0; o + 20 <= nt && rd32(tex + o) == 0x10;) {
        uint32_t flag = rd32(tex + o + 4), j = o + 8;
        if (flag & 8) j += rd32(tex + j);
        if (j + 12 > nt) break;
        if ((flag & 3) == 2) return tex + o;
        o = j + rd32(tex + j);
    }
    return NULL;
}

/* A boss's shape (types 31-33: one HMD, EV_OBJ.DAT) from its NAB, in
 * place. Its textured polygons are of two kinds: each bone's own (as the
 * other enemies', from the unit's header: 4, polygons, vertices,
 * normals), and the shared skin (type bit 0x1000000; header: 5,
 * polygons, vertices, a buffer, normals, a buffer) whose vertices each
 * belong to a bone and are kept in that bone's space. The NAB's
 * triangles: for each material the skin's, then the bones', each in the
 * HMD's order (GT4 as two). The skin's corners are in the model's space
 * in the NAB: back through their bone's rest matrix (the NAB node's 3rd
 * 4x4 matrix; the bone from the NAB's per-vertex data), z negated. Only
 * what differs from where the disc's data lands is written. A material
 * that is a sheet of small textures (type 33's) has each one's place in
 * the page in its record, and the NAB's UVs count from it, in its used
 * size; a polygon's texture is the one the disc's UVs are in. NULL, or
 * why not. */
static const char *boss_shape(const uint8_t *nab, long n, uint8_t *h, uint32_t hw, int parts, int *ntri, int *moved, int *keptuv,
                              int *changed) {   /* parts: 1 the skin, 2 the bones' polygons */
    static NabMat nmat[8];
    static NabGroup grp[256];
    uint32_t nmg = 0, ng = 0;
    const char *bad = nab_layout(nab, n, nmat, &nmg, grp, &ng);
    if (bad) return bad;
    uint32_t gi[8][65], go[8][65];              /* each (material, node)'s next group and how far into it */
    memset(gi, 0, sizeof gi);
    memset(go, 0, sizeof go);
    uint32_t nv = rd32(nab), nf = rd32(nab + 4);
    uint64_t at = 8 + (uint64_t)nv * 36;
    if (n < 8 || at + 4 > (uint64_t)n) return "it's cut short";
    struct { uint32_t page, w, h, nt, bpp; int32_t rect[10][4]; } mat[8];   /* rect: a sheet's textures (u, v, w, h in the page) */
    uint32_t nm = rd32(nab + at);
    at += 4;
    if (nm > 8) return "it has more materials than this reads";
    for (uint32_t m = 0; m < nm; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        mat[m].page = rd32(nab + at + 4) & 0x1F;
        mat[m].nt = rd32(nab + at + 8);
        uint32_t b = nab[at + 92];               /* its first texture's depth */
        mat[m].bpp = b == 4 || b == 8 ? b : 16;
        mat[m].w = mat[m].h = 0;
        for (uint32_t t = 0; t < mat[m].nt && t < 10; t++) {   /* each texture's place: VRAM x, y, depth, used width, height */
            uint32_t x = rd16(nab + at + 12 + 2 * t), y = rd16(nab + at + 52 + 2 * t), d = nab[at + 92 + t];
            d = d == 4 || d == 8 ? d : 16;
            mat[m].rect[t][0] = (int32_t)((x - (mat[m].page & 0xF) * 64) * (16 / d));
            mat[m].rect[t][1] = (int32_t)(y - ((mat[m].page >> 4) & 1) * 256);
            mat[m].rect[t][2] = (int32_t)rd32(nab + at + 192 + 4 * t);
            mat[m].rect[t][3] = (int32_t)rd32(nab + at + 272 + 4 * t);
        }
        at += 352;
        for (uint32_t t = 0; t < mat[m].nt; t++) {
            if (at + 16 > (uint64_t)n) return "it's cut short";
            if (!mat[m].w) { mat[m].w = rd32(nab + at + 8); mat[m].h = rd32(nab + at + 12); }
            at += 16 + (uint64_t)rd32(nab + at);
        }
    }
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t ng2 = rd32(nab + at);
    at += 4;
    for (uint32_t g = 0; g < ng2 && g < 4096; g++) {
        if (at + 28 > (uint64_t)n) return "it's cut short";
        at += 24 + (rd32(nab + at) ? 4 : 0) + 4;
    }
    uint64_t aux = at;                           /* per vertex: 15 words, the 1st's top half its bone */
    at += (uint64_t)nv * 60 + (uint64_t)nf * 20;
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nq = rd32(nab + at);
    at += 4;
    for (uint32_t q = 0; q < nq && q < 256; q++) {
        if (at + 96 > (uint64_t)n) return "it's cut short";
        at += 96 + (uint64_t)rd32(nab + at + 12) * 8;
    }
    at += 4;                                     /* (a value), then two lists */
    for (int l = 0; l < 2; l++) {
        if (at + 4 > (uint64_t)n) return "it's cut short";
        uint32_t c = rd32(nab + at);
        at += 4;
        for (uint32_t e = 0; e < c && e < 65536; e++) {
            if (at + 12 > (uint64_t)n) return "it's cut short";
            at += 12 + (uint64_t)rd32(nab + at + 4) * 12;
        }
    }
    at += 12;
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nnode = rd32(nab + at) + 1;
    at += 4;
    uint64_t nodes = at;
    if (nodes + (uint64_t)nnode * 264 > (uint64_t)n) return "it's cut short";
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    uint8_t *was = malloc((size_t)hw * 4);
    if (!was) return "out of memory";
    memcpy(was, h, (size_t)hw * 4);
    uint32_t tri = 0, used = 0;
    *moved = *keptuv = 0;
    for (uint32_t m = 0; m < nm; m++)
        for (int shared = 1; shared >= 0; shared--)
            for (uint32_t bi = 0; bi < HW(3) && bi < 64; bi++)
                for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64; p = HW(p), guard++) {
                    uint32_t nt = HW(p + 2) & 0xFFFF, hdr = HW(p + 1), base = HW(hdr + 1) & 0x7FFFFFFF;
                    for (uint32_t t = 0; t < nt && t < 4096; t++) {
                        uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
                        if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
                        if (ty & 0xFE000000u || !(ty & 0x1000000) != !shared) continue;
                        int quad = (ty & 0x10) != 0;
                        uint32_t vs = HW(hdr + 2) & 0x7FFFFFFF, ns = HW(hdr + (shared ? 4 : 3)) & 0x7FFFFFFF;
                        for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF); k++) {
                            uint32_t w0 = base + idx + k * (quad ? 7 : 6);
                            if (w0 + 7 > hw) { memcpy(h, was, (size_t)hw * 4); free(was); return "the disc's model isn't laid out as expected"; }
                            uint8_t *a = h + 4 * w0;
                            uint32_t pg = (rd32(a + 4) >> 16) & 0x1F, best = nm;
                            for (uint32_t q = 0; q < nm; q++)
                                if ((mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF) &&
                                    (best == nm || (mat[q].page & 0xF) > (mat[best].page & 0xF)))
                                    best = q;
                            if (best != m) continue;
                            if (!(parts & (shared ? 1 : 2))) continue;
                            static const int corners[2][3] = {{0, 1, 2}, {3, 2, 1}};
                            uint32_t node = shared ? 0 : bi;
                            for (int half = 0; half < (quad ? 2 : 1); half++) {
                                /* the NAB's next triangle of this material and node */
                                while (gi[m][node] < ng && (grp[gi[m][node]].mat != m || grp[gi[m][node]].node != node ||
                                                            go[m][node] >= grp[gi[m][node]].count)) {
                                    gi[m][node]++;
                                    go[m][node] = 0;
                                }
                                if (gi[m][node] >= ng) { memcpy(h, was, (size_t)hw * 4); free(was); return "it has fewer polygons than the PS1's model (for the bosses' skin they can be moved, not added or taken away)"; }
                                tri = grp[gi[m][node]].start + go[m][node]++;
                                used++;
                                for (int c = 0; c < 3; c++) {
                                    int corner = corners[half][c];
                                    uint32_t ni, vi;
                                    if (!quad) { ni = rd16(a + 12 + 4 * corner); vi = rd16(a + 14 + 4 * corner); }
                                    else if (!corner) { ni = rd16(a + 10); vi = rd16(a + 14); }
                                    else { ni = rd16(a + 16 + 4 * (corner - 1)); vi = rd16(a + 18 + 4 * (corner - 1)); }
                                    if (vs + 2 * vi + 2 > hw || ns + 2 * ni + 2 > hw) { memcpy(h, was, (size_t)hw * 4); free(was); return "the disc's model isn't laid out as expected"; }
                                    uint32_t ci = 3 * tri + (uint32_t)c;
                                    float f[9];
                                    memcpy(f, nab + 8 + (size_t)ci * 36, sizeof f);
                                    uint8_t *pv = h + 4 * (size_t)vs + 8 * vi, *pn = h + 4 * (size_t)ns + 8 * ni;
                                    const uint8_t *ov = was + 4 * (size_t)vs + 8 * vi, *on = was + 4 * (size_t)ns + 8 * ni;
                                    int16_t pos[3], nor[3];
                                    if (!shared) {
                                        pos[0] = sat16(f[0]); pos[1] = sat16(f[1]); pos[2] = sat16(-f[2]);
                                        nor[0] = sat16(f[3]); nor[1] = sat16(f[4]); nor[2] = sat16(-f[5]);
                                    } else {
                                        uint32_t bone = rd32(nab + aux + (uint64_t)ci * 60) >> 16;
                                        if (bone >= nnode) { memcpy(h, was, (size_t)hw * 4); free(was); return "a vertex's bone isn't one of its own"; }
                                        float M[16];
                                        memcpy(M, nab + nodes + (uint64_t)bone * 264 + 128, sizeof M);
                                        /* where the disc's lands: M (x, y, -z) + t; the same for the normal, no t */
                                        int same_p = 1, same_n = 1;
                                        for (int r = 0; r < 3; r++) {
                                            float lp = 0, ln = 0;
                                            for (int x = 0; x < 3; x++) {
                                                float sp = (float)(int16_t)rd16(ov + 2 * x), sn = (float)(int16_t)rd16(on + 2 * x);
                                                if (x == 2) { sp = -sp; sn = -sn; }
                                                lp += M[4 * r + x] * sp;
                                                ln += M[4 * r + x] * sn;
                                            }
                                            lp += M[4 * r + 3];
                                            if (lrintf(lp) != lrintf(f[r])) same_p = 0;
                                            if (lrintf(ln) != lrintf(f[3 + r])) same_n = 0;
                                        }
                                        /* back: M^-1 (p - t), z negated */
                                        double A[9], inv[9];
                                        for (int x = 0; x < 9; x++) A[x] = M[4 * (x / 3) + x % 3];
                                        double det = A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]);
                                        if (fabs(det) < 1e-9) { memcpy(h, was, (size_t)hw * 4); free(was); return "a bone's rest matrix can't be turned back"; }
                                        inv[0] = (A[4] * A[8] - A[5] * A[7]) / det; inv[1] = (A[2] * A[7] - A[1] * A[8]) / det; inv[2] = (A[1] * A[5] - A[2] * A[4]) / det;
                                        inv[3] = (A[5] * A[6] - A[3] * A[8]) / det; inv[4] = (A[0] * A[8] - A[2] * A[6]) / det; inv[5] = (A[2] * A[3] - A[0] * A[5]) / det;
                                        inv[6] = (A[3] * A[7] - A[4] * A[6]) / det; inv[7] = (A[1] * A[6] - A[0] * A[7]) / det; inv[8] = (A[0] * A[4] - A[1] * A[3]) / det;
                                        double dp[3] = {f[0] - M[3], f[1] - M[7], f[2] - M[11]};
                                        for (int r = 0; r < 3; r++) {
                                            double lp = inv[3 * r] * dp[0] + inv[3 * r + 1] * dp[1] + inv[3 * r + 2] * dp[2];
                                            double ln = inv[3 * r] * f[3] + inv[3 * r + 1] * f[4] + inv[3 * r + 2] * f[5];
                                            if (r == 2) { lp = -lp; ln = -ln; }
                                            pos[r] = same_p ? (int16_t)rd16(ov + 2 * r) : sat16((float)lp);
                                            nor[r] = same_n ? (int16_t)rd16(on + 2 * r) : sat16((float)ln);
                                        }
                                    }
                                    for (int x = 0; x < 3; x++) {
                                        if ((int16_t)rd16(ov + 2 * x) != pos[x]) { wr16(pv + 2 * x, (uint16_t)pos[x]); (*moved)++; }
                                        if ((int16_t)rd16(on + 2 * x) != nor[x]) wr16(pn + 2 * x, (uint16_t)nor[x]);
                                    }
                                    long u, v;
                                    if (mat[m].nt > 1) {     /* a sheet: the texture the disc's UVs are in, the NAB's counted in it */
                                        uint32_t nc = quad ? 4 : 3, tx = 10;
                                        for (uint32_t q = 0; q < mat[m].nt && q < 10 && tx == 10; q++) {
                                            int in = 1;
                                            for (uint32_t cc = 0; cc < nc && in; cc++) {
                                                int32_t ou = was[(size_t)(a - h) + 4 * cc], ov8 = was[(size_t)(a - h) + 4 * cc + 1];
                                                in = ou >= mat[m].rect[q][0] && ou <= mat[m].rect[q][0] + mat[m].rect[q][2] &&
                                                     ov8 >= mat[m].rect[q][1] && ov8 <= mat[m].rect[q][1] + mat[m].rect[q][3];
                                            }
                                            if (in) tx = q;
                                        }
                                        if (tx == 10) { *keptuv = 1; continue; }
                                        u = mat[m].rect[tx][0] + lrintf(f[7] * (float)mat[m].rect[tx][2]);
                                        v = mat[m].rect[tx][1] + lrintf(f[8] * (float)mat[m].rect[tx][3]);
                                    } else {
                                        uint32_t per = 64 * (16 / mat[m].bpp);   /* pixels a texture page is wide */
                                        u = lrintf(f[7] * (float)mat[m].w) - (long)per * (long)((pg & 0xF) - (mat[m].page & 0xF));
                                        v = lrintf(f[8] * (float)(mat[m].h ? mat[m].h : 256));
                                    }
                                    uint8_t nu = (uint8_t)(u < 0 ? 0 : u > 255 ? 255 : u), nv8 = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                                    size_t ua = (size_t)(a - h) + 4 * (size_t)corner;
                                    if (was[ua] != nu) a[4 * corner] = nu;
                                    if (was[ua + 1] != nv8) a[4 * corner + 1] = nv8;
                                }
                            }
                        }
                    }
                }
#undef HW
    *ntri = (int)used;
    uint32_t want = 0;
    for (uint32_t g = 0; g < ng; g++) if (grp[g].mat != ~0u && (parts & (grp[g].node ? 2 : 1))) want += grp[g].count;
    if (used != want) {
        memcpy(h, was, (size_t)hw * 4);
        free(was);
        return "it has more polygons than the PS1's model (for the bosses' skin they can be moved, not added or taken away)";
    }
    *changed = memcmp(h, was, (size_t)hw * 4) != 0;
    free(was);
    return NULL;
}

/* A boss's textures (types 31-33) from its NAB, into the 8-bit TIMs that
 * have copies of them elsewhere on the disc (modfs_boss.h): each part of
 * one in every copy (the boss's own are in its model: boss_images). A palette row only that texture is drawn with is
 * made again for it (median cut); a shared one keeps its colours, the
 * nearest taken. Pixels as the disc's stay as they were; see-through
 * where the NAB's alpha is 0. The number of textures written, or -1 and
 * why. */
static int boss_textures(int type, const uint8_t *nab, long n, const char **why) {
    /* the NAB's textures, by material and number */
    struct { uint32_t mat, tex, w, h; const uint8_t *px; } tx[64];
    uint32_t ntx = 0;
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;
    if (n < 8 || at + 4 > (uint64_t)n) { *why = "it's cut short"; return -1; }
    uint32_t nm = rd32(nab + at);
    at += 4;
    for (uint32_t m = 0; m < nm && m < 64; m++) {
        if (at + 352 > (uint64_t)n) { *why = "it's cut short"; return -1; }
        uint32_t nt = rd32(nab + at + 8);
        at += 352;
        for (uint32_t t = 0; t < nt; t++) {
            if (at + 16 > (uint64_t)n) { *why = "it's cut short"; return -1; }
            uint32_t size = rd32(nab + at), W = rd32(nab + at + 8), H = rd32(nab + at + 12);
            if (at + 16 + size > (uint64_t)n || (uint64_t)W * H * 2 > size) { *why = "it's cut short"; return -1; }
            if (ntx < 64) tx[ntx++] = (typeof(tx[0])){m, t, W, H, nab + at + 16};
            at += 16 + (uint64_t)size;
        }
    }
    /* the TIMs as they are on the disc (checked before any is changed) */
    int ok[sizeof modfs_boss / sizeof modfs_boss[0]];
    uint8_t *tim[sizeof modfs_boss / sizeof modfs_boss[0]];
    Touched *test = touch(disc_index("TEST.BIN"));
    for (size_t i = 0; i < sizeof modfs_boss / sizeof modfs_boss[0]; i++) {
        ok[i] = 0;
        tim[i] = NULL;
        if (modfs_boss[i].type != type || !test) continue;
        int fi = disc_index(modfs_boss[i].disc);
        Touched *t = touch(fi);
        uint32_t tab = archive_table(fi);
        if (!t || !tab) continue;
        uint32_t start = rd32(test->data + tab - TEST_BASE + 4 * modfs_boss[i].block) * 2048 + modfs_boss[i].offset;
        if (start + modfs_boss[i].len > t->nsec * 2048) continue;
        tim[i] = t->data + start;
        ok[i] = (uint32_t)mz_crc32(0, tim[i], modfs_boss[i].len) == modfs_boss[i].crc;
    }
    int done = 0;
    for (size_t i = 0; i < sizeof modfs_boss / sizeof modfs_boss[0]; i++) {
        if (!ok[i]) continue;
        const typeof(tx[0]) *src = NULL;
        for (uint32_t k = 0; k < ntx; k++)
            if (tx[k].mat == (uint32_t)modfs_boss[i].mat && tx[k].tex == (uint32_t)modfs_boss[i].tex) src = &tx[k];
        uint32_t w = modfs_boss[i].w, h = modfs_boss[i].h;
        if (!src || src->w < w || src->h < h) continue;
        uint8_t *t8 = tim[i];
        uint32_t cw = rd16(t8 + 16), chs = rd16(t8 + 18);
        uint8_t *row = t8 + 20 + 2 * (size_t)cw * modfs_boss[i].row, *pix = t8 + 8 + rd32(t8 + 8);
        uint32_t tw = rd16(pix + 8) * 2, th = rd16(pix + 10);
        if (cw != 256 || modfs_boss[i].row >= chs || modfs_boss[i].x + w > tw || modfs_boss[i].y + h > th) continue;
        uint8_t *idx = pix + 12;
        /* the NAB's pixels (15-bit, 0 see-through), and whether any differs from the disc's */
        uint16_t *col = malloc((size_t)w * h * 2);
        if (!col) continue;
        int differs = 0;
        size_t opaque = 0;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                uint16_t p = (uint16_t)rd16(src->px + 2 * ((size_t)y * src->w + x));
                uint16_t v = (uint16_t)(((p >> 10) & 31) | ((p >> 5) & 31) << 5 | (p & 31) << 10);
                uint8_t was = idx[(size_t)(modfs_boss[i].y + y) * tw + modfs_boss[i].x + x];
                uint16_t wc = (uint16_t)(rd16(row + 2 * was) & 0x7FFF);
                if (v == wc) { col[y * w + x] = 0x8000 | was; continue; }   /* as it was: its own index (top bit) */
                if (!(p & 0x8000)) v = 0;
                else if (!v) v = 0x0400;
                col[y * w + x] = v;
                differs = 1;
                opaque += v != 0;
            }
        if (!differs) { free(col); continue; }
        uint16_t pal[256];
        for (int k = 0; k < 256; k++) pal[k] = (uint16_t)(rd16(row + 2 * k) & 0x7FFF);
        if (modfs_boss[i].own) {                 /* a palette made for the new picture: index 0 see-through */
            uint16_t *list = malloc((opaque + w * h + 1) * 2);
            size_t m = 0;
            for (uint32_t k = 0; list && k < w * h; k++) {
                uint16_t c = col[k] & 0x8000 ? pal[col[k] & 0xFF] : col[k];
                if (c) list[m++] = c;
            }
            int got = list && m ? median_cut(list, m, pal + 1, 255) : 0;
            free(list);
            pal[0] = 0;
            for (int k = 1 + got; k < 256; k++) pal[k] = 0;
            uint16_t stp = 0;
            for (int k = 0; k < 256; k++) if (rd16(row + 2 * k) & 0x8000) stp = 0x8000;
            for (uint32_t k = 0; k < w * h; k++)
                if (col[k] & 0x8000) col[k] = (uint16_t)(rd16(row + 2 * (col[k] & 0xFF)) & 0x7FFF);   /* (by colour now) */
            for (int k = 0; k < 256; k++) wr16(row + 2 * k, pal[k] ? (uint16_t)(pal[k] | stp) : 0);
        }
        int clear = -1;
        for (int k = 0; k < 256 && clear < 0; k++) if (!pal[k]) clear = k;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                uint16_t c = col[y * w + x];
                int ix = c & 0x8000 ? (int)(c & 0xFF) : c ? nearest(pal, 256, c) : (clear >= 0 ? clear : nearest(pal, 256, 0x0400));
                idx[(size_t)(modfs_boss[i].y + y) * tw + modfs_boss[i].x + x] = (uint8_t)ix;
            }
        free(col);
        done++;
    }
    return done;
}

/* Whether the NAB has the boss's polygons, material by material, skin and
 * each bone's: 0 the same; bit 0, another number of the skin's; bit 1,
 * of the bones' (those are built again). */
static int boss_same_layout(const uint8_t *nab, long n, const uint8_t *h, uint32_t hw) {
    static NabMat mat[8];
    static NabGroup grp[256];
    uint32_t nm = 0, ng = 0;
    if (nab_layout(nab, n, mat, &nm, grp, &ng)) return 0;   /* (boss_shape says why) */
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    static uint32_t ps1[8][65], pc[8][65];
    memset(ps1, 0, sizeof ps1);
    memset(pc, 0, sizeof pc);
    for (uint32_t bi = 0; bi < HW(3) && bi < 64; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64; p = HW(p), guard++) {
            uint32_t nt = HW(p + 2) & 0xFFFF, base = HW(HW(p + 1) + 1) & 0x7FFFFFFF;
            for (uint32_t t = 0; t < nt && t < 4096; t++) {
                uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
                if (((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) || ty & 0xFE000000u) continue;
                int quad = (ty & 0x10) != 0, shared = (ty & 0x1000000) != 0;
                for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF); k++) {
                    uint32_t pg = (HW(base + idx + k * (quad ? 7 : 6) + 1) >> 16) & 0x1F, best = nm;
                    for (uint32_t q = 0; q < nm; q++)
                        if ((mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF) &&
                            (best == nm || (mat[q].page & 0xF) > (mat[best].page & 0xF)))
                            best = q;
                    if (best < nm) ps1[best][shared ? 0 : bi] += quad ? 2 : 1;
                }
            }
        }
#undef HW
    for (uint32_t g = 0; g < ng; g++)
        if (grp[g].mat != ~0u && grp[g].node < 65 && grp[g].mat < 8) pc[grp[g].mat][grp[g].node] += grp[g].count;
    int diff = 0;
    for (uint32_t m = 0; m < 8; m++)
        for (uint32_t b = 0; b < 65; b++)
            if (ps1[m][b] != pc[m][b]) diff |= b ? 2 : 1;
    return diff;
}

/* The bones' polygons of a boss built again from its NAB: nab_rebuild on a
 * model made of the HMD and its bones' vertices and normals (the first
 * bone unit's header: 4, polygons, vertices, normals, coordinates); the
 * new vertices and normals (the disc's first) go after the new HMD's end
 * and the header points at them (a header outside the HMD's header
 * section isn't mapped by the game, and its units aren't drawn). The
 * new HMD (malloc'd) and its words, or NULL and why. */
static uint8_t *boss_rebuild(const uint8_t *nab, long n, const uint8_t *h, uint32_t hw, uint32_t *outw, int *ntri, const char **why) {
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    uint32_t hdr = 0;
    for (uint32_t bi = 0; bi < HW(3) && bi < 64 && !hdr; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64 && !hdr; p = HW(p), guard++) {
            uint32_t ty = HW(p + 3);
            if (((ty & 0xFF) == 0x0D || (ty & 0xFF) == 0x15) && !(ty & 0xFF000000u) && HW(HW(p + 1)) == 4) hdr = HW(p + 1);
        }
    *why = "the disc's model isn't laid out as expected";
    if (!hdr) return NULL;
    uint32_t vs = HW(hdr + 2) & 0x7FFFFFFF, ns = HW(hdr + 3) & 0x7FFFFFFF, ve = hw, ne = hw;
    for (uint32_t w = HW(2); w < hw && w < HW(2) + 256; w++) {   /* each section ends where the next one starts */
        uint32_t v = HW(w);
        if (!(v & 0x80000000u) || v == 0xFFFFFFFFu) continue;
        v &= 0x7FFFFFFF;
        if (v > vs && v < ve) ve = v;
        if (v > ns && v < ne) ne = v;
    }
    if (vs >= ve || ns >= ne || ve > hw || ne > hw) return NULL;
    uint32_t nS = (ve - vs) / 2, nN = (ne - ns) / 2, b0n = 8 + 8 * (nS + nN), nb = 12 + b0n + hw * 4;
    uint8_t *B = malloc(nb);
    if (!B) { *why = "out of memory"; return NULL; }
    wr32(B, 2); wr32(B + 4, 12); wr32(B + 8, 12 + b0n);
    wr32(B + 12, 2); wr32(B + 16, 2 + nS * 2);
    memcpy(B + 20, h + 4 * (size_t)vs, (size_t)nS * 8);
    memcpy(B + 20 + (size_t)nS * 8, h + 4 * (size_t)ns, (size_t)nN * 8);
    memcpy(B + 12 + b0n, h, (size_t)hw * 4);
    uint32_t nn = 0;
    uint8_t *RB = nab_rebuild(nab, n, B, nb, 0, &nn, ntri, why);
    free(B);
    if (!RB) return NULL;
    uint32_t o0 = rd32(RB + 4), o1 = rd32(RB + 8), vb = o0 + rd32(RB + o0) * 4, nb2 = o0 + rd32(RB + o0 + 4) * 4;
    uint32_t cs = (nb2 - vb) / 8, cn = (o1 - nb2) / 8, hw2 = (nn - o1) / 4;
    uint32_t total = hw2 + 2 * cs + 2 * cn;
    uint8_t *H = malloc((size_t)total * 4);
    if (!H) { free(RB); *why = "out of memory"; return NULL; }
    memcpy(H, RB + o1, (size_t)hw2 * 4);
    uint32_t sv = hw2, sn = hw2 + 2 * cs;
    memcpy(H + 4 * (size_t)sv, RB + vb, (size_t)cs * 8);
    memcpy(H + 4 * (size_t)sn, RB + nb2, (size_t)cn * 8);
    /* the header itself points at them (the disc's vertices and normals come first there, so the
     * bones not built again keep theirs); a header put elsewhere isn't mapped by the game */
    wr32(H + 4 * (size_t)(hdr + 2), 0x80000000u | sv);
    wr32(H + 4 * (size_t)(hdr + 3), 0x80000000u | sn);
    free(RB);
#undef HW
    *outw = total;
    *why = NULL;
    return H;
}

/* A boss's textures are pictures inside its own HMD (units of type
 * 0x2000001: the VRAM rectangle, where its 8-bit pixels are, where its
 * palette goes in VRAM and where it is), uploaded as the boss loads. The
 * NAB has each as a texture of its material (a sheet's at their place,
 * stretched to a power of two, nearest pixel). Each changed picture is
 * written back; the pictures whose palettes go to the same place in VRAM
 * (type 33's 17) get one palette made for all of them, the others one
 * each. Pixels as they were keep their index. The number written, or -1
 * and why. */
static int boss_images(const uint8_t *nab, long n, uint8_t *h, uint32_t hw, const char **why) {
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    struct { uint32_t x, y, w, h, pix, cx, cy, clut; uint16_t *col; int changed; } im[64];
    uint32_t ni = 0;
    for (uint32_t bi = 0; bi < HW(3) && bi < 64; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64; p = HW(p), guard++) {
            uint32_t nt = HW(p + 2) & 0xFFFF, hdr = HW(p + 1), q = p + 3;
            for (uint32_t t = 0; t < nt && t < 256; t++) {
                uint32_t ty = HW(q);
                if (ty >> 24 != 2) break;
                if ((ty & 1) && ni < 64) {
                    uint32_t yx = HW(q + 2), hwd = HW(q + 3), cyx = HW(q + 5);
                    im[ni].x = yx & 0xFFFF; im[ni].y = yx >> 16; im[ni].w = (hwd & 0xFFFF) * 2; im[ni].h = hwd >> 16;
                    im[ni].pix = (HW(hdr + 1) & 0x7FFFFFFF) + HW(q + 4);
                    im[ni].cx = cyx & 0xFFFF; im[ni].cy = cyx >> 16;
                    im[ni].clut = (HW(hdr + 2) & 0x7FFFFFFF) + HW(q + 7);
                    im[ni].col = NULL;
                    im[ni].changed = 0;
                    if ((uint64_t)im[ni].pix * 4 + (uint64_t)im[ni].w * im[ni].h <= (uint64_t)hw * 4 && (uint64_t)im[ni].clut * 4 + 512 <= (uint64_t)hw * 4) ni++;
                }
                q += ty & 1 ? 8 : 5;
            }
        }
#undef HW
    if (!ni) return 0;
    /* the NAB's textures: each to the picture at its place in VRAM */
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;
    if (n < 8 || at + 4 > (uint64_t)n) { *why = "it's cut short"; return -1; }
    uint32_t nm = rd32(nab + at);
    at += 4;
    int done = 0;
    for (uint32_t m = 0; m < nm && m < 64; m++) {
        if (at + 352 > (uint64_t)n) { *why = "it's cut short"; goto fail; }
        const uint8_t *rec = nab + at;
        uint32_t nt = rd32(rec + 8), page = rd32(rec + 4) & 0x1F;
        at += 352;
        for (uint32_t t = 0; t < nt; t++) {
            if (at + 16 > (uint64_t)n) { *why = "it's cut short"; goto fail; }
            uint32_t size = rd32(nab + at), W = rd32(nab + at + 8), H = rd32(nab + at + 12);
            const uint8_t *px = nab + at + 16;
            at += 16 + (uint64_t)size;
            if (at > (uint64_t)n || (uint64_t)W * H * 2 > size) { *why = "it's cut short"; goto fail; }
            uint32_t vx, vy, rw, rh;
            if (nt > 1 && t < 10) { vx = rd16(rec + 12 + 2 * t); vy = rd16(rec + 52 + 2 * t); rw = rd32(rec + 192 + 4 * t); rh = rd32(rec + 272 + 4 * t); }
            else { vx = rd32(rec + 12); vy = ((page >> 4) & 1) * 256; rw = W; rh = H; }
            for (uint32_t k = 0; k < ni; k++) {
                if (im[k].x != vx || im[k].y != vy || !rw || !rh || rw > im[k].w || rh > im[k].h) continue;
                const uint8_t *ix = h + 4 * (size_t)im[k].pix, *cl = h + 4 * (size_t)im[k].clut;
                if (!im[k].col && !(im[k].col = malloc((size_t)im[k].w * im[k].h * 2))) continue;
                for (uint32_t y = 0; y < im[k].h; y++)
                    for (uint32_t x = 0; x < im[k].w; x++) {
                        uint8_t was = ix[(size_t)y * im[k].w + x];
                        uint16_t c = 0x8000 | was;   /* (as it was: its index) */
                        if (x < rw && y < rh) {
                            uint16_t p = (uint16_t)rd16(px + 2 * ((size_t)((y * H + rh - 1) / rh) * W + (x * W + rw - 1) / rw));
                            uint16_t v = (uint16_t)(((p >> 10) & 31) | ((p >> 5) & 31) << 5 | (p & 31) << 10);
                            if (v != (rd16(cl + 2 * was) & 0x7FFF)) {
                                c = !(p & 0x8000) ? 0 : v ? v : 0x0400;
                                im[k].changed = 1;
                            }
                        }
                        im[k].col[(size_t)y * im[k].w + x] = c;
                    }
            }
        }
    }
    /* each palette place in VRAM: one palette for its pictures, if any of them changed */
    for (uint32_t k = 0; k < ni; k++) {
        if (!im[k].col) continue;
        int any = 0;
        size_t total = 0;
        for (uint32_t j = 0; j < ni; j++)
            if (im[j].cx == im[k].cx && im[j].cy == im[k].cy) { any |= im[j].changed; total += (size_t)im[j].w * im[j].h; }
        if (!any) continue;
        uint16_t *list = malloc(total * 2), pal[256];
        size_t cnt = 0;
        for (uint32_t j = 0; list && j < ni; j++) {
            if (im[j].cx != im[k].cx || im[j].cy != im[k].cy) continue;
            const uint8_t *ix = h + 4 * (size_t)im[j].pix, *cl = h + 4 * (size_t)im[j].clut;
            for (size_t q = 0; q < (size_t)im[j].w * im[j].h; q++) {
                uint16_t c = im[j].col ? im[j].col[q] : (uint16_t)(0x8000 | ix[q]);
                if (c & 0x8000) c = (uint16_t)(rd16(cl + 2 * (c & 0xFF)) & 0x7FFF);
                if (im[j].col) im[j].col[q] = c;    /* (by colour from here) */
                if (c) list[cnt++] = c;
            }
            if (!im[j].col) {                    /* a picture the NAB didn't touch, sharing the palette: by colour too */
                im[j].col = malloc((size_t)im[j].w * im[j].h * 2);
                for (size_t q = 0; im[j].col && q < (size_t)im[j].w * im[j].h; q++)
                    im[j].col[q] = (uint16_t)(rd16(cl + 2 * ix[q]) & 0x7FFF);
            }
        }
        int got = list && cnt ? median_cut(list, cnt, pal + 1, 255) : 0;
        free(list);
        pal[0] = 0;
        for (int q = 1 + got; q < 256; q++) pal[q] = 0;
        uint16_t stp = 0;
        for (int q = 0; q < 256; q++) if (rd16(h + 4 * (size_t)im[k].clut + 2 * q) & 0x8000) stp = 0x8000;
        for (uint32_t j = 0; j < ni; j++) {
            if (im[j].cx != im[k].cx || im[j].cy != im[k].cy || !im[j].col) continue;
            uint8_t *ix = h + 4 * (size_t)im[j].pix, *cl = h + 4 * (size_t)im[j].clut;
            for (int q = 0; q < 256; q++) wr16(cl + 2 * q, pal[q] ? (uint16_t)(pal[q] | stp) : 0);
            for (size_t q = 0; q < (size_t)im[j].w * im[j].h; q++)
                ix[q] = (uint8_t)(im[j].col[q] ? nearest(pal, 256, im[j].col[q]) : 0);
            free(im[j].col);
            im[j].col = NULL;
            done++;
        }
    }
    for (uint32_t k = 0; k < ni; k++) free(im[k].col);
    return done;
fail:
    for (uint32_t k = 0; k < ni; k++) free(im[k].col);
    return -1;
}

/* A boss's skin built again from its NAB (polygons added or taken away).
 * The skin is kept bone by bone: each bone's unit has a 7-word packet
 * (type 0x1000000): its vertices (count, first in the source, first in
 * the buffer the game turns them into) and normals (the same), and the
 * skin's polygons (types 0x100000d, 0x1000015, in one unit) use the
 * buffers' indices. Its header: 5, polygons, vertex source, vertex
 * buffer, normal source, normal buffer. The NAB's skin triangles (group
 * node 0) have each corner's bone and model-space place: back through
 * the bone's rest matrix (as boss_shape), shared corners once per bone.
 * The new sections, packets and unit go after the HMD's end; the bones'
 * packets and the header are changed to match. A material that is a
 * sheet of textures can't take new skin polygons (which texture a new
 * one is on isn't known). The new HMD and its words, or NULL and why. */
static uint8_t *boss_skin(const uint8_t *nab, long n, const uint8_t *h, uint32_t hw, uint32_t *outw, int *ntri, const char **why) {
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    static NabMat mat[8];
    static NabGroup grp[256];
    uint32_t nm = 0, ng = 0;
    if ((*why = nab_layout(nab, n, mat, &nm, grp, &ng))) return NULL;
    /* the NAB's parts after the groups: per-vertex data, nodes */
    uint32_t nv = rd32(nab), nf = rd32(nab + 4);
    uint64_t at = 8 + (uint64_t)nv * 36;
    at += 4;
    for (uint32_t m = 0; m < nm; m++) {
        uint32_t nt = rd32(nab + at + 8);
        at += 352;
        for (uint32_t t = 0; t < nt; t++) at += 16 + (uint64_t)rd32(nab + at);
        if (nt > 1) {
            int uses = 0;
            for (uint32_t g = 0; g < ng; g++) if (grp[g].mat == m && grp[g].node == 0) uses = 1;
            if (uses) { *why = "a material of its skin is a sheet of textures: new skin polygons can't be put on it"; return NULL; }
        }
    }
    at += 4;
    for (uint32_t g = 0; g < ng; g++) at += 24 + (rd32(nab + at) ? 4 : 0) + 4;
    uint64_t aux = at;
    at += (uint64_t)nv * 60 + (uint64_t)nf * 20;
    uint32_t nq = rd32(nab + at);
    at += 4;
    for (uint32_t q = 0; q < nq && q < 256; q++) at += 96 + (uint64_t)rd32(nab + at + 12) * 8;
    at += 4;
    for (int l = 0; l < 2; l++) {
        uint32_t c = rd32(nab + at);
        at += 4;
        for (uint32_t e = 0; e < c && e < 65536; e++) at += 12 + (uint64_t)rd32(nab + at + 4) * 12;
    }
    at += 12;
    uint32_t nnode = rd32(nab + at) + 1;
    uint64_t nodes = at + 4;
    if (nodes + (uint64_t)nnode * 264 > (uint64_t)n) { *why = "it's cut short"; return NULL; }
    /* the skin's unit and header, and the bones' packets */
    uint32_t sb = 0, su = 0, bones[64], bunit[64], nb = 0;
    for (uint32_t bi = 0; bi < HW(3) && bi < 64; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64; p = HW(p), guard++) {
            uint32_t ty = HW(p + 3);
            if (ty == 0x1000000u && nb < 64) { bones[nb] = bi; bunit[nb] = p; nb++; }
            if ((ty & 0x1000000) && !(ty & 0xFE000000u) && ((ty & 0xFF) == 0x0D || (ty & 0xFF) == 0x15)) { sb = bi; su = p; }
        }
    *why = "the disc's model has no skin laid out as expected";
    if (!su || !nb) return NULL;
    uint32_t hb = HW(su + 1);
    if (HW(hb) != 5) return NULL;
    /* the skin's polygons' look (CLUT, texture page bits) for each material, from the disc's */
    uint32_t tsb[8] = {0}, cba[8] = {0}, sbase = HW(hb + 1) & 0x7FFFFFFF;
    for (uint32_t t = 0; t < (HW(su + 2) & 0xFFFF); t++) {
        uint32_t idx = HW(su + 5 + 3 * t), w0 = HW(sbase + idx), w1 = HW(sbase + idx + 1), pg = (w1 >> 16) & 0x1F;
        for (uint32_t q = 0; q < nm; q++)
            if (!tsb[q] && (mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF)) { tsb[q] = w1 >> 16; cba[q] = w0 >> 16; }
    }
    /* vertices and normals by bone (each bone's own, shared corners once) */
    uint32_t total = 0;
    for (uint32_t g = 0; g < ng; g++) if (grp[g].mat != ~0u && grp[g].node == 0) total += grp[g].count;
    if (!total) { *why = "it has no skin"; return NULL; }
    int16_t (*vx)[4] = calloc((size_t)total * 3, 8), (*nx)[4] = calloc((size_t)total * 3, 8);
    uint32_t *vb = calloc((size_t)total * 3, 4), *nbn = calloc((size_t)total * 3, 4), *cv = malloc((size_t)total * 3 * 4), *cn = malloc((size_t)total * 3 * 4);
    uint32_t nvx = 0, nnx = 0;
    if (!vx || !nx || !vb || !nbn || !cv || !cn) { free(vx); free(nx); free(vb); free(nbn); free(cv); free(cn); *why = "out of memory"; return NULL; }
    uint32_t k = 0;
    for (uint32_t g = 0; g < ng; g++) {
        if (grp[g].mat == ~0u || grp[g].node != 0) continue;
        for (uint32_t t = grp[g].start; t < grp[g].start + grp[g].count; t++)
            for (int c = 0; c < 3; c++, k++) {
                uint32_t ci = 3 * t + (uint32_t)c, bone = rd32(nab + aux + (uint64_t)ci * 60) >> 16, bk = 0;
                while (bk < nb && bones[bk] != bone) bk++;
                if (bk == nb || bone >= nnode) {
                    free(vx); free(nx); free(vb); free(nbn); free(cv); free(cn);
                    *why = "a skin vertex is on a bone without skin in the PS1's model";
                    return NULL;
                }
                float f[9], M[16];
                memcpy(f, nab + 8 + (size_t)ci * 36, sizeof f);
                memcpy(M, nab + nodes + (uint64_t)bone * 264 + 128, sizeof M);
                double A[9], inv[9];
                for (int x = 0; x < 9; x++) A[x] = M[4 * (x / 3) + x % 3];
                double det = A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]);
                if (fabs(det) < 1e-9) det = 1e-9;
                inv[0] = (A[4] * A[8] - A[5] * A[7]) / det; inv[1] = (A[2] * A[7] - A[1] * A[8]) / det; inv[2] = (A[1] * A[5] - A[2] * A[4]) / det;
                inv[3] = (A[5] * A[6] - A[3] * A[8]) / det; inv[4] = (A[0] * A[8] - A[2] * A[6]) / det; inv[5] = (A[2] * A[3] - A[0] * A[5]) / det;
                inv[6] = (A[3] * A[7] - A[4] * A[6]) / det; inv[7] = (A[1] * A[6] - A[0] * A[7]) / det; inv[8] = (A[0] * A[4] - A[1] * A[3]) / det;
                double dp[3] = {f[0] - M[3], f[1] - M[7], f[2] - M[11]};
                int16_t lp[3], ln[3];
                for (int r = 0; r < 3; r++) {
                    double a = inv[3 * r] * dp[0] + inv[3 * r + 1] * dp[1] + inv[3 * r + 2] * dp[2];
                    double b = inv[3 * r] * f[3] + inv[3 * r + 1] * f[4] + inv[3 * r + 2] * f[5];
                    if (r == 2) { a = -a; b = -b; }
                    lp[r] = sat16((float)a);
                    ln[r] = sat16((float)b);
                }
                uint32_t q;
                for (q = 0; q < nvx; q++) if (vb[q] == bk && !memcmp(vx[q], lp, 6)) break;
                if (q == nvx) { memcpy(vx[q], lp, 6); vx[q][3] = 0; vb[q] = bk; nvx++; }
                cv[k] = q;
                for (q = 0; q < nnx; q++) if (nbn[q] == bk && !memcmp(nx[q], ln, 6)) break;
                if (q == nnx) { memcpy(nx[q], ln, 6); nx[q][3] = 0; nbn[q] = bk; nnx++; }
                cn[k] = q;
            }
    }
    /* their order: bone by bone, as the bones' packets go */
    uint32_t *vpos = malloc(nvx * 4), *npos = malloc(nnx * 4), vstart[64], vcount[64], nstart[64], ncount[64], o = 0;
    for (uint32_t b = 0; b < nb; b++) {
        vstart[b] = o;
        for (uint32_t q = 0; q < nvx; q++) if (vb[q] == b) vpos[q] = o++;
        vcount[b] = o - vstart[b];
    }
    o = 0;
    for (uint32_t b = 0; b < nb; b++) {
        nstart[b] = o;
        for (uint32_t q = 0; q < nnx; q++) if (nbn[q] == b) npos[q] = o++;
        ncount[b] = o - nstart[b];
    }
    /* the new HMD: the disc's, then vertex source and buffer, normal source and buffer, packets, the skin's unit */
    uint32_t poly_w = total * 7, unit_w = 3 + 3 * total;
    uint32_t size = hw + 4 * nvx + 4 * nnx + poly_w + unit_w;
    uint8_t *H = calloc(size, 4);
    if (!H || !vpos || !npos) { free(H); free(vpos); free(npos); free(vx); free(nx); free(vb); free(nbn); free(cv); free(cn); *why = "out of memory"; return NULL; }
    memcpy(H, h, (size_t)hw * 4);
    uint32_t svs = hw, svb = svs + 2 * nvx, sns = svb + 2 * nvx, snb = sns + 2 * nnx, sp = snb + 2 * nnx, pw = 0;
    for (uint32_t q = 0; q < nvx; q++) memcpy(H + 4 * (size_t)(svs + 2 * vpos[q]), vx[q], 8);
    for (uint32_t q = 0; q < nnx; q++) memcpy(H + 4 * (size_t)(sns + 2 * npos[q]), nx[q], 8);
    /* the packets: two triangles that were a quad (a,b,c and d,c,b) a GT4 again */
    uint32_t ent[512][3], ne = 0;
    k = 0;
    *ntri = 0;
    for (uint32_t g = 0; g < ng; g++) {
        if (grp[g].mat == ~0u || grp[g].node != 0) continue;
        const NabMat *mm = &mat[grp[g].mat];
        for (uint32_t t = grp[g].start; t < grp[g].start + grp[g].count;) {
            const uint8_t *ta = nab + 8 + (size_t)3 * t * 36, *tb = ta + 3 * 36;
            uint32_t kb = k;
            int quad = t + 1 < grp[g].start + grp[g].count && !memcmp(tb + 36, ta + 72, 36) && !memcmp(tb + 72, ta + 36, 36) &&
                       cv[kb + 4] == cv[kb + 2] && cv[kb + 5] == cv[kb + 1];
            int nc = quad ? 4 : 3;
            uint32_t kc[4] = {kb, kb + 1, kb + 2, kb + 3};
            long u[4], v[4], lo = 1 << 30;
            for (int c = 0; c < nc; c++) {
                float f[9];
                memcpy(f, nab + 8 + (size_t)(c < 3 ? 3 * t + (uint32_t)c : 3 * t + 3) * 36, sizeof f);
                u[c] = lrintf(f[7] * (float)mm->w);
                v[c] = lrintf(f[8] * (float)(mm->h ? mm->h : 256));
                if (u[c] < lo) lo = u[c];
            }
            long off = lo < 0 ? 0 : lo / (long)mm->per;
            if ((mm->page & 0xF) + off > 15) off = 15 - (mm->page & 0xF);
            uint8_t *pk = H + 4 * (size_t)(sp + pw);
            for (int c = 0; c < nc; c++) {
                long uu = u[c] - (long)mm->per * off;
                pk[4 * c] = (uint8_t)(uu < 0 ? 0 : uu > 255 ? 255 : uu);
                pk[4 * c + 1] = (uint8_t)(v[c] < 0 ? 0 : v[c] > 255 ? 255 : v[c]);
            }
            wr16(pk + 2, (uint16_t)cba[grp[g].mat]);
            wr16(pk + 6, (uint16_t)((tsb[grp[g].mat] & ~0x1Fu) | (mm->page & 0x10) | ((mm->page & 0xF) + (uint32_t)off)));
            if (quad) {
                wr16(pk + 10, (uint16_t)npos[cn[kc[0]]]); wr16(pk + 14, (uint16_t)vpos[cv[kc[0]]]);
                wr16(pk + 16, (uint16_t)npos[cn[kc[1]]]); wr16(pk + 18, (uint16_t)vpos[cv[kc[1]]]);
                wr16(pk + 20, (uint16_t)npos[cn[kc[2]]]); wr16(pk + 22, (uint16_t)vpos[cv[kc[2]]]);
                wr16(pk + 24, (uint16_t)npos[cn[kc[3]]]); wr16(pk + 26, (uint16_t)vpos[cv[kc[3]]]);
            } else
                for (int c = 0; c < 3; c++) { wr16(pk + 12 + 4 * c, (uint16_t)npos[cn[kc[c]]]); wr16(pk + 14 + 4 * c, (uint16_t)vpos[cv[kc[c]]]); }
            uint32_t ty = quad ? 0x1000015u : 0x100000Du, ppw = quad ? 7 : 6;
            if (ne && ent[ne - 1][0] == ty && ent[ne - 1][1] + ent[ne - 1][2] * ppw == pw) ent[ne - 1][2]++;
            else if (ne < 512) { ent[ne][0] = ty; ent[ne][1] = pw; ent[ne][2] = 1; ne++; }
            pw += ppw;
            *ntri += quad ? 2 : 1;
            k += quad ? 6 : 3;
            t += quad ? 2 : 1;
        }
    }
    /* the skin's unit, the header and the bones' packets */
    uint32_t un = sp + pw;
    wr32(H + 4 * (size_t)un, HW(su));
    wr32(H + 4 * (size_t)(un + 1), hb);
    wr32(H + 4 * (size_t)(un + 2), 0x80000000u | ne);
    for (uint32_t q = 0; q < ne; q++) {
        wr32(H + 4 * (size_t)(un + 3 + 3 * q), ent[q][0]);
        wr32(H + 4 * (size_t)(un + 4 + 3 * q), 0x80000002u | ent[q][2] << 16);
        wr32(H + 4 * (size_t)(un + 5 + 3 * q), ent[q][1]);
    }
    uint32_t end = un + 3 + 3 * ne;
    wr32(H + 4 * (4 + sb), un);
    wr32(H + 4 * (size_t)(hb + 1), 0x80000000u | sp);
    wr32(H + 4 * (size_t)(hb + 2), 0x80000000u | svs);
    wr32(H + 4 * (size_t)(hb + 3), 0x80000000u | svb);
    wr32(H + 4 * (size_t)(hb + 4), 0x80000000u | sns);
    wr32(H + 4 * (size_t)(hb + 5), 0x80000000u | snb);
    for (uint32_t b = 0; b < nb; b++) {
        uint8_t *e = H + 4 * (size_t)(bunit[b] + 5);
        wr32(e, vcount[b]); wr32(e + 4, vstart[b]); wr32(e + 8, vstart[b]);
        wr32(e + 12, ncount[b]); wr32(e + 16, nstart[b]); wr32(e + 20, nstart[b]);
    }
    free(vpos); free(npos); free(vx); free(nx); free(vb); free(nbn); free(cv); free(cn);
#undef HW
    *outw = end;
    *why = NULL;
    return H;
}

/* How much a boss (one in its room) can grow by when its animations are
 * laid out again after its model's end (the disc's stay where they were). */
#define BOSS_GROWTH (modmem_enabled() ? (RAM_SIZE - RAM_GAME) : (160u * 1024))

/* Types 31 to 33 (the bosses loaded whole: EV_OBJ.DAT 0x190 to 0x192)
 * are one HMD, not packed, with their animation inside: its script and
 * keys where the animation unit's header says (its 4th and 5th words).
 * The NAC's go in place (as many commands and keys as the PS1's), each
 * key where the disc's commands that take it point, 5 words (T, R) or 6
 * (T, R, then a scale of which the PC keeps the 2nd and 3rd, swapped).
 * From the NAB its shape (boss_shape) and animation tables; their
 * textures are the room's. */
static void enemy_whole(Enemy *e) {
    int fi = disc_index("EV_OBJ.DAT");
    Touched *test = touch(disc_index("TEST.BIN")), *t = touch(fi);
    uint32_t blk = 0x190 + (uint32_t)(e->type - 31);
    uint32_t start = test && t ? rd32(test->data + 0x800A195Cu - TEST_BASE + 4 * blk) * 2048 : 0;
    uint32_t end = test && t ? rd32(test->data + 0x800A195Cu - TEST_BASE + 4 * blk + 4) * 2048 : 0;
    uint8_t *h = t ? t->data + start : NULL;
    uint32_t hw = end > start ? (end - start) / 4 : 0;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    uint32_t unit = 0;
    for (uint32_t bi = 0; h && bi < HW(3) && bi < 64 && !unit; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64 && !unit; p = HW(p), guard++)
            if (HW(p + 3) == 0x3800000u) unit = p;
    if (!h || (HW(0) & ~0x100u) != 0x50 || !unit) {
        if (e->data[1]) { note("mods: %s: the disc's enemy type %d isn't laid out as expected: not used", e->what[1], e->type); nused--; }
        if (e->data[0]) { note("mods: %s: the disc's enemy type %d isn't laid out as expected: not used", e->what[0], e->type); nused--; }
        if (e->data[2]) { note("mods: %s: enemy type %d's textures are the room's: change those: not used", e->what[2], e->type); nused--; }
        return;
    }
    uint32_t hdr = HW(unit + 1), sc = HW(hdr + 3) & 0x7FFFFFFF, ky = HW(hdr + 4) & 0x7FFFFFFF;
    uint8_t *extra = NULL;                       /* script and keys laid out again, to go after the HMD's end */
    uint32_t extra_w = 0, extra_sc = 0;
    if (e->data[1]) {
        const uint8_t *nac = e->data[1];
        long n = e->n[1];
        uint32_t nk = n >= 8 ? rd32(nac) : 0, nc = 0;
        uint32_t size = e->type == 31 ? 5 : 6;  /* a key's words: T, R (and a scale, for 32 and 33) */
        const char *why = NULL;
        if (!nk || (uint64_t)nk * 112 + 8 > (uint64_t)n) why = "it's cut short";
        else {
            nc = rd32(nac + 4 + (size_t)nk * 112);
            if (4 + (uint64_t)nk * 112 + 4 + (uint64_t)nc * 8 != (uint64_t)n) why = "it's cut short or has more after it";
            else if (ky >= hw || sc > ky) why = "the disc's animations aren't laid out as expected";
        }
        const uint8_t *cmd = nac + 4 + (size_t)nk * 112 + 4;
        /* in place when the script is as long as the disc's and each key the NAC's commands take is one
         * the disc's commands at the same places take; else laid out again */
        uint32_t *at = why ? NULL : malloc(nk * 4);
        if (!why && !at) why = "out of memory";
        int again = 0;
        if (!why) {
            memset(at, 0xFF, nk * 4);
            again = nc != ky - sc;
            for (uint32_t i = 1; i < nc && !again; i++) {
                uint32_t key = rd32(cmd + 8 * i + 4);
                if (key == 0xFFFFFFFFu) continue;
                if (key >= nk) { again = 1; break; }
                at[key] = HW(sc + i) & 0xFFFF;
            }
            for (uint32_t k = 0; k < nk && !again; k++)
                if (at[k] == 0xFFFFFFFFu) again = 1;
        }
        if (!why && !again) {
            for (uint32_t i = 0; i < nc; i++) wr32(h + 4 * (sc + i), rd32(cmd + 8 * i));
            for (uint32_t k = 0; k < nk; k++) {
                if (at[k] == 0xFFFFFFFFu || ky + at[k] + size > hw) continue;
                uint8_t *q = h + 4 * (ky + at[k]);
                float v[12];
                memcpy(v, nac + 4 + (size_t)k * 112, sizeof v);
                for (int c = 0; c < 3; c++) {
                    int32_t tv = (int32_t)lrintf(v[9 + c]);
                    if (!(rd32(q + 4 * c) == 0x7FFFFFFFu && tv == 0)) wr32(q + 4 * c, (uint32_t)tv);
                    wr16(q + 12 + 2 * c, (uint16_t)(int16_t)lrintf(v[5 + c]));
                }
                if (size == 6) {                 /* the scale: the PC's 1st is the PS1's 3rd, its 2nd the 2nd */
                    wr16(q + 22, (uint16_t)(int16_t)lrintf(v[1]));
                    wr16(q + 20, (uint16_t)(int16_t)lrintf(v[2]));
                }
            }
            note("mods: %s: its animations in place of enemy type %d's", e->what[1], e->type);
        } else if (!why) {
            /* laid out again: the script, then the keys in the NAC's order (a command's key at index x size);
             * a key that kept its values takes the disc's "unchanged" channels and first scale value */
            uint32_t keys = nk;                  /* (the bosses' commands take every key of their NAC, the last too) */
            if (keys * size > 0xFFFF) why = "it has more keys than the PS1's commands can point at";
            else if ((nc + keys * size) * 4 > BOSS_GROWTH) why = "its animations laid out again would take more than the 160 KB a boss can grow by";
            else if (!e->data[0]) why = "it has other commands or keys than the PS1's, and the NAB that says where each animation starts isn't in the mod";
            else {
                extra_w = nc + keys * size;
                extra = calloc(extra_w, 4);
                uint32_t *find = malloc(32768 * 4);
                if (!extra || !find) { free(extra); free(find); extra = NULL; why = "out of memory"; }
                else {
                    memset(find, 0xFF, 32768 * 4);
                    uint32_t region = hw - ky < 13107 * size ? (hw - ky) / size : 13107;
                    for (uint32_t k = 0; k < region; k++) {
                        const uint8_t *q = h + 4 * (ky + k * size);
                        uint32_t hsh = 0;
                        for (int c = 0; c < 3; c++) hsh = hsh * 31 + (rd32(q + 4 * c) == 0x7FFFFFFFu ? 0 : rd32(q + 4 * c));
                        for (int c = 0; c < 3; c++) hsh = hsh * 31 + rd16(q + 12 + 2 * c);
                        for (hsh &= 32767; find[hsh] != 0xFFFFFFFFu; hsh = (hsh + 1) & 32767) {}
                        find[hsh] = k;
                    }
                    for (uint32_t i = 0; i < nc && !why; i++) {
                        uint32_t w = rd32(cmd + 8 * i), key = rd32(cmd + 8 * i + 4);
                        if (i && key != 0xFFFFFFFFu) {
                            if (key >= keys) { why = "a command takes a key it doesn't have"; break; }
                            w = (w & 0xFFFF0000u) | (key * size);
                        }
                        wr32(extra + 4 * i, w);
                    }
                    for (uint32_t k = 0; k < keys && !why; k++) {
                        float v[12];
                        memcpy(v, nac + 4 + (size_t)k * 112, sizeof v);
                        uint8_t *q = extra + 4 * (nc + k * size);
                        int32_t t3[3];
                        uint16_t r3[3];
                        uint32_t hsh = 0;
                        for (int c = 0; c < 3; c++) { t3[c] = (int32_t)lrintf(v[9 + c]); hsh = hsh * 31 + (uint32_t)t3[c]; }
                        for (int c = 0; c < 3; c++) { r3[c] = (uint16_t)(int16_t)lrintf(v[5 + c]); hsh = hsh * 31 + r3[c]; }
                        const uint8_t *was = NULL;
                        for (hsh &= 32767; find[hsh] != 0xFFFFFFFFu && !was; hsh = (hsh + 1) & 32767) {
                            const uint8_t *o = h + 4 * (ky + find[hsh] * size);
                            int same = 1;
                            for (int c = 0; c < 3 && same; c++)
                                same = (rd32(o + 4 * c) == 0x7FFFFFFFu ? 0 : (int32_t)rd32(o + 4 * c)) == t3[c] && rd16(o + 12 + 2 * c) == r3[c];
                            if (same) was = o;
                        }
                        for (int c = 0; c < 3; c++) {
                            wr32(q + 4 * c, (uint32_t)(was && rd32(was + 4 * c) == 0x7FFFFFFFu && t3[c] == 0 ? 0x7FFFFFFF : t3[c]));
                            wr16(q + 12 + 2 * c, r3[c]);
                        }
                        if (size == 6) {
                            wr16(q + 18, was ? rd16(was + 18) : 4096);
                            wr16(q + 20, (uint16_t)(int16_t)lrintf(v[2]));
                            wr16(q + 22, (uint16_t)(int16_t)lrintf(v[1]));
                        }
                    }
                    free(find);
                    extra_sc = nc;
                    if (why) { free(extra); extra = NULL; }
                }
            }
            if (!why) note("mods: %s: its animations in place of enemy type %d's (laid out again after the model: %u commands, %u keys)",
                           e->what[1], e->type, nc, nk);
        }
        free(at);
        if (why) {
            note("mods: %s: %s: not used", e->what[1], why);
            nused--;
        }
    }
    uint8_t *cur = h;                            /* the HMD the rest goes into: the disc's, or one built again */
    uint32_t curw = hw;
    if (e->data[0]) {
        int ntri = 0, moved = 0, keptuv = 0, changed = 0, diff = boss_same_layout(e->data[0], e->n[0], h, hw), parts = 3;
        if (getenv("RT_MOD_REBUILD")) diff |= atoi(getenv("RT_MOD_REBUILD")) & 3;   /* (to test it: 1 the skin, 2 the bones', 3 both) */
        const char *why = NULL;
        if (diff & 2) {                          /* the bones' polygons added or taken away */
            uint32_t nw = 0;
            int nt2 = 0;
            uint8_t *nh = boss_rebuild(e->data[0], e->n[0], h, hw, &nw, &nt2, &why);
            if (nh) {
                cur = nh;
                curw = nw;
                models_grown = 1;
                parts &= ~2;
                note("mods: %s: its bones' polygons built again (%d triangles; the model %u -> %u bytes)", e->what[0], nt2, hw * 4, nw * 4);
            } else
                note("mods: %s: its bones' polygons: %s: not used", e->what[0], why);
            why = NULL;
        }
        if (diff & 1) {                          /* the skin's polygons added or taken away */
            uint32_t nw = 0;
            int nt2 = 0;
            uint8_t *nh = boss_skin(e->data[0], e->n[0], cur, curw, &nw, &nt2, &why);
            if (nh) {
                note("mods: %s: its skin built again (%d triangles; the model %u -> %u bytes)", e->what[0], nt2, curw * 4, nw * 4);
                if (cur != h) free(cur);
                cur = nh;
                curw = nw;
                models_grown = 1;
                parts &= ~1;
            } else
                note("mods: %s: its skin: %s: not used", e->what[0], why);
            why = NULL;
        }
        why = parts ? boss_shape(e->data[0], e->n[0], cur, curw, parts, &ntri, &moved, &keptuv, &changed) : NULL;
        if (why) note("mods: %s: its shape: %s: not used", e->what[0], why);
        else if (changed)
            note("mods: %s: its %s in place of enemy type %d's (%d polygons; %d vertex coordinates moved%s)", e->what[0],
                 parts == 3 ? "shape" : parts == 1 ? "skin" : "bones' polygons", e->type, ntri, moved,
                 keptuv ? "; some UVs kept: they weren't in one of its textures" : "");
        const char *why2 = nab_seqs(e->data[0], e->n[0], cur, (curw * 4) | 0x80000000u);
        if (why2) note("mods: %s: its animation tables: %s: not used", e->what[0], why2);
        else note("mods: %s: its animation tables in place of enemy type %d's", e->what[0], e->type);
        const char *why3 = NULL;
        int ni = boss_images(e->data[0], e->n[0], cur, curw, &why3);
        if (ni < 0) note("mods: %s: its textures: %s: not used", e->what[0], why3);
        else if (ni) note("mods: %s: its textures in place of enemy type %d's (%d pictures in its model)", e->what[0], e->type, ni);
        int nt = boss_textures(e->type, e->data[0], e->n[0], &why3);   /* and the copies of them elsewhere on the disc */
        if (nt > 0) note("mods: %s: its textures also in %d other places on the disc", e->what[0], nt);
        if (why && why2) nused--;
    }
    if (e->data[2]) { note("mods: %s: give enemy type %d's textures in its NAB: not used", e->what[2], e->type); nused--; }
    if (extra || cur != h) {
        uint8_t *nh = malloc((size_t)(curw + extra_w) * 4);
        if (nh && ngrows < (int)(sizeof grows / sizeof grows[0])) {
            memcpy(nh, cur, (size_t)curw * 4);
            if (extra) {
                memcpy(nh + 4 * (size_t)curw, extra, (size_t)extra_w * 4);
                wr32(nh + 4 * (hdr + 3), 0x80000000u | curw);          /* where the script and the keys start now */
                wr32(nh + 4 * (hdr + 4), 0x80000000u | (curw + extra_sc));
            }
            Grow *g = &grows[ngrows++];
            g->fi = fi;
            g->offset = start;
            g->size = (curw + extra_w) * 4;
            g->orig = hw * 4;
            g->cap = (hw * 4 + BOSS_GROWTH + 2047) & ~2047u;
            models_grown = 1;                    /* (the log says if a room's memory comes near its end) */
            mod_memory_models = 1;
            g->data = nh;
            snprintf(g->what, sizeof g->what, "%s", extra ? e->what[1] : e->what[0]);
        } else
            free(nh);
        free(extra);
        if (cur != h) free(cur);
    }
#undef HW
}

/* Each enemy a mod changes, converted into its block (packed again in
 * packs_done). */
static void enemies_done(void) {
    for (int i = 0; i < nenemies; i++) {
        Enemy *e = &enemies[i];
        int nk = !!e->data[0] + !!e->data[1] + !!e->data[2];
        const char *what = e->data[0] ? e->what[0] : e->data[1] ? e->what[1] : e->what[2];
        if (e->type >= 31 && e->type <= 33) {
            enemy_whole(e);
            goto next;
        }
        if (e->type >= 34 && e->type <= 37) {    /* the PC's types 34-37 are 16's and 17's data on the PS1 */
            int to = e->type < 36 ? 16 : 17;
            note("mods: %s: enemy type %d is type %d's data on the PS1: it goes there", what, e->type, to);
            e->type = to;
        }
        int fi = disc_index(e->type == 30 ? "EV_OBJ.DAT" : "OBJ.DAT");
        Packed *p = e->type >= 0 && e->type <= 30 ? packed_get(fi, e->type == 30 ? 0x196 : (uint32_t)e->type) : NULL;
        if (!p || !p->split) {
            note("mods: %s: the PS1 has no enemy type %d: not used", what, e->type);
            nused -= nk;
            goto next;
        }
        uint32_t n = 0;
        uint8_t *u;
        const char *why;
        int ntex = 0;
        if (e->data[0]) {
            u = packed_part(p, 0, &n);
            why = u ? nab_into(e->data[0], e->n[0], u, n, &ntex) : "the disc's textures couldn't be unpacked";
            if (why) { note("mods: %s: %s: not used", e->what[0], why); nused--; }
            else {
                packed_note(p, 0, e->what[0]);
                note("mods: %s: its textures (%d) in place of enemy type %d's", e->what[0], ntex, e->type);
            }
        }
        if (e->data[0]) {                        /* the NAB's shape */
            uint32_t nt = 0, n2 = 0, tw = 0, th = 0;
            uint8_t *tex = packed_part(p, 0, &nt), *skin = tex ? enemy_skin(tex, nt) : NULL;
            if (skin) tim_size(skin, &tw, &th);
            u = packed_part(p, 2, &n2);
            int ntri = 0, moved = 0;
            if (u && (!nab_same_layout(e->data[0], e->n[0], u, n2) || getenv("RT_MOD_REBUILD"))) {   /* (RT_MOD_REBUILD: always, to test it) */
                uint32_t nn = 0;                 /* polygons added, taken away or moved between body parts */
                uint8_t *nu = nab_rebuild(e->data[0], e->n[0], u, n2, tw, &nn, &ntri, &why);
                if (!nu) note("mods: %s: its shape: %s: not used (its textures are)", e->what[0], why);
                else {
                    note("mods: %s: its shape, built again, in place of enemy type %d's (%d triangles; the model %u -> %u bytes)", e->what[0],
                         e->type, ntri, n2, nn);
                    free(p->part[2]);
                    p->part[2] = nu;
                    p->plen[2] = nn;
                    if (nn > n2) models_grown = 1;
                    packed_note(p, 2, e->what[0]);
                }
            } else {
                why = u ? nab_shape(e->data[0], e->n[0], u, n2, tw, &ntri, &moved) : "the disc's model couldn't be unpacked";
                if (why) note("mods: %s: its shape: %s: not used (its textures are)", e->what[0], why);
                else if (p->was[2] && memcmp(p->was[2], u, n2)) {
                    packed_note(p, 2, e->what[0]);
                    note("mods: %s: its shape in place of enemy type %d's (%d polygons; %d vertex coordinates moved)", e->what[0], e->type, ntri, moved);
                }
            }
        }
        if (e->data[2]) {                        /* a picture of the skin: after the NAB's, so it wins */
            u = packed_part(p, 0, &n);
            uint8_t *tim = u ? enemy_skin(u, n) : NULL;
            int w = 0, h = 0, c;
            uint8_t *img = tim ? stbi_load_from_memory(e->data[2], (int)e->n[2], &w, &h, &c, 4) : NULL;
            const char *how = "";
            why = !tim ? "the disc's textures couldn't be unpacked" : !img ? "couldn't read it as a picture"
                                                                          : tim_from_picture(tim, img, (uint32_t)w, (uint32_t)h, &how);
            stbi_image_free(img);
            if (why) { note("mods: %s: %s: not used", e->what[2], why); nused--; }
            else {
                uint32_t tw = 0, th = 0;
                tim_size(tim, &tw, &th);
                packed_note(p, 0, e->what[2]);
                note("mods: %s: enemy type %d's texture (%ux%u%s)", e->what[2], e->type, tw, th,
                       (uint32_t)w != tw || (uint32_t)h != th ? ", scaled to the PS1's size" : "");
            }
        }
        if (e->data[1]) {
            u = packed_part(p, 1, &n);
            int rebuilt = 0;
            uint32_t was_n = n;
            why = u ? nac_into(e->data[1], e->n[1], &u, &n, &rebuilt) : "the disc's animations couldn't be unpacked";
            if (!why && rebuilt && !e->data[0]) {
                free(u);
                why = "it has other commands or keys than the PS1's, and the NAB that says where each animation starts isn't in the mod";
            }
            if (why) { note("mods: %s: %s: not used", e->what[1], why); nused--; }
            else {
                if (rebuilt) {
                    free(p->part[1]);
                    p->part[1] = u;
                    p->plen[1] = n;
                }
                packed_note(p, 1, e->what[1]);
                note("mods: %s: its animations in place of enemy type %d's%s", e->what[1], e->type, rebuilt ? " (laid out again)" : "");
                if (rebuilt) note("mods: %s: the animations %u -> %u bytes", e->what[1], was_n, n);
            }
        }
        if (e->data[0]) {                        /* the NAB's animation tables: where each animation starts */
            uint32_t n2 = 0;
            uint8_t *b2 = packed_part(p, 2, &n2), *before = b2 ? malloc(n2) : NULL;
            if (before) memcpy(before, b2, n2);
            why = b2 ? nab_seqs(e->data[0], e->n[0], b2, n2) : "the disc's model couldn't be unpacked";
            if (why) note("mods: %s: its animation tables: %s: not used", e->what[0], why);
            else if (before && memcmp(before, b2, n2)) {
                packed_note(p, 2, e->what[0]);
                note("mods: %s: its animation tables in place of enemy type %d's", e->what[0], e->type);
            }
            free(before);
        }
    next:
        for (int k = 0; k < 3; k++) free(e->data[k]);
    }
    nenemies = 0;
}

/* ------------------------------------------------------------ extracting */

/* The originals of everything a mod can replace, written under the names
 * a mod gives them, to start from (the platform's `extract` folder, when
 * it's there and empty; RT_EXTRACT for the headless runner): the disc's
 * files that aren't code or streams, by their disc path; the files the
 * PC version has loose (rooms, doors, effects, items, Resource...), by
 * their PC path; the PC's 2D pictures as PNG (map/MAP00_1.png); each
 * enemy's texture as PNG (nab/ENEM00.png); every streamed sound channel
 * as WAV (X/BGM_32.STR/05.wav). Movies' frames come out as they play
 * (movie_dump). Read from the disc, mods left out. */
char g_extract_dir[1024];

static int write_out(const char *rel, const uint8_t *d, size_t n) {
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", g_extract_dir, rel);
    for (char *c = path + 1; *c; c++)
        if (*c == '/') { *c = 0; mkdir(path); *c = '/'; }
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(d, 1, n, f);
    fclose(f);
    return 1;
}

static uint8_t *disc_read(int fi) {               /* a disc file as it is on the disc */
    uint32_t nsec = (disc_files[fi].size + 2047) / 2048;
    uint8_t *d = malloc((size_t)nsec * 2048 + 1), s[2352];
    disc_only = 1;
    for (uint32_t k = 0; d && k < nsec; k++) {
        cd_read_sector_raw(disc_files[fi].lba + k, s);
        memcpy(d + (size_t)k * 2048, s + 24, 2048);
    }
    disc_only = 0;
    return d;
}

/* A TIM as a PNG (its first palette), to path. */
static int tim_png(const char *rel, const uint8_t *tim) {
    uint32_t flag = rd32(tim + 4), bpp = (uint32_t[]){4, 8, 16, 24}[flag & 3], w, h;
    if (bpp == 24) return 0;
    const uint8_t *clut = flag & 8 ? tim + 12 + 8 : NULL, *pix = flag & 8 ? tim + 8 + rd32(tim + 8) : tim + 8;
    tim_size(tim, &w, &h);
    uint8_t *rgb = malloc((size_t)w * h * 3);
    for (uint32_t k = 0; k < w * h; k++) {
        uint32_t v = bpp == 16 ? rd16(pix + 12 + 2 * k) : bpp == 8 ? rd16(clut + 2 * pix[12 + k]) : rd16(clut + 2 * ((pix[12 + k / 2] >> (k & 1) * 4) & 15));
        uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
        rgb[3 * k] = (uint8_t)(r | r >> 5);
        rgb[3 * k + 1] = (uint8_t)(g | g >> 5);
        rgb[3 * k + 2] = (uint8_t)(b | b >> 5);
    }
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", g_extract_dir, rel);
    for (char *c = path + 1; *c; c++)
        if (*c == '/') { *c = 0; mkdir(path); *c = '/'; }
    png_write(path, rgb, (int)w, (int)h);
    free(rgb);
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* XA ADPCM, one sector, appended to a channel's PCM (as spu.c plays it). */
typedef struct { int16_t *pcm; size_t n, cap; int32_t hist[2][2]; uint32_t rate; int stereo; } XaChan;
static void xa_decode(const uint8_t *sec, XaChan *c) {
    static const int pos[5] = {0, 60, 115, 98, 122}, neg[5] = {0, 0, -52, -55, -60};
    uint8_t coding = sec[19];
    int stereo = (coding & 3) == 1, bits8 = ((coding >> 4) & 3) == 1;
    c->stereo = stereo;
    c->rate = (coding >> 2) & 1 ? 18900 : 37800;
    size_t frames = (size_t)(bits8 ? 4 : 8) * 28 * 18 / (stereo ? 2 : 1);
    if (c->n + frames > c->cap) {
        c->cap = (c->n + frames) * 2;
        c->pcm = realloc(c->pcm, c->cap * 4);
    }
    int16_t *out = c->pcm + c->n * 2;
    size_t at[2] = {0, 0};
    for (int g = 0; g < 18; g++) {
        const uint8_t *grp = sec + 24 + g * 128;
        for (int u = 0; u < (bits8 ? 4 : 8); u++) {
            int shift = grp[4 + u] & 15, filt = (grp[4 + u] >> 4) & 3;
            if (shift > 12) shift = 9;
            int ch = stereo ? (u & 1) : 0;
            int32_t *h = c->hist[ch];
            for (int i = 0; i < 28; i++) {
                int32_t t;
                if (bits8) t = (int32_t)(int8_t)grp[16 + i * 4 + u] << 8;
                else {
                    int nib = (grp[16 + i * 4 + (u >> 1)] >> ((u & 1) * 4)) & 15;
                    t = (nib & 8 ? nib - 16 : nib) << 12;
                }
                int32_t v = (t >> shift) + ((h[0] * pos[filt] + h[1] * neg[filt] + 32) >> 6);
                v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
                h[1] = h[0];
                h[0] = v;
                size_t k = at[ch]++;
                if (stereo) out[2 * k + ch] = (int16_t)v;
                else out[2 * k] = out[2 * k + 1] = (int16_t)v;
            }
        }
    }
    c->n += frames;
}

static int wav_write(const char *rel, const int16_t *lr, size_t frames, uint32_t rate) {
    size_t n = frames * 4;
    uint8_t *d = malloc(44 + n);
    memcpy(d, "RIFF", 4);
    wr32(d + 4, (uint32_t)(36 + n));
    memcpy(d + 8, "WAVEfmt ", 8);
    wr32(d + 16, 16);
    wr16(d + 20, 1);
    wr16(d + 22, 2);
    wr32(d + 24, rate);
    wr32(d + 28, rate * 4);
    wr16(d + 32, 4);
    wr16(d + 34, 16);
    memcpy(d + 36, "data", 4);
    wr32(d + 40, (uint32_t)n);
    for (size_t k = 0; k < frames * 2; k++) wr16(d + 44 + 2 * k, (uint16_t)lr[k]);
    int ok = write_out(rel, d, 44 + n);
    free(d);
    return ok;
}

/* A sound of the banks as a WAV, mono, with its loop (a smpl chunk) if it repeats. */
static int sfx_write(const char *rel, const int16_t *pcm, uint32_t frames, uint32_t rate, int64_t from) {
    uint32_t n = frames * 2, extra = from >= 0 ? 8 + 60 : 0;
    uint8_t *d = calloc(1, 44 + n + extra);
    if (!d) return 0;
    memcpy(d, "RIFF", 4);
    wr32(d + 4, 36 + n + extra);
    memcpy(d + 8, "WAVEfmt ", 8);
    wr32(d + 16, 16);
    wr16(d + 20, 1);
    wr16(d + 22, 1);
    wr32(d + 24, rate);
    wr32(d + 28, rate * 2);
    wr16(d + 32, 2);
    wr16(d + 34, 16);
    memcpy(d + 36, "data", 4);
    wr32(d + 40, n);
    for (uint32_t k = 0; k < frames; k++) wr16(d + 44 + 2 * k, (uint16_t)pcm[k]);
    if (extra) {
        uint8_t *m = d + 44 + n;
        memcpy(m, "smpl", 4);
        wr32(m + 4, 60);
        wr32(m + 8 + 12, (uint32_t)(1000000000ull / rate));   /* sample period, ns */
        wr32(m + 8 + 16, 60);                                /* MIDI unity note */
        wr32(m + 8 + 28, 1);                                 /* one loop: */
        wr32(m + 8 + 44, (uint32_t)from);                    /* from, */
        wr32(m + 8 + 48, frames - 1);                        /* to the end */
    }
    int ok = write_out(rel, d, 44 + n + extra);
    free(d);
    return ok;
}

static int nextracted;
int modfs_extracted(void) { return nextracted; }

/* Returns how many files it wrote. */
int modfs_extract(void) {
    if (!g_extract_dir[0]) return 0;
    note("mods: writing the originals to %s (it takes a minute)", g_extract_dir);
    int files = 0;
    char rel[700];
    /* the disc's files, and the sound of its streams */
    for (size_t i = 0; i < sizeof disc_files / sizeof disc_files[0]; i++) {
        if (disc_files[i].hash) continue;
        if (!strstr(disc_files[i].name, ".STR")) {
            uint8_t *d = disc_read((int)i);
            size_t z = 0;
            while (d && z < disc_files[i].size && !d[z]) z++;
            if (d && z < disc_files[i].size) files += write_out(disc_files[i].name, d, disc_files[i].size);   /* (not the disc's filler) */
            free(d);
            continue;
        }
        static XaChan ch[32];
        memset(ch, 0, sizeof ch);
        uint8_t s[2352];
        uint32_t nsec = (disc_files[i].size + 2047) / 2048;
        disc_only = 1;
        for (uint32_t k = 0; k < nsec; k++) {
            if (!cd_read_sector_raw(disc_files[i].lba + k, s)) continue;
            if (s[15] != 2 || !(s[18] & 0x04) || s[17] >= 32) continue;
            xa_decode(s, &ch[s[17]]);
        }
        disc_only = 0;
        for (int c = 0; c < 32; c++) {
            if (!ch[c].n) continue;
            snprintf(rel, sizeof rel, "%s/%02d.wav", disc_files[i].name, c);
            files += wav_write(rel, ch[c].pcm, ch[c].n, ch[c].rate);
            free(ch[c].pcm);
        }
    }
    /* the sound banks' sounds, each once (the same one in other banks is changed with it) */
    {
        Touched *t = touch(disc_index("NOISE.BIN"));
        static uint32_t v[1024];
        Bank *bk = NULL;
        uint32_t nb = noise_banks(t, v, &bk);
        uint32_t *seen = calloc(65536, sizeof *seen);   /* by crc: the first place it was, block << 8 | sound, +1 */
        for (uint32_t b = 0; seen && b < nb; b++)
            for (uint32_t j = 1; j <= bk[b].vs; j++) {
                const uint8_t *w = t->data + vag_off(t->data, &bk[b], j);
                uint32_t sz = vag_size(t->data, &bk[b], j), crc = (uint32_t)mz_crc32(0, w, sz), h = crc & 0xFFFF, dup = 0;
                for (uint32_t k = h; seen[k] && !dup; k = (k + 1) & 0xFFFF) {
                    const Bank *o = &bk[(seen[k] - 1) >> 8];
                    uint32_t oj = (seen[k] - 1) & 0xFF;
                    dup = vag_size(t->data, o, oj) == sz && !memcmp(t->data + vag_off(t->data, o, oj), w, sz);
                    h = (k + 1) & 0xFFFF;
                }
                if (dup || sz < 16) continue;
                seen[h] = (b << 8 | j) + 1;
                Shape sh = vag_shape(w, sz / 16);
                int16_t *pcm = vag_decode(w, sz / 16);
                snprintf(rel, sizeof rel, "NOISE/%03u/%02u.wav", b, j);
                if (pcm) files += sfx_write(rel, pcm + 28 * sh.lead, sh.data * 28, vag_rate(t->data, &bk[b], j), sh.loop ? (int64_t)sh.from * 28 : -1);
                free(pcm);
            }
        for (uint32_t b = 0; b < nb; b++) {     /* the rooms' music */
            uint32_t at = v[b] * 2048, c = bk[b].vs ? rd32(t->data + at) : 0;
            for (uint32_t k = 1; k + 2 <= c; k++) {
                uint32_t off = rd32(t->data + at + 4 + 4 * k), end = rd32(t->data + at + 8 + 4 * k);
                snprintf(rel, sizeof rel, "NOISE/%03u/music%u.seq", b, k);
                if (end > off && !memcmp(t->data + at + off, "pQES", 4)) files += write_out(rel, t->data + at + off, end - off);
            }
        }
        free(seen);
        free(bk);
    }
    /* the texts, the font's pages, the items' pictures */
    files += texts_extract();
    files += data_extract();
    files += diary_extract();
    files += ui_extract();
    for (unsigned k = 0x186; k <= 0x188; k++) {
        char up[32], name[64];
        snprintf(up, sizeof up, "FONT/FONT%X.PNG", k);
        uint8_t *tim = own_picture(up, name, sizeof name);
        snprintf(rel, sizeof rel, "FONT/FONT%X.png", k);
        if (tim) files += tim_png(rel, tim);
    }
    for (unsigned k = 0; k < 64; k++) {
        char up[32], name[64];
        snprintf(up, sizeof up, "BIGTIM/%02u.PNG", k);
        uint8_t *tim = own_picture(up, name, sizeof name);
        snprintf(rel, sizeof rel, "BIGTIM/%02u.png", k);
        if (tim) files += tim_png(rel, tim);
    }
    /* the PC version's names: loose files, ones in packed blocks, 2D pictures, enemies' textures */
    for (size_t i = 0; i < sizeof modfs_pc / sizeof modfs_pc[0]; i++) {
        Touched *t = touch(disc_index(modfs_pc[i].disc));
        const uint8_t *d = t ? t->data : NULL;
        if (d && (uint32_t)mz_crc32(0, d + modfs_pc[i].offset, modfs_pc[i].size) == modfs_pc[i].crc)
            files += write_out(modfs_pc[i].pc, d + modfs_pc[i].offset, modfs_pc[i].size);
    }
    for (size_t i = 0; i < sizeof modfs_pack / sizeof modfs_pack[0]; i++) {
        if (i && !strcmp(modfs_pack[i].pc, modfs_pack[i - 1].pc)) continue;   /* (the same file in another place) */
        Packed *p = packed_get(disc_index(modfs_pack[i].disc), modfs_pack[i].block);
        uint32_t n = 0;
        const uint8_t *u = NULL;
        if (modfs_pack[i].part == -2) {
            int fi = disc_index(modfs_pack[i].disc);
            Touched *test = touch(disc_index("TEST.BIN")), *t = touch(fi);
            uint32_t at = t && test ? rd32(test->data + archive_table(fi) - TEST_BASE + 4 * modfs_pack[i].block) * 2048 + modfs_pack[i].offset : 0;
            if (t && (uint32_t)mz_crc32(0, t->data + at, modfs_pack[i].size) == modfs_pack[i].crc)
                files += write_out(modfs_pack[i].pc, t->data + at, modfs_pack[i].size);
            continue;
        }
        u = packed_part(p, modfs_pack[i].part < 0 ? 0 : modfs_pack[i].part, &n);
        if (u && modfs_pack[i].offset + modfs_pack[i].size <= n &&
            (uint32_t)mz_crc32(0, u + modfs_pack[i].offset, modfs_pack[i].size) == modfs_pack[i].crc)
            files += write_out(modfs_pack[i].pc, u + modfs_pack[i].offset, modfs_pack[i].size);
    }
    for (size_t i = 0; i < sizeof modfs_bmp / sizeof modfs_bmp[0]; i++) {
        if (i && !strcmp(modfs_bmp[i].pc, modfs_bmp[i - 1].pc)) continue;
        Touched *t = touch(disc_index(modfs_bmp[i].disc));
        const uint8_t *d = t ? t->data : NULL;
        snprintf(rel, sizeof rel, "%s", modfs_bmp[i].pc);
        char *dot = strrchr(rel, '.');
        if (dot) strcpy(dot, ".png");
        if (d && (uint32_t)mz_crc32(0, d + modfs_bmp[i].offset, modfs_bmp[i].size) == modfs_bmp[i].crc)
            files += tim_png(rel, d + modfs_bmp[i].offset);
    }
    for (size_t fi = 0; fi < sizeof disc_files / sizeof disc_files[0]; fi++) {   /* every picture, by file and number */
        const char *nm = disc_files[fi].name, *ext = strrchr(nm, '.');
        if (!ext || !strcasecmp(ext, ".STR") || !strcasecmp(ext, ".XA") || disc_files[fi].size < 32) continue;
        uint8_t *d = disc_read((int)fi);
        static uint32_t off[4096];
        int count = d ? file_pictures(d, (disc_files[fi].size + 2047) / 2048 * 2048, off, 4096) : 0;
        for (int k = 0; k < count; k++) {
            snprintf(rel, sizeof rel, "PICTURES/%s/%03d.png", nm, k);
            files += tim_png(rel, d + off[k]);
        }
        free(d);
    }
    for (int type = 0; type <= 30; type++) {
        Packed *p = packed_get(disc_index(type == 30 ? "EV_OBJ.DAT" : "OBJ.DAT"), type == 30 ? 0x196 : (uint32_t)type);
        uint32_t n = 0;
        uint8_t *u = p && p->split ? packed_part(p, 0, &n) : NULL, *tim = u ? enemy_skin(u, n) : NULL;
        snprintf(rel, sizeof rel, "NAB/ENEM%02d.png", type);
        if (tim) files += tim_png(rel, tim);
    }
    for (int i = 0; i < npacks; i++)             /* nothing changed: just let them go */
        for (int k = 0; k < 3; k++) { free(packs[i].part[k]); free(packs[i].was[k]); }
    npacks = 0;
    note("mods: %d originals written to %s", files, g_extract_dir);
    nextracted = files;
    return files;
}

static void walk(const char *mod, const char *dir, const char *rel) {
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char path[1024], r[512];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        snprintf(r, sizeof r, "%s%s%s", rel, *rel ? "/" : "", e->d_name);
        struct stat st;
        if (stat(path, &st)) continue;
        if (S_ISDIR(st.st_mode)) walk(mod, path, r);
        else if (*rel || disc_index(e->d_name) >= 0 || !strcasecmp(e->d_name, "pc-bitmaps.txt"))   /* at the top only the disc's own files (the rest: readme, pictures...) */
            apply(mod, r, path);
    }
    closedir(d);
}

static int cmp_str(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

/* The dynamic heap is separate from the static prefix reapplied on load.
 * Cache the final (possibly relocated) archive sizes once, after grow_all.
 * Never touch a relocated archive by its former LBA: find its named copy. */
uint32_t modfs_static_end(void) { return room_bytes ? g_room_win_end : 0x80200000u + ((ext_used + 15u) & ~15u); }
int modfs_dynamic_models(void) { return mod_memory_models && nused > 0; }
static Touched *memory_archive(const char *name) {
    for (int i = 0; i < ntouched; i++) if (!strcmp(touched[i].name, name)) return &touched[i];
    return touch(disc_index(name));
}
/* Reserve before archive publication, using only measured whole raw-TIM
 * identities and bounded pending sizes. No allocation occurs during CD I/O. */
static uint32_t sync_prefix_before;
static int sync_ext_before,sync_reserved;
static Touched *sync_touched(const char *name) {
    for(int i=0;i<ntouched;i++)if(!strcmp(touched[i].name,name))return &touched[i];
    return NULL;
}
static int sync_span(const Touched *test,const Touched *file,uint32_t table,uint32_t index,uint32_t *start,uint32_t *bytes) {
    uint64_t at=(uint64_t)table-TEST_BASE+(uint64_t)index*4;
    if(!test || !file || at+8>(uint64_t)test->nsec*2048)return 0;
    uint32_t first=rd32(test->data+(size_t)at),last=rd32(test->data+(size_t)at+4);
    if(first>last || last>file->nsec || (uint64_t)last*2048>UINT32_MAX)return 0;
    *start=first*2048;*bytes=(last-first)*2048;return 1;
}
static void sync_buffers_reserve(void) {
    if(!modio_boot(0,0))rt_fatal("mods: cannot rebuild synchronous staging during an active read");
    sync_reserved=0;sync_prefix_before=ext_used;sync_ext_before=g_ram_ext_used;
    if(!modmem_enabled() || (!sync_touched("EV_OBJ.DAT") && !sync_touched("AEX.DAT")))return;
    Touched *test=memory_archive("TEST.BIN");uint32_t maximum=0;
    for(unsigned k=0;k<13;k++) {
        uint32_t table=k<3?0x800A195Cu:0x800A2B3Cu,index=k<3?k+0x186:k-3;
        Touched *file=sync_touched(k<3?"EV_OBJ.DAT":"AEX.DAT");uint32_t start,bytes;
        if(!sync_span(test,file,table,index,&start,&bytes))continue;
        const uint8_t *data=file->data+start;uint32_t wanted=bytes;
        int fi=disc_index(k<3?"EV_OBJ.DAT":"AEX.DAT");
        for(int g=0;g<ngrows;g++)if(grows[g].fi==fi && grows[g].offset==start &&
            (((uint64_t)grows[g].orig+2047)&~UINT64_C(2047))==bytes) {
            data=grows[g].data;wanted=grows[g].size;
        }
        uint64_t rounded=((uint64_t)wanted+2047)&~UINT64_C(2047);
        if(rounded>modio_original_size(table,(int)index) && rounded<=RAM_SIZE-RAM_GAME-16 &&
            modio_tim_valid(data,wanted) && rounded>maximum)maximum=(uint32_t)rounded;
    }
    if(!maximum)return;
    uint32_t allocation=ext_alloc(maximum+15);
    if(!allocation){note("mods: no extended RAM for synchronous image/font staging: larger files not used");return;}
    if(!modio_boot((allocation+15)&~15u,maximum))rt_fatal("mods: invalid synchronous staging reservation");
    sync_reserved=1;
}
static void sync_buffers_index(void) {
    if(!sync_touched("EV_OBJ.DAT") && !sync_touched("AEX.DAT"))return;
    Touched *test=memory_archive("TEST.BIN");
    for(unsigned k=0;k<13;k++) {
        uint32_t table=k<3?0x800A195Cu:0x800A2B3Cu,index=k<3?k+0x186:k-3;
        Touched *file=sync_touched(k<3?"EV_OBJ.DAT":"AEX.DAT");uint32_t start,bytes;
        if(!sync_span(test,file,table,index,&start,&bytes))continue;
        if(bytes<=modio_original_size(table,(int)index))continue;
        if(!modmem_enabled() || !modio_tim_valid(file->data+start,bytes) ||
            !modio_register(table,index,file->lba+start/2048,bytes/2048))
            rt_fatal("mods: final synchronous image/font resource has no safe staging layout");
        note("mods: %s block %u: %u synchronous raw-TIM bytes in owned staging",file->name,index,bytes);
    }
    if(!modio_active() && sync_reserved) {
        ext_used=sync_prefix_before;g_ram_ext_used=sync_ext_before;sync_reserved=0;
        if(!modio_boot(0,0))rt_fatal("mods: unexpected staging owner during boot rollback");
    }
}
static void memory_prepare(void) {
    modfs_memory_reset();
    if (!modmem_enabled() || !modfs_dynamic_models()) return;
    Touched *test = memory_archive("TEST.BIN");
    static const struct { const char *name; uint32_t table; } sources[] = {
        {"OBJ.DAT",0x800A0CD8u}, {"EV_OBJ.DAT",0x800A195Cu}, {"ITEMDAT.DAT",0x800A3EDCu}
    };
    for (unsigned i = 0; i < sizeof sources / sizeof sources[0]; i++) {
        Touched *t = memory_archive(sources[i].name);
        uint32_t off = sources[i].table - TEST_BASE;
        size_t tb = test ? (size_t)test->nsec * 2048 : 0;
        if (!test || !t || off > tb || !modfs_memory_index(sources[i].table,t->data,(size_t)t->nsec*2048,test->data+off,tb-off))
            note("mods: dynamic model metadata unavailable for %s; a room needing it will be rejected before model writes",sources[i].name);
    }
}

/* mods/disabled.txt (the settings' Mods page): folders switched off, a name a line. */
static int mod_switched_off(const char *name) {
    char p[1100], line[256];
    snprintf(p, sizeof p, "%s/disabled.txt", g_mods_dir);
    FILE *f = fopen(p, "r");
    int off = 0;
    while (f && !off && fgets(line, sizeof line, f)) {
        line[strcspn(line, "\r\n")] = 0;
        off = line[0] && line[0] != '#' && !strcasecmp(line, name);
    }
    if (f) fclose(f);
    return off;
}

void modfs_open(void) {
    if (!g_mods_dir[0]) return;
    char *names[256];
    int n = 0;
    char path[1100];
    snprintf(path, sizeof path, "%s/order.txt", g_mods_dir);
    FILE *f = fopen(path, "r");
    if (f) {                                     /* the order the player set */
        char line[256];
        while (n < 256 && fgets(line, sizeof line, f)) {
            line[strcspn(line, "\r\n")] = 0;
            if (line[0] && line[0] != ';' && line[0] != '#') names[n++] = strdup(line);
        }
        fclose(f);
    } else {                                     /* by name */
        DIR *d = opendir(g_mods_dir);
        struct dirent *e;
        while (d && n < 256 && (e = readdir(d))) {
            if (e->d_name[0] == '.') continue;
            snprintf(path, sizeof path, "%s/%s", g_mods_dir, e->d_name);
            struct stat st;
            if (stat(path, &st) || !S_ISDIR(st.st_mode)) continue;
            if (e->d_name[0] == '_') { note("mods: %s: switched off (its name starts with _)", e->d_name); continue; }
            names[n++] = strdup(e->d_name);
        }
        if (d) closedir(d);
        qsort(names, (size_t)n, sizeof *names, cmp_str);
    }
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof path, "%s/%s", g_mods_dir, names[i]);
        struct stat st;
        if (stat(path, &st) || !S_ISDIR(st.st_mode)) { note("mods: %s (order.txt): no such folder", names[i]); continue; }
        if (names[i][0] == '_') { note("mods: %s: switched off (its name starts with _)", names[i]); free(names[i]); continue; }
        if (mod_switched_off(names[i])) { note("mods: %s: switched off (disabled.txt, the settings' Mods page)", names[i]); free(names[i]); continue; }
        int before = nused;
        mod_file_hash = UINT64_C(14695981039346656037); mod_file_count = 0;
        pc_bitmaps_load(names[i], path);
        walk(names[i], path, "");
        mod_signature = mod_hash(mod_signature, names[i], strlen(names[i]) + 1);
        uint8_t digest[12];
        for (int k = 0; k < 8; k++) digest[k] = (uint8_t)(mod_file_hash >> (k * 8));
        wr32(digest + 8, mod_file_count);
        mod_signature = mod_hash(mod_signature, digest, sizeof digest);
        if (nused > before) nmods++;
        free(names[i]);
    }
    enemies_done();
    packs_done();
    sfx_done();
    music_done();
    texts_done();
    data_done();
    sync_buffers_reserve();
    grow_all();
    sync_buffers_index();
    movie_done();
    memory_prepare();
    room_window_place();
    if (ext_used) {                              /* (kept for the saves loaded later) */
        ext_image = malloc(ext_used);
        if (ext_image) memcpy(ext_image, g_ram + RAM_GAME, ext_used);
        ext_image_n = ext_used;
        note("mods: %u bytes past the game's 2 MB of RAM", ext_used);
    }
    if (nused || nskipped) note("mods: %d files from %d mods, %d not used", nused, nmods, nskipped);
}

int modfs_used(void) { return nused; }
int modfs_skipped(void) { return nskipped; }
int modfs_mods(void) { return nmods; }

/* cd_read_sector_raw: a sector of a file a mod changed (1), or the disc's (0).
 * Past the end of the disc the sector's header is made up too. */
int modfs_sector(uint32_t lba, uint8_t out[2352]) {
    if (disc_only) return 0;
    for (int i = 0; i < ntouched; i++)
        if (lba - touched[i].lba < touched[i].nsec) {
            if (lba >= disc_image_sectors()) {
                static const uint8_t sync[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};
                uint32_t a = lba + 150, mm = a / 4500, ss = a / 75 % 60, ff = a % 75;
                memset(out, 0, 2352);
                memcpy(out, sync, 12);
                out[12] = (uint8_t)(mm / 10 * 16 + mm % 10);
                out[13] = (uint8_t)(ss / 10 * 16 + ss % 10);
                out[14] = (uint8_t)(ff / 10 * 16 + ff % 10);
                out[15] = 2;                      /* mode 2, form 1, data */
                out[18] = out[22] = 8;
            }
            memcpy(out + 24, touched[i].data + (size_t)(lba - touched[i].lba) * 2048, 2048);
            return 1;
        }
    return 0;
}
