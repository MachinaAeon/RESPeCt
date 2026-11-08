/* modfs.c, part: Enemies and bosses: the PC's NAB/NAC, skins. Included by modfs.c, in this order. */

/* ------------------------------------------------------------ enemies */

/* The PC version's enemies (its nab folder: ENEMnn.NAB, the model, and
 * ENEMnn.NAC, its animations, nn the enemy type) are the PS1's own data
 * read out into floats: the NAC's commands are the PS1's animation
 * script word for word and its keys the PS1's 20-byte keys (s32
 * translation x,y,z, 0x7FFFFFFF for "unchanged", which the PC turns
 * into 0; s16 rotation x,y,z; a zero), the NAB's textures the PS1's
 * 16-bit TIMs as A1R5G5B5 (192-wide ones stretched to 256, nearest
 * pixel). On the disc a type's data is one block of OBJ.DAT (type 30:
 * EV_OBJ.DAT 0x196): a header (3, then where each part starts) and three
 * parts packed with the game's LZSS: textures, animations (2: script,
 * keys), model (HMD). A mod's NAB/NAC is written back into that: the
 * script and keys, the textures; the model's shape (the NAB's polygons)
 * isn't taken yet. What the PC loses (the 0x7FFFFFFF keys, the
 * textures' semi-transparency bit) comes from the disc's. */

/* What mods give for an enemy type: [0] its NAB, [1] its NAC, [2] a
 * picture for its texture (nab/ENEM00.png). */
typedef struct { int type; uint8_t *data[3]; long n[3]; char what[3][300]; } Enemy;
static Enemy enemies[64];
static int nenemies;

/* nab/ENEM00.NAB, nab/ENEM00.NAC, or nab/ENEM00.png (the texture as a picture) */
static int enemy_apply(const char *mod, const char *rel, const char *up, const char *path) {
    if (strncmp(up, "NAB/ENEM", 8) || !isdigit((unsigned char)up[8]) || !isdigit((unsigned char)up[9]) || up[10] != '.') return 0;
    int kind = !strcmp(up + 11, "NAB") ? 0 : !strcmp(up + 11, "NAC") ? 1 : is_picture(up + 10) ? 2 : -1;
    if (kind < 0) return 0;
    int type = (up[8] - '0') * 10 + (up[9] - '0');
    Enemy *e = NULL;
    for (int i = 0; i < nenemies; i++) if (enemies[i].type == type) e = &enemies[i];
    if (!e && nenemies < (int)(sizeof enemies / sizeof enemies[0])) {
        e = &enemies[nenemies++];
        memset(e, 0, sizeof *e);
        e->type = type;
    }
    long n = 0;
    uint8_t *d = e ? load(path, &n) : NULL;
    if (!d) { note("mods: %s/%s: couldn't read it", mod, rel); return 1; }
    if (!e->data[kind]) nused++;
    free(e->data[kind]);                         /* a later mod wins */
    e->data[kind] = d;
    e->n[kind] = n;
    snprintf(e->what[kind], sizeof e->what[kind], "%s/%s", mod, rel);
    return 1;
}

/* The NAC's animations into the PS1's (A: 2, script, keys). */
/* How much bigger an enemy type's animations (loaded once for all of its
 * kind, in the same memory as the models) can get. */
#define ANIM_GROWTH (modmem_enabled() ? (RAM_SIZE - RAM_GAME) : (64u * 1024))

/* The NAC's animations into the PS1's (A: 2, script, keys; *A replaced
 * when the NAC has another number of commands or keys). A command that
 * takes a key has the key's word offset (index x 5) in its low 16 bits:
 * from the NAC's own key index, so commands and keys can be added (up to
 * 13107 keys). NULL, or why not. */
static const char *nac_into(const uint8_t *nac, long n, uint8_t **Ap, uint32_t *nap, int *rebuilt) {
    uint8_t *A = *Ap;
    uint32_t na = *nap;
    *rebuilt = 0;
    if (n < 8 || na < 12 || rd32(A) != 2) return "the disc's animations aren't laid out as expected";
    uint32_t a0 = rd32(A + 4), a1 = rd32(A + 8);
    if (a0 > a1 || a1 > na) return "the disc's animations aren't laid out as expected";
    uint32_t ncmd = (a1 - a0) / 4, nkey = (na - a1) / 20, nk = rd32(nac);
    if (!nk || (uint64_t)nk * 112 + 8 > (uint64_t)n) return "it's cut short";
    uint32_t nc = rd32(nac + 4 + (size_t)nk * 112);
    if (4 + (uint64_t)nk * 112 + 4 + (uint64_t)nc * 8 != (uint64_t)n) return "it's cut short or has more after it";
    uint32_t keys = nk - 1;                      /* (the PC's NAC has a key more than the PS1's, at its end) */
    if (keys > 13107) return "it has more than 13107 keys, more than the PS1's commands can point at";
    uint8_t *old = A;
    uint32_t oa0 = a0, oa1 = a1;
    if (nc != ncmd || keys != nkey) {            /* laid out again: 2, 12, the script's end, then the script and the keys */
        uint32_t nn = 12 + 4 * nc + 20 * keys;
        if (nn > na + ANIM_GROWTH) {
            static char b[160];
            snprintf(b, sizeof b, "its animations would grow by %u bytes, more than the %u the game's memory has room for", nn - na, ANIM_GROWTH);
            return b;
        }
        A = calloc(nn, 1);
        if (!A) return "out of memory";
        wr32(A, 2);
        wr32(A + 4, 12);
        wr32(A + 8, 12 + 4 * nc);
        a0 = 12;
        a1 = 12 + 4 * nc;
        *rebuilt = 1;
        *nap = nn;
    }
    /* laid out again, keys may have moved: each found by what it holds among the disc's (as the PC
     * shows them), to know which of its channels were "unchanged" there */
    uint32_t *find = NULL;
    if (*rebuilt && (find = malloc(32768 * 4))) {
        memset(find, 0xFF, 32768 * 4);
        for (uint32_t k = 0; k < nkey; k++) {
            const uint8_t *q = old + oa1 + 20 * k;
            uint32_t hsh = 0;
            for (int c = 0; c < 3; c++) hsh = hsh * 31 + (rd32(q + 4 * c) == 0x7FFFFFFFu ? 0 : rd32(q + 4 * c));
            for (int c = 0; c < 3; c++) hsh = hsh * 31 + rd16(q + 12 + 2 * c);
            hsh &= 32767;
            while (find[hsh] != 0xFFFFFFFFu) hsh = (hsh + 1) & 32767;
            find[hsh] = k;
        }
    }
    const uint8_t *cmd = nac + 4 + (size_t)nk * 112 + 4;
    for (uint32_t i = 0; i < nc; i++) {
        uint32_t w = rd32(cmd + 8 * i), key = rd32(cmd + 8 * i + 4);
        if (i && key != 0xFFFFFFFFu) {
            if (key >= keys) { if (*rebuilt) free(A); free(find); return "a command takes a key it doesn't have"; }
            w = (w & 0xFFFF0000u) | (key * 5);
        }
        wr32(A + a0 + 4 * i, w);
    }
    for (uint32_t k = 0; k < keys; k++) {
        const uint8_t *f = nac + 4 + (size_t)k * 112;
        uint8_t *p = A + a1 + 20 * k;
        const uint8_t *was = !*rebuilt && k < nkey ? old + oa1 + 20 * k : NULL;
        float v[12];
        memcpy(v, f, sizeof v);
        if (find) {                              /* the disc's key with the same values, if there is one */
            int32_t t3[3];
            uint16_t r3[3];
            uint32_t hsh = 0;
            for (int c = 0; c < 3; c++) { t3[c] = (int32_t)lrintf(v[9 + c]); hsh = hsh * 31 + (uint32_t)t3[c]; }
            for (int c = 0; c < 3; c++) { r3[c] = (uint16_t)(int16_t)lrintf(v[5 + c]); hsh = hsh * 31 + r3[c]; }
            for (hsh &= 32767; find[hsh] != 0xFFFFFFFFu; hsh = (hsh + 1) & 32767) {
                const uint8_t *q = old + oa1 + 20 * find[hsh];
                int same = 1;
                for (int c = 0; c < 3 && same; c++)
                    same = (rd32(q + 4 * c) == 0x7FFFFFFFu ? 0 : (int32_t)rd32(q + 4 * c)) == t3[c] && rd16(q + 12 + 2 * c) == r3[c];
                if (same) { was = q; break; }
            }
        }
        for (int c = 0; c < 3; c++) {
            int32_t t = (int32_t)lrintf(v[9 + c]);
            if (was && rd32(was + 4 * c) == 0x7FFFFFFFu && t == 0) t = 0x7FFFFFFF;   /* "unchanged", which the PC turns into 0 */
            wr32(p + 4 * c, (uint32_t)t);
            wr16(p + 12 + 2 * c, (uint16_t)(int16_t)lrintf(v[5 + c]));
        }
        wr16(p + 18, 0);
    }
    (void)oa0;
    free(find);
    if (*rebuilt) *Ap = A;
    return NULL;
}

/* The NAB's animation tables (for each bone, where each of its animations
 * starts in the script: control << 16 | command) into the HMD's
 * animation unit (B). NULL, or why not. */
static const char *nab_seqs(const uint8_t *nab, long n, uint8_t *B, uint32_t nb) {
    if (n < 8) return "it's cut short";
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;  /* past the vertices, materials and groups */
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nm = rd32(nab + at);
    at += 4;
    for (uint32_t m = 0; m < nm && m < 64; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        uint32_t ntx = rd32(nab + at + 8);
        at += 352;
        for (uint32_t t = 0; t < ntx && at + 16 <= (uint64_t)n; t++) at += 16 + (uint64_t)rd32(nab + at);
    }
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t ng = rd32(nab + at);
    at += 4;
    for (uint32_t g = 0; g < ng && g < 4096; g++) {
        if (at + 24 > (uint64_t)n) return "it's cut short";
        at += 24 + (rd32(nab + at) ? 4 : 0) + 4;
    }
    at += (uint64_t)rd32(nab) * 60 + (uint64_t)rd32(nab + 4) * 20;   /* per-vertex and per-face data */
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nq = rd32(nab + at);
    at += 4;
    /* the HMD's animation unit (B: the model, 2 parts; or nb's top bit set: B is the HMD itself) */
    uint32_t o1 = 0;
    if (nb & 0x80000000u) nb &= 0x7FFFFFFF;
    else {
        if (nb < 12 || rd32(B) != 2) return "the disc's model isn't laid out as expected";
        o1 = rd32(B + 8);
        if (o1 > nb) return "the disc's model isn't laid out as expected";
    }
    uint32_t hw = (nb - o1) / 4, unit = 0;
    const uint8_t *h = B + o1;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    for (uint32_t bi = 0; bi < HW(3) && bi < 64 && !unit; bi++)   /* (following each block's chain of units) */
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64 && !unit; p = HW(p), guard++)
            if (HW(p + 3) == 0x3800000u) unit = p;
    if (!unit) return "the disc's model has no animation table";
    uint32_t pos = unit + 5;
    for (uint32_t q = 0; q < nq; q++) {
        if (at + 96 > (uint64_t)n) return "it's cut short";
        uint32_t cnt = rd32(nab + at + 12), w1 = HW(pos + 1);
        if (w1 == 0xFFFFFFFFu || (w1 >> 16) != cnt) return "its animation tables aren't the size of the PS1's (as many animations for each bone)";
        if (at + 96 + (uint64_t)cnt * 8 > (uint64_t)n) return "it's cut short";
        if ((uint64_t)pos + 6 + cnt > hw) return "the disc's model isn't laid out as expected";
        for (uint32_t k = 0; k < cnt; k++) {
            uint32_t ctl = rd32(nab + at + 96 + 8 * k), start = rd32(nab + at + 100 + 8 * k);
            if (start > 0xFFFF || ctl > 0xFFFF) return "an animation starts past what the PS1 can point at";
            wr32((uint8_t *)h + 4 * (size_t)(pos + 6 + k), ctl << 16 | start);
        }
        at += 96 + (uint64_t)cnt * 8;
        pos += w1 & 0xFFFF;
    }
#undef HW
    return NULL;
}

static int16_t sat16(float f) {
    long v = lrintf(f);
    return (int16_t)(v < -32768 ? -32768 : v > 32767 ? 32767 : v);
}

/* The NAB's shape into the PS1's model (B: 2, then the vertices and
 * normals, then the HMD). The NAB's triangles are the HMD's textured
 * polygons (GT3; GT4 as two: corners 0,1,2 and 3,2,1) ordered by
 * material (the NAB's textures, each on its VRAM texture page), then by
 * block, then as in the HMD; untextured ones come last and aren't
 * shapes anyone edits. A corner gives its vertex's position (the PC's z
 * is the PS1's -z), its normal and its UV (in the material's texture,
 * the polygon's page counted from it). Only what differs from the disc's
 * is written, so a vertex shared by many corners takes the one that was
 * moved. NULL, or why not. */
