/*
 * The text printer (bank 2): $CAA7 draws a message, $CAB2 erases it,
 * $CABD / $CAC8 draw / erase one character a call (the typewriter), all
 * at X with U the message: a word pair (the list's start, its end).
 * Items: a word at $8000 or above points to a '/'-ended word printed with
 * a space after it; else a control code (byte) 0-8 and its byte(s):
 * 0 / 1 the character / line spacing (`<$4C` / `<$4D`), 2 / 3 the cursor
 * column from the line start / on, 4 / 5 its line, 6 the position (both,
 * a word), 7 a new line, 8 a glyph (its descriptor, a word). The font is
 * bank 2's ($C5E3); characters are drawn ($F522) or cleared ($F57B) at the
 * cursor (`<$50-<$51`), which moves on by their width and the spacing.
 * The printer's state: `<$3D` the glyph routine ($FFB3 / $FFB6), `<$3F`
 * what follows a character ($CAED: the next, $CB5F: return), `<$4E` the
 * line start, `<$52` where in the list (high byte 0: none in progress),
 * `<$54` the word, `<$56` the list's end, `<$58` a space due.
 */
#ifndef DEFENDER_TEXT_H
#define DEFENDER_TEXT_H

#include <stdint.h>

#include "sim/game.h"

/* 1 when the message is done, 0 after one character (one_char). */
int text_print(Game *g, uint16_t at, uint16_t message, int erase, int one_char);
/* Bank 2 $CBC1 ($C00E): the BCD number `n` (leading zeros left out, 0 as
   "0") at `at`, through the font and the spacing `<$4C`; `<$59` keeps the
   digits still to print ($FFFF: none in progress). Returns the cursor. */
uint16_t number_print(Game *g, uint16_t at, uint16_t n);
/* $F522 / $F57B: a glyph (or any 4-byte descriptor) copied / cleared at `at`. */
void glyph_draw(Game *g, uint16_t desc, uint16_t at);
void glyph_clear(Game *g, uint16_t desc, uint16_t at);

#endif
