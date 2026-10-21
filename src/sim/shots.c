#include "sim/shots.h"

#include "sim/explosions.h"
#include "sim/score.h"

#define SHOT_COUNT 0xA099
#define SCROLL 0xA020
#define SHIP_COLUMN 0xA0BF
#define SHIP_LINE 0xA0C0
#define SHIP_SPEED 0xA0C7
#define RANDOM 0xA0DF
#define RANDOM_LO 0xA0E1
#define ON_SCREEN 0x2580    /* x - scroll below this */
#define TOP_LINE 0x2A
#define LIFE 0x14           /* +15 and +16 */
#define BOMB_DATA 0xE4D9
#define BOMB_PICTURE 0xF95B
#define BOMB_HIT 0xE51E

uint16_t shot_create(Game *g, uint16_t from, uint16_t owner, uint16_t data, uint16_t picture, uint16_t hit)
{
    if (g->ram[SHOT_COUNT] >= SHOTS_MAX) return 0;
    uint16_t rel = (uint16_t)(game_word(g, from + 0xA) - game_word(g, SCROLL));
    if (rel >= ON_SCREEN) return 0;
    uint8_t column = (uint8_t)((uint16_t)(rel << 2) >> 8);
    uint8_t line = (uint8_t)game_peek(g, from + 0xC);
    if (line <= TOP_LINE) return 0;
    uint16_t shot = g->objects.free_list;
    Object *s = objects_at(&g->objects, shot);
    if (!s) return 0;
    s->screen = s->x = (uint16_t)(column << 8 | line);
    s->y = (uint16_t)(line << 8 | column);
    s->owner = owner;
    s->vx = s->vy = 0;
    s->data = data;
    s->picture = picture;
    s->hit = hit;
    s->extra[0] = s->extra[1] = LIFE;
    g->objects.free_list = s->link;
    s->link = g->objects.shots;
    g->ram[SHOT_COUNT]++;
    g->objects.shots = shot;
    return shot;
}

/* A random spread around `target - at`, as the ROM's 8-bit sum, times 4. */
static uint16_t aim(uint8_t random, uint8_t target, uint8_t at)
{
    int8_t d = (int8_t)(uint8_t)((random & 0x1F) + 0xF0 + target - at);
    return (uint16_t)(d * 4);
}

int shot_fire(Game *g, uint16_t from, uint16_t owner)
{
    uint16_t shot = shot_create(g, from, owner, BOMB_DATA, BOMB_PICTURE, BOMB_HIT);
    Object *s = objects_at(&g->objects, shot);
    if (!s) return 0;
    s->vx = aim(g->ram[RANDOM], g->ram[SHIP_COLUMN], (uint8_t)(s->screen >> 8));
    if (g->ram[RANDOM] > 0x78) s->vx = (uint16_t)(game_word(g, SHIP_SPEED) * 4 + s->vx);
    s->vy = aim(g->ram[RANDOM_LO], g->ram[SHIP_LINE], (uint8_t)s->screen);
    return 1;
}

int shot_hit(Game *g, uint16_t shot)
{
    score_add(g, 0x0025);
    g->ram[SHOT_COUNT]--;
    if (objects_free_shot(&g->objects, shot)) return -1;
    game_erase_object(g, shot);
    Object *s = objects_at(&g->objects, shot);
    s->x = (uint16_t)((s->x >> 2) + game_word(g, SCROLL));
    s->y = (uint16_t)((uint8_t)((s->y >> 8) - 2) << 8 | (s->y & 0xFF));
    s->picture = 0xF951;
    explosion_start(g, shot);
    game_sound(g, 0xD4E4);
    return 0;
}
