#include "sim/player.h"

#include "sim/background.h"
#include "sim/enemies.h"
#include "sim/score.h"

#define PLAYERS 0xA08C
#define BOMB_GOING 0xA09A
#define REVERSING 0xA0AF
#define FACING 0xA0BD
#define NEW_FACING 0xA0BB
#define IN0 0xA07B
#define BACKGROUND 0xA026

/* $F522 at screen address `at` (the picture's even data). */
static void draw(Game *g, uint16_t picture, uint16_t at)
{
    int w = game_read(g, picture), h = game_read(g, (uint16_t)(picture + 1));
    uint16_t src = (uint16_t)(game_read(g, (uint16_t)(picture + 2)) << 8 | game_read(g, (uint16_t)(picture + 3)));
    for (int c = 0; c < w; c++)
        for (int r = 0; r < h; r++) game_poke(g, (uint16_t)(at + c * 0x100 + r), game_read(g, (uint16_t)(src + c * h + r)));
}

static void icons(Game *g, uint16_t at, uint8_t n, uint8_t max, uint8_t w, uint8_t h, uint16_t picture, uint16_t step)
{
    if (n > max) n = max;
    game_clear(g, at, w, h);
    for (; n; n--, at = (uint16_t)(at + step)) draw(g, picture, at);
}

void hud_ships(Game *g)
{
    icons(g, 0x0F14, (uint8_t)game_peek(g, 0xA1C9), 5, 0x20, 0x06, 0xF9D5, 0x0600);
    if (g->ram[PLAYERS] != 1) icons(g, 0x7114, (uint8_t)game_peek(g, 0xA206), 5, 0x20, 0x06, 0xF9D5, 0x0600);
}

void hud_bombs(Game *g)
{
    icons(g, 0x291B, (uint8_t)game_peek(g, 0xA1CB), 3, 0x03, 0x0B, 0xF9D9, 0x0004);
    if (g->ram[PLAYERS] != 1) icons(g, 0x8B1B, (uint8_t)game_peek(g, 0xA208), 3, 0x03, 0x0B, 0xF9D9, 0x0004);
}

void wave_save(Game *g)
{
    uint16_t u = score_player(g);
    for (int i = 0; i < 0x33; i++) game_poke(g, (uint16_t)(u + 0xA + i), 0);
    game_poke(g, (uint16_t)(u + 0xA), g->ram[0xA0FA]);
    u = (uint16_t)(u + 0xB);
    for (uint16_t x = 0xA0FB; x != 0xA112; x++, u++) {
        uint8_t a = g->ram[x];
        if (x + 1 <= 0xA100) a = (uint8_t)(a + g->ram[x + 1 + 0x16]);
        game_poke(g, u, a);
    }
}

static StepResult sleep(Game *g, uint8_t ticks, uint16_t resume)
{
    processes_sleep(&g->processes, ticks, resume);
    return STEP_DONE;
}

static StepResult end(Game *g)
{
    game_end_process(g);
    return STEP_DONE;
}

StepResult player_step(Game *g, uint16_t proc, uint16_t resume)
{
    uint8_t *r = g->ram;
    switch (resume) {
    case SMART_BOMB: {
        uint16_t block = game_word(g, 0xA08D);
        if (r[BOMB_GOING] || !game_peek(g, (uint16_t)(block + 9))) return end(g);
        r[BOMB_GOING]++;
        game_poke(g, (uint16_t)(block + 9), (uint8_t)(game_peek(g, (uint16_t)(block + 9)) - 1));
        hud_bombs(g);
        game_sound(g, 0xD4D2);
        for (uint16_t x = g->objects.screen; x;) {
            Object *o = objects_at(&g->objects, x);
            if (!o) return STEP_UNPORTED;
            if (o->screen && o->flags < 2) {
                if (enemy_hit(g, o->hit, x) != 1) return STEP_UNPORTED;
                x = g->objects.screen;
            } else {
                x = o->link;
            }
        }
        game_poke(g, (uint16_t)(proc + 7), 4);
        r[BACKGROUND] = (uint8_t)~r[BACKGROUND];
        return sleep(g, 2, SMART_BOMB_FLASH);
    }
    case SMART_BOMB_FLASH: {
        uint8_t n = (uint8_t)(game_peek(g, (uint16_t)(proc + 7)) - 1);
        game_poke(g, (uint16_t)(proc + 7), n);
        if (n) {
            r[BACKGROUND] = (uint8_t)~r[BACKGROUND];
            return sleep(g, 2, SMART_BOMB_FLASH);
        }
        return sleep(g, 10, SMART_BOMB_HELD);
    }
    case SMART_BOMB_HELD:
        return sleep(g, 10, r[IN0] & 0x04 ? SMART_BOMB_HELD : SMART_BOMB_DONE);
    case SMART_BOMB_DONE:
        r[BOMB_GOING] = 0;
        return end(g);
    case REVERSE:
        if (r[REVERSING]) return end(g);
        r[REVERSING]++;
        game_set_word(g, NEW_FACING, (uint16_t)-game_word(g, FACING));
        return sleep(g, 2, REVERSE_HELD);
    case REVERSE_HELD:
        return r[IN0] & 0x40 ? sleep(g, 2, REVERSE_HELD) : sleep(g, 5, REVERSE_DONE);
    case REVERSE_DONE:
        r[REVERSING] = 0;
        return end(g);
    }
    return STEP_UNPORTED;
}
