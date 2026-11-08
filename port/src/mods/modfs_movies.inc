/* modfs.c, part: Movies: frames from pictures, video files. Included by modfs.c, in this order. */

/* ------------------------------------------------------------ movies */

/* A movie's pictures from a mod: M/OPENING.STR/0042.png is the whole
 * picture the game shows while it shows that stream's frame 42 (the
 * number in the frame's own header, from 1), at any size.
 *
 * Which frame the screen shows is followed as the game plays it: a frame
 * read whole from the disc (its last sector delivered) waits for the
 * decoder; the next picture decoded is that frame; its first slice
 * written to VRAM says which buffer it went to; and the screen shows
 * whatever buffer it shows. So it stays the game's own timing, skipping
 * and stopping included. */
typedef struct { int fi; uint32_t frame, seq; char *path; } Pic;
static Pic *pics;
static int npics, cpics;

static int cmp_pic(const void *a, const void *b) {
    const Pic *x = a, *y = b;
    if (x->fi != y->fi) return x->fi < y->fi ? -1 : 1;
    if (x->frame != y->frame) return x->frame < y->frame ? -1 : 1;
    return x->seq < y->seq ? -1 : x->seq > y->seq;
}

/* A movie as a video file (modvideo.c, FFmpeg): M/OPENING.STR/video.mp4 (any
 * format FFmpeg reads), or the PC version's avi/Opening.avi for the stream
 * of the same name. Its frames go over the stream's in proportion (the
 * first to the first, the last to the last), so the game's own timing
 * stays; a frame given as a picture wins over the video's. */
typedef struct Video Video;
Video *modvideo_open(const char *path, const char **why);
int modvideo_frames(Video *v);
const uint32_t *modvideo_frame(Video *v, int n, int *w, int *h);
typedef struct { int fi; char *path; char what[600]; Video *v; int bad, frames, str_frames; } Vid;
static Vid vids[32];
static int nvids;

static int is_video(const char *ext) {
    static const char *const e[] = {".AVI", ".MPG", ".MPEG", ".MP4", ".MKV", ".WEBM", ".MOV", ".WMV", ".M4V"};
    for (size_t i = 0; i < sizeof e / sizeof e[0]; i++)
        if (!strcasecmp(ext, e[i])) return 1;
    return 0;
}

static int video_apply(const char *mod, const char *rel, const char *up, const char *path) {
    const char *sl = strrchr(up, '/'), *dot = strrchr(up, '.');
    if (!sl || !dot || dot < sl || !is_video(dot)) return 0;
    char stream[512];
    if (!strncmp(up, "AVI/", 4) && !strchr(up + 4, '/'))           /* the PC's: avi/Opening.avi */
        snprintf(stream, sizeof stream, "M/%.*s.STR", (int)(dot - up - 4), up + 4);
    else if (!strncmp(sl, "/VIDEO.", 7))
        snprintf(stream, sizeof stream, "%.*s", (int)(sl - up), up);
    else
        return 0;
    int fi = disc_index(stream);
    if (fi < 0 || !strstr(stream, ".STR")) {
        note("mods: %s/%s: a video, but the disc has no movie %s: not used", mod, rel, stream);
        return 1;
    }
    int k = 0;
    while (k < nvids && vids[k].fi != fi) k++;   /* a later mod wins */
    if (k == nvids) {
        if (nvids == (int)(sizeof vids / sizeof vids[0])) { note("mods: %s/%s: too many videos: not used", mod, rel); return 1; }
        nvids++;
    } else {
        free(vids[k].path);
        nused--;
    }
    vids[k] = (Vid){fi, strdup(path), "", NULL, 0, 0, 0};
    snprintf(vids[k].what, sizeof vids[k].what, "%s/%s", mod, rel);
    note("mods: %s/%s: the video for %s (read when it plays)", mod, rel, stream);
    nused++;
    return 1;
}

/* How many frames a stream has (its last frame's number in the sectors' headers). */
static int str_frames(int fi) {
    uint8_t s[2352];
    uint32_t n = (disc_files[fi].size + 2047) / 2048, most = 0;
    disc_only = 1;
    for (uint32_t k = 0; k < n; k++) {
        if (!cd_read_sector_raw(disc_files[fi].lba + k, s)) continue;
        if (rd32(s + 24) == 0x80010160u && rd32(s + 32) > most) most = rd32(s + 32);
    }
    disc_only = 0;
    return (int)most;
}

