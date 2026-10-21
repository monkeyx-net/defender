#include "sim/wave.h"

#include "sim/baiter.h"
#include "sim/cmos.h"
#include "sim/death.h"
#include "sim/flow.h"
#include "sim/player.h"
#include "sim/bombers.h"
#include "sim/humanoids.h"
#include "sim/pods.h"
#include "sim/score.h"
#include "sim/lander.h"

#define STATE 0xA0BA
#define LANDERS 0xA112
#define LANDERS_TO_COME 0xA0FB
#define LANDER_EVERY 0xA100
#define LANDERS_AT_ONCE 0xA101
#define BAITER_EVERY 0xA10F
#define LANDER_TIMER 0xA117
#define BAITER_TIMER 0xA118
#define BAITERS 0xA119
#define BAITERS_MAX 0x0C
#define SETTINGS 0xA0FB
#define RAMP_EVERY 0x28

/* $DBE1: `n` humanoids at x high byte (random & $1F) + `base`. */
static void place_humanoids(Game *g, uint8_t n, uint8_t base, uint16_t *slot, uint16_t owner)
{
    game_poke(g, 0xA074, n);
    do {
        uint16_t obj = objects_alloc(&g->objects);
        if (!obj) return;
        objects_setup(&g->objects, obj, owner, 0xF901, HUMANOID_ON_GROUND, 0x6666);
        Object *o = objects_at(&g->objects, obj);
        game_random(g);
        uint8_t lo = g->ram[0xA0E1];
        o->x = (uint16_t)((uint8_t)((g->ram[0xA0E0] & 0x1F) + base) << 8 | lo);
        if (lo & 1) o->picture = 0xF915;
        o->y = (uint16_t)(0xE0 << 8 | (o->y & 0xFF));
        o->flags = 0x10;
        o->vy = o->vx = 0;
        o->owner = 0;
        g->objects.screen = obj;
        game_set_word(g, *slot, obj);
        *slot = (uint16_t)(*slot + 2);
        game_poke(g, 0xA074, (uint8_t)(g->ram[0xA074] - 1));
    } while (g->ram[0xA074]);
}

void wave_start(Game *g, uint8_t b)
{
    uint8_t *r = g->ram;
    uint16_t walker = processes_create(&g->processes, HUMANOID_WALK, 0, 0);
    if (!walker) return;
    game_set_word(g, (uint16_t)(walker + 7), HUMANOIDS);
    for (uint16_t a = HUMANOIDS; a < 0xA142; a++) game_poke(g, a, 0);
    uint16_t slot = HUMANOIDS, block = game_word(g, 0xA08D);
    uint8_t a = (uint8_t)game_peek(g, (uint16_t)(block + 0xA));
    game_poke(g, HUMANOIDS_LEFT, a);
    if (a) {
        if (a > 7) {
            uint8_t quarter = a >> 2;
            b = 0;
            do {
                place_humanoids(g, quarter, b, &slot, block);
                b = (uint8_t)(b + 0x40);
            } while (b);
            a = (uint8_t)(game_peek(g, (uint16_t)(block + 0xA)) - (uint8_t)(quarter << 2));
        }
        if (a) {
            game_poke(g, 0xA073, a);
            do {
                b = r[0xA0E0];
                place_humanoids(g, 1, b, &slot, block);
                game_poke(g, 0xA073, (uint8_t)(r[0xA073] - 1));
            } while (r[0xA073]);
        }
    }
    for (uint16_t x = 0xA0FB, u = (uint16_t)(block + 0xB); x != 0xA112; x++, u++) game_poke(g, x, (uint8_t)game_peek(g, u));
    for (uint16_t x = 0xA112; x != 0xA11A; x++) game_poke(g, x, 0);
    for (;;) {
        /* a spare block at a random place, taken before the count is
           looked at: with no swarmers carried over it is never given back */
        uint16_t spare = objects_alloc(&g->objects);
        Object *o = objects_at(&g->objects, spare);
        if (!o) return;
        o->y = (uint16_t)((uint8_t)((r[0xA0DF] >> 1) + 0x2A) << 8 | (o->y & 0xFF));
        game_random(g);
        o->x = (uint16_t)((uint16_t)((uint8_t)((r[0xA0DF] & 0x3F) + 0x80) << 8 | b) + game_word(g, 0xA020));
        b = (uint8_t)o->x; /* ADDD leaves the sum's low byte in B */
        if (!r[0xA0FF]) break;
        uint8_t n = r[0xA0FF] > 6 ? 6 : r[0xA0FF];
        swarmer_spawn(g, n, spare);
        o->link = g->objects.free_list; /* the spare goes back */
        g->objects.free_list = spare;
        game_poke(g, 0xA0FF, (uint8_t)(r[0xA0FF] - n));
        if (!r[0xA0FF]) break;
    }
    uint16_t u = (uint16_t)(block + 0x22); /* U after copying the settings */
    if (r[0xA0FE]) {
        mutant_spawn(g, r[0xA0FE]);
        game_poke(g, 0xA0FE, 0);
        u = processes_last_created(&g->processes); /* $EF19 leaves U at its last process */
    }
    game_poke(g, 0xA114, r[0xA0FD]);
    if (r[0xA0FD]) {
        uint8_t n = r[0xA0FD];
        game_poke(g, 0xA0FD, 0);
        pod_spawn(g, n, u);
    }
    game_poke(g, 0xA113, r[0xA0FC]);
    while (r[0xA0FC]) {
        uint8_t n = r[0xA0FC] > 3 ? 3 : r[0xA0FC];
        bomber_spawn(g, n);
        game_poke(g, 0xA0FC, (uint8_t)(r[0xA0FC] - n));
    }
}

