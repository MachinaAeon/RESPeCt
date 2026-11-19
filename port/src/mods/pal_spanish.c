/* Build a Spanish-text mod from the player's own Spanish PAL disc (SLES-02732).
 *
 * The Spanish release has the USA data layout, so its changed data files
 * (room messages, item descriptions, the font, translated pictures) go
 * into the mod whole, by disc name. The item names are in TEST.BIN, game
 * code that can't be swapped, so they're read out of it into
 * text/GAME.txt in the USA entries' order. The Spanish font keeps its
 * letters where the USA font has German and French ones; text/CHARS.txt
 * maps them.
 *
 * Nothing from the game is here: everything is copied from the disc the
 * player picks, into their own mods folder. (Same as tools/pal_spanish.py.) */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define MKDIR(p) _mkdir(p)
#else
#define MKDIR(p) mkdir(p, 0777)
#endif

static const char *const data_files[] = {
    "AEX.DAT", "B/BIGTIM.BIN", "B/MAPTIM.BIN", "B/STGBG2.BIN", "B/STGBG10.BIN",
    "CONFFILE.BIN", "EV_OBJ.DAT", "ITEMDAT.DAT", "OBJ.DAT", "OPTTIM.BIN",
    "ROOMENV.DAT", "SCR_TIM.BIN", "TITTIM.BIN", "TYPE_TIM.BIN",
};

/* The font's 128 cells as the Spanish font draws them (UTF-8). */
static const char *const es_glyph[128] = {
    " ", "{_}", "▸", "{L2}", "{R2}", "{L1}", "{R1}", "△", "○", "×", "□", "▾",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9", ":", ";", ",", "\"", "!", "?", "⁉",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M", "N", "O", "P", "Q", "R",
    "S", "T", "U", "V", "W", "X", "Y", "Z", "(", "/", ")", "'", "-", "·",
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j", "k", "l", "m", "n", "o", "p", "q", "r",
    "s", "t", "u", "v", "w", "x", "y", "z",
    "Á", "á", "Í", "í", "Ó", "ó", "Ú", "ú", "¿", "¡", "Ñ", "ñ", "è", "É", "é", "º", "ª",
    "«", "»", "Ü", "ü", "Ô", "ô", "Ù", "ù", "Û", "û", "Ç", "ç", "{S.}", "{T.}", "{A.}", "{R.}",
    "“", ".", "…", "{dash1}", "{dash2}", "+", "=", "&",
};
static const struct { const char *ch; int g; } es_chars[] = {
    {"Á", 87}, {"á", 88}, {"Í", 89}, {"í", 90}, {"Ó", 91}, {"ó", 92}, {"Ú", 93}, {"ú", 94}, {"¿", 95},
    {"¡", 96}, {"Ñ", 97}, {"ñ", 98}, {"º", 102}, {"ª", 103}, {"«", 104}, {"»", 105}, {"Ü", 106}, {"ü", 107},
};

#define PAL_TEST_BASE 0x8006C108u   /* where the Spanish TEST.BIN loads (16 bytes below the USA one) */
#define PAL_NAME_PTRS 0x3AE04u      /* its item names' pointers */
/* USA GAME.txt entry -> the pointer slot that uses it (the same slots on both discs). */
static const uint8_t entry_slot[][2] = {
    {1, 0}, {2, 1}, {3, 2}, {4, 3}, {5, 4}, {6, 5}, {7, 6}, {8, 7}, {9, 8}, {10, 10}, {11, 11}, {12, 12},
    {13, 13}, {14, 14}, {15, 15}, {16, 16}, {17, 20}, {18, 21}, {19, 22}, {20, 23}, {21, 24}, {22, 30},
    {23, 31}, {24, 32}, {25, 33}, {26, 34}, {27, 35}, {28, 36}, {29, 37}, {30, 38}, {31, 39}, {33, 41},
    {34, 43}, {35, 44}, {36, 45}, {37, 46}, {38, 47}, {39, 48}, {40, 49}, {41, 52}, {42, 53}, {43, 54},
    {44, 55}, {45, 64}, {46, 65}, {47, 66}, {48, 67}, {49, 68}, {50, 69}, {51, 70}, {52, 71}, {53, 72},
    {54, 73}, {55, 74}, {56, 75}, {57, 76}, {58, 108}, {59, 111},
};

