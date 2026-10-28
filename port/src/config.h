/* Player settings from resurv.ini (see config.c for the file's contents). */
#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>

enum {
    ACT_FORWARD, ACT_BACK, ACT_LEFT, ACT_RIGHT, ACT_SPRINT, ACT_QUICK_SEARCH, ACT_CROSS,
    ACT_TRIANGLE, ACT_SQUARE, ACT_CIRCLE, ACT_START, ACT_SELECT, ACT_R1, ACT_L2, ACT_R2,
    ACT_RELOAD,                /* not a pad button: the port reloads the gun */
    ACT_COUNT
};
#define MAX_KEYS_PER_ACTION 4

/* Controller buttons (Xbox layout, through XInput). */
enum { PADB_A, PADB_B, PADB_X, PADB_Y, PADB_LB, PADB_RB, PADB_LT, PADB_RT, PADB_BACK, PADB_START,
       PADB_L3, PADB_R3, PADB_COUNT };
extern const char *g_pad_button_names[PADB_COUNT];

typedef struct {
    float sens_x, sens_y;
    int invert_y;
    int look_limit;            /* degrees */
    int menu_mouse, menu_mouse_step;
    char keys[ACT_COUNT][MAX_KEYS_PER_ACTION][16];
    int nkeys[ACT_COUNT];
    int fullscreen;
    int resolution_scale;
    int widescreen;
    int subpixel;
    int smooth_scaling;
    int movie_upscaling;
    int gap_fill;                  /* close gaps in distant models at high resolution */
    int hd_textures;               /* use an HD texture pack if there is one */
    int hd_texture_memory;         /* MB of decoded HD textures kept */
    int depth_buffer;              /* depth-test 3D at high resolution */           /* bicubic, deblocked movies at the internal resolution */
    int skip_intro;
    int god_mode, infinite_ammo, no_reload;
    int perspective;           /* perspective-correct textures */
    int brightness;            /* %, 100 = the PlayStation's */
    int field_of_view;         /* degrees across 4:3; 0 = the game's own (61) */
    int volume;                /* master, 0-100 % */
    int music_volume, sfx_volume;  /* 0-100 % */
    int limiter;
    int same_sound_limit;          /* copies of one sound effect at a time, 0 = any */
    int pad_enabled;               /* use a controller */
    int pad_look_speed, pad_look_speed_y;   /* degrees per second at full tilt */
    int pad_invert_y;
    int pad_deadzone;              /* % of the stick's travel ignored */
    char texture_pack[512];        /* HD pack folder or archive; empty = look next to the exe */
    char pad_bind[PADB_COUNT][16]; /* action per button (config.c lists them) */
} Config;

extern Config g_cfg;
extern const uint16_t g_action_bits[ACT_COUNT];
void config_load(const char *path);
int config_save(const char *path, const char *extra);

#endif
