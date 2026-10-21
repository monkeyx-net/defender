#include "sim/death.h"

#include "sim/attract.h"
#include "sim/background.h"
#include "sim/cmos.h"
#include "sim/explosions.h"
#include "sim/flow.h"
#include "sim/player.h"
#include "sim/score.h"
#include "sim/sounds.h"
#include "sim/terrain.h"
#include "sim/text.h"
#include "sim/turn.h"
#include "sim/wave.h"

#define STATE 0xA0BA
#define WHITE_SHIP 0xAFDD
#define FLASHES 0xDB4B
#define EXPLOSION_TICK 0xC649 /* bank 7 */
#define FADE 0xC6AB           /* bank 7 */
#define PARTICLES 0xB300
#define PARTICLES_END 0xB800

static StepResult sleep(Game *g, uint8_t ticks, uint16_t resume)
{
    processes_sleep(&g->processes, ticks, resume);
    return STEP_DONE;
}

/* $DA3D: `<$BA` = b, | 2 without humanoids. */
static void set_state(Game *g, uint8_t b) { g->ram[STATE] = (uint8_t)(b | (g->ram[0xA0FA] ? 0 : 0x02)); }

/* $DB5C: a white copy of picture `pic` at `at` (descriptor, then data). */
static void white_copy(Game *g, uint16_t pic, uint16_t at)
{
    g->ram[0xA036] = 2;
    uint8_t w = game_read(g, pic), h = game_read(g, (uint16_t)(pic + 1));
    game_poke(g, at, w);
    game_poke(g, (uint16_t)(at + 1), h);
    uint16_t size = (uint16_t)(w * h);
    uint16_t even = (uint16_t)(at + 0xA), odd = (uint16_t)(even + size), end = (uint16_t)(odd + size);
    game_set_word(g, (uint16_t)(at + 2), even);
    game_set_word(g, (uint16_t)(at + 4), odd);
    for (int i = 6; i < 10; i++) game_poke(g, (uint16_t)(at + i), game_read(g, (uint16_t)(pic + i)));
    for (int half = 0; half < 2; half++) {
        uint16_t src = (uint16_t)(game_read(g, (uint16_t)(pic + 2 + 2 * half)) << 8 | game_read(g, (uint16_t)(pic + 3 + 2 * half)));
        uint16_t u = half ? odd : even, stop = half ? end : odd;
        do { /* $DB90: two bytes at a time, every lit nibble $B */
            for (int k = 0; k < 2; k++) {
                uint8_t v = game_read(g, src++);
                if (v & 0xF0) v |= 0xF0;
                if (v & 0x0F) v |= 0x0F;
                game_poke(g, u++, (uint8_t)(v & 0xBB));
            }
        } while (u < stop);
    }
}

/* bank 7 $C5D0: the particles, from the ship's position `at`. */
static void explosion_setup(Game *g, uint16_t proc, uint16_t at, uint16_t back)
{
    uint8_t *r = g->ram;
    game_set_word(g, (uint16_t)(proc + 9), back);
    game_set_word(g, 0xA000, at);
    game_set_word(g, 0xA004, 0x0808);
    game_set_word(g, 0xA006, 0x1732);
    for (uint16_t y = PARTICLES; y != PARTICLES_END; y = (uint16_t)(y + 0xA)) {
        for (;;) {
            game_set_word(g, (uint16_t)(y + 2), (uint16_t)(r[0xA000] << 8));
            game_set_word(g, (uint16_t)(y + 4), (uint16_t)(r[0xA001] << 8));
            uint8_t a = r[0xA005];
            int carry = (((a >> 1) ^ a) >> 1) & 1; /* LSRA, EORA, LSRA, LSRA */
            uint8_t c4 = r[0xA004];
            r[0xA004] = (uint8_t)(carry << 7 | c4 >> 1);
            r[0xA005] = (uint8_t)((c4 & 1) << 7 | r[0xA005] >> 1);
            uint16_t vx = (uint16_t)((uint8_t)((r[0xA004] & 1) - 1) << 8 | r[0xA005]);
            game_set_word(g, (uint16_t)(y + 6), vx);
            uint16_t ax = (vx & 0x8000) ? (uint16_t)~vx : vx;
            a = r[0xA007];
            carry = (((a >> 1) ^ a) >> 1) & 1;
            uint8_t c6 = r[0xA006];
            r[0xA006] = (uint8_t)(carry << 7 | c6 >> 1);
            r[0xA007] = (uint8_t)((c6 & 1) << 7 | r[0xA007] >> 1);
            uint16_t vy = (uint16_t)((uint8_t)((r[0xA006] & 3) - 2) << 8 | r[0xA007]);
            game_set_word(g, (uint16_t)(y + 8), vy);
            uint16_t ay = (vy & 0x8000) ? (uint16_t)~vy : vy;
            ay = (uint16_t)(ay >> 1);
            if ((uint16_t)(ay + ax) < 0x016A) break;
        }
        game_set_word(g, y, 0);
    }
    game_set_word(g, 0xA002, FADE);
    r[0xA001] = 0x38;
}

