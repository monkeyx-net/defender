/*
 * Humanoids: the table at $A11A holds their objects (32 words); `<$FA`
 * counts them. On the ground a humanoid's hit handler is $ED70; carried by
 * a lander, $ED91.
 */
#ifndef DEFENDER_HUMANOIDS_H
#define DEFENDER_HUMANOIDS_H

#include <stdint.h>

#include "sim/game.h"

#define HUMANOIDS 0xA11A
#define HUMANOIDS_END 0xA15A
#define HUMANOIDS_LEFT 0xA0FA
#define HUMANOID_ON_GROUND 0xED70
#define HUMANOID_CARRIED 0xED91
#define PLANET_GONE 0xEDEA       /* the process started when the last one dies */
#define HUMANOID_FALL 0xF216     /* a dropped humanoid's process: +7 its object */

/* $EDC4: take humanoid `obj` out of the table; the last one gone starts
   PLANET_GONE. 0, or -1 if it is not in the table (the ROM stops). */
int humanoid_remove(Game *g, uint16_t obj);
/* $ED77: humanoid `obj` dies: out of the table, freed and erased, then it
   explodes ($F8D8, 2 pixels left) with script $D4E4. */
int humanoid_kill(Game *g, uint16_t obj);
/* $F216 every 4 ticks: falling, 8/256 of a line a tick faster to 3 lines;
   on the terrain: dies if faster than $E0 ($ED77), else lands (not ported). */
StepResult humanoid_fall(Game *g, uint16_t proc);

#endif
