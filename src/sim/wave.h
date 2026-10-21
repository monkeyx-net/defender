/*
 * A wave's process: $DCD9 starts it, $DCEA every 15 ticks.
 *   - no enemies left ($DDAE: landers, those to come, and the other
 *     counts at $A112-$A116, `<$FB`, `<$FE`): the wave ends (not ported)
 *   - with 8 or fewer left the baiter timer ($A118) is cut to [$A10F]/2+1
 *     (/4+1 with 3 or fewer); when it runs out a baiter comes ($EA80,
 *     at most 12, `<$A119`) and it restarts at [$A10F] (1..[$A10F]/4+1
 *     with under 4 left)
 *   - every [$A100] steps ($A117), or when no lander is left, up to
 *     [$A101] of the landers to come (`<$FB`) arrive, keeping under 8
 *   - `<$AE` counts up to $10; `<$24` counts the steps and every $F1
 *     (a minute) adds to a CMOS bookkeeping counter ($F51C, not ported)
 *   - every 40 steps the wave's settings at $A0FB ramp up ($DEEC: bank
 *     7's table at [$C011], per setting max, min and deltas)
 */
#ifndef DEFENDER_WAVE_H
#define DEFENDER_WAVE_H

#include <stdint.h>

#include "sim/game.h"

#define WAVE 0xDCEA

/* $DC1E: a wave starts: the humanoid walker; the humanoids (player block
   +A) placed in four groups at x $00/$40/$80/$C0 (more than 7) and the rest
   at random ($DBE1: picture $F901 or $F915, line $E0, flags $10); the
   wave's settings from the player block (+B on) to $A0FB-$A111, $A112-$A119
   cleared; the enemies carried over: swarmers (`<$FF`, up to 6 at a time
   from a spare block at a random place), mutants (`<$FE`), pods (`<$FD`),
   bombers (`<$FC`, groups of up to 3). The spare block's x takes B as the
   ROM leaves it (`b`: on entry, or from placing the humanoids, then the
   last spare's x); with no swarmers carried over the spare stays taken.
   The pods' owner word is U as the ROM leaves it (the player block + $22,
   or the last mutant's process). */
void wave_start(Game *g, uint8_t b);
/* $DDAE */
uint8_t wave_enemies_left(const Game *g);
/* $DEEC: add delta column `column` of the ramp table to the settings at `settings`. */
void wave_ramp(Game *g, int column, uint16_t settings);
StepResult wave_step(Game *g, uint16_t proc);

#endif
