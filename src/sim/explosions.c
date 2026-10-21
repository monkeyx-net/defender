#include "sim/explosions.h"

#define SCROLL 0xA020
#define SCRATCH_E9 0xA0E9      /* <$E9: x - scroll */
#define ON_SCREEN 0x26          /* (x - scroll) >> 8 at most this */
#define LIST_END 0x40

void explosion_erase(Game *g, uint16_t slot)
{
    uint16_t end = (uint16_t)(slot + LIST_END), at = game_word(g, slot + 4);
    game_set_word(g, 0xA0F3, end); /* <$F3: the ROM keeps the end there */
    if (at == end) return;
    while (at != end) {
        uint16_t px = game_word(g, at);
        game_set_word(g, px, 0);
        at = (uint16_t)(at + 2);
    }
    game_set_word(g, slot + 4, at);
}

void explosion_start(Game *g, uint16_t obj)
{
    uint16_t rel = (uint16_t)(game_word(g, obj + 0xA) - game_word(g, SCROLL));
    if ((rel >> 8) > ON_SCREEN) return;
    game_set_word(g, SCRATCH_E9, rel);
    uint16_t last = game_word(g, EXPLOSION_LAST), y = last;
    for (;;) {
        y = y ? (uint16_t)(y + EXPLOSION_SIZE) : EXPLOSIONS;
        if (y == EXPLOSIONS_END) y = EXPLOSIONS;
        if (y == last) return;
        uint8_t state = (uint8_t)game_peek(g, y);
        if (state & 0x80) continue;
        if (state) explosion_erase(g, y);
        break;
    }
    game_set_word(g, EXPLOSION_LAST, y);
    game_set_word(g, y, 0x0100);
    uint16_t picture = game_word(g, obj + 2);
    game_set_word(g, y + 2, picture);
    game_set_word(g, y + 4, (uint16_t)(y + LIST_END));
    uint16_t corner = (uint16_t)((uint16_t)(rel << 2) & 0xFF00) | (uint8_t)game_peek(g, obj + 0xC);
    game_set_word(g, y + 8, corner);
    uint16_t d = (uint16_t)(corner - game_word(g, HIT_POINT));
    uint8_t w = game_read(g, picture), h = game_read(g, (uint16_t)(picture + 1));
    uint16_t centre;
    if ((d >> 8) + w > 0xFF && (d & 0xFF) + h > 0xFF) centre = game_word(g, HIT_POINT);
    else centre = (uint16_t)((w >> 1) << 8 | h >> 1) + corner;
    game_set_word(g, y + 6, centre);
}
