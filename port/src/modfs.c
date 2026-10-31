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
 * object file can take in memory (0x80108000-0x80150000). Files that hold
 * game code (the game runs translated, from what it read) can't be
 * replaced, nor streams whole; their sound goes by channel instead:
 * X/BGM_32.STR/05.wav is what the game plays from that file's channel 5
 * (music, voices, a movie's sound), sample for sample in step with the
 * disc's, so loops, cuts and the end stay the game's; and their pictures
 * by frame: M/OPENING.STR/0042.png is shown when the game shows that
 * stream's frame 42. What is and isn't used is in the log. */
#include "rt.h"
#include "disc_ids.h"
#include "modfs_pc.h"
#include "modfs_bmp.h"
#include "modfs_pack.h"

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
typedef struct { int fi; uint32_t offset, size, orig; uint8_t *data; char what[300]; } Grow;
static Grow grows[256];
static int ngrows;
static uint32_t vnext;                           /* sectors used past the end of the disc */
uint32_t disc_image_sectors(void);               /* discimg.c */

static int disc_index(const char *name) {
    for (size_t i = 0; i < sizeof disc_files / sizeof disc_files[0]; i++)
        if (!strcasecmp(disc_files[i].name, name)) return (int)i;
    return -1;
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
static int movie_apply(const char *mod, const char *rel, const char *up, const char *path);
static int bmp_apply(const char *mod, const char *rel, const char *up, const char *path);
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
 * their block table (the first sector of each block, then the end). A
 * block can grow to the biggest of its kind the game itself loads (kinds:
 * the block's number modulo `kinds`), or to `cap`: the doors' are by
 * turns a door's model, loaded below 0x80150000 (room_change.c), and its
 * data at 0x80150000; the effects' blocks (shot_effects.c) all go to one
 * buffer; an enemy's block (OBJ.DAT, type 30's in EV_OBJ.DAT) is read
 * into 0x800F8000 and unpacked from there (room_loader.c), as big as the
 * biggest of them (type 18's, 93 sectors). OBJ.DAT's table has copies in
 * many of the game's modules (the overlays each have theirs): `copies`
 * has every copy found in them changed too. */
static const struct { const char *name; uint32_t loc, tab, kinds, cap; int copies; } archives[] = {
    {"B/DOORANM.BIN", 0x8005857Cu, 0x800A42F4u, 2, 0, 0},
    {"EFFECTX.BIN", 0x80058544u, 0x800A6F1Cu, 1, 0, 0},
    {"OBJ.DAT", 0x80058530u, 0x800A0CD8u, 1, 93u * 2048, 1},
    {"EV_OBJ.DAT", 0x80058534u, 0x800A195Cu, 1, 93u * 2048, 1},
    {"ITEMDAT.DAT", 0x8005855Cu, 0x800A3EDCu, 1, 0, 1},
};

static int can_grow(const char *disc) {
    if (!strncasecmp(disc, "B/STGBG", 7)) return 1;
    for (size_t k = 0; k < sizeof archives / sizeof archives[0]; k++)
        if (!strcasecmp(disc, archives[k].name)) return 1;
    return 0;
}

static void apply(const char *mod, const char *rel, const char *path) {
    char up[512];
    size_t k = 0;
    for (; rel[k] && k < sizeof up - 1; k++) up[k] = rel[k] == '\\' ? '/' : (char)toupper((unsigned char)rel[k]);
    up[k] = 0;
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
        nused++;
        return;
    }
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
        nused++;
        return;
    }
    if (bmp_apply(mod, rel, up, path)) return;
    if (enemy_apply(mod, rel, up, path)) return;
    if (pack_apply(mod, rel, up, path)) return;
    if (sound_apply(mod, rel, up, path)) return;
    if (movie_apply(mod, rel, up, path)) return;
    note("mods: %s/%s: not a file of the disc or of the PC version's PSX_DATA: not used", mod, rel);
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
static RamFix ramfixes[64];
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
    if (fixed) note("mods: the save was made without some of these mods: %d places in memory brought up to date", fixed);
}

/* The archives that have files growing: each block they're in laid out
 * again (a room's or an effect set's header's offsets, then its files,
 * packed as they were; or the one file a door's block is), the archive
 * moved past the end of the disc if it outgrew its place, and TEST.BIN's
 * block table changed to match. */
static void grow_all(void) {
    if (!ngrows) return;
    Touched *test = touch(disc_index("TEST.BIN"));
    if (!test) return;
    uint32_t vbase = disc_image_sectors() > DISC_SECTORS ? disc_image_sectors() : DISC_SECTORS;
    for (int gi = 0; gi < ngrows; gi++) {
        int fi = grows[gi].fi;
        if (fi < 0) continue;                    /* done with another */
        Touched *t = touch(fi);
        const char *base = strrchr(disc_files[fi].name, '/') + 1;
        /* its block table, from TEST.BIN's list of the stages' files */
        uint32_t tab = 0, nb = 0, loc = 0, kinds = 1, fixed = 0;
        int copies = 0;
        static uint32_t v[1024];
        for (size_t k = 0; k < sizeof archives / sizeof archives[0]; k++)
            if (!strcasecmp(disc_files[fi].name, archives[k].name)) {
                tab = archives[k].tab - TEST_BASE;
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
        if (tab && tab < test->nsec * 2048 - 4096) {
            for (nb = 0; nb < 1023; nb++) {
                v[nb] = rd32(test->data + tab + 4 * nb);
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
        uint32_t cap[4] = {OBJ_ROOM, OBJ_ROOM, OBJ_ROOM, OBJ_ROOM}, extra = 0;
        if (fixed)
            cap[0] = cap[1] = cap[2] = cap[3] = fixed;
        else if (kinds != 1 || strncasecmp(disc_files[fi].name, "B/STGBG", 7)) {
            memset(cap, 0, sizeof cap);
            for (uint32_t b = 0; b < nb; b++)
                if ((v[b + 1] - v[b]) * 2048 > cap[b % kinds]) cap[b % kinds] = (v[b + 1] - v[b]) * 2048;
        }
        for (int g = gi; g < ngrows; g++)        /* room for the blocks that grow */
            if (grows[g].fi == fi) extra += cap[0] > cap[1] ? (cap[0] > cap[2] ? cap[0] : cap[2]) : (cap[1] > cap[2] ? cap[1] : cap[2]);
        uint8_t *out = malloc((size_t)t->nsec * 2048 + extra);
        static uint32_t nv[1024];
        uint32_t cur = 0;
        nv[0] = 0;
        for (uint32_t b = 0; b < nb; b++) {
            uint32_t start = v[b] * 2048, len = (v[b + 1] - v[b]) * 2048, room = cap[b % kinds];
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
                if (grows[g].fi == fi && grows[g].offset == start && (grows[g].orig + 2047) / 2048 * 2048 == len) whole = &grows[g];
            if (whole) {
                newlen = (whole->size + 2047) & ~2047u;
                if (newlen < len) newlen = len;   /* as it was, if it fits */
                if (newlen > room) {
                    note("mods: %s block %u would be %u bytes, more than the %u the game has for it: not used",
                           disc_files[fi].name, b, newlen, room);
                    newlen = len;
                    whole = NULL;
                    ok = 0;
                } else {
                    memcpy(out + cur, whole->data, whole->size);
                    memset(out + cur + whole->size, 0, newlen - whole->size);
                    ok = 1;
                }
            } else if (ok) {                     /* laid out again */
                uint8_t *o = out + cur;
                memcpy(o, blk, off[0]);
                uint32_t at = off[0];
                for (uint32_t i = 0; i < c; i++) {
                    uint32_t sz = (i + 1 < c ? off[i + 1] : len) - off[i];
                    const uint8_t *src = blk + off[i];
                    for (int g = gi; g < ngrows; g++)  /* the last mod wins */
                        if (grows[g].fi == fi && grows[g].offset == start + off[i]) { src = grows[g].data; sz = grows[g].size; }
                    wr32(o + 4 + 4 * i, at);
                    memcpy(o + at, src, sz);
                    at += (sz + 3) & ~3u;
                }
                newlen = (at + 2047) & ~2047u;
                memset(o + at, 0, newlen - at);
                if (newlen > room) {
                    note("mods: %s block %u would be %u bytes, more than the %u the game has for it: not used",
                           disc_files[fi].name, b, newlen, room);
                    ok = 0;
                    newlen = len;
                }
            } else if (any)
                note("mods: %s block %u: its layout isn't one this knows: not used", disc_files[fi].name, b);
            if (!ok) memcpy(out + cur, blk, len);
            for (int g = gi; g < ngrows; g++)
                if (grows[g].fi == fi && grows[g].offset - start < len) {
                    if (ok) note("mods: %s: into %s block %u (%u bytes; the block %u -> %u bytes)", grows[g].what, disc_files[fi].name, b, grows[g].size, len, newlen);
                    else nused--;
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
                continue;
            }
        }
        uint8_t was[4096], now[4096];               /* the table, for saves made before (modfs_state_loaded) */
        uint32_t tn = 4 * (nb + 1) <= sizeof was ? 4 * (nb + 1) : 0;
        if (tn) memcpy(was, test->data + tab, tn);
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
        for (uint32_t b = 0; b <= nb; b++) wr32(test->data + tab + 4 * b, nv[b]);   /* the block table */
        if (tn) {
            memcpy(now, test->data + tab, tn);
            ram_fix(TEST_BASE + tab, was, now, tn);
            if (copies) ram_fix(0, was, now, tn);
        }
        free(t->data);
        t->data = out;
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
    for (long o = 12; o + 8 <= n;) {
        uint32_t len = rd32(d + o + 4);
        if (!memcmp(d + o, "fmt ", 4)) fmt = d + o + 8;
        if (!memcmp(d + o, "data", 4)) { data = d + o + 8; dlen = len; if ((long)(o + 8 + len) > n) dlen = (uint32_t)(n - o - 8); }
        o += 8 + len + (len & 1);
    }
    if (!fmt || !data) { *why = "no format or no samples"; return NULL; }
    uint32_t chans = rd16(fmt + 2), srate = rd32(fmt + 4), bits = rd16(fmt + 14);
    if (rd16(fmt) != 1 || bits != 16 || chans < 1 || chans > 2 || !srate) { *why = "not 16-bit PCM, mono or stereo"; return NULL; }
    uint32_t in = dlen / (2 * chans);
    uint32_t out = (uint32_t)((uint64_t)in * rate / srate);
    int16_t *pcm = malloc((size_t)(out ? out : 1) * 4);
    if (!pcm) { *why = "out of memory"; return NULL; }
    for (uint32_t i = 0; i < out; i++) {
        uint64_t pos = (uint64_t)i * srate * 65536 / rate;
        uint32_t a = (uint32_t)(pos >> 16), f = (uint32_t)(pos & 0xFFFF), b = a + 1 < in ? a + 1 : a;
        for (int c = 0; c < 2; c++) {
            uint32_t cc = chans == 2 ? (uint32_t)c : 0;
            int32_t va = (int16_t)rd16(data + (a * chans + cc) * 2), vb = (int16_t)rd16(data + (b * chans + cc) * 2);
            pcm[2 * i + c] = (int16_t)(va + (((vb - va) * (int32_t)f) >> 16));
        }
    }
    *frames = out;
    return pcm;
}

/* X/BGM_32.STR/05.wav: channel 5 of that stream. */
static int sound_apply(const char *mod, const char *rel, const char *up, const char *path) {
    const char *sl = strrchr(up, '/');
    if (!sl || strcasecmp(up + strlen(up) - 4, ".WAV")) return 0;
    char stream[512];
    snprintf(stream, sizeof stream, "%.*s", (int)(sl - up), up);
    int fi = disc_index(stream);
    if (fi < 0 || !strstr(stream, ".STR")) return 0;
    char *end;
    long chan = strtol(sl + 1, &end, 10);
    if (end == sl + 1 || strcasecmp(end, ".WAV") || chan < 0 || chan > 31) {
        note("mods: %s/%s: name it by the channel's number (00.wav to 31.wav): not used", mod, rel);
        return 1;
    }
    /* the channel's sectors, in order, and how they're coded */
    uint32_t lba = disc_files[fi].lba, nsec = (disc_files[fi].size + 2047) / 2048, count = 0, rate = 0, per = 0;
    int32_t *index = malloc(nsec * sizeof *index);
    uint8_t sec[2352];
    disc_only = 1;
    for (uint32_t k = 0; index && k < nsec; k++) {
        index[k] = -1;
        if (!cd_read_sector_raw(lba + k, sec)) continue;
        if (sec[15] != 2 || !(sec[18] & 0x04) || sec[17] != chan) continue;
        index[k] = (int32_t)count++;
        if (!rate) {
            uint8_t c = sec[19];
            rate = (c >> 2) & 1 ? 18900 : 37800;
            per = (((c >> 4) & 3) == 1 ? 4 : 8) * 28 * 18 / ((c & 3) == 1 ? 2 : 1);
        }
    }
    disc_only = 0;
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
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < n; i++) {
        int dr = (int)(pal[i] & 31) - (v & 31), dg = (int)((pal[i] >> 5) & 31) - ((v >> 5) & 31);
        int db = (int)((pal[i] >> 10) & 31) - ((v >> 10) & 31);
        int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (pal[i] && d < bd) { bd = d; best = i; }
    }
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
    const uint8_t *row0 = clut ? clut + 12 : NULL;
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
    for (uint32_t k = 0; k < tw * th; k++) {
        uint32_t p = rgba[k];
        int clear = (p >> 24) < 128 || ((p & 0xFFFFFF) == 0 && was[k] == 0);
        col[k] = clear ? 0 : to15(p);
        opaque += !clear;
    }
    free(rgba);
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
        if (chs == 1) {                          /* a palette for this picture: entry 0 see-through */
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
    const char *how = "", *why = tim_from_picture(tim, img, (uint32_t)w, (uint32_t)h, &how);
    stbi_image_free(img);
    if (why) {
        note("mods: %s/%s: %s: not used", mod, rel, why);
        return 1;
    }
    uint32_t tw = 0, th = 0;
    tim_size(tim, &tw, &th);
    note("mods: %s/%s: into %s's picture at %u (%ux%u, %s%s)", mod, rel, modfs_bmp[i].disc, modfs_bmp[i].offset, tw, th, how,
           (uint32_t)w != tw || (uint32_t)h != th ? ", scaled to the PS1's size" : "");
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

/* M/OPENING.STR/0042.png: frame 42 of that stream. */
static int movie_apply(const char *mod, const char *rel, const char *up, const char *path) {
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

/* display.c, once a frame: which frame the screen shows; written out
 * when dumping (g_movie_dump/M/OPENING.STR/0042.png, the picture as the
 * PS1 shows it). */
void modfs_movie_vblank(void) {
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
    if (!p) return NULL;
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
static uint8_t *lz_unpack(const uint8_t *s, uint32_t avail, uint32_t *n) {
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
            for (uint32_t k = 0; k < len && o < size + 34; k++, o++) d[o] = o >= dist ? d[o - dist] : 0;
        }
        flags >>= 1;
    }
    if (d && o < size) { free(d); return NULL; }
    *n = size;
    return d;
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
    uint32_t plen[3];
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
        if (p->part[k] && (p->was[k] = malloc(p->plen[k]))) memcpy(p->was[k], p->part[k], p->plen[k]);
    }
    *n = p->plen[k];
    return p->part[k];
}

static void packed_note(Packed *p, int k, const char *what) {
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
                int same = !p->part[k] || (p->was[k] && !memcmp(p->was[k], p->part[k], p->plen[k]));   /* the disc's packing kept */
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
static const char *nac_into(const uint8_t *nac, long n, uint8_t *A, uint32_t na) {
    if (n < 8 || na < 12 || rd32(A) != 2) return "the disc's animations aren't laid out as expected";
    uint32_t a0 = rd32(A + 4), a1 = rd32(A + 8);
    if (a0 > a1 || a1 > na) return "the disc's animations aren't laid out as expected";
    uint32_t ncmd = (a1 - a0) / 4, nkey = (na - a1) / 20, nk = rd32(nac);
    if ((uint64_t)nk * 112 + 8 > (uint64_t)n) return "it's cut short";
    uint32_t nc = rd32(nac + 4 + (size_t)nk * 112);
    if (4 + (uint64_t)nk * 112 + 4 + (uint64_t)nc * 8 != (uint64_t)n) return "it's cut short or has more after it";
    if (nc != ncmd || nk < nkey)
        return "its animations aren't as many as the PS1's (new animations or keys can't be added; change the existing ones)";
    const uint8_t *cmd = nac + 4 + (size_t)nk * 112 + 4;
    for (uint32_t i = 0; i < ncmd; i++) wr32(A + a0 + 4 * i, rd32(cmd + 8 * i));
    for (uint32_t k = 0; k < nkey; k++) {
        const uint8_t *f = nac + 4 + (size_t)k * 112;
        uint8_t *p = A + a1 + 20 * k;
        float v[12];
        memcpy(v, f, sizeof v);
        for (int c = 0; c < 3; c++) {
            int32_t t = (int32_t)lrintf(v[9 + c]);
            if (!(rd32(p + 4 * c) == 0x7FFFFFFFu && t == 0)) wr32(p + 4 * c, (uint32_t)t);
            wr16(p + 12 + 2 * c, (uint16_t)(int16_t)lrintf(v[5 + c]));
        }
    }
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

/* Each enemy a mod changes, converted into its block (packed again in
 * packs_done). */
static void enemies_done(void) {
    for (int i = 0; i < nenemies; i++) {
        Enemy *e = &enemies[i];
        int nk = !!e->data[0] + !!e->data[1] + !!e->data[2];
        const char *what = e->data[0] ? e->what[0] : e->data[1] ? e->what[1] : e->what[2];
        int fi = disc_index(e->type == 30 ? "EV_OBJ.DAT" : "OBJ.DAT");
        Packed *p = e->type >= 0 && e->type <= 30 ? packed_get(fi, e->type == 30 ? 0x196 : (uint32_t)e->type) : NULL;
        if (!p || !p->split) {
            note("mods: %s: enemy type %d isn't one the PS1 keeps in this form (types 0-30 are): not used", what, e->type);
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
        if (e->data[0] && e->type != 30) {      /* the NAB's shape (type 30's model on the disc isn't the NAB's) */
            uint32_t nt = 0, n2 = 0, tw = 0, th = 0;
            uint8_t *tex = packed_part(p, 0, &nt), *skin = tex ? enemy_skin(tex, nt) : NULL;
            if (skin) tim_size(skin, &tw, &th);
            u = packed_part(p, 2, &n2);
            int ntri = 0, moved = 0;
            why = u ? nab_shape(e->data[0], e->n[0], u, n2, tw, &ntri, &moved) : "the disc's model couldn't be unpacked";
            if (why) note("mods: %s: its shape: %s: not used (its textures are)", e->what[0], why);
            else if (p->was[2] && memcmp(p->was[2], u, n2)) {
                packed_note(p, 2, e->what[0]);
                note("mods: %s: its shape in place of enemy type %d's (%d polygons; %d vertex coordinates moved)", e->what[0], e->type, ntri, moved);
            }
        } else if (e->data[0])
            note("mods: %s: type 30's model on the disc isn't the one in the NAB: its shape isn't taken", e->what[0]);
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
            why = u ? nac_into(e->data[1], e->n[1], u, n) : "the disc's animations couldn't be unpacked";
            if (why) { note("mods: %s: %s: not used", e->what[1], why); nused--; }
            else {
                packed_note(p, 1, e->what[1]);
                note("mods: %s: its animations in place of enemy type %d's", e->what[1], e->type);
            }
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
        else if (*rel || disc_index(e->d_name) >= 0)   /* at the top only the disc's own files (the rest: readme, pictures...) */
            apply(mod, r, path);
    }
    closedir(d);
}

static int cmp_str(const void *a, const void *b) { return strcasecmp(*(char *const *)a, *(char *const *)b); }

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
        int before = nused;
        walk(names[i], path, "");
        if (nused > before) nmods++;
        free(names[i]);
    }
    enemies_done();
    packs_done();
    grow_all();
    movie_done();
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