static StepResult after_explosion(Game *g, uint16_t proc);

/* bank 7 $C649 */
static StepResult explosion_tick(Game *g, uint16_t proc)
{
    uint8_t *r = g->ram;
    uint8_t fade = game_read_bank(g, 7, game_word(g, 0xA002));
    r[0xA031] = fade;
    if (!fade) return after_explosion(g, proc);
    for (uint16_t y = PARTICLES; y != PARTICLES_END; y = (uint16_t)(y + 0xA)) {
        uint16_t u = game_word(g, y);
        game_set_word(g, u, 0);
        game_set_word(g, (uint16_t)(u + 0x100), 0);
        uint16_t d = (uint16_t)(game_word(g, (uint16_t)(y + 8)) + game_word(g, (uint16_t)(y + 4)));
        if ((d >> 8) < 0x2A) continue;
        game_set_word(g, (uint16_t)(y + 4), d);
        game_poke(g, (uint16_t)(y + 1), (uint8_t)(d >> 8));
        d = (uint16_t)(game_word(g, (uint16_t)(y + 6)) + game_word(g, (uint16_t)(y + 2)));
        if ((d >> 8) > 0x98) continue;
        game_set_word(g, (uint16_t)(y + 2), d);
        game_poke(g, y, (uint8_t)(d >> 8));
        if (!(d & 0x80)) game_set_word(g, game_word(g, y), 0xBBBB);
        else {
            u = game_word(g, y);
            game_set_word(g, u, 0x0B0B);
            game_set_word(g, (uint16_t)(u + 0x100), 0xB0B0);
        }
    }
    if (!--r[0xA001]) {
        game_set_word(g, 0xA002, (uint16_t)(game_word(g, 0xA002) + 1));
        r[0xA001] = 4;
    }
    return game_sleep_banked(g, proc, 1, EXPLOSION_TICK);
}

/* $DB15: the next player with ships left, and their turn. */
static StepResult next_player(Game *g, uint16_t proc)
{
    uint8_t *r = g->ram;
    uint8_t a = r[0xA08B];
    for (;;) {
        a++;
        if (a > r[0xA08C]) a = 1;
        if (game_peek(g, (uint16_t)((a == 1 ? PLAYER_ONE : PLAYER_TWO) + 7))) break;
    }
    r[0xA08B] = a;
    r[0xA025]++;
    return turn_step(g, proc, TURN);
}

/* $DADE on */
static StepResult after_wave(Game *g, uint16_t proc)
{
    uint8_t *r = g->ram;
    uint8_t a = r[0xA08B];
    if (game_peek(g, (uint16_t)(game_word(g, 0xA08D) + 7))) return next_player(g, proc);
    if (r[0xA08C] != 1) {
        a ^= 3;
        if (game_peek(g, (uint16_t)((a == 1 ? PLAYER_ONE : PLAYER_TWO) + 7))) {
            r[0xA036] = 2;
            text_print(g, 0x3C78, a == 2 ? 0xC0EF : 0xC0F1, 0, 0);
            text_print(g, 0x3E88, 0xC075, 0, 0);
            return sleep(g, 0x60, DEATH_NEXT);
        }
    }
    /* $DB2C: game over */
    r[STATE] = 0xFF;
    r[0xA036] = 2;
    text_print(g, 0x3E80, 0xC075, 0, 0);
    r[0xA0B3] = 0;
    game_send_sound(g, 0x13);
    return sleep(g, 0x28, GAME_OVER_DONE);
}

