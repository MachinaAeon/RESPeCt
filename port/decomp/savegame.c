/* The port's hooks into the game's mode switches and TEST's start-up
 * (tools/recomp/hooks.txt), and a fix for the title's memory card list.
 *
 * Saved games at payphones, a port feature, lived here until v0.3.1. A
 * payphone opened the game's own save screen through its game-over
 * handler, and the ending's results counted that: a click at a phone by
 * mistake spoiled them. The game's own memory card save (offered after
 * dying) is all there is now, as on the PlayStation. */
#include "game_vars.h"
#include "savegame.h"

void rt_log(const char *fmt, ...);

/* kernel 80012FF4 (mode): an overlay mode starts. */
void sg_hook_mode_switch(void) { rt_log("savegame: mode switch to %u (game type %d)", R[4], D_80062DF4); }

/* title 8006C244, 8006CF24: the title screen's loop and its frames. */
void sg_hook_title(void) {}
void sg_hook_title_frame(void) {}

/* test 80071CF4: TEST rebuilds the game from the checkpoint. */
void sg_hook_restore(void) {}

/* kernel 80039C58 (pattern, dirent): libapi's firstfile. It looks the
 * device up in the BIOS's device table in low RAM (0x150) to patch its
 * search function before calling the BIOS; the port's BIOS has no such
 * table, so it found no device and every search came back empty (the
 * title's LOAD GAME said "No save data"). Call the BIOS directly. */
void bios_call(u32 table, u32 fn);
void n_card_FirstFile(void) {
    u32 src = R[4], dst = 0x8006BCF8u;            /* the device name, as the original keeps it */
    while ((s8)GV(u8, src) >= 0x3B) GV(u8, dst++) = GV(u8, src++);
    GV(u8, dst) = 0;
    bios_call(0xB0, 0x42);
}
