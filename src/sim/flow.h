/*
 * The game's flow: coins, the start buttons, a player's turn and waves.
 *   $D46E / $D47C / $D475: a coin in slot 1 / 2 / 3 (not while the slam
 *     timer `<$7E` or its own debounce `<$7F` / `<$81` / `<$80` runs): the
 *     debounce set to $16, 10 ticks, script $D4AB, then bank 3's coin
 *     routine (cmos.h) with bank 3 selected.
 *   $D839 / $D84E: the 1 / 2 player start, in the attract mode (`<$BA`
 *     negative) with a credit (two for 2 players; free play [$C495] = 1
 *     gives 2): script $D4BD / $D4C2 and $D861: `<$38` cleared; a first
 *     start ends every other process, clears the screen and both player
 *     blocks, sets player 1's ships from [$C485] & $0F, its smart bombs the
 *     same and 10 humanoids, its first wave ($DE7C), its next extra ship
 *     ([$C481], also `<$AB`), copies the block to player 2 and starts the
 *     turn's process ($D919); each start counts a player (`<$8C`), takes a
 *     credit (saved to CMOS) and, the second, redraws the HUD ($D665).
 *   $DE7C: a player's next wave: +8 the wave number; every [$C49D] waves 10
 *     humanoids again; bank 7's settings column for the wave (1-4) to
 *     +B..; beyond wave 4 the ramp's column 3 once a wave, between the
 *     CMOS limits [$C497] and [$C499].
 *   $D07C ends every process but the current one and those of type 2.
 *   $F5D1 clears the screen ($0000-$9BFF), $F5F1 the playfield (lines
 *     $2A-$FF of each column).
 *   $D665 the HUD: the scanner's area cleared, its frame ($D6BC), the
 *     ships, the smart bombs, each player's score.
 */
#ifndef DEFENDER_FLOW_H
#define DEFENDER_FLOW_H

#include <stdint.h>

#include "sim/game.h"

#define COIN_1 0xD46E
#define COIN_2 0xD47C
#define COIN_3 0xD475
#define COIN_WAIT 0xD499
#define START_1 0xD839
#define START_2 0xD84E

/* $D861: a start (credits checked by the caller). */
void game_start(Game *g);
void kill_others(Game *g);
void clear_screen(Game *g);
void clear_playfield(Game *g);
void hud_draw(Game *g);
void wave_setup(Game *g, uint16_t block);
StepResult flow_step(Game *g, uint16_t proc, uint16_t resume);

#endif
