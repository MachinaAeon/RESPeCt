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
    "; Internal rendering resolution: 1 = original (320x240), 2-4 = sharper;\n"
    "; 5-8 with graphics_card = 1.\n"
    "resolution_scale = 4\n"
    "; Draw the sharper picture with the graphics card (Direct3D 11) instead of\n"
    "; the processor: much faster, and resolutions up to 8x (2560x1920).\n"
    "graphics_card = 0\n"
    "; Widescreen 16:9: wider view of the 3D scene (the HUD keeps its shape).\n"
    "; Experimental: some full-screen menus are stretched.\n"
    "widescreen = 0\n"
    "; Field of view in degrees across a 4:3 screen (widescreen adds to it).\n"
    "; 0 = the game's own, about 61. Wider shows more but makes things smaller.\n"
    "field_of_view = 0\n"
    "; Brightness in % (50-200; 100 = as on the PlayStation). Higher lifts dark areas.\n"
    "brightness = 100\n"
    "; Perspective-correct textures: no bending or swimming across big polygons. At\n"
    "; 2x and higher, close floors and walls also stay whole as you approach instead\n"
    "; of being split into smaller pieces (which made them jump).\n"
    "perspective = 1\n"
    "; Smooth the game's own textures on 3D surfaces at 2x and higher (as the PC\n"
    "; version draws them) instead of the PlayStation's square texels.\n"
    "smooth_textures = 1\n"
    "; Shade and blend the picture in 24-bit colour at 2x and higher (as the PC\n"
    "; version does) instead of the PlayStation's 15 bits: no banding in the dark.\n"
    "true_color = 1\n"
    "; Lower the resolution by itself when this PC can't keep the game at full speed.\n"
    "auto_resolution = 1\n"
    "; Smoother, steadier 3D geometry (less PlayStation wobble).\n"
    "subpixel = 1\n"
    "; Smooth the picture when scaling it to the window (0 = sharp pixels).\n"
    "smooth_scaling = 1\n"
    "; Upscale movies to the internal resolution (smoothed, compression blocks softened).\n"
    "movie_upscaling = 1\n"
    "; Draw 3D models front to back properly at higher resolutions (the PS1 has no\n"
    "; depth buffer: models show their insides through their faces up close).\n"
    "depth_buffer = 1\n"
    "; Close the hair-thin gaps between models' parts and where walls meet, which\n"
    "; open and shut as things move at higher resolutions (outlines stay as they are).\n"
    "gap_fill = 1\n"
    "; HD texture pack (Beetle PSX HW format: <hash>-<hash>.png images): use it if\n"
    "; there is one. Put the pack's folder, or the .rar/.zip/.7z it came in, next to\n"
    "; the exe (an archive is unpacked into a \"textures\" folder the first time).\n"
    "hd_textures = 1\n"
    "; Where the pack is, if it's somewhere else: a folder (such as RetroArch's copy)\n"
    "; or an archive. Empty = look next to the exe.\n"
    "texture_pack =\n"
    "; Memory kept for HD textures, in MB.\n"
    "hd_texture_memory = 1536\n"
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
    "[controller]\n"
    "; Xbox-style controllers (and others through Steam Input). Left stick or\n"
    "; d-pad: move and strafe (menus: move the cursor); right stick: look.\n"
    "controller = 1\n"
    "; Turning and looking speed at full tilt, in degrees per second.\n"
    "look_speed = 400\n"
    "look_speed_y = 280\n"
    "; 1 = pushing the right stick up looks down.\n"
    "pad_invert_y = 0\n"
    "; How much of the sticks' travel is ignored around the centre, in %.\n"
    "deadzone = 20\n"
    "; L3 + R3 opens the settings (LB/RB: pages, d-pad: choose and change,\n"
    "; A: change, B: close). Hold A to fast-forward cutscenes and doors.\n"
    "; What each button does: cross (OK, run), square (cancel), circle, triangle,\n"
    "; start, select, r1, l2, r2, quick_search, sprint, reload, fire,\n"
    "; next_weapon, prev_weapon, or none. sprint is a toggle: it stays on\n"
    "; until you stop moving.\n"
    "button_a = cross\n"
    "button_b = square\n"
    "button_x = reload\n"
    "button_y = triangle\n"
    "button_lb = prev_weapon\n"
    "button_rb = next_weapon\n"
    "button_lt = quick_search\n"
    "button_rt = fire\n"
    "button_back = select\n"
    "button_start = start\n"
    "button_l3 = sprint\n"
    "button_r3 = circle\n"
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
    g_cfg.brightness = 100;
    g_cfg.perspective = 1;
    g_cfg.smooth_textures = 1;
    g_cfg.true_color = 1;
    g_cfg.auto_resolution = 1;
    g_cfg.look_limit = 67;
    g_cfg.menu_mouse = 1;
    g_cfg.menu_mouse_step = 240;
    g_cfg.resolution_scale = 4;
    g_cfg.graphics_card = 0;
    g_cfg.subpixel = 1;
    g_cfg.fullscreen = 1;
    g_cfg.smooth_scaling = 1;
    g_cfg.movie_upscaling = 1;
    g_cfg.depth_buffer = 1;
    g_cfg.gap_fill = 1;
    g_cfg.hd_textures = 1;
    g_cfg.hd_texture_memory = 1536;
    g_cfg.skip_intro = 0;
    g_cfg.volume = 100;
    g_cfg.music_volume = 100;
    g_cfg.sfx_volume = 100;
    g_cfg.limiter = 1;
    g_cfg.same_sound_limit = 3;
    g_cfg.pad_enabled = 1;
    g_cfg.pad_look_speed = 400;
    g_cfg.pad_look_speed_y = 280;
    g_cfg.pad_deadzone = 20;
    static const char *def[PADB_COUNT] = {"cross", "square", "reload", "triangle", "prev_weapon", "next_weapon",
                                          "quick_search", "fire", "select", "start", "sprint", "circle"};
    for (int i = 0; i < PADB_COUNT; i++) snprintf(g_cfg.pad_bind[i], sizeof g_cfg.pad_bind[i], "%s", def[i]);
}

