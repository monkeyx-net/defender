#include "sim/bombers.h"

#include "sim/explosions.h"
#include "sim/score.h"
#include "sim/shots.h"

#define SCRATCH 0xA073
#define RANDOM 0xA0DF
#define RANDOM_HI 0xA0E0
#define RANDOM_LO 0xA0E1
#define SHIP_X 0xA0CC
#define SHIP_LINE 0xA0C0
#define SHOT_COUNT 0xA099
#define DIRECTION 0xA0AA
#define BOMBER_COUNT 0xA113
#define WAVE_BOMBER_X 0xA106

static uint16_t sext(uint8_t b) { return (uint16_t)(int16_t)(int8_t)b; }

void bomber_spawn(Game *g, uint8_t n)
{
    g->ram[SCRATCH] = n;
    uint8_t vx = g->ram[WAVE_BOMBER_X];
    g->ram[DIRECTION] = (uint8_t)~g->ram[DIRECTION];
    if (!(g->ram[DIRECTION] & 0x80)) vx = (uint8_t)-vx;
    g->ram[SCRATCH + 1] = vx;
    uint16_t proc = processes_create(&g->processes, BOMBERS, 0, 1);
    if (!proc) return;
    game_poke(g, (uint16_t)(proc + 0xF), n);
    for (uint16_t a = 7; a < 0xF; a++) game_poke(g, (uint16_t)(proc + a), 0);
    uint8_t b = 0;
    do {
        uint16_t obj = objects_alloc(&g->objects);
        if (!obj) return;
        objects_setup(&g->objects, obj, proc, 0xF929, HIT_BOMBER, 0x8888);
        Object *o = objects_at(&g->objects, obj);
        o->vx = sext(g->ram[SCRATCH + 1]);
        o->vy = 0;
        b = 0;
        uint8_t k = g->ram[SCRATCH];
        uint16_t d = (uint16_t)(((uint8_t)((k >> 1) + k) << 8) | (k & 1 ? 0x80 : 0) | b);
        d = (uint16_t)(d + game_word(g, SHIP_X));
        o->x = (uint16_t)(d + 0x8000);
        o->y = (uint16_t)(0x50 << 8 | (o->y & 0xFF));
        game_poke(g, (uint16_t)(proc + 0x10), 0x50);
        o->owner = proc;
        g->objects.screen = obj;
        game_set_word(g, (uint16_t)(proc + k * 2 + 5), obj);
    } while (--g->ram[SCRATCH]);
}

/* $F3DC */
static void lay_mine(Game *g, uint16_t obj, uint16_t proc)
{
    if (g->ram[SHOT_COUNT] >= 0x0A) return;
    uint16_t shot = shot_create(g, obj, proc, 0xE498, 0xF95B, 0xE51E);
    Object *s = objects_at(&g->objects, shot);
    if (s) s->extra[0] = (uint8_t)((g->ram[RANDOM] & 0x1F) + 1);
}

StepResult bomber_step(Game *g, uint16_t proc)
{
    uint8_t *r = g->ram;
    uint16_t obj = game_word(g, (uint16_t)(proc + (r[RANDOM] & 6) + 7));
    Object *o = objects_at(&g->objects, obj);
    if (obj && !o) return STEP_UNPORTED;
    if (o) {
        uint8_t b = (uint8_t)((r[RANDOM] & 0x3F) + 0xE0);
        int8_t step = (b & 0x80) ? 0x0A : -0x0A;
        uint16_t picture = (uint16_t)(o->picture + step);
        if (picture < 0xF929) picture = 0xF929;
        if (picture > 0xF947) picture = 0xF947;
        o->picture = picture;
        o->vy = (uint16_t)(sext(b) + o->vy);
        uint16_t s = (uint16_t)(o->vy << 3);
        o->vy = (uint16_t)(o->vy + sext((uint8_t)-(uint8_t)(s >> 8)));
        uint8_t line = (uint8_t)o->screen; /* where it was drawn; 0: off screen */
        if (!line) {
            if (r[RANDOM] <= 0x40) {
                uint8_t base = (uint8_t)((r[RANDOM] & 3) + 0xFE + game_peek(g, (uint16_t)(proc + 0x10)));
                if (base < 0x40) base = 0x40;
                if (base >= 0x68) base = 0x68;
                game_poke(g, (uint16_t)(proc + 0x10), base);
            }
            uint8_t a = (uint8_t)(game_peek(g, (uint16_t)(proc + 0x10)) - (uint8_t)(o->y >> 8) + 0x10);
            if (a > 0x20) o->vy = (uint16_t)(o->vy + ((int8_t)(uint8_t)(a - 0x10) < 0 ? 0x0010 : 0xFFF0));
        } else {
            int8_t a = (int8_t)(uint8_t)(line - r[SHIP_LINE]);
            uint16_t d = 0;
            if (a < 0) {
                if (a <= -0x20) d = 0x0010;
                else if (a >= -0x10) d = 0xFFF0;
            } else {
                if ((uint8_t)a >= 0x20) d = 0xFFF0;
                else if ((uint8_t)a <= 0x10) d = 0x0010;
            }
            o->vy = (uint16_t)(o->vy + d);
            if (!(r[RANDOM_LO] & 7)) lay_mine(g, obj, proc);
        }
    }
    processes_sleep(&g->processes, 1, BOMBERS);
    return STEP_DONE;
}

int bomber_hit(Game *g, uint16_t obj)
{
    if (objects_free(&g->objects, obj)) return -1;
    if (score_add(g, 0x0125)) return -1;
    game_erase_object(g, obj);
    explosion_start(g, obj);
    game_sound(g, 0xD502);
    g->ram[BOMBER_COUNT]--;
    uint16_t proc = game_word(g, (uint16_t)(obj + 6));
    uint16_t y = (uint16_t)(proc + 7);
    while (game_word(g, y) != obj) y = (uint16_t)(y + 2);
    game_set_word(g, y, 0);
    uint8_t left = (uint8_t)(game_peek(g, (uint16_t)(proc + 0xF)) - 1);
    game_poke(g, (uint16_t)(proc + 0xF), left);
    if (!left && !processes_kill(&g->processes, proc)) return -1;
    return 0;
}
