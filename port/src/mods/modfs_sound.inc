/* modfs.c, part: Sound: streams (voices, music, movies' sound) and sound effects from WAVs. Included by modfs.c, in this order. */

/* ------------------------------------------------------------ sound */

/* A stream's channel played from a mod's WAV: each of the channel's
 * sectors on the disc numbered in order (index), the WAV in stereo at the
 * channel's rate. */
typedef struct {
    uint32_t lba, nsec, chan, rate, per;          /* per: frames a sector holds */
    int32_t *index;                               /* the sector's place in the channel, -1 another's */
    int16_t *pcm;                                 /* stereo */
    uint32_t frames;
} Track;
static Track tracks[128];
static int ntracks;


/* PCM WAV, 16 bits, mono or stereo, to stereo at rate (linear). */
static int16_t *wav_load(const uint8_t *d, long n, uint32_t rate, uint32_t *frames, const char **why) {
    if (n < 44 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) { *why = "not a WAV file"; return NULL; }
    const uint8_t *fmt = NULL, *data = NULL;
    uint32_t dlen = 0;
    for (size_t o = 12; o + 8 <= (size_t)n;) {
        uint32_t len = rd32(d + o + 4);
        if (len > (size_t)n - o - 8) { *why = "truncated WAV chunk"; return NULL; }
        if (!memcmp(d + o, "fmt ", 4)) {
            if (len < 16) { *why = "truncated WAV format"; return NULL; }
            fmt = d + o + 8;
        }
        if (!memcmp(d + o, "data", 4)) { data = d + o + 8; dlen = len; }
        o += 8 + (size_t)len + (len & 1);
    }
    if (!fmt || !data) { *why = "no format or no samples"; return NULL; }
    uint32_t chans = rd16(fmt + 2), srate = rd32(fmt + 4), bits = rd16(fmt + 14);
    if (rd16(fmt) != 1 || bits != 16 || chans < 1 || chans > 2 || !srate) { *why = "not 16-bit PCM, mono or stereo"; return NULL; }
    if (!dlen || dlen % (2 * chans) || !rate) { *why = "no complete PCM frames or invalid target rate"; return NULL; }
    uint32_t in = dlen / (2 * chans);
    uint64_t count = (uint64_t)in * rate / srate;
    if (!count || count > UINT32_MAX || count > SIZE_MAX / 4) { *why = "resampled WAV size out of range"; return NULL; }
    uint32_t out = (uint32_t)count;
    int16_t *pcm = malloc((size_t)out * 4);
    if (!pcm) { *why = "out of memory"; return NULL; }
    for (uint32_t i = 0; i < out; i++) {
        uint64_t pos = (uint64_t)i * srate;
        uint32_t a = (uint32_t)(pos / rate), f = (uint32_t)((pos % rate) * 65536 / rate), b = a + 1 < in ? a + 1 : a;
        for (int c = 0; c < 2; c++) {
            uint32_t cc = chans == 2 ? (uint32_t)c : 0;
            int32_t va = (int16_t)rd16(data + (a * chans + cc) * 2), vb = (int16_t)rd16(data + (b * chans + cc) * 2);
            pcm[2 * i + c] = (int16_t)(va + (((vb - va) * (int32_t)f) >> 16));
        }
    }
    *frames = out;
    return pcm;
}

/* X/BGM_32.STR/05.wav: channel 5 of that stream; wav/Scene05_1.wav (the
 * PC's name): the channel it is. */
