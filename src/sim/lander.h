/*
 * Landers and mutants. A lander's process block: +7 its object, +9 the
 * humanoid it is after, +B that humanoid's slot in the table at $A11A, +D
 * ticks to its next shot (a mutant's: +9).
 *
 * Steps, as the ROM resumes them:
 *   $EFF6 every 6 ticks: unless its humanoid is on the ground (hit handler
 *     $ED70), pick another ($EF78); keep $32 lines above the terrain ($B300:
 *     a line per 64 world units) at the wave's speed ($A103); count down to
 *     the next shot ($EEFE); step the picture ($F985, $F98F, $F999). Right
 *     above the humanoid (same x & $FC00) the capture starts.
 *   $F074 every tick: line up on the humanoid, $20 world units a tick, and
 *     come down to 12 lines above it; within $40 units it grabs it (hit
 *     handlers $F1E0 and $ED91) and both rise at the wave's speed.
 *   $F0E6 every 4 ticks: rising until line $32.
 *   $F101 every tick: the humanoid is pulled up a line a tick; when it
 *     reaches the lander it dies ($ED77) and the lander becomes a mutant
 *     ($F8CE, hit handler $EF6D). If the humanoid has gone (shot), the
 *     lander leaves (`<$FB` counts them for later).
 *   $F15E every 3 ticks, the mutant: sideways towards the ship at $A10A,
 *     up or down at $A108, jittering $A107 lines; shoots every 1..$A10B + 1
 *     steps.
 * With no humanoid left a lander becomes a mutant at once.
 */
#ifndef DEFENDER_LANDER_H
#define DEFENDER_LANDER_H

#include <stdint.h>

#include "sim/game.h"

#define LANDER_SEEK 0xEFF6
#define LANDER_ALIGN 0xF074
#define LANDER_RISE 0xF0E6
#define LANDER_TOP 0xF101
#define MUTANT 0xF15E

StepResult lander_seek(Game *g, uint16_t proc);
StepResult lander_align(Game *g, uint16_t proc);
StepResult lander_rise(Game *g, uint16_t proc);
StepResult lander_top(Game *g, uint16_t proc);
StepResult mutant_step(Game *g, uint16_t proc);

#endif
