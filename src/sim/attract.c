#include "sim/attract.h"

#include "sim/background.h"
#include "sim/explosions.h"
#include "sim/flow.h"
#include "sim/score.h"
#include "sim/terrain.h"
#include "sim/text.h"
#include "sim/turn.h"

#define STATE 0xA0BA
#define CREDITS 0xA037
#define PLAYERS 0xA08C
#define SCORES_TODAY 0xB260
#define SCORES_ALL_TIME 0xC41D
#define LOGO 0xB300 /* its descriptor: $3C x $18 bytes at $B412 */
#define LOGO_DATA 0xB412
#define LOGO_STROKES 0xC941

static uint8_t peek(const Game *g, uint16_t a) { return (uint8_t)game_peek(g, a); }
static void poke(Game *g, uint16_t a, uint8_t v) { game_poke(g, a, v); }
static uint8_t rom(const Game *g, uint16_t a) { return game_read_bank(g, 1, a); }
static uint16_t rom_word(const Game *g, uint16_t a) { return (uint16_t)(rom(g, a) << 8 | rom(g, (uint16_t)(a + 1))); }
static uint16_t word(const Game *g, uint16_t a) { return game_word(g, a); }
static void set_word(Game *g, uint16_t a, uint16_t v) { game_set_word(g, a, v); }
static void create(Game *g, uint16_t at) { processes_create(&g->processes, at, 0, 0); }

static StepResult sleep(Game *g, uint16_t proc, uint8_t ticks, uint16_t resume)
{
    return game_sleep_banked(g, proc, ticks, resume);
}

/* A nibble as $F7F1 reads it: CMOS at $C400-$C4FF, else RAM. */
static uint8_t nibble(const Game *g, uint16_t a)
{
    return a >= 0xC400 && a < 0xC500 ? g->cmos[a & 0xFF] : peek(g, a);
}

static void set_nibble(Game *g, uint16_t a, uint8_t v)
{
    if (a >= 0xC400 && a < 0xC500) g->cmos[a & 0xFF] = (uint8_t)(v | 0xF0);
    else poke(g, a, v);
}

/* $F822 / $F842: a byte from / to two nibbles. */
static uint8_t read_byte(const Game *g, uint16_t a)
{
    return (uint8_t)(nibble(g, a) << 4 | (nibble(g, (uint16_t)(a + 1)) & 0x0F));
}

static void write_byte(Game *g, uint16_t a, uint8_t v)
{
    set_nibble(g, (uint16_t)(a + 1), v);
    set_nibble(g, a, (uint8_t)(v >> 4));
}

/* $C147: does the score of the player block at `block` beat the entry at `entry`? */
static int beats(const Game *g, uint16_t entry, uint16_t block)
{
    uint16_t d = (uint16_t)(read_byte(g, entry) << 8 | read_byte(g, (uint16_t)(entry + 2)));
    uint16_t s = word(g, (uint16_t)(block + 1));
    if (d != s) return d < s;
    return read_byte(g, (uint16_t)(entry + 4)) < peek(g, (uint16_t)(block + 3));
}

/* $C194: the player `<$A17B` sorted into the table from `x` up to (not) `top`. */
static void sort_in(Game *g, uint16_t x, uint16_t top)
{
    set_word(g, 0xA164, top);
    uint16_t block = word(g, 0xA17B);
    while (beats(g, x, block)) {
        for (int i = 0; i < 12; i += 4) { /* $C1C5: the entry a place down */
            uint16_t from = (uint16_t)(x + i);
            uint8_t a = read_byte(g, from), b = read_byte(g, (uint16_t)(from + 2));
            write_byte(g, (uint16_t)(from + 12), a);
            write_byte(g, (uint16_t)(from + 14), b);
        }
        x = (uint16_t)(x - 12);
        if (x <= word(g, 0xA164)) break;
    }
    x = (uint16_t)(x + 12);
    write_byte(g, x, peek(g, (uint16_t)(block + 1)));
    write_byte(g, (uint16_t)(x + 2), peek(g, (uint16_t)(block + 2)));
    write_byte(g, (uint16_t)(x + 4), peek(g, (uint16_t)(block + 3)));
    for (int i = 0; i < 3; i++) write_byte(g, (uint16_t)(x + 6 + i * 2), peek(g, (uint16_t)(0xA000 + i * 2)));
}

