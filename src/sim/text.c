#include "sim/text.h"

#define FONT 0xC5E3
#define DRAW 0xFFB3
#define ERASE 0xFFB6
#define NEXT 0xCAED
#define ONE 0xCB5F
#define W(x) game_word(g, (uint16_t)(0xA000 + (x)))
#define SW(x, v) game_set_word(g, (uint16_t)(0xA000 + (x)), (uint16_t)(v))
#define R(x) (g->ram[0xA000 + (x)])

static uint16_t rd16(const Game *g, uint16_t a) { return (uint16_t)(game_read(g, a) << 8 | game_read(g, (uint16_t)(a + 1))); }

void glyph_draw(Game *g, uint16_t desc, uint16_t at)
{
    int w = game_read(g, desc), h = game_read(g, (uint16_t)(desc + 1));
    uint16_t src = rd16(g, (uint16_t)(desc + 2));
    for (int c = 0; c < w; c++)
        for (int r = 0; r < h; r++) game_poke(g, (uint16_t)(at + c * 0x100 + r), game_read(g, (uint16_t)(src + c * h + r)));
}

void glyph_clear(Game *g, uint16_t desc, uint16_t at)
{
    int w = game_read(g, desc), h = game_read(g, (uint16_t)(desc + 1));
    for (int c = 0; c < w; c++)
        for (int r = 0; r < h; r++) game_poke(g, (uint16_t)(at + c * 0x100 + r), 0);
}

/* A glyph at the cursor, which moves on. */
static void put_glyph(Game *g, uint16_t desc)
{
    uint16_t at = W(0x50);
    if (W(0x3D) == ERASE) glyph_clear(g, desc, at);
    else glyph_draw(g, desc, at);
    R(0x50) = (uint8_t)((at >> 8) + game_read(g, desc) + R(0x4C));
}

/* $CB29: the glyph for character b. */
static void put_char(Game *g, uint8_t b)
{
    b = (uint8_t)(b - 0x20);
    if (b > 0x01) {
        if (b <= 0x0B) b = 3;
        else {
            b = (uint8_t)(b - 0x0A);
            if (b > 0x10) {
                if (b <= 0x14) b = 3;
                else {
                    b = (uint8_t)(b - 4);
                    if (b > 0x2C) b = 3;
                }
            }
        }
    }
    put_glyph(g, (uint16_t)(FONT + b * 4));
}

int text_print(Game *g, uint16_t at, uint16_t message, int erase, int one_char)
{
    SW(0x3D, erase ? ERASE : DRAW);
    SW(0x3F, one_char ? ONE : NEXT);
    uint16_t x;
    int item = 0;
    if (!R(0x52)) {
        SW(0x50, at);
        SW(0x4E, at);
        SW(0x4C, 0x010A);
        R(0x58) = 0;
        SW(0x56, rd16(g, (uint16_t)(message + 2)));
        x = rd16(g, message);
        item = 1;
    }
    for (;;) {
        if (!item) {
            if (!R(0x58)) { /* $CAED: the word's next character */
                x = W(0x54);
                uint8_t b = game_read(g, x++);
                if (b == '/') {
                    b = 0x20;
                    R(0x58) = 0x20;
                }
                SW(0x54, x);
                put_char(g, b);
                if (W(0x3F) == ONE) return 0;
                continue;
            }
            R(0x58) = 0; /* $CAFF: the list's next item */
            x = W(0x52);
        }
        item = 0;
        for (;;) {
            if (x == W(0x56)) {
                R(0x52) = 0;
                return 1;
            }
            uint16_t u = rd16(g, x);
            x = (uint16_t)(x + 2);
            if (u & 0x8000) {
                SW(0x52, x);
                SW(0x54, u);
                break;
            }
            x--;
            uint8_t code = (uint8_t)(u >> 8);
            if (code > 8) {
                R(0x52) = 0;
                return 1;
            }
            switch (code) {
            case 0: R(0x4C) = game_read(g, x++); break;
            case 1: R(0x4D) = game_read(g, x++); break;
            case 2: R(0x50) = (uint8_t)(R(0x4E) + game_read(g, x++)); break;
            case 3: R(0x50) = (uint8_t)(R(0x50) + game_read(g, x++)); break;
            case 4: R(0x51) = (uint8_t)(R(0x4F) + game_read(g, x++)); break;
            case 5: R(0x51) = (uint8_t)(R(0x51) + game_read(g, x++)); break;
            case 6: {
                uint16_t d = rd16(g, x);
                x = (uint16_t)(x + 2);
                SW(0x4E, d);
                SW(0x50, d);
                break;
            }
            case 7: SW(0x50, (uint16_t)(R(0x4E) << 8 | (uint8_t)(R(0x51) + R(0x4D)))); break;
            case 8: {
                /* drawn at D as the dispatch left it ($10, the descriptor's
                   high byte), not at the cursor */
                uint16_t desc = rd16(g, x);
                x = (uint16_t)(x + 2);
                SW(0x52, x);
                uint16_t at8 = (uint16_t)(0x10 << 8 | (desc >> 8));
                if (W(0x3D) == ERASE) glyph_clear(g, desc, at8);
                else glyph_draw(g, desc, at8);
                R(0x50) = (uint8_t)(0x10 + game_read(g, desc) + R(0x4C));
                if (W(0x3F) == ONE) return 0;
                goto next_char;
            }
            }
        }
        continue;
    next_char:
        continue;
    }
}
