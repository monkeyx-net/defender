#include "sim/humanoids.h"

#include "sim/explosions.h"
#include "sim/score.h"

#define SLOTS_SEARCHED 0x40     /* the ROM looks this many words on */
#define PICTURE_DEAD 0xF8D8
#define SOUND_DEAD 0xD4E4

int humanoid_remove(Game *g, uint16_t obj)
{
    uint16_t at = HUMANOIDS;
    int n = SLOTS_SEARCHED;
    while (game_word(g, at) != obj) {
        at = (uint16_t)(at + 2);
        if (!--n) return -1;
    }
    game_set_word(g, at, 0);
    if (!--g->ram[HUMANOIDS_LEFT]) processes_create(&g->processes, PLANET_GONE, 0, 0);
    return 0;
}

int humanoid_kill(Game *g, uint16_t obj)
{
    if (humanoid_remove(g, obj)) return -1;
    if (game_kill_object(g, obj)) return -1;
    Object *o = objects_at(&g->objects, obj);
    o->picture = PICTURE_DEAD;
    o->x = (uint16_t)(o->x - 0x40);
    explosion_start(g, obj);
    game_sound(g, SOUND_DEAD);
    return 0;
}

StepResult humanoid_fall(Game *g, uint16_t proc)
{
    uint16_t obj = game_word(g, (uint16_t)(proc + 7));
    Object *o = objects_at(&g->objects, obj);
    if (!o) return STEP_UNPORTED;
    uint16_t vy = (uint16_t)(o->vy + 8);
    if (vy < 0x0300) o->vy = vy;
    if (game_terrain(g, o->x) > (uint8_t)(o->y >> 8)) {
        processes_sleep(&g->processes, 4, HUMANOID_FALL);
        return STEP_DONE;
    }
    if (o->vy <= 0x00E0) {
        /* lands: on the ground again, 250 */
        uint16_t bonus = processes_create(&g->processes, BONUS_250, 0, 0);
        if (!bonus) return STEP_UNPORTED;
        game_set_word(g, (uint16_t)(bonus + 7), obj);
        o->owner = 0;
        o->vy = 0;
        o->hit = HUMANOID_ON_GROUND;
        game_sound(g, 0xD4DF);
        game_end_process(g);
        return STEP_DONE;
    }
    game_set_word(g, 0xA0F8, (uint16_t)(o->screen + 0x0107));
    if (humanoid_kill(g, obj)) return STEP_UNPORTED;
    game_end_process(g);
    return STEP_DONE;
}

#define SHIP_LINE_HI 0xA0C5     /* the ship's y, high byte */
#define SHIP_X 0xA0CC
#define SHIP_SPEED 0xA0C7
#define TOUCHED 0xA0DE          /* <$DE: set while the ship's collisions run */
#define RANDOM 0xA0DF
#define WALK_END 0xA13A         /* the walker visits the first 16 slots */

StepResult humanoid_carried(Game *g, uint16_t proc)
{
    uint16_t obj = game_word(g, (uint16_t)(proc + 7));
    Object *o = objects_at(&g->objects, obj);
    if (!o) return STEP_UNPORTED;
    o->vy = 0;
    o->y = (uint16_t)((uint8_t)(g->ram[SHIP_LINE_HI] + 0x0A) << 8 | (o->y & 0xFF));
    o->x = (uint16_t)(game_word(g, SHIP_X) + 0x0080);
    if (game_terrain(g, o->x) >= (uint8_t)(o->y >> 8)) {
        processes_sleep(&g->processes, 1, HUMANOID_CARRIED_BY_SHIP);
        return STEP_DONE;
    }
    /* set down: 500 */
    uint16_t bonus = processes_create(&g->processes, BONUS_500, 0, 0);
    if (!bonus) return STEP_UNPORTED;
    game_set_word(g, (uint16_t)(bonus + 7), obj);
    o->owner = 0;
    o->vy = 0;
    o->hit = HUMANOID_ON_GROUND;
    game_sound(g, 0xD4DF);
    game_end_process(g);
    return STEP_DONE;
}

