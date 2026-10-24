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
    int depth_buffer;              /* depth-test 3D at high resolution */           /* bicubic, deblocked movies at the internal resolution */
    int skip_intro;
    int god_mode, infinite_ammo, no_reload;
    int field_of_view;         /* degrees across 4:3; 0 = the game's own (61) */
    int volume;                /* master, 0-100 % */
    int music_volume, sfx_volume;  /* 0-100 % */
    int limiter;
    int same_sound_limit;          /* copies of one sound effect at a time, 0 = any */
} Config;

extern Config g_cfg;
extern const uint16_t g_action_bits[ACT_COUNT];
void config_load(const char *path);
int config_save(const char *path, const char *extra);

#endif
