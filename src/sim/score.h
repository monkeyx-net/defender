/*
 * Scoring: $D360 adds points to the player's score and redraws it ($D3D9).
 * The player block ($D6FE: $A1C2 for player 1, $A1FF for player 2 by
 * `<$8B`): +0..+3 the score in BCD (the display shows +1..+3), +4..+6 the
 * next extra life's score, +7 ships, +9 smart bombs. Points come as a
 * word: the low bit of the high byte picks the digit (set: the low byte
 * is shifted up a digit), the rest of the high byte which byte pair. Every
 * [<$AB-<$AC] (BCD) points give a ship and a smart bomb.
 */
#ifndef DEFENDER_SCORE_H
#define DEFENDER_SCORE_H

#include <stdint.h>

#include "sim/game.h"

#define PLAYER_ONE 0xA1C2
#define PLAYER_TWO 0xA1FF

/* $D6FE */
uint16_t score_player(const Game *g);
/* $D360: add `points`; reaching the next extra life's score gives a ship
   and a smart bomb (the HUD redrawn, script $D4B0, bookkeeping counter 5
   with bank 3 left selected) and moves it on by [<$AB-<$AC] (once a call). */
void score_add(Game *g, uint16_t points);
/* $D3D9: draw the current player's score (6 digits, leading zeros blank). */
void score_draw(Game *g);
/* $D3DB: draw player `player`'s score. */
void score_draw_player(Game *g, uint8_t player);

#endif
