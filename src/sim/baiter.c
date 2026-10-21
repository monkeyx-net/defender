#include "sim/baiter.h"

#include "sim/enemies.h"
#include "sim/explosions.h"
#include "sim/shots.h"

#define PICTURE_FIRST 0xF9A3
#define PICTURE_LAST 0xF9B7
#define PICTURE_HIDDEN 0xF8EC
#define BAITERS 0xA119
#define WAVE_SHOTS 0xA110
#define WAVE_STEER 0xA111
#define RANDOM 0xA0DF
#define SCRATCH 0xA073
#define SHIP_X 0xA0CC
#define SHIP_LINE 0xA0C0
#define SHIP_SPEED 0xA0C7
#define SHIP_VY 0xA0CA

/* $EAEE ($EAF5 without the random check) */
static void steer(Game *g, Object *o, int check)
{
    if (check && g->ram[RANDOM] <= g->ram[WAVE_STEER]) return;
    g->ram[SCRATCH] = 0x40;
    g->ram[SCRATCH + 1] = 0x01;
    uint16_t d = (uint16_t)(o->x - game_word(g, SHIP_X));
    if (!(d & 0x8000)) g->ram[SCRATCH] = (uint8_t)-g->ram[SCRATCH];
    if ((uint16_t)(d + 0x0280) > 0x0500)
        o->vx = (uint16_t)((int8_t)g->ram[SCRATCH] + game_word(g, SHIP_SPEED));
    uint8_t a = (uint8_t)((o->y >> 8) - g->ram[SHIP_LINE]);
    if (!(a & 0x80)) g->ram[SCRATCH + 1] = (uint8_t)-g->ram[SCRATCH + 1];
    if ((uint8_t)(a + 0x0A) <= 0x14) return;
    o->vy = (uint16_t)((int16_t)(uint16_t)((g->ram[SCRATCH + 1] << 8) + game_word(g, SHIP_VY)) >> 1);
}

void baiter_spawn(Game *g)
{
    uint16_t proc = processes_create(&g->processes, BAITER, 0, 0);
    uint16_t obj = objects_alloc(&g->objects);
    if (!proc || !obj) return;
    objects_setup(&g->objects, obj, proc, PICTURE_FIRST, HIT_BAITER, 0x3333);
    Object *o = objects_at(&g->objects, obj);
    game_set_word(g, (uint16_t)(proc + 7), obj);
    o->owner = proc;
    uint16_t x = (uint16_t)(((g->ram[RANDOM] & 0x1F) << 8 | g->ram[RANDOM + 1]) + game_word(g, 0xA020));
    o->x = x;
    o->y = (uint16_t)((uint8_t)(((uint8_t)x >> 1) + 0x2A) << 8 | (o->y & 0xFF));
    o->vy = o->vx = 0;
    game_poke(g, (uint16_t)(proc + 9), 8);
    steer(g, o, 0);
    appear_start(g, obj);
}

StepResult baiter_step(Game *g, uint16_t proc)
{
    uint16_t obj = game_word(g, (uint16_t)(proc + 7));
    Object *o = objects_at(&g->objects, obj);
    if (!o) return STEP_UNPORTED;
    if (o->picture != PICTURE_HIDDEN) {
        uint8_t n = (uint8_t)(game_peek(g, (uint16_t)(proc + 9)) - 1);
        game_poke(g, (uint16_t)(proc + 9), n);
        if (!n) {
            game_poke(g, (uint16_t)(proc + 9), game_random_upto(g, g->ram[WAVE_SHOTS]));
            if (shot_fire(g, obj, proc)) game_sound(g, 0xD52F);
        }
        uint16_t next = (uint16_t)(o->picture + 10);
        if (next > PICTURE_LAST) {
            next = PICTURE_FIRST;
            steer(g, o, 1);
        }
        o->picture = next;
    }
    processes_sleep(&g->processes, 6, BAITER);
    return STEP_DONE;
}

int baiter_hit(Game *g, uint16_t obj)
{
    g->ram[BAITERS]--;
    return enemy_kill(g, obj, 0x0120, 0xD4FD, 1);
}
