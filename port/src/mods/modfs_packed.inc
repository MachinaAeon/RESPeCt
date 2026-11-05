/* modfs.c, part: Blocks of the packed archives (OBJ.DAT, EV_OBJ.DAT, ITEMDAT.DAT). Included by modfs.c, in this order. */

/* ------------------------------------------------------------ packed archives */

/* Blocks of the packed archives (OBJ.DAT, EV_OBJ.DAT, ITEMDAT.DAT) that
 * mods change: unpacked once, changed by every mod that has something
 * for them, packed again at the end (a part nothing changed keeps the
 * disc's packing). A block is either a header (3, then where each part
 * starts) and three packed parts, or packed whole (part 0 here). */
struct Packed {
    int fi;
    uint32_t block, start, len, off[4];
    int split, changed;                          /* changed: a bit per part */
    uint8_t *part[3], *was[3];
    uint32_t plen[3], was_len[3];
    char what[400];
};
static Packed packs[64];
static int npacks;

static uint32_t archive_table(int fi) {          /* its block table's address in TEST.BIN, or 0 */
    for (size_t k = 0; k < sizeof archives / sizeof archives[0]; k++)
        if (fi >= 0 && !strcasecmp(disc_files[fi].name, archives[k].name)) return archives[k].tab;
    return 0;
}

/* The archives whose blocks are packed (the game unpacks them as it loads them). */
static int packed_archive(int fi) {
    return fi >= 0 && (!strcasecmp(disc_files[fi].name, "OBJ.DAT") || !strcasecmp(disc_files[fi].name, "EV_OBJ.DAT") ||
                       !strcasecmp(disc_files[fi].name, "ITEMDAT.DAT"));
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