static int sound_apply(const char *mod, const char *rel, const char *up, const char *path) {
    const char *sl = strrchr(up, '/');
    if (!sl || strcasecmp(up + strlen(up) - 4, ".WAV")) return 0;
    char stream[512];
    long chan = -1;
    for (size_t i = 0; i < sizeof modfs_wav / sizeof modfs_wav[0]; i++)
        if (!strcmp(modfs_wav[i].pc, up)) {
            snprintf(stream, sizeof stream, "%s", modfs_wav[i].stream);
            chan = modfs_wav[i].chan;
        }
    if (chan < 0) snprintf(stream, sizeof stream, "%.*s", (int)(sl - up), up);
    int fi = disc_index(stream);
    if (fi < 0 || !strstr(stream, ".STR")) return 0;
    char *end;
    if (chan < 0 && ((chan = strtol(sl + 1, &end, 10)), end == sl + 1 || strcasecmp(end, ".WAV") || chan < 0 || chan > 31)) {
        note("mods: %s/%s: name it by the channel's number (00.wav to 31.wav): not used", mod, rel);
        return 1;
    }
    /* the channel's sectors, in order, and how they're coded (each stream read once: which channel
     * each of its sectors is, 0xFF not sound, and its coding byte) */
    static struct { int fi; uint8_t *chan, *coding; } scans[16];
    static int nscans;
    uint32_t lba = disc_files[fi].lba, nsec = (disc_files[fi].size + 2047) / 2048, count = 0, rate = 0, per = 0;
    int si = 0;
    while (si < nscans && scans[si].fi != fi) si++;
    if (si == nscans) {
        if (nscans == 16) { free(scans[0].chan); free(scans[0].coding); memmove(scans, scans + 1, sizeof scans[0] * 15); nscans--; si = nscans; }
        scans[si].fi = fi;
        scans[si].chan = malloc(nsec);
        scans[si].coding = malloc(nsec);
        uint8_t sec[2352];
        disc_only = 1;
        for (uint32_t k = 0; scans[si].chan && scans[si].coding && k < nsec; k++) {
            scans[si].chan[k] = 0xFF;
            if (!cd_read_sector_raw(lba + k, sec) || sec[15] != 2 || !(sec[18] & 0x04) || sec[17] >= 32) continue;
            scans[si].chan[k] = sec[17];
            scans[si].coding[k] = sec[19];
        }
        disc_only = 0;
        nscans++;
    }
    int32_t *index = malloc(nsec * sizeof *index);
    for (uint32_t k = 0; index && scans[si].chan && k < nsec; k++) {
        index[k] = -1;
        if (scans[si].chan[k] != chan) continue;
        index[k] = (int32_t)count++;
        if (!rate) {
            uint8_t c = scans[si].coding[k];
            rate = (c >> 2) & 1 ? 18900 : 37800;
            per = (((c >> 4) & 3) == 1 ? 4 : 8) * 28 * 18 / ((c & 3) == 1 ? 2 : 1);
        }
    }
    if (!count) {
        note("mods: %s/%s: %s has no sound on channel %ld: not used", mod, rel, stream, chan);
        free(index);
        return 1;
    }
    long n = 0;
    uint8_t *d = load(path, &n);
    const char *why = "couldn't read it";
    uint32_t frames = 0;
    int16_t *pcm = d ? wav_load(d, n, rate, &frames, &why) : NULL;
    free(d);
    if (!pcm || ntracks == (int)(sizeof tracks / sizeof tracks[0])) {
        note("mods: %s/%s: %s: not used", mod, rel, pcm ? "too many sounds" : why);
        free(pcm);
        free(index);
        return 1;
    }
    for (int i = 0; i < ntracks; i++)             /* a later mod wins */
        if (tracks[i].lba == lba && tracks[i].chan == (uint32_t)chan) {
            free(tracks[i].pcm);
            free(tracks[i].index);
            tracks[i] = tracks[--ntracks];
            break;
        }
    Track *t = &tracks[ntracks++];
    *t = (Track){lba, nsec, (uint32_t)chan, rate, per, index, pcm, frames};
    double secs = (double)count * per / rate;
    note("mods: %s/%s: in place of %s channel %ld (%.1f s; the WAV %.1f s%s)", mod, rel, stream, chan, secs,
           (double)frames / rate, frames < count * per ? ", silence after it" : frames > count * per ? ", cut to fit" : "");
    nused++;
    return 1;
}

