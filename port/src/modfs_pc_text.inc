#include "modfs_pc_pickups.h"
/* Raster messages are deliberately opt-in, keyed by original identities and
 * authored ranges of the native text cursor (halfwords, end exclusive).
 * This never changes the text, controls, waits, choices or game pointers. */
#include "modfs_pc_text.h"
#include "modfs_pc_messages.h"
typedef struct {
    unsigned kind, block, id, begin, end;
    int x, y, w, h, iw, ih;
    int cx, cy, cw, ch;                          /* rgba holds only this part of the iw x ih picture (cw 0: all of it) */
    uint8_t *rgba;
    uint32_t native_words, native_crc; /* zero words: authored range */
} PcBitmap;
static PcBitmap *pc_bitmaps;
static size_t npc_bitmaps, pc_bitmap_bytes, pc_diary_bytes;
/* Check before subtracting a prior allocation or adding a replacement.
 * Inconsistent/overflowing accounting never admits an allocation. */
static int pc_picture_fits(size_t replacing, uint64_t bytes) {
    const uint64_t limit=MODFS_PC_PICTURE_BUDGET;
    if(pc_bitmap_bytes>limit || pc_diary_bytes>limit-pc_bitmap_bytes)return 0;
    uint64_t used=(uint64_t)pc_bitmap_bytes+pc_diary_bytes;
    return replacing<=used && bytes<=limit && used-replacing<=limit-bytes;
}
static void pc_diaries_load(const char *mod, const char *root);
static void pc_diary_disable(unsigned doc);
static void pc_diary_composite(uint32_t *out, int w, int h, int native_w);
static int pc_diary_title_message(uint32_t text,int x,int y,int w,int h);
static void pc_book_composite(uint32_t *out,int w,int h,int native_w);
static void pc_diary_keys_composite(uint32_t *out,int w,int h,int native_w);
static void pc_diary_titles_composite(uint32_t *out,int w,int h,int native_w);
static uint32_t pc_pickup_id = UINT32_MAX;
static int pc_pickup_pending;
static uint32_t pc_caption_block = UINT32_MAX, pc_caption_index = UINT32_MAX, pc_caption_offset = UINT32_MAX;
typedef struct { char mod[256], rel[512]; } PcBitmapSource;
static PcBitmapSource *pc_bitmap_sources;
static size_t npc_bitmap_sources;

int modfs_pc_has_bitmaps(void) { return npc_bitmaps != 0 || modfs_pc_has_diaries(); }
void modfs_pc_pickup(uint32_t item) { pc_pickup_id = item < 96 ? item : UINT32_MAX; pc_pickup_pending = pc_pickup_id != UINT32_MAX; }
void modfs_pc_entry(uint32_t text) {
    if (!pc_pickup_pending || text != 0x800A88C8u) pc_pickup_id = UINT32_MAX;
    pc_pickup_pending = 0;
}
uint32_t modfs_pc_pickup_state(void) { return pc_pickup_id; }
void modfs_pc_restore_pickup(uint32_t item) { modfs_pc_pickup(item); pc_pickup_pending = 0; }
void modfs_pc_get_state(ModPcBitmapState *s) { *s = (ModPcBitmapState){1, pc_pickup_id, pc_caption_block, pc_caption_index, pc_caption_offset}; }
int modfs_pc_valid_state(const ModPcBitmapState *s) {
    return s && s->version == 1 && (s->pickup < 96 || s->pickup == UINT32_MAX) &&
        ((s->caption_block == UINT32_MAX && s->caption_index == UINT32_MAX && s->caption_offset == UINT32_MAX) ||
         (s->caption_block < 1024 && s->caption_index < 256 && s->caption_offset < 256));
}
void modfs_pc_set_state(const ModPcBitmapState *s) {
    pc_pickup_pending = 0;
    if (!modfs_pc_valid_state(s)) { pc_pickup_id = pc_caption_block = pc_caption_index = pc_caption_offset = UINT32_MAX; return; }
    pc_pickup_id = s->pickup; pc_caption_block = s->caption_block; pc_caption_index = s->caption_index; pc_caption_offset = s->caption_offset;
}

