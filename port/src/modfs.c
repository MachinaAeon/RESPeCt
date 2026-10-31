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

#include <ctype.h>
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
static Touched touched[64];
static int ntouched;
static int disc_only;                            /* reading the disc's own sectors */
unsigned long mz_crc32(unsigned long crc, const unsigned char *ptr, size_t buf_len);   /* miniz */
static int nused, nmods;

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
 * only copy of it in the game's code). A block can grow to the biggest
 * of its kind the game itself loads (kinds: the block's number modulo
 * `kinds`): the doors' are by turns a door's model, loaded below
 * 0x80150000 (room_change.c), and its data at 0x80150000; the effects'
 * blocks (shot_effects.c) all go to one buffer. */
static const struct { const char *name; uint32_t loc, tab, kinds; } archives[] = {
    {"B/DOORANM.BIN", 0x8005857Cu, 0x800A42F4u, 2},
    {"EFFECTX.BIN", 0x80058544u, 0x800A6F1Cu, 1},
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
            rt_log("mods: %s/%s: game code or a stream, can't be replaced", mod, rel);
            return;
        }
        uint8_t *d = load(path, &n);
        Touched *t = d ? touch(fi) : NULL;
        if (!t) { free(d); rt_log("mods: %s/%s: couldn't read it", mod, rel); return; }
        if ((uint32_t)n > t->nsec * 2048) {
            rt_log("mods: %s/%s: %ld bytes, %u more than its place on the disc: not used", mod, rel, n,
                   (unsigned)((uint32_t)n - t->nsec * 2048));
            free(d);
            return;
        }
        memset(t->data, 0, (size_t)t->nsec * 2048);
        memcpy(t->data, d, (size_t)n);
        free(d);
        rt_log("mods: %s/%s: in place of the disc's (%ld bytes)", mod, rel, n);
        nused++;
        return;
    }
    for (size_t i = 0; i < sizeof modfs_pc / sizeof modfs_pc[0]; i++) {
        if (strcmp(modfs_pc[i].pc, up)) continue;
        int di = disc_index(modfs_pc[i].disc);
        uint8_t *d = di >= 0 ? load(path, &n) : NULL;
        Touched *t = d ? touch(di) : NULL;
        if (!t) { free(d); rt_log("mods: %s/%s: couldn't read it", mod, rel); return; }
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
            rt_log("mods: %s/%s: the disc's %s isn't the one expected there: not used", mod, rel, modfs_pc[i].disc);
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
            rt_log("mods: %s/%s: %ld bytes, %u more than the original's place in %s: not used", mod, rel, n,
                   (unsigned)((uint32_t)n - modfs_pc[i].size), modfs_pc[i].disc);
            free(d);
            return;
        }
        memset(at, 0, modfs_pc[i].size);
        memcpy(at, d, (size_t)n);
        free(d);
        rt_log("mods: %s/%s: into %s at %u (%ld bytes)", mod, rel, modfs_pc[i].disc, modfs_pc[i].offset, n);
        nused++;
        return;
    }
    if (bmp_apply(mod, rel, up, path)) return;
    if (sound_apply(mod, rel, up, path)) return;
    if (movie_apply(mod, rel, up, path)) return;
    rt_log("mods: %s/%s: not a file of the disc or of the PC version's PSX_DATA: not used", mod, rel);
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
        uint32_t tab = 0, nb = 0, loc = 0, kinds = 1;
        static uint32_t v[1024];
        for (size_t k = 0; k < sizeof archives / sizeof archives[0]; k++)
            if (!strcasecmp(disc_files[fi].name, archives[k].name)) {
                tab = archives[k].tab - TEST_BASE;
                loc = archives[k].loc;
                kinds = archives[k].kinds;
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
                    rt_log("mods: %s: bigger than the original, and %s's block table wasn't found: not used", grows[g].what, disc_files[fi].name);
                    grows[g].fi = -1;
                    nused--;
                }
            continue;
        }
        /* how big each kind of block can be */
        uint32_t cap[4] = {OBJ_ROOM, OBJ_ROOM, OBJ_ROOM, OBJ_ROOM}, extra = 0;
        if (kinds != 1 || strncasecmp(disc_files[fi].name, "B/STGBG", 7)) {
            memset(cap, 0, sizeof cap);
            for (uint32_t b = 0; b < nb; b++)
                if ((v[b + 1] - v[b]) * 2048 > cap[b % kinds]) cap[b % kinds] = (v[b + 1] - v[b]) * 2048;
        }
        for (uint32_t b = 0; b < nb; b++) extra += cap[b % kinds];
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
            for (int g = gi; !ok && g < ngrows; g++)
                if (grows[g].fi == fi && grows[g].offset == start && (grows[g].orig + 2047) / 2048 * 2048 == len) whole = &grows[g];
            if (whole) {
                newlen = (whole->size + 2047) & ~2047u;
                if (newlen > room) {
                    rt_log("mods: %s block %u would be %u bytes, more than the %u the game has for it: not used",
                           disc_files[fi].name, b, newlen, room);
                    newlen = len;
                    whole = NULL;
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
                    rt_log("mods: %s block %u would be %u bytes, more than the %u the game has for it: not used",
                           disc_files[fi].name, b, newlen, room);
                    ok = 0;
                    newlen = len;
                }
            } else if (any)
                rt_log("mods: %s block %u: its layout isn't one this knows: not used", disc_files[fi].name, b);
            if (!ok) memcpy(out + cur, blk, len);
            for (int g = gi; g < ngrows; g++)
                if (grows[g].fi == fi && grows[g].offset - start < len) {
                    if (ok) rt_log("mods: %s: into %s block %u (%u bytes; the block %u -> %u bytes)", grows[g].what, disc_files[fi].name, b, grows[g].size, len, newlen);
                    else nused--;
                }
            cur += newlen;
            nv[b + 1] = cur / 2048;
        }
        for (int g = gi; g < ngrows; g++)
            if (grows[g].fi == fi) { free(grows[g].data); grows[g].fi = -1; }
        uint32_t nsec = cur / 2048;
        if (nsec == t->nsec && !memcmp(out, t->data, cur)) { free(out); continue; }
        for (uint32_t b = 0; b <= nb; b++) wr32(test->data + tab + 4 * b, nv[b]);   /* the block table */
        free(t->data);
        t->data = out;
        if (nsec > t->nsec) {                    /* past the end of the disc: its place in KERNEL.BIN's table and its directory entry told */
            uint32_t sec, off;
            Touched *dir = iso_record(disc_files[fi].name, &sec, &off) ? touch_lba(sec, 1, "directory") : NULL;
            Touched *kern = touch(disc_index("KERNEL.BIN"));
            uint32_t ko = kern ? loc - rd32(kern->data + 0x18) + 0x800 : 0;
            uint32_t a = t->lba + 150;
            uint8_t want[3] = {(uint8_t)(a / 4500 / 10 * 16 + a / 4500 % 10), (uint8_t)(a / 75 % 60 / 10 * 16 + a / 75 % 60 % 10),
                               (uint8_t)(a % 75 / 10 * 16 + a % 75 % 10)};
            if (!dir || !kern || ko + 4 > kern->nsec * 2048 || memcmp(kern->data + ko, want, 3)) {
                rt_log("mods: %s: where the game finds it wasn't found: the bigger files in it not used", disc_files[fi].name);
                continue;
            }
            t->lba = vbase + vnext;
            vnext += nsec + 16;
            a = t->lba + 150;
            kern->data[ko] = (uint8_t)(a / 4500 / 10 * 16 + a / 4500 % 10);
            kern->data[ko + 1] = (uint8_t)(a / 75 % 60 / 10 * 16 + a / 75 % 60 % 10);
            kern->data[ko + 2] = (uint8_t)(a % 75 / 10 * 16 + a % 75 % 10);
            wr32(dir->data + off + 2, t->lba);
            wr32be(dir->data + off + 6, t->lba);
            wr32(dir->data + off + 10, nsec * 2048);
            wr32be(dir->data + off + 14, nsec * 2048);
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
        rt_log("mods: %s/%s: name it by the channel's number (00.wav to 31.wav): not used", mod, rel);
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
        rt_log("mods: %s/%s: %s has no sound on channel %ld: not used", mod, rel, stream, chan);
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
        rt_log("mods: %s/%s: %s: not used", mod, rel, pcm ? "too many sounds" : why);
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
    rt_log("mods: %s/%s: in place of %s channel %ld (%.1f s; the WAV %.1f s%s)", mod, rel, stream, chan, secs,
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

static int bmp_apply(const char *mod, const char *rel, const char *up, const char *path) {
    size_t i = 0;
    for (; i < sizeof modfs_bmp / sizeof modfs_bmp[0]; i++)
        if (!strcmp(modfs_bmp[i].pc, up)) break;
    if (i == sizeof modfs_bmp / sizeof modfs_bmp[0]) return 0;
    int di = disc_index(modfs_bmp[i].disc);
    Touched *t = di >= 0 ? touch(di) : NULL;
    uint8_t *tim = t ? t->data + modfs_bmp[i].offset : NULL;
    if (!tim || (uint32_t)mz_crc32(0, tim, modfs_bmp[i].size) != modfs_bmp[i].crc) {
        /* it may be a picture an earlier mod changed: then the TIM's header is still the one expected */
        if (!tim || rd32(tim) != 0x10) {
            rt_log("mods: %s/%s: the disc's %s isn't the one expected there: not used", mod, rel, modfs_bmp[i].disc);
            return 1;
        }
    }
    long n = 0;
    uint8_t *d = load(path, &n);
    int w, h, c;
    uint8_t *img = d ? stbi_load_from_memory(d, (int)n, &w, &h, &c, 4) : NULL;
    free(d);
    if (!img) {
        rt_log("mods: %s/%s: couldn't read it as a picture: not used", mod, rel);
        return 1;
    }
    /* the TIM: its palette (if any), then its pixels */
    uint32_t flag = rd32(tim + 4), bpp = (uint32_t[]){4, 8, 16, 24}[flag & 3];
    uint8_t *clut = flag & 8 ? tim + 8 : NULL;
    uint32_t cw = clut ? rd16(clut + 8) : 0, chs = clut ? rd16(clut + 10) : 0;
    uint8_t *pix = clut ? clut + rd32(clut) : tim + 8;
    uint32_t tw = rd16(pix + 8) * 16 / bpp, th = rd16(pix + 10);
    uint8_t *px = pix + 12;
    if (bpp == 24 || (bpp < 16 && (!clut || cw < (1u << bpp)))) {
        rt_log("mods: %s/%s: the TIM's format isn't one this writes: not used", mod, rel);
        stbi_image_free(img);
        return 1;
    }
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
    stbi_image_free(img);
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
    const char *how;
    if (bpp == 16) {
        for (uint32_t k = 0; k < tw * th; k++) wr16(px + k * 2, col[k] ? (uint16_t)(col[k] | stp) : 0);
        how = "16-bit";
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
            how = bpp == 8 ? "256 colours" : "16 colours";
        } else {                                 /* the TIM's own palettes kept */
            for (int k = 0; k < ncol; k++) pal[k] = rd16(row0 + k * 2) & 0x7FFF;
            how = "its own palettes";
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
    rt_log("mods: %s/%s: into %s's picture at %u (%ux%u, %s%s)", mod, rel, modfs_bmp[i].disc, modfs_bmp[i].offset, tw, th, how,
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
        rt_log("mods: %s/%s: name it by the frame's number (0001.png, 0002.png...): not used", mod, rel);
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
        rt_log("mods: %s: pictures for %d frames (%u to %u)", disc_files[pics[i].fi].name, j - i, pics[i].frame,
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
            rt_log("mods: %s: %s: not shown", p->path, img ? "bigger than 4096x4096" : "couldn't read it as a picture");
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
        else if (*rel || (strcasecmp(e->d_name, "readme.txt") && strcasecmp(e->d_name, "readme.md")))
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
            if (!stat(path, &st) && S_ISDIR(st.st_mode)) names[n++] = strdup(e->d_name);
        }
        if (d) closedir(d);
        qsort(names, (size_t)n, sizeof *names, cmp_str);
    }
    for (int i = 0; i < n; i++) {
        snprintf(path, sizeof path, "%s/%s", g_mods_dir, names[i]);
        struct stat st;
        if (stat(path, &st) || !S_ISDIR(st.st_mode)) { rt_log("mods: %s (order.txt): no such folder", names[i]); continue; }
        int before = nused;
        walk(names[i], path, "");
        if (nused > before) nmods++;
        free(names[i]);
    }
    grow_all();
    movie_done();
    if (nused) rt_log("mods: %d files from %d mods", nused, nmods);
}

int modfs_used(void) { return nused; }
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