/* cdrom.c: an XA sound sector the game is playing: a mod's in its place (1). */
int modfs_xa(uint32_t lba, const uint8_t *s) {
    for (int i = 0; i < ntracks; i++) {
        Track *t = &tracks[i];
        if (lba - t->lba >= t->nsec || s[17] != t->chan) continue;
        int32_t k = t->index[lba - t->lba];
        if (k < 0) return 0;
        static int16_t buf[4032 * 2];
        uint64_t at = (uint64_t)k * t->per;
        for (uint32_t f = 0; f < t->per && f < 4032; f++) {
            uint64_t j = at + f;
            buf[2 * f] = j < t->frames ? t->pcm[2 * j] : 0;
            buf[2 * f + 1] = j < t->frames ? t->pcm[2 * j + 1] : 0;
        }
        void spu_xa_pcm(const int16_t *lr, int frames, int rate);
        spu_xa_pcm(buf, (int)t->per, (int)t->rate);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------ sound effects */

/* The sound effects (and the instruments the music plays) are the sound
 * banks in NOISE.BIN. Its blocks (KERNEL.BIN's table at 0x80058D2C: the
 * first sector of each, then the end) are each [n, n offsets]: a bank's
 * attributes (VAB header) at the first, its waveforms (SPU ADPCM, one
 * after another, their sizes in the header) at the last, a room's music
 * (sequences) in between. NOISE/012/03.wav is waveform 3 of block 12,
 * and of every other bank that has the very same one (many share their
 * sounds), at the rate its tones play it at note 60 (centre note C, fine
 * tune s: 44100 * 2^((60 - C + s/128) / 12)). A bank goes whole into the
 * sound chip's memory (512 KB, shared with the other banks open), so it
 * can't grow: a sound can be longer than the original by what the mods'
 * other sounds for that bank leave free. It ends, or repeats, as the
 * original did (from the WAV's own loop start if it has one, a smpl
 * chunk; else from the original's). */
#define NOISE_TABLE 0x80058D2Cu

typedef struct { uint32_t block, vag; uint8_t *wav; long n; int places, cut, bad, rank; double secs; uint32_t rate; char what[600];
                 int16_t *whole; uint32_t whole_frames, whole_rate; } Sfx;   /* whole: the WAV, mono at its own rate (spu.c plays it) */   /* cut: in how many banks */
static Sfx *sfxs;
static int nsfx;

/* A bank in NOISE.BIN: its block (at, n bytes), header and body (offsets in the block), programs, waveforms, their size table. */
typedef struct { uint32_t at, n, vh, body, ps, vs, tab; } Bank;

static int bank_at(const uint8_t *d, uint32_t at, uint32_t n, Bank *b) {
    if (n < 16) return 0;
    uint32_t c = rd32(d + at);
    if (c < 2 || c > 8 || 4 + 4 * c > n) return 0;
    uint32_t vh = rd32(d + at + 4), body = rd32(d + at + 4 * c);
    if (vh != 4 + 4 * c || vh + 32 > n || memcmp(d + at + vh, "pBAV", 4) || body > n) return 0;
    uint32_t ps = rd16(d + at + vh + 18), vs = rd16(d + at + vh + 22), tab = vh + 32 + 2048 + 512 * ps;
    if (ps > 128 || vs > 255 || tab + 512 > body) return 0;
    uint64_t sum = 0;
    for (uint32_t i = 1; i <= vs; i++) sum += rd16(d + at + tab + 2 * i) * 8u;
    if (body + sum > n) return 0;
    *b = (Bank){at, n, vh, body, ps, vs, tab};
    return 1;
}

static uint32_t vag_size(const uint8_t *d, const Bank *b, uint32_t v) { return rd16(d + b->at + b->tab + 2 * v) * 8u; }
static uint32_t vag_off(const uint8_t *d, const Bank *b, uint32_t v) {   /* in NOISE.BIN */
    uint32_t o = b->at + b->body;
    for (uint32_t i = 1; i < v; i++) o += vag_size(d, b, i);
    return o;
}

/* The rate waveform v plays at, by the first tone that uses it. */
static uint32_t vag_rate(const uint8_t *d, const Bank *b, uint32_t v) {
    const uint8_t *tone = d + b->at + b->vh + 32 + 2048;
    for (uint32_t k = 0; k < 16 * b->ps; k++, tone += 32)
        if (rd16(tone + 22) == v) return (uint32_t)(44100.0 * pow(2.0, (60.0 - tone[4] + tone[5] / 128.0) / 12.0) + 0.5);
    return 22050;
}

/* A waveform's blocks (16 bytes, 28 samples each): a silent one before
 * the sound (lead), the sound (its last flagged "end", 1; "repeat", 2, if
 * it loops, its loop start flagged 4, from), and what's after (tail: the
 * silent block a sound that doesn't loop ends in). */
typedef struct { uint32_t lead, data, tail, loop, from; } Shape;
static Shape vag_shape(const uint8_t *v, uint32_t nb) {
    static const uint8_t zero[16];
    Shape s = {0, 0, 0, 0, 0};
    s.lead = nb > 1 && !memcmp(v, zero, 16);
    uint32_t j = s.lead;
    while (j + 1 < nb && !(v[16 * j + 1] & 1)) j++;
    s.data = j + 1 - s.lead;
    s.tail = nb - 1 - j;
    s.loop = !s.tail && (v[16 * j + 1] & 2);
    for (uint32_t k = s.lead; k <= j; k++)
        if (v[16 * k + 1] & 4) { s.from = k - s.lead; break; }
    return s;
}

static int16_t *vag_decode(const uint8_t *v, uint32_t nb) {   /* every block's 28 samples */
    static const int pos[5] = {0, 60, 115, 98, 122}, neg[5] = {0, 0, -52, -55, -60};
    int16_t *o = malloc((size_t)nb * 28 * 2 + 2);
    int32_t h0 = 0, h1 = 0;
    for (uint32_t k = 0; o && k < nb; k++) {
        const uint8_t *b = v + 16 * k;
        int shift = b[0] & 15, f = (b[0] >> 4) & 7;
        if (f > 4) f = 0;
        if (shift > 12) shift = 9;
        for (int i = 0; i < 28; i++) {
            int n = (b[2 + i / 2] >> ((i & 1) * 4)) & 15;
            int32_t s = (((n & 8 ? n - 16 : n) * 4096) >> shift) + ((h0 * pos[f] + h1 * neg[f] + 32) >> 6);
            s = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
            h1 = h0;
            h0 = s;
            o[28 * k + i] = (int16_t)s;
        }
    }
    return o;
}

/* 28 samples as one block (flags left 0): of the five filters and the
 * steps that fit, the one closest to them as the SPU decodes it; h: the
 * last two samples decoded, moved on. */
static void adpcm_block(const int16_t *x, int32_t h[2], uint8_t out[16]) {
    static const int pos[5] = {0, 60, 115, 98, 122}, neg[5] = {0, 0, -52, -55, -60};
    int64_t best = -1;
    int32_t b0 = h[0], b1 = h[1];
    for (int f = 0; f < 5; f++) {
        int32_t p0 = h[0], p1 = h[1], peak = 0;
        for (int i = 0; i < 28; i++) {
            int32_t r = x[i] - ((p0 * pos[f] + p1 * neg[f] + 32) >> 6);
            if (r < 0) r = -r;
            if (r > peak) peak = r;
            p1 = p0;
            p0 = x[i];
        }
        int s0 = 0;
        while (s0 < 12 && peak > (7 << s0)) s0++;
        for (int s = s0; s <= s0 + 1 && s <= 12; s++) {
            uint8_t t[16] = {(uint8_t)(f << 4 | (12 - s)), 0};
            int32_t g0 = h[0], g1 = h[1];
            int64_t err = 0;
            for (int i = 0; i < 28; i++) {
                int32_t pr = (g0 * pos[f] + g1 * neg[f] + 32) >> 6, q = (x[i] - pr + ((1 << s) >> 1)) >> s;
                q = q < -8 ? -8 : q > 7 ? 7 : q;
                int32_t v = q * (1 << s) + pr;
                v = v < -32768 ? -32768 : v > 32767 ? 32767 : v;
                err += (int64_t)(x[i] - v) * (x[i] - v);
                g1 = g0;
                g0 = v;
                t[2 + i / 2] |= (uint8_t)((q & 15) << ((i & 1) * 4));
            }
            if (best < 0 || err < best) { best = err; memcpy(out, t, 16); b0 = g0; b1 = g1; }
        }
    }
    h[0] = b0;
    h[1] = b1;
}

/* A WAV's rate and its first loop's start (a smpl chunk), -1 none. */
static int64_t wav_loop(const uint8_t *d, long n, uint32_t *rate) {
    int64_t from = -1;
    *rate = 0;
    if (n < 12 || memcmp(d, "RIFF", 4) || memcmp(d + 8, "WAVE", 4)) return -1;
    for (size_t o = 12; o + 8 <= (size_t)n;) {
        uint32_t len = rd32(d + o + 4);
        if (len > (size_t)n - o - 8 || (!memcmp(d + o, "fmt ", 4) && len < 16)) { *rate = 0; return -1; }
        if (!memcmp(d + o, "fmt ", 4)) *rate = rd32(d + o + 12);
        if (!memcmp(d + o, "smpl", 4) && len >= 60 && rd32(d + o + 8 + 28)) from = rd32(d + o + 8 + 44);
        o += 8 + (size_t)len + (len & 1);
    }
    return from;
}

static uint32_t noise_banks(Touched *t, uint32_t *v, Bank **bk);
static int music_apply(const char *mod, const char *rel, unsigned blk, unsigned part, const char *path, int alt);

/* NOISE/012/music1.seq: a room's sound set's first sequence (its music),
 * as the disc has it (pQES), in its place; bigger, its block laid out
 * again (grow_all). */
static int seq_apply(const char *mod, const char *rel, unsigned blk, unsigned part, const char *path) {
    int fi = disc_index("NOISE.BIN");
    Touched *t = touch(fi);
    static uint32_t v[1024];
    Bank *bk = NULL;
    uint32_t nb = noise_banks(t, v, &bk), c = 0, at = 0, len = 0;
    if (blk < nb && bk[blk].vs) {
        at = v[blk] * 2048;
        c = rd32(t->data + at);
    }
    free(bk);
    if (part < 1 || part + 2 > c) {
        note("mods: %s/%s: NOISE.BIN's block %u has no music %u: not used", mod, rel, blk, part);
        return 1;
    }
    uint32_t off = rd32(t->data + at + 4 + 4 * part);
    len = rd32(t->data + at + 8 + 4 * part) - off;
    long n = 0;
    uint8_t *d = load(path, &n);
    if (!d || n < 16 || memcmp(d, "pQES", 4)) {
        note("mods: %s/%s: %s: not used", mod, rel, d ? "not a sequence (pQES)" : "couldn't read it");
        free(d);
        return 1;
    }
    if ((uint32_t)n <= len) {
        memset(t->data + at + off, 0, len);
        memcpy(t->data + at + off, d, (size_t)n);
        free(d);
        note("mods: %s/%s: into NOISE.BIN block %u (%ld bytes)", mod, rel, blk, n);
        nused++;
        return 1;
    }
    if (ngrows == (int)(sizeof grows / sizeof grows[0])) {
        note("mods: %s/%s: too many files growing: not used", mod, rel);
        free(d);
        return 1;
    }
    Grow *g = &grows[ngrows++];
    *g = (Grow){fi, at + off, (uint32_t)n, len, 0, d, ""};
    snprintf(g->what, sizeof g->what, "%s/%s", mod, rel);
    nused++;
    return 1;
}

/* NOISE/012/03.wav: waveform 3 of NOISE.BIN's block 12 (done once every mod is in). */
static int sfx_apply(const char *mod, const char *rel, const char *up, const char *path) {
    char alias[64];
    int sets = 0, was = nused;
    for (size_t i = 0; i < sizeof modfs_pc_bgm / sizeof modfs_pc_bgm[0]; i++)   /* the PC's music by its name (modfs_pc_bgm.h), */
        if (!strcmp(up, modfs_pc_bgm[i].pc)) {                                  /* in each set that has that sequence */
            music_apply(mod, rel, (unsigned)modfs_pc_bgm[i].block, (unsigned)modfs_pc_bgm[i].music, path, modfs_pc_bgm[i].alt);
            sets++;
        }
    if (sets) {                                  /* one file: it counts once, and one it took the place of (all its */
        nused = was + (nused - was == sets);     /* sets: the same name, or one of the same sequence) no more */
        return 1;
    }
    int rank = 0;
    for (size_t i = 0; i < sizeof modfs_pc_sfx / sizeof modfs_pc_sfx[0]; i++)   /* the PS1's own recordings */
        if (!strcmp(up, modfs_pc_sfx[i].pc)) {
            snprintf(alias, sizeof alias, "NOISE/%u/%u.WAV", modfs_pc_sfx[i].block, modfs_pc_sfx[i].vag);
            up = alias;
            break;
        }
    for (size_t i = 0; up != alias && i < sizeof modfs_pc_sfx_events / sizeof modfs_pc_sfx_events[0]; i++)   /* by their event */
        if (!strcmp(up, modfs_pc_sfx_events[i].pc)) {
            snprintf(alias, sizeof alias, "NOISE/%u/%u.WAV", modfs_pc_sfx_events[i].block, modfs_pc_sfx_events[i].vag);
            up = alias;
            rank = (int)modfs_pc_sfx_events[i].rank;
            break;
        }
    if (strncmp(up, "NOISE/", 6)) return 0;
    unsigned blk, vag;
    char rest[8] = "";
    long n = 0;
    if (sscanf(up + 6, "%u/MUSIC%u%7s", &blk, &vag, rest) == 3 && !strcmp(rest, ".SEQ")) return seq_apply(mod, rel, blk, vag, path);
    if (sscanf(up + 6, "%u/MUSIC%u%7s", &blk, &vag, rest) == 3 && !strcmp(rest, ".WAV")) return music_apply(mod, rel, blk, vag, path, 0);
    if (sscanf(up + 6, "%u/%u%7s", &blk, &vag, rest) != 3 || strcmp(rest, ".WAV")) {
        note("mods: %s/%s: name it by the bank's block and the sound's number (NOISE/012/03.wav), or a room's music (NOISE/240/music1.seq or .wav): not used", mod, rel);
        return 1;
    }
    uint8_t *d = load(path, &n);
    Sfx *s = d ? realloc(sfxs, (size_t)(nsfx + 1) * sizeof *sfxs) : NULL;
    if (!s) { free(d); note("mods: %s/%s: couldn't read it", mod, rel); return 1; }
    sfxs = s;
    s = &sfxs[nsfx++];
    memset(s, 0, sizeof *s);
    s->block = blk;
    s->vag = vag;
    s->rank = rank;
    s->wav = d;
    s->n = n;
    snprintf(s->what, sizeof s->what, "%s/%s", mod, rel);
    nused++;
    return 1;
}

/* A waveform anew from sound s (mono, at its rate), in at most room
 * bytes, shaped as the original (onb blocks); the bytes written. */
static uint32_t sfx_encode(Sfx *s, const int16_t *pcm, uint32_t frames, uint32_t rate, const uint8_t *orig, uint32_t onb, uint32_t room, uint8_t *out) {
    Shape sh = vag_shape(orig, onb);
    uint32_t want = frames ? (frames + 27) / 28 : 1, cap = room / 16 > sh.lead + sh.tail ? room / 16 - sh.lead - sh.tail : 1;
    if (want > cap) { want = cap; s->cut++; }
    uint32_t rs;
    int64_t from = wav_loop(s->wav, s->n, &rs);
    uint32_t lf = from >= 0 && rs ? (uint32_t)((uint64_t)from * rate / rs / 28) : sh.from;
    if (lf >= want) lf = 0;
    uint8_t *o = out;
    if (sh.lead) { memset(o, 0, 16); o += 16; }
    int32_t h[2] = {0, 0};
    int16_t x[28];
    for (uint32_t k = 0; k < want; k++, o += 16) {
        for (uint32_t i = 0; i < 28; i++) x[i] = 28 * k + i < frames ? pcm[28 * k + i] : 0;
        adpcm_block(x, h, o);
        o[1] = sh.loop ? 2 : 0;
        if (k == (sh.loop ? lf : 0)) o[1] |= 4;
        if (k == want - 1) o[1] |= 1;
    }
    memcpy(o, orig + 16 * (onb - sh.tail), 16 * sh.tail);
    o += 16 * sh.tail;
    double secs = (double)(want * 28 < frames ? want * 28 : frames) / rate;
    if (secs > s->secs) s->secs = secs;
    if (!s->rate) s->rate = rate;
    return (uint32_t)(o - out);
}

/* The SPU plays sound s whole, at the WAV's own rate and length, where the
 * game starts the waveform just written (out, n bytes; its original's
 * shape orig, onb blocks, at rate; NOISE.BIN block block): spu.c finds it
 * by the CRC of its blocks up to the end flag. */
static void sfx_whole(Sfx *s, const uint8_t *out, uint32_t n, const uint8_t *orig, uint32_t onb, uint32_t rate, uint32_t block) {
    void spu_hd_add(uint32_t crc, uint32_t len, const int16_t *pcm, uint32_t frames, uint32_t wav_rate, uint32_t rate, int64_t loop, uint32_t lead,
                    int block);
    uint32_t rs;
    int64_t from = wav_loop(s->wav, s->n, &rs);
    if (!s->whole && rs) {
        const char *why = "";
        uint32_t fr = 0;
        int16_t *lr = wav_load(s->wav, s->n, rs, &fr, &why);
        if ((s->whole = lr ? malloc((size_t)fr * 2 + 2) : NULL)) {
            for (uint32_t k = 0; k < fr; k++) s->whole[k] = (int16_t)((lr[2 * k] + lr[2 * k + 1]) / 2);
            s->whole_frames = fr;
            s->whole_rate = rs;
        }
        free(lr);
    }
    if (!s->whole || !s->whole_frames) return;
    uint32_t len = 16;
    while (len < n && !(out[len - 16 + 1] & 1)) len += 16;
    Shape sh = vag_shape(orig, onb);
    int64_t loop = !sh.loop ? -1 : from >= 0 ? from : (int64_t)((uint64_t)sh.from * 28 * s->whole_rate / rate);
    spu_hd_add((uint32_t)mz_crc32(0, out, len), len, s->whole, s->whole_frames, s->whole_rate, rate, loop, 28 * sh.lead, -1);
    /* and the original's blocks in this bank, for a savestate made before the mod (spu.c) */
    uint32_t olen = 16;
    while (olen < 16 * onb && !(orig[olen - 16 + 1] & 1)) olen += 16;
    spu_hd_add((uint32_t)mz_crc32(0, orig, olen), olen, s->whole, s->whole_frames, s->whole_rate, rate, loop, 28 * sh.lead, (int)block);
}

/* NOISE.BIN's table of blocks (KERNEL.BIN's), and its banks: the number of blocks, 0 if not found. */
static uint32_t noise_banks(Touched *t, uint32_t *v, Bank **bk) {
    Touched *kern = touch(disc_index("KERNEL.BIN"));
    uint32_t nb = 0, ko = kern ? NOISE_TABLE - rd32(kern->data + 0x18) + 0x800 : 0;
    for (; t && kern && nb < 1023 && ko + 4 * nb + 4 <= kern->nsec * 2048; nb++) {
        v[nb] = rd32(kern->data + ko + 4 * nb);
        if (nb && v[nb] < v[nb - 1]) break;
        if (nb && v[nb] == t->nsec) break;
    }
    if (!t || !kern || !nb || v[0] || v[nb] != t->nsec || !(*bk = calloc(nb, sizeof **bk))) return 0;
    for (uint32_t b = 0; b < nb; b++)
        if (v[b + 1] == v[b] || !bank_at(t->data, v[b] * 2048, (v[b + 1] - v[b]) * 2048, &(*bk)[b])) (*bk)[b].vs = 0;
    return nb;
}

#include "modfs_pc_noise.inc"

/* The sounds into their banks. */
static void sfx_done(void) {
    if (!nsfx) return;
    Touched *t = touch(disc_index("NOISE.BIN"));
    static uint32_t v[1024];
    Bank *bk = NULL;
    uint32_t nb = noise_banks(t, v, &bk);
    uint32_t *base = nb ? calloc(nb + 1, sizeof *base) : NULL;
    for (uint32_t b = 0; base && b < nb; b++) base[b + 1] = base[b] + bk[b].vs + 1;
    int *who = base ? malloc(sizeof *who * (base[nb] + 1)) : NULL;
    if (!who) {
        for (int i = 0; i < nsfx; i++) { note("mods: %s: NOISE.BIN's table of banks wasn't found: not used", sfxs[i].what); nused--; }
        nb = 0;
    }
    uint8_t *d = t ? t->data : NULL;
    for (uint32_t k = 0; who && k <= base[nb]; k++) who[k] = -1;
    /* which sound each waveform gets: the copies of the one a mod names, then the one it names (a later mod
     * wins; inside a mod, of the PC files of one waveform the lowest ranked, modfs_pc_sfx_events.h: they go
     * highest rank first) */
    int *ord = malloc(sizeof *ord * (size_t)nsfx), no = 0;
    for (int i = 0, j; ord && i < nsfx; i = j) {
        size_t ml = strcspn(sfxs[i].what, "/");
        int top = 0;
        for (j = i; j < nsfx && strcspn(sfxs[j].what, "/") == ml && !strncmp(sfxs[j].what, sfxs[i].what, ml); j++)
            if (sfxs[j].rank > top) top = sfxs[j].rank;
        for (int r = top; r >= 0; r--)
            for (int k = i; k < j; k++)
                if (sfxs[k].rank == r) ord[no++] = k;
    }
    for (int pass = 0; who && ord && pass < 2; pass++)
        for (int oi = 0; oi < nsfx; oi++) {
            int i = ord[oi];
            Sfx *s = &sfxs[i];
            if (s->block >= nb || !bk[s->block].vs || s->vag < 1 || s->vag > bk[s->block].vs) {
                if (pass) { note("mods: %s: NOISE.BIN has no bank in block %u with a sound %u: not used", s->what, s->block, s->vag); nused--; }
                continue;
            }
            const uint8_t *was = d + vag_off(d, &bk[s->block], s->vag);
            uint32_t sz = vag_size(d, &bk[s->block], s->vag);
            if (pass) {
                if (s->places >= 0) who[base[s->block] + s->vag] = i;
                continue;
            }
            /* the very sound that's there (the originals written out, given back): nothing to do */
            uint32_t rate = vag_rate(d, &bk[s->block], s->vag), rs, frames = 0;
            wav_loop(s->wav, s->n, &rs);
            Shape sh = vag_shape(was, sz / 16);
            const char *why = "";
            int16_t *pcm = rs == rate ? wav_load(s->wav, s->n, rate, &frames, &why) : NULL, *dec = pcm ? vag_decode(was, sz / 16) : NULL;
            int same = dec && frames == sh.data * 28;
            for (uint32_t k = 0; same && k < frames; k++) same = pcm[2 * k] == dec[28 * sh.lead + k];
            free(pcm);
            free(dec);
            if (same) { s->places = -1; continue; }
            for (uint32_t b = 0; b < nb; b++)
                for (uint32_t j = 1; j <= bk[b].vs; j++)
                    if (vag_size(d, &bk[b], j) == sz && !memcmp(d + vag_off(d, &bk[b], j), was, sz)) who[base[b] + j] = i;
        }
    /* each bank with sounds to change: its waveforms again, in what its body was */
    for (uint32_t b = 0; who && b < nb; b++) {
        Bank *B = &bk[b];
        int any = 0;
        for (uint32_t j = 1; j <= B->vs; j++) any |= who[base[b] + j] >= 0;
        if (!any) continue;
        uint32_t total = 0, keep = 0, give[256] = {0}, want[256] = {0}, frames[256] = {0}, rate[256] = {0};
        int16_t *mono[256] = {0};
        for (uint32_t j = 1; j <= B->vs; j++) {
            uint32_t sz = vag_size(d, B, j);
            total += sz;
            int i = who[base[b] + j];
            if (i < 0) { keep += sz; continue; }
            Sfx *s = &sfxs[i];
            const char *why = "couldn't read it";
            rate[j] = vag_rate(d, B, j);
            int16_t *lr = wav_load(s->wav, s->n, rate[j], &frames[j], &why);
            if (!lr) {
                if (!s->bad) { note("mods: %s: %s: not used", s->what, why); nused--; }
                s->bad = 1;
                keep += sz;
                continue;
            }
            mono[j] = malloc((size_t)frames[j] * 2 + 2);
            for (uint32_t k = 0; mono[j] && k < frames[j]; k++) mono[j][k] = (int16_t)((lr[2 * k] + lr[2 * k + 1]) / 2);
            free(lr);
            if (!mono[j]) { keep += sz; continue; }
            Shape sh = vag_shape(d + vag_off(d, B, j), sz / 16);
            want[j] = 16 * (sh.lead + sh.tail + (frames[j] ? (frames[j] + 27) / 28 : 1));
            give[j] = want[j] < sz ? want[j] : sz;
        }
        /* what the shorter ones leave, to the longer ones, in order */
        uint32_t left = total - keep;
        for (uint32_t j = 1; j <= B->vs; j++) if (mono[j]) left -= give[j];
        for (uint32_t j = 1; j <= B->vs; j++)
            if (mono[j] && want[j] > give[j]) {
                uint32_t more = want[j] - give[j] < left ? want[j] - give[j] : left;
                give[j] += more;
                left -= more;
            }
        uint8_t *body = calloc(1, total + 16), *o = body;
        uint32_t at = B->at + B->body, now[256] = {0};
        for (uint32_t j = 1; body && j <= B->vs; j++) {
            uint32_t sz = vag_size(d, B, j);
            now[j] = sz;
            if (mono[j]) {
                Sfx *s = &sfxs[who[base[b] + j]];
                now[j] = sfx_encode(s, mono[j], frames[j], rate[j], d + at, sz / 16, give[j], o);
                sfx_whole(s, o, now[j], d + at, sz / 16, rate[j], b);
                s->places++;
            } else
                memcpy(o, d + at, sz);
            o += now[j];
            at += sz;
        }
        if (body) {
            memcpy(d + B->at + B->body, body, total);   /* (zeros after, if it's shorter) */
            for (uint32_t j = 1; j <= B->vs; j++) wr16(d + B->at + B->tab + 2 * j, (uint16_t)(now[j] / 8));
        }
        free(body);
        for (uint32_t j = 1; j <= B->vs; j++) free(mono[j]);
    }
    for (int i = 0; i < nsfx; i++) {
        Sfx *s = &sfxs[i];
        if (s->places > 0) {
            char cut[64] = "";
            if (s->cut) snprintf(cut, sizeof cut, "; %d of its banks keep less of it, RESPeCt plays it whole", s->cut);
            note("mods: %s: in place of NOISE.BIN's sound %u of block %u%s (%d bank%s; %.2f s at %u Hz%s)", s->what, s->vag, s->block,
                 s->places > 1 ? " and its copies" : "", s->places, s->places > 1 ? "s" : "", s->secs, s->rate, cut);
        } else if (s->places == 0 && !s->bad && who && s->block < nb && s->vag >= 1 && s->vag <= bk[s->block].vs) {
            int w = who[base[s->block] + s->vag];
            note("mods: %s: NOISE.BIN's sound %u of block %u is %s's (the same sound%s): not used", s->what, s->vag, s->block,
                 w >= 0 && w != i ? sfxs[w].what : "another file", s->rank ? "; the PS1 plays one sound where the PC has more" : "");
            nused--;
        }
        free(s->wav);
    }
    free(sfxs);
    sfxs = NULL;
    nsfx = 0;
    free(ord);
    free(who);
    free(bk);
    free(base);
}