static const char *nab_shape(const uint8_t *nab, long n, uint8_t *B, uint32_t nb, uint32_t timw, int *ntri, int *moved) {
    if (n < 8 || nb < 12 || rd32(B) != 2) return "the disc's model isn't laid out as expected";
    uint32_t nv = rd32(nab), o0 = rd32(B + 4), o1 = rd32(B + 8);
    if (o0 + 8 > o1 || o1 + 16 > nb) return "the disc's model isn't laid out as expected";
    uint32_t vb = o0 + rd32(B + o0) * 4, nbase = o0 + rd32(B + o0 + 4) * 4;
    if (vb > nbase || nbase > o1) return "the disc's model isn't laid out as expected";
    uint32_t nvert = (nbase - vb) / 8, nnorm = (o1 - nbase) / 8;
    /* the NAB's materials: texture page and size */
    struct { uint32_t page, w, h; } mat[8];
    uint64_t at = 8 + (uint64_t)nv * 36;
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nm = rd32(nab + at);
    at += 4;
    if (nm > 8) return "it has more materials than this reads";
    for (uint32_t m = 0; m < nm; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        uint32_t ntx = rd32(nab + at + 8);
        mat[m].page = rd32(nab + at + 4) & 0x1F;
        mat[m].w = mat[m].h = 0;
        at += 352;
        for (uint32_t t = 0; t < ntx; t++) {
            if (at + 16 > (uint64_t)n) return "it's cut short";
            if (!mat[m].w) { mat[m].w = rd32(nab + at + 8); mat[m].h = rd32(nab + at + 12); }
            at += 16 + (uint64_t)rd32(nab + at);
        }
    }
    uint8_t *was = malloc(nb);                   /* the disc's, to tell what the NAB changes */
    if (!was) return "out of memory";
    memcpy(was, B, nb);
    const uint8_t *h = B + o1;
    uint32_t hw = (nb - o1) / 4;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    /* the textured polygons, in the NAB's order */
    uint32_t tri = 0, nblk = HW(3);
    *moved = 0;
    for (uint32_t m = 0; m < nm; m++)
        for (uint32_t bi = 0; bi < nblk && bi < 256; bi++)
            for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 4096; p = HW(p), guard++) {
                uint32_t nt = HW(p + 2) & 0xFFFF, base = HW(HW(p + 1) + 1) & 0x7FFFFFFF;
                for (uint32_t t = 0; t < nt && t < 4096; t++) {
                    uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
                    if (ty & 0xFF000000u) break;
                    if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
                    int quad = (ty & 0x10) != 0;
                    for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF); k++) {
                        uint32_t w0 = base + idx + k * (quad ? 7 : 6);
                        if (w0 + 7 > hw) { free(was); return "the disc's model isn't laid out as expected"; }
                        uint8_t *a = (uint8_t *)h + 4 * w0;
                        uint32_t pg = (rd32(a + 4) >> 16) & 0x1F, best = nm;
                        for (uint32_t q = 0; q < nm; q++)   /* the material whose page it is on */
                            if ((mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF) &&
                                (best == nm || (mat[q].page & 0xF) > (mat[best].page & 0xF)))
                                best = q;
                        if (best != m) continue;
                        static const int corners[2][3] = {{0, 1, 2}, {3, 2, 1}};
                        for (int half = 0; half < (quad ? 2 : 1); half++, tri++) {
                            if (tri >= nv / 3) { free(was); return "it has fewer polygons than the PS1's model (they can be moved, not added or taken away)"; }
                            for (int c = 0; c < 3; c++) {
                                int corner = corners[half][c];
                                uint32_t ni, vi;
                                if (!quad) { ni = rd16(a + 12 + 4 * corner); vi = rd16(a + 14 + 4 * corner); }
                                else if (!corner) { ni = rd16(a + 10); vi = rd16(a + 14); }
                                else { ni = rd16(a + 16 + 4 * (corner - 1)); vi = rd16(a + 18 + 4 * (corner - 1)); }
                                if (vi >= nvert || ni >= nnorm) { free(was); return "the disc's model isn't laid out as expected"; }
                                float f[9];
                                memcpy(f, nab + 8 + (size_t)(3 * tri + c) * 36, sizeof f);
                                int16_t pos[3] = {sat16(f[0]), sat16(f[1]), sat16(-f[2])}, nor[3] = {sat16(f[3]), sat16(f[4]), sat16(-f[5])};
                                uint32_t ov = vb + 8 * vi, on = nbase + 8 * ni;
                                for (int x = 0; x < 3; x++) {
                                    if ((int16_t)rd16(was + ov + 2 * x) != pos[x]) { wr16(B + ov + 2 * x, (uint16_t)pos[x]); (*moved)++; }
                                    if ((int16_t)rd16(was + on + 2 * x) != nor[x]) wr16(B + on + 2 * x, (uint16_t)nor[x]);
                                }
                                uint32_t wm = !m && timw ? timw : mat[m].w, hm = mat[m].h ? mat[m].h : 256;
                                long u = lrintf(f[7] * (float)wm) - 64 * (long)((pg & 0xF) - (mat[m].page & 0xF)), v = lrintf(f[8] * (float)hm);
                                uint8_t nu = (uint8_t)(u < 0 ? 0 : u > 255 ? 255 : u), nv8 = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                                size_t ua = (size_t)(a - B) + 4 * (size_t)corner;
                                if (was[ua] != nu) a[4 * corner] = nu;
                                if (was[ua + 1] != nv8) a[4 * corner + 1] = nv8;
                            }
                        }
                    }
                }
            }
#undef HW
    free(was);
    *ntri = (int)tri;
    return NULL;
}

/* A NAB whose polygons aren't the PS1 model's (some added or taken away,
 * or moved to other body parts): the model built again from it. The
 * HMD stays as it is (its skeleton, animation, coordinates and untextured
 * polygons), and gets after its end a new unit for every block (body
 * part) with its textured polygons, the NAB's triangles (GT3, each on the
 * texture page its UVs need), the block pointing to it; the vertices and
 * normals they need are added after the disc's (shared ones once). The
 * model, the HMD part copied for each enemy, grows by the new triangles'
 * size: up to MODEL_GROWTH, so a room full of them still fits the
 * memory the game loads enemies into (0x80150000 up; the fullest room
 * measured leaves 213 KB). */
#define MODEL_GROWTH (modmem_enabled() ? (RAM_SIZE - RAM_GAME) : (48u * 1024))

typedef struct { uint32_t page, w, h, per; } NabMat;   /* per: pixels a texture page is wide (64 at 16 bits, 128 at 8) */
typedef struct { uint32_t start, count, node, mat; } NabGroup;   /* mat ~0u: untextured */

/* The NAB's materials and groups; NULL, or why not. */
static const char *nab_layout(const uint8_t *nab, long n, NabMat *mat, uint32_t *nm, NabGroup *grp, uint32_t *ng) {
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;
    if (n < 8 || at + 4 > (uint64_t)n) return "it's cut short";
    *nm = rd32(nab + at);
    at += 4;
    if (*nm > 8) return "it has more materials than this reads";
    for (uint32_t m = 0; m < *nm; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        uint32_t ntx = rd32(nab + at + 8);
        mat[m].page = rd32(nab + at + 4) & 0x1F;
        { uint32_t d = nab[at + 92]; mat[m].per = d == 8 ? 128 : d == 4 ? 256 : 64; }   /* (its first texture's depth) */
        mat[m].w = mat[m].h = 0;
        at += 352;
        for (uint32_t t = 0; t < ntx; t++) {
            if (at + 16 > (uint64_t)n) return "it's cut short";
            if (!mat[m].w) { mat[m].w = rd32(nab + at + 8); mat[m].h = rd32(nab + at + 12); }
            at += 16 + (uint64_t)rd32(nab + at);
        }
    }
    if (at + 4 > (uint64_t)n) return "it's cut short";
    *ng = rd32(nab + at);
    at += 4;
    if (*ng > 256) return "it has more groups than this reads";
    for (uint32_t g = 0; g < *ng; g++) {
        if (at + 24 > (uint64_t)n) return "it's cut short";
        uint32_t has = rd32(nab + at);
        grp[g].start = rd32(nab + at + 8);
        grp[g].count = rd32(nab + at + 12);
        at += 24;
        grp[g].mat = ~0u;
        if (has) { grp[g].mat = rd32(nab + at); at += 4; }
        grp[g].node = rd32(nab + at);
        at += 4;
        if ((uint64_t)grp[g].start + grp[g].count > rd32(nab) / 3) return "a group has triangles past its end";
        if (grp[g].mat != ~0u && grp[g].mat >= *nm) return "a group's material isn't one of its own";
    }
    return NULL;
}

/* The model built again from the NAB; the new model (malloc'd) and its
 * size, or NULL and why. */
static uint8_t *nab_rebuild(const uint8_t *nab, long n, const uint8_t *B, uint32_t nb, uint32_t timw, uint32_t *outn, int *ntri,
                            const char **why) {
    static NabMat mat[8];
    static NabGroup grp[256];
    uint32_t nm = 0, ng = 0;
    if ((*why = nab_layout(nab, n, mat, &nm, grp, &ng))) return NULL;
    *why = "the disc's model isn't laid out as expected";
    if (nb < 12 || rd32(B) != 2) return NULL;
    uint32_t o0 = rd32(B + 4), o1 = rd32(B + 8);
    if (o0 + 8 > o1 || o1 + 16 > nb) return NULL;
    uint32_t vb = o0 + rd32(B + o0) * 4, nbase = o0 + rd32(B + o0 + 4) * 4;
    if (vb > nbase || nbase > o1) return NULL;
    uint32_t nS = (nbase - vb) / 8, nN = (o1 - nbase) / 8, hw = (nb - o1) / 4;
    const uint8_t *h = B + o1;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    uint32_t nblk = HW(3), hdr = 0, base = 0;
    if (nblk > 64) return NULL;
    /* each material's texture window bits (from a polygon of the disc's that uses it) */
    uint32_t tsb[8] = {0}, cba[8] = {0};
    int seen[8] = {0};
    for (uint32_t bi = 0; bi < nblk; bi++) {
        uint32_t p = HW(4 + bi);
        if (!p || p == 0xFFFFFFFFu) continue;
        uint32_t nt = HW(p + 2) & 0xFFFF, b0 = HW(HW(p + 1) + 1) & 0x7FFFFFFF;
        for (uint32_t t = 0; t < nt && t < 4096; t++) {
            uint32_t ty = HW(p + 3 + 3 * t), idx = HW(p + 5 + 3 * t);
            if (ty & 0xFF000000u) break;
            if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
            hdr = HW(p + 1);
            base = b0;
            uint32_t w1 = HW(b0 + idx + 1), pg = (w1 >> 16) & 0x1F;
            for (uint32_t q = 0; q < nm; q++)
                if (!seen[q] && (mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF)) {
                    seen[q] = 1;
                    tsb[q] = w1 >> 16;
                    cba[q] = HW(b0 + idx) >> 16;
                }
        }
    }
    if (!hdr) return NULL;
    for (uint32_t q = 0; q < nm; q++)
        if (!seen[q]) tsb[q] = (tsb[0] & ~0x1Fu) | mat[q].page;   /* (a material none of the disc's polygons used) */
    /* the new vertices and normals after the disc's, and the new HMD after the disc's */
    uint32_t total = 0;
    for (uint32_t g = 0; g < ng; g++) if (grp[g].mat != ~0u) total += grp[g].count;
    size_t cap = (size_t)nb + (size_t)total * 32 + (size_t)nblk * 12 + (size_t)total * 12 + 4096;
    uint8_t *S = malloc(((size_t)nS + total * 3) * 8), *N = malloc(((size_t)nN + total * 3) * 8), *H = malloc(cap);
    uint32_t *hash = malloc(2 * 65536 * 4);
    if (!S || !N || !H || !hash) { free(S); free(N); free(H); free(hash); *why = "out of memory"; return NULL; }
    memcpy(S, B + vb, (size_t)nS * 8);
    memcpy(N, B + nbase, (size_t)nN * 8);
    uint32_t cs = nS, cn = nN;
    memset(hash, 0xFF, 2 * 65536 * 4);
    /* hash[0..65535]: vertices, [65536..]: normals; slot = 1 + index, linear probing */
    for (int pass = 0; pass < 2; pass++)
        for (uint32_t i = 0; i < (pass ? nN : nS); i++) {
            const uint8_t *s = (pass ? N : S) + 8 * i;
            uint32_t k = (rd16(s) * 73856093u ^ rd16(s + 2) * 19349663u ^ rd16(s + 4) * 83492791u) & 65535;
            while (hash[pass * 65536 + k] != 0xFFFFFFFFu) k = (k + 1) & 65535;
            hash[pass * 65536 + k] = i;
        }
    memcpy(H, h, (size_t)hw * 4);
    uint32_t hn = hw;                            /* words in the new HMD */
    /* the words the disc's textured polygons take are free to use again (not their units' words: a
     * unit put there isn't drawn) */
    uint8_t *fw = calloc(hw + 1, 1);
    uint32_t (*ent)[3] = malloc(sizeof *ent * 8192);
    if (!fw || !ent) { free(S); free(N); free(H); free(hash); free(fw); free(ent); *why = "out of memory"; return NULL; }
    for (uint32_t bi = 0; bi < nblk; bi++) {
        uint32_t p = HW(4 + bi);
        if (!p || p == 0xFFFFFFFFu || HW(p) != 0xFFFFFFFFu) continue;   /* (one unit to a block, as the disc's are) */
        uint32_t nt = HW(p + 2) & 0xFFFF, b0 = HW(HW(p + 1) + 1) & 0x7FFFFFFF, other = 0;
        for (uint32_t t = 0; t < nt && t < 4096; t++) {
            uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
            if (ty & 0xFF000000u) { other = 1; break; }
            if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
            uint32_t pw = ty & 0x10 ? 7 : 6;
            for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF) * pw; k++)
                if (b0 + idx + k < hw) fw[b0 + idx + k] = 1;
        }
        (void)other;
    }
    *ntri = 0;
    for (uint32_t bi = 0; bi < nblk; bi++) {
        uint32_t p = HW(4 + bi), keep[64][3], nk = 0, flag = 0;
        int had = 0;
        if (p && p != 0xFFFFFFFFu) {
            uint32_t nt = HW(p + 2) & 0xFFFF;
            for (uint32_t t = 0; t < nt && t < 4096; t++) {
                uint32_t ty = HW(p + 3 + 3 * t);
                if (ty & 0xFF000000u) { nk = 99; break; }    /* not a unit of polygons: left as it is */
                if ((ty & 0xFF) == 0x0D || (ty & 0xFF) == 0x15) { had = 1; flag |= ty & 0x800000; continue; }
                if (nk < 64) { keep[nk][0] = ty; keep[nk][1] = HW(p + 4 + 3 * t); keep[nk][2] = HW(p + 5 + 3 * t); nk++; }
            }
        }
        if (nk == 99) continue;
        uint32_t count = 0, ne = 0;
        for (uint32_t g = 0; g < ng; g++) if (grp[g].mat != ~0u && grp[g].node == bi) count += grp[g].count;
        if (!count && !had) continue;
        /* the polygons: two triangles that were a quad (corners a,b,c and d,c,b) a GT4 again, the rest GT3 */
        for (uint32_t g = 0; g < ng; g++) {
            if (grp[g].mat == ~0u || grp[g].node != bi) continue;
            const NabMat *mm = &mat[grp[g].mat];
            uint32_t wm = !grp[g].mat && timw ? timw : mm->w, hm2 = mm->h ? mm->h : 256;
            for (uint32_t t = grp[g].start; t < grp[g].start + grp[g].count;) {
                const uint8_t *ta = nab + 8 + (size_t)3 * t * 36, *tb = ta + 3 * 36;
                int quad = t + 1 < grp[g].start + grp[g].count && !memcmp(tb + 36, ta + 72, 36) && !memcmp(tb + 72, ta + 36, 36);
                int nc = quad ? 4 : 3;
                const uint8_t *cor[4] = {ta, ta + 36, ta + 72, tb};
                long u[4], v[4], lo = 1 << 30;
                uint32_t vi[4], ni[4];
                for (int c = 0; c < nc; c++) {
                    float f[9];
                    memcpy(f, cor[c], sizeof f);
                    int16_t pos[3] = {sat16(f[0]), sat16(f[1]), sat16(-f[2])}, nor[3] = {sat16(f[3]), sat16(f[4]), sat16(-f[5])};
                    for (int pass = 0; pass < 2; pass++) {
                        const int16_t *x = pass ? nor : pos;
                        uint8_t *arr = pass ? N : S;
                        uint32_t *cnt = pass ? &cn : &cs;
                        uint32_t k = ((uint16_t)x[0] * 73856093u ^ (uint16_t)x[1] * 19349663u ^ (uint16_t)x[2] * 83492791u) & 65535, found = ~0u;
                        while (hash[pass * 65536 + k] != 0xFFFFFFFFu) {
                            const uint8_t *q = arr + 8 * hash[pass * 65536 + k];
                            if ((int16_t)rd16(q) == x[0] && (int16_t)rd16(q + 2) == x[1] && (int16_t)rd16(q + 4) == x[2]) { found = hash[pass * 65536 + k]; break; }
                            k = (k + 1) & 65535;
                        }
                        if (found == ~0u) {
                            if (*cnt >= 65535) { free(S); free(N); free(hash); free(H); free(fw); free(ent); *why = "too many vertices"; return NULL; }
                            found = (*cnt)++;
                            uint8_t *q = arr + 8 * found;
                            wr16(q, (uint16_t)x[0]); wr16(q + 2, (uint16_t)x[1]); wr16(q + 4, (uint16_t)x[2]); wr16(q + 6, 0);
                            hash[pass * 65536 + k] = found;
                        }
                        (pass ? ni : vi)[c] = found;
                    }
                    u[c] = lrintf(f[7] * (float)wm);
                    v[c] = lrintf(f[8] * (float)hm2);
                    if (u[c] < lo) lo = u[c];
                }
                long off = lo < 0 ? 0 : lo / (long)mm->per;   /* the texture page from where its UVs start */
                if ((mm->page & 0xF) + off > 15) off = 15 - (mm->page & 0xF);
                /* its place: the first free run it fits in, else after the end */
                uint32_t pw = quad ? 7 : 6, at = 0, run = 0;
                for (uint32_t w = base; w < hw && run < pw; w++) {   /* (after the polygons' start: their index is counted from it) */
                    if (!fw[w]) { run = 0; continue; }
                    if (!run) at = w;
                    run++;
                }
                if (run < pw) { at = hn; hn += pw; }
                else memset(fw + at, 0, pw);
                uint8_t *pk = H + 4 * (size_t)at;
                memset(pk, 0, 4 * pw);
                for (int c = 0; c < nc; c++) {
                    long uu = u[c] - (long)mm->per * off, vv = v[c];
                    pk[4 * c] = (uint8_t)(uu < 0 ? 0 : uu > 255 ? 255 : uu);
                    pk[4 * c + 1] = (uint8_t)(vv < 0 ? 0 : vv > 255 ? 255 : vv);
                }
                wr16(pk + 2, (uint16_t)cba[grp[g].mat]);
                wr16(pk + 6, (uint16_t)((tsb[grp[g].mat] & ~0x1Fu) | (mm->page & 0x10) | ((mm->page & 0xF) + (uint32_t)off)));
                if (quad) {
                    wr16(pk + 10, (uint16_t)ni[0]);
                    wr16(pk + 14, (uint16_t)vi[0]);
                    for (int c = 1; c < 4; c++) { wr16(pk + 16 + 4 * (c - 1), (uint16_t)ni[c]); wr16(pk + 18 + 4 * (c - 1), (uint16_t)vi[c]); }
                } else
                    for (int c = 0; c < 3; c++) { wr16(pk + 12 + 4 * c, (uint16_t)ni[c]); wr16(pk + 14 + 4 * c, (uint16_t)vi[c]); }
                uint32_t ty = quad ? 0x15 : 0x0D;     /* the type's entry: a run of packets one after the other */
                if (ne && ent[ne - 1][0] == ty && ent[ne - 1][1] + ent[ne - 1][2] * pw == at) ent[ne - 1][2]++;
                else if (ne < 8192) { ent[ne][0] = ty; ent[ne][1] = at; ent[ne][2] = 1; ne++; }
                *ntri += quad ? 2 : 1;
                t += quad ? 2 : 1;
            }
        }
        /* the block's new unit, after the end */
        uint32_t nt = nk + ne;
        if (!nt) { wr32(H + 4 * (4 + bi), 0); continue; }
        uint32_t uw = 3 + 3 * nt, unit = hn;
        hn += uw;
        uint8_t *u8 = H + 4 * (size_t)unit;
        wr32(u8, p && p != 0xFFFFFFFFu ? HW(p) : 0xFFFFFFFFu);   /* (the block's next unit, if it had one) */
        wr32(u8 + 4, p && p != 0xFFFFFFFFu ? HW(p + 1) : hdr);
        wr32(u8 + 8, 0x80000000u | nt);
        uint32_t e = 12;
        for (uint32_t k = 0; k < ne; k++, e += 12) {
            wr32(u8 + e, ent[k][0] | (k ? 0 : flag));
            wr32(u8 + e + 4, 0x80000002u | ent[k][2] << 16);
            wr32(u8 + e + 8, ent[k][1] - base);
        }
        for (uint32_t k = 0; k < nk; k++, e += 12) { wr32(u8 + e, keep[k][0]); wr32(u8 + e + 4, keep[k][1]); wr32(u8 + e + 8, keep[k][2]); }
        wr32(H + 4 * (4 + bi), unit);
    }
    free(fw);
    free(ent);
