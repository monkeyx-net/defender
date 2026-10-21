/*
 * The attract mode (ROM bank 1), and the high scores after a game. Its
 * processes run in bank 1: those it creates start there directly, the
 * rest sleep through $F4BE / $F4CC (bank and address kept at +C / +D).
 *   $D825 (the power-on process, and game over's $DB48): bank 1's $C006.
 *   $C006: every other process ended, `<$BA` = $FF, the stars ($E052),
 *     `<$A183` the credits; after a game (`<$B7`) each player's score
 *     against today's eight best (12 nibbles each at $B260: the score's
 *     bytes 1-3, then the initials; read and written as CMOS is, $F822 /
 *     $F84E): a better one is entered ($C030-$C10E) and sorted into
 *     today's table ($C194 from $B2A8) and, if it beats the last there,
 *     the all-time one in CMOS ($C411-$C47C); then the hall of fame. Never
 *     after power-on: straight to the logo ($C677).
 *   Entering initials: the screen cleared, sounds $1B and 1 (2 beating
 *     today's best), "PLAYER ONE/TWO" and the prompt; the letters at
 *     `<$00`, `<$02`, `<$04` ('/'-ended words, from "A@@"); $C1FA steps
 *     the current one (`<$A17A`) with the stick (down IN0 bit 7, up IN1
 *     bit 0: after 3 ticks, then faster), $C1E7 blinks palette entry 13,
 *     $C1DC counts `<$A17D` down every second; fire (released for 5 ticks
 *     first) takes the next letter; after the third, or the time out, the
 *     entry is sorted in.
 *   $C263 the hall of fame: the logo unpacked ($C86F, from bank 1 $CAA0
 *     run-length coded to $B412, $3C x $18 bytes), the scores, the
 *     credits' process ($C8F4), both tables ($C306: rank, initials,
 *     digits, a line each $0A apart), the logo; $3C x $0A ticks, then the
 *     demo ($C351; at once if a credit came in, `<$A184`).
 *   $C351 the demo: a lander takes a humanoid, the ship shoots it and
 *     catches the humanoid; then each enemy appears, is shot and lines up
 *     with its name ($C64F prints the names up to `<$A196`); then the
 *     logo ($C677).
 *   $C677 the logo: "WILLIAMS" drawn stroke by stroke from bank 1's
 *     $C941 ($C6B0, `<$A16A` bytes a pass, 2 bits a move, $FE x y a jump,
 *     $FF nothing, others the end; then over again 10 a pass), "PRESENTS"
 *     ($C730), the DEFENDER logo flying in: 15 blocks of 4 x $C bytes at
 *     $B304 appearing ($C754; links left 0: only the last is listed),
 *     drawn whole at $3090 once they are in ($C848), the copyright's
 *     bitmap from $CC11 at $3BD0 ($C7D4); after $3C x $0A ticks the hall
 *     of fame.
 *   $C8F4 every 16 ticks: "CREDITS" and their number, when there are some
 *     (`<$A184` + 1 when they rise).
 */
#ifndef DEFENDER_ATTRACT_H
#define DEFENDER_ATTRACT_H

#include "sim/game.h"

#define ATTRACT 0xD825
#define GAME_OVER_DONE 0xDB48

/* 1 if bank 1's step at `at` is ported. */
int attract_ported(uint16_t at);
/* Run the process at `proc` from bank 1's `at` (bank 1 selected). */
StepResult attract_step(Game *g, uint16_t proc, uint16_t at);

#endif
