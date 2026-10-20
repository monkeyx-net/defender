/*
 * The C game against the emulated board (label rom):  test_sim <defender.zip|folder>
 * Plays a scripted game on the board (coin, start, then every control in
 * turn for a minute) and checks the ported routines as the ROM runs them:
 *   - rng.c: at every call of $D711, <$DF-<$E1 equal the C generator's,
 *     which then steps
 *   - ship.c: at every call of $E263, the ship and scroll state equals the
 *     C model's, which then steps with the controls and facing in RAM
 * Code not yet ported also writes this state (a new ship, hyperspace): such
 * writes resynchronise the model from RAM, and are counted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/rom.h"
#include "emu/board.h"
#include "sim/rng.h"
#include "sim/ship.h"

#define PRIME_FRAMES 1800
#define FRAMES 5200
#define RNG_CALL 0xD711
#define RNG_RETURN 0xD72E /* PULS B,PC */
#define RNG_END 0xD730
#define SHIP_CALL 0xE263
#define SHIP_END 0xE376
#define BA_NO_SCROLL 0x40

static RomSet rom;
static Board board;
static uint16_t pc; /* the instruction running */
static Rng rng;
static Ship ship;
static int rng_sync = 1, ship_sync = 1, failures;
static int rng_depth;
static uint16_t rng_stack[8]; /* S at each call in progress */
static long rng_checks, rng_resyncs, rng_nested, ship_checks, ship_resyncs;

static uint8_t *ram(unsigned a) { return &board.ram[a]; }
static uint16_t word(unsigned a) { return (uint16_t)(board.ram[a] << 8 | board.ram[a + 1]); }

static void ship_from_ram(Ship *s)
{
    uint32_t v = (uint32_t)*ram(0xA0C7) << 16 | (uint32_t)word(0xA0C8);
    s->velocity = (v & 0x800000) ? (int32_t)v - 0x1000000 : (int32_t)v;
    s->scroll = word(0xA020);
    s->last_scroll = word(0xA022);
    s->screen_x = word(0xA0C3);
    s->slide = (int16_t)word(0xA095);
    s->world_x = word(0xA0CC);
    s->y = word(0xA0C5);
    s->vy = (int16_t)word(0xA0CA);
}

static int ship_equal(const Ship *a, const Ship *b)
{
    return a->velocity == b->velocity && a->scroll == b->scroll && a->last_scroll == b->last_scroll &&
           a->screen_x == b->screen_x && a->slide == b->slide && a->world_x == b->world_x && a->y == b->y &&
           a->vy == b->vy;
}

static void report_ship(const Ship *want, const Ship *got)
{
    fprintf(stderr,
            "test_sim: frame %lld: ship differs (board / C):\n"
            "  velocity %06X / %06X  scroll %04X / %04X  last %04X / %04X  screen x %04X / %04X\n"
            "  slide %04X / %04X  world x %04X / %04X  y %04X / %04X  vy %04X / %04X\n",
            (long long)board.frame, (unsigned)want->velocity & 0xFFFFFF, (unsigned)got->velocity & 0xFFFFFF,
            want->scroll, got->scroll, want->last_scroll, got->last_scroll, want->screen_x, got->screen_x,
            (uint16_t)want->slide, (uint16_t)got->slide, want->world_x, got->world_x, want->y, got->y,
            (uint16_t)want->vy, (uint16_t)got->vy);
}

