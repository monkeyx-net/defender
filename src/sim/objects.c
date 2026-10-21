#include "sim/objects.h"

#include <stddef.h>

#define STATE_NO_OBJECTS 0x20
#define TOP 0x2A    /* y wraps between these lines */
#define BOTTOM 0xF0

Object *objects_at(Objects *o, uint16_t addr)
{
    if (addr < OBJECTS_FIRST) return NULL;
    unsigned off = addr - OBJECTS_FIRST;
    if (off % OBJECT_SIZE || off / OBJECT_SIZE >= OBJECTS) return NULL;
    return &o->block[off / OBJECT_SIZE];
}

static uint16_t addr_of(int i) { return (uint16_t)(OBJECTS_FIRST + i * OBJECT_SIZE); }

void objects_init(Objects *o)
{
    for (int i = 0; i < OBJECTS; i++) o->block[i].link = i + 1 < OBJECTS ? addr_of(i + 1) : 0;
    o->free_list = addr_of(0);
    o->screen = o->off_screen = o->shots = 0;
}

uint16_t objects_alloc(Objects *o)
{
    uint16_t addr = o->free_list;
    Object *b = objects_at(o, addr);
    if (!b) return 0;
    o->free_list = b->link;
    b->link = o->screen;
    b->screen = 0;
    b->flags = 0;
    return addr;
}

void objects_setup(Objects *o, uint16_t addr, uint16_t owner, uint16_t picture, uint16_t hit, uint16_t data)
{
    Object *b = objects_at(o, addr);
    if (!b) return;
    b->owner = owner;
    b->picture = picture;
    b->hit = hit;
    b->data = data;
}

/* Unlink `addr` from the list whose head is *head; then onto the free list. */
static int unlink_from(Objects *o, uint16_t *head, uint16_t addr)
{
    uint16_t *prev = head;
    while (*prev) {
        if (*prev == addr) {
            Object *b = objects_at(o, addr);
            if (!b) return -1;
            *prev = b->link;
            b->link = o->free_list;
            o->free_list = addr;
            return 0;
        }
        Object *p = objects_at(o, *prev);
        if (!p) return -1;
        prev = &p->link;
    }
    return -1;
}

int objects_free(Objects *o, uint16_t addr)
{
    if (!unlink_from(o, &o->screen, addr)) return 0;
    return unlink_from(o, &o->off_screen, addr);
}

int objects_free_shot(Objects *o, uint16_t addr) { return unlink_from(o, &o->shots, addr); }

void objects_move(Objects *o, uint8_t state)
{
    if (state & STATE_NO_OBJECTS) return;
    for (Object *b = objects_at(o, o->screen); b; b = objects_at(o, b->link)) {
        b->x = (uint16_t)(b->x + b->vx);
        uint16_t y = (uint16_t)(b->y + b->vy);
        uint8_t line = (uint8_t)(y >> 8);
        if (line < TOP) line = BOTTOM;
        else if (line > BOTTOM) line = TOP;
        b->y = (uint16_t)(line << 8 | (y & 0xFF));
    }
}

/* A block as its 23 bytes in RAM, and back. */
static void encode(const Object *b, uint8_t *m)
{
    const uint16_t w[10] = { b->link, b->picture, b->screen, b->owner, b->hit, b->x, b->y, b->vx, b->vy, b->data };
    for (int i = 0; i < 10; i++) {
        m[i * 2] = (uint8_t)(w[i] >> 8);
        m[i * 2 + 1] = (uint8_t)w[i];
    }
    m[20] = b->flags;
    m[21] = b->extra[0];
    m[22] = b->extra[1];
}

static void decode(Object *b, const uint8_t *m)
{
    uint16_t w[10];
    for (int i = 0; i < 10; i++) w[i] = (uint16_t)(m[i * 2] << 8 | m[i * 2 + 1]);
    b->link = w[0];
    b->picture = w[1];
    b->screen = w[2];
    b->owner = w[3];
    b->hit = w[4];
    b->x = w[5];
    b->y = w[6];
    b->vx = w[7];
    b->vy = w[8];
    b->data = w[9];
    b->flags = m[20];
    b->extra[0] = m[21];
    b->extra[1] = m[22];
}

/* The list head holding `addr` (heads are at odd addresses); *low = the
   head's second byte. */
static uint16_t *head(Objects *o, uint16_t addr, int *low)
{
    static const uint16_t at[4] = { LIST_SCREEN, LIST_FREE, LIST_OFF_SCREEN, LIST_SHOTS };
    uint16_t *h[4] = { &o->screen, &o->free_list, &o->off_screen, &o->shots };
    for (int i = 0; i < 4; i++)
        if (addr == at[i] || addr == at[i] + 1) {
            *low = addr != at[i];
            return h[i];
        }
    return NULL;
}

static int in_blocks(uint16_t addr)
{
    return addr >= OBJECTS_FIRST && addr < OBJECTS_FIRST + OBJECTS * OBJECT_SIZE;
}

int objects_peek(const Objects *o, uint16_t addr)
{
    int low;
    uint16_t *h = head((Objects *)o, addr, &low); /* only read */
    if (h) return low ? *h & 0xFF : *h >> 8;
    if (!in_blocks(addr)) return -1;
    uint8_t m[OBJECT_SIZE];
    encode(&o->block[(addr - OBJECTS_FIRST) / OBJECT_SIZE], m);
    return m[(addr - OBJECTS_FIRST) % OBJECT_SIZE];
}

void objects_poke(Objects *o, uint16_t addr, uint8_t value)
{
    int low;
    uint16_t *h = head(o, addr, &low);
    if (h) {
        *h = low ? (uint16_t)((*h & 0xFF00) | value) : (uint16_t)((*h & 0xFF) | value << 8);
        return;
    }
    if (!in_blocks(addr)) return;
    Object *b = &o->block[(addr - OBJECTS_FIRST) / OBJECT_SIZE];
    uint8_t m[OBJECT_SIZE];
    encode(b, m);
    m[(addr - OBJECTS_FIRST) % OBJECT_SIZE] = value;
    decode(b, m);
}

void objects_load(Objects *o, const uint8_t *ram)
{
    for (int i = 0; i < OBJECTS; i++) decode(&o->block[i], ram + OBJECTS_FIRST + i * OBJECT_SIZE);
    o->screen = (uint16_t)(ram[LIST_SCREEN] << 8 | ram[LIST_SCREEN + 1]);
    o->free_list = (uint16_t)(ram[LIST_FREE] << 8 | ram[LIST_FREE + 1]);
    o->off_screen = (uint16_t)(ram[LIST_OFF_SCREEN] << 8 | ram[LIST_OFF_SCREEN + 1]);
    o->shots = (uint16_t)(ram[LIST_SHOTS] << 8 | ram[LIST_SHOTS + 1]);
}
