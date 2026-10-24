/* collision.c entry points used by the enemy code. */
#ifndef COLLISION_H
#define COLLISION_H
#include "decomp.h"

void n_geom_SideOfLine(void);      /* 8007C04C */
void n_move_Collide(void);         /* 8007CB9C */
void n_wall_FirstCrossing(void);   /* 8007F8F0 */

#endif
