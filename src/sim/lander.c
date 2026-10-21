#include "sim/lander.h"

#include "sim/shots.h"

#define HUMANOIDS 0xA11A       /* 32 object words */
#define HUMANOIDS_END 0xA15A
#define HUMANOID_NEXT 0xA09B   /* the slot the last search stopped at */
#define HUMANOIDS_LEFT 0xA0FA
#define TERRAIN 0xB300
#define WAVE_DESCENT 0xA103    /* word */
#define WAVE_SHOT_TICKS 0xA105
#define ON_GROUND 0x70         /* the low byte of $ED70, a humanoid's handler on the ground */
#define HOVER 0x32             /* lines above the terrain */
#define PICTURE_HIDDEN 0xF8EC
#define PICTURE_FIRST 0xF985
#define PICTURE_LAST 0xF999
#define PICTURE_STEP 10
#define SLEEP 6
#define SOUND_SHOT 0xD525
#define SCRATCH 0xA073         /* <$73: the ROM's scratch byte */

/* $EF78: the next humanoid in the table after the last one picked. */
static int pick_humanoid(Game *g, uint16_t proc)
{
    if (!g->ram[HUMANOIDS_LEFT]) return 0;
    uint16_t start = game_word(g, HUMANOID_NEXT), x = start;
    do {
        x = (uint16_t)(x + 2);
        if (x >= HUMANOIDS_END) x = HUMANOIDS;
        uint16_t h = game_word(g, x);
        if (h) {
            game_set_word(g, HUMANOID_NEXT, x);
            game_set_word(g, proc + 9, h);
            game_set_word(g, proc + 0xB, x);
            return 1;
        }
    } while (x != start);
    return 0;
}

/* $ED59: the terrain's line under world x. */
static uint8_t terrain(const Game *g, uint16_t x) { return (uint8_t)game_peek(g, (uint16_t)(TERRAIN + (x >> 6))); }

/* $EEFE: count down to the next shot. */
static void shot_tick(Game *g, uint16_t proc, uint16_t obj)
{
    uint8_t n = (uint8_t)(game_peek(g, proc + 0xD) - 1);
    game_poke(g, proc + 0xD, n);
    if (n) return;
    game_poke(g, proc + 0xD, game_random_upto(g, g->ram[WAVE_SHOT_TICKS]));
    if (shot_fire(g, obj, proc)) game_sound(g, SOUND_SHOT);
}

StepResult lander_seek(Game *g, uint16_t proc)
{
    uint16_t obj = game_word(g, proc + 7), human = game_word(g, proc + 9);
    Object *o = objects_at(&g->objects, obj);
    if (!o) return STEP_UNPORTED;
    int seek = 0;
    if (game_word(g, game_word(g, proc + 0xB)) && game_peek(g, human + 9) == ON_GROUND) {
        g->ram[SCRATCH] = (uint8_t)(o->x >> 8 & 0xFC);
        if (g->ram[SCRATCH] == (game_peek(g, human + 0xA) & 0xFC)) return STEP_UNPORTED; /* capture */
        seek = 1;
    }
    if (!seek) {
        o->flags &= 0xFE;
        if (!pick_humanoid(g, proc)) return STEP_UNPORTED; /* becomes a mutant */
    }

    uint8_t above = (uint8_t)(terrain(g, o->x) - HOVER), line = (uint8_t)(o->y >> 8);
    uint16_t descent = game_word(g, WAVE_DESCENT);
    if (above > line) o->vy = descent;
    else if ((int8_t)(uint8_t)(above - line) < -20) o->vy = (uint16_t)~descent;
    else o->vy = 0;

    if (o->picture != PICTURE_HIDDEN) {
        shot_tick(g, proc, obj);
        uint16_t next = (uint16_t)(o->picture + PICTURE_STEP);
        o->picture = next > PICTURE_LAST ? PICTURE_FIRST : next;
    }
    processes_sleep(&g->processes, SLEEP, LANDER_SEEK);
    return STEP_DONE;
}
