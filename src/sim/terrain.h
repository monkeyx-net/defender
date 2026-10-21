/*
 * The terrain (ROM bank 7). Its shape is the 256-byte bit stream at bank
 * 7 $C350-$C44F: one bit per step of 32 world units (a pixel), 1 down, 0
 * up. Two cursors walk it, one for each edge of the screen: left
 * `<$09` (byte), `<$0F` (bit), `<$0D` (the byte rotated), `<$11` (its
 * line); right `<$0B`, `<$10`, `<$0E`, `<$12`. Every screen column's
 * terrain is kept in two circular buffers of 152 entries (line, colour
 * word $7007 or $0770, 3 bytes; a copy $1C8 on so they read straight):
 * $B700 (cursor `<$05`) and $BA90 (`<$07`), for the two halves of each
 * 32-unit step (scroll bit 5).
 *   $C262 (bank 7 $C006): the world's heights at $B300-$B6FF, a line per
 *     64 units, for the game's code ($ED59).
 *   $C015 ($C000): the cursors and buffers for the screen at the scroll.
 *   $C092 ($C003, in the interrupt): the cursors follow the scroll (`<$15`
 *     the scroll they are at), then the 152 columns are drawn from column
 *     $98 down, each first clearing its last pixel pair (addresses listed
 *     at $BE20). It keeps S at `<$13-<$14` meanwhile (not modelled).
 *   $C29A ($C009): clears that list.
 */
#ifndef DEFENDER_TERRAIN_H
#define DEFENDER_TERRAIN_H

#include "sim/game.h"

void terrain_heights(Game *g);
void terrain_start(Game *g);
void terrain_update(Game *g);
void terrain_forget(Game *g);

#endif
