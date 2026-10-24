/* Saved games at payphones (decomp/savegame.c). */
#ifndef SAVEGAME_H
#define SAVEGAME_H

void sg_set_dir(const char *dir);
int sg_title_active(void);                     /* on the title screen */
int sg_at_payphone(void);                      /* for an on-screen hint */
void sg_game_frame(int use);
extern volatile int g_sg_use;                  /* platform: "use" pressed */

/* testing: saves/game<slot>.sav */
int sg_request_load(int slot);                 /* from the title screen; 0: no such save */
void sg_request_save(int slot);                /* done on the next game frame */

/* Platform layer: a saved game was written (with a card save). */
void sg_save_done(int ok);

#endif
