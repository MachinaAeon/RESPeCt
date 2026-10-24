/* The player's disc image, and checking that it's the right disc.
 *
 * Formats: a raw 2352-byte-sector image (.bin/.img), a .cue sheet pointing
 * at one, or a .chd (MAME's compressed format, read with libchdr). An
 * .iso (2048-byte sectors) can't be used: it leaves out the XA audio
 * sectors the game streams its music and voices from.
 *
 * disc_verify() checks the image is the USA release (SLUS-01087) with
 * every file where it should be and the game's code exactly as the port
 * expects (the release build runs that code; see src/interp.c). */
#include "rt.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "libchdr/chd.h"

#include "disc_ids.h"

static FILE *raw;
static uint32_t nsectors;
static chd_file *chd;
static uint8_t *hunk;
static uint32_t hunk_bytes, unit_bytes, frames_per_hunk;
static int64_t hunk_no = -1;

static int64_t file_size(FILE *f) {
#ifdef _WIN32
    _fseeki64(f, 0, SEEK_END);
    return _ftelli64(f);
#else
    fseeko(f, 0, SEEK_END);
    return ftello(f);
#endif
}

static int seek_to(FILE *f, int64_t pos) {
#ifdef _WIN32
    return _fseeki64(f, pos, SEEK_SET);
#else
    return fseeko(f, pos, SEEK_SET);
#endif
}

static int ends_with(const char *s, const char *ext) {
    size_t n = strlen(s), m = strlen(ext);
    if (n < m) return 0;
    for (size_t i = 0; i < m; i++)
        if (tolower((unsigned char)s[n - m + i]) != tolower((unsigned char)ext[i])) return 0;
    return 1;
}

static void close_image(void) {
    if (raw) fclose(raw);
    raw = NULL;
    if (chd) chd_close(chd);
    chd = NULL;
    free(hunk);
    hunk = NULL;
    hunk_no = -1;
    nsectors = 0;
}

static const uint8_t sync_bytes[12] = {0, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0};

static int open_raw(const char *path, char *err, size_t n) {
    raw = fopen(path, "rb");
    if (!raw) { snprintf(err, n, "couldn't open it"); return 0; }
    int64_t size = file_size(raw);
    uint8_t s[2352];
    seek_to(raw, 16 * 2352);
    if (size % 2352 == 0 && fread(s, 1, 2352, raw) == 2352 && !memcmp(s, sync_bytes, 12)) {
        nsectors = (uint32_t)(size / 2352);
        return 1;
    }
    seek_to(raw, 16 * 2048);
    if (size % 2048 == 0 && fread(s, 1, 2048, raw) == 2048 && !memcmp(s + 1, "CD001", 5)) {
        snprintf(err, n, "it's an ISO image (2048-byte sectors), which leaves out the game's music and voices. "
                         "A .bin/.cue or .chd copy of the disc is needed");
    } else {
        snprintf(err, n, "it isn't a CD image this port can read");
    }
    close_image();
    return 0;
}

/* A cue sheet: the image is its first FILE (the game's disc has one track). */
static int open_cue(const char *path, char *err, size_t n) {
    FILE *f = fopen(path, "r");
    if (!f) { snprintf(err, n, "couldn't open it"); return 0; }
    char line[1024], file[1024] = "", mode[64] = "";
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (isspace((unsigned char)*p)) p++;
        if (!strncasecmp(p, "FILE", 4) && !*file) {
            char *q = strchr(p, '"'), *e;
            if (q && (e = strchr(q + 1, '"'))) { *e = 0; snprintf(file, sizeof file, "%s", q + 1); }
            else if (sscanf(p + 4, " %1023s", file) != 1) file[0] = 0;
        } else if (!strncasecmp(p, "TRACK", 5) && !*mode) {
            sscanf(p + 5, " %*s %63s", mode);
        }
    }
    fclose(f);
    if (!*file) { snprintf(err, n, "the cue sheet names no image file"); return 0; }
    if (*mode && strcasecmp(mode, "MODE2/2352") && strcasecmp(mode, "MODE1/2352")) {
        snprintf(err, n, "its first track is %s; a raw MODE2/2352 image is needed", mode);
        return 0;
    }
    char full[2048];
    const char *slash = strrchr(path, '/'), *bslash = strrchr(path, '\\');
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash && !(file[0] == '/' || file[0] == '\\' || (file[0] && file[1] == ':')))
        snprintf(full, sizeof full, "%.*s%s", (int)(slash - path + 1), path, file);
    else
        snprintf(full, sizeof full, "%s", file);
    if (!open_raw(full, err, n)) {
        char inner[512];
        snprintf(inner, sizeof inner, "%s", err);
        snprintf(err, n, "%s (named by the cue sheet): %s", file, inner);
        return 0;
    }
    return 1;
}

static int open_chd(const char *path, char *err, size_t n) {
    chd_error e = chd_open(path, CHD_OPEN_READ, NULL, &chd);
    if (e != CHDERR_NONE) {
        snprintf(err, n, "libchdr couldn't open it (%s)", chd_error_string(e));
        chd = NULL;
        return 0;
    }
    const chd_header *h = chd_get_header(chd);
    hunk_bytes = h->hunkbytes;
    unit_bytes = h->unitbytes;
    if (unit_bytes < 2352 || hunk_bytes % unit_bytes) {
        snprintf(err, n, "it isn't a CD image");
        close_image();
        return 0;
    }
    frames_per_hunk = hunk_bytes / unit_bytes;
    nsectors = (uint32_t)((uint64_t)h->totalhunks * frames_per_hunk);
    hunk = malloc(hunk_bytes);
    return 1;
}

