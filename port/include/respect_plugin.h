/* RESPeCt plugins: native code a mod brings (mods/<mod>/plugins/<name>.dll),
 * the way DLLs modify the PC version. A plugin is a 64-bit Windows DLL
 * that exports
 *
 *     int respect_plugin_init(const RespectApi *api);
 *
 * called once, after every mod's files are in and before the game starts;
 * it returns 0 when it's ready (anything else: it isn't used). Through the
 * api it reads and writes the game's memory and registers, calls the
 * game's functions, and hooks them: a function of the game (by its address
 * on the PS1, as in symbol_addrs.txt and port/decomp) can run code of the
 * plugin before it (which may skip it, giving its result in V0) and after
 * it. A callback can also run every frame.
 *
 * The game's RAM is 8 MB: the PS1's 2 MB, then what mods use; addresses
 * are the game's own (0x80000000 + offset), see RESPECT_RAM. Hooks run on
 * the game's thread, between its instructions: keep them short, and don't
 * keep pointers into RAM across frames' state loads. A quick save stores
 * the game's memory, not the plugin's own variables.
 *
 * Plugins run only when the player switches them on (mod_plugins = 1 in
 * the settings file): they are programs, not data.
 *
 * Build one with any C compiler for x86_64 Windows, e.g.
 *     clang --target=x86_64-w64-mingw32 -shared -O2 -o my.dll my.c -I port/include
 * (port/plugins/example.c is one). */
#ifndef RESPECT_PLUGIN_H
#define RESPECT_PLUGIN_H

#include <stdint.h>

#define RESPECT_PLUGIN_API 1

/* Registers, for get_reg / set_reg (MIPS numbering). */
enum { RESPECT_A0 = 4, RESPECT_A1 = 5, RESPECT_A2 = 6, RESPECT_A3 = 7, RESPECT_V0 = 2, RESPECT_V1 = 3,
       RESPECT_SP = 29, RESPECT_RA = 31 };

/* Before a hooked function: return 1 to skip it (set V0 to its result), 0 to let it run. */
typedef int (*RespectBefore)(void *user, uint32_t addr);
/* After it (also when an earlier plugin's before skipped it). */
typedef void (*RespectAfter)(void *user, uint32_t addr);
typedef void (*RespectFrame)(void *user, uint32_t frame);

typedef struct RespectApi {
    uint32_t version;                            /* RESPECT_PLUGIN_API */
    uint32_t size;                               /* sizeof (RespectApi) of the game (fields are only added at the end) */
    const char *mod_dir;                         /* the plugin's mod folder */
    uint8_t *ram;                                /* the game's RAM, ram_size bytes */
    uint32_t ram_size;
    uint16_t *vram;                              /* the GPU's 1024 x 512 halfwords */
    void (*log)(const char *fmt, ...);           /* a line in resurv.log */
    uint32_t (*get_reg)(int reg);
    void (*set_reg)(int reg, uint32_t value);
    /* Hook the function at addr: before and/or after (either may be NULL). 0 ok, -1 not a function of the game. */
    int (*hook)(uint32_t addr, RespectBefore before, RespectAfter after, void *user);
    /* Call a function of the game with up to four arguments; its result (V0). */
    uint32_t (*call)(uint32_t addr, uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3);
    void (*on_frame)(RespectFrame cb, void *user);
    uint32_t (*frame)(void);                     /* frames since boot */
} RespectApi;

/* A game address's bytes in the api's RAM (NULL outside it). */
static inline void *RESPECT_RAM(const RespectApi *api, uint32_t addr) {
    uint32_t p = addr & 0x1FFFFFFFu;
    return p < api->ram_size ? api->ram + p : (void *)0;
}

#ifdef _WIN32
#define RESPECT_EXPORT __declspec(dllexport)
#else
#define RESPECT_EXPORT __attribute__((visibility("default")))
#endif

#endif