/* No absolute, parent, empty, drive or alternate-stream components. Resolve
 * case as the normal loader does, and reject symlinks/junctions below root. */
/* A folder's names, read once (the automatic pictures look up hundreds of
 * names in the same few folders, each under five extensions). */
static struct { char dir[1024]; char **names; size_t n; } pc_dirs[32];
static size_t npc_dirs;
static void pc_dirs_reset(void) {             /* (each mod's load reads its folders afresh) */
    for (size_t k = 0; k < npc_dirs; k++) {
        for (size_t i = 0; i < pc_dirs[k].n; i++) free(pc_dirs[k].names[i]);
        free(pc_dirs[k].names);
    }
    npc_dirs = 0;
}
static int pc_dir_find(const char *dir, const char *part, char actual[256]) {
    size_t k = 0;
    while (k < npc_dirs && strcmp(pc_dirs[k].dir, dir)) k++;
    if (k == npc_dirs) {
        if (npc_dirs == sizeof pc_dirs / sizeof pc_dirs[0]) {      /* full: read it as before */
            DIR *d = opendir(dir); struct dirent *e; int found = 0;
            while (d && (e = readdir(d))) if (!strcasecmp(e->d_name, part)) { snprintf(actual, 256, "%s", e->d_name); found = 1; break; }
            if (d) closedir(d);
            return found;
        }
        snprintf(pc_dirs[k].dir, sizeof pc_dirs[k].dir, "%s", dir);
        pc_dirs[k].names = NULL; pc_dirs[k].n = 0;
        DIR *d = opendir(dir); struct dirent *e; size_t cap = 0;
        while (d && (e = readdir(d))) {
            if (pc_dirs[k].n == cap) {
                char **m = realloc(pc_dirs[k].names, (cap = cap ? cap * 2 : 64) * sizeof *m);
                if (!m) break;
                pc_dirs[k].names = m;
            }
            pc_dirs[k].names[pc_dirs[k].n] = strdup(e->d_name);
            if (pc_dirs[k].names[pc_dirs[k].n]) pc_dirs[k].n++;
        }
        if (d) closedir(d);
        npc_dirs++;
    }
    for (size_t i = 0; i < pc_dirs[k].n; i++)
        if (!strcasecmp(pc_dirs[k].names[i], part)) { snprintf(actual, 256, "%s", pc_dirs[k].names[i]); return 1; }
    return 0;
}
static int pc_bitmap_path(const char *root, const char *rel, char path[1024]) {
    if (!*rel || strlen(rel) >= 512 || rel[0] == '/' || rel[0] == '\\' || strchr(rel, ':')) return 0;
    if (snprintf(path, 1024, "%s", root) >= 1024) return 0;
    const char *p = rel;
    while (*p) {
        const char *end = p; while (*end && *end != '/' && *end != '\\') end++;
        size_t n = (size_t)(end - p);
        if (!n || n > 255 || (n == 1 && p[0] == '.') || (n == 2 && !memcmp(p, "..", 2))) return 0;
        char part[256], actual[256] = ""; memcpy(part, p, n); part[n] = 0;
        if (!pc_dir_find(path, part, actual) || strlen(path) + strlen(actual) + 2 > 1024) return 0;
        strcat(path, "/"); strcat(path, actual);
#ifdef _WIN32
        DWORD attr = GetFileAttributesA(path);
        if (attr == INVALID_FILE_ATTRIBUTES || (attr & FILE_ATTRIBUTE_REPARSE_POINT)) return 0;
#else
        struct stat st; if (lstat(path, &st) || S_ISLNK(st.st_mode)) return 0;
#endif
        p = *end ? end + 1 : end;
        if (*end && !*p) return 0;
    }
    return 1;
}
static void pc_bitmap_source_add(const char *mod, const char *rel) {
    PcBitmapSource *p = realloc(pc_bitmap_sources, (npc_bitmap_sources + 1) * sizeof *p);
    if (!p) return;
    pc_bitmap_sources = p; p += npc_bitmap_sources++;
    snprintf(p->mod, sizeof p->mod, "%s", mod);
    for (size_t i = 0;; i++) { p->rel[i] = rel[i] == '\\' ? '/' : (char)toupper((unsigned char)rel[i]); if (!rel[i]) break; }
}
static int pc_bitmaps_source(const char *mod, const char *up) {
    if (!strcmp(up, "PC-BITMAPS.TXT")) return 1;
    for (size_t i = 0; i < npc_bitmap_sources; i++)
        if (!strcmp(mod, pc_bitmap_sources[i].mod) && !strcmp(up, pc_bitmap_sources[i].rel)) return 1;
    return 0;
}
static int pc_bitmap_uint(const char *s, unsigned *out) {
    if (!*s) return 0;
    uint64_t n = 0;
    for (; *s; s++) { if (*s < '0' || *s > '9') return 0; n = n * 10 + (unsigned)(*s - '0'); if (n > UINT32_MAX) return 0; }
    *out = (unsigned)n; return 1;
}
static int diary_apply_picture(const char *mod, const char *rel, unsigned doc, unsigned page, const uint8_t *img, int w, int h);