#undef HW
    free(hash);
    if ((hn - hw) * 4 > MODEL_GROWTH) {
        free(S); free(N); free(H);
        static char b[160];
        snprintf(b, sizeof b, "the model would grow by %u bytes, more than the %u each enemy of it can take in memory", (hn - hw) * 4, MODEL_GROWTH);
        *why = b;
        return NULL;
    }
    /* B: 2, then the vertices and normals (their header: where each starts), then the HMD */
    uint32_t b0n = 8 + cs * 8 + cn * 8;
    size_t total_n = 12 + (size_t)b0n + (size_t)hn * 4;
    uint8_t *o = malloc(total_n);
    if (!o) { free(S); free(N); free(H); *why = "out of memory"; return NULL; }
    wr32(o, 2);
    wr32(o + 4, 12);
    wr32(o + 8, 12 + b0n);
    wr32(o + 12, 2);
    wr32(o + 16, 2 + cs * 2);
    memcpy(o + 20, S, (size_t)cs * 8);
    memcpy(o + 20 + (size_t)cs * 8, N, (size_t)cn * 8);
    memcpy(o + 12 + b0n, H, (size_t)hn * 4);
    free(S); free(N); free(H);
    *outn = (uint32_t)total_n;
    *why = NULL;
    return o;
}

/* Whether the NAB has the PS1 model's textured polygons, block by block
 * and material by material (then it goes in place, nab_shape). */
static int nab_same_layout(const uint8_t *nab, long n, const uint8_t *B, uint32_t nb) {
    static NabMat mat[8];
    static NabGroup grp[256];
    uint32_t nm = 0, ng = 0;
    if (nab_layout(nab, n, mat, &nm, grp, &ng) || nb < 12 || rd32(B) != 2) return 0;
    uint32_t o1 = rd32(B + 8);
    if (o1 + 16 > nb) return 0;
    const uint8_t *h = B + o1;
    uint32_t hw = (nb - o1) / 4;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    static uint32_t ps1[64][8], pc[64][8];
    memset(ps1, 0, sizeof ps1);
    memset(pc, 0, sizeof pc);
    uint32_t nblk = HW(3);
    if (nblk > 64) return 0;
    for (uint32_t bi = 0; bi < nblk; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 4096; p = HW(p), guard++) {
            uint32_t nt = HW(p + 2) & 0xFFFF, base = HW(HW(p + 1) + 1) & 0x7FFFFFFF;
            for (uint32_t t = 0; t < nt && t < 4096; t++) {
                uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
                if (ty & 0xFF000000u) break;
                if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
                int quad = (ty & 0x10) != 0;
                for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF); k++) {
                    uint32_t pg = (HW(base + idx + k * (quad ? 7 : 6) + 1) >> 16) & 0x1F, best = nm;
                    for (uint32_t q = 0; q < nm; q++)
                        if ((mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF) &&
                            (best == nm || (mat[q].page & 0xF) > (mat[best].page & 0xF)))
                            best = q;
                    if (best < nm) ps1[bi][best] += quad ? 2 : 1;
                }
            }
        }
#undef HW
    for (uint32_t g = 0; g < ng; g++)
        if (grp[g].mat != ~0u) {
            if (grp[g].node >= 64) return 0;
            pc[grp[g].node][grp[g].mat] += grp[g].count;
        }
    return !memcmp(ps1, pc, sizeof ps1);
}

/* The NAB's textures into the PS1's 16-bit TIMs (in order, same height). */
static const char *nab_into(const uint8_t *nab, long n, uint8_t *tex, uint32_t nt, int *ntex) {
    if (n < 8) return "it's cut short";
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nm = rd32(nab + at);
    at += 4;
    uint32_t used = 0;                           /* TIMs taken, by offset order */
    for (uint32_t m = 0; m < nm; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        uint32_t ntx = rd32(nab + at + 8);
        at += 352;
        for (uint32_t t = 0; t < ntx; t++) {
            if (at + 16 > (uint64_t)n) return "it's cut short";
            uint32_t size = rd32(nab + at), W = rd32(nab + at + 8), H = rd32(nab + at + 12);
            const uint8_t *px = nab + at + 16;
            at += 16 + (uint64_t)size;
            if (at > (uint64_t)n || (uint64_t)W * H * 2 > size) return "it's cut short";
            uint32_t o = 0, k = 0;               /* the next 16-bit TIM of that height */
            uint8_t *dst = NULL;
            uint32_t w = 0;
            for (; o + 20 <= nt && rd32(tex + o) == 0x10; k++) {
                uint32_t flag = rd32(tex + o + 4), j = o + 8;
                if (flag & 8) j += rd32(tex + j);
                if (j + 12 > nt) break;
                uint32_t il = rd32(tex + j), iw = rd16(tex + j + 8), ih = rd16(tex + j + 10);
                if (!dst && k >= used && (flag & 3) == 2 && ih == H && iw <= W && iw && j + 12 + iw * ih * 2 <= nt) {
                    dst = tex + j + 12;
                    w = iw;
                    used = k + 1;
                }
                o = j + il;
            }
            if (!dst) continue;                  /* a texture the PS1 has elsewhere */
            for (uint32_t y = 0; y < H; y++)
                for (uint32_t x = 0; x < w; x++) {
                    uint16_t p = (uint16_t)rd16(px + 2 * ((size_t)y * W + (x * W + w - 1) / w));
                    uint16_t was = (uint16_t)rd16(dst + 2 * ((size_t)y * w + x));
                    uint16_t v = (uint16_t)(((p >> 10) & 31) | ((p >> 5) & 31) << 5 | (p & 31) << 10);
                    if (v == (was & 0x7FFF)) v = was;                            /* as it was */
                    else if (!(p & 0x8000)) v = 0;                               /* see-through */
                    else v = (uint16_t)((v ? v : 0x0400) | (was & 0x8000));
                    wr16(dst + 2 * ((size_t)y * w + x), v);
                }
            (*ntex)++;
        }
    }
    return NULL;
}

/* The first 16-bit TIM of an enemy's textures (its skin), or NULL. */
static uint8_t *enemy_skin(uint8_t *tex, uint32_t nt) {
    for (uint32_t o = 0; o + 20 <= nt && rd32(tex + o) == 0x10;) {
        uint32_t flag = rd32(tex + o + 4), j = o + 8;
        if (flag & 8) j += rd32(tex + j);
        if (j + 12 > nt) break;
        if ((flag & 3) == 2) return tex + o;
        o = j + rd32(tex + j);
    }
    return NULL;
}

/* A boss's shape (types 31-33: one HMD, EV_OBJ.DAT) from its NAB, in
 * place. Its textured polygons are of two kinds: each bone's own (as the
 * other enemies', from the unit's header: 4, polygons, vertices,
 * normals), and the shared skin (type bit 0x1000000; header: 5,
 * polygons, vertices, a buffer, normals, a buffer) whose vertices each
 * belong to a bone and are kept in that bone's space. The NAB's
 * triangles: for each material the skin's, then the bones', each in the
 * HMD's order (GT4 as two). The skin's corners are in the model's space
 * in the NAB: back through their bone's rest matrix (the NAB node's 3rd
 * 4x4 matrix; the bone from the NAB's per-vertex data), z negated. Only
 * what differs from where the disc's data lands is written. A material
 * that is a sheet of small textures (type 33's) has each one's place in
 * the page in its record, and the NAB's UVs count from it, in its used
 * size; a polygon's texture is the one the disc's UVs are in. NULL, or
 * why not. */
