/* HD texture packs: replacement images for the game's textures, in the
 * format Beetle PSX HW (and so most PS1 texture packs) uses.
 *
 * A pack is a folder of images named <texture hash>-<palette hash>.png:
 *   texture hash  CRC-32 of the 16-bit VRAM words of one upload to VRAM
 *                 (the rectangle the game sent, as sent)
 *   palette hash  CRC-32 of the CLUT the texture is drawn with (16 entries
 *                 for 4-bit textures, 256 for 8-bit), 0 for 16-bit ones
 * The image covers that upload in texels (4 per word at 4 bits, 2 at 8),
 * scaled up by a whole factor, with the tri-alpha convention: alpha 0 is
 * transparent, about half is a semi-transparent texel (the PS1's STP
 * bit), full is opaque.
 *
 * Uploads are hashed as the game makes them (only when the pack has a
 * texture with that hash) and remembered with where they are; a fill or a
 * copy over one forgets it, a copy of one moves it along. A textured
 * primitive whose texels all lie in a remembered upload, drawn with a CLUT
 * the pack has an image for, is drawn from that image (render.c) at the
 * high resolution. Images load on a background thread (first sighting:
 * the original shows for a moment) and are kept up to a memory budget.
 *
 * Only the high-resolution picture changes: the PS1's own VRAM, and so
 * everything the game sees, stays the same. */
#ifdef _WIN32
#include <windows.h>
#else
#include <dirent.h>
#include <pthread.h>
#include <unistd.h>
#endif

#include "rt.h"
#include "render.h"
#include "savestate.h"
#include "texpack.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_ONLY_BMP
#define STBI_ONLY_TGA
#define STBI_NO_STDIO
#define STBI_FAILURE_USERMSG
#include "stb/stb_image.h"
#pragma GCC diagnostic pop
#include <time.h>

unsigned long mz_crc32(unsigned long crc, const unsigned char *ptr, size_t buf_len);   /* miniz */

int g_hd_textures = 1;                     /* setting */
size_t g_hd_budget = (size_t)1536 << 20;    /* decoded images kept, bytes */

enum { E_NONE, E_QUEUED, E_READY, E_BAD };

typedef struct Entry {
    uint64_t key;                           /* texture hash << 32 | palette hash */
    char *name;
    volatile int state;
    HdImg img;
    unsigned last_used;
    struct Entry *next_done;
} Entry;

static char pack_dir[1024];
static Entry *entries;
static int nentries;
static uint32_t *tex_hashes;               /* sorted, unique */
static int ntex;

/* uploads remembered (newest last): the part of the upload still in VRAM
 * (x, y, w, h; part of it can be overwritten by a later upload), and the
 * whole upload, which the pack's image covers */
typedef struct { int32_t x, y, w, h, ox, oy, ow, oh; uint32_t hash, live_crc; } Tracked;
static Tracked *tracked;
static uint32_t ntracked, cap_tracked;
#define MAX_TRACKED 4096

static size_t bytes_ready;
static struct { unsigned lookups, in_upload, found, drawn, loads, evicted; } st;

/* ------------------------------------------------------------ index */

static int cmp_entry(const void *a, const void *b) {
    uint64_t x = ((const Entry *)a)->key, y = ((const Entry *)b)->key;
    return x < y ? -1 : x > y;
}

static int cmp_u32(const void *a, const void *b) {
    uint32_t x = *(const uint32_t *)a, y = *(const uint32_t *)b;
    return x < y ? -1 : x > y;
}

static void add_name(const char *name) {
    unsigned h, p;
    int n = 0;
    if (sscanf(name, "%x-%x.%n", &h, &p, &n) != 2 || n == 0) {
        p = 0;                                       /* "<hash>.png": a 16-bit texture (older dumps) */
        if (sscanf(name, "%x.%n", &h, &n) != 1 || n == 0) return;
    }
    const char *ext = name + n;
    if (strcasecmp(ext, "png") && strcasecmp(ext, "jpg") && strcasecmp(ext, "jpeg") && strcasecmp(ext, "bmp") &&
        strcasecmp(ext, "tga"))
        return;
    static int cap;
    if (nentries == cap) {
        cap = cap ? cap * 2 : 1024;
        entries = realloc(entries, (size_t)cap * sizeof *entries);
    }
    Entry *e = &entries[nentries++];
    memset(e, 0, sizeof *e);
    e->key = (uint64_t)h << 32 | p;
    e->name = strdup(name);
}