/* Reference profiles apply before authored rows in each mod. File order
 * never creates guessed identities, crops, timing or pagination. */
static void pc_bitmaps_automatic(const char *mod, const char *root) {
    static const char *const ext[] = {"BMP", "PNG", "JPG", "JPEG", "TGA"};
    const size_t rooms=sizeof modfs_pc_messages/sizeof modfs_pc_messages[0];
    char last_path[1024] = "";                   /* one picture's rows follow each other: decoded once, shared */
    uint8_t *last_img = NULL;
    for (size_t i=0;i<rooms+sizeof modfs_pc_pickups/sizeof modfs_pc_pickups[0];i++) {
        unsigned kind=i>=rooms;PcMessageProfile route;
        if(kind){const PcPickupProfile *q=&modfs_pc_pickups[i-rooms];route=(PcMessageProfile){q->pc,0,q->id,q->words,q->crc};}
        else route=modfs_pc_messages[i];
        const PcMessageProfile *p=&route;
        for (unsigned wrapper = 0; wrapper < 2; wrapper++) for (unsigned e = 0; e < sizeof ext / sizeof ext[0]; e++) {
            char rel[512], path[1024];
            snprintf(rel, sizeof rel, "%s%.*s.%s", wrapper ? "PSX_DATA/" : "", (int)(strlen(p->pc) - 4), p->pc, ext[e]);
            if (!pc_bitmap_path(root, rel, path)) continue;
            if (last_img && !strcmp(path, last_path)) {
                PcBitmap *b = realloc(pc_bitmaps, (npc_bitmaps + 1) * sizeof *b);
                if (!b) { note("mods: %s/%s: bitmap allocation failed: not used", mod, rel); continue; }
                pc_bitmaps = b;
                b[npc_bitmaps++] = (PcBitmap){.kind=kind, .block=p->block, .id=p->id, .begin=0, .end=p->words,
                    .x=0, .y=0, .w=320, .h=240, .iw=512, .ih=512, .cx=b[npc_bitmaps-1].cx, .cy=b[npc_bitmaps-1].cy,
                    .cw=b[npc_bitmaps-1].cw, .ch=b[npc_bitmaps-1].ch, .rgba=last_img, .native_words=p->words, .native_crc=p->crc};
                note("mods: %s/%s: automatic %s %u.%u bitmap (the same picture), original-stream guard enabled", mod, rel, kind?"PICKUP":"ROOM",p->block,p->id);
                continue;
            }
            pc_bitmap_source_add(mod, rel); mod_hash_file(rel, path);
            long n = 0; int iw = 0, ih = 0, channels;
            uint8_t *data = load(path, &n);
            int good = data && n > 0 && n <= INT32_MAX && stbi_info_from_memory(data, (int)n, &iw, &ih, &channels) && iw == 512 && ih == 512;
            uint8_t *img = good ? stbi_load_from_memory(data, (int)n, &iw, &ih, &channels, 4) : NULL;
            free(data);
            if (img && (iw != 512 || ih != 512)) { stbi_image_free(img); img = NULL; }
            size_t bytes = 512u * 512u * 4u;
            int visible = 0, bx0 = 512, by0 = 512, bx1 = 0, by1 = 0;
            for (size_t k = 0; img && k < bytes; k += 4) {
                if (!(img[k] | img[k+1] | img[k+2])) img[k+3] = 0;
                if (img[k+3]) {
                    int px = (int)(k / 4 % 512), py = (int)(k / 4 / 512);
                    visible = 1;
                    if (px < bx0) bx0 = px;
                    if (px >= bx1) bx1 = px + 1;
                    if (py < by0) by0 = py;
                    if (py >= by1) by1 = py + 1;
                }
            }
            if (visible) {                       /* keep only what shows: the rest is see-through */
                int cw = bx1 - bx0, ch = by1 - by0;
                uint8_t *crop = malloc((size_t)cw * ch * 4);
                if (crop) {
                    for (int r = 0; r < ch; r++) memcpy(crop + (size_t)r * cw * 4, img + ((size_t)(by0 + r) * 512 + bx0) * 4, (size_t)cw * 4);
                    stbi_image_free(img);
                    img = crop;
                    bytes = (size_t)cw * ch * 4;
                } else { bx0 = by0 = 0; bx1 = by1 = 512; }
            }
            if (!img || !visible || !pc_picture_fits(0,bytes)) {
                stbi_image_free(img);
                note("mods: %s/%s: invalid/empty picture or canvas (requires 512x512), or bitmap budget: not used", mod, rel); continue;
            }
            PcBitmap *b = realloc(pc_bitmaps, (npc_bitmaps + 1) * sizeof *b);
            if (!b) { stbi_image_free(img); note("mods: %s/%s: bitmap allocation failed: not used", mod, rel); continue; }
            pc_bitmaps = b;
            b[npc_bitmaps++] = (PcBitmap){.kind=kind, .block=p->block, .id=p->id, .begin=0, .end=p->words,
                .x=0, .y=0, .w=320, .h=240, .iw=512, .ih=512, .cx=bx0, .cy=by0, .cw=bx1-bx0, .ch=by1-by0,
                .rgba=img, .native_words=p->words, .native_crc=p->crc};
            pc_bitmap_bytes += bytes; nused++;
            snprintf(last_path, sizeof last_path, "%s", path); last_img = img;
            note("mods: %s/%s: automatic %s %u.%u bitmap, original-stream guard enabled", mod, rel, kind?"PICKUP":"ROOM",p->block,p->id);
        }
    }
}
static void pc_bitmaps_load(const char *mod, const char *root) {
    pc_dirs_reset();
    pc_diaries_load(mod, root);
    pc_bitmaps_automatic(mod, root);
    char manifest[1024];
    if (!pc_bitmap_path(root, "pc-bitmaps.txt", manifest)) return;
    long size = 0; uint8_t *raw = load(manifest, &size);
    if (!raw || size < 0 || size > 1024 * 1024) { free(raw); note("mods: %s/pc-bitmaps.txt: invalid manifest size: not used", mod); return; }
    char *buf = malloc((size_t)size + 1);
    if (!buf) { free(raw); return; }
    memcpy(buf, raw, (size_t)size); buf[size] = 0; free(raw);
    mod_hash_file("PC-BITMAPS.TXT", manifest);
    unsigned line_no = 0;
    for (char *line = buf; line;) {
        char *next = strchr(line, '\n'); if (next) *next++ = 0; line_no++;
        char *tok[17]; int nt = 0; char *s = line;
        while (*s) {
            while (isspace((unsigned char)*s)) s++;
            if (!*s || *s == '#' || *s == ';') break;
            if (nt == 17) break;
            tok[nt++] = s; while (*s && !isspace((unsigned char)*s)) s++;
            if (*s) *s++ = 0;
        }
        if (!nt) { line = next; continue; }
        unsigned kind = !strcmp(tok[0], "ROOM") ? 0 : !strcmp(tok[0], "PICKUP") ? 1 : !strcmp(tok[0], "DIARY") ? 2 : 99;
        int ids = kind == 0 ? 4 : kind == 1 ? 3 : 2;
        int path_token = 1 + ids, coords = kind == 2 ? 4 : 8;
        unsigned v[12] = {0}; int good = kind < 3 && nt == path_token + 1 + coords + (kind == 2 ? 0 : 1);
        for (int i = 0; good && i < ids; i++) good = pc_bitmap_uint(tok[1 + i], &v[i]);
        for (int i = 0; good && i < coords; i++) good = pc_bitmap_uint(tok[path_token + 1 + i], &v[ids + i]);
        if (good && kind != 2) good = !strcmp(tok[nt - 1], "alpha") || !strcmp(tok[nt - 1], "black");
        if (good && kind == 0) good = v[0] < 1024 && v[1] < 256 && v[2] < v[3] && v[3] <= 16384;
        if (good && kind == 1) good = v[0] < 96 && v[1] < v[2] && v[2] <= 256;
        if (good && kind == 2) good = v[0] < 13 && v[1] > 0 && v[1] <= 90;
        char path[1024];
        if (!good || !pc_bitmap_path(root, tok[path_token], path)) { note("mods: %s/pc-bitmaps.txt:%u: invalid mapping or unsafe/missing relative picture path: not used", mod, line_no); line = next; continue; }
        pc_bitmap_source_add(mod, tok[path_token]);
        char up[512]; for (size_t i = 0;; i++) { up[i] = tok[path_token][i] == '\\' ? '/' : (char)toupper((unsigned char)tok[path_token][i]); if (!tok[path_token][i]) break; }
        mod_hash_file(up, path);
        long n = 0; uint8_t *data = load(path, &n); int iw = 0, ih = 0, c;
        int dimensions = data && n > 0 && n <= INT32_MAX && stbi_info_from_memory(data, (int)n, &iw, &ih, &c) && iw > 1 && ih > 1 && iw <= 4096 && ih <= 4096;
        uint8_t *img = dimensions ? stbi_load_from_memory(data, (int)n, &iw, &ih, &c, 4) : NULL; free(data);
        unsigned *r = v + ids;
        good = img && iw > 1 && ih > 1 && iw <= 4096 && ih <= 4096 && r[2] && r[3] && (uint64_t)r[0] + r[2] <= (unsigned)iw && (uint64_t)r[1] + r[3] <= (unsigned)ih;
        if (good && kind != 2) good = r[4] <= 320 && r[5] <= 240 && r[6] && r[7] && (uint64_t)r[4] + r[6] <= 320 && (uint64_t)r[5] + r[7] <= 240;
        uint64_t bytes = (uint64_t)r[2] * r[3] * 4;
        if (bytes > (UINT64_C(64) << 20) || !pc_picture_fits(0,bytes)) good = 0;
        uint8_t *crop = good ? malloc((size_t)bytes) : NULL;
        if (!crop) { stbi_image_free(img); note("mods: %s/pc-bitmaps.txt:%u: invalid/placeholder picture, rectangle or bitmap memory budget: not used", mod, line_no); line = next; continue; }
        for (unsigned y = 0; y < r[3]; y++) memcpy(crop + (size_t)y * r[2] * 4, img + ((size_t)(r[1] + y) * iw + r[0]) * 4, (size_t)r[2] * 4);
        stbi_image_free(img);
        if (kind == 2) {
            char rel[64]; snprintf(rel, sizeof rel, "pc-bitmaps.txt:%u", line_no);
            diary_apply_picture(mod, rel, v[0], v[1], crop, (int)r[2], (int)r[3]); free(crop);
        } else {
            /* PC black-key bitmaps need this explicitly; alpha preserves opaque black. */
            if (!strcmp(tok[nt - 1], "black")) for (uint64_t i = 0; i < bytes; i += 4) if (!(crop[i] | crop[i + 1] | crop[i + 2])) crop[i + 3] = 0;
            int visible = 0;
            for (uint64_t i = 3; i < bytes; i += 4) if (crop[i]) { visible = 1; break; }
            if (!visible) { free(crop); note("mods: %s/pc-bitmaps.txt:%u: empty raster after transparency: not used", mod, line_no); line = next; continue; }
            PcBitmap *p = realloc(pc_bitmaps, (npc_bitmaps + 1) * sizeof *p);
            if (!p) { free(crop); note("mods: %s/pc-bitmaps.txt:%u: allocation failed: not used", mod, line_no); line = next; continue; }
            pc_bitmaps = p; p += npc_bitmaps++;
            *p = (PcBitmap){kind, kind == 0 ? v[0] : 0, kind == 0 ? v[1] : v[0], v[ids - 2], v[ids - 1], (int)r[4], (int)r[5], (int)r[6], (int)r[7], (int)r[2], (int)r[3], 0, 0, 0, 0, crop, 0, 0};
            pc_bitmap_bytes += (size_t)bytes; nused++;
            note("mods: %s/pc-bitmaps.txt:%u: authored %s bitmap, original controls retained", mod, line_no, tok[0]);
        }
        line = next;
    }
    free(buf);
}