/* The menus' strings the Spanish disc changed, which live in its code
 * files: (file, USA address, Spanish disc's address, kind). Kinds: 'w' a
 * 16-bit text, copied; 'a' the same with its accents dropped (a screen
 * whose font is the USA one); 'n' an 8-bit small-font text, its codes turned into
 * the USA small font's (which has no accents); 'b' a line the Spanish disc
 * merged into the one before it, left blank. Found by lining up the two
 * discs' code; written to text/SCREENS.txt. */
static const struct { const char *file; uint32_t usa, es; char kind; } screens[] = {
    {"OPTION.BIN", 0x800CC480u, 0x800CC610u, 'n'}, {"OPTION.BIN", 0x800CC48Cu, 0x800CC61Cu, 'n'},
    {"OPTION.BIN", 0x800CC494u, 0x800CC71Cu, 'n'}, {"OPTION.BIN", 0x800CC984u, 0x800CCA9Cu, 'n'},
    {"OPTION.BIN", 0x800CCB24u, 0x800CCC3Cu, 'n'}, {"OPTION.BIN", 0x800CCB2Bu, 0x800CCC43u, 'n'},
    {"OPTION.BIN", 0x800CCB32u, 0x800CCC4Au, 'n'}, {"OPTION.BIN", 0x800CC8C0u, 0x800CC9B0u, 'w'},
    {"OPTION.BIN", 0x800CC8CCu, 0x800CC9D4u, 'w'}, {"OPTION.BIN", 0x800CC8D4u, 0x800CC9DCu, 'b'},
    {"OPTION.BIN", 0x800CC8E4u, 0x800CC9E8u, 'w'}, {"OPTION.BIN", 0x800CC8FCu, 0x800CCA1Cu, 'w'},
    {"OPTION.BIN", 0x800CC90Cu, 0x800CC9C0u, 'w'}, {"OPTION.BIN", 0x800CC9C4u, 0x800CCAC4u, 'w'},
    {"OPTION.BIN", 0x800CC9D0u, 0x800CCADCu, 'w'}, {"OPTION.BIN", 0x800CC9E8u, 0x800CCAF4u, 'w'},
    {"OPTION.BIN", 0x800CCAE0u, 0x800CC998u, 'w'}, {"OPTION.BIN", 0x800CCB3Cu, 0x800CCC54u, 'w'},
    {"TEST.BIN", 0x800A2ECCu, 0x800A3078u, 'w'}, {"TEST.BIN", 0x800A2E84u, 0x800A3038u, 'w'},   /* the doors' messages */
    {"TEST.BIN", 0x800A2EF4u, 0x800A30D8u, 'w'}, {"TEST.BIN", 0x800A2F14u, 0x800A30A4u, 'w'},
    {"SCORE.BIN", 0x8006D92Cu, 0x8006D90Cu, 'a'}, {"SCORE.BIN", 0x8006D93Cu, 0x8006D924u, 'a'},   /* the results' labels */
    {"SCORE.BIN", 0x8006D948u, 0x8006D930u, 'a'}, {"SCORE.BIN", 0x8006D960u, 0x8006D940u, 'a'},
    {"SCORE.BIN", 0x8006D978u, 0x8006D954u, 'a'}, {"SCORE.BIN", 0x8006D998u, 0x8006D964u, 'a'},
    {"SCORE.BIN", 0x8006D9ACu, 0x8006D984u, 'a'},
    {"TITLE.BIN", 0x8007FCB4u, 0x8007FDC0u, 'w'}, {"TITLE.BIN", 0x8007FCC0u, 0x8007FDCCu, 'b'},   /* the title screen's options */
    {"TITLE.BIN", 0x8007FCD0u, 0x8007FDD0u, 'w'}, {"TITLE.BIN", 0x8007FCE0u, 0x8007FDE0u, 'w'},
    {"TITLE.BIN", 0x8007FCE8u, 0x8007FDE8u, 'b'}, {"TITLE.BIN", 0x8007FCF8u, 0x8007FDF4u, 'w'},
    {"TITLE.BIN", 0x8007FD10u, 0x8007FE28u, 'w'}, {"TITLE.BIN", 0x8007FEB0u, 0x8007F828u, 'w'},
    {"TITLE.BIN", 0x8007FF0Cu, 0x80080020u, 'w'}, {"TITLE.BIN", 0x8007FD8Cu, 0x8007FEA0u, 'n'},
    {"TITLE.BIN", 0x8007FEF4u, 0x80080008u, 'n'}, {"TITLE.BIN", 0x8007FEFBu, 0x8008000Fu, 'n'},
    {"TITLE.BIN", 0x8007FF02u, 0x80080016u, 'n'},
};
#define PAL_OPTION_BASE 0x800C4008u   /* where the Spanish OPTION.BIN loads */

