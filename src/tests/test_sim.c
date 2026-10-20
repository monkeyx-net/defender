/*
 * The C game against the emulated board (label rom):  test_sim <defender.zip|folder>
 * Plays a scripted game on the board (coin, start, then every control in
 * turn for a minute) and checks the ported routines as the ROM runs them:
 *   - rng.c: at every call of $D711, <$DF-<$E1 equal the C generator's,
 *     which then steps
 *   - ship.c: at every call of $E263, the ship and scroll state equals the
 *     C model's, which then steps with the controls and facing in RAM
 *   - sounds.c: at every frame's step ($D568) the sequencer's state equals
 *     the C one's (started by every $D54D), and each sound it sends ($D539)
 *     is the one the C sequencer returned
 *   - objects.c: the object pool and lists. Writes to them from code not
 *     yet ported are copied to the C pool; the ported routines (set up
 *     $D805, take $D0AD, set $D095, free $D0C7 / $D0F2, move $E376) run in
 *     C when the ROM calls them, and after each the lists and every block
 *     on them equal the board's RAM
 *   - laser.c: each laser process's head, middle and tail at every tick
 *     equal the C laser's, and every byte the C laser writes to the screen
 *     (and the colour pointer <$A4) is what the ROM wrote once it has drawn
 *     or erased
 * Code not yet ported also writes this state (a new ship, hyperspace): such
 * writes resynchronise the model from RAM, and are counted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/rom.h"
#include "emu/board.h"
#include "sim/laser.h"
#include "sim/objects.h"
#include "sim/rng.h"
#include "sim/ship.h"
#include "sim/sounds.h"

#define PRIME_FRAMES 1800
#define FRAMES 5200
#define RNG_CALL 0xD711
#define RNG_RETURN 0xD72E /* PULS B,PC */
#define RNG_END 0xD730
#define SHIP_CALL 0xE263
#define SHIP_END 0xE376
#define BA_NO_SCROLL 0x40
#define BA_FROZEN 0x40
/* the laser process: start, tick, drawn, end and ended, right and left */
#define LASER_START_R 0xE5C1
#define LASER_START_L 0xE630
#define LASER_TICK_R 0xE5CB
#define LASER_TICK_L 0xE638
#define LASER_DRAWN_R 0xE611
#define LASER_DRAWN_L 0xE67E
#define LASER_END_R 0xE621
#define LASER_END_L 0xE68D
#define LASER_ENDED 0xE69A
#define VRAM 0xC000
#define COLOURS_FILL 0xE69F
#define OBJ_SETUP_POOL 0xD805
#define OBJ_SETUP_POOL_END 0xD824
#define OBJ_CREATE 0xD095   /* BSR $D0AD, then the inline words */
#define OBJ_CREATE_SET 0xD099
#define OBJ_CREATE_END 0xD0AC
#define OBJ_ALLOC 0xD0AD
#define OBJ_ALLOC_END 0xD0C6
#define OBJ_FREE 0xD0C7
#define OBJ_FREE_SHOT 0xD0F2
#define OBJ_FREE_END 0xD0F8
#define OBJ_MOVE 0xE376
#define OBJ_MOVE_END 0xE39E
#define SOUNDS_START 0xD54D
#define SOUNDS_START_CLEARED 0xD551 /* <$AD cleared */
#define SOUNDS_START_DONE 0xD566     /* the script stored (the interrupt masked from $D55D) */
#define SOUNDS_TICK 0xD568
#define SOUNDS_TICK_DONE 0xD5A8
#define SOUNDS_SEND 0xD539
#define COLOURS_FILLED 0xE6B9

