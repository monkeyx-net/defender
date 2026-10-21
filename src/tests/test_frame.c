/*
 * The C game on its own against the board (label rom):
 *   test_frame <defender.zip|folder> [hunter]
 * The board plays a scripted game (as test_game). At a frame boundary
 * where the board's main loop is idle, the C game (sim/frame.h) is loaded
 * from RAM; then both play the same controls frame by frame and all of
 * RAM is compared after each (bar the stack page and the bytes where
 * routines keep S: <$13-<$14, <$77-<$78). Two things the board's timing
 * decides are given to the C game: the bottom half's band split (the beam
 * when it re-reads it) and where in the main loop's tick the bottom half
 * came (the C game otherwise runs it after the tick). A difference, or something the
 * C game does not have yet, ends the run; the next idle boundary starts a
 * new one. Reported: how many frames the C game ran, how long the runs
 * were, and why each ended. The runs are short: the bottom half lands in
 * the board's main loop by its cycles, which the C game does not count
 * (the writes below $A000 place it only roughly; the processes' logic is
 * checked exactly by test_game). At least 1000 frames must match: a guard
 * on the frame's assembly. DBG_ALL=1 (and DBG_FROM=<hex address>) lists
 * the bytes that differ.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/rom.h"
#include "emu/board.h"
#include "sim/boot.h"
#include "sim/frame.h"

#define PRIME_FRAMES 1800
#define FRAMES 5200
#define HUNTER_FRAMES 12000
#define ALONE_FRAMES 20000
#define STACK 0xBF00
#define IDLE_FIRST 0xE7C3 /* the main loop waiting for the tick */
#define IDLE_LAST 0xE7C6

static RomSet rom;
static Board board;
static uint8_t data[0x10000], banks[8][0x1000];
static Game game;
static int script;
/* where the board's main loop was when its bottom half began: the raw
   writes its tick had made (-1: none, it was idle) */
static int in_irq, ticking;
static uint16_t main_pc;
static long tick_writes, bottom_at;

static int raw(uint16_t a) { return a < 0xA000; } /* as sim/frame.c counts */

static void write_hook(Board *b, uint16_t a, uint8_t v)
{
    (void)b;
    (void)v;
    if (!in_irq && ticking && a < STACK && raw(a)) tick_writes++;
}

static Board boot_board; /* power-on, on a board of its own */
static int booted;        /* the ROM's power-on set-up done ($D7B3) */
static uint8_t boot_ram[0xC000], boot_cmos[BOARD_CMOS_SIZE];

static void boot_hook(Board *b)
{
    if (b->cpu.pc == 0xD7B3 && !booted) {
        booted = 1;
        memcpy(boot_ram, b->ram, sizeof boot_ram);
        memcpy(boot_cmos, b->cmos, sizeof boot_cmos);
    }
}

static void step_hook(Board *b)
{
    if ((b->cpu.irq && !(b->cpu.cc & CC_I)) || (b->cpu.firq && !(b->cpu.cc & CC_F))) return;
    uint16_t pc = b->cpu.pc;
    if (b->cpu.last_interrupt) in_irq = 1;
    if (!in_irq) {
        main_pc = pc;
        if (pc == 0xE7C7) {
            ticking = 1;
            tick_writes = 0;
        }
        if (pc >= IDLE_FIRST && pc <= IDLE_LAST) ticking = 0;
    }
    if (pc == 0xDF31) /* the bottom half's work */
        bottom_at = ticking ? tick_writes : -1;
    if (pc == 0xDFC1) in_irq = 0;
}
static uint16_t unported_at[16];
static long unported_n[16];

static void hunter(long f, uint8_t *in0, uint8_t *in1)
{
    *in0 = f % 8 < 4 ? IN0_FIRE : 0;
    if (f % 180 < 4) *in0 |= IN0_REVERSE;
    if (f % 360 < 60) *in0 |= IN0_THRUST;
    long phase = f % 240;
    if (phase < 100) *in1 = IN1_UP;
    else if (phase >= 120 && phase < 220) *in0 |= IN0_DOWN;
}

