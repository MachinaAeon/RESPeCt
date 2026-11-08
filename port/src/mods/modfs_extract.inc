/* modfs.c, part: Writing out the originals (the extract folder). Included by modfs.c, in this order. */

/* ------------------------------------------------------------ extracting */

/* The originals of everything a mod can replace, written under the names
 * a mod gives them, to start from (the platform's `extract` folder, when
 * it's there and empty; RT_EXTRACT for the headless runner): the disc's
 * files that aren't code or streams, by their disc path; the files the
 * PC version has loose (rooms, doors, effects, items, Resource...), by
 * their PC path; the PC's 2D pictures as PNG (map/MAP00_1.png); each
 * enemy's texture as PNG (nab/ENEM00.png); every streamed sound channel
 * as WAV (X/BGM_32.STR/05.wav). Movies' frames come out as they play
 * (movie_dump). Read from the disc, mods left out. */
char g_extract_dir[1024];

static int write_out(const char *rel, const uint8_t *d, size_t n) {
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", g_extract_dir, rel);
    for (char *c = path + 1; *c; c++)
        if (*c == '/') { *c = 0; mkdir(path); *c = '/'; }
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    fwrite(d, 1, n, f);
    fclose(f);
    return 1;
}

static int ui_extract(void) {
    Touched *t;
    const uint8_t *tab, *pages;
    if (!diary_info(&t, &tab, &pages)) return 0;
    char output[4096] = "", text[512];
    size_t n = 0;
    Touched *test = touch(disc_index("TEST.BIN"));
    for (size_t i = 0; i < sizeof ui_texts / sizeof ui_texts[0]; i++) {
        int ended;
        if (ui_texts[i].original >= 0x800C3E48u) text_decode(diary_meta + ui_texts[i].original - 0x800C3E48u, 56, text, sizeof text, &ended);
        else if (test) text_decode(test->data + ui_texts[i].original - TEST_BASE, 240, text, sizeof text, &ended);   /* (the doors') */
        else continue;
        if (!ended) return 0;
        n += (size_t)snprintf(output + n, sizeof output - n, "@%s\n%s\n", ui_texts[i].name, text);
    }
    return write_out("text/UI.txt", (const uint8_t *)output, n);
}

static uint8_t *disc_read(int fi) {               /* a disc file as it is on the disc */
    uint32_t nsec = (disc_files[fi].size + 2047) / 2048;
    uint8_t *d = malloc((size_t)nsec * 2048 + 1), s[2352];
    disc_only = 1;
    for (uint32_t k = 0; d && k < nsec; k++) {
        cd_read_sector_raw(disc_files[fi].lba + k, s);
        memcpy(d + (size_t)k * 2048, s + 24, 2048);
    }
    disc_only = 0;
    return d;
}

/* A TIM as a PNG (its first palette), to path. */
static int tim_png(const char *rel, const uint8_t *tim) {
    uint32_t flag = rd32(tim + 4), bpp = (uint32_t[]){4, 8, 16, 24}[flag & 3], w, h;
    if (bpp == 24) return 0;
    const uint8_t *clut = flag & 8 ? tim + 12 + 8 : NULL, *pix = flag & 8 ? tim + 8 + rd32(tim + 8) : tim + 8;
    tim_size(tim, &w, &h);
    uint8_t *rgb = malloc((size_t)w * h * 3);
    for (uint32_t k = 0; k < w * h; k++) {
        uint32_t v = bpp == 16 ? rd16(pix + 12 + 2 * k) : bpp == 8 ? rd16(clut + 2 * pix[12 + k]) : rd16(clut + 2 * ((pix[12 + k / 2] >> (k & 1) * 4) & 15));
        uint32_t r = (v & 31) << 3, g = ((v >> 5) & 31) << 3, b = ((v >> 10) & 31) << 3;
        rgb[3 * k] = (uint8_t)(r | r >> 5);
        rgb[3 * k + 1] = (uint8_t)(g | g >> 5);
        rgb[3 * k + 2] = (uint8_t)(b | b >> 5);
    }
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", g_extract_dir, rel);
    for (char *c = path + 1; *c; c++)
        if (*c == '/') { *c = 0; mkdir(path); *c = '/'; }
    png_write(path, rgb, (int)w, (int)h);
    free(rgb);
    FILE *f = fopen(path, "rb");
    if (f) fclose(f);
    return f != NULL;
}

