/*
 * The user's Defender ROM set (MAME `defender`, red label), read from a
 * folder or a zip. Files are matched by CRC-32, so any naming works (MAME's
 * current names, older sets, FBNeo).
 *
 * CPU map of the program ROMs: $D000-$FFFF is fixed (12 KB); $C000-$CFFF
 * shows the bank picked by a write to $D000-$DFFF: bank 0 is the I/O page,
 * banks 1-3 and 7 are ROM, 4-6 and 8-15 are empty and read as 0 (as in
 * MAME). The sound board's 6808 runs the 2 KB sound ROM at $F800. The two
 * decoder PROMs are not needed.
 */
#ifndef DEFENDER_ROM_H
#define DEFENDER_ROM_H

#include <stddef.h>
#include <stdint.h>

#define ROM_FIXED_SIZE 0x3000 /* $D000-$FFFF */
#define ROM_BANKS 7           /* banks 1-7 at $C000 */
#define ROM_BANK_SIZE 0x1000
#define ROM_SOUND_SIZE 0x800  /* $F800-$FFFF on the sound board */

typedef struct {
    uint8_t fixed[ROM_FIXED_SIZE];
    uint8_t bank[ROM_BANKS][ROM_BANK_SIZE];
    uint8_t sound[ROM_SOUND_SIZE];
    int loaded;
} RomSet;

/* Load the program ROMs and the sound ROM from a folder or a zip. Returns 0,
   or -1 with a message in err. */
int rom_load(RomSet *rom, const char *src, char *err, int errlen);
/* Byte at a main-CPU address in the fixed ROM ($D000-$FFFF); -1 elsewhere. */
int rom_byte(const RomSet *rom, unsigned addr);
/* Byte at $C000 + offset with `bank` (0-15) selected; -1 for bank 0 (I/O). */
int rom_bank_byte(const RomSet *rom, int bank, unsigned offset);

/* Where the ROM set is: $DEFENDER_ROM, else defender.zip or a defender/ folder next to
   the program or in the current folder (development builds: also in the
   source tree's roms/). Returns 0 if found. */
int rom_locate(const char *argv0, char *out, size_t outlen);

#endif
