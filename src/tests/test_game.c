/*
 * The C game's process steps against the emulated board (label rom):
 *   test_game <defender.zip|folder>
 * Plays the scripted game of test_sim on the board while a C model of all
 * of RAM (sim/game.h) follows it:
 *   - every write the board makes is copied to the model, except during a
 *     ported process step;
 *   - when the ROM's dispatcher runs a process whose step is ported
 *     ($E853), the C step runs on a copy of the model, which gives the
 *     bytes it changes; as the ROM's step writes a byte, the model takes
 *     the C value for it;
 *   - when the ROM's step yields (back at $E856), the model must equal
 *     RAM, and every byte the C step changed must have been written.
 * Interrupts run inside steps: their writes are copied as they happen. A
 * step an interrupt broke into may then differ (the interrupt can change
 * what the step reads): it must match RAM when the C step reads the state
 * from before the interrupt or from after it, and each byte both wrote
 * keeps the value of the later write in the ROM's order; or, for long
 * steps (the scanner), when each interrupt's writes land in the C step
 * after as many of its raw writes as the ROM's step had made. Between steps the
 * model must equal RAM at every frame (a check of the copying).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/rom.h"
#include "emu/board.h"
#include "sim/game.h"

#define PRIME_FRAMES 1800
#define FRAMES 5200
#define HUNTER_FRAMES 12000
#define DISPATCH 0xE853  /* JMP [2,U] */
#define DISPATCH_NEXT 0xE856
#define STACK 0xBF00     /* the stack page: copied, never compared */
#define IRQ_RETURN 0xDFC1 /* the interrupt handler's PULS CC,D,DP,X,Y,U,PC */

static RomSet rom;
static Board board;
static uint8_t data[0x10000];
static Game model, scratch, before, other;
static struct { uint16_t addr; uint8_t value; } irq_writes[8192];
static int irq_count;
static uint32_t step_seq[STACK], irq_seq[STACK], seq; /* when each byte was last written */
static uint16_t step_proc;
/* where each interrupt fell in the step: the ROM step's raw writes before it, and its writes */
static struct { long after; int first, count; } irqs[64];
static int irq_n;
static long step_raw_writes;

static int raw_address(uint16_t a)
{
    return !(a >= 0xA05F && a < 0xA06F) && !(a >= OBJECTS_FIRST && a < LARGE_FIRST + LARGES * LARGE_SIZE);
}
static uint8_t predicted[GAME_RAM], has_prediction[GAME_RAM], written[GAME_RAM];
static int step_active, step_interrupted, irq_depth, failures;
static struct { uint16_t resume; long n; } by_resume[32];
static long handler_calls[3]; /* the ROM's $F20B, $EF6D, $F1E0 */
static long steps, steps_unported, steps_interrupted, steps_matched, frame_checks, steps_reordered;

/* RAM outside the typed pools, compared with memcmp; the pools by byte. */
static const struct { uint16_t from, to; } raw[] = {
    { 0x0000, 0xA05F }, { 0xA06F, OBJECTS_FIRST }, { LARGE_FIRST + LARGES * LARGE_SIZE, STACK },
};

static void load_model(void)
{
    for (unsigned a = 0; a < STACK; a++) game_poke(&model, (uint16_t)a, board.ram[a]);
}

/* The first address where `m` differs from RAM, or -1. */
static long first_difference_of(const Game *m)
{
    for (size_t i = 0; i < sizeof raw / sizeof raw[0]; i++)
        if (memcmp(m->ram + raw[i].from, board.ram + raw[i].from, (size_t)(raw[i].to - raw[i].from)))
            for (unsigned a = raw[i].from; a < raw[i].to; a++)
                if (m->ram[a] != board.ram[a]) return a;
    for (unsigned a = 0xA05F; a < 0xA06F; a++)
        if (game_peek(m, (uint16_t)a) != board.ram[a]) return a;
    for (unsigned a = OBJECTS_FIRST; a < LARGE_FIRST + LARGES * LARGE_SIZE; a++)
        if (game_peek(m, (uint16_t)a) >= 0 && game_peek(m, (uint16_t)a) != board.ram[a]) return a;
    return -1;
}

static void apply_irq_writes(Game *m)
{
    for (int i = 0; i < irq_count; i++) game_poke(m, irq_writes[i].addr, irq_writes[i].value);
}

static Game reads, merged;

/* The C step run on `reads`, merged with the interrupt's writes in the ROM's order. */
static int merged_matches(uint16_t proc)
{
    other = reads;
    if (game_step(&other, proc) != STEP_DONE) return 0;
    merged = before;
    apply_irq_writes(&merged);
    for (unsigned a = 0; a < STACK; a++) {
        int now = game_peek(&other, (uint16_t)a);
        if (now == game_peek(&reads, (uint16_t)a)) continue;
        if (step_seq[a] && step_seq[a] > irq_seq[a]) game_poke(&merged, (uint16_t)a, (uint8_t)now);
    }
    return first_difference_of(&merged) < 0;
}

