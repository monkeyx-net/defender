#include "sim/score.h"

#define PLAYER 0xA08B          /* <$8B: 1 or 2 */
#define SCORED 0xA08A          /* <$8A: a 1 is shifted in for every score */
#define SCRATCH 0xA073         /* <$73-<$74 */
#define EXTRA_LIFE 0xA0AB      /* <$AB-<$AC, BCD */
#define BANK 0xA036
#define DIGITS 0xC000          /* bank 2: the address of '0' in the font */
#define DIGIT_STEP 0x0400      /* 4 columns (8 pixels) a digit */

uint16_t score_player(const Game *g) { return g->ram[PLAYER] == 1 ? PLAYER_ONE : PLAYER_TWO; }

/* The 6809's ADDA/ADCA then DAA: a + b + carry in BCD; *carry in and out. */
static uint8_t bcd_add(uint8_t a, uint8_t b, int *carry)
{
    unsigned sum = (unsigned)a + b + (unsigned)*carry;
    int half = ((a & 0xF) + (b & 0xF) + *carry) > 0xF;
    int c = sum > 0xFF;
    uint8_t r = (uint8_t)sum;
    unsigned lsn = r & 0xF, msn = r >> 4, fix = 0;
    if (half || lsn > 9) fix |= 0x06;
    if (c || msn > 9 || (msn > 8 && lsn > 9)) fix |= 0x60;
    unsigned t = (unsigned)r + fix;
    *carry = c || t > 0xFF;
    return (uint8_t)t;
}

/* $F522: copy the w x h bytes of the picture at descriptor `desc` to `at`. */
static void draw_glyph(Game *g, uint16_t desc, uint16_t at)
{
    int w = g->data[desc], h = g->data[desc + 1];
    uint16_t src = (uint16_t)(g->data[desc + 2] << 8 | g->data[desc + 3]);
    for (int c = 0; c < w; c++)
        for (int r = 0; r < h; r++) game_poke(g, (uint16_t)(at + c * 0x100 + r), g->data[(uint16_t)(src + c * h + r)]);
}

/* $F57B: clear w x h bytes at `at`. */
static void clear_glyph(Game *g, uint16_t desc, uint16_t at)
{
    int w = g->data[desc], h = g->data[desc + 1];
    for (int c = 0; c < w; c++)
        for (int r = 0; r < h; r++) game_poke(g, (uint16_t)(at + c * 0x100 + r), 0);
}

void score_draw(Game *g)
{
    uint16_t at = g->ram[PLAYER] == 1 ? 0x0F1C : 0x711C;
    uint16_t digits = (uint16_t)(score_player(g) + 1);
    uint16_t zero = (uint16_t)(g->data[DIGITS] << 8 | g->data[DIGITS + 1]);
    uint8_t bank = g->ram[BANK];
    g->ram[BANK] = 2;
    g->ram[SCRATCH] = 0;
    for (int b = 6; b > 0; b--, at = (uint16_t)(at + DIGIT_STEP)) {
        uint8_t byte = (uint8_t)game_peek(g, digits);
        if (b & 1) digits++;
        uint8_t d = b & 1 ? byte & 0x0F : byte >> 4;
        if (!d && b > 2 && !g->ram[SCRATCH]) {
            clear_glyph(g, zero, at);
            continue;
        }
        g->ram[SCRATCH]++;
        draw_glyph(g, (uint16_t)(zero + d * 4), at);
    }
    g->ram[BANK] = bank;
}

int score_add(Game *g, uint16_t points)
{
    g->ram[SCORED] = (uint8_t)(g->ram[SCORED] << 1 | 1);
    uint8_t hi = (uint8_t)(points >> 8), lo = (uint8_t)points;
    int shift = hi & 1;
    uint8_t pair = hi >> 1;
    uint8_t a = 0, b = lo;
    if (shift) {
        a = (uint8_t)(lo >> 4);
        b = (uint8_t)(lo << 4);
    }
    uint16_t x = score_player(g);
    g->ram[SCRATCH] = a;
    g->ram[SCRATCH + 1] = b;
    int i = 3 - pair, carry = 0;
    game_poke(g, (uint16_t)(x + i), bcd_add((uint8_t)game_peek(g, (uint16_t)(x + i)), b, &carry));
    for (i--; i >= 0; i--) {
        game_poke(g, (uint16_t)(x + i), bcd_add((uint8_t)game_peek(g, (uint16_t)(x + i)), g->ram[SCRATCH], &carry));
        g->ram[SCRATCH] = 0;
    }
    if (g->ram[EXTRA_LIFE] || g->ram[EXTRA_LIFE + 1]) {
        /* $D3CA: the score (+1..+3) against the next life's (+4..+6) */
        uint16_t s = game_word(g, (uint16_t)(x + 1)), n = game_word(g, (uint16_t)(x + 4));
        int below = s != n ? s < n : game_peek(g, (uint16_t)(x + 3)) < game_peek(g, (uint16_t)(x + 6));
        if (!below) return -1; /* an extra life: not ported yet */
    }
    score_draw(g);
    return 0;
}
