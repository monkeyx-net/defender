/*
 * Hyperspace, the ship's death, the wave's end and game over.
 *   $E91F hyperspace (not while `<$BA` has bits other than 1): `<$BA` =
 *     $77, the playfield cleared, 15 ticks; $E936: the shots freed
 *     ($D0F2), the scroll from the random state (`<$DF-<$E0`), the facing
 *     by `<$E0` bit 0 (right: screen x $2000, else $7000), the line
 *     `<$E0`/2 + $2A, at rest, the terrain rebuilt (bank 7 $C000),
 *     `<$BA` = $50 (| 2), the ship shown as an object ($F9C1 / $F9CB,
 *     appearing) for $28 ticks; $E9AC: the object gone, `<$BA` = 0 (| 2),
 *     and the ship dies when `<$E1` > $C0 (the process becomes $DA46).
 *   $DA46 the death: `<$BA` = $58 (| 2), the ship's 8 x 6 cleared, the
 *     wave saved ($DBB6), script $D4B5, a white copy of the ship's picture
 *     at $AFDD ($DB5C: every lit nibble $B), flashed ($DA7C / $DA8C, 2 ticks
 *     each) while palette entry 11 (`<$31`) takes $DB4B's values and the
 *     background (`<$26`) goes black; then ($DAA8) `<$BA` = $7F and the
 *     background white for 2 ticks ($DAB8), every other process ended and
 *     bank 7's ship explosion ($C00E): 128 particles at $B300-$B7FF fly
 *     from the ship while `<$31` fades through $C6AB, a tick at a time
 *     through the banked sleep ($FFD1: resuming at $F4CC); then ($DAC9)
 *     the score drawn, the sound script stopped, sound $13; with no enemy
 *     left the wave's end ($DDEC); then the next player with ships
 *     ($DB15, "PLAYER ONE/TWO" "GAME OVER" first if this one has none
 *     left: $DAE6) or game over ($DB2C: "GAME OVER" at $3E80, `<$BA` =
 *     $FF, sound $13, $28 ticks, the attract mode: not ported).
 *   $DDEC the wave's end (it saves where it was called at +D): the
 *     background black, the playfield cleared, "ATTACK WAVE n COMPLETED"
 *     ($C0F9, $C0FB), "BONUS X" ($C0F3) and 100 x the wave (at most 5) per
 *     humanoid left, each drawn ($F915) and scored 4 ticks apart; the next
 *     wave set up ($DE7C), $80 ticks, back where it was called.
 */
#ifndef DEFENDER_DEATH_H
#define DEFENDER_DEATH_H

#include <stdint.h>

#include "sim/game.h"

#define HYPERSPACE 0xE91F
#define HYPERSPACE_JUMP 0xE936
#define HYPERSPACE_DONE 0xE9AC
#define DEATH 0xDA46
#define DEATH_ERASE 0xDA7C
#define DEATH_DRAW 0xDA8C
#define DEATH_WHITE 0xDAB8
#define DEATH_NEXT 0xDB15
#define BANKED_RESUME 0xF4CC
#define WAVE_END_BONUS 0xDE66
#define WAVE_END_DONE 0xDE79

StepResult death_step(Game *g, uint16_t proc, uint16_t resume);
/* $DDEC from the wave's process or the death, returning to `back`. */
StepResult wave_end(Game *g, uint16_t proc, uint16_t back);

#endif
