#include "sim/hit.h"

#include "sim/enemies.h"

#define D0 0xA0D0
#define D6 0xA0D6
#define D8 0xA0D8
#define DA 0xA0DA
#define DC 0xA0DC
#define CE 0xA0CE
#define S73 0xA073
#define HIT_AT 0xA0F8

static uint8_t data8(const Game *g, uint16_t a) { return game_read(g, a); }
static uint16_t data16(const Game *g, uint16_t a) { return (uint16_t)(game_read(g, a) << 8 | game_read(g, (uint16_t)(a + 1))); }

int hit_test(Game *g, uint16_t list, uint16_t at, uint16_t picture)
{
    uint8_t *r = g->ram;
    game_set_word(g, D6, at);
    uint16_t far = (uint16_t)(at + data16(g, picture));
    game_set_word(g, D8, far);
    for (uint16_t x = game_word(g, list); x; x = game_word(g, x)) {
        uint16_t pos = game_word(g, (uint16_t)(x + 4));
        if (!pos) continue;
        if ((pos >> 8) >= r[D8] || (pos & 0xFF) >= r[D8 + 1]) continue;
        uint16_t pic = game_word(g, (uint16_t)(x + 2));
        uint16_t end = (uint16_t)(pos + data16(g, pic));
        if ((end >> 8) <= r[D6] || (end & 0xFF) <= r[D6 + 1]) continue;

        /* the boxes overlap: compare the pictures' bytes */
        game_set_word(g, DC, picture);
        game_set_word(g, S73, pos);
        r[D0] = r[D0 + 1] = r[D0 + 2] = r[D0 + 3] = 0;
        uint8_t a = (uint8_t)(pos >> 8), b = (uint8_t)pos;
        uint8_t db = (uint8_t)(b - r[D6 + 1]);
        if (b > r[D6 + 1]) r[D0 + 3] = db;
        else r[D0 + 1] = (uint8_t)-db;
        uint8_t da = (uint8_t)(a - r[D6]);
        if (a > r[D6]) r[D0 + 2] = da;
        else r[D0] = (uint8_t)-da;
        uint16_t oend = (uint16_t)(pos + data16(g, pic));
        uint8_t ob = (uint8_t)oend > r[D8 + 1] ? (uint8_t)((uint8_t)oend - r[D8 + 1]) : 0;
        uint8_t oa = (uint8_t)(oend >> 8) > r[D8] ? (uint8_t)((oend >> 8) - r[D8]) : 0;
        game_set_word(g, DA, (uint16_t)(oa << 8 | ob));
        uint16_t size = (uint16_t)(data16(g, pic) - game_word(g, D0) - game_word(g, DA));
        game_set_word(g, CE, size);
        r[0xA0D5] = data8(g, (uint16_t)(picture + 1));
        uint16_t u = (uint16_t)(data16(g, (uint16_t)(picture + 2)) + data8(g, (uint16_t)(picture + 1)) * r[D0 + 2]);
        r[0xA0D4] = data8(g, (uint16_t)(pic + 1));
        uint16_t y = (uint16_t)(data16(g, (uint16_t)(pic + 2)) + data8(g, (uint16_t)(pic + 1)) * r[D0]);
        y = (uint16_t)(y + (int8_t)r[D0 + 1]);
        u = (uint16_t)(u + (int8_t)r[D0 + 3]);
        for (;;) {
            /* rows from the overlap's height - 1 down to 0 (DECB / BPL: signed) */
            uint8_t rowb = (uint8_t)(r[CE + 1] - 1);
            do {
                int row = (int8_t)rowb--;
                if (!data8(g, (uint16_t)(u + row)) || !data8(g, (uint16_t)(y + row))) continue;
                /* a hit: the screen address of the object's byte */
                uint16_t off = (uint16_t)(y + row - data16(g, (uint16_t)(pic + 2)));
                uint8_t h = data8(g, (uint16_t)(pic + 1));
                uint16_t screen = pos;
                while (off >= h) {
                    off = (uint16_t)(off - h);
                    screen = (uint16_t)(screen + 0x100);
                }
                game_set_word(g, HIT_AT, (uint16_t)(screen + off));
                return enemy_hit(g, game_word(g, (uint16_t)(x + 8)), x) ? -1 : 1;
            } while (!(rowb & 0x80));
            y = (uint16_t)(y + r[0xA0D4]);
            u = (uint16_t)(u + r[0xA0D5]);
            if (!--r[CE]) break;
        }
    }
    return 0;
}
