/*
 * Ported routines against the ROM's own, on chosen inputs (label rom):
 *   test_routines <defender.zip|folder>
 * The board is booted into a game; then for each case the RAM is set up,
 * the ROM routine runs on the emulated CPU (interrupts masked) until it
 * returns, and the C routine runs on a model of the same RAM: all of RAM
 * (bar the stack page) must then be equal. DBG_ALL=1 lists every byte
 * that differs, not just the first.
 *   - process steps from situations the scripted games rarely reach: the
 *     state is built in the model and copied to RAM, the ROM's step runs
 *     from its resume address until it is back at the dispatcher ($E856),
 *     the C step from the same state: a humanoid falling onto the terrain
 *     at various speeds ($F216), carried by the ship down to it ($F24C),
 *     the bonus that follows ($EE65, $EE73, $EEB2); the wave's step ($DCEA)
 *     with random counts, timers and settings (landers, mutants and
 *     baiters arriving, the ramp); baiters ($EAB4) anywhere around the ship
 *   - the arrivals ($EF9C landers, $EF15 mutants, $EA80 baiters, $EB36
 *     pods, $EB9E swarmers, $F29D bombers) from random states, then the
 *     steps of the swarmers and bombers they made, then every hit handler
 *     on them ($F20B, $EF6D, $EB2B, $EB74, $EBE9, $F3BC), and a smart bomb
 *     ($E8C1) on them, some on screen, some still appearing
 *   - a wave's start ($DC1E) from random player blocks: humanoids, the
 *     settings, swarmers, mutants, pods and bombers carried over
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
#include "sim/baiter.h"
#include "sim/bombers.h"
#include "sim/enemies.h"
#include "sim/lander.h"
#include "sim/player.h"
#include "sim/pods.h"
#include "sim/humanoids.h"
#include "sim/wave.h"
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

static int rom_call(uint16_t pc, uint16_t d);

/* Run the ROM routine at `pc` with these registers until it returns. */
static int rom_call_with(uint16_t pc, uint16_t d, uint16_t x, uint16_t y, uint16_t u)
{
    board.cpu.x = x;
    board.cpu.y = y;
    board.cpu.u = u;
    board.cpu.dp = 0xA0;
    return rom_call(pc, d);
}

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
    if (getenv("DBG_ALL")) {
        int k = 0;
        for (unsigned a = 0; a < STACK && k < 12; a++)
            if (game_peek(&model, (uint16_t)a) != board.ram[a]) {
                unsigned off = a >= OBJECTS_FIRST && a < OBJECTS_FIRST + OBJECTS * OBJECT_SIZE ? (a - OBJECTS_FIRST) % OBJECT_SIZE : 99;
                fprintf(stderr, "  %s %ld: $%04X (obj +%02X) = %02X, C %02X\n", what, n, a, off, board.ram[a], game_peek(&model, (uint16_t)a));
                k++;
            }
    }
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

static void test_wave_steps(int cases)
{
    long before_compared = steps_compared, before_skipped = steps_skipped;
    for (int n = 0; n < cases; n++) {
        memcpy(board.ram, game_ram, sizeof game_ram);
        load_model();
        uint8_t *r = model.ram;
        r[0xA0BA] = rnd(8) ? 0 : 0x08;
        r[0xA112] = (uint8_t)rnd(10);           /* landers */
        r[0xA0FB] = (uint8_t)rnd(20);           /* landers to come */
        r[0xA113] = (uint8_t)rnd(3);
        r[0xA114] = (uint8_t)rnd(3);
        r[0xA115] = (uint8_t)rnd(3);
        r[0xA116] = (uint8_t)rnd(3);
        r[0xA0FE] = 0;
        r[0xA100] = (uint8_t)(1 + rnd(8));      /* landers every */
        r[0xA101] = (uint8_t)(1 + rnd(6));      /* landers at once */
        r[0xA10F] = (uint8_t)(1 + rnd(40));     /* baiters every */
        r[0xA117] = (uint8_t)(1 + rnd(2));
        r[0xA118] = (uint8_t)(rnd(2) ? 1 + rnd(3) : 1 + rnd(40));
        if (!rnd(3)) { /* few left: the baiter timer's cut */
            r[0xA112] = (uint8_t)rnd(4);
            r[0xA0FB] = r[0xA113] = r[0xA114] = r[0xA115] = r[0xA116] = 0;
        }
        r[0xA119] = (uint8_t)rnd(14);
        r[0xA0AE] = (uint8_t)rnd(0x14);
        r[0xA024] = (uint8_t)rnd(0xF0);
        if (rnd(2)) r[HUMANOIDS_LEFT] = 0;      /* mutants instead of landers */
        r[0xA0DF] = (uint8_t)rnd(256);
        r[0xA0E0] = (uint8_t)rnd(256);
        r[0xA0E1] = (uint8_t)rnd(256);
        game_set_word(&model, 0xA020, (uint16_t)rnd(0x10000));
        uint16_t p = new_process(WAVE, 0);
        game_poke(&model, (uint16_t)(p + 7), (uint8_t)(1 + rnd(3)));
        step_case("wave", n, p);
    }
    printf("test_routines: wave steps: %ld equal the ROM's, %ld not ported\n", steps_compared - before_compared,
           steps_skipped - before_skipped);
}

