#include "sim/lander.h"

#include "sim/explosions.h"
#include "sim/humanoids.h"
#include "sim/shots.h"

#define HUMANOID_NEXT 0xA09B   /* the slot the last search stopped at */
#define TERRAIN 0xB300
#define SHIP_X 0xA0CC          /* the ship's world x */
#define SHIP_LINE 0xA0C0
#define RANDOM 0xA0DF
#define SCRATCH 0xA073         /* <$73 and <$75: the ROM's scratch bytes */
#define SCRATCH_75 0xA075
#define LANDERS 0xA112
#define MUTANTS 0xA115
#define LANDERS_LEFT 0xA0FB    /* landers that left with no humanoid */
#define WAVE_JITTER 0xA107
#define WAVE_MUTANT_Y 0xA108   /* word */
#define WAVE_MUTANT_X 0xA10A
#define WAVE_MUTANT_SHOTS 0xA10B
#define WAVE_DESCENT 0xA103    /* word */
#define WAVE_SHOT_TICKS 0xA105
#define ON_GROUND 0x70         /* the low byte of $ED70 */
#define HOVER 0x32             /* lines above the terrain */
#define TOP 0x32               /* where a lander with a humanoid stops rising */
#define PICTURE_HIDDEN 0xF8EC
#define PICTURE_FIRST 0xF985
#define PICTURE_LAST 0xF999
#define PICTURE_STEP 10
#define PICTURE_MUTANT 0xF8CE
#define MUTANT_DATA 0xCC33
#define MUTANT_HIT 0xEF6D
#define CARRYING_HIT 0xF1E0
#define SOUND_SHOT 0xD525
#define SOUND_MUTANT_SHOT 0xD52A
#define SOUND_GRAB 0xD50C
#define SOUND_TOP 0xD511

static Object *object_of(Game *g, uint16_t proc) { return objects_at(&g->objects, game_word(g, proc + 7)); }
static Object *humanoid_of(Game *g, uint16_t proc) { return objects_at(&g->objects, game_word(g, proc + 9)); }
static uint8_t line_of(const Object *o) { return (uint8_t)(o->y >> 8); }

/* Still in the table, and on the ground ($ED70). */
static int humanoid_here(Game *g, uint16_t proc)
{
    return game_word(g, game_word(g, proc + 0xB)) && game_peek(g, game_word(g, proc + 9) + 9) == ON_GROUND;
}

static StepResult sleep(Game *g, uint8_t ticks, uint16_t resume)
{
    processes_sleep(&g->processes, ticks, resume);
    return STEP_DONE;
}

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

/* $F15E */
StepResult mutant_step(Game *g, uint16_t proc)
{
    uint16_t obj = game_word(g, proc + 7);
    Object *o = objects_at(&g->objects, obj);
    if (!o) return STEP_UNPORTED;
    uint16_t ship = game_word(g, SHIP_X);
    int8_t speed = (int8_t)g->ram[WAVE_MUTANT_X];
    if ((int16_t)ship < (int16_t)o->x) speed = (int8_t)-speed;
    o->vx = (uint16_t)(int16_t)speed;
    uint16_t vy = game_word(g, WAVE_MUTANT_Y);
    uint8_t ship_line = g->ram[SHIP_LINE], line = line_of(o);
    if ((uint16_t)(ship - o->x + 0x017C) <= 0x0700) {
        /* near the ship: towards its line */
        o->vy = ship_line >= line ? vy : (uint16_t)~vy;
        if (!o->screen) return sleep(g, 3, MUTANT); /* off screen: no jitter, no shot */
    } else {
        /* far: away from it when within 8 lines */
        uint8_t d = (uint8_t)(ship_line - line);
        if (ship_line <= line) o->vy = (int8_t)d > -8 ? vy : 0;
        else o->vy = d > 8 ? 0 : (uint16_t)~vy;
    }
    uint8_t jitter = g->ram[WAVE_JITTER];
    if (!(g->ram[RANDOM] & 0x80)) jitter = (uint8_t)-jitter;
    uint8_t to = (uint8_t)(jitter + line_of(o));
    if (to < 0x2A) to = 0xF0;
    o->y = (uint16_t)(to << 8 | (o->y & 0xFF));
    uint8_t n = (uint8_t)(game_peek(g, proc + 9) - 1);
    game_poke(g, proc + 9, n);
    if (!n) {
        game_poke(g, proc + 9, game_random_upto(g, g->ram[WAVE_MUTANT_SHOTS]));
        if (shot_fire(g, obj, proc)) game_sound(g, SOUND_MUTANT_SHOT);
    }
    return sleep(g, 3, MUTANT);
}

/* $F13E */
static StepResult mutate(Game *g, uint16_t proc)
{
    g->ram[LANDERS]--;
    g->ram[MUTANTS]++;
    Object *o = object_of(g, proc);
    if (!o) return STEP_UNPORTED;
    o->flags = 0;
    o->picture = PICTURE_MUTANT;
    o->data = MUTANT_DATA;
    o->hit = MUTANT_HIT;
    game_poke(g, proc + 9, g->ram[WAVE_MUTANT_SHOTS]);
    return mutant_step(g, proc);
}

