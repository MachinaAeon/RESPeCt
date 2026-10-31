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
 * disc's, so loops, cuts and the end stay the game's. What is and isn't
 * used is in the log. */
#include "rt.h"
#include "disc_ids.h"
#include "modfs_pc.h"

#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

char g_mods_dir[1024];                           /* set by the platform; "" = none */

typedef struct { uint32_t lba, nsec; uint8_t *data; const char *name; } Touched;
static Touched touched[64];
static int ntouched;
static int disc_only;                            /* reading the disc's own sectors */
unsigned long mz_crc32(unsigned long crc, const unsigned char *ptr, size_t buf_len);   /* miniz */
static int nused, nmods;

/* Files inside an archive that grow (done once every mod is in). */
typedef struct { int fi; uint32_t offset, size; uint8_t *data; char what[300]; } Grow;
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
            if (!strncasecmp(modfs_pc[i].disc, "B/STGBG", 7) && ngrows < (int)(sizeof grows / sizeof grows[0])) {
                Grow *g = &grows[ngrows++];       /* the room's block is laid out again (grow_all) */
                g->fi = di;
                g->offset = modfs_pc[i].offset;
                g->size = (uint32_t)n;
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
    if (sound_apply(mod, rel, up, path)) return;
    rt_log("mods: %s/%s: not a file of the disc or of the PC version's PSX_DATA: not used", mod, rel);
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
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

/* The stage archives that have files growing: each room block they're in
 * laid out again (its header's offsets, then its files, packed as they
 * were), the archive moved past the end of the disc if it outgrew its
 * place, and TEST.BIN's block table changed to match. */
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
        uint32_t tab = 0, v[64] = {0}, nb = 0, loc = 0;
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
        if (tab && tab < test->nsec * 2048 - 256) {
            for (nb = 0; nb < 63; nb++) {
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
        uint8_t *out = malloc((size_t)t->nsec * 2048 + (size_t)nb * OBJ_ROOM);
        uint32_t nv[64], cur = 0;
        nv[0] = 0;
        for (uint32_t b = 0; b < nb; b++) {
            uint32_t start = v[b] * 2048, len = (v[b + 1] - v[b]) * 2048;
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
            if (ok) {                            /* laid out again */
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
                if (newlen > OBJ_ROOM) {
                    rt_log("mods: %s room %u would be %u bytes, more than the %u the game has for it: not used",
                           disc_files[fi].name, b, newlen, OBJ_ROOM);
                    ok = 0;
                    newlen = len;
                }
            } else if (any)
                rt_log("mods: %s room %u: its layout isn't one this knows: not used", disc_files[fi].name, b);
            if (!ok) memcpy(out + cur, blk, len);
            for (int g = gi; g < ngrows; g++)
                if (grows[g].fi == fi && grows[g].offset - start < len) {
                    if (ok) rt_log("mods: %s: into %s room %u (%u bytes; the room %u -> %u bytes)", grows[g].what, disc_files[fi].name, b, grows[g].size, len, newlen);
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

static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

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
