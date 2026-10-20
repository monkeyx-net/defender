/*
 * The game's random numbers: a port of $D711. State is <$DF (the result),
 * <$E0-<$E1 (a 16-bit shift register). The main loop steps it once a tick
 * and the game's routines call it as they need. The `sim` test checks
 * every call against the emulated board.
 */
#ifndef DEFENDER_RNG_H
#define DEFENDER_RNG_H

#include <stdint.h>

typedef struct {
    uint8_t value; /* <$DF */
    uint8_t hi;    /* <$E0 */
    uint8_t lo;    /* <$E1 */
} Rng;

/* One call of $D711; returns the new <$DF. */
uint8_t rng_step(Rng *r);

#endif
