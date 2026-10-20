/*
 * The emulated board against MAME (label mame):
 *   test_emu <defender.zip|folder> <primed CMOS> <script> <capture dir>
 *            <trace dir> <trace from> <trace to> <frame>...
 * Runs the scripted game the probes ran (the same PROBE_SCRIPT string, e.g.
 * "1200:coin 1215: 1300:start 1315:") from power-on with the same CMOS and
 * checks it against MAME:
 *   - MAME's instruction traces of frames <trace from>-<trace to>
 *     (trace_probe.lua): every main-CPU instruction at the same cycle, every
 *     sound-CPU instruction in the same order
 *   - at each given frame, what capture_probe.lua dumped (capture
 *     dir/frame_<n>.txt) and MAME's snapshot (capture dir/<n>.png): RAM, the
 *     palette and the ROM bank, bar the main CPU's stack page, and the
 *     picture pixel for pixel.
 * MAME's frame callback, where the probes dump, runs a few instructions off
 * the frame boundary, so the stacks and the sound board's RAM (reported, not
 * checked) can hold a slightly different moment; the traces cover them.
 * Inputs a MAME probe sets in its frame callback reach the game at the start
 * of the frame after next (MAME updates its ports once per frame).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/png.h"
#include "assets/rom.h"
#include "emu/board.h"

#define STACK_PAGE 0xBF00 /* the main CPU's stack: $BF00-$BFFF */

static RomSet rom;
static Board board;
static uint8_t cmos[BOARD_CMOS_SIZE];

/* the probe's script: from each frame on, these switches are held */
typedef struct {
    long frame;
    uint8_t in0, in1, in2;
} Step;
static Step steps[1024];
static int nsteps;

static int parse_script(const char *text)
{
    const char *p = text;
    while (*p) {
        while (*p == ' ') p++;
        if (!*p) break;
        if (nsteps == (int)(sizeof steps / sizeof steps[0])) return -1;
        char *end;
        Step *s = &steps[nsteps++];
        memset(s, 0, sizeof *s);
        s->frame = strtol(p, &end, 10);
        if (end == p || *end != ':') return -1;
        p = end + 1;
        while (*p && *p != ' ') {
            size_t n = strcspn(p, ", ");
            static const struct { const char *name; int port; uint8_t bit; } names[] = {
                { "coin", 2, IN2_COIN1 }, { "start", 0, IN0_START1 }, { "start2", 0, IN0_START2 },
                { "up", 1, IN1_UP }, { "down", 0, IN0_DOWN }, { "thrust", 0, IN0_THRUST },
                { "reverse", 0, IN0_REVERSE }, { "fire", 0, IN0_FIRE }, { "bomb", 0, IN0_SMART_BOMB },
                { "hyper", 0, IN0_HYPERSPACE },
            };
            int ok = 0;
            for (size_t i = 0; i < sizeof names / sizeof names[0]; i++)
                if (strlen(names[i].name) == n && !strncmp(p, names[i].name, n)) {
                    uint8_t *port = names[i].port == 0 ? &s->in0 : names[i].port == 1 ? &s->in1 : &s->in2;
                    *port |= names[i].bit;
                    ok = 1;
                }
            if (!ok) return -1;
            p += n;
            if (*p == ',') p++;
        }
    }
    return 0;
}

static void script(Board *b, int line)
{
    if (line != BOARD_FIRST_LINE) return;
    long f = (long)b->frame - 1; /* the probe's frame number whose inputs apply now */
    for (int i = 0; i < nsteps; i++)
        if (steps[i].frame == f) {
            b->in0 = steps[i].in0;
            b->in1 = steps[i].in1;
            b->in2 = steps[i].in2;
        }
}

/* ---- traces ---------------------------------------------------------------- */

typedef struct {
    int64_t *cycles; /* main CPU only */
    uint32_t *pc;
    long n, cap;
} Trace;

#define INTERRUPT 0xFFFFFFFFu /* a sound trace entry: an interrupt was taken here */
#define SOUND_JITTER 32       /* instructions an interrupt may land away from MAME's */

static Trace mame_main, mame_sound, ours_main, ours_sound;
static long trace_from, trace_to;

