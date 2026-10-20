/*
 * Williams first-generation video as Defender uses it (checked against MAME
 * by the mame_capture test):
 *   video RAM $0000-$97FF: 152 columns of 256 bytes; byte (x/2)*256 + y holds
 *     pixels x (high nibble) and x+1 (low nibble), 4 bits per pixel (the rest
 *     of RAM, to $BFFF, is the game's work RAM in the same layout)
 *   palette $C000-$C00F (I/O page): one byte per entry, BBGGGRRR through
 *     resistor networks (R, G: 1200/560/330 ohms; B: 560/330 ohms)
 *   visible area: x 12..303, y 7..246 (292 x 240, the framebuffer)
 * Defender has no blitter: the CPU draws everything.
 */
#ifndef DEFENDER_WILLIAMS_H
#define DEFENDER_WILLIAMS_H

#include <stdint.h>
#include "gfx/fb.h"

#define WILLIAMS_VRAM_SIZE 0xC000
#define WILLIAMS_SCREEN_SIZE 0x9800 /* the columns the beam shows */
#define WILLIAMS_VISIBLE_X 12
#define WILLIAMS_VISIBLE_Y 7

/* A palette byte as 0xRRGGBB. */
uint32_t williams_rgb(uint8_t entry);
/* The visible part of video RAM through the palette into fb (any scale). */
void williams_draw_vram(Framebuffer *fb, const uint8_t *vram, const uint8_t palette[16]);

#endif