static void controls(long f, uint8_t *in0, uint8_t *in1, uint8_t *in2)
{
    static const struct { uint8_t in0, in1; int hold; } moves[] = {
        { IN0_THRUST | IN0_FIRE, 0, 0 }, { IN0_THRUST, IN1_UP, 0 }, { IN0_FIRE, 0, 0 },
        { IN0_DOWN | IN0_FIRE, 0, 0 },   { IN0_REVERSE, 0, 4 },     { IN0_THRUST, IN1_UP, 0 },
        { IN0_FIRE | IN0_DOWN, 0, 0 },   { IN0_SMART_BOMB, 0, 4 },  { IN0_THRUST, 0, 0 },
        { IN0_HYPERSPACE, 0, 4 },        { IN0_THRUST | IN0_FIRE, 0, 0 }, { 0, IN1_UP, 0 },
    };
    *in0 = *in1 = *in2 = 0;
    if (f >= 1200 && f < 1215) *in2 = IN2_COIN1;
    if (f >= 1300 && f < 1315) *in0 = IN0_START1;
    if (f < 1400) return;
    if (script) {
        hunter(f, in0, in1);
        return;
    }
    long start = 1400;
    for (int k = 0;; k++) {
        int m = k % (int)(sizeof moves / sizeof moves[0]);
        int hold = moves[m].hold ? moves[m].hold : 20 + (k * 7) % 25;
        if (f < start + hold) {
            *in0 = moves[m].in0;
            if (f % 8 >= 4) *in0 &= (uint8_t)~IN0_FIRE;
            *in1 = moves[m].in1;
            return;
        }
        if (f < start + hold + 6) return;
        start += hold + 6;
    }
}

static long first_difference(void)
{
    for (unsigned a = 0; a < STACK; a++) {
        if (a == 0xA013 || a == 0xA014 || a == 0xA077 || a == 0xA078) continue;
        if (game_peek(&game, (uint16_t)a) != board.ram[a]) return a;
    }
    return -1;
}