/* XA ADPCM, one sector, appended to a channel's PCM (as spu.c plays it). */
typedef struct { int16_t *pcm; size_t n, cap; int32_t hist[2][2]; uint32_t rate; int stereo; } XaChan;
static void xa_decode(const uint8_t *sec, XaChan *c) {
    static const int pos[5] = {0, 60, 115, 98, 122}, neg[5] = {0, 0, -52, -55, -60};
    uint8_t coding = sec[19];
    int stereo = (coding & 3) == 1, bits8 = ((coding >> 4) & 3) == 1;
    c->stereo = stereo;
    c->rate = (coding >> 2) & 1 ? 18900 : 37800;
    size_t frames = (size_t)(bits8 ? 4 : 8) * 28 * 18 / (stereo ? 2 : 1);
    if (c->n + frames > c->cap) {
        c->cap = (c->n + frames) * 2;
        c->pcm = realloc(c->pcm, c->cap * 4);
    }
    int16_t *out = c->pcm + c->n * 2;
    size_t at[2] = {0, 0};
    for (int g = 0; g < 18; g++) {
        const uint8_t *grp = sec + 24 + g * 128;
        for (int u = 0; u < (bits8 ? 4 : 8); u++) {
            int shift = grp[4 + u] & 15, filt = (grp[4 + u] >> 4) & 3;
            if (shift > 12) shift = 9;
            int ch = stereo ? (u & 1) : 0;
            int32_t *h = c->hist[ch];
            for (int i = 0; i < 28; i++) {
                int32_t t;
                if (bits8) t = (int32_t)(int8_t)grp[16 + i * 4 + u] << 8;
                else {
                    int nib = (grp[16 + i * 4 + (u >> 1)] >> ((u & 1) * 4)) & 15;
                    t = (nib & 8 ? nib - 16 : nib) << 12;
                }
                int32_t v = (t >> shift) + ((h[0] * pos[filt] + h[1] * neg[filt] + 32) >> 6);
                v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
                h[1] = h[0];
                h[0] = v;
                size_t k = at[ch]++;
                if (stereo) out[2 * k + ch] = (int16_t)v;
                else out[2 * k] = out[2 * k + 1] = (int16_t)v;
            }
        }
    }
    c->n += frames;
}

static int wav_write(const char *rel, const int16_t *lr, size_t frames, uint32_t rate) {
    size_t n = frames * 4;
    uint8_t *d = malloc(44 + n);
    memcpy(d, "RIFF", 4);
    wr32(d + 4, (uint32_t)(36 + n));
    memcpy(d + 8, "WAVEfmt ", 8);
    wr32(d + 16, 16);
    wr16(d + 20, 1);
    wr16(d + 22, 2);
    wr32(d + 24, rate);
    wr32(d + 28, rate * 4);
    wr16(d + 32, 4);
    wr16(d + 34, 16);
    memcpy(d + 36, "data", 4);
    wr32(d + 40, (uint32_t)n);
    for (size_t k = 0; k < frames * 2; k++) wr16(d + 44 + 2 * k, (uint16_t)lr[k]);
    int ok = write_out(rel, d, 44 + n);
    free(d);
    return ok;
}

