#include "sim/wave.h"

#include "sim/baiter.h"
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
        if (!left) return STEP_UNPORTED; /* the wave is over */
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
    if (minute > 0xF0) return STEP_UNPORTED; /* the bookkeeping counter */
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
