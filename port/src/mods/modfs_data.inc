/* modfs.c, part: Game data: data/COMBAT.txt, data/ROOMS.txt, data/SCRIPTS.txt. Included by modfs.c, in this order. */

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

typedef struct { int kind; uint8_t *d; long n; char what[600]; } DataFile;   /* kind: 0 combat, 1 rooms, 2 scripts */
static DataFile *datafiles;
static int ndatafiles;

static int data_apply(const char *mod, const char *rel, const char *up, const char *path) {
    int kind = !strcmp(up, "DATA/COMBAT.TXT") ? 0 : !strcmp(up, "DATA/ROOMS.TXT") ? 1 : !strcmp(up, "DATA/SCRIPTS.TXT") ? 2 : -1;
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

/* The rooms' event scripts (data/SCRIPTS.txt): a ROOMENV.DAT block's part 5
 * (+0x18) is the script table, a u32 count then a word per script whose
 * low half is its offset from the table, and the scripts: s16 codes, an op
 * and its operands (the lengths at 0x800A2FC8, the handlers at 0x800A2B8C;
 * script_runner.c, script_ops.c). Names of the ops known from the game's
 * code, the others OpNN (hex). */
#define SCR_OPLEN 0x800A2FC8u
#define SCR_NOPS  105
static const char *const scr_names[SCR_NOPS] = {
    [0x00] = "Nop0", [0x01] = "WhiteIn", [0x02] = "CutsceneStart", [0x03] = "ThreadEnd", [0x05] = "Message", [0x06] = "Sound",
    [0x09] = "Fade", [0x0B] = "Wait", [0x0C] = "PickUp", [0x0E] = "FlagOp", [0x13] = "Door", [0x14] = "DoorState",
    [0x17] = "IfFlag", [0x18] = "Else", [0x19] = "EndIf", [0x20] = "WhiteOut", [0x22] = "Music", [0x2A] = "SceneSeq",
    [0x2B] = "ActorHold", [0x2C] = "ActorRelease", [0x2D] = "Nop", [0x2E] = "ActorAnim", [0x30] = "StartThread",
    [0x31] = "ActorSetPos", [0x32] = "ActorSetRot", [0x33] = "ActorShow", [0x34] = "ActorMove", [0x35] = "ActorTurnTo",
    [0x36] = "ActorWalk", [0x37] = "CameraDetach", [0x38] = "CameraRestore", [0x3B] = "CameraPlace", [0x3C] = "CameraMoveAim",
    [0x3D] = "CameraPan", [0x3E] = "CameraMoveTurn", [0x42] = "Caption", [0x43] = "LoadImage", [0x46] = "SceneStart",
    [0x47] = "SceneEnd", [0x48] = "LoadFontPage", [0x4A] = "DimHold", [0x4B] = "DimStart", [0x4C] = "ScreenFade",
    [0x53] = "ActorRemove", [0x57] = "SoundNext", [0x58] = "ActorMoveTurn", [0x59] = "SkipOn", [0x5A] = "SkipOff",
    [0x5B] = "Sparks", [0x5C] = "StreamPlay", [0x5E] = "SoundFade", [0x5F] = "CameraWalk", [0x63] = "MusicStart",
    [0x64] = "MusicLoad", [0x65] = "MusicStop",
};
#define SCR_END 0x03                             /* ThreadEnd */
static int scr_oplen(const uint8_t *test, int op) { return op >= 0 && op < SCR_NOPS ? test[SCR_OPLEN - TEST_BASE + (uint32_t)op] : 0; }
static void scr_name(int op, char *out, size_t n) {
    if (op >= 0 && op < SCR_NOPS && scr_names[op]) snprintf(out, n, "%s", scr_names[op]);
    else snprintf(out, n, "Op%02X", (unsigned)op & 0xFF);
}
static int scr_op(const char *w) {               /* an op by its name, OpNN or a number; -1 if none */
    for (int k = 0; k < SCR_NOPS; k++) if (scr_names[k] && !strcasecmp(w, scr_names[k])) return k;
    unsigned v;
    char rest[4];
    if ((sscanf(w, "Op%x%3s", &v, rest) == 1 || sscanf(w, "op%x%3s", &v, rest) == 1) && v < SCR_NOPS) return (int)v;
    return -1;
}
/* A block's script table: where in the block, how many scripts; 0 if it has none. */
static int scr_part(const uint8_t *d, uint32_t at, uint32_t len, uint32_t *base, uint32_t *cnt) {
    if (len < 28 || rd32(d + at) != 6) return 0;
    uint32_t b = rd32(d + at + 0x18);
    if (b < 28 || b + 4 > len) return 0;
    uint32_t c = rd32(d + at + b);
    if (c > 255 || b + 4 + 4 * c > len) return 0;
    for (uint32_t k = 0; k < c; k++) if ((int16_t)rd16(d + at + b + 4 + 4 * k) < (int32_t)(4 + 4 * c)) return 0;
    *base = b;
    *cnt = c;
    return 1;
}
/* Script n's place (from the table): to the next script's start, the last
 * one as far as its ops decode (other data follows it). */
static void scr_span(const uint8_t *test, const uint8_t *d, uint32_t at, uint32_t len, uint32_t base, uint32_t cnt, uint32_t n,
                     uint32_t *s, uint32_t *e) {
    *s = (uint32_t)(int16_t)rd16(d + at + base + 4 + 4 * n) & ~1u;
    uint32_t end = len - base;
    int last = 1;
    for (uint32_t k = 0; k < cnt; k++) {
        uint32_t o = (uint32_t)(int16_t)rd16(d + at + base + 4 + 4 * k) & ~1u;
        if (o > *s && o < end) { end = o; last = 0; }
    }
    if (last) {
        uint32_t p = *s;
        for (;;) {
            int l = p + 2 <= end ? scr_oplen(test, (int16_t)rd16(d + at + base + p)) : 0;
            if (!l || p + 2 * (uint32_t)l > end) break;
            p += 2 * (uint32_t)l;
        }
        end = p;
    }
    *e = end < *s ? *s : end;
}

/* data/SCRIPTS.txt: "@<block>", "script <n>", then its ops, a line each
 * (the name and the operands; "raw" and words as they are). A script given
 * goes in its own place when it fits (shorter: only if it ends with
 * ThreadEnd, the rest never runs), else after the block's data (it must end
 * with ThreadEnd), the table pointing there; the other scripts and data stay
 * where they are. */
static void scripts_apply(DataFile *f, Touched *test, char *s) {
    int fi = disc_index("ROOMENV.DAT");
    Touched *t = touch(fi);
    static uint32_t v[1024];
    uint32_t nb = roomenv_blocks(test, t, v);
    if (!t || !nb) { note("mods: %s: ROOMENV.DAT's table of blocks wasn't found: not used", f->what); return; }
    uint16_t *w = malloc(2 * 32768);
    uint8_t *app = malloc(65536);
    int cur = -1, sn = -1, nw = 0, bad = 0, last = -1, ln = 0, changed = 0, moved = 0, nmv = 0;
    uint32_t at = 0, len = 0, base = 0, cnt = 0, tail = 0, napp = 0, mv[256][2];   /* moved: script, its new offset */
    char *save = NULL;
    for (char *line = strtok_r(s, "\n", &save);; line = strtok_r(NULL, "\n", &save)) {
        char *h = line ? strchr(line, '#') : NULL;
        if (h) *h = 0;
        if (line) ln++;
        unsigned b, n;
        char word[32];
        int head = line && sscanf(line, " @%u", &b) == 1, scr = line && sscanf(line, " script %u", &n) == 1;
        if ((head || scr || !line) && cur >= 0 && sn >= 0 && !bad) {   /* the script before, in */
            uint32_t s0, e0, bytes = 2 * (uint32_t)nw;
            scr_span(test->data, t->data, at, len, base, cnt, (uint32_t)sn, &s0, &e0);
            uint8_t *p = t->data + at + base + s0;
            int same = bytes == e0 - s0;
            for (int k = 0; same && k < nw; k++) same = rd16(p + 2 * k) == w[k];
            if (same) {
            } else if (bytes <= e0 - s0 && (bytes == e0 - s0 || last == SCR_END)) {
                memset(p, 0, e0 - s0);           /* (op 0s after its end, never run) */
                for (int k = 0; k < nw; k++) wr16(p + 2 * k, w[k]);
                changed++;
            } else if (last != SCR_END) {
                note("mods: %s: @%d script %d: longer than its place, so it goes after the room's data, and must end with ThreadEnd there: not used", f->what, cur, sn);
            } else if (tail - base + napp + bytes >= 0x8000 || napp + bytes > 65536) {
                note("mods: %s: @%d script %d: past the 32 KB the script table reaches: not used", f->what, cur, sn);
            } else {
                int k = 0;
                while (k < nmv && mv[k][0] != (uint32_t)sn) k++;
                if (k == nmv) nmv++;
                mv[k][0] = (uint32_t)sn;
                mv[k][1] = tail - base + napp;
                for (int j = 0; j < nw; j++) wr16(app + napp + 2 * (uint32_t)j, w[j]);
                napp += bytes;
            }
        }
        if ((head || !line) && cur >= 0 && napp) {   /* the block before: what goes after its data, and the table pointing there */
            if (tail + napp <= len) {
                memcpy(t->data + at + tail, app, napp);
                for (int k = 0; k < nmv; k++) wr16(t->data + at + base + 4 + 4 * mv[k][0], (uint16_t)mv[k][1]);
                changed += nmv;
                moved += nmv;
            } else if (ngrows < (int)(sizeof grows / sizeof grows[0])) {   /* (the table in the grown copy: as it was if it can't grow) */
                uint8_t *np = malloc(tail - base + napp);
                memcpy(np, t->data + at + base, tail - base);
                memcpy(np + tail - base, app, napp);
                for (int k = 0; k < nmv; k++) wr16(np + 4 + 4 * mv[k][0], (uint16_t)mv[k][1]);
                Grow *g = &grows[ngrows++];
                *g = (Grow){fi, at + base, tail - base + napp, len - base, 0, np, ""};
                snprintf(g->what, sizeof g->what, "the scripts of ROOMENV.DAT block %d", cur);
                changed += nmv;
                moved += nmv;
            } else note("mods: %s: @%d: too many files growing: its moved scripts not used", f->what, cur);
            napp = 0;
        }
        if (head || !line) nmv = 0;
        if (!line) break;
        if (head) {
            cur = sn = -1;
            if (b >= nb || !scr_part(t->data, v[b] * 2048, (v[b + 1] - v[b]) * 2048, &base, &cnt)) {
                note("mods: %s: @%u: no such room, or it has no scripts (ROOMENV.DAT block): not used", f->what, b);
                continue;
            }
            cur = (int)b;
            at = v[b] * 2048;
            len = (v[b + 1] - v[b]) * 2048;
            tail = len;
            while (tail > base && !t->data[at + tail - 1]) tail--;
            tail = ((tail + 3) & ~3u) + 4;   /* (past the last data, a word apart) */
            continue;
        }
        if (scr) {
            sn = -1;
            if (cur < 0) continue;
            if (n >= cnt) { note("mods: %s: @%d has %u scripts, not a script %u: not used", f->what, cur, cnt, n); continue; }
            sn = (int)n;
            if (nmv == 256) { note("mods: %s: @%d: too many scripts moved: script %u not used", f->what, cur, n); sn = -1; continue; }
            nw = 0;
            bad = 0;
            last = -1;
            continue;
        }
        if (sscanf(line, "%31s", word) != 1 || sn < 0 || bad) continue;
        static long a[4096];
        int na = 0;
        char *q = strstr(line, word) + strlen(word), *e;
        for (; na < 4096; q = e) {
            a[na] = strtol(q, &e, 0);
            if (e == q) break;
            na++;
        }
        int raw = !strcasecmp(word, "raw"), op = raw ? -1 : scr_op(word), l = op >= 0 ? scr_oplen(test->data, op) : 0;
        while (*q == ' ' || *q == '\t' || *q == '\r') q++;
        if (*q || (!raw && (op < 0 || !l || na != l - 1)) || nw + 1 + na > 32768) {
            note("mods: %s: line %d: not an op with its %d operand(s) (data/SCRIPTS.txt has them all): @%d script %d not used",
                 f->what, ln, l ? l - 1 : 0, cur, sn);
            bad = 1;
            continue;
        }
        if (!raw) { w[nw++] = (uint16_t)op; last = op; }
        for (int k = 0; k < na; k++) w[nw++] = (uint16_t)(int16_t)a[k];
    }
    free(w);
    free(app);
    note("mods: %s: %d scripts changed (%d after their room's data)", f->what, changed, moved);
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
        } else if (f->kind == 2) {
            scripts_apply(f, test, s);
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
    o = 0;
    ADD("# The rooms' event scripts (ROOMENV.DAT: @block, block = stage * 30 + room; script_runner.c, script_ops.c).\n"
        "# An op a line: its name (OpNN when the game's code for it hasn't a name yet) and its operands, 16-bit\n"
        "# numbers (a 32-bit one is two, the low half first). A script you list replaces that one; the others stay.\n"
        "# Ops: a script thread runs them in order; IfFlag ... Else ... EndIf are blocks; StartThread starts script\n"
        "# n; ThreadEnd ends the thread. A script longer than its place moves after the room's data and must end\n"
        "# with ThreadEnd. Operand counts:");
    for (int k = 0; k < SCR_NOPS; k++) {
        scr_name(k, name, sizeof name);
        ADD("%s %s %d", k % 8 ? "," : "\n#  ", name, scr_oplen(test->data, k) - 1);
    }
    ADD("\n");
    for (uint32_t b = 0; b < nb; b++) {
        uint32_t at = v[b] * 2048, len = (v[b + 1] - v[b]) * 2048, base, cnt;
        if (v[b + 1] == v[b] || !scr_part(t->data, at, len, &base, &cnt) || !cnt) continue;
        ADD("@%u    # stage %u, room %u\n", b, b / 30, b % 30);
        for (uint32_t n = 0; n < cnt; n++) {
            uint32_t s0, e0;
            scr_span(test->data, t->data, at, len, base, cnt, n, &s0, &e0);
            ADD("script %u\n", n);
            for (uint32_t p = s0; p < e0;) {
                int op = (int16_t)rd16(t->data + at + base + p), l = scr_oplen(test->data, op);
                if (!l || p + 2 * (uint32_t)l > e0) {   /* (not ops: as they are) */
                    ADD("  raw");
                    for (; p < e0; p += 2) ADD(" %d", (int16_t)rd16(t->data + at + base + p));
                    ADD("\n");
                    break;
                }
                scr_name(op, name, sizeof name);
                ADD("  %s", name);
                for (int k = 1; k < l; k++) ADD(" %d", (int16_t)rd16(t->data + at + base + p + 2 * (uint32_t)k));
                ADD("\n");
                p += 2 * (uint32_t)l;
            }
        }
    }
    files += write_out("data/SCRIPTS.txt", (const uint8_t *)out, o);
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