/* A Spanish font cell with its accent dropped, as a font cell. */
static uint16_t plain_glyph(uint16_t g) {
    static const char accents[] = "AaIiOoUu?!Nn" "eEe" "oa\"\"Uu" "OoUuUuCc";   /* cells 87..114 */
    if (g < 87 || g > 114) return g;
    char c = accents[g - 87];
    return c >= 'A' && c <= 'Z' ? (uint16_t)(29 + c - 'A') : c >= 'a' && c <= 'z' ? (uint16_t)(61 + c - 'a') :
           c == '?' ? 27 : c == '!' ? 26 : 25;
}

/* A Spanish font cell as the USA small font's code (ASCII - 0x20), accents dropped. */
static uint8_t narrow_code(uint8_t g) {
    static const char accents[] = "AaIiOoUu?!Nn" "eEe" "oa\"\"Uu" "OoUuUuCc";   /* cells 87..114 */
    char c = ' ';
    if (g >= 87 && g <= 114) c = accents[g - 87];
    else if (g == 120) c = '"';
    else if (g == 60 || g == 121 || g == 122) c = '.';
    else if (g < 128 && es_glyph[g][0] && !es_glyph[g][1] && es_glyph[g][0] != '{') c = es_glyph[g][0];
    return (uint8_t)(c - 0x20);
}

/* Words of RESPeCt's own over menu words that are pictures (the Spanish
 * disc left those English): text/CAPTIONS.txt (captions.c). The picture's
 * colour depth, its texels from VRAM's corner, the hash of that VRAM. */
static const struct { int depth, x0, y0, x1, y1; uint32_t hash; const char *text; } captions[] = {
    {1, 1426, 270, 1580, 297, 0xE9FA3DB5u, "CONTROLES"},
    {1, 1598, 267, 1693, 282, 0xCCC4E8D7u, "TIPO DE MANDO"},
    {1, 1454, 330, 1499, 345, 0x162212C3u, "SIN USO"},
    {1, 1641, 330, 1686, 345, 0x1DB97D8Bu, "SIN USO"},
    {1, 1433, 352, 1518, 367, 0x962C3148u, "BÚSQUEDA RÁPIDA"},
    {1, 1639, 352, 1689, 367, 0x4B56846Bu, "APUNTAR"},
    {1, 1454, 372, 1539, 387, 0x345005BEu, "AVANZAR/CORRER"},
    {1, 1429, 392, 1493, 407, 0xB5D13750u, "GIRAR IZQ."},
    {1, 1500, 392, 1570, 407, 0x6479D9EEu, "GIRAR DCHA."},
    {1, 1662, 392, 1707, 407, 0x162212C3u, "SIN USO"},
    {1, 1482, 413, 1516, 428, 0x70967DF7u, "ATRÁS"},
    {0, 2664, 256, 2728, 272, 0xB4F624C0u, "ACEPTAR/ATACAR"},
    {0, 2744, 256, 2808, 272, 0x31A53053u, "CANCELAR"},
    {1, 1487, 271, 1650, 288, 0x22C60DF3u, "AJUSTAR PANTALLA"},
    {1, 1440, 488, 1632, 504, 0x9E788A01u, "PULSA START"},
};

typedef struct { char name[64]; uint32_t lba, size; } DFile;
typedef struct { FILE *f; DFile *files; int n, cap; } Disc;

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

