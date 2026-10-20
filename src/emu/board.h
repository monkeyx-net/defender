/*
 * The Defender main board (Williams first generation, before the blitter):
 * a 1 MHz 6809, 48 KB RAM ($0000-$BFFF, the first 38 KB shown on screen),
 * fixed program ROM at $D000-$FFFF and a 4 KB window at $C000-$CFFF onto the
 * bank last written to $D000-$DFFF (rom.h). Bank 0 is the I/O page: palette
 * $C000-$C00F (mirrored, with the video control at $C010-$C01F), 256
 * nibbles of CMOS at $C400 (mirrored to $C7FF), the video counter at $C800,
 * PIAs at $CC00 (coins, interrupts, sound commands) and $CC04 (controls),
 * both mirrored every $20 bytes to $CFFF.
 *
 * A frame is 260 lines of 64 CPU cycles, from line 247 (vertical blank) to
 * line 246. VA11 (scanline bit 5) drives CB1 of the second PIA, interrupting
 * 4 times a frame; COUNT240 drives its CA1. Each visible line is drawn into
 * the framebuffer as the beam passes it. Instruction for instruction and
 * cycle for cycle this matches MAME (the mame_emu test). The sound board
 * (emu/sound.c) runs alongside, line by line; a frame's audio is
 * BOARD_AUDIO_RATE / BOARD_AUDIO_FPS samples.
 */
#ifndef DEFENDER_BOARD_H
#define DEFENDER_BOARD_H

#include <stdint.h>

#include "assets/rom.h"
#include "emu/m6809.h"
#include "emu/pia6821.h"
#include "emu/sound.h"
#include "gfx/fb.h"
#include "gfx/williams.h"

#define BOARD_LINES 260
#define BOARD_CYCLES_PER_LINE 64
#define BOARD_FIRST_LINE 247 /* frames start at vertical blank, as MAME's do */
#define BOARD_FRAME_HZ (1e6 / (BOARD_LINES * BOARD_CYCLES_PER_LINE)) /* 60.096 */
#define BOARD_AUDIO_RATE 48000
#define BOARD_AUDIO_FPS 60 /* the sound is made for this many frames a second (the port's tick) */
#define BOARD_CMOS_SIZE 256

/* Inputs, active high, as MAME's ports */
#define IN0_FIRE 0x01
#define IN0_THRUST 0x02
#define IN0_SMART_BOMB 0x04
#define IN0_HYPERSPACE 0x08
#define IN0_START2 0x10
#define IN0_START1 0x20
#define IN0_REVERSE 0x40
#define IN0_DOWN 0x80
#define IN1_UP 0x01
#define IN2_AUTO_UP 0x01
#define IN2_ADVANCE 0x02
#define IN2_COIN3 0x04
#define IN2_HIGH_SCORE_RESET 0x08
#define IN2_COIN1 0x10
#define IN2_COIN2 0x20
#define IN2_TILT 0x40

typedef struct Board Board;

struct Board {
    M6809 cpu;
    const RomSet *rom;
    uint8_t ram[WILLIAMS_VRAM_SIZE];
    uint8_t palette[16];
    uint8_t cmos[BOARD_CMOS_SIZE];
    int cmos_dirty;
    uint8_t bank;            /* $D000: what $C000-$CFFF shows (0 = I/O) */
    Pia pia[2];              /* 0 at $CC04 (controls), 1 at $CC00 */
    int scanline;
    int64_t frame;
    uint8_t in0, in1, in2;
    /* the sound board; its samples for the last frame are in sound.out */
    Sound sound;
    Framebuffer *fb;         /* during board_run_frame: where lines are drawn, or NULL */
    int next_line, next_drawn;
    int64_t next_line_start;
    int sound_pending;       /* a command written this instruction, sent when it ends */
    uint8_t sound_latch;
    /* optional: called on every CPU write (tests trace RAM through this) */
    void (*write_hook)(Board *b, uint16_t addr, uint8_t value);
    /* optional: called at the start of each line (scripted input timing) */
    void (*line_hook)(Board *b, int line);
    /* optional: called before each CPU step (instruction traces) */
    void (*step_hook)(Board *b);
    void *user;
};

/* Power on; cmos = saved CMOS (BOARD_CMOS_SIZE bytes) or NULL for a blank one. */
void board_init(Board *b, const RomSet *rom, const uint8_t *cmos);
/* Run one frame; draws the visible lines into fb (scale 1) if not NULL. */
void board_run_frame(Board *b, Framebuffer *fb);

#endif