static const char *boss_shape(const uint8_t *nab, long n, uint8_t *h, uint32_t hw, int parts, int *ntri, int *moved, int *keptuv,
                              int *changed) {   /* parts: 1 the skin, 2 the bones' polygons */
    static NabMat nmat[8];
    static NabGroup grp[256];
    uint32_t nmg = 0, ng = 0;
    const char *bad = nab_layout(nab, n, nmat, &nmg, grp, &ng);
    if (bad) return bad;
    uint32_t gi[8][65], go[8][65];              /* each (material, node)'s next group and how far into it */
    memset(gi, 0, sizeof gi);
    memset(go, 0, sizeof go);
    uint32_t nv = rd32(nab), nf = rd32(nab + 4);
    uint64_t at = 8 + (uint64_t)nv * 36;
    if (n < 8 || at + 4 > (uint64_t)n) return "it's cut short";
    struct { uint32_t page, w, h, nt, bpp; int32_t rect[10][4]; } mat[8];   /* rect: a sheet's textures (u, v, w, h in the page) */
    uint32_t nm = rd32(nab + at);
    at += 4;
    if (nm > 8) return "it has more materials than this reads";
    for (uint32_t m = 0; m < nm; m++) {
        if (at + 352 > (uint64_t)n) return "it's cut short";
        mat[m].page = rd32(nab + at + 4) & 0x1F;
        mat[m].nt = rd32(nab + at + 8);
        uint32_t b = nab[at + 92];               /* its first texture's depth */
        mat[m].bpp = b == 4 || b == 8 ? b : 16;
        mat[m].w = mat[m].h = 0;
        for (uint32_t t = 0; t < mat[m].nt && t < 10; t++) {   /* each texture's place: VRAM x, y, depth, used width, height */
            uint32_t x = rd16(nab + at + 12 + 2 * t), y = rd16(nab + at + 52 + 2 * t), d = nab[at + 92 + t];
            d = d == 4 || d == 8 ? d : 16;
            mat[m].rect[t][0] = (int32_t)((x - (mat[m].page & 0xF) * 64) * (16 / d));
            mat[m].rect[t][1] = (int32_t)(y - ((mat[m].page >> 4) & 1) * 256);
            mat[m].rect[t][2] = (int32_t)rd32(nab + at + 192 + 4 * t);
            mat[m].rect[t][3] = (int32_t)rd32(nab + at + 272 + 4 * t);
        }
        at += 352;
        for (uint32_t t = 0; t < mat[m].nt; t++) {
            if (at + 16 > (uint64_t)n) return "it's cut short";
            if (!mat[m].w) { mat[m].w = rd32(nab + at + 8); mat[m].h = rd32(nab + at + 12); }
            at += 16 + (uint64_t)rd32(nab + at);
        }
    }
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t ng2 = rd32(nab + at);
    at += 4;
    for (uint32_t g = 0; g < ng2 && g < 4096; g++) {
        if (at + 28 > (uint64_t)n) return "it's cut short";
        at += 24 + (rd32(nab + at) ? 4 : 0) + 4;
    }
    uint64_t aux = at;                           /* per vertex: 15 words, the 1st's top half its bone */
    at += (uint64_t)nv * 60 + (uint64_t)nf * 20;
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nq = rd32(nab + at);
    at += 4;
    for (uint32_t q = 0; q < nq && q < 256; q++) {
        if (at + 96 > (uint64_t)n) return "it's cut short";
        at += 96 + (uint64_t)rd32(nab + at + 12) * 8;
    }
    at += 4;                                     /* (a value), then two lists */
    for (int l = 0; l < 2; l++) {
        if (at + 4 > (uint64_t)n) return "it's cut short";
        uint32_t c = rd32(nab + at);
        at += 4;
        for (uint32_t e = 0; e < c && e < 65536; e++) {
            if (at + 12 > (uint64_t)n) return "it's cut short";
            at += 12 + (uint64_t)rd32(nab + at + 4) * 12;
        }
    }
    at += 12;
    if (at + 4 > (uint64_t)n) return "it's cut short";
    uint32_t nnode = rd32(nab + at) + 1;
    at += 4;
    uint64_t nodes = at;
    if (nodes + (uint64_t)nnode * 264 > (uint64_t)n) return "it's cut short";
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    uint8_t *was = malloc((size_t)hw * 4);
    if (!was) return "out of memory";
    memcpy(was, h, (size_t)hw * 4);
    uint32_t tri = 0, used = 0;
    *moved = *keptuv = 0;
    for (uint32_t m = 0; m < nm; m++)
        for (int shared = 1; shared >= 0; shared--)
            for (uint32_t bi = 0; bi < HW(3) && bi < 64; bi++)
                for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64; p = HW(p), guard++) {
                    uint32_t nt = HW(p + 2) & 0xFFFF, hdr = HW(p + 1), base = HW(hdr + 1) & 0x7FFFFFFF;
                    for (uint32_t t = 0; t < nt && t < 4096; t++) {
                        uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
                        if ((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) continue;
                        if (ty & 0xFE000000u || !(ty & 0x1000000) != !shared) continue;
                        int quad = (ty & 0x10) != 0;
                        uint32_t vs = HW(hdr + 2) & 0x7FFFFFFF, ns = HW(hdr + (shared ? 4 : 3)) & 0x7FFFFFFF;
                        for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF); k++) {
                            uint32_t w0 = base + idx + k * (quad ? 7 : 6);
                            if (w0 + 7 > hw) { memcpy(h, was, (size_t)hw * 4); free(was); return "the disc's model isn't laid out as expected"; }
                            uint8_t *a = h + 4 * w0;
                            uint32_t pg = (rd32(a + 4) >> 16) & 0x1F, best = nm;
                            for (uint32_t q = 0; q < nm; q++)
                                if ((mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF) &&
                                    (best == nm || (mat[q].page & 0xF) > (mat[best].page & 0xF)))
                                    best = q;
                            if (best != m) continue;
                            if (!(parts & (shared ? 1 : 2))) continue;
                            static const int corners[2][3] = {{0, 1, 2}, {3, 2, 1}};
                            uint32_t node = shared ? 0 : bi;
                            for (int half = 0; half < (quad ? 2 : 1); half++) {
                                /* the NAB's next triangle of this material and node */
                                while (gi[m][node] < ng && (grp[gi[m][node]].mat != m || grp[gi[m][node]].node != node ||
                                                            go[m][node] >= grp[gi[m][node]].count)) {
                                    gi[m][node]++;
                                    go[m][node] = 0;
                                }
                                if (gi[m][node] >= ng) { memcpy(h, was, (size_t)hw * 4); free(was); return "it has fewer polygons than the PS1's model (for the bosses' skin they can be moved, not added or taken away)"; }
                                tri = grp[gi[m][node]].start + go[m][node]++;
                                used++;
                                for (int c = 0; c < 3; c++) {
                                    int corner = corners[half][c];
                                    uint32_t ni, vi;
                                    if (!quad) { ni = rd16(a + 12 + 4 * corner); vi = rd16(a + 14 + 4 * corner); }
                                    else if (!corner) { ni = rd16(a + 10); vi = rd16(a + 14); }
                                    else { ni = rd16(a + 16 + 4 * (corner - 1)); vi = rd16(a + 18 + 4 * (corner - 1)); }
                                    if (vs + 2 * vi + 2 > hw || ns + 2 * ni + 2 > hw) { memcpy(h, was, (size_t)hw * 4); free(was); return "the disc's model isn't laid out as expected"; }
                                    uint32_t ci = 3 * tri + (uint32_t)c;
                                    float f[9];
                                    memcpy(f, nab + 8 + (size_t)ci * 36, sizeof f);
                                    uint8_t *pv = h + 4 * (size_t)vs + 8 * vi, *pn = h + 4 * (size_t)ns + 8 * ni;
                                    const uint8_t *ov = was + 4 * (size_t)vs + 8 * vi, *on = was + 4 * (size_t)ns + 8 * ni;
                                    int16_t pos[3], nor[3];
                                    if (!shared) {
                                        pos[0] = sat16(f[0]); pos[1] = sat16(f[1]); pos[2] = sat16(-f[2]);
                                        nor[0] = sat16(f[3]); nor[1] = sat16(f[4]); nor[2] = sat16(-f[5]);
                                    } else {
                                        uint32_t bone = rd32(nab + aux + (uint64_t)ci * 60) >> 16;
                                        if (bone >= nnode) { memcpy(h, was, (size_t)hw * 4); free(was); return "a vertex's bone isn't one of its own"; }
                                        float M[16];
                                        memcpy(M, nab + nodes + (uint64_t)bone * 264 + 128, sizeof M);
                                        /* where the disc's lands: M (x, y, -z) + t; the same for the normal, no t */
                                        int same_p = 1, same_n = 1;
                                        for (int r = 0; r < 3; r++) {
                                            float lp = 0, ln = 0;
                                            for (int x = 0; x < 3; x++) {
                                                float sp = (float)(int16_t)rd16(ov + 2 * x), sn = (float)(int16_t)rd16(on + 2 * x);
                                                if (x == 2) { sp = -sp; sn = -sn; }
                                                lp += M[4 * r + x] * sp;
                                                ln += M[4 * r + x] * sn;
                                            }
                                            lp += M[4 * r + 3];
                                            if (lrintf(lp) != lrintf(f[r])) same_p = 0;
                                            if (lrintf(ln) != lrintf(f[3 + r])) same_n = 0;
                                        }
                                        /* back: M^-1 (p - t), z negated */
                                        double A[9], inv[9];
                                        for (int x = 0; x < 9; x++) A[x] = M[4 * (x / 3) + x % 3];
                                        double det = A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]);
                                        if (fabs(det) < 1e-9) { memcpy(h, was, (size_t)hw * 4); free(was); return "a bone's rest matrix can't be turned back"; }
                                        inv[0] = (A[4] * A[8] - A[5] * A[7]) / det; inv[1] = (A[2] * A[7] - A[1] * A[8]) / det; inv[2] = (A[1] * A[5] - A[2] * A[4]) / det;
                                        inv[3] = (A[5] * A[6] - A[3] * A[8]) / det; inv[4] = (A[0] * A[8] - A[2] * A[6]) / det; inv[5] = (A[2] * A[3] - A[0] * A[5]) / det;
                                        inv[6] = (A[3] * A[7] - A[4] * A[6]) / det; inv[7] = (A[1] * A[6] - A[0] * A[7]) / det; inv[8] = (A[0] * A[4] - A[1] * A[3]) / det;
                                        double dp[3] = {f[0] - M[3], f[1] - M[7], f[2] - M[11]};
                                        for (int r = 0; r < 3; r++) {
                                            double lp = inv[3 * r] * dp[0] + inv[3 * r + 1] * dp[1] + inv[3 * r + 2] * dp[2];
                                            double ln = inv[3 * r] * f[3] + inv[3 * r + 1] * f[4] + inv[3 * r + 2] * f[5];
                                            if (r == 2) { lp = -lp; ln = -ln; }
                                            pos[r] = same_p ? (int16_t)rd16(ov + 2 * r) : sat16((float)lp);
                                            nor[r] = same_n ? (int16_t)rd16(on + 2 * r) : sat16((float)ln);
                                        }
                                    }
                                    for (int x = 0; x < 3; x++) {
                                        if ((int16_t)rd16(ov + 2 * x) != pos[x]) { wr16(pv + 2 * x, (uint16_t)pos[x]); (*moved)++; }
                                        if ((int16_t)rd16(on + 2 * x) != nor[x]) wr16(pn + 2 * x, (uint16_t)nor[x]);
                                    }
                                    long u, v;
                                    if (mat[m].nt > 1) {     /* a sheet: the texture the disc's UVs are in, the NAB's counted in it */
                                        uint32_t nc = quad ? 4 : 3, tx = 10;
                                        for (uint32_t q = 0; q < mat[m].nt && q < 10 && tx == 10; q++) {
                                            int in = 1;
                                            for (uint32_t cc = 0; cc < nc && in; cc++) {
                                                int32_t ou = was[(size_t)(a - h) + 4 * cc], ov8 = was[(size_t)(a - h) + 4 * cc + 1];
                                                in = ou >= mat[m].rect[q][0] && ou <= mat[m].rect[q][0] + mat[m].rect[q][2] &&
                                                     ov8 >= mat[m].rect[q][1] && ov8 <= mat[m].rect[q][1] + mat[m].rect[q][3];
                                            }
                                            if (in) tx = q;
                                        }
                                        if (tx == 10) { *keptuv = 1; continue; }
                                        u = mat[m].rect[tx][0] + lrintf(f[7] * (float)mat[m].rect[tx][2]);
                                        v = mat[m].rect[tx][1] + lrintf(f[8] * (float)mat[m].rect[tx][3]);
                                    } else {
                                        uint32_t per = 64 * (16 / mat[m].bpp);   /* pixels a texture page is wide */
                                        u = lrintf(f[7] * (float)mat[m].w) - (long)per * (long)((pg & 0xF) - (mat[m].page & 0xF));
                                        v = lrintf(f[8] * (float)(mat[m].h ? mat[m].h : 256));
                                    }
                                    uint8_t nu = (uint8_t)(u < 0 ? 0 : u > 255 ? 255 : u), nv8 = (uint8_t)(v < 0 ? 0 : v > 255 ? 255 : v);
                                    size_t ua = (size_t)(a - h) + 4 * (size_t)corner;
                                    if (was[ua] != nu) a[4 * corner] = nu;
                                    if (was[ua + 1] != nv8) a[4 * corner + 1] = nv8;
                                }
                            }
                        }
                    }
                }
#undef HW
    *ntri = (int)used;
    uint32_t want = 0;
    for (uint32_t g = 0; g < ng; g++) if (grp[g].mat != ~0u && (parts & (grp[g].node ? 2 : 1))) want += grp[g].count;
    if (used != want) {
        memcpy(h, was, (size_t)hw * 4);
        free(was);
        return "it has more polygons than the PS1's model (for the bosses' skin they can be moved, not added or taken away)";
    }
    *changed = memcmp(h, was, (size_t)hw * 4) != 0;
    free(was);
    return NULL;
}

/* A boss's textures (types 31-33) from its NAB, into the 8-bit TIMs that
 * have copies of them elsewhere on the disc (modfs_boss.h): each part of
 * one in every copy (the boss's own are in its model: boss_images). A palette row only that texture is drawn with is
 * made again for it (median cut); a shared one keeps its colours, the
 * nearest taken. Pixels as the disc's stay as they were; see-through
 * where the NAB's alpha is 0. The number of textures written, or -1 and
 * why. */