int main(int argc, char **argv)
{
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: test_frame <defender.zip|folder> [hunter]\n");
        return 2;
    }
    if (argc == 3 && !strcmp(argv[2], "hunter")) script = 1;
    char err[512];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "test_frame: %s\n", err);
        return 1;
    }
    for (unsigned a = 0xD000; a <= 0xFFFF; a++) data[a] = (uint8_t)rom_byte(&rom, a);
    for (unsigned a = 0xC000; a < 0xD000; a++) data[a] = (uint8_t)rom_bank_byte(&rom, 2, a - 0xC000);
    for (int b = 1; b <= 7; b++)
        for (unsigned a = 0; a < 0x1000; a++) banks[b][a] = (uint8_t)rom_bank_byte(&rom, b, a);
    game.data = data;
    for (int b = 1; b <= 7; b++) game.bank[b] = banks[b];
    static uint8_t cmos[BOARD_CMOS_SIZE];
    board_init(&board, &rom, NULL);
    for (int i = 0; i < PRIME_FRAMES; i++) board_run_frame(&board, NULL);
    memcpy(cmos, board.cmos, sizeof cmos);
    board_init(&board, &rom, cmos);
    board.step_hook = step_hook;
    board.write_hook = write_hook;

    /* power-on: the C set-up against the ROM's when its self-test is done */
    board_init(&boot_board, &rom, cmos);
    boot_board.step_hook = boot_hook;
    while (!booted) board_run_frame(&boot_board, NULL);
    game_load_cmos(&game, boot_cmos);
    game_power_on(&game);
    for (unsigned a = 0; a < STACK; a++)
        if (game_peek(&game, (uint16_t)a) != boot_ram[a]) {
            fprintf(stderr, "test_frame: power-on: $%04X = %02X, C %02X\n", a, boot_ram[a], game_peek(&game, (uint16_t)a));
            return 1;
        }
    printf("test_frame: power-on equals the ROM's (frame %lld)\n", (long long)boot_board.frame);

    /* the C game alone from power-on: the script's controls (frames counted
       from the board's power-on), past game over into the attract mode */
    long games = 0, alone_unported = 0;
    for (long f = boot_board.frame + 1, n = 0; n < ALONE_FRAMES; f++, n++) {
        controls(f, &game.in0, &game.in1, &game.in2);
        game.cra = 0;
        int was = !(game.ram[0xA0BA] & 0x80); /* in a game: <$BA positive */
        if (game_frame(&game) && alone_unported++ < 3)
            fprintf(stderr, "test_frame: alone, frame %ld: not ported (a step at $%04X)\n", f, game.unported);
        if (was && (game.ram[0xA0BA] & 0x80)) games++;
    }
    printf("test_frame: the C game alone: %d frames from power-on, %ld games played to the end, %ld frames not ported\n",
           ALONE_FRAMES, games, alone_unported);
    if (alone_unported || !games) {
        fprintf(stderr, "test_frame: the C game alone did not play through\n");
        return 1;
    }

    long frames = script ? HUNTER_FRAMES : FRAMES, c_frames = 0, runs = 0, longest = 0, run = 0;
    long ended_diff = 0, ended_unported = 0, shown = 0;
    int running = 0;
    for (long f = 1; f <= frames; f++) {
        controls(f, &board.in0, &board.in1, &board.in2);
        if (!running && f > 1400 && board.cpu.pc >= IDLE_FIRST && board.cpu.pc <= IDLE_LAST) {
            game_load(&game, board.ram);
            running = 1;
            run = 0;
            runs++;
        }
        uint8_t in0 = board.in0, in1 = board.in1, in2 = board.in2, cra = board.pia[1].a.cr;
        board_run_frame(&board, NULL);
        int ported = 1;
        if (running) {
            game.in0 = in0;
            game.in1 = in1;
            game.in2 = in2;
            game.cra = cra;
            game.split = board.ram[0xA0A2] >= 0xA8 ? 0xA8 : board.ram[0xA0A2]; /* the split the ROM took */
            game.unported = 0;
            game.bottom_at_write = bottom_at; /* where the ROM's came */
            ported = !game_frame(&game);
        }
        if (!running) continue;
        if (!ported) {
            ended_unported++;
            if (game.unported) {
                int i;
                for (i = 0; i < 16 && unported_at[i] && unported_at[i] != game.unported; i++) {}
                if (i < 16) {
                    unported_at[i] = game.unported;
                    unported_n[i]++;
                }
            }
            running = 0;
        } else {
            long d = first_difference();
            if (d < 0) {
                c_frames++;
                run++;
                if (run > longest) longest = run;
                continue;
            }
            ended_diff++;
            if (shown++ < 6)
                fprintf(stderr, "test_frame: frame %ld, after %ld frames of C: $%04lX = %02X, C %02X\n", f, run, d,
                        board.ram[d], game_peek(&game, (uint16_t)d));
            if (getenv("DBG_ALL") && shown <= 3) {
                fprintf(stderr, "  bottom at write %ld, split %d\n", bottom_at, game.split);
                int k = 0;
                for (unsigned a = (getenv("DBG_FROM") ? (unsigned)strtoul(getenv("DBG_FROM"), 0, 16) : 0); a < STACK && k < 30; a++)
                    if (a != 0xA013 && a != 0xA014 && a != 0xA077 && a != 0xA078 && game_peek(&game, (uint16_t)a) != board.ram[a]) {
                        fprintf(stderr, "  $%04X = %02X, C %02X\n", a, board.ram[a], game_peek(&game, (uint16_t)a));
                        k++;
                    }
            }
            running = 0;
        }
    }
    printf("test_frame: the C game ran %ld frames in %ld runs (longest %ld); runs ended by a difference %ld, by\n"
           "            something not ported %ld\n",
           c_frames, runs, longest, ended_diff, ended_unported);
    printf("test_frame: not ported, by resume address:");
    for (int i = 0; i < 16 && unported_at[i]; i++) printf(" $%04X %ld", unported_at[i], unported_n[i]);
    printf("\n");
    if (c_frames < 1000) {
        fprintf(stderr, "test_frame: too few frames in step\n");
        return 1;
    }
    printf("test_frame: OK\n");
    return 0;
}
