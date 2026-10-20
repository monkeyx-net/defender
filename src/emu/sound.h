/*
 * The Williams sound board, as MAME's williams.cpp models it: a 6808 at
 * 3.579545 MHz / 4 running the 2 KB sound ROM ($F800-$FFFF), 128 bytes of RAM, a
 * PIA at $0400 whose port B takes the main board's command byte (CB1 high,
 * and so an interrupt, unless the command is $FF) and whose port A drives an
 * 8-bit DAC. The DAC level is averaged over each output sample.
 */
#ifndef DEFENDER_SOUND_H
#define DEFENDER_SOUND_H

#include <stdint.h>

#include "assets/rom.h"
#include "emu/m6800.h"
#include "emu/pia6821.h"

#define SOUND_CPU_HZ 894886.25
#define SOUND_MAX_SAMPLES 2048

typedef struct {
    M6800 cpu;
    const RomSet *rom;
    uint8_t ram[128];
    Pia pia;
    uint8_t command;          /* the main board's latch */
    uint8_t dac;
    /* audio out: mono samples, filled as the CPU runs */
    double cycles_per_sample;
    double sample_left;       /* sound cycles until the current sample is complete */
    double acc;               /* DAC level x cycles in the current sample */
    double dc_in, dc_out;     /* DC blocker state */
    int16_t out[SOUND_MAX_SAMPLES];
    int out_len;
    /* optional: called before each CPU step (instruction traces) */
    void (*step_hook)(void *user, const M6800 *cpu);
    void *user;
} Sound;

/* cycles_per_sample: sound CPU cycles per output sample (the board picks it
   so a frame's sound fills one tick of the port's audio). */
void sound_init(Sound *s, const RomSet *rom, double cycles_per_sample);
void sound_command(Sound *s, uint8_t cmd);
/* Run the sound CPU until its cycle count reaches `until`. */
void sound_run(Sound *s, int64_t until);

#endif
