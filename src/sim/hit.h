/*
 * The hit test, $E6BA (and $E6BD with a list): does the picture at
 * descriptor `picture`, at screen position `at` ((column << 8) | line),
 * touch an object on the list? First by bounding boxes against each drawn
 * object (+4 not 0), then the two pictures' even data byte by byte where
 * they overlap. On a hit it leaves the hit byte's screen address in
 * `<$F8` and calls the object's hit handler (+8), then stops. The ROM's
 * scratch bytes <$73, <$CE-<$CF and <$D0-<$DD are set as it goes.
 */
#ifndef DEFENDER_HIT_H
#define DEFENDER_HIT_H

#include <stdint.h>

#include "sim/game.h"

/* 1 if it hit (the handler has run), 0 if not, -1 if the handler is not
   ported (the state is then unspecified). */
int hit_test(Game *g, uint16_t list, uint16_t at, uint16_t picture);

#endif
