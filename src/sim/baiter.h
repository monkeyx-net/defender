/*
 * Baiters: they arrive when a wave drags on ($EA80, from the wave's
 * process), at a random x near the screen, and harry the ship.
 *   $EAB4 every 6 ticks: shoots every 1..[$A110]+1 steps (script $D52F);
 *     steps its picture ($F9A3, $F9AD, $F9B7) and at each round, when
 *     `<$DF` > [$A111], steers ($EAEE): far from the ship (beyond $280
 *     units) it moves at the ship's speed $40 more towards it; more than
 *     10 lines off it moves up or down at half the ship's vertical speed
 *     plus a line a frame towards it.
 *   $EB2B, hit: `<$A119` - 1, 200 points, script $D4FD.
 * Process: +7 its object, +9 ticks to its next shot.
 */
#ifndef DEFENDER_BAITER_H
#define DEFENDER_BAITER_H

#include <stdint.h>

#include "sim/game.h"

#define BAITER 0xEAB4
#define HIT_BAITER 0xEB2B

void baiter_spawn(Game *g);
StepResult baiter_step(Game *g, uint16_t proc);
int baiter_hit(Game *g, uint16_t obj);

#endif
