/* An example plugin (respect_plugin.h): it says in resurv.log which room
 * loads and what each message box says, and gives back one point of health
 * every so many frames (regen.txt in the mod's folder: the frames, 60 if
 * there's none).
 *
 *     clang --target=x86_64-w64-mingw32 -shared -O2 -o example.dll example.c -I ../include
 *
 * then mods/<mod>/plugins/example.dll, and mod_plugins = 1 in resurv.ini. */
#include "respect_plugin.h"

#include <stdio.h>

#define ROOM_EVENTS  0x8008DAD0u                 /* room_load: the room's event file (script_runner.c) */
#define MSG_SHOW     0x8007D6E0u                 /* (text, kind): a message box (message.c) */
#define STAGE        0x800B4BA0u                 /* s32 */
#define ROOM         0x800B3E20u                 /* s32 */
#define PLAYER_HP    0x800A8584u                 /* s16, up to 200 (cheats.c) */

static const RespectApi *api;
static unsigned every = 60;

static void room_loaded(void *user, uint32_t addr) {
    (void)user, (void)addr;
    api->log("room %d/%d", *(int32_t *)RESPECT_RAM(api, STAGE), *(int32_t *)RESPECT_RAM(api, ROOM));
}

/* The game's glyph codes as letters (text/CHARS.txt has them all). */
static char letter(uint16_t c) {
    if (c == 0) return ' ';
    if (c >= 12 && c <= 21) return (char)('0' + c - 12);
    if (c >= 29 && c <= 54) return (char)('A' + c - 29);
    if (c >= 61 && c <= 86) return (char)('a' + c - 61);
    return c == 121 ? '.' : c == 24 ? ',' : c == 58 ? '\'' : c == 27 ? '?' : c == 26 ? '!' : c == 0xFFFE ? '/' : '_';
}

static int message(void *user, uint32_t addr) {
    (void)user, (void)addr;
    const uint16_t *t = RESPECT_RAM(api, api->get_reg(RESPECT_A0));
    char s[96];
    int n = 0;
    while (t && n < 95 && t[n] != 0xFFFF && t[n] != 0xFF00) { s[n] = letter(t[n]); n++; }
    s[n] = 0;
    api->log("message: %s", s);
    return 0;                                    /* (1 would skip the game's own, with V0 set) */
}

static void frame(void *user, uint32_t f) {
    (void)user;
    int16_t *hp = RESPECT_RAM(api, PLAYER_HP);
    if (f % every == 0 && *hp > 0 && *hp < 200) (*hp)++;
}

RESPECT_EXPORT int respect_plugin_init(const RespectApi *a) {
    if (a->version < 1 || a->size < sizeof *a) return 1;
    api = a;
    char path[1200];
    snprintf(path, sizeof path, "%s/regen.txt", a->mod_dir);
    FILE *f = fopen(path, "r");
    if (f) {
        unsigned v;
        if (fscanf(f, "%u", &v) == 1 && v) every = v;
        fclose(f);
    }
    if (a->hook(ROOM_EVENTS, NULL, room_loaded, NULL) || a->hook(MSG_SHOW, message, NULL, NULL)) return 2;
    a->on_frame(frame, NULL);
    a->log("example: a point of health every %u frames", every);
    return 0;
}