static int boss_textures(int type, const uint8_t *nab, long n, const char **why) {
    /* the NAB's textures, by material and number */
    struct { uint32_t mat, tex, w, h; const uint8_t *px; } tx[64];
    uint32_t ntx = 0;
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;
    if (n < 8 || at + 4 > (uint64_t)n) { *why = "it's cut short"; return -1; }
    uint32_t nm = rd32(nab + at);
    at += 4;
    for (uint32_t m = 0; m < nm && m < 64; m++) {
        if (at + 352 > (uint64_t)n) { *why = "it's cut short"; return -1; }
        uint32_t nt = rd32(nab + at + 8);
        at += 352;
        for (uint32_t t = 0; t < nt; t++) {
            if (at + 16 > (uint64_t)n) { *why = "it's cut short"; return -1; }
            uint32_t size = rd32(nab + at), W = rd32(nab + at + 8), H = rd32(nab + at + 12);
            if (at + 16 + size > (uint64_t)n || (uint64_t)W * H * 2 > size) { *why = "it's cut short"; return -1; }
            if (ntx < 64) tx[ntx++] = (typeof(tx[0])){m, t, W, H, nab + at + 16};
            at += 16 + (uint64_t)size;
        }
    }
    /* the TIMs as they are on the disc (checked before any is changed) */
    int ok[sizeof modfs_boss / sizeof modfs_boss[0]];
    uint8_t *tim[sizeof modfs_boss / sizeof modfs_boss[0]];
    Touched *test = touch(disc_index("TEST.BIN"));
    for (size_t i = 0; i < sizeof modfs_boss / sizeof modfs_boss[0]; i++) {
        ok[i] = 0;
        tim[i] = NULL;
        if (modfs_boss[i].type != type || !test) continue;
        int fi = disc_index(modfs_boss[i].disc);
        Touched *t = touch(fi);
        uint32_t tab = archive_table(fi);
        if (!t || !tab) continue;
        uint32_t start = rd32(test->data + tab - TEST_BASE + 4 * modfs_boss[i].block) * 2048 + modfs_boss[i].offset;
        if (start + modfs_boss[i].len > t->nsec * 2048) continue;
        tim[i] = t->data + start;
        ok[i] = (uint32_t)mz_crc32(0, tim[i], modfs_boss[i].len) == modfs_boss[i].crc;
    }
    int done = 0;
    for (size_t i = 0; i < sizeof modfs_boss / sizeof modfs_boss[0]; i++) {
        if (!ok[i]) continue;
        const typeof(tx[0]) *src = NULL;
        for (uint32_t k = 0; k < ntx; k++)
            if (tx[k].mat == (uint32_t)modfs_boss[i].mat && tx[k].tex == (uint32_t)modfs_boss[i].tex) src = &tx[k];
        uint32_t w = modfs_boss[i].w, h = modfs_boss[i].h;
        if (!src || src->w < w || src->h < h) continue;
        uint8_t *t8 = tim[i];
        uint32_t cw = rd16(t8 + 16), chs = rd16(t8 + 18);
        uint8_t *row = t8 + 20 + 2 * (size_t)cw * modfs_boss[i].row, *pix = t8 + 8 + rd32(t8 + 8);
        uint32_t tw = rd16(pix + 8) * 2, th = rd16(pix + 10);
        if (cw != 256 || modfs_boss[i].row >= chs || modfs_boss[i].x + w > tw || modfs_boss[i].y + h > th) continue;
        uint8_t *idx = pix + 12;
        /* the NAB's pixels (15-bit, 0 see-through), and whether any differs from the disc's */
        uint16_t *col = malloc((size_t)w * h * 2);
        if (!col) continue;
        int differs = 0;
        size_t opaque = 0;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                uint16_t p = (uint16_t)rd16(src->px + 2 * ((size_t)y * src->w + x));
                uint16_t v = (uint16_t)(((p >> 10) & 31) | ((p >> 5) & 31) << 5 | (p & 31) << 10);
                uint8_t was = idx[(size_t)(modfs_boss[i].y + y) * tw + modfs_boss[i].x + x];
                uint16_t wc = (uint16_t)(rd16(row + 2 * was) & 0x7FFF);
                if (v == wc) { col[y * w + x] = 0x8000 | was; continue; }   /* as it was: its own index (top bit) */
                if (!(p & 0x8000)) v = 0;
                else if (!v) v = 0x0400;
                col[y * w + x] = v;
                differs = 1;
                opaque += v != 0;
            }
        if (!differs) { free(col); continue; }
        uint16_t pal[256];
        for (int k = 0; k < 256; k++) pal[k] = (uint16_t)(rd16(row + 2 * k) & 0x7FFF);
        if (modfs_boss[i].own) {                 /* a palette made for the new picture: index 0 see-through */
            uint16_t *list = malloc((opaque + w * h + 1) * 2);
            size_t m = 0;
            for (uint32_t k = 0; list && k < w * h; k++) {
                uint16_t c = col[k] & 0x8000 ? pal[col[k] & 0xFF] : col[k];
                if (c) list[m++] = c;
            }
            int got = list && m ? median_cut(list, m, pal + 1, 255) : 0;
            free(list);
            pal[0] = 0;
            for (int k = 1 + got; k < 256; k++) pal[k] = 0;
            uint16_t stp = 0;
            for (int k = 0; k < 256; k++) if (rd16(row + 2 * k) & 0x8000) stp = 0x8000;
            for (uint32_t k = 0; k < w * h; k++)
                if (col[k] & 0x8000) col[k] = (uint16_t)(rd16(row + 2 * (col[k] & 0xFF)) & 0x7FFF);   /* (by colour now) */
            for (int k = 0; k < 256; k++) wr16(row + 2 * k, pal[k] ? (uint16_t)(pal[k] | stp) : 0);
        }
        int clear = -1;
        for (int k = 0; k < 256 && clear < 0; k++) if (!pal[k]) clear = k;
        for (uint32_t y = 0; y < h; y++)
            for (uint32_t x = 0; x < w; x++) {
                uint16_t c = col[y * w + x];
                int ix = c & 0x8000 ? (int)(c & 0xFF) : c ? nearest(pal, 256, c) : (clear >= 0 ? clear : nearest(pal, 256, 0x0400));
                idx[(size_t)(modfs_boss[i].y + y) * tw + modfs_boss[i].x + x] = (uint8_t)ix;
            }
        free(col);
        done++;
    }
    return done;
}

/* Whether the NAB has the boss's polygons, material by material, skin and
 * each bone's: 0 the same; bit 0, another number of the skin's; bit 1,
 * of the bones' (those are built again). */
static int boss_same_layout(const uint8_t *nab, long n, const uint8_t *h, uint32_t hw) {
    static NabMat mat[8];
    static NabGroup grp[256];
    uint32_t nm = 0, ng = 0;
    if (nab_layout(nab, n, mat, &nm, grp, &ng)) return 0;   /* (boss_shape says why) */
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    static uint32_t ps1[8][65], pc[8][65];
    memset(ps1, 0, sizeof ps1);
    memset(pc, 0, sizeof pc);
    for (uint32_t bi = 0; bi < HW(3) && bi < 64; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64; p = HW(p), guard++) {
            uint32_t nt = HW(p + 2) & 0xFFFF, base = HW(HW(p + 1) + 1) & 0x7FFFFFFF;
            for (uint32_t t = 0; t < nt && t < 4096; t++) {
                uint32_t ty = HW(p + 3 + 3 * t), sz = HW(p + 4 + 3 * t), idx = HW(p + 5 + 3 * t);
                if (((ty & 0xFF) != 0x0D && (ty & 0xFF) != 0x15) || ty & 0xFE000000u) continue;
                int quad = (ty & 0x10) != 0, shared = (ty & 0x1000000) != 0;
                for (uint32_t k = 0; k < ((sz >> 16) & 0x7FFF); k++) {
                    uint32_t pg = (HW(base + idx + k * (quad ? 7 : 6) + 1) >> 16) & 0x1F, best = nm;
                    for (uint32_t q = 0; q < nm; q++)
                        if ((mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF) &&
                            (best == nm || (mat[q].page & 0xF) > (mat[best].page & 0xF)))
                            best = q;
                    if (best < nm) ps1[best][shared ? 0 : bi] += quad ? 2 : 1;
                }
            }
        }
#undef HW
    for (uint32_t g = 0; g < ng; g++)
        if (grp[g].mat != ~0u && grp[g].node < 65 && grp[g].mat < 8) pc[grp[g].mat][grp[g].node] += grp[g].count;
    int diff = 0;
    for (uint32_t m = 0; m < 8; m++)
        for (uint32_t b = 0; b < 65; b++)
            if (ps1[m][b] != pc[m][b]) diff |= b ? 2 : 1;
    return diff;
}

/* The bones' polygons of a boss built again from its NAB: nab_rebuild on a
 * model made of the HMD and its bones' vertices and normals (the first
 * bone unit's header: 4, polygons, vertices, normals, coordinates); the
 * new vertices and normals (the disc's first) go after the new HMD's end
 * and the header points at them (a header outside the HMD's header
 * section isn't mapped by the game, and its units aren't drawn). The
 * new HMD (malloc'd) and its words, or NULL and why. */
static uint8_t *boss_rebuild(const uint8_t *nab, long n, const uint8_t *h, uint32_t hw, uint32_t *outw, int *ntri, const char **why) {
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    uint32_t hdr = 0;
    for (uint32_t bi = 0; bi < HW(3) && bi < 64 && !hdr; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64 && !hdr; p = HW(p), guard++) {
            uint32_t ty = HW(p + 3);
            if (((ty & 0xFF) == 0x0D || (ty & 0xFF) == 0x15) && !(ty & 0xFF000000u) && HW(HW(p + 1)) == 4) hdr = HW(p + 1);
        }
    *why = "the disc's model isn't laid out as expected";
    if (!hdr) return NULL;
    uint32_t vs = HW(hdr + 2) & 0x7FFFFFFF, ns = HW(hdr + 3) & 0x7FFFFFFF, ve = hw, ne = hw;
    for (uint32_t w = HW(2); w < hw && w < HW(2) + 256; w++) {   /* each section ends where the next one starts */
        uint32_t v = HW(w);
        if (!(v & 0x80000000u) || v == 0xFFFFFFFFu) continue;
        v &= 0x7FFFFFFF;
        if (v > vs && v < ve) ve = v;
        if (v > ns && v < ne) ne = v;
    }
    if (vs >= ve || ns >= ne || ve > hw || ne > hw) return NULL;
    uint32_t nS = (ve - vs) / 2, nN = (ne - ns) / 2, b0n = 8 + 8 * (nS + nN), nb = 12 + b0n + hw * 4;
    uint8_t *B = malloc(nb);
    if (!B) { *why = "out of memory"; return NULL; }
    wr32(B, 2); wr32(B + 4, 12); wr32(B + 8, 12 + b0n);
    wr32(B + 12, 2); wr32(B + 16, 2 + nS * 2);
    memcpy(B + 20, h + 4 * (size_t)vs, (size_t)nS * 8);
    memcpy(B + 20 + (size_t)nS * 8, h + 4 * (size_t)ns, (size_t)nN * 8);
    memcpy(B + 12 + b0n, h, (size_t)hw * 4);
    uint32_t nn = 0;
    uint8_t *RB = nab_rebuild(nab, n, B, nb, 0, &nn, ntri, why);
    free(B);
    if (!RB) return NULL;
    uint32_t o0 = rd32(RB + 4), o1 = rd32(RB + 8), vb = o0 + rd32(RB + o0) * 4, nb2 = o0 + rd32(RB + o0 + 4) * 4;
    uint32_t cs = (nb2 - vb) / 8, cn = (o1 - nb2) / 8, hw2 = (nn - o1) / 4;
    uint32_t total = hw2 + 2 * cs + 2 * cn;
    uint8_t *H = malloc((size_t)total * 4);
    if (!H) { free(RB); *why = "out of memory"; return NULL; }
    memcpy(H, RB + o1, (size_t)hw2 * 4);
    uint32_t sv = hw2, sn = hw2 + 2 * cs;
    memcpy(H + 4 * (size_t)sv, RB + vb, (size_t)cs * 8);
    memcpy(H + 4 * (size_t)sn, RB + nb2, (size_t)cn * 8);
    /* the header itself points at them (the disc's vertices and normals come first there, so the
     * bones not built again keep theirs); a header put elsewhere isn't mapped by the game */
    wr32(H + 4 * (size_t)(hdr + 2), 0x80000000u | sv);
    wr32(H + 4 * (size_t)(hdr + 3), 0x80000000u | sn);
    free(RB);
#undef HW
    *outw = total;
    *why = NULL;
    return H;
}

/* A boss's textures are pictures inside its own HMD (units of type
 * 0x2000001: the VRAM rectangle, where its 8-bit pixels are, where its
 * palette goes in VRAM and where it is), uploaded as the boss loads. The
 * NAB has each as a texture of its material (a sheet's at their place,
 * stretched to a power of two, nearest pixel). Each changed picture is
 * written back; the pictures whose palettes go to the same place in VRAM
 * (type 33's 17) get one palette made for all of them, the others one
 * each. Pixels as they were keep their index. The number written, or -1
 * and why. */