/* Resolve the current native cursor, not a guessed PC filename or display
 * timer. All bounds are checked before reading optional game tables. */
static uint32_t pc_ram32(uint32_t a) { return rd32(ram_ptr(a)); }
static uint16_t pc_ram16(uint32_t a) { return rd16(ram_ptr(a)); }
/* Final compositing cannot reproduce an ordering-table fade above text.
 * While an original screen overlay is present, keep its native glyphs
 * and ordering instead of painting a raster over the completed frame. */
static int pc_bitmap_unoccluded(void) {
    return !pc_ram16(0x800A55A2u) && !*ram_ptr(0x800BBD10u) && !pc_ram32(0x800BB1B0u);
}
static int pc_bitmap_stream(const PcBitmap *p, uint32_t root) {
    if (!p->native_words) return 1;
    uint64_t end = (uint64_t)root + (uint64_t)p->native_words * 2;
    if(root<0x80000000u || (root&1) || end>0x80800000u)return 0;
    if((uint32_t)mz_crc32(0,ram_ptr(root),(size_t)p->native_words*2)==p->native_crc)return 1;
    /* Original pickup confirmation replaces FE10 with FF01 until state107.
     * Normalize only that measured suffix and its exact saved owner; never
     * rewrite RAM or weaken guards on the item name/prefix/other controls. */
    if(p->kind!=1 || p->native_words<3)return 0;
    uint32_t after=root+(p->native_words-2)*2;
    if(pc_ram32(0x800BA9D8u)!=after || pc_ram16(0x800BA9D4u)!=0xFE10 ||
       pc_ram16(after-2)!=0xFF01 || pc_ram16(after)!=0xFF01 || pc_ram16(after+2)!=0xFE00)return 0;
    static const uint8_t wait[]={0x10,0xFE};
    unsigned long crc=mz_crc32(0,ram_ptr(root),(p->native_words-3)*2);
    crc=mz_crc32(crc,wait,2);crc=mz_crc32(crc,ram_ptr(after),4);
    return (uint32_t)crc==p->native_crc;
}
/* A reference image describes a complete text, not its individual pages.
 * Retain native glyphs if the actual box/caption splits that text. */
