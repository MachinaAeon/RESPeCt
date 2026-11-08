/* Mods' movies as video files (modfs.c): decoded with FFmpeg, whose DLLs
 * (avformat, avcodec, avutil, swscale: the ones Classic Rebirth ships, or
 * any FFmpeg 6 or 7 build) RESPeCt loads from its own folder the first
 * time a mod has a video; without them a video isn't used (the log says
 * so) and the disc's movie shows.
 *
 * No FFmpeg header is built in: the functions are looked up by name, and
 * the few fields read are those FFmpeg 6 and 7 keep in the same place
 * (AVFormatContext.streams, AVStream.codecpar/duration/nb_frames,
 * AVPacket.stream_index, AVFrame.data/linesize/width/height). A
 * version before 6 is refused.
 *
 * Frames are decoded in order as the movie asks for them; going back
 * opens the file again. Each comes out as BGRX at the video's own size. */
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
#include "rt.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
typedef struct {
    unsigned (*avformat_version)(void);
    int (*avformat_open_input)(void **ctx, const char *url, const void *fmt, void **opts);
    int (*avformat_find_stream_info)(void *ctx, void **opts);
    int (*av_find_best_stream)(void *ctx, int type, int wanted, int related, const void **dec, int flags);
    void (*avformat_close_input)(void **ctx);
    int (*av_read_frame)(void *ctx, void *pkt);
    void *(*avcodec_alloc_context3)(const void *codec);
    int (*avcodec_parameters_to_context)(void *ctx, const void *par);
    int (*avcodec_open2)(void *ctx, const void *codec, void **opts);
    int (*avcodec_send_packet)(void *ctx, const void *pkt);
    int (*avcodec_receive_frame)(void *ctx, void *frame);
    void (*avcodec_free_context)(void **ctx);
    void *(*av_packet_alloc)(void);
    void (*av_packet_unref)(void *pkt);
    void (*av_packet_free)(void **pkt);
    void *(*av_frame_alloc)(void);
    void (*av_frame_free)(void **frame);
    void *(*sws_getContext)(int sw, int sh, int sf, int dw, int dh, int df, int flags, void *a, void *b, const double *p);
    int (*sws_scale)(void *ctx, const uint8_t *const src[], const int stride[], int y, int h, uint8_t *const dst[], const int dstride[]);
    void (*sws_freeContext)(void *ctx);
} Ff;
static Ff ff;
static int ff_state;                             /* 0 not tried, 1 ready, -1 missing */

/* Only from the game's folder: by bare name Windows would also search
 * System32, the current folder and PATH for an FFmpeg to feed mods' videos
 * to. The altered search order lets each DLL find its siblings there. */
static HMODULE load_one(const char *const *names) {
    char dir[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, dir, sizeof dir);
    if (!n || n >= sizeof dir) return NULL;
    char *slash = strrchr(dir, '\\');
    if (!slash) return NULL;
    slash[1] = 0;
    for (int i = 0; names[i]; i++) {
        char path[MAX_PATH];
        if (snprintf(path, sizeof path, "%s%s", dir, names[i]) >= (int)sizeof path) continue;
        if (GetFileAttributesA(path) == INVALID_FILE_ATTRIBUTES) continue;
        HMODULE h = LoadLibraryExA(path, NULL, LOAD_WITH_ALTERED_SEARCH_PATH);
        if (h) return h;
    }
    return NULL;
}

static int ff_load(void) {
    if (ff_state) return ff_state > 0;
    static const char *const fmt[] = {"avformat-62.dll", "avformat-61.dll", "avformat-60.dll", NULL};
    static const char *const cod[] = {"avcodec-62.dll", "avcodec-61.dll", "avcodec-60.dll", NULL};
    static const char *const utl[] = {"avutil-60.dll", "avutil-59.dll", "avutil-58.dll", NULL};
    static const char *const sws[] = {"swscale-9.dll", "swscale-8.dll", "swscale-7.dll", NULL};
    HMODULE u = load_one(utl), c = load_one(cod), s = load_one(sws), f = load_one(fmt);
    ff_state = -1;
    if (!f || !c || !u || !s) {
        rt_log("mods: FFmpeg: %s couldn't be loaded (error %lu)", !u ? "avutil" : !c ? "avcodec" : !s ? "swscale" : "avformat", (unsigned long)GetLastError());
        return 0;
    }
#define GET(m, n) (*(FARPROC *)&ff.n = GetProcAddress(m, #n), ff.n != NULL)
    int ok = GET(f, avformat_version) && GET(f, avformat_open_input) && GET(f, avformat_find_stream_info) &&
             GET(f, av_find_best_stream) && GET(f, avformat_close_input) && GET(f, av_read_frame) &&
             GET(c, avcodec_alloc_context3) && GET(c, avcodec_parameters_to_context) && GET(c, avcodec_open2) &&
             GET(c, avcodec_send_packet) && GET(c, avcodec_receive_frame) && GET(c, avcodec_free_context) &&
             GET(c, av_packet_alloc) && GET(c, av_packet_unref) && GET(c, av_packet_free) && GET(u, av_frame_alloc) &&
             GET(u, av_frame_free) && GET(s, sws_getContext) && GET(s, sws_scale) && GET(s, sws_freeContext);
#undef GET
    if (!ok || ff.avformat_version() >> 16 < 60) {
        rt_log("mods: FFmpeg: %s", !ok ? "a function is missing from its DLLs" : "older than version 6");
        return 0;
    }
    ff_state = 1;
    return 1;
}
#endif