static int sectors(Disc *d, uint32_t lba, uint32_t n, uint8_t *out) {
    uint8_t raw[2352];
    if (fseek(d->f, (long)lba * 2352, SEEK_SET)) return 0;
    for (uint32_t i = 0; i < n; i++) {
        if (fread(raw, 1, 2352, d->f) != 2352) return 0;
        memcpy(out + i * 2048, raw + (raw[15] == 2 ? 24 : 16), 2048);
    }
    return 1;
}

static int walk(Disc *d, uint32_t lba, uint32_t size, const char *path, int depth) {
    uint32_t ns = (size + 2047) / 2048;
    if (depth > 4 || ns > 64) return 0;
    uint8_t *buf = malloc(ns * 2048);
    if (!buf || !sectors(d, lba, ns, buf)) { free(buf); return 0; }
    for (uint32_t i = 0; i < ns * 2048;) {
        uint8_t l = buf[i];
        if (!l) { i = (i / 2048 + 1) * 2048; continue; }
        if (i + l > ns * 2048 || l < 34) break;
        const uint8_t *r = buf + i;
        i += l;
        int nl = r[32];
        if (nl == 1 && r[33] <= 1) continue;
        char name[64];
        int k = 0;
        for (int j = 0; j < nl && r[33 + j] != ';' && k < 30; j++) name[k++] = (char)r[33 + j];
        name[k] = 0;
        char full[64];
        snprintf(full, sizeof full, "%s%s", path, name);
        if (r[25] & 2) {
            char sub[72];
            snprintf(sub, sizeof sub, "%s/", full);
            walk(d, rd32(r + 2), rd32(r + 10), sub, depth + 1);
        } else {
            if (d->n == d->cap) {
                d->cap = d->cap ? d->cap * 2 : 128;
                DFile *nf = realloc(d->files, (size_t)d->cap * sizeof *nf);
                if (!nf) break;
                d->files = nf;
            }
            DFile *f = &d->files[d->n++];
            snprintf(f->name, sizeof f->name, "%s", full);
            f->lba = rd32(r + 2);
            f->size = rd32(r + 10);
        }
    }
    free(buf);
    return 1;
}

static uint8_t *disc_read(Disc *d, const char *name, uint32_t *size) {
    for (int i = 0; i < d->n; i++)
        if (!strcmp(d->files[i].name, name)) {
            uint32_t ns = (d->files[i].size + 2047) / 2048;
            uint8_t *b = malloc((size_t)ns * 2048 + 4);
            if (!b || !sectors(d, d->files[i].lba, ns, b)) { free(b); return NULL; }
            memset(b + d->files[i].size, 0xFF, 4);   /* (an end code past the file, for safety) */
            *size = d->files[i].size;
            return b;
        }
    return NULL;
}

/* A .cue: the .bin it names (same folder). */
static void cue_bin(const char *cue, char *out, size_t n) {
    snprintf(out, n, "%s", cue);
    size_t l = strlen(cue);
    if (l < 4 || (strcmp(cue + l - 4, ".cue") && strcmp(cue + l - 4, ".CUE"))) return;
    FILE *f = fopen(cue, "r");
    char line[512];
    while (f && fgets(line, sizeof line, f)) {
        char *a = strchr(line, '"'), *b = a ? strchr(a + 1, '"') : NULL;
        if (strstr(line, "FILE") && a && b) {
            *b = 0;
            const char *s1 = strrchr(cue, '/'), *s2 = strrchr(cue, '\\');
            const char *sl = s1 > s2 ? s1 : s2;
            int dl = sl ? (int)(sl - cue + 1) : 0;
            snprintf(out, n, "%.*s%s", dl, cue, a + 1);
            break;
        }
    }
    if (f) fclose(f);
}

static void mkdirs(const char *path) {          /* every folder in path (not the last part) */
    char p[1100];
    snprintf(p, sizeof p, "%s", path);
    for (char *s = p + 1; *s; s++)
        if (*s == '/' || *s == '\\') { char c = *s; *s = 0; MKDIR(p); *s = c; }
}