static void trace_add(Trace *t, int64_t cycles, uint32_t pc)
{
    if (t->n == t->cap) {
        t->cap = t->cap ? t->cap * 2 : 1 << 16;
        t->cycles = realloc(t->cycles, (size_t)t->cap * sizeof *t->cycles);
        t->pc = realloc(t->pc, (size_t)t->cap * sizeof *t->pc);
    }
    t->cycles[t->n] = cycles;
    t->pc[t->n++] = pc;
}

/* MAME's trace: instruction lines are "[cycles ]PPPP: ..."; of the notes,
   "(interrupted at ...)" marks an interrupt. */
static int load_trace(Trace *t, const char *path, int with_cycles)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char line[512];
    while (fgets(line, sizeof line, f)) {
        char *p = line, *end;
        long long cycles = 0;
        if (with_cycles) {
            cycles = strtoll(p, &end, 10);
            if (end == p || *end != ' ') continue;
            p = end + 1;
        }
        unsigned pc;
        char colon;
        if (sscanf(p, "%4x%c", &pc, &colon) == 2 && colon == ':' && strlen(p) > 5) trace_add(t, cycles, pc);
        else if (!with_cycles && strstr(line, "(interrupted at")) trace_add(t, 0, INTERRUPT);
    }
    fclose(f);
    return 0;
}

static void main_step(Board *b)
{
    if (b->frame >= trace_from && b->frame < trace_to) trace_add(&ours_main, b->cpu.cycles, b->cpu.pc);
}

static void sound_step(void *user, const M6800 *cpu)
{
    Board *b = user;
    if (b->frame < trace_from || b->frame >= trace_to) return;
    /* this step takes an interrupt instead of the instruction at pc */
    trace_add(&ours_sound, 0, cpu->irq && !(cpu->cc & 0x10) ? INTERRUPT : cpu->pc);
}

/* MAME's main-CPU instructions, cycle and address, are the board's, which
   also lists the instruction an interrupt is taken before (once per
   interrupt). The traces start and stop at slightly different moments:
   only the cycles both cover are compared. */
static int compare_main(void)
{
    if (!mame_main.n || !ours_main.n) return -1;
    int64_t lo = mame_main.cycles[0] > ours_main.cycles[0] ? mame_main.cycles[0] : ours_main.cycles[0];
    int64_t hi = mame_main.cycles[mame_main.n - 1] < ours_main.cycles[ours_main.n - 1] ? mame_main.cycles[mame_main.n - 1]
                                                                                     : ours_main.cycles[ours_main.n - 1];
    long i = 0, j = 0, matched = 0, interrupts = 0;
    while (i < mame_main.n && mame_main.cycles[i] < lo) i++;
    while (j < ours_main.n && ours_main.cycles[j] < lo) j++;
    while (i < mame_main.n && mame_main.cycles[i] <= hi && j < ours_main.n) {
        if (mame_main.cycles[i] == ours_main.cycles[j] && mame_main.pc[i] == ours_main.pc[j]) {
            i++, j++, matched++;
        } else if (j + 1 < ours_main.n && mame_main.cycles[i] == ours_main.cycles[j + 1] &&
                   mame_main.pc[i] == ours_main.pc[j + 1]) {
            j++, interrupts++;
        } else {
            fprintf(stderr, "main CPU: MAME runs $%04X at cycle %lld, the board $%04X at %lld (frame %lld)\n",
                    (unsigned)mame_main.pc[i], (long long)mame_main.cycles[i], (unsigned)ours_main.pc[j],
                    (long long)ours_main.cycles[j],
                    (long long)(ours_main.cycles[j] / (BOARD_LINES * BOARD_CYCLES_PER_LINE)));
            return -1;
        }
    }
    printf("main CPU: %ld instructions and %ld interrupts as MAME's trace, cycle for cycle\n", matched, interrupts);
    return matched > 1000 ? 0 : -1;
}

