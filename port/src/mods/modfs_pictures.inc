/* modfs.c, part: Pictures: TIMs, the PC's BMPs, PICTURES/<file>/<n>.png, HD versions. Included by modfs.c, in this order. */

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
    /* remembered for the palette last asked about (a picture asks for each of its pixels) */
    static uint16_t last[256];
    static int lastn = -1;
    static int16_t memo[32768];
    if (n > 256) n = 256;
    if (n != lastn || memcmp(last, pal, (size_t)n * 2)) {
        memcpy(last, pal, (size_t)n * 2);
        lastn = n;
        memset(memo, 0xFF, sizeof memo);
    }
    if (memo[v & 0x7FFF] >= 0) return memo[v & 0x7FFF];
    int best = 0, bd = 1 << 30;
    for (int i = 0; i < n; i++) {
        int dr = (int)(pal[i] & 31) - (v & 31), dg = (int)((pal[i] >> 5) & 31) - ((v >> 5) & 31);
        int db = (int)((pal[i] >> 10) & 31) - ((v >> 10) & 31);
        int d = dr * dr * 3 + dg * dg * 4 + db * db * 2;
        if (pal[i] && d < bd) { bd = d; best = i; }
    }
    memo[v & 0x7FFF] = (int16_t)best;
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

static int tim_keep_palette;                    /* (picture_apply: the palette's place is another picture's too) */
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
    uint8_t *row0 = clut ? clut + 12 : NULL;
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
    int same = 1;
    for (uint32_t k = 0; k < tw * th; k++) {
        uint32_t p = rgba[k];
        int clear = (p >> 24) < 128 || ((p & 0xFFFFFF) == 0 && was[k] == 0);
        col[k] = clear ? 0 : to15(p);
        opaque += !clear;
        if (same && !(col[k] == (was[k] & 0x7FFF) || (col[k] == 0x0400 && was[k] && !(was[k] & 0x7FFF)))) same = 0;
    }
    free(rgba);
    if (same) {                                  /* the picture as the disc has it: left as it is */
        free(col);
        free(was);
        *how = "as on the disc";
        return NULL;
    }
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
        if (chs == 1 && !tim_keep_palette) {     /* a palette for this picture: entry 0 see-through */
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

/* TIMs a mod changed, as they'd be in VRAM: a quick save made before the
 * mod has the old pixels there (a font loaded once a game), so loading it
 * puts the new ones in where the old still are (modfs_state_loaded). */
static uint32_t tim_len(const uint8_t *tim) {
    uint32_t flag = rd32(tim + 4), n = 8;
    if (flag & 8) n += rd32(tim + 8);
    return n + rd32(tim + n);
}

/* The TIM's blocks (its palette, its pixels) before and after. */
static void vram_fix_add(const uint8_t *before, const uint8_t *after) {
    uint32_t flag = rd32(after + 4), o = 8;
    for (int b = 0; b < ((flag & 8) ? 2 : 1); b++) {
        uint32_t len = rd32(after + o), x = rd16(after + o + 4), y = rd16(after + o + 6), w = rd16(after + o + 8), h = rd16(after + o + 10);
        if (len == rd32(before + o) && w && h && x + w <= 1024 && y + h <= 512 && memcmp(before + o + 12, after + o + 12, (size_t)w * h * 2)) {
            vfix = realloc(vfix, (size_t)(nvfix + 1) * sizeof *vfix);
            VramFix *f = &vfix[nvfix++];
            *f = (VramFix){(uint16_t)x, (uint16_t)y, (uint16_t)w, (uint16_t)h, malloc((size_t)w * h * 2), malloc((size_t)w * h * 2)};
            memcpy(f->was, before + o + 12, (size_t)w * h * 2);
            memcpy(f->now, after + o + 12, (size_t)w * h * 2);
        }
        o += len;
    }
}

static int modfs_hd_add(const uint8_t *tim, const uint8_t *img, uint32_t w, uint32_t h);

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
    uint32_t tl = tim_len(tim);
    uint8_t *before = malloc(tl);
    if (before) memcpy(before, tim, tl);
    const char *how = "", *why = tim_from_picture(tim, img, (uint32_t)w, (uint32_t)h, &how);
    if (!why && before) vram_fix_add(before, tim);
    free(before);
    int hd = !why && modfs_hd_add(tim, img, (uint32_t)w, (uint32_t)h);
    stbi_image_free(img);
    if (why) {
        note("mods: %s/%s: %s: not used", mod, rel, why);
        return 1;
    }
    uint32_t tw = 0, th = 0;
    tim_size(tim, &tw, &th);
    note("mods: %s/%s: into %s's picture at %u (%ux%u, %s%s)", mod, rel, modfs_bmp[i].disc, modfs_bmp[i].offset, tw, th, how,
           hd ? ", with an HD replacement" : (uint32_t)w != tw || (uint32_t)h != th ? ", scaled to the PS1's size" : "");
    nused++;
    return 1;
}

