/* modfs.c, part: Texts: the rooms' messages, items, UI, fonts. Included by modfs.c, in this order. */

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