static int pc_bitmap_single_page(const PcBitmap *p, uint32_t root, uint32_t text, int caption) {
    if (!p->native_words) return 1;
    if (text != root) return 0;
    unsigned breaks = 0;
    for (uint32_t i = 0; i < p->native_words; i++) if (pc_ram16(root + i * 2) == 0xFFFE) breaks++;
    if (caption) return breaks < 2;
    for (unsigned i = 0; i < 8; i++) {
        uint32_t e = 0x800BA8E8u + i * 16;
        if (pc_ram32(e) == text && breaks <= pc_ram16(e + 10)) return 1;
    }
    return 0;
}
static const PcBitmap *pc_bitmap_find(uint32_t text) {
    if (!npc_bitmaps || !pc_bitmap_unoccluded() || pc_ram32(0x800BA9C4)) return NULL; /* native choices/highlights */
    unsigned kind = 99, block = 0, id = 0, offset = 0;
    uint32_t stream = 0;
    if (text >= 0x800A88C8u && text < 0x800A8AC8u && !(text & 1) && pc_pickup_id < 96) {
        kind = 1; id = pc_pickup_id; offset = (text - 0x800A88C8u) / 2; stream=0x800A88C8u;
    } else {
        uint32_t tab = pc_ram32(0x800BBB7C), stage = pc_ram32(0x800B4BA0), room = pc_ram32(0x800B3E20);
        if (tab < 0x80000000u || tab > 0x80800000u - 1024 || (tab & 1) || stage > 34 || room >= 30 || stage * 30 + room >= 1024) return NULL;
        unsigned count = pc_ram16(tab); if (!count || count > 255) return NULL;
        uint32_t root = 0; int ambiguous = 0;
        for (unsigned i = 0; i < count; i++) {
            int off = (int16_t)pc_ram16(tab + 4 + i * 4); off = off / 2 * 2;
            if (off < (int)(4 + count * 4)) continue;
            uint64_t at = (uint64_t)tab + (unsigned)off;
            if (at <= text && text < at + 32768 && at >= root && at < 0x80800000u) {
                if (at == root) ambiguous = 1;
                else { root = (uint32_t)at; id = i; ambiguous = 0; }
            }
        }
        if (!root || ambiguous || (text & 1)) return NULL;
        kind = 0; block = stage * 30 + room; offset = (text - root) / 2; stream = root;
    }
    for (size_t i = npc_bitmaps; i-- > 0;) {
        const PcBitmap *p = &pc_bitmaps[i];
        if (p->kind == kind && p->block == block && p->id == id && offset >= p->begin && offset < p->end && pc_bitmap_stream(p, stream) && pc_bitmap_single_page(p, stream, text, 0)) return p;
    }
    return NULL;
}
void modfs_pc_caption(uint32_t index, uint32_t offset) {
    pc_caption_block = pc_caption_index = pc_caption_offset = UINT32_MAX;
    if (!npc_bitmaps || offset >= 256) return;
    uint32_t tab = pc_ram32(0x800BBB7C), stage = pc_ram32(0x800B4BA0), room = pc_ram32(0x800B3E20);
    if (tab < 0x80000000u || tab > 0x80800000u - 1024 || (tab & 1) || stage > 34 || room >= 30 || stage * 30 + room >= 1024) return;
    unsigned count = pc_ram16(tab); if (!count || count > 255 || index >= count) return;
    pc_caption_block = stage * 30 + room; pc_caption_index = index; pc_caption_offset = offset;
}
static const PcBitmap *pc_caption_find(void) {
    if (!npc_bitmaps || !pc_bitmap_unoccluded() || pc_ram32(0x800BA9C4) || pc_ram32(0x800BA9E0) < 2 || pc_caption_block == UINT32_MAX) return NULL;
    uint32_t text = pc_ram32(0x800BA9E8);
    if (text < 0x800BB570u || text >= 0x800BB770u || (text & 1) || (text - 0x800BB570u) / 2 != pc_caption_offset ||
        (uint64_t)pc_ram32(0x800B4BA0) * 30 + pc_ram32(0x800B3E20) != pc_caption_block) return NULL;
    uint32_t tab = pc_ram32(0x800BBB7C);
    if (tab < 0x80000000u || tab > 0x80800000u - 1024 || (tab & 1)) return NULL;
    unsigned count = pc_ram16(tab);
    if (!count || count > 255 || pc_caption_index >= count) return NULL;
    int off = (int16_t)pc_ram16(tab + 4 + pc_caption_index * 4); off = off / 2 * 2;
    if (off < (int)(4 + count * 4)) return NULL;
    uint32_t stream = tab + (unsigned)off;
    for (size_t i = npc_bitmaps; i-- > 0;) {
        const PcBitmap *p = &pc_bitmaps[i];
        if (!p->kind && p->block == pc_caption_block && p->id == pc_caption_index && pc_caption_offset >= p->begin && pc_caption_offset < p->end && pc_bitmap_stream(p, stream) && pc_bitmap_single_page(p, stream, stream + pc_caption_offset * 2, 1)) return p;
    }
    return NULL;
}
static int pc_bitmap_covers(const PcBitmap *p, int x, int y, int w, int h) {
    if (!p || w <= 0 || h <= 0 || x < p->x || y < p->y || (int64_t)x + w > p->x + p->w || (int64_t)y + h > p->y + p->h) return 0;
    GpuDisplay d; gpu_display(&d);
    return d.enabled && !d.rgb24 && d.w >= 320 && d.h == 240;
}
int modfs_pc_caption_message(int x, int y, int w, int h) { return pc_bitmap_covers(pc_caption_find(), x, y, w, h); }
int modfs_pc_message(uint32_t text, int x, int y, int w, int h) {
    if(pc_diary_title_message(text,x,y,w,h))return 1;
    const PcBitmap *p = pc_bitmap_find(text);
    return pc_bitmap_covers(p, x, y, w, h);
}
void modfs_pc_composite(uint32_t *out, int w, int h, int native_w, int native_h) {
    if ((!npc_bitmaps && !modfs_pc_has_diaries()) || !out || w <= 0 || h <= 0 || native_w < 320 || native_h != 240) return;
    pc_diary_composite(out, w, h, native_w);
    pc_book_composite(out,w,h,native_w);
    pc_diary_titles_composite(out,w,h,native_w);
    pc_diary_keys_composite(out,w,h,native_w);
    for (unsigned e = 0; e < 9; e++) {
        const PcBitmap *p = e == 8 ? pc_caption_find() : pc_ram32(0x800BA9F4) ? pc_bitmap_find(pc_ram32(0x800BA8E8 + e * 16)) : NULL; if (!p) continue;
        int lx = p->x + (native_w - 320) / 2;
        int x0 = (int)((int64_t)lx * w / native_w), x1 = (int)((int64_t)(lx + p->w) * w / native_w);
        int y0 = (int)((int64_t)p->y * h / 240), y1 = (int)((int64_t)(p->y + p->h) * h / 240);
        if (x1 <= x0 || y1 <= y0) continue;
        for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) {
            int sx = (int)((int64_t)(x - x0) * p->iw / (x1 - x0)), sy = (int)((int64_t)(y - y0) * p->ih / (y1 - y0));
            const uint8_t *s;
            if (p->cw) {                         /* outside the kept part: see-through */
                if (sx < p->cx || sx >= p->cx + p->cw || sy < p->cy || sy >= p->cy + p->ch) continue;
                s = p->rgba + ((size_t)(sy - p->cy) * p->cw + (size_t)(sx - p->cx)) * 4;
            } else s = p->rgba + ((size_t)sy * p->iw + (size_t)sx) * 4;
            uint32_t old = out[(size_t)y * w + x], v = 0, a = s[3];
            for (int c = 0; c < 3; c++) v |= ((s[2 - c] * a + ((old >> (8 * c)) & 255) * (255 - a) + 127) / 255) << (8 * c);
            out[(size_t)y * w + x] = v;
        }
    }
}
