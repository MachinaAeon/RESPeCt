/* HUD weapon graphics.
 *
 * func_80092878 (data, buf, id) unpacks entry id of an image archive (or
 * every entry for id -1) into buf with the game's LZSS (func_80016F78) and
 * uploads it to VRAM as a TIM (func_8006F370). When the stage starts, the
 * weapon HUD loads its archive (one image per weapon) into 0x800CDA90 and
 * unpacks the held weapon's; that RAM is reused for drawing soon after.
 * The native version does exactly the same, and also keeps every weapon's
 * image (unpacked) on the PC side so switching weapons outside the
 * inventory screen can show the new one (hud_show_weapon). */
#include "game_vars.h"

#include <stdio.h>
#include <stdlib.h>
void rt_log(const char *fmt, ...);
#include <string.h>

void n_hud_UnpackImages(void);
void gpu_load_image(int x, int y, int w, int h, const uint16_t *pixels);
extern uint16_t g_vram[512][1024];
static void hud_upload_weapon(int id);

#define F_LZSS_UNPACK  0x80016F78u
#define F_LOAD_TIM     0x8006F370u
#define MAX_IMAGES     96

static uint8_t *image[MAX_IMAGES];
static uint32_t image_len[MAX_IMAGES];

/* The game's LZSS: u32 size, then flag bytes (bit set = literal byte, clear
 * = 16-bit back reference: distance >> 5, length & 31 + 3; before the start
 * reads as zero). */
static uint8_t *lzss_unpack(const uint8_t *src, size_t avail, uint32_t *out_len) {
    if (avail < 4) return NULL;
    uint32_t n = src[0] | src[1] << 8 | src[2] << 16 | (uint32_t)src[3] << 24;
    if (n == 0 || n > (1u << 20)) return NULL;
    uint8_t *out = malloc(n);
    size_t s = 4, d = 0;
    uint32_t flags = 0;
    while (d < n && s < avail) {
        flags >>= 1;
        if (!(flags & 0x100)) flags = src[s++] | 0xFF00;
        if (flags & 1) {
            if (s >= avail) break;
            out[d++] = src[s++];
        } else {
            if (s + 1 >= avail) break;
            uint32_t v = src[s] | src[s + 1] << 8;
            s += 2;
            uint32_t dist = v >> 5, len = (v & 31) + 3;
            for (uint32_t k = 0; k < len && d < n; k++, d++) out[d] = d >= dist ? out[d - dist] : 0;
        }
    }
    *out_len = n;
    return out;
}

static uint32_t rd32x(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }

/* Unpack every image of an archive (host memory, n bytes) into the cache. */
static void cache_archive(const uint8_t *data, size_t n) {
    if (n < 8) return;
    uint32_t count = rd32x(data);
    if (count == 0 || count > MAX_IMAGES || 4 + 4 * count > n) return;
    for (uint32_t i = 0; i < count; i++) {
        free(image[i]);
        image[i] = NULL;
        uint32_t off = rd32x(data + 4 + 4 * i);
        if (off >= n) continue;
        image[i] = lzss_unpack(data + off, n - off, &image_len[i]);
        if (getenv("RT_HUD_DEBUG") && image[i])
            rt_log("hud: image %u: %u bytes", i, image_len[i]);
    }
}

static void cache_images(u32 data) {
    if ((data & 0x1FFFFF) >= RAM_SIZE) return;
    cache_archive(gp(data), RAM_SIZE - (data & 0x1FFFFF));
}

/* The same archive straight from the disc (CONPAU.BIN + 0x10000 on the
 * US disc), for when the stage loaded it before this session did, i.e.
 * after loading a save state. Checked against the header the stage uses. */
int cd_read_sector_raw(uint32_t lba, uint8_t out[2352]);
#define ARCHIVE_LBA     16763
#define ARCHIVE_SECTORS 20
static void cache_from_disc(void) {
    static int tried;
    if (tried) return;
    tried = 1;
    uint8_t *buf = malloc(ARCHIVE_SECTORS * 2048), raw[2352];
    for (int i = 0; i < ARCHIVE_SECTORS; i++) {
        if (!cd_read_sector_raw(ARCHIVE_LBA + (uint32_t)i, raw)) { free(buf); return; }
        memcpy(buf + i * 2048, raw + 24, 2048);
    }
    if (rd32x(buf) == 54 && rd32x(buf + 4) == 0xDC) cache_archive(buf, ARCHIVE_SECTORS * 2048);
    else rt_log("hud: weapon images not found on the disc");
    free(buf);
}

