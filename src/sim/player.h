/*
 * The player's side: the HUD, the smart bomb and reverse processes, saving
 * a wave's state.
 *   $D629 the ships left (player block +7, at most 5 icons $F9D5, 6
 *     columns apart, at $0F14 / $7114 after clearing $20 x 6); $D680 the
 *     smart bombs (+9, at most 3 icons $F9D9, 4 lines apart, at $291B /
 *     $8B1B after clearing 3 x $0B); both players' when `<$8C` is 2.
 *   $E8C1 the smart bomb (`<$9A` while one is going): see the plan.
 *   $E897 reverse: `<$BB` = -`<$BD` (the ship turns when next drawn), then
 *     it waits for the button's release and 5 ticks (`<$AF`).
 *   $DBB6 saves the wave into the player block: +A..+3C cleared, +A the
 *     humanoids left, +B.. the wave's settings from $A0FB, the counts of
 *     enemies to come ($A0FB-$A100) with those alive ($A112-$A117) added.
 */
#ifndef DEFENDER_PLAYER_H
#define DEFENDER_PLAYER_H

#include <stdint.h>

#include "sim/game.h"

#define SMART_BOMB 0xE8C1
#define SMART_BOMB_FLASH 0xE900
#define SMART_BOMB_HELD 0xE90C
#define SMART_BOMB_DONE 0xE91A
#define REVERSE 0xE897
#define REVERSE_HELD 0xE8AE
#define REVERSE_DONE 0xE8BC

void hud_ships(Game *g);
void hud_bombs(Game *g);
void wave_save(Game *g);
StepResult player_step(Game *g, uint16_t proc, uint16_t resume);

#endif