/* The sound CPU against MAME, interrupt to interrupt. The sound board's
   interrupt handler starts every sound afresh (it resets the stack), so
   between two interrupts both must run the same instructions. Where each
   interrupt cuts in may differ by a few instructions: MAME delivers the main
   board's commands with some jitter (up to a dozen sound cycles late), where
   the board delivers them as the main CPU's store ends. Complete segments
   are compared (the traces start and stop at slightly different moments). */
static long next_interrupt(const Trace *t, long i)
{
    while (i < t->n && t->pc[i] != INTERRUPT) i++;
    return i;
}

static int compare_sound(void)
{
    long i = next_interrupt(&mame_sound, 0), j = next_interrupt(&ours_sound, 0);
    long segments = 0, instructions = 0, jitter = 0;
    for (;;) {
        long i_end = next_interrupt(&mame_sound, i + 1), j_end = next_interrupt(&ours_sound, j + 1);
        if (i_end >= mame_sound.n || j_end >= ours_sound.n) break;
        long nm = i_end - i - 1, no = j_end - j - 1, n = nm < no ? nm : no;
        for (long k = 1; k <= n; k++)
            if (mame_sound.pc[i + k] != ours_sound.pc[j + k]) {
                fprintf(stderr, "sound CPU: %ld instructions after interrupt %ld: MAME $%04X, the board $%04X\n", k,
                        segments + 1, (unsigned)mame_sound.pc[i + k], (unsigned)ours_sound.pc[j + k]);
                return -1;
            }
        long d = nm > no ? nm - no : no - nm;
        if (d > SOUND_JITTER) {
            fprintf(stderr, "sound CPU: interrupt %ld comes %ld instructions after MAME's\n", segments + 2, no - nm);
            return -1;
        }
        if (d > jitter) jitter = d;
        instructions += n;
        segments++;
        i = i_end;
        j = j_end;
    }
    if (segments < 2) {
        fprintf(stderr, "sound CPU: too few interrupts in the traces to compare\n");
        return -1;
    }
    printf("sound CPU: %ld instructions over %ld interrupts in MAME's order (interrupts within %ld instructions)\n",
           instructions, segments, jitter);
    return 0;
}

/* ---- captured frames ------------------------------------------------------- */

static int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int hex_bytes(const char *p, uint8_t *out, int n)
{
    for (int i = 0; i < n; i++) {
        int hi = hex(p[2 * i]), lo = hex(p[2 * i + 1]);
        if (hi < 0 || lo < 0) return -1;
        out[i] = (uint8_t)(hi << 4 | lo);
    }
    return 0;
}

/* Count the differences from a probe dump; print the first few. */
static long compare_dump(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) {
        fprintf(stderr, "cannot read %s\n", path);
        return -1;
    }
    static char line[2048];
    static uint8_t ram[WILLIAMS_VRAM_SIZE];
    uint8_t palette[16], sound[128];
    int cols = 0, have_palette = 0, have_sound = 0, bank = -1;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "palette ", 8)) {
            unsigned v[16];
            have_palette = sscanf(line + 8, "%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", &v[0], &v[1], &v[2],
                                  &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13],
                                  &v[14], &v[15]) == 16;
            for (int i = 0; i < 16; i++) palette[i] = (uint8_t)v[i];
        } else if (!strncmp(line, "vram ", 5)) {
            char *end;
            long col = strtol(line + 5, &end, 10);
            if (col >= 0 && col < 192 && *end == ' ' && !hex_bytes(end + 1, ram + col * 256, 256)) cols++;
        } else if (!strncmp(line, "bank ", 5)) {
            bank = atoi(line + 5);
        } else if (!strncmp(line, "sound ", 6)) {
            have_sound = !hex_bytes(line + 6, sound, 128);
        }
    }
    fclose(f);
    if (!have_palette || cols != 192 || !have_sound || bank < 0) {
        fprintf(stderr, "%s: not a capture frame\n", path);
        return -1;
    }
    long diff = 0, loose = 0;
    int shown = 0;