static int boss_images(const uint8_t *nab, long n, uint8_t *h, uint32_t hw, const char **why) {
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    struct { uint32_t x, y, w, h, pix, cx, cy, clut; uint16_t *col; int changed; } im[64];
    uint32_t ni = 0;
    for (uint32_t bi = 0; bi < HW(3) && bi < 64; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64; p = HW(p), guard++) {
            uint32_t nt = HW(p + 2) & 0xFFFF, hdr = HW(p + 1), q = p + 3;
            for (uint32_t t = 0; t < nt && t < 256; t++) {
                uint32_t ty = HW(q);
                if (ty >> 24 != 2) break;
                if ((ty & 1) && ni < 64) {
                    uint32_t yx = HW(q + 2), hwd = HW(q + 3), cyx = HW(q + 5);
                    im[ni].x = yx & 0xFFFF; im[ni].y = yx >> 16; im[ni].w = (hwd & 0xFFFF) * 2; im[ni].h = hwd >> 16;
                    im[ni].pix = (HW(hdr + 1) & 0x7FFFFFFF) + HW(q + 4);
                    im[ni].cx = cyx & 0xFFFF; im[ni].cy = cyx >> 16;
                    im[ni].clut = (HW(hdr + 2) & 0x7FFFFFFF) + HW(q + 7);
                    im[ni].col = NULL;
                    im[ni].changed = 0;
                    if ((uint64_t)im[ni].pix * 4 + (uint64_t)im[ni].w * im[ni].h <= (uint64_t)hw * 4 && (uint64_t)im[ni].clut * 4 + 512 <= (uint64_t)hw * 4) ni++;
                }
                q += ty & 1 ? 8 : 5;
            }
        }
#undef HW
    if (!ni) return 0;
    /* the NAB's textures: each to the picture at its place in VRAM */
    uint64_t at = 8 + (uint64_t)rd32(nab) * 36;
    if (n < 8 || at + 4 > (uint64_t)n) { *why = "it's cut short"; return -1; }
    uint32_t nm = rd32(nab + at);
    at += 4;
    int done = 0;
    for (uint32_t m = 0; m < nm && m < 64; m++) {
        if (at + 352 > (uint64_t)n) { *why = "it's cut short"; goto fail; }
        const uint8_t *rec = nab + at;
        uint32_t nt = rd32(rec + 8), page = rd32(rec + 4) & 0x1F;
        at += 352;
        for (uint32_t t = 0; t < nt; t++) {
            if (at + 16 > (uint64_t)n) { *why = "it's cut short"; goto fail; }
            uint32_t size = rd32(nab + at), W = rd32(nab + at + 8), H = rd32(nab + at + 12);
            const uint8_t *px = nab + at + 16;
            at += 16 + (uint64_t)size;
            if (at > (uint64_t)n || (uint64_t)W * H * 2 > size) { *why = "it's cut short"; goto fail; }
            uint32_t vx, vy, rw, rh;
            if (nt > 1 && t < 10) { vx = rd16(rec + 12 + 2 * t); vy = rd16(rec + 52 + 2 * t); rw = rd32(rec + 192 + 4 * t); rh = rd32(rec + 272 + 4 * t); }
            else { vx = rd32(rec + 12); vy = ((page >> 4) & 1) * 256; rw = W; rh = H; }
            for (uint32_t k = 0; k < ni; k++) {
                if (im[k].x != vx || im[k].y != vy || !rw || !rh || rw > im[k].w || rh > im[k].h) continue;
                const uint8_t *ix = h + 4 * (size_t)im[k].pix, *cl = h + 4 * (size_t)im[k].clut;
                if (!im[k].col && !(im[k].col = malloc((size_t)im[k].w * im[k].h * 2))) continue;
                for (uint32_t y = 0; y < im[k].h; y++)
                    for (uint32_t x = 0; x < im[k].w; x++) {
                        uint8_t was = ix[(size_t)y * im[k].w + x];
                        uint16_t c = 0x8000 | was;   /* (as it was: its index) */
                        if (x < rw && y < rh) {
                            uint16_t p = (uint16_t)rd16(px + 2 * ((size_t)((y * H + rh - 1) / rh) * W + (x * W + rw - 1) / rw));
                            uint16_t v = (uint16_t)(((p >> 10) & 31) | ((p >> 5) & 31) << 5 | (p & 31) << 10);
                            if (v != (rd16(cl + 2 * was) & 0x7FFF)) {
                                c = !(p & 0x8000) ? 0 : v ? v : 0x0400;
                                im[k].changed = 1;
                            }
                        }
                        im[k].col[(size_t)y * im[k].w + x] = c;
                    }
            }
        }
    }
    /* each palette place in VRAM: one palette for its pictures, if any of them changed */
    for (uint32_t k = 0; k < ni; k++) {
        if (!im[k].col) continue;
        int any = 0;
        size_t total = 0;
        for (uint32_t j = 0; j < ni; j++)
            if (im[j].cx == im[k].cx && im[j].cy == im[k].cy) { any |= im[j].changed; total += (size_t)im[j].w * im[j].h; }
        if (!any) continue;
        uint16_t *list = malloc(total * 2), pal[256];
        size_t cnt = 0;
        for (uint32_t j = 0; list && j < ni; j++) {
            if (im[j].cx != im[k].cx || im[j].cy != im[k].cy) continue;
            const uint8_t *ix = h + 4 * (size_t)im[j].pix, *cl = h + 4 * (size_t)im[j].clut;
            for (size_t q = 0; q < (size_t)im[j].w * im[j].h; q++) {
                uint16_t c = im[j].col ? im[j].col[q] : (uint16_t)(0x8000 | ix[q]);
                if (c & 0x8000) c = (uint16_t)(rd16(cl + 2 * (c & 0xFF)) & 0x7FFF);
                if (im[j].col) im[j].col[q] = c;    /* (by colour from here) */
                if (c) list[cnt++] = c;
            }
            if (!im[j].col) {                    /* a picture the NAB didn't touch, sharing the palette: by colour too */
                im[j].col = malloc((size_t)im[j].w * im[j].h * 2);
                for (size_t q = 0; im[j].col && q < (size_t)im[j].w * im[j].h; q++)
                    im[j].col[q] = (uint16_t)(rd16(cl + 2 * ix[q]) & 0x7FFF);
            }
        }
        int got = list && cnt ? median_cut(list, cnt, pal + 1, 255) : 0;
        free(list);
        pal[0] = 0;
        for (int q = 1 + got; q < 256; q++) pal[q] = 0;
        uint16_t stp = 0;
        for (int q = 0; q < 256; q++) if (rd16(h + 4 * (size_t)im[k].clut + 2 * q) & 0x8000) stp = 0x8000;
        for (uint32_t j = 0; j < ni; j++) {
            if (im[j].cx != im[k].cx || im[j].cy != im[k].cy || !im[j].col) continue;
            uint8_t *ix = h + 4 * (size_t)im[j].pix, *cl = h + 4 * (size_t)im[j].clut;
            for (int q = 0; q < 256; q++) wr16(cl + 2 * q, pal[q] ? (uint16_t)(pal[q] | stp) : 0);
            for (size_t q = 0; q < (size_t)im[j].w * im[j].h; q++)
                ix[q] = (uint8_t)(im[j].col[q] ? nearest(pal, 256, im[j].col[q]) : 0);
            free(im[j].col);
            im[j].col = NULL;
            done++;
        }
    }
    for (uint32_t k = 0; k < ni; k++) free(im[k].col);
    return done;
fail:
    for (uint32_t k = 0; k < ni; k++) free(im[k].col);
    return -1;
}

/* A boss's skin built again from its NAB (polygons added or taken away).
 * The skin is kept bone by bone: each bone's unit has a 7-word packet
 * (type 0x1000000): its vertices (count, first in the source, first in
 * the buffer the game turns them into) and normals (the same), and the
 * skin's polygons (types 0x100000d, 0x1000015, in one unit) use the
 * buffers' indices. Its header: 5, polygons, vertex source, vertex
 * buffer, normal source, normal buffer. The NAB's skin triangles (group
 * node 0) have each corner's bone and model-space place: back through
 * the bone's rest matrix (as boss_shape), shared corners once per bone.
 * The new sections, packets and unit go after the HMD's end; the bones'
 * packets and the header are changed to match. A material that is a
 * sheet of textures can't take new skin polygons (which texture a new
 * one is on isn't known). The new HMD and its words, or NULL and why. */