/* 80092878 */
void n_hud_UnpackImages(void) {
    u32 data = A0, buf = A1;
    s32 id = (s16)A2;
    s32 count = GV(s32, data);
    if (id == -1) {
        for (s32 i = 0; i < count; i++) {
            GCALL2(F_LZSS_UNPACK, data + GV(u32, data + 4 + 4 * (u32)i), buf);
            GCALL1(F_LOAD_TIM, buf);
        }
        return;
    }
    if (count < id) return;
    cache_images(data);                      /* port: keep them all */
    GCALL2(F_LZSS_UNPACK, data + GV(u32, data + 4 + 4 * (u32)id), buf);
    GCALL1(F_LOAD_TIM, buf);
}

static uint32_t rd32(const uint8_t *p) { return p[0] | p[1] << 8 | p[2] << 16 | (uint32_t)p[3] << 24; }
static uint16_t rd16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }

/* VRAM under a temporarily uploaded weapon image (see hud_show_weapon). */
static struct { int x, y, w, h; uint16_t *px; } saved[2];
static int nsaved, saving;

/* A TIM block: u32 length, x, y, w, h, then w * h halfwords. */
static size_t load_block(const uint8_t *p, size_t avail) {
    if (avail < 12) return avail;
    uint32_t len = rd32(p);
    int x = rd16(p + 4), y = rd16(p + 6), w = rd16(p + 8), h = rd16(p + 10);
    if (len < 12 || len > avail || (size_t)w * h * 2 > len - 12) return avail;
    if (getenv("RT_HUD_DEBUG")) rt_log("hud: upload %d,%d %dx%d", x, y, w, h);
    if (saving && nsaved < 2) {
        uint16_t *old = malloc((size_t)w * h * 2);
        for (int j = 0; j < h; j++)
            for (int i = 0; i < w; i++) old[j * w + i] = g_vram[(y + j) & 511][(x + i) & 1023];
        saved[nsaved++] = (typeof(saved[0])){x, y, w, h, old};
    }
    uint16_t *px = malloc((size_t)w * h * 2);
    memcpy(px, p + 12, (size_t)w * h * 2);
    gpu_load_image(x, y, w, h, px);
    free(px);
    return len;
}

/* Put weapon id's HUD image into VRAM, as unpacking it would, for the
 * weapon view to copy into the HUD (func_80094AE4); hud_weapon_done()
 * then puts back what was there. Each weapon's image has its own place in
 * VRAM, and during play most of those places hold enemy textures: only
 * the image of the weapon held when the stage started is resident. */
void hud_weapon_done(void) {
    while (nsaved > 0) {
        nsaved--;
        gpu_load_image(saved[nsaved].x, saved[nsaved].y, saved[nsaved].w, saved[nsaved].h, saved[nsaved].px);
        free(saved[nsaved].px);
    }
}

void hud_show_weapon(int id) {
    hud_weapon_done();
    saving = 1;
    hud_upload_weapon(id);
    saving = 0;
}

static void hud_upload_weapon(int id) {
    if (id >= 0 && id < MAX_IMAGES && !image[id]) cache_from_disc();
    if (id < 0 || id >= MAX_IMAGES || !image[id] || image_len[id] < 8) return;
    const uint8_t *t = image[id];
    size_t n = image_len[id];
    if (rd32(t) != 0x10) return;
    uint32_t flags = rd32(t + 4);
    size_t pos = 8;
    if (flags & 8) pos += load_block(t + pos, n - pos);      /* palette */
    if (pos < n) load_block(t + pos, n - pos);               /* pixels */
}

/* func_80094C60 (ammo item id): the HUD's bullet image for that ammo. The
 * game reads it from CONPAU.BIN into 0x800D28B0 - stage memory during play -
 * and loads the TIM from there; here it's read on the PC side. The table at
 * 0x800A617C pairs ammo items with CONPAU files, 0x800A0C74 holds the files'
 * sector offsets. */
#define CONPAU_LBA 16731
void hud_show_ammo(int item) {
    int file = -1;
    for (u32 i = 0; i < 32; i += 2) {
        s16 it = GV(s16, 0x800A617C + i * 2);
        if (it == item) { file = GV(s16, 0x800A617C + i * 2 + 2); break; }
    }
    if (file < 0 || file > 16) return;
    u32 first = GV(u32, 0x800A0C74 + (u32)file * 4), last = GV(u32, 0x800A0C74 + (u32)file * 4 + 4);
    if (last <= first || last - first > 16) return;
    size_t n = (size_t)(last - first) * 2048;
    uint8_t *buf = malloc(n), raw[2352];
    for (u32 i = 0; i < last - first; i++) {
        if (!cd_read_sector_raw(CONPAU_LBA + first + i, raw)) { free(buf); return; }
        memcpy(buf + (size_t)i * 2048, raw + 24, 2048);
    }
    if (rd32(buf) == 0x10) {
        uint32_t flags = rd32(buf + 4);
        size_t pos = 8;
        if (flags & 8) pos += load_block(buf + pos, n - pos);
        if (pos < n) load_block(buf + pos, n - pos);
    }
    free(buf);
}
