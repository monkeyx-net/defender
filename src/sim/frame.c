#include "sim/frame.h"

#include <stddef.h>

#include "sim/explosions.h"
#include "sim/irq.h"
#include "sim/player.h"
#include "sim/ship.h"
#include "sim/sounds.h"
#include "sim/terrain.h"

#define STATE 0xA0BA
#define TICKS 0xA05D
#define HALF 0xA092
#define BAND_TOP 0xA0A1
#define BAND_SPLIT 0xA0A2
#define LINE_TOP 0x00     /* the video counter as the top half reads it */
#define LINE_BOTTOM 0x80

/* $D5E5: queue the process of the lowest bit of `pressed` from the table at `table`. */
static void queue_switch(Game *g, uint8_t pressed, uint16_t table)
{
    uint8_t b = 0;
    int carry;
    do {
        b = (uint8_t)(b + 4);
        carry = pressed & 1;
        pressed >>= 1;
    } while (!carry);
    uint16_t u = (uint16_t)(table + b);
    uint16_t d = (uint16_t)(game_read(g, u) << 8 | game_read(g, (uint16_t)(u + 1)));
    uint16_t x = (uint16_t)(game_read(g, (uint16_t)(u + 2)) << 8 | game_read(g, (uint16_t)(u + 3)));
    if (!game_word(g, 0xA082)) {
        game_set_word(g, 0xA082, d);
        game_set_word(g, 0xA084, x);
    } else {
        game_set_word(g, 0xA086, d);
        game_set_word(g, 0xA088, x);
    }
}

/* $D5FD: the coin switches (IN2), debounced. */
static void coins(Game *g)
{
    uint8_t *r = g->ram;
    uint8_t a = (uint8_t)~(r[0xA079] | r[0xA07A]);
    r[0xA07A] = r[0xA079];
    r[0xA079] = g->in2 & 0x3F;
    if (!(a & r[0xA079])) return;
    r[0xA079] = (uint8_t)(g->in2 & r[0xA079]); /* read again after a delay */
    a &= r[0xA079];
    if (a) queue_switch(g, a, 0xF8A2);
}

/* $D568: the sound sequencer, the timers, the controls. */
static void sounds_and_controls(Game *g)
{
    uint8_t *r = g->ram;
    Sounds s = {
        .script = g->data,
        .pos = game_word(g, 0xA0B0),
        .priority = r[0xA0B2],
        .timer = r[0xA0B3],
        .repeat = r[0xA0B4],
        .thrust = r[0xA0AD],
    };
    int send = sounds_tick(&s, r[0xA07B], r[STATE]);
    game_set_word(g, 0xA0B0, (uint16_t)((s.script - g->data) + s.pos));
    r[0xA0B2] = s.priority;
    r[0xA0B3] = s.timer;
    r[0xA0B4] = s.repeat;
    r[0xA0AD] = s.thrust;
    if (send >= 0) game_send_sound(g, (uint8_t)send);
    if (g->cra & 0x40) r[0xA07E] = 0x3C;
    if (r[0xA07E]) r[0xA07E]--;
    if (r[0xA07F]) r[0xA07F]--;
    if (r[0xA081]) r[0xA081]--;
    if (r[0xA080]) r[0xA080]--;
    uint8_t a = (uint8_t)~(r[0xA07B] | r[0xA07C]);
    r[0xA07C] = r[0xA07B];
    r[0xA07B] = g->in0;
    r[0xA07D] = g->in1;
    a &= r[0xA07B];
    if (a) queue_switch(g, a, 0xF882);
}

/* $E263 */
static void ship_motion(Game *g)
{
    uint8_t *r = g->ram;
    if (r[STATE] & 0x40) return;
    uint32_t v = (uint32_t)r[0xA0C7] << 16 | game_word(g, 0xA0C8);
    Ship s = {
        .velocity = (v & 0x800000) ? (int32_t)v - 0x1000000 : (int32_t)v,
        .scroll = game_word(g, 0xA020),
        .last_scroll = game_word(g, 0xA022),
        .screen_x = game_word(g, 0xA0C3),
        .slide = (int16_t)game_word(g, 0xA095),
        .world_x = game_word(g, 0xA0CC),
        .y = game_word(g, 0xA0C5),
        .vy = (int16_t)game_word(g, 0xA0CA),
    };
    ShipInput in = { r[0xA07B], r[0xA07D], (int16_t)game_word(g, 0xA0BD) };
    int vertical = ship_step(&s, &in);
    r[0xA0C7] = (uint8_t)((uint32_t)s.velocity >> 16);
    game_set_word(g, 0xA0C8, (uint16_t)s.velocity);
    game_set_word(g, 0xA020, s.scroll);
    game_set_word(g, 0xA022, s.last_scroll);
    game_set_word(g, 0xA0C3, s.screen_x);
    r[0xA0C1] = (uint8_t)(s.screen_x >> 8);
    game_set_word(g, 0xA095, (uint16_t)s.slide);
    game_set_word(g, 0xA093, s.lead);
    game_set_word(g, 0xA0CC, s.world_x);
    if (vertical) {
        game_set_word(g, 0xA0CA, (uint16_t)s.vy);
        game_set_word(g, 0xA0C5, s.y);
        r[0xA0C2] = (uint8_t)(s.y >> 8);
    }
}

