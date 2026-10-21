#include "sim/terrain.h"

#define STREAM 0xC350
#define STREAM_END 0xC450
#define EVEN 0xB700
#define EVEN_END 0xB8C8
#define ODD 0xBA90
#define ODD_END 0xBC58
#define COPY 0x01C8
#define DRAWN 0xBE20
#define DRAWN_END 0xBF50
#define UP 0x7007    /* the colour words of a step up and down */
#define DOWN 0x0770

/* <$xx: the terrain's variables, $A000-$A017 */
#define V(x) (g->ram[0xA000 + (x)])
static uint16_t W(Game *g, int x) { return game_word(g, (uint16_t)(0xA000 + x)); }
static void SW(Game *g, int x, uint16_t v) { game_set_word(g, (uint16_t)(0xA000 + x), v); }
static uint8_t stream(Game *g, uint16_t a) { return game_read_bank(g, 7, a); }
static uint8_t rol(uint8_t a) { return (uint8_t)(a << 1 | a >> 7); }
static uint8_t ror(uint8_t a) { return (uint8_t)(a >> 1 | a << 7); }

/* $C2AA / $C2D0: the left / right cursor a bit on. */
static void left_on(Game *g)
{
    if (V(0x0F)) {
        V(0x0F)--;
        V(0x0D) = rol(V(0x0D));
        return;
    }
    uint16_t u = (uint16_t)(W(g, 0x09) + 1);
    if (u == STREAM_END) u = STREAM;
    SW(g, 0x09, u);
    V(0x0F) = 7;
    V(0x0D) = stream(g, u);
}

static void right_on(Game *g)
{
    if (V(0x10)) {
        V(0x10)--;
        V(0x0E) = rol(V(0x0E));
        return;
    }
    uint16_t u = (uint16_t)(W(g, 0x0B) + 1);
    if (u == STREAM_END) u = STREAM;
    SW(g, 0x0B, u);
    V(0x10) = 7;
    V(0x0E) = stream(g, u);
}

/* $C2F6 / $C323: the left / right cursor a bit back. */
static void left_back(Game *g)
{
    if (V(0x0F) != 7) {
        V(0x0F)++;
        V(0x0D) = ror(V(0x0D));
        return;
    }
    uint16_t u = W(g, 0x09);
    if (u == STREAM) u = STREAM_END;
    u--;
    SW(g, 0x09, u);
    V(0x0F) = 0;
    V(0x0D) = ror(stream(g, u));
}

static void right_back(Game *g)
{
    if (V(0x10) != 7) {
        V(0x10)++;
        V(0x0E) = ror(V(0x0E));
        return;
    }
    uint16_t u = W(g, 0x0B);
    if (u == STREAM) u = STREAM_END;
    u--;
    SW(g, 0x0B, u);
    V(0x10) = 0;
    V(0x0E) = ror(stream(g, u));
}

/* One buffer entry: its line (and the copy's), colour word. */
static void entry(Game *g, uint16_t x, uint8_t line, uint16_t colour)
{
    game_poke(g, x, line);
    game_poke(g, (uint16_t)(x + COPY), line);
    game_set_word(g, (uint16_t)(x + 1), colour);
    game_set_word(g, (uint16_t)(x + COPY + 1), colour);
}

/* $C12C: a step to the left (new terrain at the left edge). */
static void step_left(Game *g)
{
    right_back(g);
    if (V(0x0E) & 0x80) V(0x12)++;
    else V(0x12)--;
    int even = V(0x16) & 0x20;
    int cur = even ? 0x05 : 0x07;
    uint16_t x = W(g, cur), start = even ? EVEN : ODD, end = even ? EVEN_END : ODD_END;
    left_back(g);
    if (!(V(0x0D) & 0x80)) {
        V(0x11)--;
        entry(g, x, V(0x11), UP);
    } else {
        entry(g, x, V(0x11), DOWN);
        V(0x11)++;
    }
    x = (uint16_t)(x + 3);
    if (x == end) x = start;
    SW(g, cur, x);
}