const char *g_pad_button_names[PADB_COUNT] = {"button_a", "button_b", "button_x", "button_y", "button_lb", "button_rb",
                                              "button_lt", "button_rt", "button_back", "button_start", "button_l3", "button_r3"};

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
    if (!strcmp(key, "texture_pack")) {
        char *v = trim(val);
        size_t n = strlen(v);
        if (n >= 2 && v[0] == '"' && v[n - 1] == '"') { v[n - 1] = 0; v++; }
        snprintf(g_cfg.texture_pack, sizeof g_cfg.texture_pack, "%s", v);
        return;
    }
    double d = atof(val);
    int i = atoi(val);
    for (int b = 0; b < PADB_COUNT; b++)
        if (!strcmp(key, g_pad_button_names[b])) { snprintf(g_cfg.pad_bind[b], sizeof g_cfg.pad_bind[b], "%s", trim(val)); return; }
    if (!strcmp(key, "controller")) g_cfg.pad_enabled = i;
    else if (!strcmp(key, "look_speed")) g_cfg.pad_look_speed = i == 200 ? 400 : i;      /* 200/140: the first test build's */
    else if (!strcmp(key, "look_speed_y")) g_cfg.pad_look_speed_y = i == 140 ? 280 : i;
    else if (!strcmp(key, "pad_invert_y")) g_cfg.pad_invert_y = i;
    else if (!strcmp(key, "deadzone")) g_cfg.pad_deadzone = i < 0 ? 0 : i > 90 ? 90 : i;
    else if (!strcmp(key, "sensitivity_x")) g_cfg.sens_x = (float)d;
    else if (!strcmp(key, "sensitivity_y")) g_cfg.sens_y = (float)d;
    else if (!strcmp(key, "invert_y")) g_cfg.invert_y = i;
    else if (!strcmp(key, "look_limit")) g_cfg.look_limit = i;
    else if (!strcmp(key, "menu_mouse")) g_cfg.menu_mouse = i;
    else if (!strcmp(key, "menu_mouse_step")) g_cfg.menu_mouse_step = i;
    else if (!strcmp(key, "fullscreen")) g_cfg.fullscreen = i;
    else if (!strcmp(key, "resolution_scale")) g_cfg.resolution_scale = i;
    else if (!strcmp(key, "graphics_card")) g_cfg.graphics_card = i;
    else if (!strcmp(key, "widescreen")) g_cfg.widescreen = i;
    else if (!strcmp(key, "subpixel")) g_cfg.subpixel = i;
    else if (!strcmp(key, "smooth_scaling")) g_cfg.smooth_scaling = i;
    else if (!strcmp(key, "movie_upscaling")) g_cfg.movie_upscaling = i;
    else if (!strcmp(key, "depth_buffer")) g_cfg.depth_buffer = i;
    else if (!strcmp(key, "gap_fill")) g_cfg.gap_fill = i;
    else if (!strcmp(key, "hd_textures")) g_cfg.hd_textures = i;
    else if (!strcmp(key, "hd_texture_memory")) g_cfg.hd_texture_memory = i < 256 ? 256 : i;
    else if (!strcmp(key, "skip_intro")) g_cfg.skip_intro = i;
    else if (!strcmp(key, "god_mode")) g_cfg.god_mode = i;
    else if (!strcmp(key, "infinite_ammo")) g_cfg.infinite_ammo = i;
    else if (!strcmp(key, "no_reload")) g_cfg.no_reload = i;
    else if (!strcmp(key, "field_of_view")) g_cfg.field_of_view = i;
    else if (!strcmp(key, "perspective")) g_cfg.perspective = i;
    else if (!strcmp(key, "smooth_textures")) g_cfg.smooth_textures = i;
    else if (!strcmp(key, "true_color")) g_cfg.true_color = i;
    else if (!strcmp(key, "auto_resolution")) g_cfg.auto_resolution = i;
    else if (!strcmp(key, "brightness")) g_cfg.brightness = i < 50 ? 50 : i > 200 ? 200 : i;
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
    if (g_cfg.resolution_scale > 8) g_cfg.resolution_scale = 8;
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
    for (int b = 0; b < PADB_COUNT; b++)
        if (!strcmp(key, g_pad_button_names[b])) { snprintf(out, n, "%s", g_cfg.pad_bind[b]); return 1; }
    if (!strcmp(key, "texture_pack")) snprintf(out, n, "%s", g_cfg.texture_pack);
    else if (!strcmp(key, "controller")) snprintf(out, n, "%d", g_cfg.pad_enabled);
    else if (!strcmp(key, "look_speed")) snprintf(out, n, "%d", g_cfg.pad_look_speed);
    else if (!strcmp(key, "look_speed_y")) snprintf(out, n, "%d", g_cfg.pad_look_speed_y);
    else if (!strcmp(key, "pad_invert_y")) snprintf(out, n, "%d", g_cfg.pad_invert_y);
    else if (!strcmp(key, "deadzone")) snprintf(out, n, "%d", g_cfg.pad_deadzone);
    else if (!strcmp(key, "sensitivity_x")) snprintf(out, n, "%.2f", g_cfg.sens_x);
    else if (!strcmp(key, "sensitivity_y")) snprintf(out, n, "%.2f", g_cfg.sens_y);
    else if (!strcmp(key, "invert_y")) snprintf(out, n, "%d", g_cfg.invert_y);
    else if (!strcmp(key, "look_limit")) snprintf(out, n, "%d", g_cfg.look_limit);
    else if (!strcmp(key, "menu_mouse")) snprintf(out, n, "%d", g_cfg.menu_mouse);
    else if (!strcmp(key, "menu_mouse_step")) snprintf(out, n, "%d", g_cfg.menu_mouse_step);
    else if (!strcmp(key, "fullscreen")) snprintf(out, n, "%d", g_cfg.fullscreen);
    else if (!strcmp(key, "resolution_scale")) snprintf(out, n, "%d", g_cfg.resolution_scale);
    else if (!strcmp(key, "graphics_card")) snprintf(out, n, "%d", g_cfg.graphics_card);
    else if (!strcmp(key, "widescreen")) snprintf(out, n, "%d", g_cfg.widescreen);
    else if (!strcmp(key, "subpixel")) snprintf(out, n, "%d", g_cfg.subpixel);
    else if (!strcmp(key, "smooth_scaling")) snprintf(out, n, "%d", g_cfg.smooth_scaling);
    else if (!strcmp(key, "movie_upscaling")) snprintf(out, n, "%d", g_cfg.movie_upscaling);
    else if (!strcmp(key, "depth_buffer")) snprintf(out, n, "%d", g_cfg.depth_buffer);
    else if (!strcmp(key, "gap_fill")) snprintf(out, n, "%d", g_cfg.gap_fill);
    else if (!strcmp(key, "hd_textures")) snprintf(out, n, "%d", g_cfg.hd_textures);
    else if (!strcmp(key, "hd_texture_memory")) snprintf(out, n, "%d", g_cfg.hd_texture_memory);
    else if (!strcmp(key, "skip_intro")) snprintf(out, n, "%d", g_cfg.skip_intro);
    else if (!strcmp(key, "god_mode")) snprintf(out, n, "%d", g_cfg.god_mode);
    else if (!strcmp(key, "infinite_ammo")) snprintf(out, n, "%d", g_cfg.infinite_ammo);
    else if (!strcmp(key, "no_reload")) snprintf(out, n, "%d", g_cfg.no_reload);
    else if (!strcmp(key, "field_of_view")) snprintf(out, n, "%d", g_cfg.field_of_view);
    else if (!strcmp(key, "brightness")) snprintf(out, n, "%d", g_cfg.brightness);
    else if (!strcmp(key, "perspective")) snprintf(out, n, "%d", g_cfg.perspective);
    else if (!strcmp(key, "smooth_textures")) snprintf(out, n, "%d", g_cfg.smooth_textures);
    else if (!strcmp(key, "true_color")) snprintf(out, n, "%d", g_cfg.true_color);
    else if (!strcmp(key, "auto_resolution")) snprintf(out, n, "%d", g_cfg.auto_resolution);
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