static uint8_t *boss_skin(const uint8_t *nab, long n, const uint8_t *h, uint32_t hw, uint32_t *outw, int *ntri, const char **why) {
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    static NabMat mat[8];
    static NabGroup grp[256];
    uint32_t nm = 0, ng = 0;
    if ((*why = nab_layout(nab, n, mat, &nm, grp, &ng))) return NULL;
    /* the NAB's parts after the groups: per-vertex data, nodes */
    uint32_t nv = rd32(nab), nf = rd32(nab + 4);
    uint64_t at = 8 + (uint64_t)nv * 36;
    at += 4;
    for (uint32_t m = 0; m < nm; m++) {
        uint32_t nt = rd32(nab + at + 8);
        at += 352;
        for (uint32_t t = 0; t < nt; t++) at += 16 + (uint64_t)rd32(nab + at);
        if (nt > 1) {
            int uses = 0;
            for (uint32_t g = 0; g < ng; g++) if (grp[g].mat == m && grp[g].node == 0) uses = 1;
            if (uses) { *why = "a material of its skin is a sheet of textures: new skin polygons can't be put on it"; return NULL; }
        }
    }
    at += 4;
    for (uint32_t g = 0; g < ng; g++) at += 24 + (rd32(nab + at) ? 4 : 0) + 4;
    uint64_t aux = at;
    at += (uint64_t)nv * 60 + (uint64_t)nf * 20;
    uint32_t nq = rd32(nab + at);
    at += 4;
    for (uint32_t q = 0; q < nq && q < 256; q++) at += 96 + (uint64_t)rd32(nab + at + 12) * 8;
    at += 4;
    for (int l = 0; l < 2; l++) {
        uint32_t c = rd32(nab + at);
        at += 4;
        for (uint32_t e = 0; e < c && e < 65536; e++) at += 12 + (uint64_t)rd32(nab + at + 4) * 12;
    }
    at += 12;
    uint32_t nnode = rd32(nab + at) + 1;
    uint64_t nodes = at + 4;
    if (nodes + (uint64_t)nnode * 264 > (uint64_t)n) { *why = "it's cut short"; return NULL; }
    /* the skin's unit and header, and the bones' packets */
    uint32_t sb = 0, su = 0, bones[64], bunit[64], nb = 0;
    for (uint32_t bi = 0; bi < HW(3) && bi < 64; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64; p = HW(p), guard++) {
            uint32_t ty = HW(p + 3);
            if (ty == 0x1000000u && nb < 64) { bones[nb] = bi; bunit[nb] = p; nb++; }
            if ((ty & 0x1000000) && !(ty & 0xFE000000u) && ((ty & 0xFF) == 0x0D || (ty & 0xFF) == 0x15)) { sb = bi; su = p; }
        }
    *why = "the disc's model has no skin laid out as expected";
    if (!su || !nb) return NULL;
    uint32_t hb = HW(su + 1);
    if (HW(hb) != 5) return NULL;
    /* the skin's polygons' look (CLUT, texture page bits) for each material, from the disc's */
    uint32_t tsb[8] = {0}, cba[8] = {0}, sbase = HW(hb + 1) & 0x7FFFFFFF;
    for (uint32_t t = 0; t < (HW(su + 2) & 0xFFFF); t++) {
        uint32_t idx = HW(su + 5 + 3 * t), w0 = HW(sbase + idx), w1 = HW(sbase + idx + 1), pg = (w1 >> 16) & 0x1F;
        for (uint32_t q = 0; q < nm; q++)
            if (!tsb[q] && (mat[q].page & 0x10) == (pg & 0x10) && (mat[q].page & 0xF) <= (pg & 0xF)) { tsb[q] = w1 >> 16; cba[q] = w0 >> 16; }
    }
    /* vertices and normals by bone (each bone's own, shared corners once) */
    uint32_t total = 0;
    for (uint32_t g = 0; g < ng; g++) if (grp[g].mat != ~0u && grp[g].node == 0) total += grp[g].count;
    if (!total) { *why = "it has no skin"; return NULL; }
    int16_t (*vx)[4] = calloc((size_t)total * 3, 8), (*nx)[4] = calloc((size_t)total * 3, 8);
    uint32_t *vb = calloc((size_t)total * 3, 4), *nbn = calloc((size_t)total * 3, 4), *cv = malloc((size_t)total * 3 * 4), *cn = malloc((size_t)total * 3 * 4);
    uint32_t nvx = 0, nnx = 0;
    if (!vx || !nx || !vb || !nbn || !cv || !cn) { free(vx); free(nx); free(vb); free(nbn); free(cv); free(cn); *why = "out of memory"; return NULL; }
    uint32_t k = 0;
    for (uint32_t g = 0; g < ng; g++) {
        if (grp[g].mat == ~0u || grp[g].node != 0) continue;
        for (uint32_t t = grp[g].start; t < grp[g].start + grp[g].count; t++)
            for (int c = 0; c < 3; c++, k++) {
                uint32_t ci = 3 * t + (uint32_t)c, bone = rd32(nab + aux + (uint64_t)ci * 60) >> 16, bk = 0;
                while (bk < nb && bones[bk] != bone) bk++;
                if (bk == nb || bone >= nnode) {
                    free(vx); free(nx); free(vb); free(nbn); free(cv); free(cn);
                    *why = "a skin vertex is on a bone without skin in the PS1's model";
                    return NULL;
                }
                float f[9], M[16];
                memcpy(f, nab + 8 + (size_t)ci * 36, sizeof f);
                memcpy(M, nab + nodes + (uint64_t)bone * 264 + 128, sizeof M);
                double A[9], inv[9];
                for (int x = 0; x < 9; x++) A[x] = M[4 * (x / 3) + x % 3];
                double det = A[0] * (A[4] * A[8] - A[5] * A[7]) - A[1] * (A[3] * A[8] - A[5] * A[6]) + A[2] * (A[3] * A[7] - A[4] * A[6]);
                if (fabs(det) < 1e-9) det = 1e-9;
                inv[0] = (A[4] * A[8] - A[5] * A[7]) / det; inv[1] = (A[2] * A[7] - A[1] * A[8]) / det; inv[2] = (A[1] * A[5] - A[2] * A[4]) / det;
                inv[3] = (A[5] * A[6] - A[3] * A[8]) / det; inv[4] = (A[0] * A[8] - A[2] * A[6]) / det; inv[5] = (A[2] * A[3] - A[0] * A[5]) / det;
                inv[6] = (A[3] * A[7] - A[4] * A[6]) / det; inv[7] = (A[1] * A[6] - A[0] * A[7]) / det; inv[8] = (A[0] * A[4] - A[1] * A[3]) / det;
                double dp[3] = {f[0] - M[3], f[1] - M[7], f[2] - M[11]};
                int16_t lp[3], ln[3];
                for (int r = 0; r < 3; r++) {
                    double a = inv[3 * r] * dp[0] + inv[3 * r + 1] * dp[1] + inv[3 * r + 2] * dp[2];
                    double b = inv[3 * r] * f[3] + inv[3 * r + 1] * f[4] + inv[3 * r + 2] * f[5];
                    if (r == 2) { a = -a; b = -b; }
                    lp[r] = sat16((float)a);
                    ln[r] = sat16((float)b);
                }
                uint32_t q;
                for (q = 0; q < nvx; q++) if (vb[q] == bk && !memcmp(vx[q], lp, 6)) break;
                if (q == nvx) { memcpy(vx[q], lp, 6); vx[q][3] = 0; vb[q] = bk; nvx++; }
                cv[k] = q;
                for (q = 0; q < nnx; q++) if (nbn[q] == bk && !memcmp(nx[q], ln, 6)) break;
                if (q == nnx) { memcpy(nx[q], ln, 6); nx[q][3] = 0; nbn[q] = bk; nnx++; }
                cn[k] = q;
            }
    }
    /* their order: bone by bone, as the bones' packets go */
    uint32_t *vpos = malloc(nvx * 4), *npos = malloc(nnx * 4), vstart[64], vcount[64], nstart[64], ncount[64], o = 0;
    for (uint32_t b = 0; b < nb; b++) {
        vstart[b] = o;
        for (uint32_t q = 0; q < nvx; q++) if (vb[q] == b) vpos[q] = o++;
        vcount[b] = o - vstart[b];
    }
    o = 0;
    for (uint32_t b = 0; b < nb; b++) {
        nstart[b] = o;
        for (uint32_t q = 0; q < nnx; q++) if (nbn[q] == b) npos[q] = o++;
        ncount[b] = o - nstart[b];
    }
    /* the new HMD: the disc's, then vertex source and buffer, normal source and buffer, packets, the skin's unit */
    uint32_t poly_w = total * 7, unit_w = 3 + 3 * total;
    uint32_t size = hw + 4 * nvx + 4 * nnx + poly_w + unit_w;
    uint8_t *H = calloc(size, 4);
    if (!H || !vpos || !npos) { free(H); free(vpos); free(npos); free(vx); free(nx); free(vb); free(nbn); free(cv); free(cn); *why = "out of memory"; return NULL; }
    memcpy(H, h, (size_t)hw * 4);
    uint32_t svs = hw, svb = svs + 2 * nvx, sns = svb + 2 * nvx, snb = sns + 2 * nnx, sp = snb + 2 * nnx, pw = 0;
    for (uint32_t q = 0; q < nvx; q++) memcpy(H + 4 * (size_t)(svs + 2 * vpos[q]), vx[q], 8);
    for (uint32_t q = 0; q < nnx; q++) memcpy(H + 4 * (size_t)(sns + 2 * npos[q]), nx[q], 8);
    /* the packets: two triangles that were a quad (a,b,c and d,c,b) a GT4 again */
    uint32_t ent[512][3], ne = 0;
    k = 0;
    *ntri = 0;
    for (uint32_t g = 0; g < ng; g++) {
        if (grp[g].mat == ~0u || grp[g].node != 0) continue;
        const NabMat *mm = &mat[grp[g].mat];
        for (uint32_t t = grp[g].start; t < grp[g].start + grp[g].count;) {
            const uint8_t *ta = nab + 8 + (size_t)3 * t * 36, *tb = ta + 3 * 36;
            uint32_t kb = k;
            int quad = t + 1 < grp[g].start + grp[g].count && !memcmp(tb + 36, ta + 72, 36) && !memcmp(tb + 72, ta + 36, 36) &&
                       cv[kb + 4] == cv[kb + 2] && cv[kb + 5] == cv[kb + 1];
            int nc = quad ? 4 : 3;
            uint32_t kc[4] = {kb, kb + 1, kb + 2, kb + 3};
            long u[4], v[4], lo = 1 << 30;
            for (int c = 0; c < nc; c++) {
                float f[9];
                memcpy(f, nab + 8 + (size_t)(c < 3 ? 3 * t + (uint32_t)c : 3 * t + 3) * 36, sizeof f);
                u[c] = lrintf(f[7] * (float)mm->w);
                v[c] = lrintf(f[8] * (float)(mm->h ? mm->h : 256));
                if (u[c] < lo) lo = u[c];
            }
            long off = lo < 0 ? 0 : lo / (long)mm->per;
            if ((mm->page & 0xF) + off > 15) off = 15 - (mm->page & 0xF);
            uint8_t *pk = H + 4 * (size_t)(sp + pw);
            for (int c = 0; c < nc; c++) {
                long uu = u[c] - (long)mm->per * off;
                pk[4 * c] = (uint8_t)(uu < 0 ? 0 : uu > 255 ? 255 : uu);
                pk[4 * c + 1] = (uint8_t)(v[c] < 0 ? 0 : v[c] > 255 ? 255 : v[c]);
            }
            wr16(pk + 2, (uint16_t)cba[grp[g].mat]);
            wr16(pk + 6, (uint16_t)((tsb[grp[g].mat] & ~0x1Fu) | (mm->page & 0x10) | ((mm->page & 0xF) + (uint32_t)off)));
            if (quad) {
                wr16(pk + 10, (uint16_t)npos[cn[kc[0]]]); wr16(pk + 14, (uint16_t)vpos[cv[kc[0]]]);
                wr16(pk + 16, (uint16_t)npos[cn[kc[1]]]); wr16(pk + 18, (uint16_t)vpos[cv[kc[1]]]);
                wr16(pk + 20, (uint16_t)npos[cn[kc[2]]]); wr16(pk + 22, (uint16_t)vpos[cv[kc[2]]]);
                wr16(pk + 24, (uint16_t)npos[cn[kc[3]]]); wr16(pk + 26, (uint16_t)vpos[cv[kc[3]]]);
            } else
                for (int c = 0; c < 3; c++) { wr16(pk + 12 + 4 * c, (uint16_t)npos[cn[kc[c]]]); wr16(pk + 14 + 4 * c, (uint16_t)vpos[cv[kc[c]]]); }
            uint32_t ty = quad ? 0x1000015u : 0x100000Du, ppw = quad ? 7 : 6;
            if (ne && ent[ne - 1][0] == ty && ent[ne - 1][1] + ent[ne - 1][2] * ppw == pw) ent[ne - 1][2]++;
            else if (ne < 512) { ent[ne][0] = ty; ent[ne][1] = pw; ent[ne][2] = 1; ne++; }
            pw += ppw;
            *ntri += quad ? 2 : 1;
            k += quad ? 6 : 3;
            t += quad ? 2 : 1;
        }
    }
    /* the skin's unit, the header and the bones' packets */
    uint32_t un = sp + pw;
    wr32(H + 4 * (size_t)un, HW(su));
    wr32(H + 4 * (size_t)(un + 1), hb);
    wr32(H + 4 * (size_t)(un + 2), 0x80000000u | ne);
    for (uint32_t q = 0; q < ne; q++) {
        wr32(H + 4 * (size_t)(un + 3 + 3 * q), ent[q][0]);
        wr32(H + 4 * (size_t)(un + 4 + 3 * q), 0x80000002u | ent[q][2] << 16);
        wr32(H + 4 * (size_t)(un + 5 + 3 * q), ent[q][1]);
    }
    uint32_t end = un + 3 + 3 * ne;
    wr32(H + 4 * (4 + sb), un);
    wr32(H + 4 * (size_t)(hb + 1), 0x80000000u | sp);
    wr32(H + 4 * (size_t)(hb + 2), 0x80000000u | svs);
    wr32(H + 4 * (size_t)(hb + 3), 0x80000000u | svb);
    wr32(H + 4 * (size_t)(hb + 4), 0x80000000u | sns);
    wr32(H + 4 * (size_t)(hb + 5), 0x80000000u | snb);
    for (uint32_t b = 0; b < nb; b++) {
        uint8_t *e = H + 4 * (size_t)(bunit[b] + 5);
        wr32(e, vcount[b]); wr32(e + 4, vstart[b]); wr32(e + 8, vstart[b]);
        wr32(e + 12, ncount[b]); wr32(e + 16, nstart[b]); wr32(e + 20, nstart[b]);
    }
    free(vpos); free(npos); free(vx); free(nx); free(vb); free(nbn); free(cv); free(cn);
#undef HW
    *outw = end;
    *why = NULL;
    return H;
}

/* How much a boss (one in its room) can grow by when its animations are
 * laid out again after its model's end (the disc's stay where they were). */
#define BOSS_GROWTH (modmem_enabled() ? (RAM_SIZE - RAM_GAME) : (160u * 1024))

/* Types 31 to 33 (the bosses loaded whole: EV_OBJ.DAT 0x190 to 0x192)
 * are one HMD, not packed, with their animation inside: its script and
 * keys where the animation unit's header says (its 4th and 5th words).
 * The NAC's go in place (as many commands and keys as the PS1's), each
 * key where the disc's commands that take it point, 5 words (T, R) or 6
 * (T, R, then a scale of which the PC keeps the 2nd and 3rd, swapped).
 * From the NAB its shape (boss_shape) and animation tables; their
 * textures are the room's. */
