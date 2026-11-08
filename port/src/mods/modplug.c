/* Mods' plugins (respect_plugin.h): mods/<mod>/plugins/<name>.dll, native code
 * that hooks the game's functions and reads and writes its memory, as DLLs
 * do with the PC version. Loaded after every mod's files are in (rt_boot),
 * only when the player switched them on (mod_plugins = 1, RT_PLUGINS=1 for
 * the headless runner).
 *
 * Every call of a game function goes through interp_call (interp.c); a
 * hooked one runs its plugins' "before" callbacks, the function unless one
 * of them skipped it, then the "after" callbacks. A bit per address (a
 * hash of it) keeps the check for the others to one load. A hook doesn't
 * run again inside itself (a plugin calling the function it hooks gets
 * the game's own).
 *
 * The game's functions rewritten in C (port/decomp) also call each other
 * directly, past interp_call. Those are built with 16 bytes of NOPs at
 * their start and without inlining (the Makefile's HOOK_CFLAGS): a hooked
 * one gets a jump there to native_dispatch, which runs the hooks around
 * the function's own code after the NOPs, however it was called, the way
 * a PC DLL patches gun_bio.exe. */
#ifdef _WIN32                                   /* (before rt.h: its R and the like) */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif
#include "rt.h"
#include "config.h"
#include "../include/respect_plugin.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

uint32_t g_plug_mask[1024];                     /* interp.c: a bit per (addr >> 2) & 0x7FFF */

typedef struct { uint32_t addr; RespectBefore before; RespectAfter after; void *user; int busy; } Hook;
static Hook *hooks;
static int nhooks;
typedef struct { RespectFrame cb; void *user; } FrameCb;
static FrameCb *frames;
static int nframes;
typedef struct { char mod[256], path[1024]; } Plugin;
static Plugin *plugs;
static int nplugs, nloaded;

int modplug_enabled(void) {
    const char *s = getenv("RT_PLUGINS");
    return s ? !strcmp(s, "1") : g_cfg.mod_plugins != 0;
}

/* modfs.c: a mod's plugins/x.dll (1: it is one). */
int modplug_add(const char *mod, const char *mod_dir, const char *up, const char *path) {
    size_t l = strlen(up);
    if (strncmp(up, "PLUGINS/", 8) || l < 12 || strcmp(up + l - 4, ".DLL")) return 0;
    Plugin *p = realloc(plugs, (size_t)(nplugs + 1) * sizeof *plugs);
    if (!p) return 1;
    plugs = p;
    snprintf(plugs[nplugs].mod, sizeof plugs[nplugs].mod, "%s", mod);
    snprintf(plugs[nplugs].path, sizeof plugs[nplugs].path, "%s", path);
    (void)mod_dir;
    nplugs++;
    return 1;
}