typedef struct Video {
    char path[1024];
    void *fmt, *dec, *pkt, *frm, *sws;
    int stream, next, w, h, sw, sh, sf, eof;      /* next: the frame the decoder gives next */
    uint32_t *px;
    int have;                                     /* the frame in px */
    int frames;
} Video;

static void vclose(Video *v) {
#ifdef _WIN32
    if (v->sws) ff.sws_freeContext(v->sws);
    if (v->frm) ff.av_frame_free(&v->frm);
    if (v->pkt) ff.av_packet_free(&v->pkt);
    if (v->dec) ff.avcodec_free_context(&v->dec);
    if (v->fmt) ff.avformat_close_input(&v->fmt);
#endif
    v->sws = v->frm = v->pkt = v->dec = v->fmt = NULL;
}

static int vopen(Video *v, const char **why) {
#ifdef _WIN32
    const void *codec = NULL;
    v->next = 0;
    v->eof = 0;
    if (ff.avformat_open_input(&v->fmt, v->path, NULL, NULL) < 0) { *why = "FFmpeg couldn't open it"; return 0; }
    if (ff.avformat_find_stream_info(v->fmt, NULL) < 0 || (v->stream = ff.av_find_best_stream(v->fmt, 0, -1, -1, &codec, 0)) < 0 || !codec) {
        *why = "it has no video FFmpeg can decode";
        return 0;
    }
    uint8_t *st = ((uint8_t **)*(void **)((uint8_t *)v->fmt + 48))[v->stream];   /* AVFormatContext.streams[i] */
    const void *par = *(void **)(st + 16);                                         /* AVStream.codecpar */
    if (!(v->dec = ff.avcodec_alloc_context3(codec)) || ff.avcodec_parameters_to_context(v->dec, par) < 0 ||
        ff.avcodec_open2(v->dec, codec, NULL) < 0 || !(v->pkt = ff.av_packet_alloc()) || !(v->frm = ff.av_frame_alloc())) {
        *why = "FFmpeg couldn't decode its video";
        return 0;
    }
    if (!v->frames) v->frames = (int)*(int64_t *)(st + 56);                        /* AVStream.nb_frames, 0 unknown */
    return 1;
#else
    (void)v;
    *why = "videos are read on Windows only";
    return 0;
#endif
}

/* The next frame decoded into px (1), or 0 at the end. */
static int vnext(Video *v) {
#ifdef _WIN32
    for (;;) {
        int r = ff.avcodec_receive_frame(v->dec, v->frm);
        if (r == 0) {
            uint8_t *f = v->frm;
            int w = *(int *)(f + 104), h = *(int *)(f + 108), fmtv = *(int *)(f + 116);
            if (w <= 0 || h <= 0 || w > 4096 || h > 4096) return 0;
            if (!v->sws || w != v->sw || h != v->sh || fmtv != v->sf) {
                if (v->sws) ff.sws_freeContext(v->sws);
                v->sws = ff.sws_getContext(w, h, fmtv, w, h, 28 /* BGRA */, 2 /* bilinear */, NULL, NULL, NULL);
                v->sw = w, v->sh = h, v->sf = fmtv;
                free(v->px);
                v->px = malloc((size_t)w * h * 4);
                if (!v->sws || !v->px) return 0;
            }
            uint8_t *dst[4] = {(uint8_t *)v->px, NULL, NULL, NULL};
            int ds[4] = {w * 4, 0, 0, 0};
            ff.sws_scale(v->sws, (const uint8_t *const *)f, (const int *)(f + 64), 0, h, dst, ds);
            v->w = w;
            v->h = h;
            v->have = v->next++;
            return 1;
        }
        if (v->eof) return 0;
        if (ff.av_read_frame(v->fmt, v->pkt) < 0) {
            v->eof = 1;
            ff.avcodec_send_packet(v->dec, NULL);     /* (what the decoder still holds) */
            continue;
        }
        if (*(int *)((uint8_t *)v->pkt + 36) == v->stream) ff.avcodec_send_packet(v->dec, v->pkt);
        ff.av_packet_unref(v->pkt);
    }
#else
    (void)v;
    return 0;
#endif
}

/* A video file: NULL and why if it can't be read. */
Video *modvideo_open(const char *path, const char **why) {
#ifdef _WIN32
    if (!ff_load()) {
        *why = "FFmpeg's 64-bit DLLs (avformat, avcodec, avutil, swscale; version 6 or 7) aren't next to RESPeCt.exe";
        return NULL;
    }
#endif
    Video *v = calloc(1, sizeof *v);
    if (!v) { *why = "out of memory"; return NULL; }
    snprintf(v->path, sizeof v->path, "%s", path);
    v->have = -1;
    if (!vopen(v, why) || !vnext(v)) {
        if (!*why || !**why) *why = "it has no frame";
        vclose(v);
        free(v->px);
        free(v);
        return NULL;
    }
    return v;
}

/* How many frames it has (counted by decoding it once if the file doesn't say). */
int modvideo_frames(Video *v) {
    if (v->frames > 0) return v->frames;
    while (vnext(v)) {}
    v->frames = v->next;
    vclose(v);
    const char *why = "";
    if (!vopen(v, &why)) return v->frames;
    vnext(v);
    return v->frames;
}

/* Frame n (from 0) as BGRX, its size; NULL past the end. */
const uint32_t *modvideo_frame(Video *v, int n, int *w, int *h) {
    if (n < v->have) {                            /* back: from the start again */
        vclose(v);
        const char *why = "";
        if (!vopen(v, &why)) return NULL;
        v->have = -1;
    }
    while (v->have < n)
        if (!vnext(v)) break;
    if (v->have < 0 || !v->px) return NULL;
    *w = v->w;
    *h = v->h;
    return v->px;
}