/* $C158: the initials. */
static void initials_draw(Game *g)
{
    game_clear(g, 0x46AC, 0x14, 0x08);
    text_print(g, 0x46AC, 0xC0FF, 0, 0);
}

/* $C16B: the bars under the initials, the current one bright. */
static void initials_bars(Game *g)
{
    poke(g, 0xA162, 0);
    for (uint16_t x = 0x45B7; x != 0x5DB7; x = (uint16_t)(x + 0x800)) {
        uint16_t u = peek(g, 0xA162) == peek(g, 0xA17A) ? 0xDDDD : 0x1111;
        for (int a = 4; a; a--) set_word(g, (uint16_t)(x + a * 0x100), u);
        poke(g, 0xA162, (uint8_t)(peek(g, 0xA162) + 1));
    }
}

/* $C936 */
static void scores_draw(Game *g)
{
    for (uint8_t a = peek(g, PLAYERS); a; a--) score_draw_player(g, a);
}

/* $D8DC / $D8EA: the interrupt's handler for player 1's side / for the
   player's (a cocktail table's player 2 side: $DFC3, flipped). */
static void irq_handler(Game *g, int player2)
{
    uint16_t x = player2 && (g->in1 & 0x80) ? 0xDFC3 : 0xDF17;
    set_word(g, 0xA090, x);
    poke(g, 0xA08F, 0x7E);
}

/* $C891: a run of the logo's code: 2 bits of length, 2 of colour. */
static uint8_t logo_run(Game *g, uint8_t a, uint16_t *x)
{
    if (!(a & 0x0C)) {
        a = (uint8_t)((a + peek(g, 0xA176)) << 2);
        poke(g, 0xA176, a);
        return a;
    }
    poke(g, 0xA176, (uint8_t)((a & 3) + peek(g, 0xA176)));
    poke(g, 0xA175, rom(g, (uint16_t)(0xCC0D + ((a & 0x0C) >> 2))));
    if (*x >= 0xB9B2) *x = (uint16_t)(*x - 0x059F);
    uint8_t n;
    a = peek(g, 0xA175);
    if (peek(g, 0xA177)) {
        poke(g, *x, peek(g, *x) & 0xF0);
        poke(g, *x, (uint8_t)((a & 0x0F) | peek(g, *x)));
        goto next;
    }
    poke(g, 0xA177, (uint8_t)~peek(g, 0xA177));
    for (;;) {
        poke(g, *x, a);
        n = (uint8_t)(peek(g, 0xA176) - 1);
        poke(g, 0xA176, n);
        if (n & 0x80) goto done;
    next:
        *x = (uint16_t)(*x + 0x18);
        n = (uint8_t)(peek(g, 0xA176) - 1);
        poke(g, 0xA176, n);
        if (n & 0x80) break;
    }
    poke(g, 0xA177, 0);
done:
    poke(g, 0xA176, 0);
    return a;
}

/* $C86F: the DEFENDER logo unpacked to $B412. */
static void logo_unpack(Game *g)
{
    uint16_t x = LOGO_DATA, y = 0xCAA0;
    poke(g, 0xA177, 0);
    poke(g, 0xA176, 0);
    uint8_t a;
    do {
        logo_run(g, rom(g, y) >> 4, &x);
        a = logo_run(g, rom(g, y++) & 0x0F, &x);
    } while (y != 0xCC0E);
    logo_run(g, a, &x);
}

/* $C306: a table's eight lines, from [<$A164] at [<$A181]. */
static void table_draw(Game *g)
{
    for (poke(g, 0xA006, 0x31); peek(g, 0xA006) != 0x39; poke(g, 0xA006, (uint8_t)(peek(g, 0xA006) + 1))) {
        uint8_t shown = 0;
        uint16_t x = word(g, 0xA164);
        for (uint16_t y = 0xA00C; y != 0xA012; y++) {
            uint8_t b = nibble(g, x++) & 0x0F;
            if (!b && !shown) b = 0x40;
            else {
                shown++;
                b = (uint8_t)(b + 0x30);
            }
            poke(g, y, b);
        }
        uint8_t a = read_byte(g, x), b = read_byte(g, (uint16_t)(x + 2));
        poke(g, 0xA008, a);
        poke(g, 0xA009, b);
        poke(g, 0xA00A, read_byte(g, (uint16_t)(x + 4)));
        set_word(g, 0xA164, (uint16_t)(x + 6));
        uint16_t at = word(g, 0xA181);
        text_print(g, at, 0xC103, 0, 0);
        set_word(g, 0xA181, (uint16_t)(at + 0x0A));
    }
}

