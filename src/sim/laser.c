#include "sim/laser.h"

#include "sim/hit.h"

#define COLUMN 0x100
#define RIGHT_EDGE 0x9800 /* a head at or past this column ends */
#define LEFT_EDGE 0x0500  /* or at or before this one */
#define BODY 0x11
#define TIP 0x99
#define COLOURS_WRAP 0xA15F

void laser_colours_fill(LaserColours *c, Rng *r)
{
    c->next = LASER_COLOURS;
    for (int i = 0; i < LASER_COLOURS_SIZE; i++) {
        uint8_t a = rng_step(r);
        c->table[i] = (uint8_t)((a & 1 ? 0x01 : 0) + (a & 2 ? 0x10 : 0));
    }
}

void laser_start(Laser *l, uint16_t ship, int left)
{
    uint16_t at = (uint16_t)(ship + (left ? 0x0004 : 0x0704));
    l->head = l->mid = l->tail = at;
    l->left = left;
}

int laser_tick(Laser *l, LaserColours *c, uint8_t *vram, int frozen)
{
    if (frozen) return 1;
    uint16_t step = l->left ? (uint16_t)-COLUMN : COLUMN;
    uint16_t x = l->head;
    if (l->left ? x <= LEFT_EDGE : x >= RIGHT_EDGE) return 1;
    for (int i = 0; i < 4; i++, x = (uint16_t)(x + step)) vram[x] = BODY;
    vram[x] = TIP;
    l->head = x;

    if (c->next >= COLOURS_WRAP) c->next = LASER_COLOURS;
    x = l->mid;
    for (int i = 0; i < 3; i++, x = (uint16_t)(x + step)) vram[x] = c->table[c->next++ - LASER_COLOURS];
    l->mid = x;

    vram[l->tail] = 0;
    l->tail = (uint16_t)(l->tail + step); /* the column byte, +1 or -1 */
    return 0;
}

uint16_t laser_hit_position(const Laser *l)
{
    return l->left ? l->head : (uint16_t)(l->head - 6 * COLUMN);
}

void laser_end(const Laser *l, uint8_t *vram)
{
    uint16_t x = l->tail;
    if (l->left) {
        do {
            vram[x] = 0;
            x = (uint16_t)(x - COLUMN);
        } while (x >= l->head);
    } else {
        do {
            vram[x] = 0;
            x = (uint16_t)(x + COLUMN);
        } while (x <= l->head);
    }
}

/* ---- as process steps on the game's RAM ($E591-$E69E) ---------------------- */

#define LASERS_OUT 0xA0B5
#define SHIP_SCREEN 0xA0C1
#define FACING 0xA0BB
#define STATE 0xA0BA
#define BANK 0xA036
#define COLOUR_NEXT 0xA0A4
#define SOUND_LASER 0xD51B
#define HIT_LINE 0xF96F

static void load(Game *g, uint16_t proc, Laser *l, LaserColours *c, int left)
{
    l->head = game_word(g, (uint16_t)(proc + 7));
    l->mid = game_word(g, (uint16_t)(proc + 9));
    l->tail = game_word(g, (uint16_t)(proc + 11));
    l->left = left;
    c->next = game_word(g, COLOUR_NEXT);
    for (int i = 0; i < LASER_COLOURS_SIZE; i++) c->table[i] = g->ram[LASER_COLOURS + i];
}

static void store(Game *g, uint16_t proc, const Laser *l, const LaserColours *c)
{
    game_set_word(g, (uint16_t)(proc + 7), l->head);
    game_set_word(g, (uint16_t)(proc + 9), l->mid);
    game_set_word(g, (uint16_t)(proc + 11), l->tail);
    game_set_word(g, COLOUR_NEXT, c->next);
}

/* laser_tick and laser_end on the game's RAM, every write through game_poke
   in the ROM's order (so a test can follow it). */
static int tick_game(Game *g, Laser *l, LaserColours *c, int frozen)
{
    if (frozen) return 1;
    uint16_t step = l->left ? (uint16_t)-0x100 : 0x100;
    uint16_t x = l->head;
    if (l->left ? x <= 0x0500 : x >= 0x9800) return 1;
    for (int i = 0; i < 4; i++, x = (uint16_t)(x + step)) game_poke(g, x, 0x11);
    game_poke(g, x, 0x99);
    l->head = x;
    if (c->next >= 0xA15F) c->next = LASER_COLOURS;
    x = l->mid;
    for (int i = 0; i < 3; i++, x = (uint16_t)(x + step)) game_poke(g, x, c->table[c->next++ - LASER_COLOURS]);
    l->mid = x;
    game_poke(g, l->tail, 0);
    l->tail = (uint16_t)(l->tail + step);
    return 0;
}

static void end_game(Game *g, const Laser *l)
{
    uint16_t x = l->tail;
    if (l->left) {
        do {
            game_poke(g, x, 0);
            x = (uint16_t)(x - 0x100);
        } while (x >= l->head);
    } else {
        do {
            game_poke(g, x, 0);
            x = (uint16_t)(x + 0x100);
        } while (x <= l->head);
    }
}

static StepResult end(Game *g, const Laser *l)
{
    end_game(g, l);
    g->ram[LASERS_OUT]--;
    game_end_process(g);
    return STEP_DONE;
}

StepResult laser_step(Game *g, uint16_t proc, int left)
{
    Laser l;
    LaserColours c;
    load(g, proc, &l, &c, left);
    if (tick_game(g, &l, &c, g->ram[STATE] & 0x40)) return end(g, &l);
    store(g, proc, &l, &c);
    g->ram[BANK] = 2; /* $E5AB */
    int hit = hit_test(g, LIST_SCREEN, laser_hit_position(&l), HIT_LINE);
    if (hit < 0) return STEP_UNPORTED;
    if (hit) {
        load(g, proc, &l, &c, left); /* as the ROM: its +7 and +B */
        return end(g, &l);
    }
    processes_sleep(&g->processes, 1, left ? LASER_TICK_LEFT : LASER_TICK_RIGHT);
    return STEP_DONE;
}

StepResult laser_fire(Game *g, uint16_t proc)
{
    if (g->ram[LASERS_OUT] >= LASER_MAX) {
        game_end_process(g);
        return STEP_DONE;
    }
    g->ram[LASERS_OUT]++;
    game_sound(g, SOUND_LASER);
    int left = g->ram[FACING] & 0x80;
    Laser l;
    laser_start(&l, game_word(g, SHIP_SCREEN), left);
    game_set_word(g, (uint16_t)(proc + 7), l.head);
    game_set_word(g, (uint16_t)(proc + 9), l.mid);
    game_set_word(g, (uint16_t)(proc + 11), l.tail);
    return laser_step(g, proc, left);
}
