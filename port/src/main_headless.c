#include <time.h>
/* Headless runner for development: no window. Saves the displayed picture
 * as PNG every N frames and can script button presses.
 *
 *   resurv_headless <disc.bin> [--frames N] [--every K] [--out DIR]
 *                   [--press FRAME:BUTTONS:LEN ...]
 */
#include "rt.h"
#include "render.h"
#include "texpack.h"
#include "savestate.h"

#include <stdio.h>
#include <stdlib.h>
#include <zlib.h>

void rt_set_log(FILE *f);
int rt_boot(const char *disc_path);

static uint32_t max_frames = 600, every = 60;
static const char *outdir = "frames";
static FILE *wav;
static uint32_t wav_frames;
static struct { uint32_t frame, len; uint16_t buttons; } presses[64];
static int npresses;

static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = (uint8_t)v; }

static void png_chunk(FILE *f, const char *type, const uint8_t *data, uint32_t n) {
    uint8_t h[8];
    put32(h, n);
    memcpy(h + 4, type, 4);
    fwrite(h, 1, 8, f);
    if (n) fwrite(data, 1, n, f);
    uLong crc = crc32(0, (const Bytef *)type, 4);
    crc = crc32(crc, data, n);
    uint8_t c[4];
    put32(c, (uint32_t)crc);
    fwrite(c, 1, 4, f);
}

void write_png(const char *path, const uint8_t *rgb, int w, int h) {
    FILE *f = fopen(path, "wb");
    if (!f) return;
    fwrite("\x89PNG\r\n\x1a\n", 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, (uint32_t)w);
    put32(ihdr + 4, (uint32_t)h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    png_chunk(f, "IHDR", ihdr, 13);
    size_t raw_n = (size_t)(w * 3 + 1) * h;
    uint8_t *raw = malloc(raw_n);
    for (int y = 0; y < h; y++) {
        raw[y * (w * 3 + 1)] = 0;
        memcpy(raw + y * (w * 3 + 1) + 1, rgb + (size_t)y * w * 3, (size_t)w * 3);
    }
    uLongf zn = compressBound(raw_n);
    uint8_t *z = malloc(zn);
    compress2(z, &zn, raw, raw_n, 6);
    png_chunk(f, "IDAT", z, (uint32_t)zn);
    png_chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
}

static void dump_vram(const char *path) {
    uint8_t *rgb = malloc(1024 * 512 * 3);
    for (int y = 0; y < 512; y++)
        for (int x = 0; x < 1024; x++) {
            uint16_t p = g_vram[y][x];
            uint8_t *o = rgb + ((size_t)y * 1024 + x) * 3;
            o[0] = (uint8_t)((p & 31) << 3);
            o[1] = (uint8_t)(((p >> 5) & 31) << 3);
            o[2] = (uint8_t)(((p >> 10) & 31) << 3);
        }
    write_png(path, rgb, 1024, 512);
    free(rgb);
}

static void wav_header(FILE *f, uint32_t frames) {
    uint32_t bytes = frames * 4;
    uint8_t h[44] = "RIFF....WAVEfmt ";
    uint32_t v;
    v = 36 + bytes; memcpy(h + 4, &v, 4);
    v = 16; memcpy(h + 16, &v, 4);
    uint16_t s = 1; memcpy(h + 20, &s, 2);
    s = 2; memcpy(h + 22, &s, 2);
    v = 44100; memcpy(h + 24, &v, 4);
    v = 44100 * 4; memcpy(h + 28, &v, 4);
    s = 4; memcpy(h + 32, &s, 2);
    s = 16; memcpy(h + 34, &s, 2);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &bytes, 4);
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, 44, f);
    fseek(f, 0, SEEK_END);
}

void plat_ss_done(int ok, int was_load, const char *msg) {
    rt_log("savestate %s: %s (%s)", was_load ? "load" : "save", ok ? "ok" : "failed", msg);
}