static void build_index(void) {
    qsort(entries, (size_t)nentries, sizeof *entries, cmp_entry);
    /* the same texture and palette in two formats: keep the first (png sorts before others by name? keep any) */
    int k = 0;
    for (int i = 0; i < nentries; i++) {
        if (k && entries[k - 1].key == entries[i].key) { free(entries[i].name); continue; }
        entries[k++] = entries[i];
    }
    nentries = k;
    tex_hashes = malloc((size_t)(nentries ? nentries : 1) * sizeof *tex_hashes);
    ntex = 0;
    for (int i = 0; i < nentries; i++) {
        uint32_t h = (uint32_t)(entries[i].key >> 32);
        if (!ntex || tex_hashes[ntex - 1] != h) tex_hashes[ntex++] = h;
    }
}

static Entry *find_entry(uint32_t hash, uint32_t pal) {
    uint64_t key = (uint64_t)hash << 32 | pal;
    int lo = 0, hi = nentries;
    while (lo < hi) {
        int mid = (lo + hi) / 2;
        if (entries[mid].key < key) lo = mid + 1;
        else hi = mid;
    }
    return lo < nentries && entries[lo].key == key ? &entries[lo] : NULL;
}

static int has_texture(uint32_t hash) {
    return bsearch(&hash, tex_hashes, (size_t)ntex, sizeof *tex_hashes, cmp_u32) != NULL;
}

/* ------------------------------------------------------------ loading */

static Entry **lq;                          /* load requests (ring) */
static int lq_head, lq_tail, lq_cap;
static Entry *done_list;
static int loader_started;
#ifdef _WIN32
static CRITICAL_SECTION mtx;
static CONDITION_VARIABLE cv;
#define LOCK() EnterCriticalSection(&mtx)
#define UNLOCK() LeaveCriticalSection(&mtx)
#define WAIT() SleepConditionVariableCS(&cv, &mtx, INFINITE)
#define WAKE() WakeConditionVariable(&cv)
#else
static pthread_mutex_t mtx = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t cv = PTHREAD_COND_INITIALIZER;
#define LOCK() pthread_mutex_lock(&mtx)
#define UNLOCK() pthread_mutex_unlock(&mtx)
#define WAIT() pthread_cond_wait(&cv, &mtx)
#define WAKE() pthread_cond_signal(&cv)
#endif

static void load_one(Entry *e) {
    char path[1400];
    snprintf(path, sizeof path, "%s/%s", pack_dir, e->name);
    FILE *f = fopen(path, "rb");
    uint8_t *data = NULL;
    long n = 0;
    if (f) {
        fseek(f, 0, SEEK_END);
        n = ftell(f);
        fseek(f, 0, SEEK_SET);
        data = malloc((size_t)(n > 0 ? n : 1));
        if (fread(data, 1, (size_t)n, f) != (size_t)n) n = 0;
        fclose(f);
    }
    int w = 0, h = 0, c = 0;
    uint8_t *px = n > 0 ? stbi_load_from_memory(data, (int)n, &w, &h, &c, 4) : NULL;
    free(data);
    e->img.w = w;
    e->img.h = h;
    e->img.px = (uint32_t *)px;
}