uint8_t wave_enemies_left(const Game *g)
{
    const uint8_t *r = g->ram;
    return (uint8_t)(r[LANDERS] + r[0xA0FB] + r[0xA113] + r[0xA114] + r[0xA116] + r[0xA115] + r[0xA0FE]);
}

void wave_ramp(Game *g, int column, uint16_t settings)
{
    g->ram[0xA036] = 7; /* $F4FF */
    uint16_t x = (uint16_t)(game_read_bank(g, 7, 0xC011) << 8 | game_read_bank(g, 7, 0xC012));
    uint16_t end = (uint16_t)(game_read_bank(g, 7, 0xC013) << 8 | game_read_bank(g, 7, 0xC014));
    for (uint16_t y = settings; x != end; x = (uint16_t)(x + 8), y++) {
        uint8_t delta = game_read_bank(g, 7, (uint16_t)(x + column)), now = (uint8_t)game_peek(g, y);
        unsigned sum = (unsigned)delta + now;
        uint8_t a = (uint8_t)sum;
        if (delta & 0x80) {
            if (sum > 0xFF && a >= game_read_bank(g, 7, (uint16_t)(x + 1))) game_poke(g, y, a);
        } else {
            if (sum <= 0xFF && a <= game_read_bank(g, 7, x)) game_poke(g, y, a);
        }
    }
}

StepResult wave_step(Game *g, uint16_t proc)
{
    uint8_t *r = g->ram;
    if (!(r[STATE] & 0x08)) {
        uint8_t left = wave_enemies_left(g);
        if (!left) { /* the wave is over */
            r[STATE] = 0x77;
            kill_others(g);
            wave_save(g);
            return wave_end(g, proc, 0xDD02);
        }
        if (left <= 8) {
            uint8_t b = r[BAITER_EVERY] >> 1;
            if (left <= 3) b >>= 1;
            b++;
            if (b < r[BAITER_TIMER]) r[BAITER_TIMER] = b;
        }
        if (!--r[BAITER_TIMER]) {
            r[BAITER_TIMER] = left >= 4 ? r[BAITER_EVERY] : game_random_upto(g, r[BAITER_EVERY] >> 2);
            if (r[BAITERS] < BAITERS_MAX) {
                baiter_spawn(g);
                r[BAITERS]++;
            }
        }
        if (!--r[LANDER_TIMER] || !r[LANDERS]) {
            r[LANDER_TIMER] = r[LANDER_EVERY];
            if (r[LANDERS_TO_COME] && r[LANDERS] < 8) {
                uint8_t n = r[LANDERS_AT_ONCE];
                if (n > r[LANDERS_TO_COME]) n = r[LANDERS_TO_COME];
                lander_spawn(g, n);
                r[LANDERS_TO_COME] = (uint8_t)(r[LANDERS_TO_COME] - n);
            }
        }
    }
    if (r[0xA0AE] < 0x10) r[0xA0AE]++;
    uint8_t minute = (uint8_t)(r[0xA024] + 1);
    if (minute > 0xF0) { /* $F0 steps of 15 ticks: a minute played (bank 3 left selected) */
        r[0xA036] = 3;
        bookkeep(g, 6, 1);
        minute = 0;
    }
    r[0xA024] = minute;
    uint8_t ramp = (uint8_t)(game_peek(g, (uint16_t)(proc + 7)) - 1);
    game_poke(g, (uint16_t)(proc + 7), ramp);
    if (!ramp) {
        wave_ramp(g, 2, SETTINGS);
        game_poke(g, (uint16_t)(proc + 7), RAMP_EVERY);
    }
    processes_sleep(&g->processes, 15, WAVE);
    return STEP_DONE;
}
