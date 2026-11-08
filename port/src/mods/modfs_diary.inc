/* Per-page diary pictures. The USA file viewer's metadata is in the
 * CONPAU_E overlay, LZ-packed at STAGPROG.BIN + 0x21800. Its original
 * tables are read, never changed: 0x800CEDA0 block sectors, 0x800CF454
 * page counts/boundaries (file_viewer.c). A mod replaces a packed TIM
 * inside the existing CONFFILE block; CD positions, sizes and game code
 * stay intact. PC diary crops can be adapted explicitly by pc-bitmaps.txt;
 * filenames alone are not aliases because page order and pagination differ. */
#define DIARY_DOCS 13
#define DIARY_BLOCKS 119
static uint8_t *diary_meta;

static int diary_info(Touched **conf, const uint8_t **table, const uint8_t **pages) {
    *conf = touch(disc_index("CONFFILE.BIN"));
    if (!*conf) return 0;
    if (!diary_meta) {
        Touched *stage = touch(disc_index("STAGPROG.BIN"));
        uint32_t n = 0;
        if (!stage || stage->nsec * 2048u <= 0x21804) return 0;
        uint8_t *m = lz_unpack(stage->data + 0x21800, stage->nsec * 2048u - 0x21800, &n);
        if (!m || n < 0x800CF454u - 0x800C3E48u + DIARY_DOCS * 3 || rd32(m) != 13) { free(m); return 0; }
        diary_meta = m;
    }
    *table = diary_meta + 0x800CEDA0u - 0x800C3E48u;
    *pages = diary_meta + 0x800CF454u - 0x800C3E48u;
    if (rd32(*table) != 0 || rd32(*table + DIARY_BLOCKS * 4) != (*conf)->nsec) return 0;
    for (int i = 0; i < DIARY_BLOCKS; i++)
        if (rd32(*table + i * 4) > rd32(*table + (i + 1) * 4)) return 0;
    return 1;
}

/* All offsets and the TIM itself are checked before conversion. */
static int diary_tim_ok(const uint8_t *t, uint32_t n) {
    if (n < 20 || rd32(t) != 0x10 || rd32(t + 4) & ~0xBu || (rd32(t + 4) & 3) == 3) return 0;
    uint32_t o = 8, flag = rd32(t + 4);
    for (int k = 0; k < ((flag & 8) ? 2 : 1); k++) {
        if (o > n - 12) return 0;
        uint32_t z = rd32(t + o), x = rd16(t + o + 4), y = rd16(t + o + 6), w = rd16(t + o + 8), h = rd16(t + o + 10);
        if (!w || !h || z != 12 + (uint64_t)w * h * 2 || z > n - o || x + w > 1024 || y + h > 512) return 0;
        if (!k && (flag & 8) && w < ((flag & 3) == 0 ? 16u : 256u)) return 0;
        o += z;
    }
    return (flag & 3) == 2 || (flag & 8);
}

static int diary_slot(Touched *t, const uint8_t *tab, const uint8_t *pages, unsigned doc, unsigned page,
                      uint32_t *start, uint32_t *len, uint32_t *count, uint32_t *slot, uint32_t off[257]) {
    if (doc >= DIARY_DOCS || !page || page > pages[doc * 3]) return 0;
    uint32_t p1 = pages[doc * 3 + 1], p2 = pages[doc * 3 + 2];
    uint32_t part = page - 1 >= p2 ? 2 : page - 1 >= p1 ? 1 : 0;
    *slot = page - (part == 2 ? p2 : part == 1 ? p1 : 0);
    uint32_t block = 0x28 + doc * 3 + part;
    *start = rd32(tab + block * 4) * 2048;
    uint32_t end = rd32(tab + (block + 1) * 4) * 2048;
    if (end <= *start || end > t->nsec * 2048) return 0;
    *len = end - *start;
    const uint8_t *b = t->data + *start;
    *count = rd32(b);
    if (!*count || *count > 256 || 4 + 4 * *count >= *len || *slot >= *count) return 0;
    for (uint32_t i = 0; i < *count; i++) {
        off[i] = rd32(b + 4 + 4 * i);
        if (off[i] < 4 + 4 * *count || off[i] >= *len || (i && off[i] < off[i - 1])) return 0;
    }
    off[*count] = *len;
    return 1;
}