static RomSet rom;
static Board board;
static uint16_t pc; /* the instruction running */
static Rng rng;
static Ship ship;
static int rng_sync = 1, ship_sync = 1, failures;
static Objects objs;
static int objs_valid; /* from the first $D805 on */
/* ported routines running: compare when each returns (its return address and stack) */
static struct { uint16_t ret, sp; } obj_calls[8];
static int obj_depth;
static long obj_inits, obj_allocs, obj_setups, obj_frees, obj_moves;
static long obj_compares, obj_mirrored, obj_pauses;
static uint8_t fixed_rom[0x10000]; /* $D000-$FFFF: the scripts the C sequencer reads */
static Sounds sounds;
static int sounds_sync = 1, sound_expected = -1, sound_in_tick, sound_sent;
static int sound_pending = -1; /* a script $D54D is starting */
static long sound_ticks, sound_sends, sound_starts, sound_resyncs;
static long laser_ticks, laser_ends, laser_bytes, laser_left, laser_right, colour_fills;
static LaserColours colours_want;
static long colours_nested;
static int colours_pending;

/* One laser process, by its block (U), and what the C laser expects. */
typedef struct {
    uint16_t u;
    int have;          /* `next` holds the C laser's state for the next tick */
    Laser next;
    uint16_t colours;  /* <$A4 expected once drawn */
    int writes;        /* the expected bytes below are valid */
} LaserCheck;
static LaserCheck lasers[8];
static uint8_t before[VRAM], after[VRAM];
static uint8_t overdrawn[VRAM]; /* written since by other code (the interrupt drawing objects) */
static long laser_overdrawn;
#define LASER_CODE_FIRST 0xE591
#define LASER_CODE_LAST 0xE69E
static int rng_depth;
static uint16_t rng_stack[8]; /* S at each call in progress */
static long rng_checks, rng_resyncs, rng_nested, ship_checks, ship_resyncs;

static uint8_t *ram(unsigned a) { return &board.ram[a]; }
static uint16_t word(unsigned a) { return (uint16_t)(board.ram[a] << 8 | board.ram[a + 1]); }

static void ship_from_ram(Ship *s)
{
    uint32_t v = (uint32_t)*ram(0xA0C7) << 16 | (uint32_t)word(0xA0C8);
    s->velocity = (v & 0x800000) ? (int32_t)v - 0x1000000 : (int32_t)v;
    s->scroll = word(0xA020);
    s->last_scroll = word(0xA022);
    s->screen_x = word(0xA0C3);
    s->slide = (int16_t)word(0xA095);
    s->world_x = word(0xA0CC);
    s->y = word(0xA0C5);
    s->vy = (int16_t)word(0xA0CA);
}

static int ship_equal(const Ship *a, const Ship *b)
{
    return a->velocity == b->velocity && a->scroll == b->scroll && a->last_scroll == b->last_scroll &&
           a->screen_x == b->screen_x && a->slide == b->slide && a->world_x == b->world_x && a->y == b->y &&
           a->vy == b->vy;
}

static void report_ship(const Ship *want, const Ship *got)
{
    fprintf(stderr,
            "test_sim: frame %lld: ship differs (board / C):\n"
            "  velocity %06X / %06X  scroll %04X / %04X  last %04X / %04X  screen x %04X / %04X\n"
            "  slide %04X / %04X  world x %04X / %04X  y %04X / %04X  vy %04X / %04X\n",
            (long long)board.frame, (unsigned)want->velocity & 0xFFFFFF, (unsigned)got->velocity & 0xFFFFFF,
            want->scroll, got->scroll, want->last_scroll, got->last_scroll, want->screen_x, got->screen_x,
            (uint16_t)want->slide, (uint16_t)got->slide, want->world_x, got->world_x, want->y, got->y,
            (uint16_t)want->vy, (uint16_t)got->vy);
}

static LaserCheck *laser_check(uint16_t u)
{
    LaserCheck *free_one = NULL;
    for (int i = 0; i < 8; i++) {
        if (lasers[i].u == u) return &lasers[i];
        if (!lasers[i].u && !free_one) free_one = &lasers[i];
    }
    if (!free_one) {
        fprintf(stderr, "test_sim: more than 8 lasers\n");
        exit(1);
    }
    memset(free_one, 0, sizeof *free_one);
    free_one->u = u;
    return free_one;
}

static void laser_from_ram(Laser *l, uint16_t u, int left)
{
    l->head = word(u + 7);
    l->mid = word(u + 9);
    l->tail = word(u + 11);
    l->left = left;
}