static int write_file(const char *path, const void *data, size_t n) {
    mkdirs(path);
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, n, f) == n;
    return fclose(f) == 0 && ok;
}

static void put_name(FILE *f, const uint8_t *t, uint32_t tsize, uint32_t o) {
    char line[512];
    size_t n = 0;
    line[0] = 0;
    for (; o + 1 < tsize && n < sizeof line - 32; o += 2) {
        uint16_t c = rd16(t + o), next = o + 3 < tsize ? rd16(t + o + 2) : 0xFFFF;
        if (c == 0xFFFF) break;
        if (c == 0xFFFE) n += (size_t)snprintf(line + n, sizeof line - n, "%s", next == 0xFFFF ? "{n}" : "\n");
        else if (c == 0 && (next == 0xFFFE || next == 0xFFFF)) n += (size_t)snprintf(line + n, sizeof line - n, "{sp}");
        else if (c < 128) n += (size_t)snprintf(line + n, sizeof line - n, "%s", es_glyph[c]);
        else if (c >> 8 == 0xFC) n += (size_t)snprintf(line + n, sizeof line - n, "{color %u}", c & 0xFF);
        else if (c == 0xFF00) n += (size_t)snprintf(line + n, sizeof line - n, "{more}");
        else n += (size_t)snprintf(line + n, sizeof line - n, "{x%04X}", c);
    }
    char *s = line, *e = line + strlen(line);   /* the padding spaces off both ends */
    while (*s == ' ') s++;
    while (e > s && e[-1] == ' ') e--;
    fprintf(f, "%.*s\n", (int)(e - s), s);
}