static void api_log(const char *fmt, ...) {
    char b[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    rt_log("plugin: %s", b);
}
static uint32_t api_get_reg(int r) { return r == 32 ? g_cpu.hi : r == 33 ? g_cpu.lo : r > 0 && r < 32 ? R[r] : 0; }
static void api_set_reg(int r, uint32_t v) {
    if (r > 0 && r < 32) R[r] = v;
    else if (r == 32) g_cpu.hi = v;
    else if (r == 33) g_cpu.lo = v;
}
int rt_is_function(uint32_t addr);              /* rt.c */
int rt_natives_at(uint32_t addr, RtFn *out, int max);
int modplug_enter(uint32_t addr), modplug_before(int first);
void modplug_after(int first);

#define NATIVE_PAD 16                            /* (HOOK_CFLAGS: -fpatchable-function-entry=16) */
typedef struct { uint32_t addr; RtFn body; } NativeHook;
static void native_dispatch(NativeHook *nh) {
    int h = modplug_enter(nh->addr);
    if (h < 0) { nh->body(); return; }           /* (inside its own hook: the game's own) */
    if (!modplug_before(h)) nh->body();
    modplug_after(h);
}
/* Make the native fn jump to native_dispatch for addr; 1 when done. */
static int native_patch(uint32_t addr, RtFn fn) {
    static NativeHook *patched[1024];
    static int npatched;
    for (int i = 0; i < npatched; i++) if (patched[i]->addr == addr && (uint8_t *)(void *)patched[i]->body == (uint8_t *)(void *)fn + NATIVE_PAD) return 1;
    uint8_t *entry = (uint8_t *)(void *)fn;
    for (int i = 0; i < NATIVE_PAD;) {           /* only over the NOPs the build put there */
        if (entry[i] == 0x90) i++;
        else if (entry[i] == 0x66 || entry[i] == 0x0F || entry[i] == 0x2E) {   /* multi-byte NOP: 66.. 0F 1F .. */
            int k = i;
            while (k < NATIVE_PAD && entry[k] == 0x66) k++;
            if (k + 1 >= NATIVE_PAD || entry[k] == 0x2E) { while (k < NATIVE_PAD && (entry[k] == 0x2E || entry[k] == 0x66)) k++; }
            if (k + 1 >= NATIVE_PAD || entry[k] != 0x0F || entry[k + 1] != 0x1F) return 0;
            uint8_t modrm = entry[k + 2];
            int len = 3 + ((modrm & 0xC0) == 0x40 ? 1 : (modrm & 0xC0) == 0x80 ? 4 : 0) + ((modrm & 7) == 4 && (modrm & 0xC0) != 0xC0 ? 1 : 0);
            i = k + len;
        } else return 0;
    }
    if (npatched == 1024) return 0;
    NativeHook *nh = malloc(sizeof *nh);
    uint8_t *tramp = NULL;
#ifdef _WIN32
    tramp = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#endif
    if (!nh || !tramp) { free(nh); return 0; }
    nh->addr = addr;
    nh->body = (RtFn)(void *)(entry + NATIVE_PAD);
    /* the trampoline: the hook's record as the first argument, then native_dispatch */
    uint8_t *t = tramp;
#ifdef _WIN32
    *t++ = 0x48; *t++ = 0xB9;                    /* movabs rcx, nh */
#else
    *t++ = 0x48; *t++ = 0xBF;                    /* movabs rdi, nh */
#endif
    uint64_t v = (uint64_t)(uintptr_t)nh; memcpy(t, &v, 8); t += 8;
    *t++ = 0x48; *t++ = 0xB8;                    /* movabs rax, native_dispatch */
    v = (uint64_t)(uintptr_t)(void *)native_dispatch; memcpy(t, &v, 8); t += 8;
    *t++ = 0xFF; *t++ = 0xE0;                    /* jmp rax */
    uint8_t jump[12] = {0x48, 0xB8};             /* at the entry: movabs rax, tramp; jmp rax */
    v = (uint64_t)(uintptr_t)tramp; memcpy(jump + 2, &v, 8);
    jump[10] = 0xFF; jump[11] = 0xE0;
#ifdef _WIN32
    DWORD old;
    if (!VirtualProtect(entry, NATIVE_PAD, PAGE_EXECUTE_READWRITE, &old)) { free(nh); return 0; }
    memcpy(entry, jump, sizeof jump);
    VirtualProtect(entry, NATIVE_PAD, old, &old);
    FlushInstructionCache(GetCurrentProcess(), entry, NATIVE_PAD);
#endif
    patched[npatched++] = nh;
    return 1;
}
static int api_hook(uint32_t addr, RespectBefore before, RespectAfter after, void *user) {
    addr = (addr & 0x1FFFFFFFu) | 0x80000000u;
    if ((!before && !after) || !rt_is_function(addr)) return -1;
    Hook *h = realloc(hooks, (size_t)(nhooks + 1) * sizeof *hooks);
    if (!h) return -1;
    hooks = h;
    hooks[nhooks++] = (Hook){addr, before, after, user, 0};
    RtFn natives[32];
    int nn = rt_natives_at(addr, natives, 32), done = 0;
    for (int i = 0; i < nn; i++) done += native_patch(addr, natives[i]);   /* (every call of them reaches the hooks) */
    if (nn && done < nn) rt_log("plugin: %08X: %d of its native versions built without the hook pad: hooked only where the interpreter calls them", addr, nn - done);
    g_plug_mask[(addr >> 7) & 1023] |= 1u << ((addr >> 2) & 31);   /* (the interpreted ones at addr) */
    return 0;
}
extern int g_native_depth;
static uint32_t api_call(uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3) {
    uint32_t ra = R[31];
    R[4] = a0; R[5] = a1; R[6] = a2; R[7] = a3;
    g_native_depth++;                           /* (no quick save in the middle of it) */
    rt_call(addr);
    g_native_depth--;
    R[31] = ra;
    return R[2];
}
static void api_on_frame(RespectFrame cb, void *user) {
    FrameCb *f = cb ? realloc(frames, (size_t)(nframes + 1) * sizeof *frames) : NULL;
    if (!f) return;
    frames = f;
    frames[nframes++] = (FrameCb){cb, user};
}
static uint32_t api_frame(void) { return g_frame; }

/* interp.c: the hooks of the function about to run: their first index, -1 none (or running already). */
int modplug_enter(uint32_t addr) {
    int first = -1;
    for (int i = 0; i < nhooks; i++)
        if (hooks[i].addr == addr) {
            if (hooks[i].busy) return -1;
            if (first < 0) first = i;
        }
    if (first < 0) return -1;
    for (int i = first; i < nhooks; i++) if (hooks[i].addr == addr) hooks[i].busy = 1;
    return first;
}
int modplug_before(int first) {
    int skip = 0;
    uint32_t addr = hooks[first].addr;
    for (int i = first; i < nhooks; i++)
        if (hooks[i].addr == addr && hooks[i].before && !skip) skip = hooks[i].before(hooks[i].user, addr) != 0;
    return skip;
}
void modplug_after(int first) {
    uint32_t addr = hooks[first].addr;
    for (int i = first; i < nhooks; i++)
        if (hooks[i].addr == addr) {
            if (hooks[i].after) hooks[i].after(hooks[i].user, addr);
            hooks[i].busy = 0;
        }
}

/* hw.c: a frame. */
void modplug_frame(void) {
    for (int i = 0; i < nframes; i++) frames[i].cb(frames[i].user, g_frame);
}

int modplug_loaded(void) { return nloaded; }

/* rt_boot, after the mods' files: each plugin loaded and started. */
void modplug_load(const char *mods_dir) {
    if (!nplugs) return;
    if (!modplug_enabled()) {
        rt_log("mods: %d plugins (native code) not used: switch them on with mod_plugins = 1 in resurv.ini", nplugs);
        return;
    }
    static RespectApi apis[64];                 /* (each plugin keeps its own) */
    static char dirs[64][1100];
    for (int i = 0; i < nplugs && i < 64; i++) {
        typedef int (*Init)(const RespectApi *);
        Init init = NULL;
#ifdef _WIN32
        HMODULE h = LoadLibraryA(plugs[i].path);
        if (h) init = (Init)(void (*)(void))GetProcAddress(h, "respect_plugin_init");
#else
        void *h = dlopen(plugs[i].path, RTLD_NOW);
        if (h) init = (Init)dlsym(h, "respect_plugin_init");
#endif
        if (!h || !init) {
            rt_log("mods: %s/%s: %s: not used", plugs[i].mod, strrchr(plugs[i].path, '/') ? strrchr(plugs[i].path, '/') + 1 : plugs[i].path,
                   !h ? "couldn't load it (a 64-bit Windows DLL?)" : "it has no respect_plugin_init");
            continue;
        }
        char *dir = dirs[i];
        RespectApi *api = &apis[i];
        snprintf(dir, 1100, "%s/%s", mods_dir, plugs[i].mod);
        *api = (RespectApi){RESPECT_PLUGIN_API, sizeof *api, dir, g_ram, RAM_SIZE, &g_vram[0][0], api_log, api_get_reg, api_set_reg,
                           api_hook, api_call, api_on_frame, api_frame};
        int before = nhooks, beforef = nframes;
        int r = init(api);
        if (r) {
            rt_log("mods: %s: its plugin didn't start (%d): not used", plugs[i].mod, r);
            for (int k = before; k < nhooks; k++) hooks[k].addr = 0;   /* (its hooks dropped) */
            nframes = beforef;
            continue;
        }
        nloaded++;
        rt_log("mods: %s: plugin %s started (%d hooks, %d frame callbacks)", plugs[i].mod, strrchr(plugs[i].path, '/') ? strrchr(plugs[i].path, '/') + 1 : plugs[i].path,
               nhooks - before, nframes - beforef);
    }
}