/* The C step with each interrupt's writes applied where it fell. */
static long c_raw_writes;
static int next_irq, replaying;

static void replay_hook(void *user, uint16_t addr)
{
    Game *g = user;
    if (replaying || !raw_address(addr)) return;
    while (next_irq < irq_n && irqs[next_irq].after == c_raw_writes) {
        replaying = 1;
        for (int i = irqs[next_irq].first; i < irqs[next_irq].first + irqs[next_irq].count; i++)
            game_poke(g, irq_writes[i].addr, irq_writes[i].value);
        replaying = 0;
        next_irq++;
    }
    c_raw_writes++;
}

static int matches_interleaved(uint16_t proc)
{
    if (irq_n == (int)(sizeof irqs / sizeof irqs[0])) return 0;
    other = before;
    other.on_write = replay_hook;
    other.user = &other;
    c_raw_writes = 0;
    next_irq = 0;
    StepResult r = game_step(&other, proc);
    other.on_write = NULL;
    while (next_irq < irq_n) { /* interrupts after the step's last write */
        for (int i = irqs[next_irq].first; i < irqs[next_irq].first + irqs[next_irq].count; i++)
            game_poke(&other, irq_writes[i].addr, irq_writes[i].value);
        next_irq++;
    }
    return r == STEP_DONE && first_difference_of(&other) < 0;
}

/* An interrupted step: does it match with the interrupt's effect on what it read before or after? */
static int matches_reordered(uint16_t proc)
{
    if (irq_count == (int)(sizeof irq_writes / sizeof irq_writes[0])) return 0;
    reads = before;            /* the step read before the interrupt */
    if (merged_matches(proc)) return 1;
    apply_irq_writes(&reads);  /* or after it */
    if (merged_matches(proc)) return 1;
    return matches_interleaved(proc); /* or where it fell */
}

/* The first address where the model differs from RAM, or -1. */
static long first_difference(void)
{
    return first_difference_of(&model);
}

static void report(long a, const char *what)
{
    if (failures++ < 8)
        fprintf(stderr, "test_game: frame %lld: %s: $%04lX = %02X, C %02X\n", (long long)board.frame, what, a,
                board.ram[a], game_peek(&model, (uint16_t)a) & 0xFF);
}

static void start_step(uint16_t proc)
{
    if (!game_step_ported(&model, proc)) return;
    steps++;
    before = model;
    irq_count = 0;
    irq_n = 0;
    step_raw_writes = 0;
    memset(step_seq, 0, sizeof step_seq);
    memset(irq_seq, 0, sizeof irq_seq);
    seq = 0;
    uint16_t resume = processes_at(&model.processes, proc)->resume;
    for (int i = 0; i < 32; i++)
        if (by_resume[i].resume == resume || !by_resume[i].resume) {
            by_resume[i].resume = resume;
            by_resume[i].n++;
            break;
        }
    scratch = model;
    if (game_step(&scratch, proc) != STEP_DONE) {
        steps_unported++;
        return; /* the ROM's writes are copied as usual */
    }
    memset(has_prediction, 0, sizeof has_prediction);
    memset(written, 0, sizeof written);
    for (unsigned a = 0; a < STACK; a++) {
        int was = game_peek(&model, (uint16_t)a), now = game_peek(&scratch, (uint16_t)a);
        if (was != now) {
            has_prediction[a] = 1;
            predicted[a] = (uint8_t)now;
        }
    }
    step_active = 1;
    step_interrupted = 0;
    step_proc = proc;
}

static void end_step(void)
{
    step_active = 0;
    long diff = -1;
    for (unsigned a = 0; a < STACK && diff < 0; a++)
        if (has_prediction[a] && !written[a] && predicted[a] != board.ram[a]) diff = a;
    if (diff < 0) diff = first_difference();
    if (diff < 0) {
        steps_matched++;
        return;
    }
    if (step_interrupted && matches_reordered(step_proc)) {
        steps_reordered++;
    } else {
        if (step_interrupted) steps_interrupted++;
        report(diff, step_interrupted ? "after an interrupted step" : "after a step");
    }
    load_model();
}

static void step_hook(Board *b)
{
    /* the board calls the hook also before an instruction the CPU then
       puts off for an interrupt; it comes again when the instruction runs */
    if ((b->cpu.irq && !(b->cpu.cc & CC_I)) || (b->cpu.firq && !(b->cpu.cc & CC_F))) return;
    if (b->cpu.last_interrupt) {
        irq_depth++;
        if (step_active) {
            step_interrupted = 1;
            if (irq_n < (int)(sizeof irqs / sizeof irqs[0])) {
                irqs[irq_n].after = step_raw_writes;
                irqs[irq_n].first = irq_count;
                irqs[irq_n].count = 0;
                irq_n++;
            }
        }
    }
    uint16_t pc = b->cpu.pc;
    if (pc == 0xF20B) handler_calls[0]++;
    if (pc == 0xEF6D) handler_calls[1]++;
    if (pc == 0xF1E0) handler_calls[2]++;
    if (pc == IRQ_RETURN && irq_depth > 0) {
        irq_depth--;
        return;
    }
    if (irq_depth) return;
    if (pc == DISPATCH && !step_active) start_step(b->cpu.u);
    else if (pc == DISPATCH_NEXT && step_active) end_step();
}

