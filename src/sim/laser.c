#include "sim/laser.h"

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
