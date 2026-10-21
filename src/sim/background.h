/*
 * The processes that run all game long (started with the player's turn,
 * $D9CD-$D9FA):
 *   $E782 (then $E784) every 2 ticks: palette entry 1 (`<$27`, the laser's
 *     colour) through the zero-ended table at $E799.
 *   $E9BF every 4 ticks: `<$9F` steps through $A162-$A182 and `<$A8`
 *     through $A1A2-$A1BA.
 *   $F43D / $F44A every 6 ticks: palette entries 13-15 (`<$33-<$35`) from
 *     the 3-byte entries at $F45B-$F463, in turn.
 *   $F464 / $F472: palette entries 10 and 12 (`<$30`, `<$32`) flash white
 *     and black for 3 ticks, then take a random colour from $E799 for 6;
 *     `<$A6` swaps between $CCB0 and $CCBC.
 *   $F493 / $F4B0: until the first points (`<$8A`) the player's score
 *     blinks: cleared ($F5C7) for 8 ticks, drawn for 12.
 *   $E9E3 / $E9EE / $E9FC, 2, 2 and 4 ticks: objects near the screen move
 *     from the off-screen list to the screen list ($EA33, after 8 frames of
 *     their speed), those far off the other way ($EA0A; both within $3E80
 *     units from scroll - $0C80); shots age out ($E56A: +15 counts down
 *     while +16 is set); then the scanner (bank 1 $CCAD).
 * The scanner: the dots of the last drawing (addresses listed from $B05D
 * to [`<$97`]) and the ship's marker are cleared; the terrain's 64 columns
 * ($30-$6F) are redrawn from bank 1's table at $CD69 (line, colour word
 * per 3 bytes, from (scroll - $6D40) >> 10; not while `<$BA` bit 1); the
 * frame's corners; each object's +12 at ((x - (scroll - $6D40)) >> 10,
 * line >> 3) + $3007; the ship's marker $9099 at (`<$BF` >> 4, `<$C0` >>
 * 3) + $4B07.
 */
#ifndef DEFENDER_BACKGROUND_H
#define DEFENDER_BACKGROUND_H

#include <stdint.h>

#include "sim/game.h"

#define COLOUR_CYCLE 0xE782
#define COLOUR_CYCLE_NEXT 0xE784
#define POINTERS 0xE9BF
#define PALETTE_CYCLE 0xF43D
#define PALETTE_CYCLE_NEXT 0xF44A
#define FLASH 0xF464
#define FLASH_NEXT 0xF472
#define SCORE_BLINK 0xF493
#define SCORE_BLINK_DRAW 0xF4B0
#define HOUSEKEEPING 0xE9E3
#define HOUSEKEEPING_2 0xE9EE
#define HOUSEKEEPING_3 0xE9FC

/* $F5C7: clear w x h bytes at screen address `at`. */
void game_clear(Game *g, uint16_t at, uint8_t w, uint8_t h);
/* bank 1 $CCAD */
void scanner_draw(Game *g);
StepResult background_step(Game *g, uint16_t proc, uint16_t resume);

#endif