/* The video's picture for frame (from 1) of stream fi, or NULL. */
static const uint32_t *video_picture(int fi, uint32_t frame, int *w, int *h) {
    Vid *v = NULL;
    for (int i = 0; i < nvids; i++)
        if (vids[i].fi == fi) v = &vids[i];
    if (!v || v->bad) return NULL;
    if (!v->v) {
        const char *why = "";
        if (!(v->v = modvideo_open(v->path, &why))) {
            note("mods: %s: %s: not shown", v->what, why);
            v->bad = 1;
            return NULL;
        }
        v->frames = modvideo_frames(v->v);
        v->str_frames = str_frames(fi);
        note("mods: %s: %d frames over the movie's %d", v->what, v->frames, v->str_frames);
        if (v->frames < 1 || v->str_frames < 1) { v->bad = 1; return NULL; }
    }
    int n = v->str_frames > 1 ? (int)(((int64_t)(frame - 1) * (v->frames - 1) + (v->str_frames - 1) / 2) / (v->str_frames - 1)) : 0;
    return modvideo_frame(v->v, n < v->frames ? n : v->frames - 1, w, h);
}

/* M/OPENING.STR/0042.png: frame 42 of that stream. */
static int movie_apply(const char *mod, const char *rel, const char *up, const char *path) {
    if (video_apply(mod, rel, up, path)) return 1;
    const char *sl = strrchr(up, '/'), *dot = strrchr(up, '.');
    if (!sl || !dot || dot < sl) return 0;
    if (strcasecmp(dot, ".PNG") && strcasecmp(dot, ".JPG") && strcasecmp(dot, ".JPEG") && strcasecmp(dot, ".BMP") &&
        strcasecmp(dot, ".TGA"))
        return 0;
    char stream[512];
    snprintf(stream, sizeof stream, "%.*s", (int)(sl - up), up);
    int fi = disc_index(stream);
    if (fi < 0 || !strstr(stream, ".STR")) return 0;
    char *end;
    long frame = strtol(sl + 1, &end, 10);
    if (end == sl + 1 || end != dot || frame < 1) {
        note("mods: %s/%s: name it by the frame's number (0001.png, 0002.png...): not used", mod, rel);
        return 1;
    }
    if (npics == cpics) {
        cpics = cpics ? cpics * 2 : 1024;
        pics = realloc(pics, (size_t)cpics * sizeof *pics);
    }
    pics[npics] = (Pic){fi, (uint32_t)frame, (uint32_t)npics, strdup(path)};
    npics++;
    nused++;
    return 1;
}

/* Once every mod is in: sorted, a later mod's picture in place of an
 * earlier one's, and a line in the log for each stream. */
static void movie_done(void) {
    if (!npics) return;
    qsort(pics, (size_t)npics, sizeof *pics, cmp_pic);
    int n = 0;
    for (int i = 0; i < npics; i++) {
        if (n && pics[n - 1].fi == pics[i].fi && pics[n - 1].frame == pics[i].frame) {
            free(pics[n - 1].path);
            nused--;
            pics[n - 1] = pics[i];
        } else
            pics[n++] = pics[i];
    }
    npics = n;
    for (int i = 0; i < npics;) {
        int j = i;
        while (j < npics && pics[j].fi == pics[i].fi) j++;
        note("mods: %s: pictures for %d frames (%u to %u)", disc_files[pics[i].fi].name, j - i, pics[i].frame,
               pics[j - 1].frame);
        i = j;
    }
}

static const Pic *pic_find(int fi, uint32_t frame) {
    int lo = 0, hi = npics - 1;
    while (lo <= hi) {
        int m = (lo + hi) / 2;
        const Pic *p = &pics[m];
        if (p->fi == fi && p->frame == frame) return p;
        if (p->fi < fi || (p->fi == fi && p->frame < frame)) lo = m + 1;
        else hi = m - 1;
    }
    return NULL;
}

/* The stream a sector belongs to (-1: none). */
static int stream_at(uint32_t lba) {
    static int strs[64], nstrs = -1;
    if (nstrs < 0) {
        nstrs = 0;
        for (size_t i = 0; i < sizeof disc_files / sizeof disc_files[0] && nstrs < 64; i++)
            if (strstr(disc_files[i].name, ".STR")) strs[nstrs++] = (int)i;
    }
    for (int i = 0; i < nstrs; i++)
        if (lba - disc_files[strs[i]].lba < (disc_files[strs[i]].size + 2047) / 2048) return strs[i];
    return -1;
}