/* $DAC9 */
static StepResult after_explosion(Game *g, uint16_t proc)
{
    score_draw(g);
    g->ram[0xA0B3] = 0;
    game_send_sound(g, 0x13);
    if (!wave_enemies_left(g)) return wave_end(g, proc, 0xDADB);
    return after_wave(g, proc);
}

/* $DE3B: a bonus humanoid drawn at `x` and scored, the next place kept at +7. */
static StepResult bonus_humanoid(Game *g, uint16_t proc, uint16_t x)
{
    glyph_draw(g, 0xF915, x);
    x = (uint16_t)(x + 0x0400);
    uint8_t wave = (uint8_t)game_peek(g, (uint16_t)(game_word(g, 0xA08D) + 8));
    if (wave >= 5) wave = 5;
    if (score_add(g, (uint16_t)(0x0100 | (uint8_t)(wave << 4)))) return STEP_UNPORTED;
    game_set_word(g, (uint16_t)(proc + 7), x);
    return sleep(g, 4, WAVE_END_BONUS);
}

StepResult wave_end(Game *g, uint16_t proc, uint16_t back)
{
    uint8_t *r = g->ram;
    r[0xA026] = 0;
    game_set_word(g, (uint16_t)(proc + 0xD), back);
    clear_playfield(g);
    r[0xA036] = 2;
    text_print(g, 0x3850, 0xC0F9, 0, 0);
    uint16_t block = game_word(g, 0xA08D);
    uint8_t wave = (uint8_t)game_peek(g, (uint16_t)(block + 8));
    uint8_t bcd = (uint8_t)(((wave / 10) % 10) << 4 | wave % 10); /* $DDD5: DAA wraps at 100 */
    number_print(g, game_word(g, 0xA050), bcd);
    text_print(g, 0x3D60, 0xC0FB, 0, 0);
    text_print(g, 0x3C90, 0xC0F3, 0, 0);
    uint8_t times = wave > 5 ? 5 : wave;
    number_print(g, game_word(g, 0xA050), (uint16_t)(times << 8));
    game_poke(g, (uint16_t)(proc + 9), r[0xA0FA]);
    if (!r[0xA0FA]) {
        wave_setup(g, block);
        return sleep(g, 0x80, WAVE_END_DONE);
    }
    return bonus_humanoid(g, proc, 0x3CA0); /* the first in X: +7 not yet written */
}