/* Run the C laser on a copy of RAM; `after` holds what it wrote. */
static void laser_predict(LaserCheck *c, int left, int end)
{
    Laser l;
    LaserColours colours;
    laser_from_ram(&l, c->u, left);
    colours.next = word(0xA0A4);
    memcpy(colours.table, ram(LASER_COLOURS), sizeof colours.table);
    memcpy(before, board.ram, sizeof before);
    memcpy(after, board.ram, sizeof after);
    memset(overdrawn, 0, sizeof overdrawn);
    if (end) {
        laser_end(&l, after);
    } else if (laser_tick(&l, &colours, after, *ram(0xA0BA) & BA_FROZEN)) {
        c->writes = 0; /* it ends instead: checked at the end */
        return;
    }
    c->next = l;
    c->have = !end;
    c->colours = colours.next;
    c->writes = 1;
}

static void laser_compare(LaserCheck *c, const char *what)
{
    if (!c->writes) return;
    for (unsigned a = 0; a < VRAM; a++) {
        if (after[a] == before[a]) continue;
        if (overdrawn[a]) {
            laser_overdrawn++;
            continue;
        }
        laser_bytes++;
        if (board.ram[a] != after[a] && failures++ < 5)
            fprintf(stderr, "test_sim: frame %lld: laser %04X %s: $%04X = %02X, C %02X\n", (long long)board.frame,
                    c->u, what, a, board.ram[a], after[a]);
    }
    if (strcmp(what, "end") && word(0xA0A4) != c->colours && failures++ < 5)
        fprintf(stderr, "test_sim: frame %lld: laser colour pointer %04X, C %04X\n", (long long)board.frame,
                word(0xA0A4), c->colours);
    c->writes = 0;
}

static void laser_hook(Board *b)
{
    uint16_t u = b->cpu.u;
    switch (pc) {
    case LASER_START_R:
    case LASER_START_L: {
        LaserCheck *c = laser_check(u);
        if (pc == LASER_START_L) laser_left++;
        else laser_right++;
        laser_start(&c->next, (uint16_t)b->cpu.x, pc == LASER_START_L);
        c->have = 1;
        break;
    }
    case LASER_TICK_R:
    case LASER_TICK_L: {
        LaserCheck *c = laser_check(u);
        Laser now;
        laser_from_ram(&now, u, pc == LASER_TICK_L);
        if (c->have) {
            laser_ticks++;
            if ((now.head != c->next.head || now.mid != c->next.mid || now.tail != c->next.tail) && failures++ < 5)
                fprintf(stderr, "test_sim: frame %lld: laser %04X head/mid/tail %04X %04X %04X, C %04X %04X %04X\n",
                        (long long)b->frame, u, now.head, now.mid, now.tail, c->next.head, c->next.mid,
                        c->next.tail);
        }
        laser_predict(c, pc == LASER_TICK_L, 0);
        break;
    }
    case LASER_DRAWN_R:
    case LASER_DRAWN_L:
        laser_compare(laser_check(u), "tick");
        break;
    case LASER_END_R:
    case LASER_END_L:
        laser_predict(laser_check(u), pc == LASER_END_L, 1);
        break;
    case LASER_ENDED: {
        LaserCheck *c = laser_check(u);
        if (c->writes) laser_ends++;
        laser_compare(c, "end");
        c->u = 0; /* the block may be reused */
        break;
    }
    }
}

/* A byte as the main CPU sees it now (RAM, the bank at $C000, the fixed ROM). */
static int peek(uint16_t a)
{
    if (a >= 0xD000) return fixed_rom[a];
    if (a >= 0xC000) return board.bank ? rom_bank_byte(&rom, board.bank, a - 0xC000) : -1;
    return board.ram[a];
}

static uint16_t peek16(uint16_t a) { return (uint16_t)(peek(a) << 8 | peek((uint16_t)(a + 1))); }

static int obj_ported(uint16_t at)
{
    return (at >= OBJ_SETUP_POOL && at <= OBJ_SETUP_POOL_END) || (at >= OBJ_CREATE && at <= OBJ_FREE_END) ||
           (at >= OBJ_MOVE && at <= OBJ_MOVE_END);
}

