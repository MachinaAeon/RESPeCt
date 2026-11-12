/* The port's hooks into mode switches (decomp/savegame.c). */
#ifndef SAVEGAME_H
#define SAVEGAME_H

void sg_hook_mode_switch(void);
void sg_hook_title(void);
void sg_hook_title_frame(void);
void sg_hook_restore(void);

#endif
