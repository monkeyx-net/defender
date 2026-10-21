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

void appear_start(Game *g, uint16_t obj)
{
    Object *o = objects_at(&g->objects, obj);
    if (!o) return;
    uint16_t picture = o->picture;
    o->picture = (uint16_t)(game_read(g, 0xFF9D) << 8 | game_read(g, 0xFF9E)); /* $F8EC */
    g->objects.screen = obj;
    uint16_t rel = (uint16_t)(o->x - game_word(g, SCROLL));
    uint16_t last = game_word(g, EXPLOSION_LAST), y = last;
    if (rel > 0x2600) {
        o->picture = picture;
        return;
    }
    for (;;) {
        y = y ? (uint16_t)(y + EXPLOSION_SIZE) : EXPLOSIONS;
        if (y == EXPLOSIONS_END) y = EXPLOSIONS;
        if (y == last) {
            o->picture = picture;
            return;
        }
        uint8_t state = (uint8_t)game_peek(g, y);
        if (state & 0x80) continue;
        if (state) explosion_erase(g, y);
        break;
    }
    if (!(g->ram[0xA0BA] & 0x80)) game_sound(g, (uint16_t)(game_read(g, 0xFFDD) << 8 | game_read(g, 0xFFDE)));
    o->flags |= 2;
    game_set_word(g, y, 0xAF00);
    game_set_word(g, (uint16_t)(y + 2), picture);
    game_set_word(g, (uint16_t)(y + 4), (uint16_t)(y + LIST_END));
    game_set_word(g, (uint16_t)(y + 0xA), obj);
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

#define RAMB(x) (g->ram[0xA000 + (x)])

void explosion_draw(Game *g, uint16_t y)
{
    game_set_word(g, 0xA0F6, y);
    uint8_t scale = (uint8_t)(game_peek(g, y) & 0x7F);
    RAMB(0xE7) = scale;
    uint16_t u = (uint16_t)(y + LIST_END);
    RAMB(0xE6) = 0;
    uint16_t pic = game_word(g, (uint16_t)(y + 2));
    uint16_t data = (uint16_t)(game_read(g, (uint16_t)(pic + 2)) << 8 | game_read(g, (uint16_t)(pic + 3)));
    game_set_word(g, 0xA0F3, data);
    RAMB(0xF1) = game_read(g, pic);
    RAMB(0xF2) = game_read(g, (uint16_t)(pic + 1));
    game_set_word(g, 0xA0ED, (RAMB(0xF2) & 1) ? 0xFEF3 : 0xFF27);
    uint16_t d = (uint16_t)(game_word(g, (uint16_t)(y + 6)) - game_word(g, (uint16_t)(y + 8)));
    RAMB(0xE4) = (uint8_t)(d >> 8);
    RAMB(0xE5) = (uint8_t)((uint8_t)d >> 1);
    RAMB(0xE6) = (uint8_t)(RAMB(0xE6) << 1 | (d & 1));
    game_set_word(g, 0xA0E9, (uint16_t)(scale * RAMB(0xE4)));
    d = (uint16_t)((uint16_t)(uint8_t)game_peek(g, (uint16_t)(y + 6)) - game_word(g, 0xA0E9));
    game_set_word(g, 0xA0E9, d);
    int done = 0;
    while (d >> 8) { /* columns off the left: their data skipped */
        game_set_word(g, 0xA0F3, (uint16_t)(game_word(g, 0xA0F3) + RAMB(0xF2)));
        if (!--RAMB(0xF1)) {
            done = 1;
            break;
        }
        d = (uint16_t)(game_word(g, 0xA0E9) + scale);
        game_set_word(g, 0xA0E9, d);
    }
    if (!done && (uint8_t)d > 0x98) done = 1;
    if (!done) {
        RAMB(0xE8) = (uint8_t)(scale << 1);
        game_set_word(g, 0xA0EB, (uint16_t)(RAMB(0xE8) * RAMB(0xE5)));
        d = (uint16_t)((uint16_t)(uint8_t)game_peek(g, (uint16_t)(y + 7)) - game_word(g, 0xA0EB));
        /* SUBB <$E6 then ADCA #0: the borrow is added */
        uint8_t b = (uint8_t)d, a = (uint8_t)(d >> 8);
        int borrow = b < RAMB(0xE6);
        b = (uint8_t)(b - RAMB(0xE6));
        a = (uint8_t)(a + borrow);
        RAMB(0xF5) = 0;
        while (!(a == 0 && b >= 0x2A)) { /* rows above line $2A: skipped in pairs */
            RAMB(0xF5)++;
            RAMB(0xF2) = (uint8_t)(RAMB(0xF2) - 2);
            if ((int8_t)RAMB(0xF2) <= 0) {
                done = 1;
                break;
            }
            unsigned sum = (unsigned)b + RAMB(0xE8);
            b = (uint8_t)sum;
            a = (uint8_t)(a + (sum > 0xFF));
        }
        if (!done) {
            game_set_word(g, 0xA0EB, (uint16_t)(a << 8 | b));
            uint16_t entry = (uint16_t)(game_read(g, (uint16_t)(0xFF48 + (RAMB(0xF2) & 0xFE))) << 8 |
                                        game_read(g, (uint16_t)(0xFF49 + (RAMB(0xF2) & 0xFE))));
            game_set_word(g, 0xA0EF, entry);
            int pairs = (0xFEEF - entry) / 12;
            uint16_t x = game_word(g, 0xA0F3);
            int carry = RAMB(0xF5) >> 7; /* ASL <$F5 */
            RAMB(0xF5) = (uint8_t)(RAMB(0xF5) << 1);
            uint8_t col = RAMB(0xEA);
            x = (uint16_t)(x + RAMB(0xF5));
            b = RAMB(0xEC);
            for (;;) {
                int k;
                for (k = 0; k < pairs; k++) {
                    u = (uint16_t)(u - 2);
                    uint16_t at = (uint16_t)(col << 8 | b);
                    game_set_word(g, u, at);
                    game_set_word(g, at, (uint16_t)(game_read(g, x) << 8 | game_read(g, (uint16_t)(x + 1))));
                    x = (uint16_t)(x + 2);
                    unsigned sum = (unsigned)b + RAMB(0xE8);
                    b = (uint8_t)sum;
                    carry = sum > 0xFF;
                    if (carry) {
                        x = (uint16_t)(x + 2 * (pairs - 1 - k)); /* the rest of the column's pairs */
                        break;
                    }
                }
                if (game_word(g, 0xA0ED) == 0xFEF3) { /* an odd last row */
                    if (carry) x++;
                    else {
                        u = (uint16_t)(u - 2);
                        uint16_t at = (uint16_t)(col << 8 | b);
                        game_set_word(g, u, at);
                        game_poke(g, at, game_read(g, x));
                        x++;
                    }
                }
                /* $FF27: the next column */
                if (!--RAMB(0xF1)) break;
                unsigned sum = (unsigned)col + scale;
                if (sum > 0xFF) break;
                col = (uint8_t)sum;
                if (col > 0x98) break;
                carry = col < 0x98;
                x = (uint16_t)(x + RAMB(0xF5));
                b = RAMB(0xEC);
            }
        }
    }
    /* $FF35 */
    game_set_word(g, (uint16_t)(y + 4), u);
    uint16_t c = game_word(g, (uint16_t)(y + 6));
    if ((c >> 8) <= 0x98) game_set_word(g, (uint16_t)((c & 0xFF00) | (uint8_t)(c - RAMB(0xE6))), 0); /* SUBB: no borrow */
}

static void appear_done(Game *g, uint16_t y)
{
    game_set_word(g, y, 0);
    Object *o = objects_at(&g->objects, game_word(g, (uint16_t)(y + 0xA)));
    if (o) {
        o->picture = game_word(g, (uint16_t)(y + 2));
        o->flags &= 0xFD;
    }
    explosion_erase(g, y);
}

void explosions_update(Game *g)
{
    for (uint16_t y = EXPLOSIONS; y != EXPLOSIONS_END; y = (uint16_t)(y + EXPLOSION_SIZE)) {
        if (g->ram[0xA0BA] & 0x04) {
            if (game_peek(g, y) & 0x80) appear_done(g, y);
            else game_set_word(g, y, 0);
            continue;
        }
        uint16_t d = game_word(g, y);
        if (!d) continue;
        if (d & 0x8000) {
            d = (uint16_t)(d - 0x100);
            game_set_word(g, y, d);
            if (!(d & 0x8000)) {
                appear_done(g, y);
                continue;
            }
            uint16_t obj = game_word(g, (uint16_t)(y + 0xA));
            uint16_t rel = (uint16_t)(game_word(g, (uint16_t)(obj + 0xA)) - game_word(g, SCROLL));
            uint8_t a = (uint8_t)((rel >> 8) + 0x0C);
            if (a & 0xC0) {
                appear_done(g, y);
                continue;
            }
            a = (uint8_t)(a - 0x0C);
            uint16_t corner = (uint16_t)((uint16_t)(a << 8 | (uint8_t)rel) << 2);
            corner = (uint16_t)((corner & 0xFF00) | (uint8_t)game_peek(g, (uint16_t)(obj + 0xC)));
            game_set_word(g, (uint16_t)(y + 8), corner);
            uint8_t m = (uint8_t)((uint16_t)((uint8_t)(corner >> 8) * 0xDA) >> 8);
            m = (uint8_t)(m << 1);
            uint16_t pic = game_word(g, (uint16_t)(y + 2));
            uint16_t centre = (uint16_t)((uint16_t)(m * game_read(g, pic)) & 0xFF00);
            centre = (uint16_t)(centre | (uint8_t)(game_read(g, (uint16_t)(pic + 1)) >> 1));
            game_set_word(g, (uint16_t)(y + 6), (uint16_t)(centre + corner));
        } else {
            d = (uint16_t)(d + 0x00AA);
            game_set_word(g, y, d);
            if ((d >> 8) > 0x30) {
                explosion_erase(g, y);
                game_set_word(g, y, 0);
                continue;
            }
            uint16_t now = (uint16_t)(game_word(g, SCROLL) & 0xFFC0), last = (uint16_t)(game_word(g, 0xA022) & 0xFFC0);
            uint8_t delta = (uint8_t)((uint16_t)((uint16_t)(last - now) << 2) >> 8);
            game_poke(g, (uint16_t)(y + 6), (uint8_t)(game_peek(g, (uint16_t)(y + 6)) + delta));
            game_poke(g, (uint16_t)(y + 8), (uint8_t)(game_peek(g, (uint16_t)(y + 8)) + delta));
        }
        explosion_erase(g, y);
        explosion_draw(g, y);
    }
}
