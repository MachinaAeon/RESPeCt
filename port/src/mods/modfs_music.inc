/* modfs.c, part: Music: a room's sequence, or a recording in its place. Included by modfs.c, in this order. */

/* ------------------------------------------------------------ music */

/* A room's music is a sequence (NOISE/240/music1.seq, played with the
 * room's sound bank) that the game starts, stops and fades (kernel_sound.c,
 * room_sound.c), and whose place in it times the music's events. With
 * NOISE/240/music1.wav a mod gives it as a recording instead, as the PC
 * version has its music: the sequence still plays, every note at velocity
 * 0 (silent: what the game times by is the same), and spu.c mixes the WAV
 * in while it plays, at its volume (fades, the options' music volume),
 * from the start when it starts again, repeating from the WAV's loop
 * start (a smpl chunk) or else from the beginning. Sound sets: block 230
 * is set 0 (room_sound.c: 0xE6 + the room's set); sequences 0 and 1 of the
 * set are music1 and music2. */
#define SND_SET_NOW   0x80062E7Cu                /* s16: the sound set loaded, -1 none */
#define SND_SEQ_IDS   0x80062E70u                /* s16 x 2: its sequences' ids */
#define SS_SCORE      0x80064678u                /* libsnd: per id, its sequence's state */

typedef struct { uint32_t block, part, frames, loop; int alt; int16_t *pcm; uint8_t *wav; long n; char what[600]; } Music;
static Music musics[64];
static int nmusics;

/* Every note-on of a sequence (after its 15-byte header) at velocity 0; 0 if it isn't one this reads to its end. */
static int seq_mute(uint8_t *d, uint32_t n) {
    uint32_t o = 15;
    uint8_t st = 0;
    if (n < 16 || memcmp(d, "pQES", 4)) return 0;
    while (o < n) {
        while (o < n && d[o] & 0x80) o++;    /* the time before it */
        if (++o >= n) return 0;
        if (d[o] & 0x80) st = d[o++];
        if (!st || o >= n) return 0;
        if (st == 0xFF) {
            uint8_t t = d[o++];
            if (t == 0x2F) return 1;
            if (t != 0x51) return 0;
            o += 3;                              /* the tempo */
            continue;
        }
        uint32_t k = (st & 0xF0) == 0xC0 || (st & 0xF0) == 0xD0 ? 1 : 2;
        if (o + k > n) return 0;
        if ((st & 0xF0) == 0x90) d[o + 1] = 0;
        o += k;
    }
    return 0;
}

/* alt: another PC room's WAV of a sequence (modfs_pc_bgm.h): in one mod, the
 * WAV that matches the sequence best (alt 0) is used for it. */
static int music_apply(const char *mod, const char *rel, unsigned blk, unsigned part, const char *path, int alt) {
    size_t ml = strlen(mod);
    for (int i = 0; i < nmusics; i++)
        if (musics[i].block == blk && musics[i].part == part && alt && !musics[i].alt
            && !strncmp(musics[i].what, mod, ml) && musics[i].what[ml] == '/') {
            note("mods: %s/%s: block %u's music %u is %s's (the same music, that one matches it better): not used there",
                 mod, rel, blk, part, musics[i].what);
            return 1;
        }
    long n = 0;
    uint8_t *d = load(path, &n);
    if (!d) { note("mods: %s/%s: couldn't read it", mod, rel); return 1; }
    for (int i = 0; i < nmusics; i++)            /* a later mod wins */
        if (musics[i].block == blk && musics[i].part == part) {
            note("mods: %s/%s: block %u's music %u, in place of %s", mod, rel, blk, part, musics[i].what);
            free(musics[i].wav);
            musics[i] = musics[--nmusics];
            nused--;
            break;
        }
    if (nmusics == (int)(sizeof musics / sizeof musics[0])) { free(d); note("mods: %s/%s: too many: not used", mod, rel); return 1; }
    Music *m = &musics[nmusics++];
    memset(m, 0, sizeof *m);
    m->block = blk;
    m->part = part;
    m->alt = alt;
    m->wav = d;
    m->n = n;
    snprintf(m->what, sizeof m->what, "%s/%s", mod, rel);
    nused++;
    return 1;
}

