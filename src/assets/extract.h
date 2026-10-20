/*
 * Extractor: reads the user's ROM set once and writes what the game draws
 * and plays under <out>/ (DEFENDER_PORT_PLAN.md, decision 1). After that the
 * game needs only <out>/, never the ROM. Nothing here is compiled in: the
 * tables below name ROM addresses, the data comes from the player's set.
 *
 *   gfx/pictures.txt  the object pictures, from the descriptor table at
 *                     $F8CE-$F9F0 (width bytes, height, even-x data, odd-x
 *                     data; data at $C000-$CFFF is in bank 2)
 *   gfx/font.txt      the 45 glyphs of the game's font, descriptors at bank 2
 *                     $C5E3 (width bytes, height, data)
 *   gfx/logos.txt     the Williams and DEFENDER logos, as the attract mode
 *                     draws them (the emulated board, frame LOGO_FRAME)
 *   gfx/palette.txt   the palette in play
 *   gfx/<name>.png    previews of the above (not read by the game)
 *   sounds/NN.wav     each sound-board command NN (hex, $00-$1E), recorded
 *                     from the emulated sound board, 48 kHz mono 16-bit
 *   sounds/sounds.txt per command: once, loop (still playing after
 *                     SOUND_MAX_SECONDS; the WAV holds its first seconds) or
 *                     none (no output on its own)
 *   sounds/scripts.txt  the game's 26 sound scripts, $D4AB-$D538
 *                     (priority; count, ticks and sound per entry;
 *                     sim/sounds.h)
 *   manifest.txt      written last: the folder is complete
 *
 * Pictures and glyphs are rows of hex digits, one palette index per pixel.
 */
#ifndef DEFENDER_EXTRACT_H
#define DEFENDER_EXTRACT_H

#include "assets/rom.h"

#define EXTRACT_MANIFEST "manifest.txt"
#define EXTRACT_VERSION 2

/* Write everything to out_dir (created if needed). Returns 0, or -1 with a
   message in err; without manifest.txt the folder counts as missing. */
int extract_all(const RomSet *rom, const char *out_dir, char *err, int errlen);

#endif