/* Writes the mod into out_dir (made if needed); 1, or 0 and why. */
int pal_spanish_build(const char *disc_path, const char *out_dir, char *why, size_t wn) {
    char bin[1100];
    cue_bin(disc_path, bin, sizeof bin);
    Disc d = {0};
    d.f = fopen(bin, "rb");
    if (!d.f) { snprintf(why, wn, "couldn't open %s", bin); return 0; }
    uint8_t pvd[2048];
    int ok = 0;
    uint8_t *test = NULL;
    if (!sectors(&d, 16, 1, pvd) || memcmp(pvd + 1, "CD001", 5)) {
        snprintf(why, wn, "not a raw PlayStation disc image (.bin/.cue)");
        goto done;
    }
    walk(&d, rd32(pvd + 156 + 2), rd32(pvd + 156 + 10), "", 0);
    int sles = 0;
    for (int i = 0; i < d.n; i++) sles |= !strcmp(d.files[i].name, "SLES_027.32");
    if (!sles) { snprintf(why, wn, "that isn't the Spanish disc (SLES-02732)"); goto done; }
    uint32_t tsize;
    test = disc_read(&d, "TEST.BIN", &tsize);
    if (!test || tsize < PAL_NAME_PTRS + 112 * 4) { snprintf(why, wn, "couldn't read TEST.BIN"); goto done; }
    {   /* the translated disc's, not the English PAL one's */
        uint64_t h = 0xCBF29CE484222325ull;
        for (uint32_t k = 0; k < tsize; k++) h = (h ^ test[k]) * 0x100000001B3ull;
        if (h != 0x4DAF01E260EF3EA0ull) {
            snprintf(why, wn, "this PAL disc doesn't have the Tio Victor Spanish translation (v1.0.1) applied");
            goto done;
        }
    }
    char path[1100];
    for (size_t i = 0; i < sizeof data_files / sizeof *data_files; i++) {
        uint32_t n;
        uint8_t *b = disc_read(&d, data_files[i], &n);
        snprintf(path, sizeof path, "%s/%s", out_dir, data_files[i]);
        int w = b && write_file(path, b, n);
        free(b);
        if (!w) { snprintf(why, wn, "couldn't copy %s", data_files[i]); goto done; }
    }
    snprintf(path, sizeof path, "%s/text/GAME.txt", out_dir);
    mkdirs(path);
    FILE *f = fopen(path, "wb");
    if (!f) { snprintf(why, wn, "couldn't write %s", path); goto done; }
    fprintf(f, "# Item names, from the Spanish disc's TEST.BIN, in the USA order.\n");
    for (size_t i = 0; i < sizeof entry_slot / sizeof *entry_slot; i++) {
        uint32_t o = rd32(test + PAL_NAME_PTRS + 4 * entry_slot[i][1]) - PAL_TEST_BASE;
        fprintf(f, "@%d\n", entry_slot[i][0]);
        if (o < tsize) put_name(f, test, tsize, o); else fprintf(f, "\n");
    }
    fclose(f);
    {   /* the menus' strings */
        uint32_t osize, ssize, tlsize;
        uint8_t *opt = disc_read(&d, "OPTION.BIN", &osize), *score = disc_read(&d, "SCORE.BIN", &ssize),
                *title = disc_read(&d, "TITLE.BIN", &tlsize);
        snprintf(path, sizeof path, "%s/text/SCREENS.txt", out_dir);
        f = opt ? fopen(path, "wb") : NULL;
        if (!f || !score || !title) { if (f) fclose(f); free(opt); free(score); free(title); snprintf(why, wn, "couldn't write %s", path); goto done; }
        fprintf(f, "# The menus' strings from the Spanish disc: @address (USA), then the bytes the game reads.\n");
        for (size_t i = 0; i < sizeof screens / sizeof *screens; i++) {
            const char *fn = screens[i].file;
            int is_opt = !strcmp(fn, "OPTION.BIN"), is_score = !strcmp(fn, "SCORE.BIN"), is_title = !strcmp(fn, "TITLE.BIN");
            const uint8_t *b = is_opt ? opt : is_score ? score : is_title ? title : test;
            uint32_t n = is_opt ? osize : is_score ? ssize : is_title ? tlsize : tsize;
            uint32_t o = screens[i].es - (is_opt ? PAL_OPTION_BASE : PAL_TEST_BASE), e = o;   /* (SCORE and TITLE load where TEST does) */
            if (o >= n) continue;
            fprintf(f, "@%08X\n", screens[i].usa);
            if (screens[i].kind == 'b') { fprintf(f, "0000ffff\n"); continue; }
            if (screens[i].kind == 'n') {
                while (e < n && b[e] != 0xFF) e++;
                for (uint32_t k = o; k < e; k++) fprintf(f, "%02x", b[k] >= 0xF0 ? b[k] : narrow_code(b[k]));
                fprintf(f, "ff\n");
            } else {
                while (e + 1 < n && rd16(b + e) != 0xFFFF) e += 2;
                for (uint32_t k = o; k + 1 < e + 2 && k + 1 < n; k += 2) {
                    uint16_t c = rd16(b + k);
                    if (screens[i].kind == 'a') c = plain_glyph(c);
                    fprintf(f, "%02x%02x", c & 0xFF, c >> 8);
                }
                fprintf(f, "\n");
            }
        }
        fclose(f);
        free(opt);
        free(score);
        free(title);
    }
    snprintf(path, sizeof path, "%s/text/CAPTIONS.txt", out_dir);
    f = fopen(path, "wb");
    if (!f) { snprintf(why, wn, "couldn't write %s", path); goto done; }
    fprintf(f, "# RESPeCt's own Spanish words over menu words that are pictures: @depth x0 y0 x1 y1 hash, then the words.\n");
    for (size_t i = 0; i < sizeof captions / sizeof *captions; i++)
        fprintf(f, "@%d %d %d %d %d %08x\n%s\n", captions[i].depth, captions[i].x0, captions[i].y0, captions[i].x1, captions[i].y1,
                (unsigned)captions[i].hash, captions[i].text);
    fclose(f);
    snprintf(path, sizeof path, "%s/text/CHARS.txt", out_dir);
    f = fopen(path, "wb");
    if (!f) { snprintf(why, wn, "couldn't write %s", path); goto done; }
    fprintf(f, "# The Spanish font's letters (they sit where the USA font has German and French ones).\n");
    for (size_t i = 0; i < sizeof es_chars / sizeof *es_chars; i++) fprintf(f, "%s %d\n", es_chars[i].ch, es_chars[i].g);
    fclose(f);
    ok = 1;
done:
    free(test);
    free(d.files);
    fclose(d.f);
    return ok;
}