/* $C1CD: a step to the right (new terrain at the right edge). */
static void step_right(Game *g)
{
    if (V(0x0D) & 0x80) V(0x11)--;
    else V(0x11)++;
    left_on(g);
    int even = !(V(0x16) & 0x20);
    int cur = even ? 0x05 : 0x07;
    uint16_t x = (uint16_t)(W(g, cur) - 3);
    if (x == (uint16_t)((even ? EVEN : ODD) - 3)) x = (uint16_t)((even ? EVEN_END : ODD_END) - 3);
    SW(g, cur, x);
    if (V(0x0E) & 0x80) {
        V(0x12)--;
        entry(g, x, V(0x12), DOWN);
    } else {
        entry(g, x, V(0x12), UP);
        V(0x12)++;
    }
    right_on(g);
}

void terrain_heights(Game *g)
{
    SW(g, 0x0B, STREAM);
    V(0x0E) = stream(g, STREAM);
    V(0x10) = 7;
    V(0x12) = 0xE0;
    for (uint16_t x = 0xB300; x != 0xB700; x++) {
        game_poke(g, x, V(0x12));
        for (int i = 0; i < 2; i++) {
            if (V(0x0E) & 0x80) V(0x12)--;
            else V(0x12)++;
            right_on(g);
        }
    }
}

void terrain_forget(Game *g)
{
    for (uint16_t y = DRAWN; y != DRAWN_END; y = (uint16_t)(y + 2)) game_set_word(g, game_word(g, y), 0);
}

void terrain_start(Game *g)
{
    uint16_t base = (uint16_t)(game_word(g, 0xA020) & 0xFFE0);
    SW(g, 0x17, base);
    SW(g, 0x15, (uint16_t)(base + 0x2610));
    V(0x0F) = 0;
    SW(g, 0x09, 0xC34F);
    V(0x11) = 0xE0;
    left_on(g);
    for (uint16_t x = 0x0010; x != W(g, 0x15); x = (uint16_t)(x + 0x20)) {
        if (V(0x0D) & 0x80) V(0x11)--;
        else V(0x11)++;
        left_on(g);
    }
    SW(g, 0x03, W(g, 0x09));
    V(0x00) = V(0x0F);
    V(0x01) = V(0x0D);
    V(0x02) = V(0x11);
    SW(g, 0x05, EVEN);
    SW(g, 0x07, ODD);
    for (;;) {
        uint16_t s = (uint16_t)(W(g, 0x15) - 0x20);
        SW(g, 0x15, s);
        if ((int16_t)(uint16_t)(s - W(g, 0x17)) < 0) break;
        step_left(g);
    }
    SW(g, 0x0B, W(g, 0x03));
    V(0x10) = V(0x00);
    V(0x0E) = V(0x01);
    V(0x12) = V(0x02);
    for (uint16_t y = DRAWN; y != DRAWN_END; y = (uint16_t)(y + 2)) game_set_word(g, y, 0);
}

void terrain_update(Game *g)
{
    uint16_t scroll = (uint16_t)(game_word(g, 0xA020) & 0xFFE0);
    int8_t steps = (int8_t)(uint8_t)((uint16_t)((uint16_t)(scroll - W(g, 0x15)) << 3) >> 8);
    V(0x00) = (uint8_t)steps;
    while (V(0x00)) {
        if ((int8_t)V(0x00) < 0) {
            SW(g, 0x15, (uint16_t)(W(g, 0x15) - 0x20));
            step_left(g);
            V(0x00)++;
        } else {
            SW(g, 0x15, (uint16_t)(W(g, 0x15) + 0x20));
            step_right(g);
            V(0x00)--;
        }
    }
    SW(g, 0x15, scroll);
    uint16_t s = W(g, (uint8_t)scroll & 0x20 ? 0x05 : 0x07), y = DRAWN;
    for (uint8_t column = 0x98; column; column--, s = (uint16_t)(s + 3), y = (uint16_t)(y + 2)) {
        game_set_word(g, game_word(g, y), 0);
        uint16_t at = (uint16_t)(column << 8 | game_peek(g, s));
        game_set_word(g, y, at);
        game_set_word(g, at, game_word(g, (uint16_t)(s + 1)));
    }
}
