/* resurv.ini: player settings. Read at start; a commented default file is
 * written if there isn't one. Simple "key = value" lines in [sections];
 * ';' or '#' start a comment. */
#include "config.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

Config g_cfg;

static const char *default_ini =
    "; Resident Evil Survivor PC port settings.\n"
    "; Delete this file to get the defaults back.\n"
    "\n"
    "[mouse]\n"
    "; Turn speed and look up/down speed (1.0 = the default).\n"
    "sensitivity_x = 1.0\n"
    "sensitivity_y = 1.0\n"
    "; 1 = moving the mouse forward looks down.\n"
    "invert_y = 0\n"
    "; How far you can look up or down, in degrees (10-85).\n"
    "look_limit = 67\n"
    "; In menus, the inventory and the title screen the mouse moves the cursor:\n"
    "; left click = OK, right click = cancel. 0 turns this off.\n"
    "menu_mouse = 1\n"
    "; Mouse movement needed per cursor step in menus.\n"
    "menu_mouse_step = 240\n"
    "\n"
    "[keys]\n"
    "; Each action takes one or more keys separated by commas.\n"
    "; Letters and digits, Space, Enter, Tab, Backspace, LShift, RShift, LCtrl,\n"
    "; RCtrl, LAlt, RAlt, Up, Down, Left, Right, F1-F12, Insert, Delete, Home,\n"
    "; End, PageUp, PageDown, Numpad0-Numpad9, Mouse2 (right), Mouse3 (middle),\n"
    "; Mouse4, Mouse5. The left mouse button always fires; 1-9 and the mouse\n"
    "; wheel pick weapons (a number key bound here does that action instead).\n"
    "forward = W, Up\n"
    "back = S, Down\n"
    "left = A, Left\n"
    "right = D, Right\n"
    "sprint = LShift\n"
    "; Reloads the gun you're holding (when it isn't full and you have ammo).\n"
    "reload = R\n"
    "quick_search = Mouse2\n"
    "cross = Space\n"
    "triangle = E\n"
    "square = Q\n"
    "circle = F\n"
    "start = Enter\n"
    "select = Tab\n"
    "r1 = V\n"
    "l2 = Z\n"
    "r2 = C\n"
    "\n"
    "[video]\n"
    "; Start in fullscreen (F11 or Alt+Enter switches at any time).\n"
    "fullscreen = 1\n"
    "; Internal rendering resolution: 1 = original (320x240), 2-4 = sharper.\n"
    "resolution_scale = 4\n"
    "; Widescreen 16:9: wider view of the 3D scene (the HUD keeps its shape).\n"
    "; Experimental: some full-screen menus are stretched.\n"
    "widescreen = 0\n"
    "; Field of view in degrees across a 4:3 screen (widescreen adds to it).\n"
    "; 0 = the game's own, about 61. Wider shows more but makes things smaller.\n"
    "field_of_view = 0\n"
    "; Smoother, steadier 3D geometry (less PlayStation wobble).\n"
    "subpixel = 1\n"
    "; Smooth the picture when scaling it to the window (0 = sharp pixels).\n"
    "smooth_scaling = 1\n"
    "; Upscale movies to the internal resolution (smoothed, compression blocks softened).\n"
    "movie_upscaling = 1\n"
    "; Draw 3D models front to back properly at higher resolutions (the PS1 has no\n"
    "; depth buffer: models show their insides through their faces up close).\n"
    "depth_buffer = 1\n"
    "; Close the gaps distant models show at higher resolutions (they open and shut\n"
    "; as models move: flicker) by drawing their small triangles a little larger.\n"
    "gap_fill = 1\n"
    "\n"
    "[audio]\n"
    "; Volumes, 0-100 (%): everything, music and speech from the disc, sound effects.\n"
    "volume = 100\n"
    "music_volume = 100\n"
    "sfx_volume = 100\n"
    "; Turn loud moments down smoothly instead of letting them distort.\n"
    "limiter = 1\n"
    "; At most this many copies of one sound effect at a time (0 = no limit).\n"
    "same_sound_limit = 3\n"
    "\n"
    "[game]\n"
    "; Fast-forward the logos, warnings and disc check at startup.\n"
    "skip_intro = 0\n"
    "\n"
    "[cheats]\n"
    "; Health stays full.\n"
    "god_mode = 0\n"
    "; The magazine never runs down; no ammo is used.\n"
    "infinite_ammo = 0\n"
    "; The magazine is refilled from your spare ammo as you fire, so you\n"
    "; never have to reload (ammo is still used up).\n"
    "no_reload = 0\n";

