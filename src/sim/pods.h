/*
 * Pods and swarmers.
 *   $EB36: `n` pods, objects with no process of their own ($F8F7, handler
 *     $EB74, +12 $CCCC, owned by the caller's process) at a random x
 *     ((`<$E0` & $3F) + $10, `<$E1`), drifting at random fixed speeds.
 *   $EB74, a pod hit: freed (its owner lives on), 1000 points, script
 *     $D4F3, and 1..7 swarmers burst out ($EB9E); `<$A114` - 1.
 *   $EB9E: up to `n` swarmers (`<$A116` at most 20) at an object: a
 *     process ($EC17) each and an object ($F97B, handler $EBE9, +12 $2424),
 *     random speeds ($EB89), +9 a vertical pull (random & [$A10E]), a
 *     first sleep of 0-31 ticks, +B ticks to its first shot.
 *   $EC17 / $EC2A every 3 ticks: sideways at [$A10C] towards the ship,
 *     pulled up or down towards its line (within ±$200, damped, jittered);
 *     far off screen it turns back; shoots every 1..[$A10D]+1 steps when
 *     heading for the ship ($EC86: script $D534).
 *   $EBE9, a swarmer hit: 150 points, script $D516.
 */
#ifndef DEFENDER_PODS_H
#define DEFENDER_PODS_H

#include <stdint.h>

#include "sim/game.h"

#define SWARMER_AIM 0xEC17
#define SWARMER 0xEC2A
#define HIT_POD 0xEB74
#define HIT_SWARMER 0xEBE9

void pod_spawn(Game *g, uint8_t n);
void swarmer_spawn(Game *g, uint8_t n, uint16_t from);
StepResult swarmer_step(Game *g, uint16_t proc, int aim);
int pod_hit(Game *g, uint16_t obj);
int swarmer_hit(Game *g, uint16_t obj);

#endif
