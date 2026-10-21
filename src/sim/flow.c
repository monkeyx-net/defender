#include "sim/flow.h"

#include "sim/background.h"
#include "sim/cmos.h"
#include "sim/player.h"
#include "sim/score.h"
#include "sim/wave.h"

#define STATE 0xA0BA
#define CREDITS 0xA037
#define PLAYERS 0xA08C
#define PLAYER_UP 0xA08B
#define TURN 0xD919

void kill_others(Game *g)
{
    uint16_t x = PROCESS_HEAD;
    for (;;) {
        x = x == PROCESS_HEAD ? g->processes.head : processes_at(&g->processes, x)->link;
        if (!x) return;
        Process *p = processes_at(&g->processes, x);
        if (x == g->processes.current || p->type == 2) continue;
        x = processes_kill(&g->processes, x);
    }
}

void clear_screen(Game *g)
{
    for (uint16_t a = 0x9C00; a;) game_poke(g, --a, 0);
}

void clear_playfield(Game *g)
{
    for (int c = 0x9B; c >= 0; c--)
        for (int line = 0xFF; line >= 0x2A; line--) game_poke(g, (uint16_t)(c << 8 | line), 0);
}

void hud_frame(Game *g)
{
    for (uint16_t x = 0x0028; x < 0x9C00; x = (uint16_t)(x + 0x100)) game_set_word(g, x, 0x5555);
    for (uint16_t x = 0x2F08; x != 0x2F28; x = (uint16_t)(x + 2)) {
        game_set_word(g, (uint16_t)(x + 0x4100), 0x5555);
        game_set_word(g, x, 0x5555);
    }
    for (uint16_t x = 0x2F07; x != 0x7107; x = (uint16_t)(x + 0x100)) game_poke(g, x, 0x55);
    for (uint16_t x = 0x4C07; x != 0x5407; x = (uint16_t)(x + 0x100)) {
        game_set_word(g, x, 0x9999);
        game_set_word(g, (uint16_t)(x + 0x21), 0x9999);
    }
}

void hud_draw(Game *g)
{
    game_clear(g, 0x3008, 0x40, 0x20);
    hud_frame(g);
    hud_ships(g);
    hud_bombs(g);
    for (uint8_t p = g->ram[PLAYERS]; p; p--) score_draw_player(g, p);
}

void wave_setup(Game *g, uint16_t block)
{
    uint8_t *r = g->ram;
    uint8_t wave = (uint8_t)(game_peek(g, (uint16_t)(block + 8)) + 1);
    game_poke(g, (uint16_t)(block + 8), wave);
    uint8_t every = cmos_read(g, 0xC49D);
    r[0xA073] = every;
    if (every) {
        uint8_t a = wave;
        int borrow;
        do {
            borrow = a < every;
            a = (uint8_t)(a - every);
        } while (!borrow && a);
        if (!borrow) game_poke(g, (uint16_t)(block + 0xA), 0x0A);
    }
    r[0xA036] = 7; /* $F4FF */
    uint8_t col = (uint8_t)((wave > 4 ? 4 : wave) + 3);
    uint16_t u = (uint16_t)(game_read_bank(g, 7, 0xC011) << 8 | game_read_bank(g, 7, 0xC012));
    uint16_t end = (uint16_t)(game_read_bank(g, 7, 0xC013) << 8 | game_read_bank(g, 7, 0xC014));
    for (uint16_t x = (uint16_t)(block + 0xB); u != end; u = (uint16_t)(u + 8), x++) game_poke(g, x, game_read_bank(g, 7, (uint16_t)(u + col)));
    uint8_t extra = wave >= 4 ? (uint8_t)(wave - 4) : 0;
    r[0xA073] = extra;
    uint8_t lo = cmos_read(g, 0xC497), hi = cmos_read(g, 0xC499); /* $F838: two bytes */
    uint8_t n = (uint8_t)(bcd_to_binary(lo) + extra);
    r[0xA073] = n;
    if (!n) return;
    uint8_t cap = bcd_to_binary(hi);
    if (cap < n) r[0xA073] = n = cap;
    do wave_ramp(g, 3, (uint16_t)(score_player(g) + 0xB));
    while (--n);
}