/* $C43C: the demo's screen. */
static void demo_screen(Game *g)
{
    poke(g, STATE, 0xFF);
    objects_init(&g->objects);
    clear_screen(g);
    set_word(g, 0xA020, 0);
    set_word(g, 0xA022, 0);
    terrain_start(g);
    for (int i = 0; i < 16; i++) poke(g, (uint16_t)(0xA026 + i), game_read(g, (uint16_t)(0xF8BE + i)));
    poke(g, STATE, 0xDB);
    set_word(g, 0xA0BF, 0x1030);
}

/* A demo object ($D0AD): at rest, at x, y with a picture. */
static uint16_t demo_object(Game *g, uint16_t x, uint16_t y, uint16_t picture)
{
    uint16_t o = objects_alloc(&g->objects);
    if (!o) return 0;
    set_word(g, (uint16_t)(o + 0xE), 0);
    set_word(g, (uint16_t)(o + 0x10), 0);
    set_word(g, (uint16_t)(o + 0xA), x);
    set_word(g, (uint16_t)(o + 0xC), y);
    set_word(g, (uint16_t)(o + 2), picture);
    return o;
}

/* $C460: the demo's laser erased from its tail and its process killed. */
static void laser_stop(Game *g)
{
    uint16_t p = word(g, 0xA187), u = word(g, 0xA194);
    do {
        poke(g, u, 0);
        u = (uint16_t)(u + 0x100);
    } while (u <= word(g, (uint16_t)(p + 7)));
    processes_kill(&g->processes, p);
}

static void laser_start(Game *g)
{
    create(g, 0xC5F5);
    set_word(g, 0xA187, processes_last_created(&g->processes));
}

/* $C54F: the next enemy of the line-up appears at the top. */
static StepResult enemy_next(Game *g, uint16_t proc, uint16_t u)
{
    uint16_t o = objects_alloc(&g->objects);
    if (!o) return STEP_UNPORTED;
    set_word(g, (uint16_t)(o + 2), rom_word(g, (uint16_t)(u + 0x0C)));
    set_word(g, (uint16_t)(o + 0x12), rom_word(g, (uint16_t)(u + 0x24)));
    set_word(g, (uint16_t)(o + 0xA), 0x1F00);
    set_word(g, (uint16_t)(o + 0xC), 0xA000);
    set_word(g, (uint16_t)(o + 0x10), 0xFF40);
    set_word(g, (uint16_t)(o + 0xE), 0);
    appear_start(g, o);
    set_word(g, 0xA192, u);
    set_word(g, 0xA18D, o);
    return sleep(g, proc, 0x5F, 0xC585);
}

/* $C6B0: the Williams logo, `<$A16A` bytes of strokes a pass. */
static StepResult williams(Game *g, uint16_t proc)
{
    uint16_t x = 0;
    int plotted = 0;
pass:
    poke(g, 0xA163, peek(g, 0xA16A));
    uint16_t y = word(g, 0xA16B);
    for (;;) {
        uint8_t a = rom(g, y++);
        if (a > 0xAA) {
            a = (uint8_t)~a;
            if (!a) continue;
            if (--a) { /* $C711: the end */
                if (!plotted) return STEP_UNPORTED; /* X as the dispatch left it */
                set_word(g, 0xA168, x);
                if (peek(g, 0xA16A) == 3) {
                    poke(g, 0xA16A, 0x0A);
                    create(g, 0xC730);
                }
                set_word(g, 0xA16B, LOGO_STROKES);
                goto pass;
            }
            set_word(g, 0xA166, rom_word(g, y));
            y = (uint16_t)(y + 2);
            goto plot;
        }
        for (;;) {
            if (a & 0x80) poke(g, 0xA166, (uint8_t)(peek(g, 0xA166) - 1));
            if (a & 0x40) poke(g, 0xA166, (uint8_t)(peek(g, 0xA166) + 1));
            if (a & 0x20) poke(g, 0xA167, (uint8_t)(peek(g, 0xA167) - 1));
            if (a & 0x10) poke(g, 0xA167, (uint8_t)(peek(g, 0xA167) + 1));
            a = (uint8_t)(a << 4);
        plot:
            poke(g, 0xA162, a);
            {
                uint8_t px = peek(g, 0xA166);
                x = (uint16_t)((px >> 1) << 8 | peek(g, 0xA167));
                poke(g, x, (uint8_t)(peek(g, x) | ((px & 1) ? 0x0F : 0xF0)));
                plotted = 1;
            }
            a = peek(g, 0xA162);
            if (!a) break;
        }
        poke(g, 0xA163, (uint8_t)(peek(g, 0xA163) - 1));
        if (peek(g, 0xA163)) continue;
        set_word(g, 0xA16B, y);
        return sleep(g, proc, 2, 0xC6B0);
    }
}