#ifdef _WIN32
static DWORD WINAPI loader(LPVOID arg) {
#else
static void *loader(void *arg) {
#endif
    (void)arg;
    for (;;) {
        LOCK();
        while (lq_head == lq_tail) WAIT();
        Entry *e = lq[lq_head];
        lq_head = (lq_head + 1) % lq_cap;
        UNLOCK();
        load_one(e);
        LOCK();
        e->next_done = done_list;
        done_list = e;
        UNLOCK();
    }
    return 0;
}

/* Decoding a big PNG takes a while: half the cores (2 to 8) at it, so a
 * room's textures are in soon after it loads. */
static int loader_threads(void) {
#ifdef _WIN32
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    int n = (int)si.dwNumberOfProcessors / 2;
#else
    int n = (int)sysconf(_SC_NPROCESSORS_ONLN) / 2;
#endif
    return n < 2 ? 2 : n > 8 ? 8 : n;
}

static void start_loader(void) {
    if (loader_started) return;
    loader_started = 1;
    lq_cap = 8192;
    lq = malloc((size_t)lq_cap * sizeof *lq);
    int nt = loader_threads();
#ifdef _WIN32
    InitializeCriticalSection(&mtx);
    InitializeConditionVariable(&cv);
    for (int i = 0; i < nt; i++) CloseHandle(CreateThread(NULL, 0, loader, NULL, 0, NULL));
#else
    for (int i = 0; i < nt; i++) {
        pthread_t t;
        pthread_create(&t, NULL, loader, NULL);
        pthread_detach(t);
    }
#endif
}

/* now: being drawn, so ahead of the ones fetched in advance */
static void request_at(Entry *e, int now) {
    if (e->state != E_NONE) return;
    start_loader();
    LOCK();
    if ((lq_tail + 1) % lq_cap != lq_head) {
        if (now) {
            lq_head = (lq_head + lq_cap - 1) % lq_cap;
            lq[lq_head] = e;
        } else {
            lq[lq_tail] = e;
            lq_tail = (lq_tail + 1) % lq_cap;
        }
        e->state = E_QUEUED;
        st.loads++;
        WAKE();
    }
    UNLOCK();
}

static void request(Entry *e) { request_at(e, 0); }

/* ------------------------------------------------------------ setup */

int texpack_open(const char *dir) {
    nentries = 0;
#ifdef _WIN32
    char pat[1100];
    snprintf(pat, sizeof pat, "%s\\*", dir);
    WIN32_FIND_DATAA fd;
    HANDLE h = FindFirstFileA(pat, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) add_name(fd.cFileName);
        } while (FindNextFileA(h, &fd));
        FindClose(h);
    }
#else
    DIR *d = opendir(dir);
    if (d) {
        struct dirent *de;
        while ((de = readdir(d))) add_name(de->d_name);
        closedir(d);
    }
#endif
    snprintf(pack_dir, sizeof pack_dir, "%s", dir);
    build_index();
    rt_log("texpack: %s: %d images for %d textures", dir, nentries, ntex);
    return nentries > 0;
}

/* Testing without the images: names from a listing (one per line). */
int texpack_open_listing(const char *list, const char *dir) {
    FILE *f = fopen(list, "r");
    if (!f) return 0;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char name[256];
        if (sscanf(line, "%255s", name) == 1) add_name(name);
    }
    fclose(f);
    snprintf(pack_dir, sizeof pack_dir, "%s", dir ? dir : ".");
    build_index();
    rt_log("texpack: listing %s: %d images for %d textures", list, nentries, ntex);
    return nentries > 0;
}

int texpack_loaded(void) { return nentries > 0; }

/* ------------------------------------------------------------ VRAM writes */

static struct { int x, y, n; uint32_t hash; } clut_cache[64];
static int nclut;

static int overlaps(const Tracked *t, int x, int y, int w, int h) {
    return x < t->x + t->w && t->x < x + w && y < t->y + t->h && t->y < y + h;
}

static uint32_t hash_rect(int x, int y, int w, int h);

static void push(Tracked t) {
    if (ntracked == MAX_TRACKED) {
        memmove(tracked, tracked + 1, (MAX_TRACKED - 1) * sizeof *tracked);
        ntracked--;
    }
    if (ntracked == cap_tracked) {
        cap_tracked = cap_tracked ? cap_tracked * 2 : 256;
        tracked = realloc(tracked, cap_tracked * sizeof *tracked);
    }
    t.live_crc = hash_rect(t.x, t.y, t.w, t.h);
    tracked[ntracked++] = t;
}