static const char *action_names[ACT_COUNT] = {
    "forward", "back", "left", "right", "sprint", "quick_search", "cross", "triangle",
    "square", "circle", "start", "select", "r1", "l2", "r2", "reload"};

static void set_defaults(void) {
    memset(&g_cfg, 0, sizeof g_cfg);
    g_cfg.sens_x = g_cfg.sens_y = 1.0f;
    g_cfg.look_limit = 67;
    g_cfg.menu_mouse = 1;
    g_cfg.menu_mouse_step = 240;
    g_cfg.resolution_scale = 4;
    g_cfg.subpixel = 1;
    g_cfg.fullscreen = 1;
    g_cfg.smooth_scaling = 1;
    g_cfg.movie_upscaling = 1;
    g_cfg.depth_buffer = 1;
    g_cfg.gap_fill = 1;
    g_cfg.skip_intro = 0;
    g_cfg.volume = 100;
    g_cfg.music_volume = 100;
    g_cfg.sfx_volume = 100;
    g_cfg.limiter = 1;
    g_cfg.same_sound_limit = 3;
}

/* Split off the next piece of *s up to sep (portable strtok_r). */
static char *next_tok(char **s, char sep) {
    if (!*s) return NULL;
    char *start = *s, *e = strchr(start, sep);
    if (e) { *e = 0; *s = e + 1; }
    else *s = NULL;
    return start;
}

static char *trim(char *s) {
    while (isspace((unsigned char)*s)) s++;
    char *e = s + strlen(s);
    while (e > s && isspace((unsigned char)e[-1])) *--e = 0;
    return s;
}

static void parse_line(const char *section, char *key, char *val) {
    if (!strcmp(section, "keys")) {
        for (int a = 0; a < ACT_COUNT; a++) {
            if (strcmp(key, action_names[a])) continue;
            g_cfg.nkeys[a] = 0;
            char *rest = val;
            for (char *tok; (tok = next_tok(&rest, ',')) != NULL;) {
                tok = trim(tok);
                if (*tok && g_cfg.nkeys[a] < MAX_KEYS_PER_ACTION)
                    snprintf(g_cfg.keys[a][g_cfg.nkeys[a]++], sizeof g_cfg.keys[0][0], "%s", tok);
            }
            return;
        }
        return;
    }
    double d = atof(val);
    int i = atoi(val);
    if (!strcmp(key, "sensitivity_x")) g_cfg.sens_x = (float)d;
    else if (!strcmp(key, "sensitivity_y")) g_cfg.sens_y = (float)d;
    else if (!strcmp(key, "invert_y")) g_cfg.invert_y = i;
    else if (!strcmp(key, "look_limit")) g_cfg.look_limit = i;
    else if (!strcmp(key, "menu_mouse")) g_cfg.menu_mouse = i;
    else if (!strcmp(key, "menu_mouse_step")) g_cfg.menu_mouse_step = i;
    else if (!strcmp(key, "fullscreen")) g_cfg.fullscreen = i;
    else if (!strcmp(key, "resolution_scale")) g_cfg.resolution_scale = i;
    else if (!strcmp(key, "widescreen")) g_cfg.widescreen = i;
    else if (!strcmp(key, "subpixel")) g_cfg.subpixel = i;
    else if (!strcmp(key, "smooth_scaling")) g_cfg.smooth_scaling = i;
    else if (!strcmp(key, "movie_upscaling")) g_cfg.movie_upscaling = i;
    else if (!strcmp(key, "depth_buffer")) g_cfg.depth_buffer = i;
    else if (!strcmp(key, "gap_fill")) g_cfg.gap_fill = i;
    else if (!strcmp(key, "skip_intro")) g_cfg.skip_intro = i;
    else if (!strcmp(key, "god_mode")) g_cfg.god_mode = i;
    else if (!strcmp(key, "infinite_ammo")) g_cfg.infinite_ammo = i;
    else if (!strcmp(key, "no_reload")) g_cfg.no_reload = i;
    else if (!strcmp(key, "field_of_view")) g_cfg.field_of_view = i;
    else if (!strcmp(key, "volume")) g_cfg.volume = i;
    else if (!strcmp(key, "music_volume")) g_cfg.music_volume = i;
    else if (!strcmp(key, "sfx_volume")) g_cfg.sfx_volume = i;
    else if (!strcmp(key, "limiter")) g_cfg.limiter = i;
    else if (!strcmp(key, "same_sound_limit")) g_cfg.same_sound_limit = i;
}

