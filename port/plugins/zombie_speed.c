/* An example plugin that changes how the zombies behave: they walk faster
 * (or slower). speed.txt in the mod's folder holds the percent (200 if
 * there's none: twice as fast).
 *
 * The zombie's program (port/decomp/zombie.c) sets its walking speed every
 * frame in zombie_SpeedByDistance (0x80104D5C), which zombie_Update calls
 * directly: the hook runs after it and scales what it set. The same
 * address in the other slot (0x800FCD5C) is the zombie program loaded
 * there instead; and since other enemies' programs load at the same
 * addresses, the hook checks the enemy is a zombie (type 0-10).
 *
 *     clang --target=x86_64-w64-mingw32 -shared -O2 -o zombie_speed.dll zombie_speed.c -I ../include
 *
 * then mods/<mod>/plugins/zombie_speed.dll, and Plugins on in F1 > Mods. */
#include "respect_plugin.h"

#include <stdio.h>

#define SPEED_BY_DISTANCE_B 0x80104D5Cu          /* zombie_SpeedByDistance, program slot B */
#define SPEED_BY_DISTANCE_A 0x800FCD5Cu          /* the same, program slot A */
#define EN_TYPE   0x315                          /* u8: the enemy's type (0-10: zombies) */
#define EN_SPEED  0x866                          /* s16: walking speed (negative: forward), the game keeps it >= -0x7F */

static const RespectApi *api;
static int percent = 200;

static void faster(void *user, uint32_t addr) {
    (void)user, (void)addr;
    uint32_t e = api->get_reg(RESPECT_A0);       /* the zombie (its record) */
    uint8_t *type = RESPECT_RAM(api, e + EN_TYPE);
    int16_t *speed = RESPECT_RAM(api, e + EN_SPEED);
    if (!type || !speed || *type > 10) return;   /* another enemy's program at this address */
    int v = *speed * percent / 100;
    if (v < -0x7F) v = -0x7F;                    /* (as the game limits it) */
    if (v > 0x7F) v = 0x7F;
    *speed = (int16_t)v;
}

RESPECT_EXPORT int respect_plugin_init(const RespectApi *a) {
    api = a;
    char path[1200];
    snprintf(path, sizeof path, "%s/speed.txt", a->mod_dir);
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", &percent) != 1 || percent < 10 || percent > 1000) percent = 200;
        fclose(f);
    }
    if (a->hook(SPEED_BY_DISTANCE_B, 0, faster, 0) || a->hook(SPEED_BY_DISTANCE_A, 0, faster, 0)) return 1;
    a->log("zombies walk at %d%% of their speed", percent);
    return 0;
}