void plat_frame(void) {
    /* RT_FRAMETIME: per frame, the host time the game and the high-res
     * picture took (the picture finished as the PC build's paint does) */
    if (getenv("RT_FRAMETIME")) {
        static struct timespec last;
        struct timespec a, b;
        clock_gettime(CLOCK_MONOTONIC, &a);
        struct timespec m;
        render_sync();
        clock_gettime(CLOCK_MONOTONIC, &m);
        static uint32_t bgrx[2560 * 1920];
        int w, h;
        display_grab_bgrx(bgrx, &w, &h);
        clock_gettime(CLOCK_MONOTONIC, &b);
        if (last.tv_sec)
            { extern unsigned g_sync_count, g_sync_ops; rt_log("syncs %u ops %u", g_sync_count, g_sync_ops); g_sync_count = g_sync_ops = 0; }
            rt_log("frametime game %.1f ms picture %.1f ms (drawing %.1f)", (a.tv_sec - last.tv_sec) * 1e3 + (a.tv_nsec - last.tv_nsec) / 1e6,
                   (b.tv_sec - a.tv_sec) * 1e3 + (b.tv_nsec - a.tv_nsec) / 1e6, (m.tv_sec - a.tv_sec) * 1e3 + (m.tv_nsec - a.tv_nsec) / 1e6);
        last = b;
    }
    {
        /* RT_SAVE_AT=frame:path saves then; RT_LOAD=path loads at the start.
         * RT_SAVE_SKIP=n: the save waits n more safe points (polls), so it
         * lands somewhere inside the game's frame (tools/eq/sscompat.sh) */
        static int done_load;
        const char *e = getenv("RT_SAVE_AT");
        unsigned f;
        char path[512];
        if (e && sscanf(e, "%u:%511s", &f, path) == 2 && f == g_frame) {
            extern int g_ss_skip;
            if (getenv("RT_SAVE_SKIP")) g_ss_skip = atoi(getenv("RT_SAVE_SKIP"));
            ss_request_save(path);
        }
        const char *l = getenv("RT_LOAD");
        if (l && !done_load && g_frame == 2) { done_load = 1; ss_request_load(l); }
        /* RT_LOAD_AT=frame:path loads in the middle of a run */
        const char *la = getenv("RT_LOAD_AT");
        if (la && !done_load && sscanf(la, "%u:%511s", &f, path) == 2 && f == g_frame) { done_load = 1; ss_request_load(path); }
    }
    {
        int16_t buf[4096 * 2];
        int n;
        while ((n = spu_take_samples(buf, 4096)) > 0)
            if (wav) { fwrite(buf, 4, (size_t)n, wav); wav_frames += (uint32_t)n; }
    }
    uint16_t b = 0;
    for (int i = 0; i < npresses; i++)
        if (g_frame >= presses[i].frame && g_frame < presses[i].frame + presses[i].len) b |= presses[i].buttons;
    g_input.buttons = b;
    {
        /* RT_MOUSE=dx,dy,from,to: a PlayStation Mouse in port 2 moving by
         * dx,dy per frame between those frames (for testing FPS mode). */
        static int parsed, mdx, mdy, mfrom, mto;
        if (!parsed) {
            parsed = 1;
            const char *e = getenv("RT_MOUSE");
            if (e && sscanf(e, "%d,%d,%d,%d", &mdx, &mdy, &mfrom, &mto) == 4) g_input.mouse_present = 1;
        }
        /* RT_CLICK=from,to: hold the left mouse button */
        static int cparsed, cfrom, cto;
        if (!cparsed) { cparsed = 1; const char *c = getenv("RT_CLICK"); if (c) sscanf(c, "%d,%d", &cfrom, &cto); }
        g_input.mouse_left = g_frame >= (uint32_t)cfrom && g_frame < (uint32_t)cto;
        /* RT_CLICK_EVERY=n: click repeatedly (n frames down, n up) */
        const char *ce = getenv("RT_CLICK_EVERY");
        if (ce && g_input.mouse_left) g_input.mouse_left = ((g_frame - (uint32_t)cfrom) / (uint32_t)atoi(ce)) % 2 == 0;
        { extern int g_audio_same_sample; if (getenv("RT_SAME_SAMPLE")) g_audio_same_sample = atoi(getenv("RT_SAME_SAMPLE")); }
        /* RT_CHEATS=god,ammo,noreload  RT_RELOAD_AT=frame  RT_POKE16=frame:addr=val */
        extern int g_cheat_god, g_cheat_infinite_ammo, g_cheat_no_reload;
        extern volatile int g_reload_request;
        const char *ch = getenv("RT_CHEATS");
        if (ch) {
            g_cheat_god = strstr(ch, "god") != NULL;
            g_cheat_infinite_ammo = strstr(ch, "ammo") != NULL;
            g_cheat_no_reload = strstr(ch, "noreload") != NULL;
            /* RT_CHEATS_OFF_AT=frame: switch them off then */
            const char *co = getenv("RT_CHEATS_OFF_AT");
            if (co && g_frame >= (uint32_t)atoi(co)) g_cheat_god = g_cheat_infinite_ammo = g_cheat_no_reload = 0;
        }
        /* RT_GIVE_AT=frame  RT_WEAPON_AT=frame:req,frame:req  RT_FOV=degrees */
        extern volatile int g_give_all_request, g_weapon_request;
        extern int g_opt_fov;
        const char *ga = getenv("RT_GIVE_AT");
        if (ga && (uint32_t)atoi(ga) == g_frame) g_give_all_request = 1;
        for (const char *q = getenv("RT_WEAPON_AT"); q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL) {
            unsigned wf; int wr;
            if (sscanf(q, "%u:%d", &wf, &wr) == 2 && wf == g_frame) g_weapon_request = wr;
        }
        if (getenv("RT_FOV")) g_opt_fov = atoi(getenv("RT_FOV"));
        const char *ra = getenv("RT_RELOAD_AT");
        if (ra && (uint32_t)atoi(ra) == g_frame) g_reload_request = 1;
        const char *pk = getenv("RT_POKE16");
        unsigned pf, pa, pv;
        for (const char *q = pk; q && *q; q = strchr(q, ';') ? strchr(q, ';') + 1 : NULL)
            if (sscanf(q, "%u:%x=%x", &pf, &pa, &pv) == 3 && pf == g_frame) MEM_W16(pa, pv);
        if (getenv("RT_PCLOG") && g_frame % 50 == 0) rt_log("pc %08X ra %08X", g_poll_pc, R[31]);
        /* RT_GSAVE_AT=frame:slot  RT_GLOAD_AT=frame:slot  RT_GUSE_AT=frame,frame */
        {
            extern volatile int g_sg_use;
            void sg_request_save(int), sg_set_dir(const char *);
            int sg_request_load(int);
            static int dir_set;
            if (!dir_set) { dir_set = 1; if (getenv("RT_GDIR")) sg_set_dir(getenv("RT_GDIR")); }
            unsigned gf; int gs;
            const char *gsv = getenv("RT_GSAVE_AT"), *glo = getenv("RT_GLOAD_AT");
            if (gsv && sscanf(gsv, "%u:%d", &gf, &gs) == 2 && gf == g_frame) sg_request_save(gs);
            if (glo && sscanf(glo, "%u:%d", &gf, &gs) == 2 && gf == g_frame)
                rt_log("savegame: load request -> %d", sg_request_load(gs));
            for (const char *q = getenv("RT_GUSE_AT"); q && *q; q = strchr(q, ',') ? strchr(q, ',') + 1 : NULL)
                if ((unsigned)atoi(q) == g_frame) g_sg_use = 1;
        }
        /* RT_RAMDUMP=n:dir: write main RAM to dir/rNNNNN.bin every n frames
         * (for comparing two builds frame by frame) */
        const char *rd = getenv("RT_RAMDUMP");
        if (rd) {
            unsigned every_n = (unsigned)atoi(rd);
            const char *dir = strchr(rd, ':');
            if (every_n && dir && g_frame % every_n == 0) {
                char p[512];
                snprintf(p, sizeof p, "%s/r%05u.bin", dir + 1, g_frame);
                FILE *f = fopen(p, "wb");
                if (f) { fwrite(g_ram, 1, RAM_SIZE, f); fclose(f); }
            }
        }
        /* RT_FLAGS: log the control state when it changes */
        if (getenv("RT_FLAGS")) {
            static char last[96];
            char now[96];
            snprintf(now, sizeof now, "act %u lock %d handler %u step %u title %u stage %u room %u", MEM_R8(0x80062DEC),
                     (int16_t)MEM_R16(0x800B3D2C), MEM_R32(0x800BB19C), MEM_R32(0x800A28CC), MEM_R32(0x8006C118),
                     MEM_R32(0x800B4BA0), MEM_R32(0x800B3E20));
            if (strcmp(now, last)) { rt_log("flags: %s", now); strcpy(last, now); }
        }
        /* RT_WATCH=addr: log the 8 bytes there every 10 frames (RT_WATCH_EVERY=n: every n) */
        const char *wa = getenv("RT_WATCH");
        if (wa && g_frame % (getenv("RT_WATCH_EVERY") ? atoi(getenv("RT_WATCH_EVERY")) : 10) == 0) {
            uint32_t a = (uint32_t)strtoul(wa, NULL, 16);
            rt_log("watch %08X: %04X %04X %04X %04X", a, MEM_R16(a), MEM_R16(a + 2), MEM_R16(a + 4), MEM_R16(a + 6));
        }
        if (g_input.mouse_present && g_frame >= (uint32_t)mfrom && g_frame < (uint32_t)mto) {
            g_input.mouse_dx += mdx;
            g_input.mouse_dy += mdy;
            g_input.look_dx += mdx;
            g_input.look_dy += mdy;
        }
    }
    {
        /* RT_SCALE_AT=frame:scale,...: change the resolution while running */
        const char *e = getenv("RT_SCALE_AT");
        while (e && *e) {
            unsigned f; int sc;
            if (sscanf(e, "%u:%d", &f, &sc) == 2 && f == g_frame) render_set_scale(sc);
            e = strchr(e, ',');
            if (e) e++;
        }
    }
    if (every && g_frame % every == 0) {
        static uint32_t bgrx[2560 * 1920];
        static uint8_t rgb[2560 * 1920 * 3];
        int w, h;
        display_grab_bgrx(bgrx, &w, &h);
        for (int i = 0; i < w * h; i++) {
            rgb[i * 3] = (uint8_t)(bgrx[i] >> 16);
            rgb[i * 3 + 1] = (uint8_t)(bgrx[i] >> 8);
            rgb[i * 3 + 2] = (uint8_t)bgrx[i];
        }
        char p[256];
        snprintf(p, sizeof p, "%s/f%05u.png", outdir, g_frame);
        write_png(p, rgb, w, h);
        snprintf(p, sizeof p, "%s/vram%05u.png", outdir, g_frame);
        dump_vram(p);
        /* RT_HIRESDUMP=x,y,w,h: that part of the high-res VRAM (PS1 coordinates) */
        if (getenv("RT_HIRESDUMP")) {
            int hx, hy, hw, hh, st;
            const uint16_t *hv = render_hires(&st);
            int sc = st / 1024;
            if (sscanf(getenv("RT_HIRESDUMP"), "%d,%d,%d,%d", &hx, &hy, &hw, &hh) == 4 && hw * sc <= 2560 && hh * sc <= 1920) {
                for (int y = 0; y < hh * sc; y++)
                    for (int x = 0; x < hw * sc; x++) {
                        uint16_t c = hv[(size_t)((hy * sc + y) % (512 * sc)) * st + (hx * sc + x) % (1024 * sc)];
                        uint8_t *o = rgb + ((size_t)y * hw * sc + x) * 3;
                        o[0] = (uint8_t)((c & 31) << 3); o[1] = (uint8_t)(((c >> 5) & 31) << 3); o[2] = (uint8_t)(((c >> 10) & 31) << 3);
                    }
                snprintf(p, sizeof p, "%s/hires%05u.png", outdir, g_frame);
                write_png(p, rgb, hw * sc, hh * sc);
            }
        }
        if (getenv("RT_MARGIN_LOG")) { extern unsigned g_margin_hist[7]; rt_log("margin zs/wz: empty %u <0.5 %u <0.9 %u <0.98 %u ~1 %u <1.5 %u more %u", g_margin_hist[0], g_margin_hist[1], g_margin_hist[2], g_margin_hist[3], g_margin_hist[4], g_margin_hist[5], g_margin_hist[6]); memset(g_margin_hist, 0, sizeof g_margin_hist); }
        /* RT_VRAMCMP: where the high-res VRAM (one sample per PS1 pixel)
         * differs from the PS1's, in 64x64 blocks (textures should match) */
        if (getenv("RT_VRAMCMP")) {
            int st;
            const uint16_t *h = render_hires(&st);
            if (h) {
                int s = st / 1024;
                render_sync();
                char line[2048] = "";
                for (int by = 0; by < 8; by++)
                    for (int bx = 0; bx < 16; bx++) {
                        int n = 0;
                        for (int y = by * 64; y < by * 64 + 64; y++)
                            for (int x = bx * 64; x < bx * 64 + 64; x++)
                                if ((h[(size_t)(y * s) * st + x * s] & 0x7FFF) != (g_vram[y][x] & 0x7FFF)) n++;
                        if (n > 40) { char b[32]; snprintf(b, sizeof b, " %d,%d:%d", bx * 64, by * 64, n); strncat(line, b, sizeof line - strlen(line) - 1); }
                    }
                rt_log("vramcmp:%s", line);
            }
        }
    }
    {
        /* Where the game spent this frame: the loop that polled most. */
        static int trace = -1;
        if (trace < 0) trace = getenv("RT_TRACE_LOOP") != NULL;
        if (trace) rt_log("loop %08X", g_poll_pc);
    }
    if (g_frame >= max_frames) {
        rt_log("stopping after %u frames", g_frame);
        if (wav) { wav_header(wav, wav_frames); fclose(wav); }
        rt_dump_state();
        exit(0);
    }
}