static int diary_apply_picture(const char *mod, const char *rel, unsigned doc, unsigned page, const uint8_t *img, int w, int h) {
    Touched *t;
    const uint8_t *tab, *pages;
    uint32_t at, len, count, slot, off[257], n = 0;
    if (!diary_info(&t, &tab, &pages) || !diary_slot(t, tab, pages, doc, page, &at, &len, &count, &slot, off)) {
        note("mods: %s/%s: no such PS1 diary page or invalid archive layout: not used", mod, rel); return 0;
    }
    uint8_t *tim = lz_unpack(t->data + at + off[slot], off[slot + 1] - off[slot], &n);
    if (!tim || !diary_tim_ok(tim, n)) { free(tim); note("mods: %s/%s: invalid packed page: not used", mod, rel); return 0; }
    uint8_t *before = malloc(n);
    if (!img || !before) { free(tim); free(before); note("mods: %s/%s: couldn't read or allocate the picture: not used", mod, rel); return 0; }
    memcpy(before, tim, n);
    const char *how = "", *why = tim_from_picture(tim, img, (uint32_t)w, (uint32_t)h, &how);
    uint8_t *packed = NULL, *out = NULL;
    uint32_t pn = 0;
    if (!why && memcmp(before, tim, n)) {
        packed = lz_pack(tim, n, &pn);
        uint32_t last_n = 0, consumed = 0;
        uint8_t *last = lz_unpack_size(t->data + at + off[count - 1], len - off[count - 1], &last_n, &consumed);
        int last_ok = last != NULL;
        free(last);
        uint64_t need = off[0];
        for (uint32_t i = 0; i < count; i++) {
            uint32_t z = i == slot ? pn : i + 1 == count ? consumed : off[i + 1] - off[i];
            need += ((uint64_t)z + 3) & ~UINT64_C(3);
        }
        if (!packed || !last_ok || need > len) why = "the changed page doesn't fit the original packed block";
        else if (!(out = calloc(1, len))) why = "couldn't allocate the changed block";
        else {
            memcpy(out, t->data + at, off[0]);
            uint32_t o = off[0];
            for (uint32_t i = 0; i < count; i++) {
                uint32_t z = i == slot ? pn : i + 1 == count ? consumed : off[i + 1] - off[i];
                wr32(out + 4 + 4 * i, o);
                memcpy(out + o, i == slot ? packed : t->data + at + off[i], z);
                o += (z + 3) & ~3u;
            }
        }
    }
    if (why) note("mods: %s/%s: %s: not used", mod, rel, why);
    else {
        if (out) memcpy(t->data + at, out, len);   /* commit only after the complete block fits */
        vram_fix_add(before, tim);
        int hd = modfs_hd_add(tim, img, (uint32_t)w, (uint32_t)h);
        note("mods: %s/%s: PS1 diary %u page %u (%s%s)", mod, rel, doc, page, how, hd ? ", with an HD replacement" : "");
        pc_diary_disable(doc); /* Every successful native route takes priority. */
        nused++;
    }
    free(out); free(packed); free(before); free(tim);
    return why ? 0 : 1;
}

static int diary_apply(const char *mod, const char *rel, const char *up, const char *path) {
    if (strncmp(up, "DIARY/", 6)) return 0;
    /* PC diary filenames require explicit page/crop adaptation in a manifest. */
    if (!strncmp(up + 6, "FILE", 4)) return 0;
    unsigned doc, page; char ext[16] = "";
    if (sscanf(up, "DIARY/%u/%u%15s", &doc, &page, ext) != 3 || !is_picture(ext)) {
        note("mods: %s/%s: use diary/00/01.png (PS1 document, page starting at 1): not used", mod, rel); return 1;
    }
    long size = 0; uint8_t *d = load(path, &size); int w = 0, h = 0, c;
    uint8_t *img = d && size > 0 && size <= INT32_MAX ? stbi_load_from_memory(d, (int)size, &w, &h, &c, 4) : NULL;
    free(d);
    if (!img || w <= 0 || h <= 0) note("mods: %s/%s: couldn't read the picture: not used", mod, rel);
    else diary_apply_picture(mod, rel, doc, page, img, w, h);
    stbi_image_free(img); return 1;
}

static int tim_png(const char *rel, const uint8_t *tim);
static int diary_extract(void) {
    Touched *t;
    const uint8_t *tab, *pages;
    if (!diary_info(&t, &tab, &pages)) return 0;
    int written = 0;
    for (unsigned doc = 0; doc < DIARY_DOCS; doc++) for (unsigned page = 1; page <= pages[doc * 3]; page++) {
        uint32_t at, len, count, slot, off[257], n;
        if (!diary_slot(t, tab, pages, doc, page, &at, &len, &count, &slot, off)) continue;
        uint8_t *tim = lz_unpack(t->data + at + off[slot], off[slot + 1] - off[slot], &n);
        if (tim && diary_tim_ok(tim, n)) {
            char rel[64];snprintf(rel, sizeof rel, "diary/%02u/%02u.png", doc, page);
            written += tim_png(rel, tim);
        }
        free(tim);
    }
    return written;
}
