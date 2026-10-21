#include "sim/background.h"

#include "sim/score.h"

#define STATE 0xA0BA
#define SCROLL 0xA020
#define SCRATCH 0xA073
#define BANK 0xA036
#define COLOURS 0xE799
#define SHOT_COUNT 0xA099
#define DOTS 0xB05D
#define DOTS_END 0xA097
#define TERRAIN_DOTS 0xB125
#define TERRAIN_TABLE 0xCD69

static StepResult sleep(Game *g, uint8_t ticks, uint16_t resume)
{
    processes_sleep(&g->processes, ticks, resume);
    return STEP_DONE;
}

void game_clear(Game *g, uint16_t at, uint8_t w, uint8_t h)
{
    for (int c = 0; c < w; c++)
        for (int r = 0; r < h; r++) game_poke(g, (uint16_t)(at + c * 0x100 + r), 0);
}

/* Move list members by `test` from the list at `from` to the head of the list at `to`. */
static void sort_list(Game *g, uint16_t from, uint16_t to, int far, int move)
{
    uint16_t base = (uint16_t)(game_word(g, SCROLL) - 0x0C80);
    game_set_word(g, SCRATCH, base);
    uint16_t prev = from;
    for (uint16_t x = game_word(g, prev); x; x = game_word(g, prev)) {
        Object *o = objects_at(&g->objects, x);
        if (!o) return;
        if (move) {
            uint16_t y = (uint16_t)((uint16_t)(o->vy << 3) + o->y);
            uint8_t line = (uint8_t)(y >> 8);
            if (line < 0x2A) line = 0xF0;
            if (line > 0xF0) line = 0x2A;
            o->y = (uint16_t)(line << 8 | (y & 0xFF));
            o->x = (uint16_t)((uint16_t)(o->vx << 3) + o->x);
        }
        int is_far = (uint16_t)(o->x - base) >= 0x3E80;
        if (is_far == far) {
            game_set_word(g, prev, o->link); /* unlink */
            o->link = game_word(g, to);
            game_set_word(g, to, x);
            continue; /* prev stays */
        }
        prev = x;
    }
}

/* $E56A */
static void age_shots(Game *g)
{
    uint16_t prev = LIST_SHOTS;
    for (uint16_t x = game_word(g, prev); x; x = game_word(g, prev)) {
        Object *o = objects_at(&g->objects, x);
        if (!o) return;
        if (o->extra[1] && --o->extra[0]) {
            prev = x;
            continue;
        }
        game_set_word(g, prev, o->link);
        o->link = g->objects.free_list;
        g->objects.free_list = x;
        game_erase_object(g, x);
        g->ram[SHOT_COUNT]--;
    }
}

void scanner_draw(Game *g)
{
    uint16_t end = game_word(g, DOTS_END), x = DOTS;
    do { /* the ROM clears a group of 4 before it compares */
        for (int i = 0; i < 8; i += 2) game_set_word(g, game_word(g, (uint16_t)(x + i)), 0);
        x = (uint16_t)(x + 8);
    } while (x < end);
    uint16_t ship = game_word(g, end);
    if (ship) {
        game_set_word(g, ship, 0);
        game_poke(g, (uint16_t)(ship + 2), 0);
        game_set_word(g, (uint16_t)(ship - 0x100), 0);
    }
    uint16_t base = (uint16_t)(game_word(g, SCROLL) - 0x6D40);
    game_set_word(g, SCRATCH, base);
    uint16_t u = (uint16_t)(TERRAIN_TABLE + (base >> 10) * 3);
    if (!(g->ram[STATE] & 0x02)) {
        uint16_t y = TERRAIN_DOTS;
        for (uint8_t column = 0x30; column != 0x70; column++, y = (uint16_t)(y + 2), u = (uint16_t)(u + 3)) {
            game_set_word(g, game_word(g, y), 0);
            uint8_t line = game_read_bank(g, 1, u);
            uint16_t colour = (uint16_t)(game_read_bank(g, 1, (uint16_t)(u + 1)) << 8 | game_read_bank(g, 1, (uint16_t)(u + 2)));
            uint16_t at = (uint16_t)(column << 8 | line);
            game_set_word(g, y, at);
            game_set_word(g, at, colour);
        }
    }
    game_set_word(g, 0x4C09, 0x9090);
    game_set_word(g, 0x4C26, 0x9090);
    game_set_word(g, 0x5309, 0x0909);
    game_set_word(g, 0x5326, 0x0909);
    uint16_t list = DOTS;
    static const uint16_t lists[2] = { LIST_SCREEN, LIST_OFF_SCREEN };
    for (int l = 0; l < 2; l++)
        for (uint16_t x = game_word(g, lists[l]); x; x = game_word(g, x)) {
            uint16_t d = (uint16_t)(game_word(g, (uint16_t)(x + 0xA)) - base);
            uint16_t at = (uint16_t)((((d >> 8) >> 2) << 8 | (uint8_t)(game_peek(g, (uint16_t)(x + 0xC)) >> 3)) + 0x3007);
            game_set_word(g, list, at);
            game_set_word(g, at, game_word(g, (uint16_t)(x + 0x12)));
            list = (uint16_t)(list + 2);
        }
    game_set_word(g, DOTS_END, list);
    uint16_t at = (uint16_t)(((g->ram[0xA0BF] >> 4) << 8 | g->ram[0xA0C0] >> 3) + 0x4B07);
    game_set_word(g, list, at);
    game_set_word(g, at, 0x9099);
    game_poke(g, (uint16_t)(at + 2), 0x90);
    game_poke(g, (uint16_t)(at - 0xFF), 0x09);
}