StepResult death_step(Game *g, uint16_t proc, uint16_t resume)
{
    uint8_t *r = g->ram;
    switch (resume) {
    case HYPERSPACE:
        if (r[STATE] & 0xFD) {
            game_end_process(g);
            return STEP_DONE;
        }
        r[STATE] = 0x77;
        clear_playfield(g);
        return sleep(g, 15, HYPERSPACE_JUMP);
    case HYPERSPACE_JUMP: {
        while (g->objects.shots)
            if (objects_free_shot(&g->objects, g->objects.shots)) return STEP_UNPORTED;
        r[0xA099] = 0;
        uint16_t d = game_word(g, 0xA0DF);
        game_set_word(g, 0xA020, d);
        game_set_word(g, 0xA022, d);
        int right = d & 1;
        uint16_t screen = right ? 0x2000 : 0x7000;
        game_set_word(g, 0xA0C3, screen);
        game_set_word(g, 0xA0BB, right ? 0x0300 : 0xFD00);
        uint8_t line = (uint8_t)((r[0xA0E0] >> 1) + 0x2A);
        r[0xA0C5] = line;
        game_set_word(g, 0xA0C1, (uint16_t)((screen & 0xFF00) | line));
        r[0xA0C9] = 0;
        game_set_word(g, 0xA0C7, 0);
        game_set_word(g, 0xA0CA, 0);
        r[0xA036] = 7;
        terrain_start(g);
        set_state(g, 0x50);
        uint16_t obj = objects_alloc(&g->objects);
        Object *o = objects_at(&g->objects, obj);
        if (!o) return STEP_UNPORTED;
        objects_setup(&g->objects, obj, g->terrain_u, 0xF9C1, 0xEDBC, 0); /* +6: U as the terrain code left it */
        o->vx = o->vy = 0;
        o->y = game_word(g, 0xA0C5);
        o->x = (uint16_t)((game_word(g, 0xA0C3) >> 2) + game_word(g, 0xA020));
        if (r[0xA0BB] & 0x80) o->picture = 0xF9CB;
        game_set_word(g, (uint16_t)(proc + 7), obj);
        appear_start(g, obj);
        return sleep(g, 0x28, HYPERSPACE_DONE);
    }
    case HYPERSPACE_DONE:
        if (game_kill_object(g, game_word(g, (uint16_t)(proc + 7)))) return STEP_UNPORTED;
        set_state(g, 0);
        if (r[0xA0E1] > 0xC0) {
            return death_step(g, proc, DEATH);
        }
        game_end_process(g);
        return STEP_DONE;
    case DEATH: {
        set_state(g, 0x58);
        game_set_word(g, 0xA022, game_word(g, 0xA020));
        game_clear(g, game_word(g, 0xA0BF), 8, 6);
        wave_save(g);
        game_sound(g, 0xD4B5);
        uint16_t pic = (r[0xA0BB] & 0x80) ? 0xF9CB : 0xF9C1;
        game_set_word(g, (uint16_t)(proc + 7), FLASHES);
        white_copy(g, pic, WHITE_SHIP);
        game_set_word(g, (uint16_t)(proc + 0xB), WHITE_SHIP);
    }
        /* fall through */
    case DEATH_ERASE:
        glyph_clear(g, game_word(g, (uint16_t)(proc + 0xB)), game_word(g, 0xA0C1));
        return sleep(g, 2, DEATH_DRAW);
    case DEATH_DRAW: {
        glyph_draw(g, game_word(g, (uint16_t)(proc + 0xB)), game_word(g, 0xA0C1));
        uint16_t x = game_word(g, (uint16_t)(proc + 7));
        uint8_t a = game_read(g, x);
        if (a) {
            r[0xA031] = a;
            r[0xA026] = 0;
            game_set_word(g, (uint16_t)(proc + 7), (uint16_t)(x + 1));
            return sleep(g, 2, DEATH_ERASE);
        }
        r[STATE] = 0x7F;
        r[0xA026] = 0xFF;
        return sleep(g, 2, DEATH_WHITE);
    }
    case DEATH_WHITE:
        r[0xA026] = 0;
        kill_others(g);
        r[0xA036] = 7;
        explosion_setup(g, proc, (uint16_t)(game_word(g, 0xA0C1) + 0x0403), 0xDAC9);
        return game_sleep_banked(g, proc, 1, EXPLOSION_TICK);
    case BANKED_RESUME: {
        uint8_t bank = (uint8_t)game_peek(g, (uint16_t)(proc + 0xC));
        uint16_t at = game_word(g, (uint16_t)(proc + 0xD));
        r[0xA036] = bank;
        if (bank == 7 && at == EXPLOSION_TICK) return explosion_tick(g, proc);
        if (bank == 1 && attract_ported(at)) return attract_step(g, proc, at);
        g->unported = BANKED_RESUME;
        return STEP_UNPORTED;
    }
    case DEATH_NEXT:
        return next_player(g, proc);
    case ATTRACT:
    case GAME_OVER_DONE: /* $D825 */
        g->ram[0xA036] = 1;
        return attract_step(g, proc, 0xC006);
    case WAVE_END_BONUS: { /* $DE66: the next bonus humanoid */
        uint8_t n = (uint8_t)(game_peek(g, (uint16_t)(proc + 9)) - 1);
        game_poke(g, (uint16_t)(proc + 9), n);
        if (!n) {
            wave_setup(g, game_word(g, 0xA08D));
            return sleep(g, 0x80, WAVE_END_DONE);
        }
        return bonus_humanoid(g, proc, game_word(g, (uint16_t)(proc + 7)));
    }
    case WAVE_END_DONE: {
        uint16_t back = game_word(g, (uint16_t)(proc + 0xD));
        if (back == 0xDD02) {
            uint16_t block = game_word(g, 0xA08D);
            game_poke(g, (uint16_t)(block + 7), (uint8_t)(game_peek(g, (uint16_t)(block + 7)) + 1));
            return turn_step(g, proc, TURN_RESET);
        }
        if (back == 0xDADB) {
            clear_playfield(g);
            return after_wave(g, proc);
        }
        return STEP_UNPORTED;
    }
    }
    return STEP_UNPORTED;
}
