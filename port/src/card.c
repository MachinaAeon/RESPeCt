/* Memory cards through the BIOS (psx-spx "Memory Card Data Format",
 * "BIOS Memory Card Functions"), backed by 128 KB raw images: the same
 * .mcd format DuckStation and most emulators use.
 *
 * Slot 1 = memcard1.mcd (created formatted if missing), slot 2 =
 * memcard2.mcd (present only if the file exists). Card operations finish
 * at once and report through the usual card events. */
#include "rt.h"
#include "savestate.h"

#include <stdio.h>
#include <stdlib.h>

#define CARD_SIZE 0x20000
#define FRAME 128
#define BLOCK 0x2000

void bios_deliver_event(uint32_t cls, uint32_t spec);

static uint8_t card[2][CARD_SIZE];
static int present[2], loaded;
static char path[2][512];
static const char *card_dir = "";

/* Events: software card events (libcard) and hardware ones. */
#define SW_CARD 0xF4000001u
#define HW_CARD 0xF0000011u
#define EV_IOE 0x0004
#define EV_ERROR 0x8000
#define EV_TIMEOUT 0x0100
#define EV_NEW 0x2000

static void card_event(uint32_t spec) {
    bios_deliver_event(SW_CARD, spec);
    bios_deliver_event(HW_CARD, spec);
}

static uint8_t frame_xor(const uint8_t *f) {
    uint8_t x = 0;
    for (int i = 0; i < 127; i++) x ^= f[i];
    return x;
}

static void format_card(uint8_t *c) {
    memset(c, 0, CARD_SIZE);
    c[0] = 'M';
    c[1] = 'C';
    c[127] = frame_xor(c);
    for (int i = 1; i < 16; i++) {
        uint8_t *f = c + i * FRAME;
        f[0] = 0xA0;
        f[8] = f[9] = 0xFF;
        f[127] = frame_xor(f);
    }
    for (int i = 16; i < 36; i++) {
        uint8_t *f = c + i * FRAME;
        memset(f, 0, FRAME);
        f[0] = f[1] = f[2] = f[3] = 0xFF;
        f[8] = f[9] = 0xFF;
        f[127] = frame_xor(f);
    }
    memcpy(c + 63 * FRAME, c, FRAME);
}

static void save_card(int s) {
    if (!present[s]) return;
    FILE *f = fopen(path[s], "wb");
    if (!f) { rt_log("card: can't write %s", path[s]); return; }
    fwrite(card[s], 1, CARD_SIZE, f);
    fclose(f);
}

void card_set_dir(const char *dir) { card_dir = dir; }

void card_init(void) {
    if (loaded) return;
    loaded = 1;
    for (int s = 0; s < 2; s++) {
        snprintf(path[s], sizeof path[s], "%smemcard%d.mcd", card_dir, s + 1);
        FILE *f = fopen(path[s], "rb");
        if (f) {
            present[s] = fread(card[s], 1, CARD_SIZE, f) == CARD_SIZE;
            fclose(f);
            if (!present[s]) rt_log("card: %s is not a 128 KB card image, ignored", path[s]);
        } else if (s == 0) {
            format_card(card[s]);
            present[s] = 1;
            save_card(s);
            rt_log("card: created %s", path[s]);
        }
        if (present[s]) rt_log("card: slot %d = %s", s + 1, path[s]);
    }
}

/* ------------------------------------------------------------ directory */

static int slot_of_port(uint32_t port) { return (port >> 4) & 1; }

static uint8_t *dir_entry(int s, int b) { return card[s] + (1 + b) * FRAME; }

static void dir_fix(int s, int b) {
    uint8_t *e = dir_entry(s, b);
    e[127] = frame_xor(e);
}

static int dir_state(int s, int b) { return dir_entry(s, b)[0]; }

static uint16_t dir_next(int s, int b) { const uint8_t *e = dir_entry(s, b); return (uint16_t)(e[8] | e[9] << 8); }

/* Parse "bu00:NAME" / "bu10:NAME". */
static int parse_name(const char *full, int *slot, const char **name) {
    if (strncmp(full, "bu", 2) || full[4] != ':') return 0;
    *slot = full[2] == '1' ? 1 : 0;
    *name = full + 5;
    return 1;
}

static int find_file(int s, const char *name) {
    for (int b = 0; b < 15; b++) {
        const uint8_t *e = dir_entry(s, b);
        if (e[0] == 0x51 && !strncmp((const char *)e + 0x0A, name, 20)) return b;
    }
    return -1;
}

static int block_of(int s, int first, int index) {
    int b = first;
    for (int i = 0; i < index && b >= 0 && b < 15; i++) {
        uint16_t n = dir_next(s, b);
        b = n == 0xFFFF ? -1 : n;
    }
    return b;
}

/* Wildcard match for firstfile ('?' one char, '*' rest). */
static int match(const char *pat, const char *name) {
    for (;; pat++, name++) {
        if (*pat == '*') return 1;
        if (!*pat) return !*name;
        if (*pat != '?' && *pat != *name) return 0;
        if (!*name) return 0;
    }
}