/* A sound of the banks as a WAV, mono, with its loop (a smpl chunk) if it repeats. */
static int sfx_write(const char *rel, const int16_t *pcm, uint32_t frames, uint32_t rate, int64_t from) {
    uint32_t n = frames * 2, extra = from >= 0 ? 8 + 60 : 0;
    uint8_t *d = calloc(1, 44 + n + extra);
    if (!d) return 0;
    memcpy(d, "RIFF", 4);
    wr32(d + 4, 36 + n + extra);
    memcpy(d + 8, "WAVEfmt ", 8);
    wr32(d + 16, 16);
    wr16(d + 20, 1);
    wr16(d + 22, 1);
    wr32(d + 24, rate);
    wr32(d + 28, rate * 2);
    wr16(d + 32, 2);
    wr16(d + 34, 16);
    memcpy(d + 36, "data", 4);
    wr32(d + 40, n);
    for (uint32_t k = 0; k < frames; k++) wr16(d + 44 + 2 * k, (uint16_t)pcm[k]);
    if (extra) {
        uint8_t *m = d + 44 + n;
        memcpy(m, "smpl", 4);
        wr32(m + 4, 60);
        wr32(m + 8 + 12, (uint32_t)(1000000000ull / rate));   /* sample period, ns */
        wr32(m + 8 + 16, 60);                                /* MIDI unity note */
        wr32(m + 8 + 28, 1);                                 /* one loop: */
        wr32(m + 8 + 44, (uint32_t)from);                    /* from, */
        wr32(m + 8 + 48, frames - 1);                        /* to the end */
    }
    int ok = write_out(rel, d, 44 + n + extra);
    free(d);
    return ok;
}

static int nextracted;
int modfs_extracted(void) { return nextracted; }

