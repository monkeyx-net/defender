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
#define HUMANOID_CARRIED_BY_SHIP 0xF24C /* the same process once the ship has caught it */
#define HUMANOID_WALK 0xECC9     /* one process walks them all, a slot a step */
#define BONUS_250 0xEE65         /* the score shown where one landed by itself */
#define BONUS_500 0xEE73         /* ... where the ship caught one or set it down */
#define BONUS_SHOW 0xEEB2

/* $EDC4: take humanoid `obj` out of the table; the last one gone starts
   PLANET_GONE. 0, or -1 if it is not in the table (the ROM stops). */
int humanoid_remove(Game *g, uint16_t obj);
/* $ED77: humanoid `obj` dies: out of the table, freed and erased, then it
   explodes ($F8D8, 2 pixels left) with script $D4E4. */
int humanoid_kill(Game *g, uint16_t obj);
/* $F216 every 4 ticks: falling, 8/256 of a line a tick faster to 3 lines;
   on the terrain: dies if faster than $E0 ($ED77), else lands: on the
   ground again ($ED70), 250 points ($EE65), script $D4DF. */
StepResult humanoid_fall(Game *g, uint16_t proc);
/* $F24C every tick: under the ship (10 lines down, 4 pixels on) until it
   reaches the terrain: set down, 500 points ($EE73). */
StepResult humanoid_carried(Game *g, uint16_t proc);
/* $ECC9 every 2 ticks: the next table slot (16 of them); a humanoid on
   screen and on the ground walks 2 pixels, steps its picture ($F901-$F90B
   going left, $F915-$F91F right) and follows the terrain a line a step (4
   lines above it going left, 15 going right); 9 in 256 it turns. */
StepResult humanoid_walk(Game *g, uint16_t proc);
/* $EE65 / $EE73: the bonus object (pictures $F9DD / $F9E7) where the
   humanoid is, moving with the ship, for 50 ticks; $EEB2 frees it. */
StepResult bonus_start(Game *g, uint16_t proc, int five_hundred);
StepResult bonus_end(Game *g, uint16_t proc);
/* The hit handlers: $ED70 on the ground, $ED91 carried or falling.
   `<$DE` set means the ship touched it (the ship's collisions). 1 hit,
   0 declined (the hit test reports no hit), -1 not ported. */
int humanoid_hit(Game *g, uint16_t obj, int carried);

#endif