/* Compare one list, and every block on it, with the board's RAM. */
static void obj_compare_list(uint16_t head_addr, const char *name)
{
    uint16_t a = word(head_addr);
    int n = 0, fields = head_addr != LIST_FREE; /* free blocks: only their links matter */
    if (objects_peek(&objs, head_addr) != *ram(head_addr) || objects_peek(&objs, head_addr + 1) != *ram(head_addr + 1)) {
        if (failures++ < 5)
            fprintf(stderr, "test_sim: frame %lld: list %s head %04X, C %02X%02X\n", (long long)board.frame, name, a,
                    objects_peek(&objs, head_addr), objects_peek(&objs, head_addr + 1));
        return;
    }
    while (a && n++ <= OBJECTS) {
        if (!objects_at(&objs, a)) {
            /* the attract mode's logo pieces are blocks outside the pool:
               pause until the pool is next set up (a game's start) */
            objs_valid = 0;
            obj_pauses++;
            return;
        }
        for (int i = 0; i < (fields ? OBJECT_SIZE : 2); i++) {
            int want = *ram(a + i), got = objects_peek(&objs, (uint16_t)(a + i));
            if (want != got) {
                if (failures++ < 5)
                    fprintf(stderr, "test_sim: frame %lld: %s object %04X +%02X = %02X, C %02X (after %04X)\n",
                            (long long)board.frame, name, a, i, want, got & 0xFF, pc);
                return;
            }
        }
        a = word(a);
    }
}

static void obj_compare(void)
{
    obj_compares++;
    obj_compare_list(LIST_SCREEN, "screen");
    if (objs_valid) obj_compare_list(LIST_OFF_SCREEN, "off-screen");
    if (objs_valid) obj_compare_list(LIST_SHOTS, "shots");
    if (objs_valid) obj_compare_list(LIST_FREE, "free");
}

static void obj_call(Board *b)
{
    if (obj_depth < (int)(sizeof obj_calls / sizeof obj_calls[0])) {
        obj_calls[obj_depth].ret = peek16(b->cpu.s);
        obj_calls[obj_depth].sp = (uint16_t)(b->cpu.s + 2);
        obj_depth++;
    }
}

static void objects_hook(Board *b)
{
    switch (pc) {
    case OBJ_SETUP_POOL:
        /* the blocks keep what they held: start from RAM the first time */
        if (!objs_valid)
            for (unsigned a = OBJECTS_FIRST; a < OBJECTS_FIRST + OBJECTS * OBJECT_SIZE; a++)
                objects_poke(&objs, (uint16_t)a, *ram(a));
        objects_init(&objs);
        obj_inits++;
        objs_valid = 1;
        obj_call(b);
        break;
    case OBJ_ALLOC:
        if (!objs_valid) break;
        objects_alloc(&objs);
        obj_allocs++;
        obj_call(b);
        break;
    case OBJ_CREATE_SET: {
        if (!objs_valid) break;
        /* after PSHS U,Y,D: the return address (the inline words) at S+6 */
        uint16_t in = peek16((uint16_t)(b->cpu.s + 6));
        objects_setup(&objs, b->cpu.x, b->cpu.u, peek16(in), peek16((uint16_t)(in + 2)), peek16((uint16_t)(in + 4)));
        obj_setups++;
        break;
    }
    case OBJ_FREE:
    case OBJ_FREE_SHOT:
        if (!objs_valid) break;
        if ((pc == OBJ_FREE ? objects_free(&objs, b->cpu.x) : objects_free_shot(&objs, b->cpu.x)) && failures++ < 5)
            fprintf(stderr, "test_sim: frame %lld: C cannot free object %04X\n", (long long)b->frame, b->cpu.x);
        obj_frees++;
        obj_call(b);
        break;
    case OBJ_MOVE:
        if (!objs_valid) break;
        objects_move(&objs, *ram(0xA0BA));
        obj_moves++;
        obj_call(b);
        break;
    }
}

static uint16_t sounds_b0(void) { return (uint16_t)((sounds.script - fixed_rom) + sounds.pos); }