/* Returns how many files it wrote. */
int modfs_extract(void) {
    if (!g_extract_dir[0]) return 0;
    note("mods: writing the originals to %s (it takes a minute)", g_extract_dir);
    int files = 0;
    char rel[700];
    /* the disc's files, and the sound of its streams */
    for (size_t i = 0; i < sizeof disc_files / sizeof disc_files[0]; i++) {
        if (disc_files[i].hash) continue;
        if (!strstr(disc_files[i].name, ".STR")) {
            uint8_t *d = disc_read((int)i);
            size_t z = 0;
            while (d && z < disc_files[i].size && !d[z]) z++;
            if (d && z < disc_files[i].size) files += write_out(disc_files[i].name, d, disc_files[i].size);   /* (not the disc's filler) */
            free(d);
            continue;
        }
        static XaChan ch[32];
        memset(ch, 0, sizeof ch);
        uint8_t s[2352];
        uint32_t nsec = (disc_files[i].size + 2047) / 2048;
        disc_only = 1;
        for (uint32_t k = 0; k < nsec; k++) {
            if (!cd_read_sector_raw(disc_files[i].lba + k, s)) continue;
            if (s[15] != 2 || !(s[18] & 0x04) || s[17] >= 32) continue;
            xa_decode(s, &ch[s[17]]);
        }
        disc_only = 0;
        for (int c = 0; c < 32; c++) {
            if (!ch[c].n) continue;
            snprintf(rel, sizeof rel, "%s/%02d.wav", disc_files[i].name, c);
            files += wav_write(rel, ch[c].pcm, ch[c].n, ch[c].rate);
            free(ch[c].pcm);
        }
    }
    /* the sound banks' sounds, each once (the same one in other banks is changed with it) */
    {
        Touched *t = touch(disc_index("NOISE.BIN"));
        static uint32_t v[1024];
        Bank *bk = NULL;
        uint32_t nb = noise_banks(t, v, &bk);
        uint32_t *seen = calloc(65536, sizeof *seen);   /* by crc: the first place it was, block << 8 | sound, +1 */
        for (uint32_t b = 0; seen && b < nb; b++)
            for (uint32_t j = 1; j <= bk[b].vs; j++) {
                const uint8_t *w = t->data + vag_off(t->data, &bk[b], j);
                uint32_t sz = vag_size(t->data, &bk[b], j), crc = (uint32_t)mz_crc32(0, w, sz), h = crc & 0xFFFF, dup = 0;
                for (uint32_t k = h; seen[k] && !dup; k = (k + 1) & 0xFFFF) {
                    const Bank *o = &bk[(seen[k] - 1) >> 8];
                    uint32_t oj = (seen[k] - 1) & 0xFF;
                    dup = vag_size(t->data, o, oj) == sz && !memcmp(t->data + vag_off(t->data, o, oj), w, sz);
                    h = (k + 1) & 0xFFFF;
                }
                if (dup || sz < 16) continue;
                seen[h] = (b << 8 | j) + 1;
                Shape sh = vag_shape(w, sz / 16);
                int16_t *pcm = vag_decode(w, sz / 16);
                snprintf(rel, sizeof rel, "NOISE/%03u/%02u.wav", b, j);
                if (pcm) files += sfx_write(rel, pcm + 28 * sh.lead, sh.data * 28, vag_rate(t->data, &bk[b], j), sh.loop ? (int64_t)sh.from * 28 : -1);
                free(pcm);
            }
        for (uint32_t b = 0; b < nb; b++) {     /* the rooms' music */
            uint32_t at = v[b] * 2048, c = bk[b].vs ? rd32(t->data + at) : 0;
            for (uint32_t k = 1; k + 2 <= c; k++) {
                uint32_t off = rd32(t->data + at + 4 + 4 * k), end = rd32(t->data + at + 8 + 4 * k);
                snprintf(rel, sizeof rel, "NOISE/%03u/music%u.seq", b, k);
                if (end > off && !memcmp(t->data + at + off, "pQES", 4)) files += write_out(rel, t->data + at + off, end - off);
            }
        }
        free(seen);
        free(bk);
    }
    /* the texts, the font's pages, the items' pictures */
    files += texts_extract();
    files += data_extract();
    files += diary_extract();
    files += ui_extract();
    for (unsigned k = 0x186; k <= 0x188; k++) {
        char up[32], name[64];
        snprintf(up, sizeof up, "FONT/FONT%X.PNG", k);
        uint8_t *tim = own_picture(up, name, sizeof name);
        snprintf(rel, sizeof rel, "FONT/FONT%X.png", k);
        if (tim) files += tim_png(rel, tim);
    }
    for (unsigned k = 0; k < 64; k++) {
        char up[32], name[64];
        snprintf(up, sizeof up, "BIGTIM/%02u.PNG", k);
        uint8_t *tim = own_picture(up, name, sizeof name);
        snprintf(rel, sizeof rel, "BIGTIM/%02u.png", k);
        if (tim) files += tim_png(rel, tim);
    }
    /* the PC version's names: loose files, ones in packed blocks, 2D pictures, enemies' textures */
    for (size_t i = 0; i < sizeof modfs_pc / sizeof modfs_pc[0]; i++) {
        Touched *t = touch(disc_index(modfs_pc[i].disc));
        const uint8_t *d = t ? t->data : NULL;
        if (d && (uint32_t)mz_crc32(0, d + modfs_pc[i].offset, modfs_pc[i].size) == modfs_pc[i].crc)
            files += write_out(modfs_pc[i].pc, d + modfs_pc[i].offset, modfs_pc[i].size);
    }
    for (size_t i = 0; i < sizeof modfs_pack / sizeof modfs_pack[0]; i++) {
        if (i && !strcmp(modfs_pack[i].pc, modfs_pack[i - 1].pc)) continue;   /* (the same file in another place) */
        Packed *p = packed_get(disc_index(modfs_pack[i].disc), modfs_pack[i].block);
        uint32_t n = 0;
        const uint8_t *u = NULL;
        if (modfs_pack[i].part == -2) {
            int fi = disc_index(modfs_pack[i].disc);
            Touched *test = touch(disc_index("TEST.BIN")), *t = touch(fi);
            uint32_t at = t && test ? rd32(test->data + archive_table(fi) - TEST_BASE + 4 * modfs_pack[i].block) * 2048 + modfs_pack[i].offset : 0;
            if (t && (uint32_t)mz_crc32(0, t->data + at, modfs_pack[i].size) == modfs_pack[i].crc)
                files += write_out(modfs_pack[i].pc, t->data + at, modfs_pack[i].size);
            continue;
        }
        u = packed_part(p, modfs_pack[i].part < 0 ? 0 : modfs_pack[i].part, &n);
        if (u && modfs_pack[i].offset + modfs_pack[i].size <= n &&
            (uint32_t)mz_crc32(0, u + modfs_pack[i].offset, modfs_pack[i].size) == modfs_pack[i].crc)
            files += write_out(modfs_pack[i].pc, u + modfs_pack[i].offset, modfs_pack[i].size);
    }
    for (size_t i = 0; i < sizeof modfs_bmp / sizeof modfs_bmp[0]; i++) {
        if (i && !strcmp(modfs_bmp[i].pc, modfs_bmp[i - 1].pc)) continue;
        Touched *t = touch(disc_index(modfs_bmp[i].disc));
        const uint8_t *d = t ? t->data : NULL;
        snprintf(rel, sizeof rel, "%s", modfs_bmp[i].pc);
        char *dot = strrchr(rel, '.');
        if (dot) strcpy(dot, ".png");
        if (d && (uint32_t)mz_crc32(0, d + modfs_bmp[i].offset, modfs_bmp[i].size) == modfs_bmp[i].crc)
            files += tim_png(rel, d + modfs_bmp[i].offset);
    }
    for (size_t fi = 0; fi < sizeof disc_files / sizeof disc_files[0]; fi++) {   /* every picture, by file and number */
        const char *nm = disc_files[fi].name, *ext = strrchr(nm, '.');
        if (!ext || !strcasecmp(ext, ".STR") || !strcasecmp(ext, ".XA") || disc_files[fi].size < 32) continue;
        uint8_t *d = disc_read((int)fi);
        static uint32_t off[4096];
        int count = d ? file_pictures(d, (disc_files[fi].size + 2047) / 2048 * 2048, off, 4096) : 0;
        for (int k = 0; k < count; k++) {
            snprintf(rel, sizeof rel, "PICTURES/%s/%03d.png", nm, k);
            files += tim_png(rel, d + off[k]);
        }
        free(d);
    }
    static const char *const packed_files[] = {"OBJ.DAT", "EV_OBJ.DAT", "ITEMDAT.DAT"};   /* the pictures inside packed blocks */
    for (int a = 0; a < 3; a++) {
        int fi = disc_index(packed_files[a]);
        Touched *test = touch(disc_index("TEST.BIN")), *t = touch(fi);
        uint32_t tab = archive_table(fi);
        if (!test || !t || !tab) continue;
        for (uint32_t b = 0; b < 4096; b++) {
            uint32_t s0 = rd32(test->data + tab - TEST_BASE + 4 * b), s1 = rd32(test->data + tab - TEST_BASE + 4 * b + 4);
            if (s1 < s0 || s1 > t->nsec) break;
            if (s1 == s0) continue;
            int keep = npacks;
            Packed *p = packed_get(fi, b);
            for (int k = 0; p && k < (p->split ? 3 : 1); k++) {
                uint32_t n = 0;
                uint8_t *u = packed_part(p, k, &n);
                static uint32_t off[4096];
                int count = u ? file_pictures(u, n, off, 4096) : 0;
                for (int j = 0; j < count; j++) {
                    snprintf(rel, sizeof rel, "PICTURES/%s/%u-%d/%03d.png", packed_files[a], b, k, j);
                    files += tim_png(rel, u + off[j]);
                }
            }
            if (p && p == &packs[keep]) {        /* (one of its own: let it go) */
                for (int k = 0; k < 3; k++) { free(p->part[k]); free(p->was[k]); }
                npacks = keep;
            }
        }
    }
    for (int type = 0; type <= 30; type++) {
        Packed *p = packed_get(disc_index(type == 30 ? "EV_OBJ.DAT" : "OBJ.DAT"), type == 30 ? 0x196 : (uint32_t)type);
        uint32_t n = 0;
        uint8_t *u = p && p->split ? packed_part(p, 0, &n) : NULL, *tim = u ? enemy_skin(u, n) : NULL;
        snprintf(rel, sizeof rel, "NAB/ENEM%02d.png", type);
        if (tim) files += tim_png(rel, tim);
    }
    for (int i = 0; i < npacks; i++)             /* nothing changed: just let them go */
        for (int k = 0; k < 3; k++) { free(packs[i].part[k]); free(packs[i].was[k]); }
    npacks = 0;
    note("mods: %d originals written to %s", files, g_extract_dir);
    nextracted = files;
    return files;
}