static void write_hook(Board *b, uint16_t a, uint8_t v)
{
    (void)b;
    if (a >= STACK || !step_active || irq_depth) {
        game_poke(&model, a, v);
        if (step_active && a < STACK) irq_seq[a] = ++seq;
        if (step_active && a < STACK && irq_count < (int)(sizeof irq_writes / sizeof irq_writes[0])) {
            irq_writes[irq_count].addr = a;
            irq_writes[irq_count].value = v;
            irq_count++;
            if (irq_n && irq_depth) irqs[irq_n - 1].count++;
        }
        return;
    }
    written[a] = 1;
    step_seq[a] = ++seq;
    if (raw_address(a)) step_raw_writes++;
    if (has_prediction[a]) game_poke(&model, a, predicted[a]);
    /* else C did not write it: the model keeps its byte, and differs if the ROM's does */
}

static int script; /* 0: test_sim's; 1: a hunter's */

/* The hunter: rapid fire all along, sweeping up and down, turning every 3 s. */
static void hunter(long f, uint8_t *in0, uint8_t *in1)
{
    *in0 = f % 8 < 4 ? IN0_FIRE : 0;
    if (f % 180 < 4) *in0 |= IN0_REVERSE;
    if (f % 360 < 60) *in0 |= IN0_THRUST;
    long phase = f % 240;
    if (phase < 100) *in1 = IN1_UP;
    else if (phase >= 120 && phase < 220) *in0 |= IN0_DOWN;
}

/* As test_sim: coin, start, then each control in turn. */
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
    if (script == 1) {
        hunter(f, in0, in1);
        return;
    }
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
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: test_game <defender.zip|folder> [hunter]\n");
        return 2;
    }
    if (argc == 3 && !strcmp(argv[2], "hunter")) script = 1;
    char err[512];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "test_game: %s\n", err);
        return 1;
    }
    /* the data the ported code reads: the fixed ROM, and bank 2 (pictures
       and the font) at $C000, as the drawing routines select it */
    for (unsigned a = 0xD000; a <= 0xFFFF; a++) data[a] = (uint8_t)rom_byte(&rom, a);
    for (unsigned a = 0xC000; a < 0xD000; a++) data[a] = (uint8_t)rom_bank_byte(&rom, 2, a - 0xC000);
    static uint8_t banks[8][0x1000];
    for (int b = 1; b <= 7; b++)
        for (unsigned a = 0; a < 0x1000; a++) banks[b][a] = (uint8_t)rom_bank_byte(&rom, b, a);
    for (int b = 1; b <= 7; b++) model.bank[b] = banks[b];
    model.data = scratch.data = data;
    for (int b = 1; b <= 7; b++) scratch.bank[b] = model.bank[b];
    static uint8_t cmos[BOARD_CMOS_SIZE];
    board_init(&board, &rom, NULL);
    for (int i = 0; i < PRIME_FRAMES; i++) board_run_frame(&board, NULL);
    memcpy(cmos, board.cmos, sizeof cmos);
    board_init(&board, &rom, cmos);
    load_model();
    board.step_hook = step_hook;
    board.write_hook = write_hook;
    for (long f = 1; f <= (script ? HUNTER_FRAMES : FRAMES); f++) {
        controls(f, &board.in0, &board.in1, &board.in2);
        board_run_frame(&board, NULL);
        if (!step_active) {
            frame_checks++;
            long d = first_difference();
            if (d >= 0) {
                report(d, "between steps");
                load_model();
            }
        }
    }
    printf("test_game: %ld steps of ported processes run: %ld matched, %ld matched with an interrupt's writes\n"
           "           reordered, %ld took paths not yet ported; %ld frames compared\n",
           steps, steps_matched, steps_reordered, steps_unported, frame_checks);
    printf("test_game: hit handlers the ROM ran: lander %ld (with the carrying one's), mutant %ld, carrying lander %ld\n",
           handler_calls[0], handler_calls[1], handler_calls[2]);
    printf("test_game: steps by where they resume:");
    for (int i = 0; i < 32 && by_resume[i].resume; i++) printf(" $%04X %ld", by_resume[i].resume, by_resume[i].n);
    printf("\n");
    if (steps_matched < 100) {
        fprintf(stderr, "test_game: too few steps matched\n");
        return 1;
    }
    if (failures) {
        fprintf(stderr, "test_game: %d differences\n", failures);
        return 1;
    }
    printf("test_game: OK\n");
    return 0;
}