/* ------------------------------------------------------------ files */

typedef struct {
    int used, slot, first, size;
    uint32_t pos;
} CardFd;

static CardFd fds[16];

/* The last files opened for reading and for writing ("bu00:NAME"), for the
 * port's saved games (decomp/savegame.c), which go with a card save. */
static char last_name[2][32];

static unsigned writes;
const char *card_last_file(int written) { return last_name[written ? 1 : 0]; }
unsigned card_write_count(void) { return writes; }

/* FNV-1a over a file's contents (to tell whether a card save is still the
 * one a saved game was made with). 0: no such file. */
int card_file_digest(const char *full, uint32_t *out) {
    int s;
    const char *name;
    if (!parse_name(full, &s, &name) || !present[s]) return 0;
    int b = find_file(s, name);
    if (b < 0) return 0;
    uint32_t size, h = 2166136261u;
    memcpy(&size, dir_entry(s, b) + 4, 4);
    for (uint32_t i = 0; i < size / BLOCK; i++) {
        int k = block_of(s, b, (int)i);
        if (k < 0) break;
        const uint8_t *d = card[s] + (k + 1) * BLOCK;
        for (int j = 0; j < BLOCK; j++) h = (h ^ d[j]) * 16777619u;
    }
    *out = h;
    return 1;
}

static int card_open(const char *full, uint32_t mode) {
    int s;
    const char *name;
    if (!parse_name(full, &s, &name) || !present[s]) return -1;
    int b = find_file(s, name);
    if (mode & 0x200) {                                  /* create */
        if (b >= 0) return -1;
        int nblocks = (int)(mode >> 16);
        if (nblocks < 1) nblocks = 1;
        int got[15], n = 0;
        for (int i = 0; i < 15 && n < nblocks; i++)
            if ((dir_state(s, i) & 0xF0) == 0xA0) got[n++] = i;
        if (n < nblocks) return -1;
        for (int i = 0; i < n; i++) {
            uint8_t *e = dir_entry(s, got[i]);
            memset(e, 0, FRAME);
            e[0] = i == 0 ? 0x51 : (i == n - 1 ? 0x53 : 0x52);
            if (n == 1) e[0] = 0x51;
            if (i == 0) {
                uint32_t sz = (uint32_t)n * BLOCK;
                memcpy(e + 4, &sz, 4);
                strncpy((char *)e + 0x0A, name, 20);
            }
            uint16_t nx = i + 1 < n ? (uint16_t)got[i + 1] : 0xFFFF;
            e[8] = (uint8_t)nx;
            e[9] = (uint8_t)(nx >> 8);
            dir_fix(s, got[i]);
            memset(card[s] + (got[i] + 1) * BLOCK, 0, BLOCK);
        }
        save_card(s);
        b = got[0];
    }
    if (b < 0) return -1;
    for (int fd = 2; fd < 16; fd++) {
        if (fds[fd].used) continue;
        uint32_t sz;
        memcpy(&sz, dir_entry(s, b) + 4, 4);
        fds[fd] = (CardFd){1, s, b, (int)sz, 0};
        snprintf(last_name[(mode & 2) ? 1 : 0], sizeof last_name[0], "%s", full);
        return fd;
    }
    return -1;
}

static int card_rw(int fd, uint32_t buf, uint32_t len, int write) {
    if (fd < 2 || fd >= 16 || !fds[fd].used) return -1;
    CardFd *f = &fds[fd];
    uint32_t done = 0;
    while (done < len && f->pos < (uint32_t)f->size) {
        int b = block_of(f->slot, f->first, (int)(f->pos / BLOCK));
        if (b < 0) break;
        uint32_t off = (uint32_t)(b + 1) * BLOCK + f->pos % BLOCK;
        uint32_t n = BLOCK - f->pos % BLOCK;
        if (n > len - done) n = len - done;
        if (write) ram_read(buf + done, card[f->slot] + off, n);
        else ram_write(buf + done, card[f->slot] + off, n);
        done += n;
        f->pos += n;
    }
    if (write) { save_card(f->slot); writes++; }
    card_event(EV_IOE);
    return (int)done;
}

/* Directory entry for firstfile/nextfile:
 * name[20], attr, size, next (unused), head block, system[4] */
static int search_slot, search_next;
static char search_pat[32];

static uint32_t list_next(uint32_t dirent) {
    for (; search_next < 15; search_next++) {
        const uint8_t *e = dir_entry(search_slot, search_next);
        if (e[0] != 0x51) continue;
        char name[21] = {0};
        memcpy(name, e + 0x0A, 20);
        if (!match(search_pat, name)) continue;
        uint8_t d[40] = {0};
        memcpy(d, name, 20);
        uint32_t attr = 0x50, size;
        memcpy(&size, e + 4, 4);
        uint32_t head = (uint32_t)search_next;
        memcpy(d + 20, &attr, 4);
        memcpy(d + 24, &size, 4);
        memcpy(d + 32, &head, 4);
        ram_write(dirent, d, 40);
        search_next++;
        return dirent;
    }
    return 0;
}