int attract_ported(uint16_t at)
{
    switch (at) {
    case 0xC006:
    case 0xC0DA:
    case 0xC144:
    case 0xC1DC:
    case 0xC1E7:
    case 0xC1FA:
    case 0xC1FD:
    case 0xC263:
    case 0xC2EB:
    case 0xC412:
    case 0xC429:
    case 0xC475:
    case 0xC49C:
    case 0xC500:
    case 0xC531:
    case 0xC585:
    case 0xC598:
    case 0xC5CA:
    case 0xC5DA:
    case 0xC5ED:
    case 0xC5F5:
    case 0xC605:
    case 0xC64F:
    case 0xC66B:
    case 0xC677:
    case 0xC6B0:
    case 0xC730:
    case 0xC738:
    case 0xC74C:
    case 0xC754:
    case 0xC7B7:
    case 0xC7D4:
    case 0xC831:
    case 0xC848:
    case 0xC8F4:
    case 0xC921:
    case 0xC92E: return 1;
    }
    return 0;
}

StepResult attract_step(Game *g, uint16_t proc, uint16_t at)
{
    uint16_t x, y, o;
    uint8_t a;
    switch (at) {
    case 0xC006:
        kill_others(g);
        poke(g, STATE, 0xFF);
        stars_init(g);
        poke(g, 0xA183, peek(g, CREDITS));
        poke(g, 0xA184, 0);
        poke(g, 0xA178, 0);
        if (!peek(g, 0xA0B7)) goto logo;
        create(g, 0xC8F4);
        y = PLAYER_ONE;
        a = 1;
    player:
        poke(g, 0xA006, a);
        set_word(g, 0xA17B, y);
        if (!beats(g, 0xB2B4, y)) goto next_player;
        poke(g, 0xA178, (uint8_t)(peek(g, 0xA178) + 1));
        clear_screen(g);
        irq_handler(g, peek(g, 0xA006) != 1);
        poke(g, 0xA027, 0x85);
        a = beats(g, SCORES_TODAY, y) ? 0x3D : 0x3E;
        game_send_sound(g, 0x1B);
        game_send_sound(g, (uint8_t)(~a & 0x3F));
        text_print(g, 0x3E38, (uint16_t)(0xC0ED + peek(g, 0xA006) * 2), 0, 0);
        text_print(g, 0x1458, 0xC0FD, 0, 0);
        set_word(g, 0xA000, 0x412F);
        set_word(g, 0xA002, 0x402F);
        set_word(g, 0xA004, 0x402F);
        initials_draw(g);
        poke(g, 0xA17D, 0x28);
        create(g, 0xC1DC);
        create(g, 0xC1E7);
        create(g, 0xC1FA);
        poke(g, 0xA17A, 0);
        initials_bars(g);
    letter:
        poke(g, 0xA186, 0);
        poke(g, 0xA185, 0);
        return sleep(g, proc, 1, 0xC0DA);
    case 0xC0DA:
        if (peek(g, 0xA07B) & 0x01) {
            poke(g, 0xA186, 0);
            if (!peek(g, 0xA185)) return sleep(g, proc, 1, 0xC0DA);
            poke(g, 0xA17D, 0x14);
            poke(g, 0xA17A, (uint8_t)(peek(g, 0xA17A) + 1));
            initials_bars(g);
            if (peek(g, 0xA17A) != 3) goto letter;
        } else if (peek(g, 0xA17D)) {
            poke(g, 0xA186, (uint8_t)(peek(g, 0xA186) + 1));
            if (peek(g, 0xA186) == 5) poke(g, 0xA185, 5);
            return sleep(g, proc, 1, 0xC0DA);
        }
        /* $C10E */
        kill_others(g);
        sort_in(g, 0xB2A8, 0xB254);
        y = word(g, 0xA17B);
        if (beats(g, 0xC471, y)) sort_in(g, 0xC465, 0xC411);
    next_player:
        y = PLAYER_TWO;
        a = (uint8_t)(peek(g, 0xA006) + 1);
        if (a != 3) goto player;
        if (peek(g, 0xA178)) goto hall_of_fame;
        return sleep(g, proc, 0xFF, 0xC144);
    case 0xC144: goto hall_of_fame;
    case 0xC1DC:
        poke(g, 0xA17D, (uint8_t)(peek(g, 0xA17D) - 1));
        return sleep(g, proc, 0x3C, 0xC1DC);
    case 0xC1E7:
        poke(g, 0xA033, peek(g, 0xA033) ? 0 : peek(g, 0xA027));
        return sleep(g, proc, 0x0F, 0xC1E7);
    case 0xC1FA:
        poke(g, 0xA179, 0);
        /* fall through */
    case 0xC1FD:
        if (peek(g, 0xA07B) & 0x80) a = 0xFF;
        else if (peek(g, 0xA07D) & 0x01) a = 1;
        else {
            poke(g, 0xA179, 0);
            return sleep(g, proc, 1, 0xC1FD);
        }
        if (a != peek(g, 0xA179)) {
            poke(g, 0xA179, a);
            poke(g, 0xA17E, 0x37);
            poke(g, 0xA17F, 3);
            return sleep(g, proc, 1, 0xC1FD);
        }
        poke(g, 0xA17F, (uint8_t)(peek(g, 0xA17F) - 1));
        if (peek(g, 0xA17F)) return sleep(g, proc, 1, 0xC1FD);
        x = (uint16_t)(0xA000 + peek(g, 0xA17A) * 2);
        a = (uint8_t)(peek(g, x) + peek(g, 0xA179));
        if (a == 0x3F) a = 0x5A;
        if (a == 0x5B) a = 0x40;
        poke(g, x, a);
        initials_draw(g);
        a = (uint8_t)((peek(g, 0xA17E) >> 1) + 5);
        poke(g, 0xA17E, a);
        poke(g, 0xA17F, a);
        return sleep(g, proc, 1, 0xC1FD);

    case 0xC263:
    hall_of_fame:
        poke(g, 0xA162, 0);
        kill_others(g);
        logo_unpack(g);
        clear_screen(g);
        irq_handler(g, 0);
        poke(g, 0xA027, 0);
        create(g, 0xC8F4);
        scores_draw(g);
        text_print(g, 0x3854, 0xC101, 0, 0);
        for (a = 0x5F;;) {
            set_word(g, (uint16_t)(0x1E7B + a * 0x100), 0x1111);
            if (a == 0x41) a = 0x1F;
            if ((--a) & 0x80) break;
        }
        poke(g, 0xA007, 0x2F);
        poke(g, 0xA00B, 0x2F);
        poke(g, 0xA012, 0x2F);
        set_word(g, 0xA181, 0x1886);
        set_word(g, 0xA164, SCORES_TODAY);
        table_draw(g);
        set_word(g, 0xA181, 0x5986);
        set_word(g, 0xA164, SCORES_ALL_TIME);
        table_draw(g);
        poke(g, 0xA032, 0x3F);
        set_word(g, LOGO, 0x3C18);
        set_word(g, LOGO + 2, LOGO_DATA);
        glyph_draw(g, LOGO, 0x3038);
        create(g, COLOUR_CYCLE);
        poke(g, 0xA17D, 0x3C);
        /* fall through */
    case 0xC2EB:
        if (peek(g, 0xA184)) goto demo;
        if (peek(g, 0xA162)) goto hall_of_fame;
        poke(g, 0xA17D, (uint8_t)(peek(g, 0xA17D) - 1));
        if (!peek(g, 0xA17D)) goto demo;
        return sleep(g, proc, 0x0A, 0xC2EB);

    demo: /* $C351 */
        kill_others(g);
        demo_screen(g);
        poke(g, STATE, 0xD9);
        scores_draw(g);
        create(g, 0xC8F4);
        hud_frame(g);
        set_word(g, 0xA196, 0xCC63);
        create(g, COLOUR_CYCLE);
        create(g, FLASH);
        create(g, PALETTE_CYCLE);
        create(g, HOUSEKEEPING);
        create(g, 0xC64F);
        if (!(o = demo_object(g, 0x1E00, 0xDB00, 0xF901))) return STEP_UNPORTED; /* the humanoid */
        set_word(g, 0xA065, o);
        set_word(g, (uint16_t)(o + 0x12), 0x6666);
        set_word(g, 0xA189, o);
        if (!(o = demo_object(g, 0x0800, 0x5000, 0xF9C1))) return STEP_UNPORTED; /* the ship */
        set_word(g, 0xA065, o);
        set_word(g, (uint16_t)(o + 0x12), 0);
        set_word(g, 0xA18B, o);
        if (!(o = objects_alloc(&g->objects))) return STEP_UNPORTED; /* the lander */
        set_word(g, (uint16_t)(o + 2), 0xF985);
        set_word(g, (uint16_t)(o + 0xA), 0x1DA0);
        set_word(g, (uint16_t)(o + 0xC), 0x4000);
        set_word(g, (uint16_t)(o + 0x10), 0x00A0);
        set_word(g, (uint16_t)(o + 0xE), 0);
        set_word(g, (uint16_t)(o + 0x12), 0x4433);
        appear_start(g, o);
        set_word(g, 0xA18D, o);
        return sleep(g, proc, 0xE6, 0xC412);
    case 0xC412:
        set_word(g, (uint16_t)(word(g, 0xA18D) + 0x10), 0xFF50);
        set_word(g, (uint16_t)(word(g, 0xA189) + 0x10), 0xFF50);
        return sleep(g, proc, 0xA0, 0xC429);
    case 0xC429:
        laser_start(g);
        return sleep(g, proc, 0x15, 0xC475);
    case 0xC475:
        laser_stop(g);
        o = word(g, 0xA18D);
        if (objects_free(&g->objects, o)) return STEP_UNPORTED;
        explosion_start(g, o);
        o = word(g, 0xA18B);
        set_word(g, (uint16_t)(o + 0xE), 0x0040);
        set_word(g, (uint16_t)(o + 0x10), 0x00D4);
        poke(g, 0xA18F, 0x2D);
        set_word(g, (uint16_t)(word(g, 0xA189) + 0x10), 0);
        /* fall through */
    case 0xC49C:
        o = word(g, 0xA189);
        set_word(g, (uint16_t)(o + 0x10), (uint16_t)(word(g, (uint16_t)(o + 0x10)) + 8));
        poke(g, 0xA18F, (uint8_t)(peek(g, 0xA18F) - 1));
        if (peek(g, 0xA18F)) return sleep(g, proc, 2, 0xC49C);
        if (!(o = demo_object(g, 0x1DFF, 0x9000, 0xF9E7))) return STEP_UNPORTED; /* 500 */
        set_word(g, 0xA065, o);
        set_word(g, (uint16_t)(o + 0x12), 0);
        set_word(g, 0xA190, o);
        o = word(g, 0xA18B);
        set_word(g, (uint16_t)(o + 0xE), 0);
        set_word(g, (uint16_t)(o + 0x10), 0x00C0);
        o = word(g, 0xA189);
        set_word(g, (uint16_t)(o + 0xA), 0x1E80);
        set_word(g, (uint16_t)(o + 0xC), 0xA2E0);
        set_word(g, (uint16_t)(o + 0x10), 0x00C0);
        return sleep(g, proc, 0x50, 0xC500);
    case 0xC500:
        o = word(g, 0xA190);
        set_word(g, (uint16_t)(o + 0xC), 0xE000);
        set_word(g, (uint16_t)(o + 0xA), 0x1C00);
        set_word(g, (uint16_t)(word(g, 0xA189) + 0x10), 0);
        o = word(g, 0xA18B);
        set_word(g, (uint16_t)(o + 2), 0xF9CB);
        set_word(g, (uint16_t)(o + 0xE), 0xFFC0);
        set_word(g, (uint16_t)(o + 0x10), 0xFE80);
        return sleep(g, proc, 0x60, 0xC531);
    case 0xC531: {
        o = word(g, 0xA18B);
        set_word(g, (uint16_t)(o + 2), 0xF9C1);
        set_word(g, (uint16_t)(o + 0xE), 0);
        set_word(g, (uint16_t)(o + 0x10), 0);
        o = word(g, 0xA190);
        uint16_t at4 = word(g, (uint16_t)(o + 4));
        if (objects_free(&g->objects, o)) return STEP_UNPORTED;
        game_erase_picture(g, 0xF9E7, at4); /* $D350 */
        return enemy_next(g, proc, 0xCC7D);
    }
    case 0xC585:
        laser_start(g);
        return sleep(g, proc, 0x17, 0xC598);
    case 0xC598: {
        laser_stop(g);
        if (objects_free(&g->objects, word(g, 0xA18D))) return STEP_UNPORTED;
        if (!(o = objects_alloc(&g->objects))) return STEP_UNPORTED;
        explosion_start(g, o);
        uint16_t u = word(g, 0xA192);
        set_word(g, (uint16_t)(o + 0xA), rom_word(g, (uint16_t)(u + 0x18)));
        set_word(g, (uint16_t)(o + 0xC), rom_word(g, u));
        set_word(g, (uint16_t)(o + 0x10), 0);
        set_word(g, (uint16_t)(o + 0xE), 0);
        appear_start(g, o);
        set_word(g, 0xA192, (uint16_t)(u + 2));
        return sleep(g, proc, 0x20, 0xC5CA);
    }
    case 0xC5CA:
        set_word(g, 0xA196, (uint16_t)(word(g, 0xA196) + 2));
        return sleep(g, proc, 0x20, 0xC5DA);
    case 0xC5DA:
        if (word(g, 0xA192) != 0xCC89) return enemy_next(g, proc, word(g, 0xA192));
        return sleep(g, proc, 0xFF, 0xC5ED);
    case 0xC5ED: return sleep(g, proc, 0xFF, 0xC677);

    case 0xC5F5: /* the demo's laser, from the ship */
        x = (uint16_t)(word(g, (uint16_t)(word(g, 0xA18B) + 4)) + 0x0704);
        set_word(g, (uint16_t)(proc + 7), x);
        set_word(g, (uint16_t)(proc + 9), x);
        set_word(g, 0xA194, x);
        /* fall through */
    case 0xC605:
        x = word(g, (uint16_t)(proc + 7));
        for (int i = 0; i < 4; i++, x = (uint16_t)(x + 0x100)) poke(g, x, 0x11);
        poke(g, x, 0x99);
        set_word(g, (uint16_t)(proc + 7), x);
        y = word(g, 0xA0A4);
        if (y >= 0xA15F) y = 0xA142;
        x = word(g, (uint16_t)(proc + 9));
        for (int i = 0; i < 3; i++, x = (uint16_t)(x + 0x100)) poke(g, x, peek(g, y++));
        set_word(g, 0xA0A4, y);
        set_word(g, (uint16_t)(proc + 9), x);
        x = word(g, 0xA194);
        poke(g, x, 0);
        set_word(g, 0xA194, (uint16_t)(x + 0x100));
        return sleep(g, proc, 1, 0xC605);

    case 0xC64F: /* the enemies' names, up to [<$A196] */
        y = 0xCC61;
    name:
        text_print(g, rom_word(g, y), rom_word(g, (uint16_t)(y + 0x0E)), 0, 0);
        set_word(g, 0xA198, (uint16_t)(y + 2));
        return sleep(g, proc, 6, 0xC66B);
    case 0xC66B:
        y = word(g, 0xA198);
        if (y == word(g, 0xA196)) y = 0xCC61;
        goto name;

    case 0xC677:
    logo:
        kill_others(g);
        poke(g, 0xA184, 0);
        poke(g, STATE, 0xFB);
        clear_screen(g);
        poke(g, 0xA052, 0);
        set_word(g, 0xA059, 0xFFFF);
        create(g, COLOUR_CYCLE);
        create(g, PALETTE_CYCLE);
        poke(g, 0xA032, 0x3F);
        logo_unpack(g);
        poke(g, 0xA16A, 3);
        set_word(g, 0xA16B, LOGO_STROKES);
        /* fall through */
    case 0xC6B0: return williams(g, proc);
    case 0xC730:
        create(g, 0xC74C);
        /* fall through */
    case 0xC738:
        text_print(g, 0x3258, 0xC0ED, 0, 0);
        return sleep(g, proc, 5, 0xC738);
    case 0xC74C: return sleep(g, proc, 0x30, 0xC754);
    case 0xC754: /* the DEFENDER logo's blocks fly in */
        set_word(g, 0xA16D, 0xB3D6);
        set_word(g, 0xA16F, LOGO_DATA);
        set_word(g, 0xA020, 0);
        set_word(g, 0xA171, 0x0C00);
        set_word(g, 0xA173, 0xB304);
        do {
            x = word(g, 0xA173);
            y = word(g, 0xA16D);
            set_word(g, y, 0x040C);
            uint16_t d = word(g, 0xA16F);
            set_word(g, (uint16_t)(y + 2), d);
            set_word(g, 0xA16F, (uint16_t)(d + 0x60));
            set_word(g, (uint16_t)(x + 2), y);
            d = word(g, 0xA171);
            set_word(g, (uint16_t)(x + 0xA), d);
            set_word(g, 0xA171, (uint16_t)(d + 0x100));
            set_word(g, (uint16_t)(x + 0xC), 0x9800);
            appear_start(g, x);
            set_word(g, 0xA173, (uint16_t)(x + 0xE));
            y = (uint16_t)(y + 4);
            set_word(g, 0xA16D, y);
        } while (y != LOGO_DATA);
        return sleep(g, proc, 0x2E, 0xC7B7);
    case 0xC7B7:
        set_word(g, LOGO, 0x3C18);
        set_word(g, LOGO + 2, LOGO_DATA);
        create(g, 0xC848);
        return sleep(g, proc, 0x28, 0xC7D4);
    case 0xC7D4: {
        create(g, FLASH);
        uint16_t u = word(g, 0xA168);
        x = 0x3BD0;
        for (y = 0xCC11; y != 0xCC61;) {
            set_word(g, 0xA164, rom_word(g, y));
            y = (uint16_t)(y + 2);
            for (a = 1; a; a = (uint8_t)(a << 1)) {
                uint8_t b = (a & peek(g, 0xA164)) ? 0x10 : 0;
                if (a & peek(g, 0xA165)) b |= 0x01;
                poke(g, x++, b);
            }
            x = (uint16_t)(x + 0xF8);
        }
        a = (uint8_t)(~peek(g, (uint16_t)(0xA026 + rom(g, 0xC6F8))) & 7);
        if (!a) set_word(g, 0xA0B8, u);
        poke(g, 0xA0B7, 1);
        create(g, 0xC8F4);
        poke(g, 0xA17D, 0x3C);
    }
        /* fall through */
    case 0xC831:
        if (peek(g, 0xA184)) goto demo;
        poke(g, 0xA17D, (uint8_t)(peek(g, 0xA17D) - 1));
        if (!peek(g, 0xA17D)) goto hall_of_fame;
        return sleep(g, proc, 0x0A, 0xC831);
    case 0xC848:
        glyph_draw(g, LOGO, 0x3090);
        if (peek(g, 0x9C00) || peek(g, 0x9C40)) return sleep(g, proc, 1, 0xC848);
        create(g, 0xC921);
        game_end_process(g);
        return STEP_DONE;
    case 0xC921:
        poke(g, 0xA16A, 0xFF);
        return sleep(g, proc, 2, 0xC92E);
    case 0xC92E:
        poke(g, 0xA16A, 0x0A);
        game_end_process(g);
        return STEP_DONE;

    case 0xC8F4: /* the credits */
        a = peek(g, CREDITS);
        if (a) {
            if (a > peek(g, 0xA183)) {
                poke(g, 0xA183, a);
                poke(g, 0xA184, (uint8_t)(peek(g, 0xA184) + 1));
            }
            text_print(g, 0x28E5, 0xC0E9, 0, 0);
            number_print(g, 0x48E5, a);
        }
        return sleep(g, proc, 0x10, 0xC8F4);
    }
    return STEP_UNPORTED;
}