/* $D82B: free play gives 2 credits. */
static void free_play(Game *g)
{
    if (cmos_read(g, 0xC495) == 1) g->ram[CREDITS] = 2;
}

/* $D861 */
void game_start(Game *g)
{
    uint8_t *r = g->ram;
    r[0xA038] = 0;
    if (!r[0xA0B7]) return;
    if (r[STATE] & 0x80) {
        kill_others(g);
        clear_screen(g);
        r[STATE] = 0x7F;
        r[PLAYER_UP] = 1;
        r[0xA025] = 1;
        r[PLAYERS] = 0;
        for (uint16_t x = 0xA1C2; x != 0xA23C; x++) game_poke(g, x, 0);
        uint8_t ships = cmos_read(g, 0xC485) & 0x0F;
        game_poke(g, 0xA1C9, ships);
        game_poke(g, 0xA1CB, ships);
        game_poke(g, 0xA1CC, 0x0A);
        r[0xA039] = 0;
        wave_setup(g, 0xA1C2);
        uint8_t hi = cmos_read(g, 0xC481), lo = cmos_read(g, 0xC483);
        r[0xA0AB] = hi;
        r[0xA0AC] = lo;
        game_poke(g, 0xA1C6, hi);
        game_poke(g, 0xA1C7, lo);
        game_poke(g, 0xA1C8, 0);
        for (uint16_t x = 0xA1C2; x != 0xA1FF; x++) game_poke(g, (uint16_t)(x + 0x3D), (uint8_t)game_peek(g, x));
        processes_create(&g->processes, TURN, 0, 0);
    }
    r[PLAYERS]++;
    uint8_t c = r[CREDITS];
    /* ADDA #$99, DAA: one less, in BCD */
    unsigned sum = (unsigned)c + 0x99;
    int half = ((c & 0xF) + 9) > 0xF;
    uint8_t v = (uint8_t)sum, fix = 0;
    if (half || (v & 0xF) > 9) fix |= 0x06;
    if (sum > 0xFF || (v >> 4) > 9 || ((v >> 4) > 8 && (v & 0xF) > 9)) fix |= 0x60;
    r[CREDITS] = (uint8_t)(v + fix);
    cmos_write(g, 0xC47D, r[CREDITS]);
    if (r[PLAYERS] != 1) hud_draw(g);
}

static StepResult end(Game *g)
{
    game_end_process(g);
    return STEP_DONE;
}

StepResult flow_step(Game *g, uint16_t proc, uint16_t resume)
{
    uint8_t *r = g->ram;
    switch (resume) {
    case COIN_1:
    case COIN_2:
    case COIN_3: {
        uint16_t timer = resume == COIN_1 ? 0xA07F : resume == COIN_3 ? 0xA080 : 0xA081;
        uint8_t entry = resume == COIN_1 ? 0x12 : resume == COIN_3 ? 0x15 : 0x18;
        if (r[0xA07E] || r[timer]) return end(g);
        r[timer] = 0x16;
        game_set_word(g, (uint16_t)(proc + 9), (uint16_t)(0xC0 << 8 | entry));
        processes_sleep(&g->processes, 10, COIN_WAIT);
        return STEP_DONE;
    }
    case COIN_WAIT: {
        if (r[0xA07E]) return end(g);
        game_sound(g, 0xD4AB);
        r[0xA036] = 3; /* $F507 */
        uint16_t entry = game_word(g, (uint16_t)(proc + 9));
        coin_inserted(g, entry == 0xC012 ? 1 : entry == 0xC018 ? 2 : 3);
        return end(g);
    }
    case START_1:
    case START_2:
        if (!(r[STATE] & 0x80)) return end(g);
        free_play(g);
        if (resume == START_1 ? !r[CREDITS] : r[CREDITS] < 2) return end(g);
        if (resume == START_1) {
            game_sound(g, 0xD4BD);
            game_start(g);
        } else {
            game_start(g);
            game_sound(g, 0xD4C2);
            game_start(g);
        }
        return end(g);
    }
    return STEP_UNPORTED;
}