/* The music WAVs: their sequences silenced, the WAVs read. */
static void music_done(void) {
    if (!nmusics) return;
    int fi = disc_index("NOISE.BIN");
    Touched *t = touch(fi);
    static uint32_t v[1024];
    Bank *bk = NULL;
    uint32_t nb = noise_banks(t, v, &bk);
    for (int i = 0; i < nmusics; i++) {
        Music *m = &musics[i];
        uint32_t at = m->block < nb && bk[m->block].vs ? v[m->block] * 2048 : 0, c = at ? rd32(t->data + at) : 0;
        const char *why = "couldn't read it";
        uint8_t *seq = NULL, *was = NULL;
        uint32_t len = 0;
        if (m->part < 1 || m->part + 2 > c) why = "NOISE.BIN has no such music (a room's sound set: blocks 230 and up)";
        else {
            uint32_t off = at + rd32(t->data + at + 4 + 4 * m->part);
            len = rd32(t->data + at + 8 + 4 * m->part) - (off - at);
            seq = t->data + off;
            for (int g = 0; g < ngrows; g++)     /* (a mod's own sequence there) */
                if (grows[g].fi == fi && grows[g].offset == off) { seq = grows[g].data; len = grows[g].size; }
            uint32_t rs;
            int64_t from = wav_loop(m->wav, m->n, &rs);
            if ((was = malloc(len))) memcpy(was, seq, len);
            if (!seq_mute(seq, len)) why = "its sequence isn't one this can silence";
            else if ((m->pcm = wav_load(m->wav, m->n, 44100, &m->frames, &why)) && m->frames) {
                m->loop = from >= 0 && rs ? (uint32_t)((uint64_t)from * 44100 / rs) : 0;
                if (m->loop >= m->frames) m->loop = 0;
                if (was) ram_fix(0, was, seq, len);   /* (a savestate from before: the sequence there silenced too) */
                free(was);
                note("mods: %s: in place of NOISE.BIN block %u's music %u (%.1f s%s)", m->what, m->block, m->part, m->frames / 44100.0,
                     from >= 0 ? ", repeating from its loop start" : "");
                free(m->wav);
                m->wav = NULL;
                continue;
            }
        }
        if (was) memcpy(seq, was, len);          /* (the sequence left as it was: the music plays) */
        free(was);
        note("mods: %s: %s: not used", m->what, why);
        nused--;
        free(m->wav);
        free(m->pcm);
        musics[i--] = musics[--nmusics];
    }
    free(bk);
}

/* spu.c, every sample (44.1 kHz): the music WAVs playing, stereo, at full
 * scale; what the game's sequences do is looked at every 512 samples. */
void modfs_music(int32_t *l, int32_t *r) {
    static struct { int m, playing; uint32_t pos; int32_t vl, vr; } st[2] = {{-1, 0, 0, 0, 0}, {-1, 0, 0, 0, 0}};
    static int poll;
    *l = *r = 0;
    if (!g_mods_dir[0]) return;
    if (--poll <= 0) {
        poll = 512;
        int16_t set = (int16_t)rd16(ram_ptr(SND_SET_NOW));
        for (int k = 0; k < 2; k++) {
            int16_t id = (int16_t)rd16(ram_ptr(SND_SEQ_IDS + 2 * (uint32_t)k));
            uint32_t p = id >= 0 && id < 32 ? rd32(ram_ptr(SS_SCORE + 4 * (uint32_t)id)) : 0;
            int m = -1;
            if ((p >> 21) != 0x80000000u >> 21) p = 0;
            for (int i = 0; p && set >= 0 && i < nmusics; i++)
                if (musics[i].block == 0xE6u + (uint32_t)set && musics[i].part == (uint32_t)k + 1) m = i;
            if (m != st[k].m) { st[k].m = m; st[k].playing = 0; st[k].pos = 0; }
            if (!p) continue;
            uint32_t rd = rd32(ram_ptr(p)), start = rd32(ram_ptr(p + 4));
            int playing = rd32(ram_ptr(p + 0x98)) & 1;
            if (playing && !st[k].playing && rd - start < 64) {   /* (started, not resumed) */
                st[k].pos = 0;
                rt_log("music: NOISE/%03d/music%d%s", 0xE6 + set, k + 1, m >= 0 ? ".wav (a mod's)" : ".seq");
            }
            st[k].playing = playing;
            st[k].vl = (int16_t)rd16(ram_ptr(p + 0x58));
            st[k].vr = (int16_t)rd16(ram_ptr(p + 0x5A));
        }
    }
    for (int k = 0; k < 2; k++) {
        if (st[k].m < 0 || !st[k].playing) continue;
        const Music *m = &musics[st[k].m];
        if (st[k].pos >= m->frames) st[k].pos = m->loop;
        *l += m->pcm[2 * st[k].pos] * st[k].vl / 127;
        *r += m->pcm[2 * st[k].pos + 1] * st[k].vr / 127;
        st[k].pos++;
    }
}
