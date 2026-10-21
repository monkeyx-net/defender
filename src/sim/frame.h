/*
 * A frame of the game, as the board runs it: the interrupt's top half at
 * line 0 ($DF5D on: the tick, the palette, the coins, the terrain, the
 * ship and the objects below the beam drawn, the objects moved), the main
 * loop's tick ($E7C7 on: load shedding, the ship's collisions, the
 * explosions, a random number, the switches' processes started, every
 * process's step), then the bottom half at line 128 ($DF2D on: sounds and
 * the controls, the ship's motion, the stars, the objects, ship and shots
 * above the beam). The ROM's main loop usually finishes before line 128;
 * when it does not, the bottom half breaks into it, which the C game does
 * not do.
 */
#ifndef DEFENDER_FRAME_H
#define DEFENDER_FRAME_H

#include "sim/game.h"

/* 0, or -1 if something the frame needs is not ported (the state is then
   unspecified). */
int game_irq_top(Game *g);
int game_main_tick(Game *g);
int game_irq_bottom(Game *g);
int game_frame(Game *g);

#endif
