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
    /* optional, for tests: called before each raw byte write (not the typed pools' fields) */
    void (*on_write)(void *user, uint16_t addr);
    void *user;
} Game;

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
/* $F3FE: erase an object where it was drawn (its picture's erase: w x h bytes). */
void game_erase_object(Game *g, uint16_t obj);
/* $F3FB: free an object ($D0C7) and erase it. 0, or -1 if it is on no list. */
int game_kill_object(Game *g, uint16_t obj);
/* $D00A: the current process ends. */
void game_end_process(Game *g);

typedef enum {
    STEP_UNPORTED = 0, /* the process (or the path it took) is not ported: the state is unspecified */
    STEP_DONE = 1,     /* it slept or ended */
} StepResult;

/* 1 if the process at `proc` resumes at a ported step. */
int game_step_ported(const Game *g, uint16_t proc);
/* Run the process at `proc` from its resume address until it yields. */
StepResult game_step(Game *g, uint16_t proc);

#endif