typedef struct { int fi; uint32_t frame; } Shown;
static Shown readq[8];                           /* frames read whole, not decoded yet */
static int nreadq;
static Shown decoding = {-1, 0};                 /* the picture the decoder made last */
static uint32_t decoded_at;
static int placed;                               /* its first slice is in VRAM */
static struct { int x, y; Shown s; uint32_t seq; } bufs[4];   /* where pictures went */
static uint32_t nplaced;
static Shown now = {-1, 0};                      /* what the screen shows */

/* cdrom.c: a data sector the game is given. */
void modfs_data(uint32_t lba, const uint8_t *s) {
    int fi = stream_at(lba);
    if (fi < 0) { nreadq = 0; return; }
    const uint8_t *d = s + 24;
    if (rd32(d) != 0x80010160u || rd16(d + 4) + 1 != rd16(d + 6)) return;   /* not a frame's last sector */
    if (nreadq && readq[nreadq - 1].fi != fi) nreadq = 0;
    if (nreadq == 8) memmove(readq, readq + 1, sizeof readq - sizeof readq[0]), nreadq--;
    readq[nreadq++] = (Shown){fi, rd32(d + 8)};
}

/* mdec.c: a picture starts decoding. */
void modfs_mdec(void) {
    if (nreadq) {
        decoding = readq[0];
        memmove(readq, readq + 1, (size_t)--nreadq * sizeof readq[0]);
    } else
        decoding.fi = -1;
    decoded_at = g_frame;
    placed = 0;
}

/* gpu.c: pixels written to VRAM from the CPU, at x,y. */
void modfs_vram_write(int x, int y) {
    if (placed || decoding.fi < 0 || g_frame - decoded_at > 4) return;
    placed = 1;
    int k = 0;
    for (int i = 0; i < 4; i++) {
        if (bufs[i].x == x && bufs[i].y == y && bufs[i].seq) { k = i; break; }
        if (bufs[i].seq < bufs[k].seq) k = i;
    }
    bufs[k].x = x;
    bufs[k].y = y;
    bufs[k].s = decoding;
    bufs[k].seq = ++nplaced;
}

/* An RGB picture as a PNG file, not compressed (the deflate it needs
 * isn't built in; any program reads it, and saves it smaller). */
static void png_chunk(FILE *f, const char *type, const uint8_t *d, uint32_t n) {
    uint8_t b[8] = {(uint8_t)(n >> 24), (uint8_t)(n >> 16), (uint8_t)(n >> 8), (uint8_t)n};
    memcpy(b + 4, type, 4);
    fwrite(b, 1, 8, f);
    if (n) fwrite(d, 1, n, f);
    uint32_t crc = (uint32_t)mz_crc32(mz_crc32(0, b + 4, 4), d, n);
    uint8_t c[4] = {(uint8_t)(crc >> 24), (uint8_t)(crc >> 16), (uint8_t)(crc >> 8), (uint8_t)crc};
    fwrite(c, 1, 4, f);
}

static void png_write(const char *path, const uint8_t *rgb, int w, int h) {
    size_t row = (size_t)w * 3 + 1, raw = row * h, blocks = (raw + 65534) / 65535;
    uint8_t *z = malloc(2 + raw + blocks * 5 + 4), *o = z;
    FILE *f = z ? fopen(path, "wb") : NULL;
    if (!f) { free(z); return; }
    *o++ = 0x78;                                 /* zlib: deflate, stored blocks */
    *o++ = 0x01;
    uint32_t a = 1, b = 0;
    for (size_t i = 0; i < raw;) {
        size_t n = raw - i < 65535 ? raw - i : 65535;
        *o++ = i + n == raw;
        *o++ = (uint8_t)n; *o++ = (uint8_t)(n >> 8); *o++ = (uint8_t)~n; *o++ = (uint8_t)(~n >> 8);
        for (size_t k = 0; k < n; k++, i++) {
            uint8_t v = i % row ? rgb[(i / row) * w * 3 + i % row - 1] : 0;
            *o++ = v;
            a = (a + v) % 65521;
            b = (b + a) % 65521;
        }
    }
    uint32_t ad = b << 16 | a;
    *o++ = (uint8_t)(ad >> 24); *o++ = (uint8_t)(ad >> 16); *o++ = (uint8_t)(ad >> 8); *o++ = (uint8_t)ad;
    uint8_t ihdr[13] = {(uint8_t)(w >> 24), (uint8_t)(w >> 16), (uint8_t)(w >> 8), (uint8_t)w,
                        (uint8_t)(h >> 24), (uint8_t)(h >> 16), (uint8_t)(h >> 8), (uint8_t)h, 8, 2, 0, 0, 0};
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    png_chunk(f, "IHDR", ihdr, 13);
    png_chunk(f, "IDAT", z, (uint32_t)(o - z));
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(z);
}