static int erase_file(const char *full) {
    int s;
    const char *name;
    if (!parse_name(full, &s, &name) || !present[s]) return 0;
    int b = find_file(s, name);
    if (b < 0) return 0;
    for (int guard = 0; b >= 0 && b < 15 && guard < 15; guard++) {
        uint8_t *e = dir_entry(s, b);
        uint16_t n = dir_next(s, b);
        e[0] = 0xA0 | (e[0] & 0x0F);
        dir_fix(s, b);
        b = n == 0xFFFF ? -1 : n;
    }
    save_card(s);
    return 1;
}

/* ------------------------------------------------------------ BIOS calls */

int card_bios_call(uint32_t table, uint32_t fn) {
    uint32_t a = R[4], b = R[5], c = R[6];
    static int trace = -1;                               /* RT_CARDLOG: every card call */
    if (trace < 0) trace = getenv("RT_CARDLOG") != NULL;
    if (trace) rt_log("card: bios %X:%X (%X %X %X)", table, fn, a, b, c);
    if (table == 0xA0) {
        switch (fn) {
        case 0xAB:                                       /* _card_info(port) */
        case 0xAC: {                                     /* _card_load(port) */
            int s = slot_of_port(a);
            card_event(present[s] ? EV_IOE : EV_TIMEOUT);
            R[2] = 1;
            return 1;
        }
        case 0xAD: R[2] = 0; return 1;                   /* set_card_auto_format */
        }
        return 0;
    }
    switch (fn) {
    case 0x32: {                                         /* open */
        const char *name = ram_str(a);
        if (strncmp(name, "bu", 2)) return 0;
        R[2] = (uint32_t)card_open(name, b);
        rt_log("card: open(%s, %X) = %d", name, b, (int)R[2]);
        return 1;
    }
    case 0x33:                                           /* lseek(fd, off, whence) */
        if ((int)a >= 2 && a < 16 && fds[a].used) {
            if (c == 0) fds[a].pos = b;
            else if (c == 1) fds[a].pos += b;
            R[2] = fds[a].pos;
            return 1;
        }
        return 0;
    case 0x34: case 0x35:                                /* read, write */
        if ((int)a >= 2 && a < 16 && fds[a].used) {
            R[2] = (uint32_t)card_rw((int)a, b, c, fn == 0x35);
            return 1;
        }
        return 0;
    case 0x36:                                           /* close */
        if ((int)a >= 2 && a < 16 && fds[a].used) { fds[a].used = 0; R[2] = a; return 1; }
        return 0;
    case 0x41: {                                         /* format */
        int s;
        const char *n;
        if (!parse_name(ram_str(a), &s, &n)) return 0;
        if (present[s]) { format_card(card[s]); save_card(s); }
        R[2] = (uint32_t)present[s];
        return 1;
    }
    case 0x42: {                                         /* firstfile(pattern, dirent) */
        const char *n;
        if (!parse_name(ram_str(a), &search_slot, &n)) return 0;
        snprintf(search_pat, sizeof search_pat, "%s", n);
        search_next = 0;
        R[2] = present[search_slot] ? list_next(b) : 0;
        rt_log("card: firstfile(%s) = %X", ram_str(a), R[2]);
        return 1;
    }
    case 0x43: R[2] = list_next(a); return 1;           /* nextfile(dirent) */
    case 0x45: R[2] = (uint32_t)erase_file(ram_str(a)); return 1;
    case 0x4A: case 0x4B: case 0x4C: R[2] = 1; return 1; /* InitCard, StartCard, StopCard */
    case 0x4E: case 0x4F: {                              /* _card_write/_card_read(port, sector, buf) */
        int s = slot_of_port(a);
        if (!present[s] || b >= CARD_SIZE / FRAME) { card_event(EV_TIMEOUT); R[2] = 0; return 1; }
        if (fn == 0x4E) { ram_read(c, card[s] + b * FRAME, FRAME); save_card(s); }
        else ram_write(c, card[s] + b * FRAME, FRAME);
        card_event(EV_IOE);
        R[2] = 1;
        return 1;
    }
    case 0x50: R[2] = 1; return 1;                       /* _new_card */
    case 0x51: R[2] = 0xFFFFFFFF; return 1;              /* Krom2RawAdd: no BIOS font */
    case 0x5C: R[2] = 1; return 1;                       /* _card_status */
    case 0x5D: R[2] = 1; return 1;                       /* _card_wait */
    }
    return 0;
}

/* The cards' contents stay as they are on disk (a save state doesn't take
 * memory card saves back); open files and searches do belong to it. */
void card_state(StateIO *io) {
    ss_module(io, "card");
    SS(io, fds);
    SS(io, search_slot);
    SS(io, search_next);
    SS(io, search_pat);
}