static void test_baiter_steps(int cases)
{
    long before_compared = steps_compared, before_skipped = steps_skipped;
    for (int n = 0; n < cases; n++) {
        memcpy(board.ram, game_ram, sizeof game_ram);
        load_model();
        model.ram[0xA0DF] = (uint8_t)rnd(256);
        model.ram[0xA0E0] = (uint8_t)rnd(256);
        baiter_spawn(&model); /* checked by the wave's steps */
        uint16_t p = model.processes.free_small ? 0 : 0;
        for (uint16_t a = model.processes.head; a; a = processes_at(&model.processes, a)->link)
            if (processes_at(&model.processes, a)->resume == BAITER) p = a;
        if (!p) continue;
        Object *o = objects_at(&model.objects, game_word(&model, (uint16_t)(p + 7)));
        o->picture = rnd(3) ? 0xF9B7 : (uint16_t)(0xF9A3 + 10 * rnd(3));
        static const int edges[] = { -0x281, -0x280, -0x27F, 0x27F, 0x280, 0x281 };
        o->x = (uint16_t)(game_word(&model, 0xA0CC) + (rnd(2) ? (int)rnd(0x800) - 0x400 : edges[rnd(6)]));
        o->y = (uint16_t)((0x2A + rnd(0xC6)) << 8 | rnd(256));
        game_poke(&model, (uint16_t)(p + 9), (uint8_t)(1 + rnd(2)));
        model.ram[0xA0DF] = (uint8_t)rnd(256);
        model.ram[0xA111] = (uint8_t)rnd(256);
        model.ram[0xA110] = (uint8_t)rnd(30);
        game_set_word(&model, 0xA0CA, (uint16_t)(rnd(0x400) - 0x200));
        model.processes.current = p;
        step_case("baiter", n, p);
    }
    printf("test_routines: baiter steps: %ld equal the ROM's, %ld not ported\n", steps_compared - before_compared,
           steps_skipped - before_skipped);
}

/* A random state for arrivals: the random generator, the scroll, the ship. */
static void shake(void)
{
    for (int i = 0; i < 3; i++) model.ram[0xA0DF + i] = (uint8_t)rnd(256);
    game_set_word(&model, 0xA020, (uint16_t)rnd(0x10000));
    game_set_word(&model, 0xA0CC, (uint16_t)rnd(0x10000));
    model.ram[0xA0C0] = (uint8_t)(0x2A + rnd(0xC0));
    model.ram[0xA0BA] = rnd(4) ? 0 : 0x80;
    model.ram[0xA0AA] = (uint8_t)rnd(256);
}

/* The ROM routine and the C one on the same state; 0 if equal. */
static long call_case(const char *what, long n, uint16_t pc, uint16_t d, uint16_t x, uint16_t y,
                      void (*c)(uint16_t d, uint16_t x, uint16_t y))
{
    store_model();
    uint16_t u = model.processes.current;
    if (rom_call_with(pc, d, x, y, u)) {
        fprintf(stderr, "test_routines: %s: $%04X did not return\n", what, pc);
        failures++;
        return -1;
    }
    c(d, x, y);
    return compare(what, n);
}

static void c_landers(uint16_t d, uint16_t x, uint16_t y) { (void)x; (void)y; lander_spawn(&model, (uint8_t)(d >> 8)); }
static void c_mutants(uint16_t d, uint16_t x, uint16_t y) { (void)x; (void)y; mutant_spawn(&model, (uint8_t)(d >> 8)); }
static void c_baiter(uint16_t d, uint16_t x, uint16_t y) { (void)d; (void)x; (void)y; baiter_spawn(&model); }
static void c_pods(uint16_t d, uint16_t x, uint16_t y) { (void)x; (void)y; pod_spawn(&model, (uint8_t)(d >> 8), model.processes.current); }
static void c_swarmers(uint16_t d, uint16_t x, uint16_t y) { (void)x; swarmer_spawn(&model, (uint8_t)(d >> 8), y); }
static void c_bombers(uint16_t d, uint16_t x, uint16_t y) { (void)x; (void)y; bomber_spawn(&model, (uint8_t)(d >> 8)); }

static uint16_t hit_handler;
static int hit_result;
static void c_hit(uint16_t d, uint16_t x, uint16_t y) { (void)d; (void)y; hit_result = enemy_hit(&model, hit_handler, x); }

