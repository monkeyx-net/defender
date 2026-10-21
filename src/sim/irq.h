/*
 * The interrupt's work (4 interrupts a frame; $DF17 splits on the video
 * counter so the halves of the screen the beam is not on get redrawn):
 *   $E39F objects_draw: every object on the screen list drawn in the band
 *     of lines (bottom, top] is erased (+4 = 0), and every object whose
 *     line is in the band, on screen ($2580 units from the scroll) and not
 *     past column $9C is drawn at ((x - scroll) << 2, line), its odd-x data
 *     when that is odd (+4 keeps where).
 *   $E213 ship_draw: in the band, the ship (`<$BF-<$C0` where it was
 *     drawn) and its flame are erased, the facing (`<$BD` = `<$BB`) applied
 *     and it is drawn again at `<$C1-<$C2` ($F9C1 right, $F9CB left, odd x
 *     by `<$C4` bit 7) with its flame (colours from `<$9F`; the full flame
 *     while thrust is held).
 *   $E07E stars: 16 at $AF9D (column, line, colour), `<$AE` of them shown,
 *     moving at half the scroll; one's colour cycles each frame and one is
 *     moved at random.
 *   $E453 shots_move: every shot moves (its speed and the scroll's) and is
 *     redrawn by its +12 routine ($E498 a mine, two frames from `<$A6`;
 *     $E4D9 a bomb, colours from `<$A8`); at line $2A or column $98 it is
 *     erased and its +16 cleared (aged out by the next $E56A).
 * The pictures' draw routines keep the stack pointer at `<$77-<$78`: not
 * modelled.
 */
#ifndef DEFENDER_IRQ_H
#define DEFENDER_IRQ_H

#include <stdint.h>

#include "sim/game.h"

/* 0, or -1 for something not ported (a shot's routine). */
int objects_draw(Game *g, uint8_t top, uint8_t bottom);
void ship_draw(Game *g, uint8_t top, uint8_t bottom);
void stars_draw(Game *g);
int shots_move(Game *g);

#endif