int disc_image_open(const char *path, char *err, size_t n) {
    close_image();
    if (ends_with(path, ".cue")) return open_cue(path, err, n);
    if (ends_with(path, ".chd")) return open_chd(path, err, n);
    if (ends_with(path, ".ccd")) {
        char img[2048];
        snprintf(img, sizeof img, "%.*s.img", (int)strlen(path) - 4, path);
        return open_raw(img, err, n);
    }
    return open_raw(path, err, n);
}

uint32_t disc_image_sectors(void) { return nsectors; }
uint32_t disc_expected_sectors(void) { return DISC_SECTORS; }

int cd_read_sector_raw(uint32_t lba, uint8_t out[2352]) {
    if (lba >= nsectors) { memset(out, 0, 2352); return 0; }
    if (raw) {
        seek_to(raw, (int64_t)lba * 2352);
        return fread(out, 1, 2352, raw) == 2352;
    }
    if (chd) {
        int64_t want = lba / frames_per_hunk;
        if (want != hunk_no) {
            if (chd_read(chd, (uint32_t)want, hunk) != CHDERR_NONE) { hunk_no = -1; memset(out, 0, 2352); return 0; }
            hunk_no = want;
        }
        memcpy(out, hunk + (lba % frames_per_hunk) * unit_bytes, 2352);
        return 1;
    }
    memset(out, 0, 2352);
    return 0;
}

/* ------------------------------------------------------------ checking */

static uint64_t fnv64_update(uint64_t h, const uint8_t *p, size_t n) {
    for (size_t i = 0; i < n; i++) h = (h ^ p[i]) * 0x100000001B3ull;
    return h;
}

/* Find a file by path ("X/VC/VC05.BIN") in the image's ISO 9660 tree. */
static int find_path(const char *name, uint32_t *lba, uint32_t *size) {
    uint8_t s[2352];
    if (!cd_read_sector_raw(16, s) || memcmp(s + 25, "CD001", 5)) return 0;
    const uint8_t *root = s + 24 + 156;
    uint32_t dlba = root[2] | root[3] << 8 | root[4] << 16 | (uint32_t)root[5] << 24;
    uint32_t dlen = root[10] | root[11] << 8 | root[12] << 16 | (uint32_t)root[13] << 24;
    for (;;) {
        const char *slash = strchr(name, '/');
        size_t part = slash ? (size_t)(slash - name) : strlen(name);
        int found = 0;
        for (uint32_t sec = 0; sec * 2048 < dlen && !found; sec++) {
            if (!cd_read_sector_raw(dlba + sec, s)) return 0;
            const uint8_t *d = s + 24;
            for (int o = 0; o < 2048 && d[o];) {
                int len = d[o], nlen = d[o + 32];
                const char *fn = (const char *)d + o + 33;
                int flen = nlen;
                for (int i = 0; i < nlen; i++) if (fn[i] == ';') { flen = i; break; }
                if ((size_t)flen == part && !strncasecmp(fn, name, part)) {
                    dlba = d[o + 2] | d[o + 3] << 8 | d[o + 4] << 16 | (uint32_t)d[o + 5] << 24;
                    dlen = d[o + 10] | d[o + 11] << 8 | d[o + 12] << 16 | (uint32_t)d[o + 13] << 24;
                    found = 1;
                    break;
                }
                o += len;
            }
        }
        if (!found) return 0;
        if (!slash) { *lba = dlba; *size = dlen; return 1; }
        name = slash + 1;
    }
}

/* 1: the right disc. 0: err says what's wrong, for the player. */
int disc_verify(char *err, size_t n) {
    uint32_t lba, size;
    uint8_t s[2352];
    if (!find_path("SYSTEM.CNF", &lba, &size) || !cd_read_sector_raw(lba, s)) {
        snprintf(err, n, "it isn't a PlayStation disc image");
        return 0;
    }
    char cnf[129] = {0};
    memcpy(cnf, s + 24, size < 128 ? size : 128);
    for (char *p = cnf; *p; p++) *p = (char)toupper((unsigned char)*p);
    if (!strstr(cnf, "SLUS_010.87")) {
        if (strstr(cnf, "SLES_027") || strstr(cnf, "SLES_02"))
            snprintf(err, n, "it's the European release of the game; this port needs the USA release (SLUS-01087)");
        else if (strstr(cnf, "SLPS_"))
            snprintf(err, n, "it's the Japanese release (Biohazard Gun Survivor); this port needs the USA release (SLUS-01087)");
        else
            snprintf(err, n, "it's a different PlayStation game; this port needs Resident Evil Survivor (USA, SLUS-01087)");
        return 0;
    }
    if (nsectors < DISC_SECTORS) {
        snprintf(err, n, "the image is incomplete (%u of %u sectors)", nsectors, DISC_SECTORS);
        return 0;
    }
    for (size_t i = 0; i < sizeof disc_files / sizeof disc_files[0]; i++) {
        if (!find_path(disc_files[i].name, &lba, &size) || lba != disc_files[i].lba || size != disc_files[i].size) {
            snprintf(err, n, "it's not an exact copy of the USA disc (%s differs). A good dump (Redump) is needed",
                     disc_files[i].name);
            return 0;
        }
        if (!disc_files[i].hash) continue;
        uint64_t h = 0xCBF29CE484222325ull;
        for (uint32_t k = 0; k * 2048 < size; k++) {
            if (!cd_read_sector_raw(lba + k, s)) { snprintf(err, n, "it couldn't be read to the end"); return 0; }
            uint32_t m = size - k * 2048 < 2048 ? size - k * 2048 : 2048;
            h = fnv64_update(h, s + 24, m);
        }
        if (h != disc_files[i].hash) {
            snprintf(err, n, "it's not an exact copy of the USA disc (%s differs). A good dump (Redump) is needed",
                     disc_files[i].name);
            return 0;
        }
    }
    return 1;
}
