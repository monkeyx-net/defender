#include "sim/pods.h"

#include "sim/enemies.h"
#include "sim/explosions.h"
#include "sim/score.h"
#include "sim/shots.h"

#define SCRATCH 0xA073
#define RANDOM 0xA0DF
#define RANDOM_HI 0xA0E0
#define RANDOM_LO 0xA0E1
#define SHIP_X 0xA0CC
#define SHIP_LINE 0xA0C0
#define PODS 0xA114
#define SWARMERS 0xA116
#define SWARMERS_MAX 0x14
#define WAVE_SWARMER_X 0xA10C
#define WAVE_SWARMER_SHOTS 0xA10D
#define WAVE_SWARMER_PULL 0xA10E

static uint16_t sext(uint8_t b) { return (uint16_t)(int16_t)(int8_t)b; }

void pod_spawn(Game *g, uint8_t n)
{
    g->ram[SCRATCH] = n;
    do {
        uint16_t obj = objects_alloc(&g->objects);
        if (!obj) return;
        objects_setup(&g->objects, obj, g->processes.current, 0xF8F7, HIT_POD, 0xCCCC);
        Object *o = objects_at(&g->objects, obj);
        game_random(g);
        uint8_t lo = g->ram[RANDOM_LO];
        o->x = (uint16_t)((uint8_t)((g->ram[RANDOM_HI] & 0x3F) + 0x10) << 8 | lo);
        o->y = (uint16_t)((uint8_t)((lo >> 1) + 0x2A) << 8 | (o->y & 0xFF));
        o->vx = sext((uint8_t)((g->ram[RANDOM] & 0x3F) + 0xE0));
        uint8_t b = (uint8_t)((g->ram[RANDOM_LO] & 0x7F) - 0x40);
        uint16_t vy = sext(b);
        vy = (b & 0x80) ? (uint16_t)(vy & 0xFFDF) : (uint16_t)(vy | 0x0020);
        o->vy = vy;
        appear_start(g, obj);
    } while (--g->ram[SCRATCH]);
}

void swarmer_spawn(Game *g, uint8_t n, uint16_t from)
{
    g->ram[SCRATCH] = n;
    Object *f = objects_at(&g->objects, from);
    do {
        uint8_t count = (uint8_t)(g->ram[SWARMERS] + 1);
        if (count > SWARMERS_MAX) return;
        g->ram[SWARMERS] = count;
        uint16_t proc = processes_create(&g->processes, SWARMER_AIM, 0, 0);
        uint16_t obj = objects_alloc(&g->objects);
        if (!proc || !obj || !f) return;
        objects_setup(&g->objects, obj, proc, 0xF97B, HIT_SWARMER, 0x2424);
        Object *o = objects_at(&g->objects, obj);
        o->x = f->x;
        o->y = f->y;
        game_set_word(g, (uint16_t)(proc + 7), obj);
        o->owner = proc;
        game_random(g); /* $EB89 */
        o->vy = (uint16_t)(sext(g->ram[RANDOM]) << 1);
        o->vx = sext((uint8_t)((g->ram[RANDOM_LO] & 0x3F) + 0xE0));
        game_poke(g, (uint16_t)(proc + 9), (uint8_t)(g->ram[RANDOM_LO] & g->ram[WAVE_SWARMER_PULL]));
        processes_at(&g->processes, proc)->sleep = (uint8_t)(g->ram[RANDOM_HI] & 0x1F);
        game_poke(g, (uint16_t)(proc + 0xB), game_random_upto(g, g->ram[WAVE_SWARMER_SHOTS]));
        g->objects.screen = obj;
    } while (--g->ram[SCRATCH]);
}

/* $EC86 */
static void swarmer_fire(Game *g, uint16_t proc, uint16_t obj)
{
    Object *o = objects_at(&g->objects, obj);
    uint16_t d = (uint16_t)(game_word(g, SHIP_X) - o->x);
    if (!(((d >> 8) ^ (o->vx >> 8)) & 0x80)) {
        uint16_t shot = shot_create(g, obj, proc, 0xE4D9, 0xF95B, 0xE51E);
        Object *s = objects_at(&g->objects, shot);
        if (s) {
            s->vx = (uint16_t)(o->vx << 3);
            game_sound(g, 0xD534);
            uint8_t a = (uint8_t)(g->ram[SHIP_LINE] - (uint8_t)(s->y >> 8));
            s->vy = (uint16_t)((int16_t)(uint16_t)(a << 8) >> 5);
        }
    }
    game_poke(g, (uint16_t)(proc + 0xB), game_random_upto(g, g->ram[WAVE_SWARMER_SHOTS]));
}

StepResult swarmer_step(Game *g, uint16_t proc, int aim)
{
    uint16_t obj = game_word(g, (uint16_t)(proc + 7));
    Object *o = objects_at(&g->objects, obj);
    if (!o) return STEP_UNPORTED;
    if (!aim) {
        uint8_t pull = (uint8_t)game_peek(g, (uint16_t)(proc + 9));
        if (!(g->ram[SHIP_LINE] > (uint8_t)(o->y >> 8))) pull = (uint8_t)-pull;
        int16_t vy = (int16_t)(uint16_t)(sext(pull) + o->vy);
        if (vy >= 0x0200) vy = 0x0200;
        if (vy <= -0x0200) vy = -0x0200;
        o->vy = (uint16_t)vy;
        uint16_t damp = (uint16_t)((uint16_t)~(uint16_t)vy << 2);
        o->vy = (uint16_t)(o->vy + sext((uint8_t)(damp >> 8)));
        o->vy = (uint16_t)(o->vy + sext((uint8_t)((g->ram[RANDOM] & 0x1F) + 0xF0)));
        if ((uint16_t)(game_word(g, SHIP_X) - o->x + 0x12C0) > 0x2580) aim = 1;
        else {
            uint8_t n = (uint8_t)(game_peek(g, (uint16_t)(proc + 0xB)) - 1);
            game_poke(g, (uint16_t)(proc + 0xB), n);
            if (!n) swarmer_fire(g, proc, obj);
        }
    }
    if (aim) {
        uint8_t b = g->ram[WAVE_SWARMER_X];
        if (game_word(g, SHIP_X) < o->x) b = (uint8_t)-b;
        o->vx = sext(b);
    }
    processes_sleep(&g->processes, 3, SWARMER);
    return STEP_DONE;
}

int pod_hit(Game *g, uint16_t obj)
{
    if (objects_free(&g->objects, obj)) return -1;
    if (score_add(g, 0x0210)) return -1;
    game_erase_object(g, obj);
    explosion_start(g, obj);
    game_sound(g, 0xD4F3);
    swarmer_spawn(g, game_random_upto(g, 6), obj);
    g->ram[PODS]--;
    return 0;
}

int swarmer_hit(Game *g, uint16_t obj)
{
    g->ram[SWARMERS]--;
    if (game_kill_object(g, obj)) return -1;
    Object *o = objects_at(&g->objects, obj);
    if (!processes_kill(&g->processes, o->owner)) return -1;
    o->x = (uint16_t)(o->x - 0x40);
    o->y = (uint16_t)((uint8_t)((o->y >> 8) - 2) << 8 | (o->y & 0xFF));
    o->picture = 0xF8E2;
    explosion_start(g, obj);
    if (score_add(g, 0x0115)) return -1;
    game_sound(g, 0xD516);
    return 0;
}
