/*
 * The C game's state: the arcade's RAM, as the ROM lays it out. The object
 * and process pools are typed (objects.h, processes.h); every other byte is
 * still raw (`ram`), named in the code by its address until it gets a
 * type of its own. `data` is the read-only data the ported routines use,
 * at the ROM addresses the assets record (sound scripts, pictures); the
 * game fills it from assets/, the tests from the ROM set.
 *
 * Process steps: game_step() runs the C version of the process whose block
 * is `proc` from where it resumes, until it sleeps or ends, as the ROM's
 * dispatcher would ($E853 `JMP [2,U]`).
 */
#ifndef DEFENDER_GAME_H
#define DEFENDER_GAME_H

#include <stdint.h>

#include "sim/objects.h"
#include "sim/processes.h"

#define GAME_RAM 0xC000

typedef struct {
    Objects objects;
    Processes processes;
    uint8_t ram[GAME_RAM];  /* bytes outside the pools */
    const uint8_t *data;    /* 64 KB, indexed by ROM address: the fixed ROM, bank 2 at $C000 */
    const uint8_t *bank[8]; /* 4 KB each at $C000 (banks 1, 2, 3, 7), for the tables in them */
    int sound_sent;         /* the last sound sent to the board ($D539), -1 for none */
    /* the controls, as the PIAs give them: IN0 ($CC04), IN1 ($CC06), IN2
       ($CC00) and the coin PIA's control register A ($CC01: bit 6, CA2) */
    uint8_t in0, in1, in2, cra;
    uint8_t palette[16];    /* the hardware palette ($C000-$C00F), copied at the frame's top */
    uint8_t cmos[256];      /* $C400-$C4FF: nibbles, | $F0 as the board keeps them */
    uint16_t resume_from;   /* where the dispatcher carries on after a step */
    /* the bottom half's band split: the ROM re-reads the beam after the
       sounds, the ship's motion and the stars (so it depends on their
       cycles); -1 for the usual line, or the line a test saw */
    int split;
    uint16_t unported;      /* the resume address of the last step not ported */
    uint16_t terrain_u;     /* U as the terrain code last left it (a cursor's byte) */
    /* where the bottom half breaks into the main loop's tick: -1 after it
       (the C game's order), or before the tick's write number n below
       $A000 (as a test saw the board do); tick_writes counts them */
    long bottom_at_write, tick_writes;
    int bottom_failed;
    /* optional, for tests: called before each raw byte write (not the typed pools' fields) */
    void (*on_write)(void *user, uint16_t addr);
    void *user;
} Game;

/* CMOS from the board's 256 nibbles. */
void game_load_cmos(Game *g, const uint8_t *cmos);
/* The whole state from a RAM image of $0000-$BFFF. */
void game_load(Game *g, const uint8_t *ram);
/* A byte as the main CPU reads it: RAM below $C000, `data` above. */
uint8_t game_read(const Game *g, uint16_t addr);
/* A byte of ROM bank `bank` at $C000-$CFFF (or the fixed ROM above). */
uint8_t game_read_bank(const Game *g, int bank, uint16_t addr);
/* A RAM byte or big-endian word, wherever it is kept. */
int game_peek(const Game *g, uint16_t addr);
void game_poke(Game *g, uint16_t addr, uint8_t value);
uint16_t game_word(const Game *g, uint16_t addr);
void game_set_word(Game *g, uint16_t addr, uint16_t value);

/* $D711 on <$DF-<$E1. */
uint8_t game_random(Game *g);
/* $DD9E: a random number 1..n+1, halving until it is at most n. */
uint8_t game_random_upto(Game *g, uint8_t n);
/* $D54D: start the sound script at `script`. */
void game_sound(Game *g, uint16_t script);

/* $ED59: the terrain's line under world x (the table at $B300, a line per 64 units). */
uint8_t game_terrain(const Game *g, uint16_t x);
/* $D539: send a sound number to the board (not RAM: kept for the game). */
void game_send_sound(Game *g, uint8_t sound);
/* A picture's draw routine (descriptor +6): its even-x data (+2) or, for
   an odd x, its odd-x data (+4), width x height bytes at `at`. The hidden
   picture's ($F8EC, routine $D8DB) draws nothing. The ROM's routines also
   keep the stack pointer at <$77-<$78 while they copy: not modelled. Past
   line $FF most fixed-size routines spill into the column before (see
   cell() in game.c). */
void game_draw_picture(Game *g, uint16_t picture, uint16_t at, int odd);
/* A picture's erase routine (+8) at `at`: zeros over width x height (the
   hidden picture's erases nothing). */
void game_erase_picture(Game *g, uint16_t picture, uint16_t at);
/* $F3FE: erase an object where it was drawn (its picture's erase: w x h bytes). */
void game_erase_object(Game *g, uint16_t obj);
/* $F3FB: free an object ($D0C7) and erase it. 0, or -1 if it is on no list. */
int game_kill_object(Game *g, uint16_t obj);
/* $D00A: the current process ends. */
void game_end_process(Game *g);

typedef enum {
    STEP_UNPORTED = 0, /* the process (or the path it took) is not ported: the state is unspecified */
    STEP_DONE = 1,     /* it slept or ended */
    STEP_RESTART = 2,  /* it restarted the main loop ($E7BE): this tick's pass ends */
} StepResult;

/* 1 if the process at `proc` resumes at a ported step. */
int game_step_ported(const Game *g, uint16_t proc);
/* Run the process at `proc` from its resume address until it yields. */
StepResult game_step(Game *g, uint16_t proc);

#endif