static void sounds_hook(Board *b)
{
    switch (pc) {
    /* an interrupt's tick can run between $D54D's start and its stores: the
       C sequencer follows the ROM's order */
    case SOUNDS_START:
        sound_pending = b->cpu.a << 8 | b->cpu.b;
        break;
    case SOUNDS_START_CLEARED:
        sounds.thrust = 0;
        break;
    case SOUNDS_START_DONE:
        if (sound_pending >= 0 && !sounds_sync) {
            uint8_t thrust = sounds.thrust;
            sounds_start(&sounds, fixed_rom + sound_pending);
            sounds.thrust = thrust;
            sound_starts++;
        }
        sound_pending = -1;
        break;
    case SOUNDS_TICK:
        if (!sounds_sync) {
            sound_ticks++;
            if ((*ram(0xA0B2) != sounds.priority || *ram(0xA0B3) != sounds.timer || *ram(0xA0B4) != sounds.repeat ||
                 *ram(0xA0AD) != sounds.thrust || (sounds.script && word(0xA0B0) != sounds_b0())) &&
                failures++ < 5)
                fprintf(stderr,
                        "test_sim: frame %lld: sound sequencer B0-B4 %04X %02X %02X %02X AD %02X, C %04X %02X %02X %02X %02X\n",
                        (long long)b->frame, word(0xA0B0), *ram(0xA0B2), *ram(0xA0B3), *ram(0xA0B4), *ram(0xA0AD),
                        sounds_b0(), sounds.priority, sounds.timer, sounds.repeat, sounds.thrust);
        }
        sounds.script = fixed_rom + word(0xA0B0); /* from RAM, so a difference is reported once */
        sounds.pos = 0;
        sounds.priority = *ram(0xA0B2);
        sounds.timer = *ram(0xA0B3);
        sounds.repeat = *ram(0xA0B4);
        sounds.thrust = *ram(0xA0AD);
        sounds_sync = 0;
        sound_expected = sounds_tick(&sounds, *ram(0xA07B), *ram(0xA0BA));
        sound_in_tick = 1;
        sound_sent = 0;
        break;
    case SOUNDS_SEND:
        if (sound_in_tick) {
            sound_sent = 1;
            sound_sends++;
            if (b->cpu.b != sound_expected && failures++ < 5)
                fprintf(stderr, "test_sim: frame %lld: sound %02X sent, C %02X\n", (long long)b->frame, b->cpu.b,
                        sound_expected & 0xFF);
        }
        break;
    case SOUNDS_TICK_DONE:
        if (sound_in_tick && !sound_sent && sound_expected >= 0 && failures++ < 5)
            fprintf(stderr, "test_sim: frame %lld: no sound sent, C %02X\n", (long long)b->frame, sound_expected);
        sound_in_tick = 0;
        break;
    }
}