static void step_hook(Board *b)
{
    pc = b->cpu.pc;
    /* The hook also runs before an instruction the CPU then puts off to take
       an interrupt; it runs again when the instruction does. The stack
       pointer tells the two apart. */
    uint16_t sp = b->cpu.s;
    if (pc == RNG_RETURN && rng_depth > 0 && sp == (uint16_t)(rng_stack[rng_depth - 1] - 1)) rng_depth--;
    if (pc == RNG_CALL) {
        if (rng_depth > 0 && rng_stack[rng_depth - 1] == sp) return; /* seen already */
        /* the interrupt can call it while the main loop is part way through:
           both calls then work on the same bytes, so start afresh after it */
        if (rng_depth > 0) {
            rng_sync = 1;
            rng_nested++;
        }
        if (rng_depth < (int)(sizeof rng_stack / sizeof rng_stack[0])) rng_stack[rng_depth++] = sp;
        if (!rng_sync) {
            rng_checks++;
            if (rng.value != *ram(0xA0DF) || rng.hi != *ram(0xA0E0) || rng.lo != *ram(0xA0E1)) {
                if (failures++ < 5)
                    fprintf(stderr, "test_sim: frame %lld: random state %02X%02X%02X, C %02X%02X%02X\n",
                            (long long)b->frame, *ram(0xA0DF), *ram(0xA0E0), *ram(0xA0E1), rng.value, rng.hi,
                            rng.lo);
            }
        }
        rng.value = *ram(0xA0DF);
        rng.hi = *ram(0xA0E0);
        rng.lo = *ram(0xA0E1);
        rng_sync = 0;
        rng_step(&rng);
    }
    if (pc == SHIP_CALL) {
        Ship now;
        ship_from_ram(&now);
        if (!ship_sync) {
            ship_checks++;
            if (!ship_equal(&now, &ship) && failures++ < 5) report_ship(&now, &ship);
        }
        ship = now; /* from RAM, so one difference is reported once */
        ship_sync = 0;
        if (!(*ram(0xA0BA) & BA_NO_SCROLL)) {
            ShipInput in = { *ram(0xA07B), *ram(0xA07D), (int16_t)word(0xA0BD) };
            ship_step(&ship, &in);
        }
    }
}

static void write_hook(Board *b, uint16_t a, uint8_t v)
{
    (void)b;
    (void)v;
    if (a >= 0xA0DF && a <= 0xA0E1 && !(pc >= RNG_CALL && pc < RNG_END) && !rng_sync) {
        rng_sync = 1;
        rng_resyncs++;
    }
    int ship_ram = a == 0xA020 || a == 0xA021 || a == 0xA022 || a == 0xA023 || a == 0xA095 || a == 0xA096 ||
                   (a >= 0xA0C3 && a <= 0xA0CD);
    if (ship_ram && !(pc >= SHIP_CALL && pc < SHIP_END) && !ship_sync) {
        ship_sync = 1;
        ship_resyncs++;
    }
}

/* Coin and start, then each control in turn (as src/tests/mame/script.cmake). */
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
    long start = 1400;
    for (int k = 0;; k++) {
        int m = k % (int)(sizeof moves / sizeof moves[0]);
        int hold = moves[m].hold ? moves[m].hold : 20 + (k * 7) % 25;
        if (f < start + hold) {
            *in0 = moves[m].in0;
            *in1 = moves[m].in1;
            return;
        }
        if (f < start + hold + 6) return;
        start += hold + 6;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_sim <defender.zip|folder>\n");
        return 2;
    }
    char err[512];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "test_sim: %s\n", err);
        return 1;
    }
    static uint8_t cmos[BOARD_CMOS_SIZE];
    board_init(&board, &rom, NULL);
    for (int i = 0; i < PRIME_FRAMES; i++) board_run_frame(&board, NULL);
    memcpy(cmos, board.cmos, sizeof cmos);
    board_init(&board, &rom, cmos);
    board.step_hook = step_hook;
    board.write_hook = write_hook;
    for (long f = 1; f <= FRAMES; f++) {
        controls(f, &board.in0, &board.in1, &board.in2);
        board_run_frame(&board, NULL);
    }
    printf("test_sim: random %ld calls checked (%ld resyncs, %ld nested), ship %ld frames checked (%ld resyncs)\n",
           rng_checks, rng_resyncs, rng_nested, ship_checks, ship_resyncs);
    if (rng_checks < 1000 || ship_checks < 1000) {
        fprintf(stderr, "test_sim: too few checks\n");
        return 1;
    }
    if (failures) {
        fprintf(stderr, "test_sim: %d differences\n", failures);
        return 1;
    }
    printf("test_sim: OK\n");
    return 0;
}
