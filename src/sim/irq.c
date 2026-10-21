#include "sim/irq.h"

#include "sim/background.h"

#define STATE 0xA0BA
#define SCROLL 0xA020
#define LAST_SCROLL 0xA022
#define RANDOM 0xA0DF

static void poke16(Game *g, uint16_t a, uint16_t v) { game_set_word(g, a, v); }
static void poke8(Game *g, uint16_t a, uint8_t v) { game_poke(g, a, v); }

int objects_draw(Game *g, uint8_t top, uint8_t bottom)
{
    if (g->ram[STATE] & 0x20) return 0;
    for (uint16_t x = g->objects.screen; x;) {
        Object *o = objects_at(&g->objects, x);
        if (!o) return -1;
        if (o->screen) {
            uint8_t line = (uint8_t)o->screen;
            if (line > top || line <= bottom) { /* drawn outside the band: left alone */
                x = o->link;
                continue;
            }
            game_erase_picture(g, o->picture, o->screen);
            o->screen = 0;
        }
        uint8_t line = (uint8_t)(o->y >> 8);
        if (line <= top && line > bottom) {
            uint16_t rel = (uint16_t)(o->x - game_word(g, SCROLL));
            if (rel < 0x2580) {
                uint16_t d = (uint16_t)(rel << 2);
                uint8_t w = game_read(g, o->picture);
                uint8_t col = (uint8_t)(d >> 8);
                if ((uint8_t)(col + w) <= 0x9C) {
                    o->screen = (uint16_t)(col << 8 | line);
                    game_draw_picture(g, o->picture, o->screen, (d & 0x80) != 0);
                }
            }
        }
        x = o->link;
    }
    return 0;
}

/* $E1F0 / $E1CD: the flame's bytes behind a ship facing right / left. */
static void flame_erase(Game *g, uint16_t at, int left)
{
    if (left) {
        uint16_t u = (uint16_t)(at + 0x0806 - 5);
        for (int i = 0; i < 5; i++) poke8(g, (uint16_t)(u + i), 0);
        poke16(g, (uint16_t)(u + 0x0101), 0);
        poke8(g, (uint16_t)(u + 0x0103), 0);
        poke16(g, (uint16_t)(u + 0x0201), 0);
        poke8(g, (uint16_t)(u + 0x0203), 0);
        poke8(g, (uint16_t)(u + 0x0302), 0);
    } else {
        uint16_t u = (uint16_t)(at - 0x00FA - 5);
        for (int i = 0; i < 5; i++) poke8(g, (uint16_t)(u + i), 0);
        poke16(g, (uint16_t)(u - 0x00FF), 0);
        poke8(g, (uint16_t)(u - 0x00FD), 0);
        poke16(g, (uint16_t)(u - 0x01FF), 0);
        poke8(g, (uint16_t)(u - 0x01FD), 0);
        poke8(g, (uint16_t)(u - 0x02FE), 0);
    }
}