static void step_hook(Board *b)
{
    pc = b->cpu.pc;
    /* The board calls the hook before each step, also one that then takes
       an interrupt instead; it comes again when the instruction runs. */
    if ((b->cpu.irq && !(b->cpu.cc & CC_I)) || (b->cpu.firq && !(b->cpu.cc & CC_F))) return;
    uint16_t sp = b->cpu.s;
    if (pc == RNG_RETURN && rng_depth > 0 && sp == (uint16_t)(rng_stack[rng_depth - 1] - 1)) rng_depth--;
    if (pc == RNG_CALL) {
        /* the interrupt can call it while the main loop is part way through:
           both calls then work on the same bytes, so start afresh after it */
        if (rng_depth > 0) {
            rng_sync = 1;
            rng_nested++;
        }
        if (rng_depth < (int)(sizeof rng_stack / sizeof rng_stack[0])) rng_stack[rng_depth++] = sp;
        if (!rng_sync) {
            rng_checks++;
            if (rng.value != *ram(0xA0DF) || rng.hi != *ram(0xA0E0) || rng.lo != *ram(0xA0E1)) {
                if (failures++ < 5)
                    fprintf(stderr, "test_sim: frame %lld: random state %02X%02X%02X, C %02X%02X%02X\n",
                            (long long)b->frame, *ram(0xA0DF), *ram(0xA0E0), *ram(0xA0E1), rng.value, rng.hi,
                            rng.lo);
            }
        }
        rng.value = *ram(0xA0DF);
        rng.hi = *ram(0xA0E0);
        rng.lo = *ram(0xA0E1);
        rng_sync = 0;
        rng_step(&rng);
    }
    /* a ported object routine has returned: compare */
    if (obj_depth && pc == obj_calls[obj_depth - 1].ret && b->cpu.s == obj_calls[obj_depth - 1].sp) {
        obj_depth--;
        if (objs_valid) obj_compare();
    }
    if (pc == OBJ_SETUP_POOL || pc == OBJ_ALLOC || pc == OBJ_CREATE_SET || pc == OBJ_FREE || pc == OBJ_FREE_SHOT ||
        pc == OBJ_MOVE)
        objects_hook(b);
    if (pc >= LASER_START_R && pc <= LASER_ENDED) laser_hook(b);
    if (pc == SOUNDS_START || pc == SOUNDS_START_CLEARED || pc == SOUNDS_START_DONE || pc == SOUNDS_TICK || pc == SOUNDS_SEND || pc == SOUNDS_TICK_DONE) sounds_hook(b);
    if (pc == COLOURS_FILL) {
        Rng r = { *ram(0xA0DF), *ram(0xA0E0), *ram(0xA0E1) };
        laser_colours_fill(&colours_want, &r);
        colours_nested = rng_nested;
        colours_pending = 1;
    }
    if (pc == COLOURS_FILLED && colours_pending) {
        colours_pending = 0;
        /* an interrupt's call of the generator in between changes the draws */
        if (rng_nested == colours_nested) {
            colour_fills++;
            if ((memcmp(ram(LASER_COLOURS), colours_want.table, LASER_COLOURS_SIZE) ||
                 word(0xA0A4) != colours_want.next) && failures++ < 5)
                fprintf(stderr, "test_sim: frame %lld: laser colour table differs\n", (long long)b->frame);
        }
    }
    if (pc == SHIP_CALL) {
        Ship now;
        ship_from_ram(&now);
        if (!ship_sync) {
            ship_checks++;
            if (!ship_equal(&now, &ship) && failures++ < 5) report_ship(&now, &ship);
        }
        ship = now; /* from RAM, so one difference is reported once */
        ship_sync = 0;
        if (!(*ram(0xA0BA) & BA_NO_SCROLL)) {
            ShipInput in = { *ram(0xA07B), *ram(0xA07D), (int16_t)word(0xA0BD) };
            ship_step(&ship, &in);
        }
    }
}

static void write_hook(Board *b, uint16_t a, uint8_t v)
{
    (void)b;
    if (a < VRAM && !(pc >= LASER_CODE_FIRST && pc <= LASER_CODE_LAST)) overdrawn[a] = 1;
    if (a >= 0xA0DF && a <= 0xA0E1 && !(pc >= RNG_CALL && pc < RNG_END) && !rng_sync) {
        rng_sync = 1;
        rng_resyncs++;
    }
    if (objs_valid && objects_peek(&objs, a) >= 0 && !obj_ported(pc)) {
        objects_poke(&objs, a, v);
        obj_mirrored++;
    }
    if (((a >= 0xA0B0 && a <= 0xA0B4) || a == 0xA0AD) && !(pc >= SOUNDS_SEND && pc < SOUNDS_TICK_DONE) &&
        !sounds_sync) {
        sounds_sync = 1;
        sound_resyncs++;
    }
    int ship_ram = a == 0xA020 || a == 0xA021 || a == 0xA022 || a == 0xA023 || a == 0xA095 || a == 0xA096 ||
                   (a >= 0xA0C3 && a <= 0xA0CD);
    if (ship_ram && !(pc >= SHIP_CALL && pc < SHIP_END) && !ship_sync) {
        ship_sync = 1;
        ship_resyncs++;
    }
}

/* Coin and start, then each control in turn (as src/tests/mame/script.cmake,
   but fire is pressed afresh every 8 frames, so there are many lasers). */