/* Once a frame: with enemy models a mod made bigger, a line in the log if
 * a room's enemies fill the memory they're loaded into (0x80150000 up,
 * below the stacks from 0x801F0000) nearly to its end. */
static int models_grown;
static void memory_watch(void) {
    static int told;
    if (!models_grown || told) return;
    uint32_t top = rd32(ram_ptr(0x800B4D78));
    if (top > 0x801E8000u && top < 0x801F0000u) {
        told = 1;
        note("mods: this room's enemies take the memory they load into nearly to its end (up to %08X): bigger models than these are not safe", top);
    } else if (top >= 0x801F0000u && top < 0x80200000u) {
        told = 1;
        note("mods: this room's enemies don't fit the memory they load into (up to %08X): the game may break; make the bigger models lighter", top);
    }
}

/* display.c, once a frame: which frame the screen shows; written out
 * when dumping (g_movie_dump/M/OPENING.STR/0042.png, the picture as the
 * PS1 shows it). */
void modfs_movie_vblank(void) {
    memory_watch();
    now.fi = -1;
    if (g_frame - g_mdec_frame >= 8) return;     /* no movie playing */
    GpuDisplay d;
    gpu_display(&d);
    int dw = d.rgb24 ? d.w * 3 / 2 : d.w;
    uint32_t best = 0;
    for (int i = 0; i < 4; i++)
        if (bufs[i].seq > best && bufs[i].x - d.x >= 0 && bufs[i].x - d.x < dw && bufs[i].y - d.y >= 0 &&
            bufs[i].y - d.y < d.h) {
            best = bufs[i].seq;
            now = bufs[i].s;
        }
    static Shown last = {-1, 0};
    if (!g_movie_dump[0] || now.fi < 0 || (now.fi == last.fi && now.frame == last.frame)) return;
    last = now;
    if (d.w > 640 || d.h > 512) return;
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", g_movie_dump, disc_files[now.fi].name);
    for (char *c = path + 1; *c; c++)
        if (*c == '/') { *c = 0; mkdir(path); *c = '/'; }
    mkdir(path);
    snprintf(path + strlen(path), sizeof path - strlen(path), "/%04u.png", now.frame);
    static uint8_t rgb[640 * 512 * 3];
    int w, h;
    display_grab_rgb(rgb, &w, &h);
    png_write(path, rgb, w, h);
}

/* display.c: a mod's picture for what the screen shows now (BGRX, its
 * size), or NULL. The last one stays decoded. */
const uint32_t *modfs_movie_picture(int *w, int *h) {
    if (now.fi < 0 || g_frame - g_mdec_frame >= 8) return NULL;
    const Pic *p = pic_find(now.fi, now.frame);
    if (!p) return video_picture(now.fi, now.frame, w, h);
    static const Pic *have;
    static uint32_t *px;
    static int pw, ph;
    if (p != have) {
        have = p;
        free(px);
        px = NULL;
        long n = 0;
        uint8_t *d = load(p->path, &n);
        int c;
        uint8_t *img = d ? stbi_load_from_memory(d, (int)n, &pw, &ph, &c, 4) : NULL;
        free(d);
        if (!img || pw > 4096 || ph > 4096) {
            note("mods: %s: %s: not shown", p->path, img ? "bigger than 4096x4096" : "couldn't read it as a picture");
            stbi_image_free(img);
            return NULL;
        }
        px = malloc((size_t)pw * ph * 4);
        for (size_t i = 0; i < (size_t)pw * ph; i++)
            px[i] = img[i * 4 + 2] | (uint32_t)img[i * 4 + 1] << 8 | (uint32_t)img[i * 4] << 16;
        stbi_image_free(img);
    }
    if (!px) return NULL;
    *w = pw;
    *h = ph;
    return px;
}
