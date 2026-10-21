/*
 * CMOS (the I/O page's $C400-$C4FF: 256 nibbles, kept as the board keeps
 * them, | $F0) and what the game keeps there: the operator's settings, the
 * bookkeeping counters, the credits.
 *   $F822 reads a byte from two nibbles (the first the high), $F838 two
 *   bytes; $F84E writes one, $F864 two (all with bank 0 selected and
 *   restored).
 *   Bank 3 $CBE0 / $CBE6 adds 1 / A (BCD) to the 4-digit bookkeeping
 *   counter B (1-7) at $C3FD + 4B: 1-3 the coins in each slot, 4 the
 *   credits paid, 5 extra ships, 6 minutes played, 7 games.
 *   Bank 3 $CC0D adds A credits (BCD) to `<$37` (at most 99) and saves
 *   them ($C47D).
 *   Bank 3 $CC2C / $CC26 / $CC20, a coin in slot 1 / 2 / 3: its counter,
 *   its units ([$C489 + 2 x (slot - 1)]) added to `<$38` and `<$39`; at
 *   [$C493] units or more, credits: `<$38` / [$C48F] (the rest kept),
 *   plus `<$39` / [$C491] bonus credits (which clear both).
 */
#ifndef DEFENDER_CMOS_H
#define DEFENDER_CMOS_H

#include <stdint.h>

#include "sim/game.h"

uint8_t cmos_read(const Game *g, uint16_t addr);
void cmos_write(Game *g, uint16_t addr, uint8_t value);
void bookkeep(Game *g, int counter, uint8_t amount);
void credits_add(Game *g, uint8_t amount);
void coin_inserted(Game *g, int slot);
/* $DDC2: a BCD byte as binary. */
uint8_t bcd_to_binary(uint8_t a);

#endif
