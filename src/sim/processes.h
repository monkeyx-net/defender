/*
 * The game's processes (Williams' cooperative multitasking): a port of the
 * process pool and its routines, in the ROM's layout and values like
 * objects.h. A process is a routine that runs a little each tick and
 * sleeps: its block holds where it resumes and for how many ticks it
 * sleeps.
 *
 * Blocks: 75 of 15 bytes at $AAC5 and 5 large ones of 23 bytes at $AF2A
 * ($D7C6 sets them up): +0 link, +2 resume address, +4 ticks to sleep,
 * +5 type, +6 large, then the process's own bytes. The running list's head
 * is the word at $A05F (a pseudo block: the dispatcher starts there),
 * `<$63` the current process, `<$61` and `<$69` the free blocks.
 * $D055 creates a process (X = address, A = type) right after the current
 * one, so it runs in the same pass; $D03E a large one. The dispatcher
 * ($E84D) walks the list each tick, counts every block's +4 down and runs
 * those at 0 (`JMP [2,U]`). A process sleeps with $D001 (A ticks, X =
 * where to resume) or $D000 (A - 1 ticks), ends with $D00A, and $D015
 * kills another (unlinks it and frees its block).
 */
#ifndef DEFENDER_PROCESSES_H
#define DEFENDER_PROCESSES_H

#include <stdint.h>

#define PROCESS_HEAD 0xA05F      /* the list's head: a pseudo block's +0 */
#define PROCESS_CURRENT 0xA063
#define PROCESS_FREE 0xA061
#define PROCESS_FREE_LARGE 0xA069
#define PROCESSES_FIRST 0xAAC5
#define PROCESS_SIZE 15
#define PROCESSES 75
#define LARGE_FIRST 0xAF2A
#define LARGE_SIZE 23
#define LARGES 5

typedef struct {
    uint16_t link;      /* +0 */
    uint16_t resume;    /* +2 the ROM address it resumes at */
    uint8_t sleep;      /* +4 */
    uint8_t type;       /* +5 */
    uint8_t large;      /* +6 */
    uint8_t data[16];   /* +7: 8 bytes, 16 in a large block */
} Process;

typedef struct {
    Process small[PROCESSES], large[LARGES];
    uint16_t head, current, free_small, free_large;
} Processes;

Process *processes_at(Processes *p, uint16_t addr);

/* $D7C6: every block free, in address order; nothing running; the
   current process is the list's head (only the links change). */
void processes_init(Processes *p);
/* $D055 / $D03E: a process of `type` resuming at `resume`, after the
   current one. Returns its block, or 0 if none is free (the ROM stops
   with an error). */
uint16_t processes_create(Processes *p, uint16_t resume, uint8_t type, int large);
/* $D001: the current process sleeps `ticks` and resumes at `resume`. */
void processes_sleep(Processes *p, uint8_t ticks, uint16_t resume);
/* $D015: kill a process; returns the block before it in the list (the
   dispatcher carries on from there), or 0 if it is not running. */
uint16_t processes_kill(Processes *p, uint16_t addr);
/* $E84D: count the block's sleep down; 1 if it runs now (it becomes
   current). */
int processes_tick(Processes *p, uint16_t addr);

int processes_peek(const Processes *p, uint16_t addr);
void processes_poke(Processes *p, uint16_t addr, uint8_t value);

#endif
