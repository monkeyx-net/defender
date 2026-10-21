/*
 * Enemy shots: $E3F3 takes a block for a shot from an object and puts it
 * on the shots list ($A06D); $EEBA fires one aimed at the ship (landers
 * and others). A shot lives in screen coordinates: +A is (column << 8 |
 * line) and +C (line << 8 | column), as 8.8 values.
 */
#ifndef DEFENDER_SHOTS_H
#define DEFENDER_SHOTS_H

#include <stdint.h>

#include "sim/game.h"

#define SHOTS_MAX 20      /* <$99 counts them */

/* $E3F3: a shot from object `from` (on screen, below line $2A), owned by
   process `owner`, with +12, picture and hit handler. Returns its block,
   or 0 (too many, off screen, too high or no free block). */
uint16_t shot_create(Game *g, uint16_t from, uint16_t owner, uint16_t data, uint16_t picture, uint16_t hit);
/* $EEBA: fire the bomb picture $F95B from `from` towards the ship, give
   or take a random spread (and the ship's speed half the time). 1 if fired. */
int shot_fire(Game *g, uint16_t from, uint16_t owner);

#endif
