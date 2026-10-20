/*
 * The ship's motion and the scrolling: a port of $E263, which the
 * interrupt runs once a frame (not while <$BA bit 6 is set).
 *
 * World x is 16 bits, 32 units to a pixel (2048 pixels around the planet);
 * the screen's left edge is `scroll` (<$20). Velocity is 24 bits (<$C7-<$C9,
 * world units per frame x 256): thrust adds the facing's acceleration
 * (<$BD: $0300 facing right, $FD00 left), friction takes 1/64 every frame,
 * and the scroll speed is capped at $100 units (8 pixels) a frame. The
 * ship's screen x (<$C3, 1/256 byte; $2000 facing right, $7000 left) leads
 * by the speed and slides there 2 pixels a frame (the scroll making up for
 * it, so the ship stays put in the world). Up and down start at 1 line a
 * frame and add 8/256 a frame up to 2, within lines $2B-$EE.
 */
#ifndef DEFENDER_SHIP_H
#define DEFENDER_SHIP_H

#include <stdint.h>

typedef struct {
    int32_t velocity;   /* <$C7-<$C9, 24 bits, signed */
    uint16_t scroll;    /* <$20 */
    uint16_t last_scroll; /* <$22: the scroll a frame ago */
    uint16_t screen_x;  /* <$C3 (<$C1 is its high byte) */
    int16_t slide;      /* <$95: the scroll's share of the slide */
    uint16_t world_x;   /* <$CC: the ship in world x */
    uint16_t y;         /* <$C5, 8.8 (<$C2 is its high byte) */
    int16_t vy;         /* <$CA */
} Ship;

/* The controls as the game reads them: <$7B (IN0) and <$7D (IN1). */
typedef struct {
    uint8_t in0, in1;
    int16_t thrust;     /* <$BD: the facing's acceleration */
} ShipInput;

void ship_step(Ship *s, const ShipInput *in);

#endif
