/*
 * Explosions: $FCCC starts one for an object. 16 slots of $40 bytes at
 * $9C00, taken round robin from the last (`<$E2`); a slot whose +0 is
 * negative is in use for an object appearing and is skipped, one still
 * exploding is erased first ($FDD5: it lists the screen bytes it drew, from
 * +$40 back to its +4). A slot: +0 $0100 (its size, growing), +2 the
 * picture, +4 the end of its list, +6 the centre, +8 the picture's
 * top left (column << 8 | line). The centre is the hit point (`<$F8`) when
 * that lies in the picture, else its middle. The main loop's $FD2D grows
 * and draws them.
 */
#ifndef DEFENDER_EXPLOSIONS_H
#define DEFENDER_EXPLOSIONS_H

#include <stdint.h>

#include "sim/game.h"

#define EXPLOSIONS 0x9C00
#define EXPLOSIONS_END 0xA000
#define EXPLOSION_SIZE 0x40
#define EXPLOSION_LAST 0xA0E2   /* <$E2 */
#define HIT_POINT 0xA0F8        /* <$F8 */
#define HIT_POINT_ADDR HIT_POINT

/* $FCCC: an explosion of object `obj` (not if off screen, or no slot). */
void explosion_start(Game *g, uint16_t obj);
/* $FDD5: erase what the slot at `slot` drew. */
void explosion_erase(Game *g, uint16_t slot);

#endif