int plat_quit(void) { return 0; }
void plat_fatal(const char *msg) { (void)msg; }

void sg_save_done(int ok) { rt_log("savegame: save %s", ok ? "ok" : "FAILED"); }

int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s disc.bin [--frames N] [--every K] [--out DIR] [--press F:BTN:LEN]\n", argv[0]);
        return 1;
    }
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--frames") && i + 1 < argc) max_frames = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--every") && i + 1 < argc) every = (uint32_t)atoi(argv[++i]);
        else if (!strcmp(argv[i], "--out") && i + 1 < argc) outdir = argv[++i];
        else if (!strcmp(argv[i], "--scale") && i + 1 < argc) { extern int g_opt_scale; g_opt_scale = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--check-disc")) {
            /* open and check the disc image, then exit (0: it's the right disc) */
            int disc_image_open(const char *path, char *err, size_t n);
            int disc_verify(char *err, size_t n);
            char err[512];
            if (!disc_image_open(argv[1], err, sizeof err) || !disc_verify(err, sizeof err)) {
                printf("bad disc: %s\n", err);
                return 1;
            }
            printf("disc ok\n");
            return 0;
        }
        else if (!strcmp(argv[i], "--widescreen")) g_opt_widescreen = 1;
        else if (!strcmp(argv[i], "--subpixel")) g_subpixel = 1;
        else if (!strcmp(argv[i], "--wav") && i + 1 < argc) { wav = fopen(argv[++i], "wb"); if (wav) wav_header(wav, 0); }
        else if (!strcmp(argv[i], "--press") && i + 1 < argc && npresses < 64) {
            unsigned f, b, l;
            if (sscanf(argv[++i], "%u:%x:%u", &f, &b, &l) == 3)
                presses[npresses++] = (typeof(presses[0])){f, l, (uint16_t)b};
        }
    }
    rt_set_log(stderr);
    { extern int g_audio_limiter; g_audio_limiter = getenv("RT_LIMITER") != NULL; }
    if (getenv("RT_NO_DEPTH")) g_depth_buffer = 0;
    if (getenv("RT_NO_PERSP")) { extern int g_persp; g_persp = 0; }
    if (getenv("RT_BRIGHTNESS")) { extern int g_brightness; g_brightness = atoi(getenv("RT_BRIGHTNESS")); }
    {   /* RT_TEXPACK=dir: an HD texture pack; RT_TEXPACK_LIST=file: its file names only (matching stats) */
        const char *tp = getenv("RT_TEXPACK"), *tl = getenv("RT_TEXPACK_LIST");
        if (tl) texpack_open_listing(tl, tp);
        else if (tp) texpack_open(tp);
    }
    return rt_boot(argv[1]) ? 0 : 1;
}