/* $E15C / $E19B: the flame, colours from <$9F. */
static void flame_draw(Game *g, int left)
{
    uint16_t c = game_word(g, 0xA09F);
    uint16_t at = game_word(g, 0xA0BF);
    int thrust = g->ram[0xA07B] & 0x02;
#define C(i) game_peek(g, (uint16_t)(c + (i)))
    if (!left) {
        uint16_t u = (uint16_t)(at - 0x00FF);
        poke8(g, u, (uint8_t)C(0));
        poke8(g, (uint16_t)(u + 1), (uint8_t)C(1));
        poke8(g, (uint16_t)(u + 2), (uint8_t)C(5));
        poke8(g, (uint16_t)(u + 3), (uint8_t)C(9));
        poke8(g, (uint16_t)(u + 4), (uint8_t)C(0xC));
        if (thrust) {
            poke8(g, (uint16_t)(u - 0x00FF), (uint8_t)C(3));
            poke8(g, (uint16_t)(u - 0x00FE), (uint8_t)C(6));
            poke8(g, (uint16_t)(u - 0x00FD), (uint8_t)C(0xA));
            poke8(g, (uint16_t)(u - 0x01FF), (uint8_t)C(4));
            poke8(g, (uint16_t)(u - 0x01FE), (uint8_t)C(7));
            poke8(g, (uint16_t)(u - 0x01FD), (uint8_t)C(0xB));
            poke8(g, (uint16_t)(u - 0x02FE), (uint8_t)C(8));
        }
    } else {
        uint16_t x = (uint16_t)(at + 0x0801);
        poke8(g, x, (uint8_t)C(0));
        poke8(g, (uint16_t)(x + 1), (uint8_t)C(1));
        poke8(g, (uint16_t)(x + 2), (uint8_t)C(2));
        poke8(g, (uint16_t)(x + 3), (uint8_t)C(3));
        poke8(g, (uint16_t)(x + 4), (uint8_t)C(4));
        if (thrust) {
            poke8(g, (uint16_t)(x + 0x0101), (uint8_t)C(5));
            poke8(g, (uint16_t)(x + 0x0102), (uint8_t)C(6));
            poke8(g, (uint16_t)(x + 0x0103), (uint8_t)C(7));
            poke8(g, (uint16_t)(x + 0x0201), (uint8_t)C(0xA));
            poke8(g, (uint16_t)(x + 0x0202), (uint8_t)C(0xB));
            poke8(g, (uint16_t)(x + 0x0203), (uint8_t)C(8));
            poke8(g, (uint16_t)(x + 0x0302), (uint8_t)C(9));
        }
    }
#undef C
}

void ship_draw(Game *g, uint8_t top, uint8_t bottom)
{
    g->ram[0xA077] = top;
    if (g->ram[STATE] & 0x10) return;
    uint8_t line = g->ram[0xA0C0];
    if (top <= line || bottom > line) return;
    uint16_t at = game_word(g, 0xA0BF);
    game_clear(g, at, 8, 6); /* $E25E: $D2DF */
    flame_erase(g, at, (int8_t)g->ram[0xA0BD] < 0);
    game_set_word(g, 0xA0BD, game_word(g, 0xA0BB));
    int left = (int8_t)g->ram[0xA0BD] < 0;
    uint16_t now = game_word(g, 0xA0C1);
    game_set_word(g, 0xA0BF, now);
    game_draw_picture(g, left ? 0xF9CB : 0xF9C1, now, (g->ram[0xA0C4] & 0x80) != 0);
    flame_draw(g, left);
}

void stars_draw(Game *g)
{
    uint8_t *r = g->ram;
    if (r[STATE] & 0x20) return;
    uint16_t a = (uint16_t)(game_word(g, SCROLL) & 0xFF80);
    game_set_word(g, 0xA06F, a);
    uint16_t d = (uint16_t)((game_word(g, LAST_SCROLL) & 0xFF80) - a);
    d = (uint16_t)(d << 1);
    r[0xA06F] = (uint8_t)(d >> 8);
    r[0xA071] = (r[0xA021] & 0x40) ? 0xF0 : 0x0F;
    for (int i = 0; i < 16; i++) game_poke(g, game_word(g, (uint16_t)(0xAF9D + i * 4)), 0);
    uint16_t x = 0xAF9D;
    uint8_t n = r[0xA0AE];
    do { /* DECB / BNE: none shown means 256 */
        uint8_t col = (uint8_t)(game_peek(g, x) + r[0xA06F]);
        if (col >= 0x9C) col = col <= 0xC0 ? 0x00 : 0x9B;
        game_poke(g, x, col);
        game_poke(g, game_word(g, x), (uint8_t)(game_peek(g, (uint16_t)(x + 2)) & r[0xA071]));
        x = (uint16_t)(x + 4);
    } while (--n);
    x = (uint16_t)(0xAF9D + (r[RANDOM] & 0x3C));
    game_poke(g, (uint16_t)(x + 2), (uint8_t)((game_peek(g, (uint16_t)(x + 2)) + 0x11) & 0x77));
    uint8_t v = r[RANDOM];
    if (v & 1) return;
    if (v >= 0x98) {
        if (game_word(g, 0xA0B8) != 0x6245 && !(r[STATE] & 0x80) && v == 0xA0) {
            x = (uint16_t)(v << 8 | r[0xA0E1]);
            game_poke(g, x, r[0xA0E0]);
        }
        v = (uint8_t)(v - 0x84);
    }
    game_poke(g, game_word(g, x), 0); /* the star's old pixel */
    game_poke(g, x, v);
    if (r[STATE] & 0x02) game_poke(g, (uint16_t)(x + 1), (uint8_t)((r[0xA0E1] & 0x3F) * 3 + 0x2A));
}