/* Every picture of a disc file (TIMs as the game loads them: 4 or 8 bits
 * with a palette, or 16 bits), in order: PICTURES/<file>/<n>.png names
 * the n-th (extract writes them all). A picture in a packed block isn't
 * one of these (its block has its own names). */
static uint32_t tim_at(const uint8_t *d, uint32_t n, uint32_t o) {   /* its length, 0 if none there */
    if ((uint64_t)o + 20 > n || rd32(d + o) != 0x10) return 0;
    uint32_t flag = rd32(d + o + 4), p = o + 8;
    if (flag != 8 && flag != 9 && flag != 2) return 0;
    for (int part = flag & 8 ? 0 : 1; part < 2; part++) {
        if ((uint64_t)p + 12 > n) return 0;
        uint32_t len = rd32(d + p), x = rd16(d + p + 4), y = rd16(d + p + 6), w = rd16(d + p + 8), h = rd16(d + p + 10);
        if (!w || !h || x >= 1024 || y >= 512 || len != 12 + w * h * 2 || len > (part ? 0x100000u : 0x20000u) || (uint64_t)p + len > n) return 0;
        p += len;
    }
    return p - o;
}
static int file_pictures(const uint8_t *d, uint32_t n, uint32_t *off, int max) {
    int k = 0;
    for (uint32_t o = 0; o + 20 <= n && k < max;) {
        uint32_t len = tim_at(d, n, o);
        if (len) { off[k++] = o; o += (len + 3) & ~3u; }
        else o += 4;
    }
    return k;
}
static int packed_archive(int fi);
static int picture_apply(const char *mod, const char *rel, const char *up, const char *path) {
    if (strncmp(up, "PICTURES/", 9)) return 0;
    const char *slash = strrchr(up, '/'), *dot = strrchr(up, '.');
    char file[256];
    unsigned idx;
    char rest[8] = "";
    if (!dot || !is_picture(dot) || slash - (up + 9) <= 0 || slash - (up + 9) >= (int)sizeof file ||
        sscanf(slash + 1, "%u%7s", &idx, rest) != 2 || strcasecmp(rest, dot)) {
        note("mods: %s/%s: name it PICTURES/<disc file>/<number>.png, as extract does: not used", mod, rel);
        return 1;
    }
    memcpy(file, up + 9, (size_t)(slash - (up + 9)));
    file[slash - (up + 9)] = 0;
    char *sub = strrchr(file, '/');              /* PICTURES/<file>/<block>-<part>/<n>.png: inside a packed block */
    unsigned pb = 0, pk = 0;
    char prest[4];
    Packed *pp = NULL;
    if (sub && sscanf(sub + 1, "%u-%u%3s", &pb, &pk, prest) == 2) *sub = 0;
    else sub = NULL;
    int fi = disc_index(file);
    Touched *t = fi >= 0 ? touch(fi) : NULL;
    uint8_t *data = t ? t->data : NULL;
    uint32_t dn = t ? t->nsec * 2048 : 0;
    if (sub) {
        pp = t && packed_archive(fi) ? packed_get(fi, pb) : NULL;
        data = pp ? packed_part(pp, (int)pk, &dn) : NULL;
        if (!data) dn = 0;
        snprintf(file + strlen(file), sizeof file - strlen(file), " block %u part %u", pb, pk);
    }
    static uint32_t off[4096];
    int count = data ? file_pictures(data, dn, off, 4096) : 0;
    if (!data || (int)idx >= count) {
        note("mods: %s/%s: %s has no picture %u (extract shows its pictures): not used", mod, rel, file, idx);
        return 1;
    }
    uint8_t *tim = data + off[idx];
    int shared = -1;                             /* another picture whose palette goes to the same place */
    if (rd32(tim + 4) & 8)
        for (int k = 0; k < count && shared < 0; k++)
            if (k != (int)idx && (rd32(data + off[k] + 4) & 8) && rd32(data + off[k] + 12) == rd32(tim + 12)) shared = k;
    long n = 0;
    uint8_t *d = load(path, &n);
    int w, h, c;
    uint8_t *img = d ? stbi_load_from_memory(d, (int)n, &w, &h, &c, 4) : NULL;
    free(d);
    if (!img) { note("mods: %s/%s: couldn't read it as a picture: not used", mod, rel); return 1; }
    uint32_t tl = tim_len(tim);
    uint8_t *before = malloc(tl);
    if (before) memcpy(before, tim, tl);
    tim_keep_palette = shared >= 0;
    const char *how = "", *why = tim_from_picture(tim, img, (uint32_t)w, (uint32_t)h, &how);
    tim_keep_palette = 0;
    if (!why && before) vram_fix_add(before, tim);
    free(before);
    int hd = !why && modfs_hd_add(tim, img, (uint32_t)w, (uint32_t)h);
    stbi_image_free(img);
    if (why) { note("mods: %s/%s: %s: not used", mod, rel, why); return 1; }
    if (pp) {                                    /* packed again at the end (packs_done) */
        char what[400];
        snprintf(what, sizeof what, "%s/%s", mod, rel);
        packed_note(pp, (int)pk, what);
    }
    uint32_t tw = 0, th = 0;
    tim_size(tim, &tw, &th);
    char shares[160] = "";
    if (shared >= 0 && !hd)
        snprintf(shares, sizeof shares, "; its palette is picture %d's too, so its colours stay: give it at 2x or more for any colour", shared);
    note("mods: %s/%s: into %s's picture %u (%ux%u, %s%s%s)", mod, rel, file, idx, tw, th, how,
         hd ? ", with an HD replacement" : (uint32_t)w != tw || (uint32_t)h != th ? ", scaled to the PS1's size" : "", shares);
    nused++;
    return 1;
}