int game_irq_top(Game *g)
{
    uint8_t *r = g->ram;
    if (!r[HALF]) return 0;
    r[HALF] = 0;
    r[TICKS]++;
    if (LINE_TOP <= 8)
        for (int i = 0; i < 16; i++) g->palette[i] = r[0xA026 + i];
    coins(g);
    if (!(r[STATE] & 0x02)) terrain_update(g);
    ship_draw(g, r[BAND_TOP], r[BAND_SPLIT]);
    if (objects_draw(g, r[BAND_TOP], r[BAND_SPLIT])) return -1;
    objects_move(&g->objects, r[STATE]);
    return 0;
}

int game_irq_bottom(Game *g)
{
    uint8_t *r = g->ram;
    if (r[HALF]) return 0;
    r[HALF]++;
    sounds_and_controls(g);
    ship_motion(g);
    stars_draw(g);
    uint8_t split = g->split >= 0 ? (uint8_t)g->split : (uint8_t)(LINE_BOTTOM + 0x18 - 8);
    if (split > 0xA8) split = 0xA8;
    r[BAND_SPLIT] = split;
    if (objects_draw(g, r[BAND_SPLIT], r[BAND_SPLIT + 1])) return -1;
    ship_draw(g, r[BAND_SPLIT], r[BAND_SPLIT + 1]);
    return shots_move(g);
}

/* $E7C9-$E818: when ticks were missed, one object goes off screen. */
static void shed(Game *g, uint8_t ticks)
{
    uint8_t *r = g->ram;
    if (r[STATE] & 0x7D) {
        r[0xA05E] = 0;
        return;
    }
    int8_t a = (int8_t)(uint8_t)((uint8_t)(ticks << 1) + r[0xA05E] - 4);
    if (a < 0) a = 0;
    r[0xA05E] = (uint8_t)a;
    if (a < 2) return;
    r[0xA0AE] = 3;
    if (a <= 2) return;
    r[0xA05E] = 2;
    uint16_t prev = LIST_SCREEN;
    for (uint16_t x = g->objects.screen; x; prev = x, x = game_word(g, x)) {
        Object *o = objects_at(&g->objects, x);
        if (!o || o->flags) continue;
        game_set_word(g, prev, o->link);
        uint16_t d = (uint16_t)((uint8_t)((r[0xA0DF] & 0x3F) + 0x60) << 8 | r[0xA0E0]);
        o->x = (uint16_t)(o->x + d);
        game_erase_object(g, x);
        o->screen = 0;
        o->link = g->objects.off_screen;
        g->objects.off_screen = x;
        return;
    }
}

/* The writes counted: below $A000 (the screen, the explosions' slots),
   which the C code makes through game_poke like the ROM's stores. */
static int raw(uint16_t a) { return a < 0xA000; }

/* Counts the tick's raw writes and runs the bottom half before number n. */
static void bottom_hook(void *user, uint16_t addr)
{
    Game *g = user;
    if (!raw(addr)) return;
    if (g->tick_writes++ == g->bottom_at_write) {
        g->on_write = NULL;
        uint16_t current = g->processes.current;
        if (game_irq_bottom(g)) g->bottom_failed = 1;
        g->processes.current = current;
    }
}

int game_main_tick(Game *g)
{
    uint8_t *r = g->ram;
    g->processes.current = PROCESS_HEAD;
    if (!r[TICKS]) return 0;
    g->tick_writes = 0;
    g->bottom_failed = 0;
    if (g->bottom_at_write >= 0) {
        g->on_write = bottom_hook;
        g->user = g;
    }
    uint8_t ticks = r[TICKS];
    r[TICKS] = 0;
    shed(g, ticks);
    r[0xA036] = 2;
    if (ship_collide(g)) return -1;
    explosions_update(g);
    game_random(g);
    for (;;) {
        uint16_t x = game_word(g, 0xA082), d;
        if (x) {
            d = game_word(g, 0xA084);
            game_set_word(g, 0xA082, 0);
        } else {
            x = game_word(g, 0xA086);
            if (!x) break;
            d = game_word(g, 0xA088);
            game_set_word(g, 0xA086, 0);
        }
        if (!((uint8_t)d & r[STATE])) processes_create(&g->processes, x, (uint8_t)(d >> 8), 0);
    }
    uint16_t u = PROCESS_HEAD;
    for (;;) {
        u = u == PROCESS_HEAD ? g->processes.head : processes_at(&g->processes, u)->link;
        if (!u) break;
        if (!processes_tick(&g->processes, u)) continue;
        g->resume_from = u;
        if (game_step(g, u) != STEP_DONE) return -1;
        u = g->resume_from;
    }
    g->on_write = NULL;
    g->processes.current = PROCESS_HEAD;
    return g->bottom_failed ? -1 : 0;
}

int game_frame(Game *g)
{
    if (game_irq_top(g)) return -1;
    if (game_main_tick(g)) return -1;
    return game_irq_bottom(g); /* (does nothing if it already ran) */
}
