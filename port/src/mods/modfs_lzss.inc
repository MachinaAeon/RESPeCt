/* modfs.c, part: The game's LZSS, unpacking and packing. Included by modfs.c, in this order. */

/* ------------------------------------------------------------ packed data */

/* The game's LZSS (kernel_files.c, 0x80016F78): a 32-bit size, then a
 * flag byte per 8 items (bit set: a literal byte; clear: 16 bits,
 * distance in the top 11, length - 3 in the low 5). */
static uint8_t *lz_unpack_size(const uint8_t *s, uint32_t avail, uint32_t *n, uint32_t *consumed) {
    if (avail < 4) return NULL;
    uint32_t size = rd32(s), i = 4, o = 0, flags = 0;
    if (size > 16u << 20) return NULL;
    uint8_t *d = malloc(size + 64);
    while (d && o < size) {
        if (!(flags & 0x100)) {
            if (i >= avail) break;
            flags = s[i++] | 0xFF00u;
        }
        if (flags & 1) {
            if (i >= avail) break;
            d[o++] = s[i++];
        } else {
            if (i + 2 > avail) break;
            uint32_t w = s[i] | s[i + 1] << 8, dist = w >> 5, len = (w & 31) + 3;
            i += 2;
            if (!dist) { free(d); return NULL; }   /* a self-reference has no initialized source byte */
            for (uint32_t k = 0; k < len && o < size + 34; k++, o++) d[o] = o >= dist ? d[o - dist] : 0;
        }
        flags >>= 1;
    }
    if (d && o < size) { free(d); return NULL; }
    *n = size;
    if (consumed) *consumed = i;
    return d;
}

static uint8_t *lz_unpack(const uint8_t *s, uint32_t avail, uint32_t *n) {
    return lz_unpack_size(s, avail, n, NULL);
}

#define HASH(p) ((((uint32_t)in[p] << 8) ^ ((uint32_t)in[(p) + 1] << 4) ^ in[(p) + 2]) & 4095)
static void lz_match(const uint8_t *in, uint32_t n, uint32_t i, const int32_t *head, const int32_t *prev, uint32_t *best,
                     uint32_t *bdist) {
    *best = 0;
    if (i + 3 > n) return;
    int32_t c = head[HASH(i)];
    for (int tries = 0; c >= 0 && i - (uint32_t)c <= 2047 && tries < 128; tries++, c = prev[c]) {
        if ((uint32_t)c >= i) continue;
        uint32_t l = 0, max = n - i < 34 ? n - i : 34;
        while (l < max && in[c + l] == in[i + l]) l++;
        if (l > *best) { *best = l; *bdist = i - (uint32_t)c; if (l == 34) break; }
    }
}

static uint8_t *lz_pack(const uint8_t *in, uint32_t n, uint32_t *outn) {
    uint8_t *o = malloc(4 + (size_t)n + n / 8 + 16);
    int32_t *head = malloc(4096 * sizeof *head), *prev = malloc(((size_t)n + 1) * sizeof *prev);
    if (!o || !head || !prev) { free(o); free(head); free(prev); return NULL; }
    for (int k = 0; k < 4096; k++) head[k] = -1;
    wr32(o, n);
    uint32_t w = 4, flagat = 0, item = 8, i = 0;
    while (i < n) {
        if (item == 8) { flagat = w++; o[flagat] = 0; item = 0; }
        uint32_t best = 0, bdist = 0, next = 0, ndist = 0;
        lz_match(in, n, i, head, prev, &best, &bdist);
        if (best >= 3 && best < 34 && i + 1 < n) {   /* a longer one a byte on: a literal first */
            uint32_t h = HASH(i);                    /* (i itself in reach for it) */
            prev[i] = head[h];
            head[h] = (int32_t)i;
            lz_match(in, n, i + 1, head, prev, &next, &ndist);
            head[h] = prev[i];
            if (next > best + 1) best = 0;
        }
        uint32_t step = best >= 3 ? best : 1;
        if (best >= 3) {
            uint32_t v = bdist << 5 | (best - 3);
            o[w++] = (uint8_t)v;
            o[w++] = (uint8_t)(v >> 8);
        } else {
            o[flagat] |= (uint8_t)(1 << item);
            o[w++] = in[i];
        }
        item++;
        for (uint32_t k = 0; k < step; k++, i++)
            if (i + 3 <= n) { uint32_t h = HASH(i); prev[i] = head[h]; head[h] = (int32_t)i; }
    }
#undef HASH
    free(head);
    free(prev);
    *outn = w;
    return o;
}