static void parse(char *text) {
    char section[32] = "";
    char *rest = text;
    for (char *line; (line = next_tok(&rest, '\n')) != NULL;) {
        char *c = strpbrk(line, ";#");
        if (c) *c = 0;
        line = trim(line);
        if (!*line) continue;
        if (*line == '[') {
            char *e = strchr(line, ']');
            if (e) *e = 0;
            snprintf(section, sizeof section, "%s", trim(line + 1));
            for (char *p = section; *p; p++) *p = (char)tolower((unsigned char)*p);
            continue;
        }
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *k = trim(line), *v = trim(eq + 1);
        for (char *p = k; *p; p++) *p = (char)tolower((unsigned char)*p);
        parse_line(section, k, v);
    }
}

static void clamp(void) {
    if (g_cfg.sens_x <= 0) g_cfg.sens_x = 1;
    if (g_cfg.sens_y <= 0) g_cfg.sens_y = 1;
    if (g_cfg.look_limit < 10) g_cfg.look_limit = 10;
    if (g_cfg.look_limit > 85) g_cfg.look_limit = 85;
    if (g_cfg.menu_mouse_step < 5) g_cfg.menu_mouse_step = 5;
    if (g_cfg.resolution_scale < 1) g_cfg.resolution_scale = 1;
    if (g_cfg.resolution_scale > 4) g_cfg.resolution_scale = 4;
    if (g_cfg.field_of_view && g_cfg.field_of_view < 50) g_cfg.field_of_view = 50;
    if (g_cfg.field_of_view > 120) g_cfg.field_of_view = 120;
    if (g_cfg.volume < 0) g_cfg.volume = 0;
    if (g_cfg.volume > 100) g_cfg.volume = 100;
    int *vols[2] = {&g_cfg.music_volume, &g_cfg.sfx_volume};
    for (int k = 0; k < 2; k++) { if (*vols[k] < 0) *vols[k] = 0; if (*vols[k] > 100) *vols[k] = 100; }
}

void config_load(const char *path) {
    set_defaults();
    /* defaults for keys come from the default file itself */
    char *def = strdup(default_ini);
    parse(def);
    free(def);
    FILE *f = fopen(path, "rb");
    if (!f) {
        f = fopen(path, "wb");
        if (f) { fputs(default_ini, f); fclose(f); }
        clamp();
        return;
    }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    size_t got = fread(buf, 1, (size_t)n, f);
    buf[got] = 0;
    fclose(f);
    int had_reload = strstr(buf, "reload") != NULL && strstr(buf, "\nreload") != NULL;
    parse(buf);
    free(buf);
    if (!had_reload) {
        /* Settings from before the reload key: R moves from quick search to
         * reload (if it was there), reload gets R. */
        int a = ACT_QUICK_SEARCH, k2 = 0;
        for (int k = 0; k < g_cfg.nkeys[a]; k++)
            if (strcmp(g_cfg.keys[a][k], "R") && strcmp(g_cfg.keys[a][k], "r"))
                memcpy(g_cfg.keys[a][k2++], g_cfg.keys[a][k], sizeof g_cfg.keys[a][k]);
        g_cfg.nkeys[a] = k2;
        g_cfg.nkeys[ACT_RELOAD] = 1;
        snprintf(g_cfg.keys[ACT_RELOAD][0], sizeof g_cfg.keys[0][0], "R");
    }
    /* R1 used to default to 3, which now picks weapon 3. */
    if (g_cfg.nkeys[ACT_R1] == 1 && !strcmp(g_cfg.keys[ACT_R1][0], "3"))
        snprintf(g_cfg.keys[ACT_R1][0], sizeof g_cfg.keys[0][0], "V");
    clamp();
}

/* PS1 pad bits for each action. */
const uint16_t g_action_bits[ACT_COUNT] = {
    0x0010, 0x0040, 0x0080, 0x0020, 0x4000, 0x0400, 0x4000, 0x1000,
    0x8000, 0x2000, 0x0008, 0x0001, 0x0800, 0x0100, 0x0200, 0};

/* ------------------------------------------------------------ saving */

