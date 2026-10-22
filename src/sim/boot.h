/*
 * Power-on, after the ROM's self-test ($D73A-$D7B3): RAM cleared (the
 * screen by $F5D1, the rest to $BFFF), the credits from CMOS [$C47D] (0
 * if over $20 or a digit over 9), the random seed `<$E0-<$E1` = $A55A,
 * the band `<$A1-<$A2` = $FF70 and `<$A3` 0, the switches `<$79-<$7A` all
 * set, the interrupt's handler ($D8DC), bank 3's $C033 (today's high
 * scores from its $CECF, 48 bytes into $B260 as nibbles), the process
 * pool ($D7C6), the reset ($D7B6), the printer ($F800: `<$52`, `<$49`
 * cleared, `<$36` and `<$48` 3, `<$59` $FFFF), the attract mode's process
 * ($D825, type 1), `<$BA` $FF; then the main loop.
 */
#ifndef DEFENDER_BOOT_H
#define DEFENDER_BOOT_H

#include "sim/game.h"

/* The game's state at power-on from its CMOS (g->data, bank[] and cmos set). */
void game_power_on(Game *g);
/* Bank 3 $CCC1 ($C033): today's high scores from bank 3's $CECF, 48 bytes
   into $B260 as nibbles (bank 3 left selected, as $F507 before it). */
void scores_today_reset(Game *g);

#endif
