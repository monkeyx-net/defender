#include "sim/turn.h"

#include "sim/background.h"
#include "sim/cmos.h"
#include "sim/flow.h"
#include "sim/laser.h"
#include "sim/score.h"
#include "sim/terrain.h"
#include "sim/text.h"
#include "sim/wave.h"

#define STATE 0xA0BA

static StepResult sleep(Game *g, uint8_t ticks, uint16_t resume)
{
    processes_sleep(&g->processes, ticks, resume);
    return STEP_DONE;
}

/* $E052 */
static void stars_init(Game *g)
{
    g->ram[0xA0AE] = 0x10;
    uint8_t colour = 0;
    for (uint16_t x = 0xAF9D; x != 0xAFDD; x = (uint16_t)(x + 4)) {
        uint8_t a;
        do a = game_random(g);
        while (a >= 0x9C);
        game_poke(g, x, a);
        do a = game_random(g);
        while (a > 0xA8 || a <= 0x2A);
        game_poke(g, (uint16_t)(x + 1), a);
        game_poke(g, (uint16_t)(x + 2), colour);
        colour = (uint8_t)((colour + 0x11) & 0x77);
    }
}

/* $E54B: the bombs' colours. */
static void bomb_colours(Game *g)
{
    game_set_word(g, 0xA0A8, 0xA1A2);
    for (uint16_t x = 0xA1A2; x != 0xA1C2; x++) {
        uint8_t b = 0x0A;
        uint8_t a = game_random(g);
        if (!(a & 0x80)) b = 0x09;
        b = (uint8_t)(b + ((a & 1) ? 0x90 : 0xA0));
        game_poke(g, x, b);
    }
}

/* $E149: the flame's colours, twice over. */
static void flame_colours(Game *g)
{
    game_set_word(g, 0xA09F, 0xA162);
    for (uint16_t x = 0xA162; x != 0xA183; x++) {
        uint8_t a = game_random(g);
        game_poke(g, (uint16_t)(x + 0x20), a);
        game_poke(g, x, a);
    }
}

void game_reset(Game *g)
{
    for (int i = 0; i < 16; i++) g->ram[0xA026 + i] = game_read(g, (uint16_t)(0xF8BE + i));
    LaserColours c;
    Rng r = { g->ram[0xA0DF], g->ram[0xA0E0], g->ram[0xA0E1] };
    laser_colours_fill(&c, &r);
    g->ram[0xA0DF] = r.value;
    g->ram[0xA0E0] = r.hi;
    g->ram[0xA0E1] = r.lo;
    for (int i = 0; i < LASER_COLOURS_SIZE; i++) game_poke(g, (uint16_t)(LASER_COLOURS + i), c.table[i]);
    game_set_word(g, 0xA0A4, c.next);
    stars_init(g);
    objects_init(&g->objects);
    bomb_colours(g);
    flame_colours(g);
}

StepResult turn_step(Game *g, uint16_t proc, uint16_t resume)
{
    uint8_t *r = g->ram;
    switch (resume) {
    case TURN:
        r[0xA036] = 3;
        bookkeep(g, 7, 1);
        /* fall through */
    case TURN_RESET:
        game_reset(g);
        kill_others(g);
        r[STATE] = 0x7F;
        if (g->processes.current == g->processes.head && !processes_at(&g->processes, proc)->link) break;
        return sleep(g, 15, TURN_WAIT);
    case TURN_WAIT:
        if (r[0xA07F] | r[0xA080] | r[0xA07F]) return sleep(g, 15, TURN_WAIT);
        break;
    case TURN_SETUP: {
        r[0xA036] = 0;
        if (g->in1 & 0x80) return STEP_UNPORTED; /* the cocktail table's other side */
        game_set_word(g, 0xA020, 0);
        game_set_word(g, 0xA022, 0);
        r[0xA036] = 7;
        terrain_heights(g);
        terrain_start(g);
        clear_playfield(g);
        game_set_word(g, 0xA0BD, 0x0300);
        game_set_word(g, 0xA0BB, 0x0300);
        r[0xA0AD] = r[0xA0B5] = r[0xA08A] = r[0xA0AF] = r[0xA09A] = r[0xA099] = 0;
        game_set_word(g, 0xA09B, 0xA11A);
        uint16_t block = score_player(g);
        game_set_word(g, 0xA08D, block);
        r[0xA02B] = game_read(g, (uint16_t)(0xDB53 + (game_peek(g, (uint16_t)(block + 8)) & 7)));
        game_poke(g, (uint16_t)(block + 7), (uint8_t)(game_peek(g, (uint16_t)(block + 7)) - 1));
        hud_draw(g);
        game_set_word(g, 0xA0C1, 0x2080);
        game_set_word(g, 0xA0BF, 0x2080);
        game_set_word(g, 0xA0C3, 0x2000);
        game_set_word(g, 0xA0CC, (uint16_t)(0x0800 + game_word(g, 0xA020)));
        game_set_word(g, 0xA0C5, 0x8000);
        game_set_word(g, 0xA0C7, 0);
        r[0xA0C9] = 0;
        game_set_word(g, 0xA0CA, 0);
        static const uint16_t starts[6] = { 0xE9E3, 0xE782, 0xF493, 0xE9BF, 0xF464, 0xF43D };
        for (int i = 0; i < 6; i++) processes_create(&g->processes, starts[i], 0, 0);
        if (r[0xA025] && r[0xA08C] != 1) {
            r[0xA036] = 2;
            text_print(g, 0x3C80, r[0xA08B] == 1 ? 0xC0EF : 0xC0F1, 0, 0);
            return sleep(g, 0x80, TURN_CLEAR);
        }
    }
        /* fall through */
    case TURN_CLEAR:
        clear_playfield(g);
        r[STATE] = (uint8_t)(0x05 | (game_peek(g, (uint16_t)(game_word(g, 0xA08D) + 0xA)) ? 0 : 0x02));
        return sleep(g, 0x60, TURN_WAVE);
    case TURN_WAVE:
        wave_start(g, 0); /* B as the dispatcher left it: not modelled */
        r[STATE] = r[0xA0FA] ? 0 : 0x02;
        r[0xA025] = 0;
        game_poke(g, (uint16_t)(proc + 7), 0x28);
        r[0xA118] = r[0xA10F];
        r[0xA117] = 1;
        processes_at(&g->processes, proc)->resume = WAVE;
        return wave_step(g, proc);
    default:
        return STEP_UNPORTED;
    }
    /* $D942: a fresh pool with the turn's next step; the main loop again */
    processes_init(&g->processes);
    processes_create(&g->processes, TURN_SETUP, 0, 0);
    return STEP_RESTART;
}