/* VRAM rewritten: what's left of each upload there stays (up to four
 * pieces around the rewritten area), like Beetle PSX HW's tracker. */
static void forget(int x, int y, int w, int h) {
    Tracked keep[4 * 64];
    int nk = 0;
    uint32_t k = 0;
    for (uint32_t i = 0; i < ntracked; i++) {
        Tracked t = tracked[i];
        if (!overlaps(&t, x, y, w, h)) { tracked[k++] = t; continue; }
        int x2 = x + w, y2 = y + h, tx2 = t.x + t.w, ty2 = t.y + t.h;
        int my0 = y > t.y ? y : t.y, my1 = y2 < ty2 ? y2 : ty2;
        Tracked piece[4];
        int np = 0;
        if (y > t.y) { piece[np] = t; piece[np].h = y - t.y; np++; }                         /* above */
        if (y2 < ty2) { piece[np] = t; piece[np].y = y2; piece[np].h = ty2 - y2; np++; }       /* below */
        if (x > t.x) { piece[np] = t; piece[np].y = my0; piece[np].h = my1 - my0; piece[np].w = x - t.x; np++; }   /* left */
        if (x2 < tx2) { piece[np] = t; piece[np].x = x2; piece[np].w = tx2 - x2; piece[np].y = my0; piece[np].h = my1 - my0; np++; }
        for (int j = 0; j < np && nk < 4 * 64; j++)
            if (piece[j].w > 0 && piece[j].h > 0) keep[nk++] = piece[j];
    }
    ntracked = k;
    for (int j = 0; j < nk; j++) push(keep[j]);
}

static void remember(int x, int y, int w, int h, uint32_t hash) {
    push((Tracked){x, y, w, h, x, y, w, h, hash, 0});
}

static uint32_t hash_rect(int x, int y, int w, int h) {
    static uint16_t *buf;
    static size_t cap;
    size_t n = (size_t)w * h;
    if (n > cap) { cap = n; buf = realloc(buf, cap * sizeof *buf); }
    size_t k = 0;
    for (int j = 0; j < h; j++)
        for (int i = 0; i < w; i++) buf[k++] = g_vram[(y + j) & 511][(x + i) & 1023];
    return (uint32_t)mz_crc32(0, (const unsigned char *)buf, n * 2);
}

void texpack_upload(int x, int y, int w, int h) {
    if (!nentries || w <= 0 || h <= 0) return;
    nclut = 0;
    forget(x, y, w, h);
    if (w == 1024 && h == 512) return;               /* all of VRAM (a save state's) */
    uint32_t hash = hash_rect(x, y, w, h);
    { static int ul = -1; if (ul < 0) ul = getenv("RT_TEXPACK_UPLOADS") != NULL;
      if (ul) rt_log("texpack upload %d,%d %dx%d hash %x%s", x, y, w, h, hash, has_texture(hash) ? " (in pack)" : ""); }
    if (!has_texture(hash)) return;
    remember(x, y, w, h, hash);
    /* fetch all its palettes now: it's about to be drawn */
    if (g_hd_textures) {
        Entry *e = find_entry(hash, 0);
        int i = e ? (int)(e - entries) : 0;
        if (!e) {
            uint64_t key = (uint64_t)hash << 32;
            int lo = 0, hi = nentries;
            while (lo < hi) { int mid = (lo + hi) / 2; if (entries[mid].key < key) lo = mid + 1; else hi = mid; }
            i = lo;
        }
        for (; i < nentries && (uint32_t)(entries[i].key >> 32) == hash; i++) request(&entries[i]);
    }
}

void texpack_fill(int x, int y, int w, int h) {
    if (!ntracked) return;
    nclut = 0;
    forget(x, y, w, h);
}

void texpack_copy(int sx, int sy, int dx, int dy, int w, int h) {
    if (!ntracked) return;
    nclut = 0;
    Tracked moved[64];
    int nm = 0;
    for (uint32_t i = 0; i < ntracked && nm < 64; i++) {
        Tracked *t = &tracked[i];
        if (t->x >= sx && t->y >= sy && t->x + t->w <= sx + w && t->y + t->h <= sy + h) {
            moved[nm] = *t;
            moved[nm].x += dx - sx;
            moved[nm].y += dy - sy;
            moved[nm].ox += dx - sx;
            moved[nm].oy += dy - sy;
            nm++;
        }
    }
    forget(dx, dy, w, h);
    for (int i = 0; i < nm; i++) push(moved[i]);
}

