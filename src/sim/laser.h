/*
 * The ship's laser: a port of the laser process ($E591 starts it; $E5CB
 * moves one going right, $E638 left, once a tick). Up to 4 at a time
 * (<$B5). A laser lives on the screen, not in the world: it starts at the
 * ship's nose and its head runs 8 pixels a tick to the screen's edge
 * (bytes $11 then a $99 tip), 3 bytes behind it are recoloured from the
 * colour table, and the tail is cleared a byte (2 pixels) a tick. A hit
 * ($E5AB: the 8 x 1 picture $F96F at the head, through $E6BA) or the edge
 * ends it: it is erased from tail to head.
 *
 * Screen addresses are the arcade's: (column byte << 8) | line, into video
 * RAM laid out as the board's ($0000-$97FF shown, gfx/williams.h).
 */
#ifndef DEFENDER_LASER_H
#define DEFENDER_LASER_H

#include <stdint.h>

#include "sim/rng.h"

#define LASER_MAX 4
#define LASER_COLOURS 0xA142      /* the colour table's RAM address ... */
#define LASER_COLOURS_SIZE 32     /* ... and size ($A142-$A161) */

typedef struct {
    uint16_t head, mid, tail;     /* process +7, +9, +B */
    int left;                     /* facing left when fired */
} Laser;

typedef struct {
    uint16_t next;                /* <$A4: the next colour, a RAM address */
    uint8_t table[LASER_COLOURS_SIZE];
} LaserColours;

/* $E69F: 32 random bytes of palette index 0 or 1 in each nibble. */
void laser_colours_fill(LaserColours *c, Rng *r);

/* A new laser from the ship at screen position `ship` (<$C1-<$C2). */
void laser_start(Laser *l, uint16_t ship, int left);

/* One tick: returns 1 if it has reached the edge (or `frozen`, <$BA bit 6)
   and must end (laser_end), else 0 once drawn. */
int laser_tick(Laser *l, LaserColours *c, uint8_t *vram, int frozen);

/* Where the hit test looks: the screen position of the $F96F line. */
uint16_t laser_hit_position(const Laser *l);

/* Erase from tail to head. */
void laser_end(const Laser *l, uint8_t *vram);

#endif
