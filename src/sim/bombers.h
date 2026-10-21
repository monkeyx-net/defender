/*
 * Bombers ($F29D): a group of up to 3 under one large process ($F2F7,
 * every tick), its objects at +7, +9, +B (+D unused), +F how many are
 * left, +10 the group's base line. They arrive on the far side of the
 * planet at line $50, moving at ±[$A106] (each group the other way:
 * `<$AA`). Each tick one bomber picked at random steps its picture
 * ($F929-$F947), gets a random push and damping; off screen it drifts
 * towards the base line (which wanders within $40-$68), on screen it keeps
 * away from the ship's line; 1 tick in 8 it lays a mine ($F3DC: a shot
 * $F95B, +12 $E498, while there are under 10 shots, lasting 1..32).
 * Hit ($F3BC): 250 points (script $D502), out of its group; the group's
 * process ends with its last bomber.
 */
#ifndef DEFENDER_BOMBERS_H
#define DEFENDER_BOMBERS_H

#include <stdint.h>

#include "sim/game.h"

#define BOMBERS 0xF2F7
#define HIT_BOMBER 0xF3BC

void bomber_spawn(Game *g, uint8_t n);
StepResult bomber_step(Game *g, uint16_t proc);
int bomber_hit(Game *g, uint16_t obj);

#endif
