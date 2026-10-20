/*
 * The game's side of the sound: a port of the sound sequencer. $D54D
 * starts a script, $D568 (in the interrupt, once a frame) steps it and,
 * with no script playing, starts and stops the thrust sound; $D539 sends a
 * sound to the board.
 *
 * A script is a priority byte, then entries of (repeat count, ticks,
 * sound), ending with a 0 count (the 26 scripts at $D4AB-$D538, extracted
 * to sounds/scripts.txt). One starts only at or above the playing
 * script's priority; each entry sends its sound, repeats it `count` times
 * `ticks` apart, then the next entry follows. The game's sound numbers
 * are sent inverted: the board plays command ~sound & $1F (the WAV
 * sounds/NN.wav of the extractor).
 */
#ifndef DEFENDER_SOUNDS_H
#define DEFENDER_SOUNDS_H

#include <stdint.h>

#define SOUND_THRUST_ON 0x16
#define SOUND_THRUST_OFF 0x0F

typedef struct {
    const uint8_t *script; /* <$B0 is script + pos, the current entry less 1 */
    int pos;
    uint8_t priority;      /* <$B2: the playing script's, 0 for none */
    uint8_t timer;         /* <$B3: ticks to the next step */
    uint8_t repeat;        /* <$B4 */
    uint8_t thrust;        /* <$AD: the thrust sound is on */
} Sounds;

/* $D54D: start a script (priority byte first) if it outranks the playing one. */
void sounds_start(Sounds *s, const uint8_t *script);

/* $D568: one frame. `in0` is <$7B, `state` <$BA. Returns the sound to
   send ($D539), or -1. */
int sounds_tick(Sounds *s, uint8_t in0, uint8_t state);

/* The sound board's command for a game sound number. */
static inline int sounds_command(int sound) { return ~sound & 0x1F; }

#endif