/* $F016 on: a new humanoid if need be, then keep above the terrain. */
static StepResult seek(Game *g, uint16_t proc, int pick)
{
    uint16_t obj = game_word(g, proc + 7);
    Object *o = objects_at(&g->objects, obj);
    if (!o) return STEP_UNPORTED;
    if (pick) {
        o->flags &= 0xFE;
        if (!pick_humanoid(g, proc)) return mutate(g, proc);
    }
    uint8_t above = (uint8_t)(terrain(g, o->x) - HOVER), line = line_of(o);
    uint16_t descent = game_word(g, WAVE_DESCENT);
    if (above > line) o->vy = descent;
    else if ((int8_t)(uint8_t)(above - line) < -20) o->vy = (uint16_t)~descent;
    else o->vy = 0;
    if (o->picture != PICTURE_HIDDEN) {
        shot_tick(g, proc, obj);
        uint16_t next = (uint16_t)(o->picture + PICTURE_STEP);
        o->picture = next > PICTURE_LAST ? PICTURE_FIRST : next;
    }
    return sleep(g, 6, LANDER_SEEK);
}

/* $F101 */
StepResult lander_top(Game *g, uint16_t proc)
{
    uint16_t obj = game_word(g, proc + 7);
    Object *o = objects_at(&g->objects, obj), *h = humanoid_of(g, proc);
    if (!o || !h) return STEP_UNPORTED;
    if (!game_word(g, game_word(g, proc + 0xB))) {
        /* the humanoid has gone: the lander leaves */
        if (game_kill_object(g, obj)) return STEP_UNPORTED;
        g->ram[LANDERS]--;
        g->ram[LANDERS_LEFT]++;
        game_end_process(g);
        return STEP_DONE;
    }
    o->vy = h->vy = 0;
    if (line_of(h) <= line_of(o)) {
        uint16_t human = game_word(g, proc + 9);
        game_set_word(g, HIT_POINT_ADDR, (uint16_t)(h->screen + 0x100));
        if (humanoid_kill(g, human)) return STEP_UNPORTED;
        return mutate(g, proc);
    }
    h->y = (uint16_t)(h->y - 0x100);
    game_send_sound(g, 0); /* B is 0 here: the ROM's LDA #$12 does not reach $D539 */
    return sleep(g, 1, LANDER_TOP);
}

/* $F0E6 */
StepResult lander_rise(Game *g, uint16_t proc)
{
    uint16_t obj = game_word(g, proc + 7);
    Object *o = objects_at(&g->objects, obj);
    if (!o) return STEP_UNPORTED;
    if (line_of(o) > TOP) {
        shot_tick(g, proc, obj);
        return sleep(g, 4, LANDER_RISE);
    }
    game_sound(g, SOUND_TOP);
    return lander_top(g, proc);
}

/* $F074 */
StepResult lander_align(Game *g, uint16_t proc)
{
    uint16_t obj = game_word(g, proc + 7);
    Object *o = objects_at(&g->objects, obj), *h = humanoid_of(g, proc);
    if (!o || !h) return STEP_UNPORTED;
    if (!humanoid_here(g, proc)) return seek(g, proc, 1);
    uint16_t hx = h->x & 0xFFE0, ox = o->x & 0xFFE0;
    game_set_word(g, SCRATCH_75, hx);
    if (ox != hx) o->x = (uint16_t)(o->x + ((int16_t)ox < (int16_t)hx ? 0x0020 : 0xFFE0));
    uint8_t want = (uint8_t)(line_of(h) - 0x0C);
    if (want == line_of(o)) {
        if ((uint16_t)(o->x + 0x40 - h->x) <= 0x80) {
            /* grab it */
            uint16_t up = (uint16_t)~game_word(g, WAVE_DESCENT);
            o->hit = CARRYING_HIT;
            o->vy = h->vy = up;
            game_sound(g, SOUND_GRAB);
            h->hit = HUMANOID_CARRIED;
            return lander_rise(g, proc);
        }
    } else {
        uint16_t d = game_word(g, WAVE_DESCENT);
        o->y = (uint16_t)(o->y + (want >= line_of(o) ? d : (uint16_t)~d));
    }
    shot_tick(g, proc, obj);
    return sleep(g, 1, LANDER_ALIGN);
}

/* $EFF6 */
StepResult lander_seek(Game *g, uint16_t proc)
{
    Object *o = object_of(g, proc);
    if (!o) return STEP_UNPORTED;
    if (!humanoid_here(g, proc)) return seek(g, proc, 1);
    g->ram[SCRATCH] = (uint8_t)(o->x >> 8 & 0xFC);
    if (g->ram[SCRATCH] != (game_peek(g, game_word(g, proc + 9) + 0xA) & 0xFC)) return seek(g, proc, 0);
    /* $F065: right above it: the capture */
    o->flags++;
    o->vx = o->vy = 0;
    o->picture = PICTURE_FIRST;
    return lander_align(g, proc);
}
