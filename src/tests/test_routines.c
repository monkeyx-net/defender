/*
 * Ported routines against the ROM's own, on chosen inputs (label rom):
 *   test_routines <defender.zip|folder>
 * The board is booted into a game; then for each case the RAM is set up,
 * the ROM routine runs on the emulated CPU (interrupts masked) until it
 * returns, and the C routine runs on a model of the same RAM: all of RAM
 * (bar the stack page) must then be equal.
 *   - process steps from situations the scripted games rarely reach: the
 *     state is built in the model and copied to RAM, the ROM's step runs
 *     from its resume address until it is back at the dispatcher ($E856),
 *     the C step from the same state: a humanoid falling onto the terrain
 *     at various speeds ($F216), carried by the ship down to it ($F24C),
 *     the bonus that follows ($EE65, $EE73, $EEB2)
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
#include "sim/humanoids.h"
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

#define DISPATCH_NEXT 0xE856

static uint8_t game_ram[0xC000]; /* the board's RAM in a game, restored before each case */

/* Copy the model to the board's RAM. */
static void store_model(void)
{
    for (unsigned a = 0; a < STACK; a++) board.ram[a] = (uint8_t)game_peek(&model, (uint16_t)a);
}

/* Run the ROM's step of process `proc` from its resume address to the dispatcher. */
static int rom_step(uint16_t proc)
{
    M6809 *c = &board.cpu;
    c->cc |= CC_I | CC_F;
    c->irq = c->firq = 0;
    c->s = 0xBFFF;
    c->u = proc;
    c->dp = 0xA0;
    c->pc = (uint16_t)(board.ram[proc + 2] << 8 | board.ram[proc + 3]);
    for (long n = 0; n < 1000000; n++) {
        if (c->pc == DISPATCH_NEXT) return 0;
        m6809_step(c);
    }
    return -1;
}

/* A new process at `resume` in the model, current, with +7 = `arg`. */
static uint16_t new_process(uint16_t resume, uint16_t arg)
{
    uint16_t p = processes_create(&model.processes, resume, 0, 0);
    game_set_word(&model, (uint16_t)(p + 7), arg);
    model.processes.current = p;
    return p;
}

/* A humanoid from the table, or 0. */
static uint16_t some_humanoid(void)
{
    for (int tries = 0; tries < 64; tries++) {
        uint16_t h = game_word(&model, (uint16_t)(HUMANOIDS + 2 * rnd(32)));
        if (h) return h;
    }
    return 0;
}

static long steps_compared, steps_skipped;

static void step_case(const char *what, long n, uint16_t proc)
{
    store_model();
    if (rom_step(proc)) {
        fprintf(stderr, "test_routines: %s: the ROM's step did not return\n", what);
        failures++;
        return;
    }
    if (game_step(&model, proc) != STEP_DONE) {
        steps_skipped++;
        return;
    }
    compare(what, n);
    steps_compared++;
}

static void test_humanoid_steps(int cases)
{
    for (int n = 0; n < cases; n++) {
        memcpy(board.ram, game_ram, sizeof game_ram);
        load_model();
        uint16_t h = some_humanoid();
        Object *o = objects_at(&model.objects, h);
        if (!o) continue;
        int kind = (int)rnd(5);
        uint8_t ground = game_terrain(&model, o->x);
        if (kind == 0 || kind == 1) {
            /* falling: just above the terrain, at a speed around $E0 */
            o->y = (uint16_t)((uint8_t)(ground - rnd(3)) << 8 | rnd(256));
            o->vy = (uint16_t)(rnd(2) ? 0x00C0 + rnd(0x60) : 0x00D6 + rnd(6)); /* half at the $E0 edge */
            o->hit = HUMANOID_CARRIED;
            o->owner = new_process(HUMANOID_FALL, h);
            step_case("humanoid falling", n, o->owner);
        } else if (kind == 2) {
            /* under the ship, near the terrain */
            game_set_word(&model, 0xA0CC, (uint16_t)(o->x - 0x80 + rnd(0x100) - 0x80));
            model.ram[0xA0C5] = (uint8_t)(ground - 0x0A - 2 + rnd(5));
            o->hit = HUMANOID_CARRIED;
            o->owner = new_process(HUMANOID_CARRIED_BY_SHIP, h);
            step_case("humanoid carried", n, o->owner);
        } else if (kind == 3) {
            /* the walker on this humanoid: terrain anywhere (the $E8 clamp), any frame, any random value */
            uint16_t slot = 0;
            for (uint16_t a = HUMANOIDS; a < 0xA13A; a += 2)
                if (game_word(&model, a) == h) slot = a;
            if (!slot) continue;
            game_poke(&model, (uint16_t)(0xB300 + (o->x >> 6)), (uint8_t)(rnd(2) ? 0xD0 + rnd(0x30) : rnd(256)));
            o->picture = (uint16_t)(rnd(2) ? 0xF901 : 0xF915) + 10 * rnd(2);
            o->hit = HUMANOID_ON_GROUND;
            if (!o->screen) o->screen = 0x4080;
            model.ram[0xA0DF] = (uint8_t)rnd(24);
            new_process(HUMANOID_WALK, (uint16_t)(slot - 2));
            step_case("humanoid walking", n, model.processes.current);
        } else {
            /* the bonus: shown, then gone, from a humanoid at any line */
            o->y = (uint16_t)rnd(0x10000);
            uint16_t p = new_process(rnd(2) ? BONUS_250 : BONUS_500, h);
            step_case("bonus shown", n, p);
            load_model();
            model.processes.current = p;
            step_case("bonus gone", n, p);
        }
    }
    printf("test_routines: humanoid and bonus steps: %ld equal the ROM's, %ld not ported\n", steps_compared,
           steps_skipped);
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
    memcpy(game_ram, board.ram, sizeof game_ram);
    test_score(5000);
    test_humanoid_steps(2000);
    if (failures) {
        fprintf(stderr, "test_routines: %d differences\n", failures);
        return 1;
    }
    printf("test_routines: OK\n");
    return 0;
}
