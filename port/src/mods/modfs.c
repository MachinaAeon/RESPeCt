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
typedef struct Packed Packed;
static Packed *packed_get(int fi, uint32_t block);
static uint8_t *packed_part(Packed *p, int k, uint32_t *n);
static void packed_note(Packed *p, int k, const char *what);
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
static RamFix ramfixes[256];
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

/* The parts, by subject (one translation unit: they share modfs.c's
 * state and helpers; the order matters). */
#include "modfs_sound.inc"
#include "modfs_music.inc"
#include "modfs_pictures.inc"
#include "modfs_texts.inc"
#include "modfs_data.inc"
#include "modfs_movies.inc"
#include "modfs_lzss.inc"
#include "modfs_diary.inc"
#include "modfs_pc_text.inc"
#include "modfs_pc_diary.inc"
#include "modfs_packed.inc"
#include "modfs_enemies.inc"
#include "modfs_extract.inc"

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