/* ------------------------------------------------------------ drawing */

static uint32_t clut_hash(int x, int y, int n) {
    for (int i = 0; i < nclut; i++)
        if (clut_cache[i].x == x && clut_cache[i].y == y && clut_cache[i].n == n) return clut_cache[i].hash;
    uint32_t h = hash_rect(x, y, n, 1);
    if (nclut < 64) clut_cache[nclut++] = (typeof(clut_cache[0])){x, y, n, h};
    return h;
}

void texpack_attach(RPrim *p, int ax, int ay, int aw, int ah) {
    p->hd = NULL;
    if (!g_hd_textures || !ntracked || !p->textured || p->twmx || p->twmy) return;
    st.lookups++;
    const Tracked *t = NULL;
    /* the texels it draws all in one upload; or all but the last column or
     * row (the edge ones the PS1 hardly draws, often just past it) */
    for (int pass = 0; pass < 2 && !t; pass++) {
        int w = aw - (pass && aw > 1), h = ah - (pass && ah > 1);
        for (int i = (int)ntracked - 1; i >= 0; i--) {
            const Tracked *c = &tracked[i];
            if (ax >= c->x && ay >= c->y && ax + w <= c->x + c->w && ay + h <= c->y + c->h) { t = c; break; }
        }
    }
    if (!t) {
        static int miss = -1;
        if (miss < 0) miss = getenv("RT_TEXPACK_MISS") != NULL;
        if (miss) {
            char buf[512];
            int n = snprintf(buf, sizeof buf, "texpack miss: depth %d tp %d,%d area %d,%d %dx%d uv (%d,%d)(%d,%d)(%d,%d) over:", p->depth, p->tx, p->ty, ax, ay, aw, ah,
                             p->v[0].u, p->v[0].v, p->v[1].u, p->v[1].v, p->v[2].u, p->v[2].v);
            for (uint32_t i = 0; i < ntracked && n < 480; i++)
                if (overlaps(&tracked[i], ax, ay, aw, ah))
                    n += snprintf(buf + n, sizeof buf - (size_t)n, " [%d,%d %dx%d]", tracked[i].x, tracked[i].y, tracked[i].w, tracked[i].h);
            rt_log("%s", buf);
        }
        return;
    }
    st.in_upload++;
    uint32_t pal = p->depth == 2 ? 0 : clut_hash(p->clut_x, p->clut_y, p->depth == 0 ? 16 : 256);
    Entry *e = find_entry(t->hash, pal);
    if (!e) {
        static int miss = -1;
        if (miss < 0) miss = getenv("RT_TEXPACK_MISS") != NULL;
        if (miss) rt_log("texpack nopal: upload %08x at %d,%d %dx%d, palette %08x (clut %d,%d depth %d)", t->hash, t->x, t->y, t->w, t->h, pal, p->clut_x, p->clut_y, p->depth);
        return;
    }
    st.found++;
    {
        static int hitlog = -1;
        if (hitlog < 0) hitlog = getenv("RT_TEXPACK_HITS") != NULL;
        if (hitlog && e->last_used != 0xFFFFFFFFu) {
            static Entry *seen[4096]; static int nseen; int k;
            for (k = 0; k < nseen && seen[k] != e; k++) {}
            if (k == nseen && nseen < 4096) { seen[nseen++] = e; rt_log("texpack hit: %s upload %d,%d %dx%d depth %d ", e->name, t->x, t->y, t->w, t->h, p->depth); }
        }
    }
    e->last_used = g_frame;
    static int sync_load = -1;                       /* testing: RT_TEXPACK_SYNC loads right away */
    if (sync_load < 0) sync_load = getenv("RT_TEXPACK_SYNC") != NULL;
    if (sync_load && (e->state == E_NONE || e->state == E_QUEUED)) {
        if (e->state == E_QUEUED) texpack_wait();
        else {
            load_one(e);
            if (e->img.px) { e->state = E_READY; bytes_ready += (size_t)e->img.w * e->img.h * 4; }
            else e->state = E_BAD;
        }
    }
    if (e->state == E_NONE) request_at(e, 1);
    if (e->state != E_READY) return;
    int ppp = p->depth == 0 ? 4 : p->depth == 1 ? 2 : 1;
    int tw = t->ow * ppp, th = t->oh;
    if (e->img.w % tw || e->img.h % th) {
        rt_log("texpack: %s is %dx%d, not a multiple of the texture's %dx%d: not used", e->name, e->img.w, e->img.h, tw, th);
        free(e->img.px);
        e->img.px = NULL;
        e->state = E_BAD;
        return;
    }
    float kx = (float)(e->img.w / tw), ky = (float)(e->img.h / th);
    p->hd = &e->img;
    p->hd_kx = kx;
    p->hd_ky = ky;
    p->hd_ox = (float)((p->tx - t->ox) * ppp) * kx;
    p->hd_oy = (float)(p->ty - t->oy) * ky;
    st.drawn++;
}