/* Towards line `to`, a line a step. */
static void follow(Object *o, uint8_t to)
{
    uint8_t line = (uint8_t)(o->y >> 8);
    if (to == line) return;
    line = (uint8_t)(line + (to > line ? 1 : -1));
    o->y = (uint16_t)(line << 8 | (o->y & 0xFF));
}

StepResult humanoid_walk(Game *g, uint16_t proc)
{
    uint16_t slot = (uint16_t)(game_word(g, (uint16_t)(proc + 7)) + 2);
    if (slot >= WALK_END) slot = HUMANOIDS;
    game_set_word(g, (uint16_t)(proc + 7), slot);
    Object *o = objects_at(&g->objects, game_word(g, slot));
    if (o && o->screen && o->hit == HUMANOID_ON_GROUND) {
        uint16_t picture;
        uint8_t step;
        int turn = g->ram[RANDOM] <= 8;
        if (o->picture > 0xF90B) {
            if (turn) {
                picture = 0xF901;
                step = 0xE0;
            } else {
                uint8_t to = (uint8_t)(game_terrain(g, o->x) + 0x0F);
                follow(o, to > 0xE8 ? 0xE8 : to);
                picture = (uint16_t)(o->picture + 10);
                if (picture > 0xF91F) picture = 0xF915;
                step = 0x20;
            }
        } else {
            if (turn) {
                picture = 0xF915;
                step = 0x20;
            } else {
                uint8_t to = (uint8_t)(game_terrain(g, o->x) + 4);
                follow(o, to > 0xE8 ? 0xE8 : to);
                picture = (uint16_t)(o->picture + 10);
                if (picture > 0xF90B) picture = 0xF901;
                step = 0xE0;
            }
        }
        o->picture = picture;
        o->x = (uint16_t)(o->x + (int8_t)step);
    } else if (!o && game_word(g, slot)) {
        return STEP_UNPORTED;
    }
    processes_sleep(&g->processes, 2, HUMANOID_WALK);
    return STEP_DONE;
}

StepResult bonus_start(Game *g, uint16_t proc, int five_hundred)
{
    uint16_t obj = objects_alloc(&g->objects);
    if (!obj) return STEP_UNPORTED;
    objects_setup(&g->objects, obj, proc, five_hundred ? 0xF9E7 : 0xF9DD, 0xEDBC, 0);
    if (score_add(g, five_hundred ? 0x0150 : 0x0125)) return STEP_UNPORTED;
    Object *o = objects_at(&g->objects, obj), *h = objects_at(&g->objects, game_word(g, (uint16_t)(proc + 7)));
    if (!h) return STEP_UNPORTED;
    o->vx = game_word(g, SHIP_SPEED);
    o->vy = 0;
    o->flags = 0x11;
    o->x = h->x;
    o->y = (int16_t)h->y < 0 ? (uint16_t)(h->y - 0x2000) : (uint16_t)(h->y + 0x1800);
    g->objects.screen = obj;
    game_set_word(g, (uint16_t)(proc + 7), obj);
    processes_sleep(&g->processes, 0x32, BONUS_SHOW);
    return STEP_DONE;
}

StepResult bonus_end(Game *g, uint16_t proc)
{
    if (game_kill_object(g, game_word(g, (uint16_t)(proc + 7)))) return STEP_UNPORTED;
    game_end_process(g);
    return STEP_DONE;
}

int humanoid_hit(Game *g, uint16_t obj, int carried)
{
    Object *o = objects_at(&g->objects, obj);
    if (!o) return -1;
    if (carried && o->owner) {
        if (g->ram[TOUCHED]) return -1; /* caught by the ship: with the ship's collisions */
        uint16_t owner = o->owner;
        if (humanoid_kill(g, obj)) return -1;
        return processes_kill(&g->processes, owner) ? 1 : -1;
    }
    if (g->ram[TOUCHED]) return 0; /* the ship walks past it */
    return humanoid_kill(g, obj) ? -1 : 1;
}