StepResult background_step(Game *g, uint16_t proc, uint16_t resume)
{
    uint8_t *r = g->ram;
    switch (resume) {
    case COLOUR_CYCLE:
        r[0xA0B6] = 0;
        /* fall through */
    case COLOUR_CYCLE_NEXT: {
        uint8_t c = game_read(g, (uint16_t)(COLOURS + r[0xA0B6]));
        if (!c) return background_step(g, proc, COLOUR_CYCLE);
        r[0xA0B6]++;
        r[0xA027] = c;
        return sleep(g, 2, COLOUR_CYCLE_NEXT);
    }
    case POINTERS: {
        uint16_t a = (uint16_t)(game_word(g, 0xA09F) + 1);
        game_set_word(g, 0xA09F, a > 0xA182 ? 0xA162 : a);
        a = (uint16_t)(game_word(g, 0xA0A8) + 1);
        game_set_word(g, 0xA0A8, a > 0xA1BA ? 0xA1A2 : a);
        return sleep(g, 4, POINTERS);
    }
    case PALETTE_CYCLE:
        game_set_word(g, (uint16_t)(proc + 7), 0xF45B);
        return sleep(g, 6, PALETTE_CYCLE_NEXT);
    case PALETTE_CYCLE_NEXT: {
        uint16_t x = game_word(g, (uint16_t)(proc + 7));
        r[0xA033] = game_read(g, x);
        r[0xA034] = game_read(g, (uint16_t)(x + 1));
        r[0xA035] = game_read(g, (uint16_t)(x + 2));
        x = (uint16_t)(x + 3);
        if (x >= 0xF464) return background_step(g, proc, PALETTE_CYCLE);
        game_set_word(g, (uint16_t)(proc + 7), x);
        return sleep(g, 6, PALETTE_CYCLE_NEXT);
    }
    case FLASH:
        r[0xA030] = 0xFF;
        r[0xA032] = 0;
        return sleep(g, 3, FLASH_NEXT);
    case FLASH_NEXT: {
        uint8_t c = game_read(g, (uint16_t)(COLOURS + (r[0xA0DF] & 0x1F)));
        r[0xA030] = r[0xA032] = c;
        game_set_word(g, 0xA0A6, game_word(g, 0xA0A6) == 0xCCB0 ? 0xCCBC : 0xCCB0);
        return sleep(g, 6, FLASH);
    }
    case SCORE_BLINK:
        if (r[0xA08A]) {
            game_end_process(g);
            return STEP_DONE;
        }
        game_clear(g, r[0xA08B] == 1 ? 0x0F1C : 0x711C, 0x18, 0x08);
        return sleep(g, 8, SCORE_BLINK_DRAW);
    case SCORE_BLINK_DRAW:
        score_draw(g);
        return sleep(g, 0x0C, SCORE_BLINK);
    case HOUSEKEEPING:
        sort_list(g, LIST_OFF_SCREEN, LIST_SCREEN, 0, 1);
        return sleep(g, 2, HOUSEKEEPING_2);
    case HOUSEKEEPING_2:
        sort_list(g, LIST_SCREEN, LIST_OFF_SCREEN, 1, 0);
        age_shots(g);
        return sleep(g, 2, HOUSEKEEPING_3);
    case HOUSEKEEPING_3:
        r[BANK] = 1; /* $F50B */
        scanner_draw(g);
        return sleep(g, 4, HOUSEKEEPING);
    }
    return STEP_UNPORTED;
}