/* Objects on the screen list with this hit handler. */
static int find_objects(uint16_t handler, uint16_t *out, int max)
{
    int n = 0;
    for (uint16_t a = model.objects.screen; a && n < max; a = objects_at(&model.objects, a)->link)
        if (objects_at(&model.objects, a)->hit == handler) out[n++] = a;
    return n;
}

static long arrivals, enemy_steps, hits, bombs;

static void test_enemies(int cases)
{
    for (int n = 0; n < cases; n++) {
        memcpy(board.ram, game_ram, sizeof game_ram);
        load_model();
        shake();
        int kind = (int)rnd(6);
        uint8_t count = (uint8_t)(1 + rnd(kind == 5 ? 3 : 6));
        if (kind == 0 && rnd(3) == 0) model.ram[HUMANOIDS_LEFT] = 0;
        uint16_t from = model.objects.screen;
        static const uint16_t pcs[6] = { 0xEF9C, 0xEF15, 0xEA80, 0xEB36, 0xEB9E, 0xF29D };
        void (*cs[6])(uint16_t, uint16_t, uint16_t) = { c_landers, c_mutants, c_baiter, c_pods, c_swarmers, c_bombers };
        if (call_case("arrival", n, pcs[kind], (uint16_t)(count << 8), 0, from, cs[kind]) != -1) continue;
        arrivals++;
        load_model();

        /* the steps of what arrived */
        for (uint16_t p = model.processes.head; p; p = processes_at(&model.processes, p)->link) {
            uint16_t resume = processes_at(&model.processes, p)->resume;
            if (resume != SWARMER_AIM && resume != SWARMER && resume != BOMBERS && resume != BAITER) continue;
            Game saved = model;
            model.ram[0xA0DF] = (uint8_t)rnd(256);
            model.ram[0xA0E1] = (uint8_t)rnd(256);
            model.processes.current = p;
            if (resume == SWARMER_AIM && rnd(2)) {
                /* the next step (new ones aim first), its shot due, heading for the ship */
                processes_at(&model.processes, p)->resume = SWARMER;
                Object *o = objects_at(&model.objects, game_word(&model, (uint16_t)(p + 7)));
                game_poke(&model, (uint16_t)(p + 0xB), 1);
                game_set_word(&model, 0xA0CC, (uint16_t)(o->x + (int16_t)o->vx * 0x40));
                o->y = (uint16_t)((0x40 + rnd(0x90)) << 8);
                game_set_word(&model, 0xA020, (uint16_t)(o->x - 0x1000)); /* on screen */
                model.ram[0xA099] = (uint8_t)rnd(12);
            }
            if (resume == BOMBERS) {
                /* on screen at the edges of its distance from the ship's line; a mine due */
                static const int edges[] = { -0x21, -0x20, -0x1F, -0x11, -0x10, -0x0F, 0x0F, 0x10, 0x11, 0x1F, 0x20, 0x21 };
                model.ram[0xA0C0] = (uint8_t)(0x50 + rnd(0x60));
                for (int k = 7; k <= 0xB; k += 2) {
                    Object *o = objects_at(&model.objects, game_word(&model, (uint16_t)(p + k)));
                    if (!o || !rnd(3)) continue;
                    o->screen = (uint16_t)(0x4000 | (uint8_t)(model.ram[0xA0C0] + edges[rnd(12)]));
                    o->x = (uint16_t)(game_word(&model, 0xA020) + 0x1000);
                    o->y = (uint16_t)((uint8_t)o->screen << 8);
                }
                if (rnd(2)) model.ram[0xA0E1] &= 0xF8;
                model.ram[0xA099] = (uint8_t)rnd(12);
            }
            long before = steps_compared;
            step_case("enemy step", n, p);
            enemy_steps += steps_compared - before;
            memcpy(board.ram, game_ram, 0); /* keep the board as the step left it */
            model = saved;
            store_model();
        }

        /* a smart bomb on them: some on screen, some still appearing */
        {
            Game saved = model;
            for (uint16_t a = model.objects.screen; a; a = objects_at(&model.objects, a)->link) {
                Object *o = objects_at(&model.objects, a);
                if (rnd(3)) o->screen = (uint16_t)(0x2000 + rnd(0x6000));
                if (!rnd(4)) o->flags = (uint8_t)rnd(4);
            }
            model.ram[0xA09A] = rnd(6) ? 0 : 1;
            game_poke(&model, (uint16_t)(score_player(&model) + 9), (uint8_t)rnd(4));
            uint16_t p = new_process(SMART_BOMB, 0);
            long before = steps_compared;
            step_case("smart bomb", n, p);
            bombs += steps_compared - before;
            model = saved;
            store_model();
        }

        /* hit each kind */
        static const uint16_t handlers[] = { 0xF20B, 0xEF6D, HIT_BAITER, HIT_POD, HIT_SWARMER, HIT_BOMBER };
        for (int h = 0; h < 6; h++) {
            uint16_t objs[8];
            int k = find_objects(handlers[h], objs, 8);
            if (!k) continue;
            Game saved = model;
            hit_handler = handlers[h];
            uint16_t obj = objs[rnd((unsigned)k)];
            if (!processes_at(&model.processes, game_word(&model, (uint16_t)(obj + 6))) && handlers[h] != HIT_POD &&
                handlers[h] != HIT_BOMBER)
                continue;
            model.ram[0xA0DF] = (uint8_t)rnd(256);
            if (call_case("hit", n, handlers[h], 0, obj, 0, c_hit) == -1 && hit_result >= 0) hits++;
            model = saved;
            store_model();
        }
    }
    printf("test_routines: arrivals: %ld equal the ROM's; then %ld enemy steps, %ld smart bombs and %ld hits equal\n",
           arrivals, enemy_steps, bombs, hits);
}

