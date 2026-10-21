/*
 * The lander's process. Its block: +7 its object, +9 the humanoid it is
 * after, +B that humanoid's slot in the table at $A11A (32 words), +D ticks
 * to its next shot.
 *
 * $EFF6, every 6 ticks: while its humanoid is on the ground ($ED70 its hit
 * handler) and not right below, or after picking another ($EF78), it
 * keeps $32 lines above the terrain ($B300: a line per 64 world units),
 * moving down or up at the wave's speed ($A103); it counts down to its
 * next shot ($EEFE: every 1..$A105 + 1 ticks of 6, $EEBA) and steps its
 * picture ($F985, $F98F, $F999). Right above its humanoid it starts the
 * capture ($F065); with no humanoid left it becomes a mutant ($F13E).
 */
#ifndef DEFENDER_LANDER_H
#define DEFENDER_LANDER_H

#include <stdint.h>

#include "sim/game.h"

#define LANDER_SEEK 0xEFF6

StepResult lander_seek(Game *g, uint16_t proc);

#endif
