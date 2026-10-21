/*
 * Ported routines against the ROM's own, on chosen inputs (label rom):
 *   test_routines <defender.zip|folder>
 * The board is booted into a game; then for each case the RAM is set up,
 * the ROM routine runs on the emulated CPU (interrupts masked) until it
 * returns, and the C routine runs on a model of the same RAM: all of RAM
 * (bar the stack page) must then be equal.
 *   - score_add ($D360): random scores (BCD, a third of them small), points (BCD words, the pair
 *     and digit bits), players and extra-life intervals; cases that earn an
 *     extra life (not ported) are counted and skipped
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/rom.h"
#include "emu/board.h"
#include "sim/game.h"
#include "sim/score.h"

#define PRIME_FRAMES 1800
#define STACK 0xBF00
#define RETURN 0x0000   /* a return address the routines never reach */

static RomSet rom;
static Board board;
static uint8_t data[0x10000];
static Game model;
static int failures;
static uint32_t seed = 12345;

static unsigned rnd(unsigned n)
{
    seed = seed * 1103515245u + 12345u;
    return (seed >> 16) % n;
}

static uint8_t bcd(void) { return (uint8_t)(rnd(10) << 4 | rnd(10)); }

/* Run the ROM routine at `pc` with D = `d` until it returns. */
static int rom_call(uint16_t pc, uint16_t d)
{
    M6809 *c = &board.cpu;
    c->cc |= CC_I | CC_F;
    c->irq = c->firq = 0;
    c->s = 0xBF80;
    c->s -= 2;
    board.ram[c->s] = RETURN >> 8;
    board.ram[c->s + 1] = RETURN & 0xFF;
    uint16_t s0 = (uint16_t)(c->s + 2);
    c->a = (uint8_t)(d >> 8);
    c->b = (uint8_t)d;
    c->pc = pc;
    for (long n = 0; n < 1000000; n++) {
        if (c->pc == RETURN && c->s == s0) return 0;
        m6809_step(c);
    }
    return -1;
}

static void load_model(void)
{
    for (unsigned a = 0; a < STACK; a++) game_poke(&model, (uint16_t)a, board.ram[a]);
}

static long compare(const char *what, long n)
{
    for (unsigned a = 0; a < STACK; a++)
        if (game_peek(&model, (uint16_t)a) != board.ram[a]) {
            if (failures++ < 8)
                fprintf(stderr, "test_routines: %s case %ld: $%04X = %02X, C %02X\n", what, n, a, board.ram[a],
                        game_peek(&model, (uint16_t)a));
            return a;
        }
    return -1;
}

static void test_score(int cases)
{
    long done = 0, extra = 0;
    for (int n = 0; n < cases; n++) {
        board.ram[0xA08B] = (uint8_t)(1 + rnd(2));
        uint16_t player = board.ram[0xA08B] == 1 ? PLAYER_ONE : PLAYER_TWO;
        for (int i = 0; i < 7; i++) board.ram[player + i] = bcd();
        board.ram[player] = (uint8_t)rnd(2) ? 0 : board.ram[player];
        if (!rnd(3)) board.ram[player] = board.ram[player + 1] = board.ram[player + 2] = 0; /* leading zeros */
        if (!rnd(6)) board.ram[player + 3] = 0;
        if (rnd(4)) {
            board.ram[0xA0AB] = bcd();
            board.ram[0xA0AC] = bcd();
        } else {
            board.ram[0xA0AB] = board.ram[0xA0AC] = 0;
        }
        board.ram[0xA08A] = (uint8_t)rnd(256);
        uint16_t points = (uint16_t)((rnd(4) << 9 | rnd(2) << 8) | bcd());
        load_model();
        if (score_add(&model, points)) {
            extra++;
            continue;
        }
        if (rom_call(0xD360, points)) {
            fprintf(stderr, "test_routines: $D360 did not return\n");
            failures++;
            return;
        }
        compare("score_add", n);
        done++;
    }
    printf("test_routines: score_add: %ld cases equal the ROM's, %ld extra lives skipped\n", done, extra);
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_routines <defender.zip|folder>\n");
        return 2;
    }
    char err[512];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "test_routines: %s\n", err);
        return 1;
    }
    for (unsigned a = 0xD000; a <= 0xFFFF; a++) data[a] = (uint8_t)rom_byte(&rom, a);
    for (unsigned a = 0xC000; a < 0xD000; a++) data[a] = (uint8_t)rom_bank_byte(&rom, 2, a - 0xC000);
    model.data = data;
    static uint8_t cmos[BOARD_CMOS_SIZE];
    board_init(&board, &rom, NULL);
    for (int i = 0; i < PRIME_FRAMES; i++) board_run_frame(&board, NULL);
    memcpy(cmos, board.cmos, sizeof cmos);
    board_init(&board, &rom, cmos);
    for (long f = 1; f <= 1700; f++) { /* into a game */
        board.in2 = f > 1200 && f <= 1215 ? IN2_COIN1 : 0;
        board.in0 = f > 1300 && f <= 1315 ? IN0_START1 : 0;
        board_run_frame(&board, NULL);
    }
    test_score(5000);
    if (failures) {
        fprintf(stderr, "test_routines: %d differences\n", failures);
        return 1;
    }
    printf("test_routines: OK\n");
    return 0;
}