/* $E4C6: gone; erase where it was. */
static void shot_gone(Game *g, Object *s, uint16_t was)
{
    s->extra[1] = 0;
    poke16(g, was, 0);
    poke8(g, (uint16_t)(was + 2), 0);
    poke16(g, (uint16_t)(was + 0x100), 0);
    poke8(g, (uint16_t)(was + 0x102), 0);
}

int shots_move(Game *g)
{
    uint8_t *r = g->ram;
    if (r[STATE] & 0x20) return 0;
    uint16_t a = (uint16_t)(game_word(g, SCROLL) & 0xFFE0);
    game_set_word(g, 0xA09D, a);
    uint16_t d = (uint16_t)(((game_word(g, LAST_SCROLL) & 0xFFE0) - a) << 2);
    game_set_word(g, 0xA09D, d);
    for (uint16_t x = g->objects.shots; x;) {
        Object *s = objects_at(&g->objects, x);
        if (!s) return -1;
        uint16_t was = s->screen;
        uint16_t y = (uint16_t)(s->vy + s->y);
        if ((uint8_t)(y >> 8) <= 0x2A) {
            shot_gone(g, s, was);
            x = s->link;
            continue;
        }
        s->y = y;
        uint16_t nx = (uint16_t)(s->vx + d + s->x);
        if ((uint8_t)(nx >> 8) >= 0x98) {
            shot_gone(g, s, was);
            x = s->link;
            continue;
        }
        s->x = nx;
        s->screen = (uint16_t)(nx & 0xFF00) | (uint8_t)(s->y >> 8);
        uint16_t at = s->screen;
        if (s->data == 0xE498) {
            /* a mine: two frames */
            uint16_t u = game_word(g, 0xA0A6);
            if ((int8_t)(uint8_t)s->x < 0) u = (uint16_t)(u + 6);
            poke16(g, was, 0);
            poke8(g, (uint16_t)(was + 2), 0);
            poke16(g, (uint16_t)(was + 0x100), 0);
            poke8(g, (uint16_t)(was + 0x102), 0);
            poke16(g, at, game_word(g, u));
            poke8(g, (uint16_t)(at + 2), game_read(g, (uint16_t)(u + 2)));
            poke8(g, (uint16_t)(at + 0x100), game_read(g, (uint16_t)(u + 3)));
            poke16(g, (uint16_t)(at + 0x101), (uint16_t)(game_read(g, (uint16_t)(u + 4)) << 8 | game_read(g, (uint16_t)(u + 5))));
        } else if (s->data == 0xE4D9) {
            /* a bomb: by pixel parity */
            uint16_t u = game_word(g, 0xA0A8);
            int odd = ((uint8_t)s->x & 0x80) != 0;
            poke16(g, was, 0);
            poke8(g, (uint16_t)(was + 2), 0);
            poke16(g, (uint16_t)(was + 0x100), 0);
            poke8(g, (uint16_t)(was + 0x102), 0);
            uint8_t u0 = (uint8_t)game_peek(g, u), u1 = (uint8_t)game_peek(g, (uint16_t)(u + 1));
            uint8_t u2 = (uint8_t)game_peek(g, (uint16_t)(u + 2)), u3 = (uint8_t)game_peek(g, (uint16_t)(u + 3));
            if (!odd) {
                poke8(g, at, u0 & 0x0F);
                poke8(g, (uint16_t)(at + 1), u1);
                poke8(g, (uint16_t)(at + 2), u2 & 0x0F);
                poke8(g, (uint16_t)(at + 0x101), u3 & 0xF0);
            } else {
                poke8(g, (uint16_t)(at + 1), u1 & 0x0F);
                poke8(g, (uint16_t)(at + 0x102), u0 & 0xF0);
                poke8(g, (uint16_t)(at + 0x100), u2 & 0xF0);
                poke8(g, (uint16_t)(at + 0x101), u3);
            }
        } else {
            return -1;
        }
        x = s->link;
    }
    return 0;
}