#define DIFF(count, what, addr, ours, mame)                                                              \
    do {                                                                                                 \
        if ((ours) != (mame)) {                                                                          \
            if (shown++ < 10) fprintf(stderr, "  %s $%04X: %02X, MAME %02X\n", what, addr, ours, mame); \
            count++;                                                                                     \
        }                                                                                                \
    } while (0)
    for (int a = 0; a < WILLIAMS_VRAM_SIZE; a++) {
        if (a >= STACK_PAGE) DIFF(loose, "stack", a, board.ram[a], ram[a]);
        else DIFF(diff, "RAM", a, board.ram[a], ram[a]);
    }
    for (int i = 0; i < 16; i++) DIFF(diff, "palette", i, board.palette[i], palette[i]);
    for (int i = 0; i < 128; i++) DIFF(loose, "sound RAM", i, board.sound.ram[i], sound[i]);
    DIFF(diff, "bank", 0xD000, board.bank, bank);
    if (loose) printf("  (%ld bytes of the stacks and sound RAM differ: not checked)\n", loose);
    return diff;
}

static long compare_shot(const Framebuffer *fb, const char *path)
{
    Image shot;
    const char *err;
    if (png_load(path, &shot, &err)) {
        fprintf(stderr, "%s: %s\n", path, err);
        return -1;
    }
    long diff = 0;
    for (int y = 0; y < FB_HEIGHT && y < shot.height; y++)
        for (int x = 0; x < FB_WIDTH && x < shot.width; x++) {
            const uint8_t *p = shot.rgba + ((size_t)y * (size_t)shot.width + (size_t)x) * 4;
            uint32_t want = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2], got = fb_row(fb, y)[x] & 0xFFFFFF;
            if (got == want) continue;
            if (diff++ < 4) fprintf(stderr, "  pixel %d,%d: %06X, MAME %06X\n", x, y, (unsigned)got, (unsigned)want);
        }
    image_free(&shot);
    return diff;
}

int main(int argc, char **argv)
{
    if (argc < 9) {
        fprintf(stderr, "usage: test_emu <rom> <primed cmos> <script> <capture dir> <trace dir> <trace from> "
                        "<trace to> <frame>...\n");
        return 2;
    }
    char err[512];
    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "%s\n", err);
        return 1;
    }
    FILE *f = fopen(argv[2], "rb");
    if (!f || fread(cmos, 1, sizeof cmos, f) != sizeof cmos) {
        fprintf(stderr, "%s: not a %d-byte CMOS image\n", argv[2], BOARD_CMOS_SIZE);
        return 1;
    }
    fclose(f);
    if (parse_script(argv[3])) {
        fprintf(stderr, "bad script '%s'\n", argv[3]);
        return 2;
    }
    char path[1200];
    snprintf(path, sizeof path, "%s/main.tr", argv[5]);
    int bad_trace = load_trace(&mame_main, path, 1);
    snprintf(path, sizeof path, "%s/sound.tr", argv[5]);
    bad_trace |= load_trace(&mame_sound, path, 0);
    if (bad_trace) {
        fprintf(stderr, "cannot read the traces in %s\n", argv[5]);
        return 1;
    }
    trace_from = strtol(argv[6], NULL, 10);
    trace_to = strtol(argv[7], NULL, 10);
    board_init(&board, &rom, cmos);
    board.line_hook = script;
    board.step_hook = main_step;
    board.sound.step_hook = sound_step;
    board.sound.user = &board;
    Framebuffer fb = { 0 };
    fb_init(&fb, 1);

    long last = trace_to;
    for (int i = 8; i < argc; i++) {
        long fr = strtol(argv[i], NULL, 10);
        if (fr > last) last = fr;
    }
    int failed = 0;
    while (board.frame < last) {
        board_run_frame(&board, &fb);
        for (int i = 8; i < argc; i++) {
            if (strtol(argv[i], NULL, 10) != board.frame) continue;
            snprintf(path, sizeof path, "%s/frame_%ld.txt", argv[4], (long)board.frame);
            long d = compare_dump(path);
            snprintf(path, sizeof path, "%s/%ld.png", argv[4], (long)board.frame);
            long p = compare_shot(&fb, path);
            printf("frame %ld: %ld bytes of RAM and %ld pixels differ from MAME\n", (long)board.frame, d, p);
            if (d || p) failed = 1;
        }
    }
    if (compare_main() || compare_sound()) failed = 1;
    fb_free(&fb);
    return failed;
}