static void enemy_whole(Enemy *e) {
    int fi = disc_index("EV_OBJ.DAT");
    Touched *test = touch(disc_index("TEST.BIN")), *t = touch(fi);
    uint32_t blk = 0x190 + (uint32_t)(e->type - 31);
    uint32_t start = test && t ? rd32(test->data + 0x800A195Cu - TEST_BASE + 4 * blk) * 2048 : 0;
    uint32_t end = test && t ? rd32(test->data + 0x800A195Cu - TEST_BASE + 4 * blk + 4) * 2048 : 0;
    uint8_t *h = t ? t->data + start : NULL;
    uint32_t hw = end > start ? (end - start) / 4 : 0;
#define HW(i) ((uint32_t)(i) < hw ? rd32(h + 4 * (uint32_t)(i)) : 0xFFFFFFFFu)
    uint32_t unit = 0;
    for (uint32_t bi = 0; h && bi < HW(3) && bi < 64 && !unit; bi++)
        for (uint32_t p = HW(4 + bi), guard = 0; p && p != 0xFFFFFFFFu && guard < 64 && !unit; p = HW(p), guard++)
            if (HW(p + 3) == 0x3800000u) unit = p;
    if (!h || (HW(0) & ~0x100u) != 0x50 || !unit) {
        if (e->data[1]) { note("mods: %s: the disc's enemy type %d isn't laid out as expected: not used", e->what[1], e->type); nused--; }
        if (e->data[0]) { note("mods: %s: the disc's enemy type %d isn't laid out as expected: not used", e->what[0], e->type); nused--; }
        if (e->data[2]) { note("mods: %s: enemy type %d's textures are the room's: change those: not used", e->what[2], e->type); nused--; }
        return;
    }
    uint32_t hdr = HW(unit + 1), sc = HW(hdr + 3) & 0x7FFFFFFF, ky = HW(hdr + 4) & 0x7FFFFFFF;
    uint8_t *extra = NULL;                       /* script and keys laid out again, to go after the HMD's end */
    uint32_t extra_w = 0, extra_sc = 0;
    if (e->data[1]) {
        const uint8_t *nac = e->data[1];
        long n = e->n[1];
        uint32_t nk = n >= 8 ? rd32(nac) : 0, nc = 0;
        uint32_t size = e->type == 31 ? 5 : 6;  /* a key's words: T, R (and a scale, for 32 and 33) */
        const char *why = NULL;
        if (!nk || (uint64_t)nk * 112 + 8 > (uint64_t)n) why = "it's cut short";
        else {
            nc = rd32(nac + 4 + (size_t)nk * 112);
            if (4 + (uint64_t)nk * 112 + 4 + (uint64_t)nc * 8 != (uint64_t)n) why = "it's cut short or has more after it";
            else if (ky >= hw || sc > ky) why = "the disc's animations aren't laid out as expected";
        }
        const uint8_t *cmd = nac + 4 + (size_t)nk * 112 + 4;
        /* in place when the script is as long as the disc's and each key the NAC's commands take is one
         * the disc's commands at the same places take; else laid out again */
        uint32_t *at = why ? NULL : malloc(nk * 4);
        if (!why && !at) why = "out of memory";
        int again = 0;
        if (!why) {
            memset(at, 0xFF, nk * 4);
            again = nc != ky - sc;
            for (uint32_t i = 1; i < nc && !again; i++) {
                uint32_t key = rd32(cmd + 8 * i + 4);
                if (key == 0xFFFFFFFFu) continue;
                if (key >= nk) { again = 1; break; }
                at[key] = HW(sc + i) & 0xFFFF;
            }
            for (uint32_t k = 0; k < nk && !again; k++)
                if (at[k] == 0xFFFFFFFFu) again = 1;
        }
        if (!why && !again) {
            for (uint32_t i = 0; i < nc; i++) wr32(h + 4 * (sc + i), rd32(cmd + 8 * i));
            for (uint32_t k = 0; k < nk; k++) {
                if (at[k] == 0xFFFFFFFFu || ky + at[k] + size > hw) continue;
                uint8_t *q = h + 4 * (ky + at[k]);
                float v[12];
                memcpy(v, nac + 4 + (size_t)k * 112, sizeof v);
                for (int c = 0; c < 3; c++) {
                    int32_t tv = (int32_t)lrintf(v[9 + c]);
                    if (!(rd32(q + 4 * c) == 0x7FFFFFFFu && tv == 0)) wr32(q + 4 * c, (uint32_t)tv);
                    wr16(q + 12 + 2 * c, (uint16_t)(int16_t)lrintf(v[5 + c]));
                }
                if (size == 6) {                 /* the scale: the PC's 1st is the PS1's 3rd, its 2nd the 2nd */
                    wr16(q + 22, (uint16_t)(int16_t)lrintf(v[1]));
                    wr16(q + 20, (uint16_t)(int16_t)lrintf(v[2]));
                }
            }
            note("mods: %s: its animations in place of enemy type %d's", e->what[1], e->type);
        } else if (!why) {
            /* laid out again: the script, then the keys in the NAC's order (a command's key at index x size);
             * a key that kept its values takes the disc's "unchanged" channels and first scale value */
            uint32_t keys = nk;                  /* (the bosses' commands take every key of their NAC, the last too) */
            if (keys * size > 0xFFFF) why = "it has more keys than the PS1's commands can point at";
            else if ((nc + keys * size) * 4 > BOSS_GROWTH) why = "its animations laid out again would take more than the 160 KB a boss can grow by";
            else if (!e->data[0]) why = "it has other commands or keys than the PS1's, and the NAB that says where each animation starts isn't in the mod";
            else {
                extra_w = nc + keys * size;
                extra = calloc(extra_w, 4);
                uint32_t *find = malloc(32768 * 4);
                if (!extra || !find) { free(extra); free(find); extra = NULL; why = "out of memory"; }
                else {
                    memset(find, 0xFF, 32768 * 4);
                    uint32_t region = hw - ky < 13107 * size ? (hw - ky) / size : 13107;
                    for (uint32_t k = 0; k < region; k++) {
                        const uint8_t *q = h + 4 * (ky + k * size);
                        uint32_t hsh = 0;
                        for (int c = 0; c < 3; c++) hsh = hsh * 31 + (rd32(q + 4 * c) == 0x7FFFFFFFu ? 0 : rd32(q + 4 * c));
                        for (int c = 0; c < 3; c++) hsh = hsh * 31 + rd16(q + 12 + 2 * c);
                        for (hsh &= 32767; find[hsh] != 0xFFFFFFFFu; hsh = (hsh + 1) & 32767) {}
                        find[hsh] = k;
                    }
                    for (uint32_t i = 0; i < nc && !why; i++) {
                        uint32_t w = rd32(cmd + 8 * i), key = rd32(cmd + 8 * i + 4);
                        if (i && key != 0xFFFFFFFFu) {
                            if (key >= keys) { why = "a command takes a key it doesn't have"; break; }
                            w = (w & 0xFFFF0000u) | (key * size);
                        }
                        wr32(extra + 4 * i, w);
                    }
                    for (uint32_t k = 0; k < keys && !why; k++) {
                        float v[12];
                        memcpy(v, nac + 4 + (size_t)k * 112, sizeof v);
                        uint8_t *q = extra + 4 * (nc + k * size);
                        int32_t t3[3];
                        uint16_t r3[3];
                        uint32_t hsh = 0;
                        for (int c = 0; c < 3; c++) { t3[c] = (int32_t)lrintf(v[9 + c]); hsh = hsh * 31 + (uint32_t)t3[c]; }
                        for (int c = 0; c < 3; c++) { r3[c] = (uint16_t)(int16_t)lrintf(v[5 + c]); hsh = hsh * 31 + r3[c]; }
                        const uint8_t *was = NULL;
                        for (hsh &= 32767; find[hsh] != 0xFFFFFFFFu && !was; hsh = (hsh + 1) & 32767) {
                            const uint8_t *o = h + 4 * (ky + find[hsh] * size);
                            int same = 1;
                            for (int c = 0; c < 3 && same; c++)
                                same = (rd32(o + 4 * c) == 0x7FFFFFFFu ? 0 : (int32_t)rd32(o + 4 * c)) == t3[c] && rd16(o + 12 + 2 * c) == r3[c];
                            if (same) was = o;
                        }
                        for (int c = 0; c < 3; c++) {
                            wr32(q + 4 * c, (uint32_t)(was && rd32(was + 4 * c) == 0x7FFFFFFFu && t3[c] == 0 ? 0x7FFFFFFF : t3[c]));
                            wr16(q + 12 + 2 * c, r3[c]);
                        }
                        if (size == 6) {
                            wr16(q + 18, was ? rd16(was + 18) : 4096);
                            wr16(q + 20, (uint16_t)(int16_t)lrintf(v[2]));
                            wr16(q + 22, (uint16_t)(int16_t)lrintf(v[1]));
                        }
                    }
                    free(find);
                    extra_sc = nc;
                    if (why) { free(extra); extra = NULL; }
                }
            }
            if (!why) note("mods: %s: its animations in place of enemy type %d's (laid out again after the model: %u commands, %u keys)",
                           e->what[1], e->type, nc, nk);
        }
        free(at);
        if (why) {
            note("mods: %s: %s: not used", e->what[1], why);
            nused--;
        }
    }
    uint8_t *cur = h;                            /* the HMD the rest goes into: the disc's, or one built again */
    uint32_t curw = hw;
    if (e->data[0]) {
        int ntri = 0, moved = 0, keptuv = 0, changed = 0, diff = boss_same_layout(e->data[0], e->n[0], h, hw), parts = 3;
        if (getenv("RT_MOD_REBUILD")) diff |= atoi(getenv("RT_MOD_REBUILD")) & 3;   /* (to test it: 1 the skin, 2 the bones', 3 both) */
        const char *why = NULL;
        if (diff & 2) {                          /* the bones' polygons added or taken away */
            uint32_t nw = 0;
            int nt2 = 0;
            uint8_t *nh = boss_rebuild(e->data[0], e->n[0], h, hw, &nw, &nt2, &why);
            if (nh) {
                cur = nh;
                curw = nw;
                models_grown = 1;
                parts &= ~2;
                note("mods: %s: its bones' polygons built again (%d triangles; the model %u -> %u bytes)", e->what[0], nt2, hw * 4, nw * 4);
            } else
                note("mods: %s: its bones' polygons: %s: not used", e->what[0], why);
            why = NULL;
        }
        if (diff & 1) {                          /* the skin's polygons added or taken away */
            uint32_t nw = 0;
            int nt2 = 0;
            uint8_t *nh = boss_skin(e->data[0], e->n[0], cur, curw, &nw, &nt2, &why);
            if (nh) {
                note("mods: %s: its skin built again (%d triangles; the model %u -> %u bytes)", e->what[0], nt2, curw * 4, nw * 4);
                if (cur != h) free(cur);
                cur = nh;
                curw = nw;
                models_grown = 1;
                parts &= ~1;
            } else
                note("mods: %s: its skin: %s: not used", e->what[0], why);
            why = NULL;
        }
        why = parts ? boss_shape(e->data[0], e->n[0], cur, curw, parts, &ntri, &moved, &keptuv, &changed) : NULL;
        if (why) note("mods: %s: its shape: %s: not used", e->what[0], why);
        else if (changed)
            note("mods: %s: its %s in place of enemy type %d's (%d polygons; %d vertex coordinates moved%s)", e->what[0],
                 parts == 3 ? "shape" : parts == 1 ? "skin" : "bones' polygons", e->type, ntri, moved,
                 keptuv ? "; some UVs kept: they weren't in one of its textures" : "");
        const char *why2 = nab_seqs(e->data[0], e->n[0], cur, (curw * 4) | 0x80000000u);
        if (why2) note("mods: %s: its animation tables: %s: not used", e->what[0], why2);
        else note("mods: %s: its animation tables in place of enemy type %d's", e->what[0], e->type);
        const char *why3 = NULL;
        int ni = boss_images(e->data[0], e->n[0], cur, curw, &why3);
        if (ni < 0) note("mods: %s: its textures: %s: not used", e->what[0], why3);
        else if (ni) note("mods: %s: its textures in place of enemy type %d's (%d pictures in its model)", e->what[0], e->type, ni);
        int nt = boss_textures(e->type, e->data[0], e->n[0], &why3);   /* and the copies of them elsewhere on the disc */
        if (nt > 0) note("mods: %s: its textures also in %d other places on the disc", e->what[0], nt);
        if (why && why2) nused--;
    }
    if (e->data[2]) { note("mods: %s: give enemy type %d's textures in its NAB: not used", e->what[2], e->type); nused--; }
    if (extra || cur != h) {
        uint8_t *nh = malloc((size_t)(curw + extra_w) * 4);
        if (nh && ngrows < (int)(sizeof grows / sizeof grows[0])) {
            memcpy(nh, cur, (size_t)curw * 4);
            if (extra) {
                memcpy(nh + 4 * (size_t)curw, extra, (size_t)extra_w * 4);
                wr32(nh + 4 * (hdr + 3), 0x80000000u | curw);          /* where the script and the keys start now */
                wr32(nh + 4 * (hdr + 4), 0x80000000u | (curw + extra_sc));
            }
            Grow *g = &grows[ngrows++];
            g->fi = fi;
            g->offset = start;
            g->size = (curw + extra_w) * 4;
            g->orig = hw * 4;
            g->cap = (hw * 4 + BOSS_GROWTH + 2047) & ~2047u;
            models_grown = 1;                    /* (the log says if a room's memory comes near its end) */
            mod_memory_models = 1;
            g->data = nh;
            snprintf(g->what, sizeof g->what, "%s", extra ? e->what[1] : e->what[0]);
        } else
            free(nh);
        free(extra);
        if (cur != h) free(cur);
    }
#undef HW
}

/* Each enemy a mod changes, converted into its block (packed again in
 * packs_done). */
static void enemies_done(void) {
    for (int i = 0; i < nenemies; i++) {
        Enemy *e = &enemies[i];
        int nk = !!e->data[0] + !!e->data[1] + !!e->data[2];
        const char *what = e->data[0] ? e->what[0] : e->data[1] ? e->what[1] : e->what[2];
        if (e->type >= 31 && e->type <= 33) {
            enemy_whole(e);
            goto next;
        }
        if (e->type >= 34 && e->type <= 37) {    /* the PC's types 34-37 are 16's and 17's data on the PS1 */
            int to = e->type < 36 ? 16 : 17;
            note("mods: %s: enemy type %d is type %d's data on the PS1: it goes there", what, e->type, to);
            e->type = to;
        }
        int fi = disc_index(e->type == 30 ? "EV_OBJ.DAT" : "OBJ.DAT");
        Packed *p = e->type >= 0 && e->type <= 30 ? packed_get(fi, e->type == 30 ? 0x196 : (uint32_t)e->type) : NULL;
        if (!p || !p->split) {
            note("mods: %s: the PS1 has no enemy type %d: not used", what, e->type);
            nused -= nk;
            goto next;
        }
        uint32_t n = 0;
        uint8_t *u;
        const char *why;
        int ntex = 0;
        if (e->data[0]) {
            u = packed_part(p, 0, &n);
            why = u ? nab_into(e->data[0], e->n[0], u, n, &ntex) : "the disc's textures couldn't be unpacked";
            if (why) { note("mods: %s: %s: not used", e->what[0], why); nused--; }
            else {
                packed_note(p, 0, e->what[0]);
                note("mods: %s: its textures (%d) in place of enemy type %d's", e->what[0], ntex, e->type);
            }
        }
        if (e->data[0]) {                        /* the NAB's shape */
            uint32_t nt = 0, n2 = 0, tw = 0, th = 0;
            uint8_t *tex = packed_part(p, 0, &nt), *skin = tex ? enemy_skin(tex, nt) : NULL;
            if (skin) tim_size(skin, &tw, &th);
            u = packed_part(p, 2, &n2);
            int ntri = 0, moved = 0;
            if (u && (!nab_same_layout(e->data[0], e->n[0], u, n2) || getenv("RT_MOD_REBUILD"))) {   /* (RT_MOD_REBUILD: always, to test it) */
                uint32_t nn = 0;                 /* polygons added, taken away or moved between body parts */
                uint8_t *nu = nab_rebuild(e->data[0], e->n[0], u, n2, tw, &nn, &ntri, &why);
                if (!nu) note("mods: %s: its shape: %s: not used (its textures are)", e->what[0], why);
                else {
                    note("mods: %s: its shape, built again, in place of enemy type %d's (%d triangles; the model %u -> %u bytes)", e->what[0],
                         e->type, ntri, n2, nn);
                    free(p->part[2]);
                    p->part[2] = nu;
                    p->plen[2] = nn;
                    if (nn > n2) models_grown = 1;
                    packed_note(p, 2, e->what[0]);
                }
            } else {
                why = u ? nab_shape(e->data[0], e->n[0], u, n2, tw, &ntri, &moved) : "the disc's model couldn't be unpacked";
                if (why) note("mods: %s: its shape: %s: not used (its textures are)", e->what[0], why);
                else if (p->was[2] && memcmp(p->was[2], u, n2)) {
                    packed_note(p, 2, e->what[0]);
                    note("mods: %s: its shape in place of enemy type %d's (%d polygons; %d vertex coordinates moved)", e->what[0], e->type, ntri, moved);
                }
            }
        }
        if (e->data[2]) {                        /* a picture of the skin: after the NAB's, so it wins */
            u = packed_part(p, 0, &n);
            uint8_t *tim = u ? enemy_skin(u, n) : NULL;
            int w = 0, h = 0, c;
            uint8_t *img = tim ? stbi_load_from_memory(e->data[2], (int)e->n[2], &w, &h, &c, 4) : NULL;
            const char *how = "";
            why = !tim ? "the disc's textures couldn't be unpacked" : !img ? "couldn't read it as a picture"
                                                                          : tim_from_picture(tim, img, (uint32_t)w, (uint32_t)h, &how);
            stbi_image_free(img);
            if (why) { note("mods: %s: %s: not used", e->what[2], why); nused--; }
            else {
                uint32_t tw = 0, th = 0;
                tim_size(tim, &tw, &th);
                packed_note(p, 0, e->what[2]);
                note("mods: %s: enemy type %d's texture (%ux%u%s)", e->what[2], e->type, tw, th,
                       (uint32_t)w != tw || (uint32_t)h != th ? ", scaled to the PS1's size" : "");
            }
        }
        if (e->data[1]) {
            u = packed_part(p, 1, &n);
            int rebuilt = 0;
            uint32_t was_n = n;
            why = u ? nac_into(e->data[1], e->n[1], &u, &n, &rebuilt) : "the disc's animations couldn't be unpacked";
            if (!why && rebuilt && !e->data[0]) {
                free(u);
                why = "it has other commands or keys than the PS1's, and the NAB that says where each animation starts isn't in the mod";
            }
            if (why) { note("mods: %s: %s: not used", e->what[1], why); nused--; }
            else {
                if (rebuilt) {
                    free(p->part[1]);
                    p->part[1] = u;
                    p->plen[1] = n;
                }
                packed_note(p, 1, e->what[1]);
                note("mods: %s: its animations in place of enemy type %d's%s", e->what[1], e->type, rebuilt ? " (laid out again)" : "");
                if (rebuilt) note("mods: %s: the animations %u -> %u bytes", e->what[1], was_n, n);
            }
        }
        if (e->data[0]) {                        /* the NAB's animation tables: where each animation starts */
            uint32_t n2 = 0;
            uint8_t *b2 = packed_part(p, 2, &n2), *before = b2 ? malloc(n2) : NULL;
            if (before) memcpy(before, b2, n2);
            why = b2 ? nab_seqs(e->data[0], e->n[0], b2, n2) : "the disc's model couldn't be unpacked";
            if (why) note("mods: %s: its animation tables: %s: not used", e->what[0], why);
            else if (before && memcmp(before, b2, n2)) {
                packed_note(p, 2, e->what[0]);
                note("mods: %s: its animation tables in place of enemy type %d's", e->what[0], e->type);
            }
            free(before);
        }
    next:
        for (int k = 0; k < 3; k++) free(e->data[k]);
    }
    nenemies = 0;
}
