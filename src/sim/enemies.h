/*
 * What happens when something is hit: the hit handler at an object's +8,
 * which the hit test ($E6BA) and the smart bomb call with the object.
 *   $F20B a lander: `<$A112` - 1, killed for 150 ($D507)
 *   $EF6D a mutant: `<$A115` - 1, killed for 150 ($D4F8)
 *   $F1E0 a lander carrying a humanoid: the humanoid falls (process
 *         $F216, script $D4E9), then as $F20B
 * A kill ($F416; $F40D leaves the block on its list): the owner process
 * ends ($D013), the block is freed, the points scored ($D360), the picture
 * erased, an explosion started, the sound script played.
 */
#ifndef DEFENDER_ENEMIES_H
#define DEFENDER_ENEMIES_H

#include <stdint.h>

#include "sim/game.h"

#define HIT_LANDER 0xF20B
#define HIT_MUTANT 0xEF6D
#define HIT_LANDER_CARRYING 0xF1E0

/* $F416 (free = 1) or $F40D. 0, or -1 if not ported (an extra life) or
   the ROM would stop (no such process or block). */
int enemy_kill(Game *g, uint16_t obj, uint16_t points, uint16_t script, int free);
/* Call hit handler `handler` for object `obj`: 1 if it was hit, 0 if the
   handler declined (the hit test then reports no hit), -1 if not ported. */
int enemy_hit(Game *g, uint16_t handler, uint16_t obj);

#endif