/* Main thread, nothing queued for drawing: take in loaded images, keep to
 * the memory budget (least recently drawn go first). */
void texpack_frame(void) {
    if (!loader_started) return;
    LOCK();
    Entry *d = done_list;
    done_list = NULL;
    UNLOCK();
    for (; d; d = d->next_done) {
        if (!d->img.px) {
            rt_log("texpack: couldn't read %s: %s", d->name, stbi_failure_reason());
            d->state = E_BAD;
            continue;
        }
        d->state = E_READY;
        bytes_ready += (size_t)d->img.w * d->img.h * 4;
    }
    while (bytes_ready > g_hd_budget) {
        Entry *old = NULL;
        for (int i = 0; i < nentries; i++)
            if (entries[i].state == E_READY && entries[i].last_used + 2 < g_frame &&
                (!old || entries[i].last_used < old->last_used))
                old = &entries[i];
        if (!old) break;
        bytes_ready -= (size_t)old->img.w * old->img.h * 4;
        stbi_image_free(old->img.px);
        old->img.px = NULL;
        old->state = E_NONE;
        st.evicted++;
    }
    static unsigned last;
    static int stats = -1;
    if (stats < 0) stats = getenv("RT_TEXPACK_STATS") != NULL;
    if (stats && g_frame >= last + 300) {
        last = g_frame;
        rt_log("texpack: %u prims, %u in a known upload, %u with an image, %u drawn HD; %u tracked, %u loads, %u evicted, %zu MB",
               st.lookups, st.in_upload, st.found, st.drawn, ntracked, st.loads, st.evicted, bytes_ready >> 20);
        memset(&st, 0, sizeof st);
    }
}

/* Waits for every requested image (testing: screenshots with no pop-in). */
void texpack_wait(void) {
    for (;;) {
        LOCK();
        int idle = lq_head == lq_tail;
        UNLOCK();
        int pending = 0;
        for (int i = 0; i < nentries; i++) pending |= entries[i].state == E_QUEUED;
        if (!pending && idle) break;
#ifdef _WIN32
        Sleep(1);
#else
        struct timespec ts = {0, 1000000};
        nanosleep(&ts, NULL);
#endif
        texpack_frame();
    }
    texpack_frame();
}

/* ------------------------------------------------------------ save states */

void texpack_state(StateIO *io) {
    ss_module(io, "texpack");
    ss_buffer(io, "uploads2", (void **)&tracked, &ntracked, &cap_tracked, sizeof *tracked);
}

/* After loading a save state: keep the uploads whose VRAM still matches. */
void texpack_reload(void) {
    nclut = 0;
    uint32_t k = 0;
    for (uint32_t i = 0; i < ntracked; i++) {
        Tracked t = tracked[i];
        if (nentries && hash_rect(t.x, t.y, t.w, t.h) == t.live_crc) tracked[k++] = t;
    }
    ntracked = k;
}