static int format_value(const char *section, const char *key, char *out, size_t n) {
    if (!strcmp(section, "keys")) {
        for (int a = 0; a < ACT_COUNT; a++) {
            if (strcmp(key, action_names[a])) continue;
            out[0] = 0;
            for (int k = 0; k < g_cfg.nkeys[a]; k++) {
                if (k) strncat(out, ", ", n - strlen(out) - 1);
                strncat(out, g_cfg.keys[a][k], n - strlen(out) - 1);
            }
            return 1;
        }
        return 0;
    }
    if (!strcmp(key, "sensitivity_x")) snprintf(out, n, "%.2f", g_cfg.sens_x);
    else if (!strcmp(key, "sensitivity_y")) snprintf(out, n, "%.2f", g_cfg.sens_y);
    else if (!strcmp(key, "invert_y")) snprintf(out, n, "%d", g_cfg.invert_y);
    else if (!strcmp(key, "look_limit")) snprintf(out, n, "%d", g_cfg.look_limit);
    else if (!strcmp(key, "menu_mouse")) snprintf(out, n, "%d", g_cfg.menu_mouse);
    else if (!strcmp(key, "menu_mouse_step")) snprintf(out, n, "%d", g_cfg.menu_mouse_step);
    else if (!strcmp(key, "fullscreen")) snprintf(out, n, "%d", g_cfg.fullscreen);
    else if (!strcmp(key, "resolution_scale")) snprintf(out, n, "%d", g_cfg.resolution_scale);
    else if (!strcmp(key, "widescreen")) snprintf(out, n, "%d", g_cfg.widescreen);
    else if (!strcmp(key, "subpixel")) snprintf(out, n, "%d", g_cfg.subpixel);
    else if (!strcmp(key, "smooth_scaling")) snprintf(out, n, "%d", g_cfg.smooth_scaling);
    else if (!strcmp(key, "movie_upscaling")) snprintf(out, n, "%d", g_cfg.movie_upscaling);
    else if (!strcmp(key, "depth_buffer")) snprintf(out, n, "%d", g_cfg.depth_buffer);
    else if (!strcmp(key, "gap_fill")) snprintf(out, n, "%d", g_cfg.gap_fill);
    else if (!strcmp(key, "skip_intro")) snprintf(out, n, "%d", g_cfg.skip_intro);
    else if (!strcmp(key, "god_mode")) snprintf(out, n, "%d", g_cfg.god_mode);
    else if (!strcmp(key, "infinite_ammo")) snprintf(out, n, "%d", g_cfg.infinite_ammo);
    else if (!strcmp(key, "no_reload")) snprintf(out, n, "%d", g_cfg.no_reload);
    else if (!strcmp(key, "field_of_view")) snprintf(out, n, "%d", g_cfg.field_of_view);
    else if (!strcmp(key, "volume")) snprintf(out, n, "%d", g_cfg.volume);
    else if (!strcmp(key, "music_volume")) snprintf(out, n, "%d", g_cfg.music_volume);
    else if (!strcmp(key, "sfx_volume")) snprintf(out, n, "%d", g_cfg.sfx_volume);
    else if (!strcmp(key, "limiter")) snprintf(out, n, "%d", g_cfg.limiter);
    else if (!strcmp(key, "same_sound_limit")) snprintf(out, n, "%d", g_cfg.same_sound_limit);
    else return 0;
    return 1;
}

/* Write the settings: the default file's layout and comments with the
 * current values. extra is appended as-is (e.g. the disc path section). */
int config_save(const char *path, const char *extra) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    char section[32] = "";
    const char *p = default_ini;
    while (*p) {
        const char *e = strchr(p, '\n');
        size_t len = e ? (size_t)(e - p) : strlen(p);
        char line[256];
        if (len >= sizeof line) len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = 0;
        p += len + (e ? 1 : 0);
        char *eq = strchr(line, '=');
        if (line[0] == '[') {
            char *rb = strchr(line, ']');
            size_t sl = rb ? (size_t)(rb - line - 1) : 0;
            if (sl >= sizeof section) sl = sizeof section - 1;
            memcpy(section, line + 1, sl);
            section[sl] = 0;
        } else if (line[0] != ';' && eq) {
            char key[64];
            size_t kl = (size_t)(eq - line);
            while (kl && line[kl - 1] == ' ') kl--;
            if (kl >= sizeof key) kl = sizeof key - 1;
            memcpy(key, line, kl);
            key[kl] = 0;
            char val[128];
            if (format_value(section, key, val, sizeof val)) {
                fprintf(f, "%s = %s\n", key, val);
                continue;
            }
        }
        fprintf(f, "%s\n", line);
    }
    if (extra && *extra) fprintf(f, "\n%s", extra);
    fclose(f);
    return 1;
}