static void controls(long f, uint8_t *in0, uint8_t *in1, uint8_t *in2)
{
    static const struct { uint8_t in0, in1; int hold; } moves[] = {
        { IN0_THRUST | IN0_FIRE, 0, 0 }, { IN0_THRUST, IN1_UP, 0 }, { IN0_FIRE, 0, 0 },
        { IN0_DOWN | IN0_FIRE, 0, 0 },   { IN0_REVERSE, 0, 4 },     { IN0_THRUST, IN1_UP, 0 },
        { IN0_FIRE | IN0_DOWN, 0, 0 },   { IN0_SMART_BOMB, 0, 4 },  { IN0_THRUST, 0, 0 },
        { IN0_HYPERSPACE, 0, 4 },        { IN0_THRUST | IN0_FIRE, 0, 0 }, { 0, IN1_UP, 0 },
    };
    *in0 = *in1 = *in2 = 0;
    if (f >= 1200 && f < 1215) *in2 = IN2_COIN1;
    if (f >= 1300 && f < 1315) *in0 = IN0_START1;
    if (f < 1400) return;
    long start = 1400;
    for (int k = 0;; k++) {
        int m = k % (int)(sizeof moves / sizeof moves[0]);
        int hold = moves[m].hold ? moves[m].hold : 20 + (k * 7) % 25;
        if (f < start + hold) {
            *in0 = moves[m].in0;
            if (f % 8 >= 4) *in0 &= (uint8_t)~IN0_FIRE;
            *in1 = moves[m].in1;
            return;
        }
        if (f < start + hold + 6) return;
        start += hold + 6;
    }
}

int main(int argc, char **argv)
{
    if (argc != 2) {
        fprintf(stderr, "usage: test_sim <defender.zip|folder>\n");
        return 2;
    }
    char err[512];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "test_sim: %s\n", err);
        return 1;
    }
    static uint8_t cmos[BOARD_CMOS_SIZE];
    board_init(&board, &rom, NULL);
    for (int i = 0; i < PRIME_FRAMES; i++) board_run_frame(&board, NULL);
    memcpy(cmos, board.cmos, sizeof cmos);
    board_init(&board, &rom, cmos);
    for (unsigned a = 0xD000; a <= 0xFFFF; a++) fixed_rom[a] = (uint8_t)rom_byte(&rom, a);
    board.step_hook = step_hook;
    board.write_hook = write_hook;
    for (long f = 1; f <= FRAMES; f++) {
        controls(f, &board.in0, &board.in1, &board.in2);
        board_run_frame(&board, NULL);
    }
    printf("test_sim: random %ld calls checked (%ld resyncs, %ld nested), ship %ld frames checked (%ld resyncs)\n",
           rng_checks, rng_resyncs, rng_nested, ship_checks, ship_resyncs);
    printf("test_sim: sounds: %ld frames, %ld scripts started, %ld sounds sent (%ld resyncs)\n", sound_ticks,
           sound_starts, sound_sends, sound_resyncs);
    printf("test_sim: objects: %ld set-ups, %ld taken, %ld set, %ld freed, %ld moves; %ld comparisons, %ld writes\n"
           "          from unported code copied, %ld pauses\n",
           obj_inits, obj_allocs, obj_setups, obj_frees, obj_moves, obj_compares, obj_mirrored, obj_pauses);
    printf("test_sim: laser colour tables: %ld checked\n", colour_fills);
    printf("test_sim: lasers: %ld right, %ld left, %ld ticks, %ld ends, %ld screen bytes checked (%ld drawn over since)\n", laser_right, laser_left, laser_ticks,
           laser_ends, laser_bytes, laser_overdrawn);
    if (rng_checks < 1000 || ship_checks < 1000 || laser_ticks < 100 || laser_ends < 10 ||
        sound_ticks < 1000 || sound_sends < 50 || obj_compares < 1000 ||
        obj_allocs < 50 || obj_frees < 10) {
        fprintf(stderr, "test_sim: too few checks\n");
        return 1;
    }
    if (failures) {
        fprintf(stderr, "test_sim: %d differences\n", failures);
        return 1;
    }
    printf("test_sim: OK\n");
    return 0;
}
