/*
 * Explosions: $FCCC starts one for an object. 16 slots of $40 bytes at
 * $9C00, taken round robin from the last (`<$E2`); a slot whose +0 is
 * negative is in use for an object appearing and is skipped, one still
 * exploding is erased first ($FDD5: it lists the screen bytes it drew, from
 * +$40 back to its +4). A slot: +0 $0100 (its size, growing), +2 the
 * picture, +4 the end of its list, +6 the centre, +8 the picture's
 * top left (column << 8 | line). The centre is the hit point (`<$F8`) when
 * that lies in the picture, else its middle. The main loop's $FD2D grows
 * and draws them.
 */
#ifndef DEFENDER_EXPLOSIONS_H
#define DEFENDER_EXPLOSIONS_H

#include <stdint.h>

#include "sim/game.h"

#define EXPLOSIONS 0x9C00
#define EXPLOSIONS_END 0xA000
#define EXPLOSION_SIZE 0x40
#define EXPLOSION_LAST 0xA0E2   /* <$E2 */
#define HIT_POINT 0xA0F8        /* <$F8 */
#define HIT_POINT_ADDR HIT_POINT

/* $FC69: an object appears: it is put at the head of the screen list with
   the hidden picture ($F8EC, [$FFDD]) while a slot (+0 $AF00) draws it
   coming together; off screen or with no slot free it shows at once.
   Script $D4EE unless `<$BA` has bit 7. The slot is the one after `<$E2`,
   which it does not move on. */
void appear_start(Game *g, uint16_t obj);
/* $FCCC: an explosion of object `obj` (not if off screen, or no slot). */
void explosion_start(Game *g, uint16_t obj);
/* $FDEF: draw the slot at `slot`: the picture's columns spread from the
   centre (+6) by its size (+0 & $7F) and its rows in pairs 2 x size lines
   apart, from the even data; every screen word it draws is listed down
   from +$40 (+4 the list's start). Columns past $98 and rows past line $FF
   are left out. Scratch: <$E4-<$F7. */
void explosion_draw(Game *g, uint16_t slot);
/* $FD2D, every main-loop tick: explosions grow ($AA a tick, gone past
   $30) and follow the scroll; objects appearing count down ($100 a tick)
   and follow their object; done (or off screen) the object gets its
   picture back; with `<$BA` bit 2 explosions stop (not erased) and
   appearances end. */
void explosions_update(Game *g);
/* $FDD5: erase what the slot at `slot` drew. */
void explosion_erase(Game *g, uint16_t slot);

#endif