/* Every picture's erase routine ([picture + 8]) against game_erase_object. */
static void test_erase(void)
{
    long done = 0;
    for (uint16_t pic = 0xF8CE; pic < 0xF9F1; pic++) {
        uint16_t draw = (uint16_t)(data[pic + 6] << 8 | data[pic + 7]), erase = (uint16_t)(data[pic + 8] << 8 | data[pic + 9]);
        if (draw < 0xD0F0 || draw >= 0xD400 || ((erase < 0xD0F0 || erase >= 0xD400) && erase != 0xD8DB)) continue;
        for (int n = 0; n < 20; n++) {
            memcpy(board.ram, game_ram, sizeof game_ram);
            for (unsigned a = 0x2000; a < 0x9000; a++) board.ram[a] = (uint8_t)rnd(256); /* something to erase */
            load_model();
            uint16_t obj = model.objects.screen;
            Object *o = objects_at(&model.objects, obj);
            o->picture = pic;
            o->screen = (uint16_t)((0x20 + rnd(0x60)) << 8 | (0x30 + rnd(0xA0)));
            store_model();
            if (rom_call_with(erase, o->screen, 0, pic, 0)) {
                failures++;
                return;
            }
            game_erase_object(&model, obj);
            model.ram[0xA036] = board.ram[0xA036]; /* $F3FE selects the bank, the routine alone does not */
            compare("erase", pic);
            done++;
        }
    }
    printf("test_routines: picture erases: %ld equal the ROM's\n", done);
}

static uint8_t wave_b;
static void c_wave_start(uint16_t d, uint16_t x, uint16_t y) { (void)d; (void)x; (void)y; wave_start(&model, wave_b); }

static void test_wave_start(int cases)
{
    long done = 0;
    for (int n = 0; n < cases; n++) {
        memcpy(board.ram, game_ram, sizeof game_ram);
        load_model();
        shake();
        uint16_t block = score_player(&model);
        game_poke(&model, (uint16_t)(block + 0xA), (uint8_t)rnd(13));
        for (int i = 0; i < 0x17; i++) game_poke(&model, (uint16_t)(block + 0xB + i), (uint8_t)rnd(16));
        game_poke(&model, (uint16_t)(block + 0xB + 4), (uint8_t)(rnd(2) ? 0 : rnd(11))); /* swarmers */
        game_poke(&model, (uint16_t)(block + 0xB + 3), (uint8_t)(rnd(2) ? 0 : rnd(5))); /* mutants */
        game_poke(&model, (uint16_t)(block + 0xB + 2), (uint8_t)(rnd(2) ? 0 : rnd(5))); /* pods */
        game_poke(&model, (uint16_t)(block + 0xB + 1), (uint8_t)(rnd(2) ? 0 : rnd(6))); /* bombers */
        wave_b = (uint8_t)rnd(256);
        if (call_case("wave start", n, 0xDC1E, wave_b, 0, 0, c_wave_start) == -1) done++;
    }
    printf("test_routines: wave starts: %ld equal the ROM's\n", done);
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
    static uint8_t banks[8][0x1000];
    for (int b = 1; b <= 7; b++)
        for (unsigned a = 0; a < 0x1000; a++) banks[b][a] = (uint8_t)rom_bank_byte(&rom, b, a);
    for (int b = 1; b <= 7; b++) model.bank[b] = banks[b];
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
    test_erase();
    test_score(5000);
    test_humanoid_steps(2000);
    test_wave_steps(2000);
    test_baiter_steps(2000);
    test_enemies(3000);
    test_wave_start(1000);
    if (failures) {
        fprintf(stderr, "test_routines: %d differences\n", failures);
        return 1;
    }
    printf("test_routines: OK\n");
    return 0;
}
