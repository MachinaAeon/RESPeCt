/* Memory map outside main RAM: scratchpad, hardware registers, BIOS ROM,
 * expansion area, cache control. */
#include "rt.h"

#include <stdio.h>

void ram_read(uint32_t a, void *dst, uint32_t n) {
    uint8_t *d = dst;
    for (uint32_t i = 0; i < n; i++)
        d[i] = (uint8_t)MEM_R8(a + i);
}

void ram_write(uint32_t a, const void *src, uint32_t n) {
    const uint8_t *s = src;
    for (uint32_t i = 0; i < n; i++)
        MEM_W8(a + i, s[i]);
}

const char *ram_str(uint32_t a) {
    static char buf[4][256];
    static int k;
    char *b = buf[k++ & 3];
    int i = 0;
    for (; i < 255; i++) {
        b[i] = (char)MEM_R8(a + i);
        if (!b[i])
            break;
    }
    b[i] = 0;
    return b;
}

static int is_scratch(uint32_t p) { return p >= 0x1F800000 && p < 0x1F800400; }
static int is_hw(uint32_t p) { return p >= 0x1F801000 && p < 0x1F803000; }

uint32_t mem_slow_r32(uint32_t a) {
    uint32_t p = a & 0x1FFFFFFF;
    if (is_scratch(p)) { uint32_t v; memcpy(&v, g_scratch + (p & 0x3FC), 4); sxy_scratch_load(p, v); return v; }
    if (is_hw(p)) return hw_read(p, 4);
    if (a == 0xFFFE0130) return 0;            /* cache control */
    return 0;                                 /* BIOS ROM, expansion: not present */
}

uint32_t mem_slow_r16(uint32_t a) {
    uint32_t p = a & 0x1FFFFFFF;
    if (is_scratch(p)) { uint16_t v; memcpy(&v, g_scratch + (p & 0x3FE), 2); return v; }
    if (is_hw(p)) return hw_read(p, 2) & 0xFFFF;
    return 0;
}

uint32_t mem_slow_r8(uint32_t a) {
    uint32_t p = a & 0x1FFFFFFF;
    if (is_scratch(p)) return g_scratch[p & 0x3FF];
    if (is_hw(p)) return hw_read(p, 1) & 0xFF;
    return 0;
}

void mem_slow_w32(uint32_t a, uint32_t v) {
    uint32_t p = a & 0x1FFFFFFF;
    if (is_scratch(p)) { if (sxy_maybe(v)) sxy_scratch_store(p, v); memcpy(g_scratch + (p & 0x3FC), &v, 4); return; }
    if (is_hw(p)) { hw_write(p, v, 4); return; }
}

void mem_slow_w16(uint32_t a, uint32_t v) {
    uint32_t p = a & 0x1FFFFFFF;
    if (is_scratch(p)) { uint16_t h = (uint16_t)v; memcpy(g_scratch + (p & 0x3FE), &h, 2); return; }
    if (is_hw(p)) { hw_write(p, v & 0xFFFF, 2); return; }
}

void mem_slow_w8(uint32_t a, uint32_t v) {
    uint32_t p = a & 0x1FFFFFFF;
    if (is_scratch(p)) { g_scratch[p & 0x3FF] = (uint8_t)v; return; }
    if (is_hw(p)) { hw_write(p, v & 0xFF, 1); return; }
}