/* HD keeps the native upload and palette identities. With multiple
 * CLUT rows, the authored picture represents row zero: retain its palette
 * indices at high resolution and map them through each original row.
 * A green/red UI font must not reuse the row-zero colour image. */
static int modfs_hd_add_banks(const uint8_t *tim, const uint8_t *img, uint32_t w, uint32_t h, const uint8_t *extra, uint32_t extra_rows) {
    uint32_t flag = rd32(tim + 4), bpp = (uint32_t[]){4, 8, 16, 24}[flag & 3];
    const uint8_t *clut = flag & 8 ? tim + 8 : NULL;
    if (bpp == 24 || (bpp < 16 && !clut)) return 0;
    const uint8_t *pix = clut ? clut + rd32(clut) : tim + 8;
    uint32_t tw = rd16(pix + 8) * 16 / bpp, th = rd16(pix + 10), ww = rd16(pix + 8);
    uint32_t own_rows = bpp < 16 ? rd16(clut + 10) : 1, cw = clut ? rd16(clut + 8) : 0;
    if (bpp == 16) extra_rows = 0;
    uint32_t npal = own_rows + extra_rows;
    uint32_t colors = bpp == 4 ? 16 : 256;
    if (!tw || !th || !npal || (bpp < 16 && cw < colors)) return 0;
    uint32_t k = w / tw < h / th ? w / tw : h / th;
    if (k < 2) return 0;
    uint32_t W = k * tw, H = k * th, x = rd16(pix + 4), y = rd16(pix + 6);
    extern size_t g_hd_budget;
    uint64_t pixels = (uint64_t)W * H;
    if (W > INT32_MAX || H > INT32_MAX || pixels > SIZE_MAX / 4 / npal || pixels > g_hd_budget / 4 / npal || x + ww > 1024 || y + th > 512) return 0;
    uint32_t **images = calloc(npal, sizeof *images), *keys = calloc(npal, sizeof *keys);
    if (!images || !keys) { free(images); free(keys); return 0; }
    uint16_t source_pal[256];
    if (bpp < 16) for (uint32_t i = 0; i < colors; i++) source_pal[i] = rd16(clut + 12 + i * 2);
    for (uint32_t row = 0; row < npal; row++) {
        const uint8_t *pal = bpp < 16 ? (row < own_rows ? clut + 12 + (size_t)row * cw * 2 : extra + (size_t)(row - own_rows) * colors * 2) : NULL;
        keys[row] = pal ? (uint32_t)mz_crc32(0, pal, (size_t)colors * 2) : 0;
        int duplicate = 0;
        for (uint32_t j = 0; j < row; j++) if (keys[j] == keys[row]) duplicate = 1;
        if (duplicate) continue;
        uint32_t *px = images[row] = malloc((size_t)W * H * 4);
        if (!px) { for (uint32_t j = 0; j < row; j++) free(images[j]); free(images); free(keys); return 0; }
        for (uint32_t yy = 0; yy < H; yy++) for (uint32_t xx = 0; xx < W; xx++) {
            uint32_t sx = (uint32_t)((uint64_t)xx * w / W), sy = (uint32_t)((uint64_t)yy * h / H), tx = xx / k, ty = yy / k, v;
            const uint8_t *p = img + ((size_t)sy * w + sx) * 4;
            uint32_t rgba = p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16;
            if (npal > 1) {
                uint32_t ni = bpp == 8 ? pix[12 + (size_t)ty * tw + tx]
                    : (pix[12 + ((size_t)ty * tw + tx) / 2] >> (tx & 1) * 4) & 15;
                uint16_t native = source_pal[ni], colour = to15(rgba);
                /* Match the native converter's transparency rule. Keep an
                 * unchanged native index: two equal colours in row zero may
                 * have different colours in another row. STP black is opaque. */
                int clear = p[3] < 128 || (!rgba && !native);
                int index = colour == (native & 0x7FFF) || (!rgba && native == 0x8000)
                    ? (int)ni : nearest(source_pal, (int)colors, colour);
                v = clear ? 0 : rd16(pal + index * 2);
                uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
                rgba = (r | r >> 5) | (g | g >> 5) << 8 | (b | b >> 5) << 16;
            } else if (bpp == 16) v = rd16(pix + 12 + 2 * ((size_t)ty * tw + tx));
            else if (bpp == 8) v = rd16(pal + 2 * pix[12 + (size_t)ty * tw + tx]);
            else v = rd16(pal + 2 * ((pix[12 + ((size_t)ty * tw + tx) / 2] >> (tx & 1) * 4) & 15));
            px[(size_t)yy * W + xx] = rgba | (!v ? 0 : (uint32_t)p[3] << 24);
        }
    }
    uint32_t hash = (uint32_t)mz_crc32(0, pix + 12, (size_t)ww * th * 2);
    int found = 0;
    for (int i = 0; i < nmod_hd_rects; i++) {
        ModHdRect *r = &mod_hd_rects[i];
        if (r->x == x && r->y == y && r->w == ww && r->h == th && r->hash == hash) found = 1;
    }
    if (!found) {
        ModHdRect *r = realloc(mod_hd_rects, (size_t)(nmod_hd_rects + 1) * sizeof *r);
        if (!r) { for (uint32_t row = 0; row < npal; row++) free(images[row]); free(images); free(keys); return 0; }
        mod_hd_rects = r;
        mod_hd_rects[nmod_hd_rects++] = (ModHdRect){(uint16_t)x, (uint16_t)y, (uint16_t)ww, (uint16_t)th, hash};
    }
    void texpack_add_image(uint32_t hash, uint32_t pal, int w, int h, uint32_t *px);
    for (uint32_t row = 0; row < npal; row++) if (images[row]) texpack_add_image(hash, keys[row], (int)W, (int)H, images[row]);
    free(images); free(keys);
    return 1;
}

static int modfs_hd_add(const uint8_t *tim, const uint8_t *img, uint32_t w, uint32_t h) {
    return modfs_hd_add_banks(tim, img, w, h, NULL, 0);
}
