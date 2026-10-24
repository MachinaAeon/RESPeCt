/* ISO 9660 lookup on the disc image: find a file's first sector and size. */
#include "rt.h"

#include <ctype.h>
#include <stdlib.h>

int iso_find(const char *name, uint32_t *lba, uint32_t *size) {
    uint8_t s[2352];
    if (!cd_read_sector_raw(16, s)) return 0;
    const uint8_t *pvd = s + 24;
    if (memcmp(pvd + 1, "CD001", 5)) return 0;
    const uint8_t *root = pvd + 156;
    uint32_t dlba = root[2] | root[3] << 8 | root[4] << 16 | (uint32_t)root[5] << 24;
    uint32_t dlen = root[10] | root[11] << 8 | root[12] << 16 | (uint32_t)root[13] << 24;
    for (uint32_t sec = 0; sec * 2048 < dlen; sec++) {
        cd_read_sector_raw(dlba + sec, s);
        const uint8_t *d = s + 24;
        for (int o = 0; o < 2048;) {
            int len = d[o];
            if (!len) break;
            int nlen = d[o + 32];
            char fn[64] = {0};
            for (int i = 0; i < nlen && i < 63; i++) fn[i] = (char)d[o + 33 + i];
            char *semi = strchr(fn, ';');
            if (semi) *semi = 0;
            int match = 1;
            for (int i = 0;; i++) {
                if (toupper((unsigned char)fn[i]) != toupper((unsigned char)name[i])) { match = 0; break; }
                if (!fn[i]) break;
            }
            if (match) {
                *lba = d[o + 2] | d[o + 3] << 8 | d[o + 4] << 16 | (uint32_t)d[o + 5] << 24;
                *size = d[o + 10] | d[o + 11] << 8 | d[o + 12] << 16 | (uint32_t)d[o + 13] << 24;
                return 1;
            }
            o += len;
        }
    }
    return 0;
}

int iso_read_file(const char *name, uint8_t **out, uint32_t *size) {
    uint32_t lba, n;
    if (!iso_find(name, &lba, &n)) return 0;
    uint8_t *buf = malloc(n + 2048);
    uint8_t s[2352];
    for (uint32_t i = 0; i * 2048 < n; i++) {
        cd_read_sector_raw(lba + i, s);
        memcpy(buf + i * 2048, s + 24, 2048);
    }
    *out = buf;
    *size = n;
    return 1;
}
