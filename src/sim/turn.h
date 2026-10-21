/*
 * A player's turn, one process from the start to the wave:
 *   $D919: bookkeeping counter 7 + 1 (bank 3 selected), then $D91E.
 *   $D91E (also after a wave or a death): the reset ($D7B6: palette from
 *     $F8BE, laser colours $E69F, stars $E052, the objects $D805, the
 *     colour tables $E54B and $E149), every other process ended, `<$BA` =
 *     $7F; unless it is the only process, it waits ($D93A, 15 ticks at a
 *     time) for the coin timers `<$7F` and `<$80` (the ROM checks `<$7F`
 *     twice, not `<$81`); then ($D942) a fresh process pool with only the
 *     turn's next step ($D950) in it, and the main loop starts again.
 *   $D950: (a cocktail table's player 2 side: the screen flipped, not
 *     ported) the scroll 0, the terrain (bank 7 $C006, $C000), the
 *     playfield cleared, the ship's state for a new ship, `<$8D` the
 *     player's block, palette entry 5 by the wave ($DB53), a ship taken,
 *     the HUD, the background processes; with 2 players, "PLAYER ONE" /
 *     "PLAYER TWO" ($C0EF / $C0F1 at $3C80) for $80 ticks.
 *   $DA1F: the playfield cleared, `<$BA` = 5 (| 2 without humanoids), $60
 *     ticks; $DA32: the wave starts ($DC1E), `<$BA` = 0 (| 2), and the
 *     process becomes the wave's ($DCD9).
 */
#ifndef DEFENDER_TURN_H
#define DEFENDER_TURN_H

#include <stdint.h>

#include "sim/game.h"

#define TURN 0xD919
#define TURN_RESET 0xD91E
#define TURN_WAIT 0xD93A
#define TURN_SETUP 0xD950
#define TURN_CLEAR 0xDA1F
#define TURN_WAVE 0xDA32

/* $D7B6 */
void game_reset(Game *g);
StepResult turn_step(Game *g, uint16_t proc, uint16_t resume);

#endif
