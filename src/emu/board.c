#include "emu/board.h"

#include <string.h>

static void update_irq(Board *b)
{
    b->cpu.irq = pia_irq_a(&b->pia[1]) || pia_irq_b(&b->pia[1]);
}

static void beam_catch_up(Board *b);

/* ---- the PIAs' connections -------------------------------------------- */

static uint8_t pia0_read_a(void *ctx) { return ((Board *)ctx)->in0; }
static uint8_t pia0_read_b(void *ctx) { return ((Board *)ctx)->in1; }
static uint8_t pia1_read_a(void *ctx) { return ((Board *)ctx)->in2; }
/* The sound board's clock for a main-CPU cycle count. */
static int64_t sound_cycles(int64_t main_cycles) { return (int64_t)((double)main_cycles * SOUND_CPU_HZ / 1e6); }

/* A command reaches the sound board when the store's bus cycle ends (the
   instruction's last cycle): it is held until the main CPU's instruction is
   done, the sound CPU catches up to that moment, then the latch changes. */
static void pia1_write_b(void *ctx, uint8_t pins)
{
    Board *b = ctx;
    b->sound_pending = 1;
    b->sound_latch = pins | 0xC0; /* the top two bits are pulled up on the board */
}

static void send_sound_command(Board *b)
{
    b->sound_pending = 0;
    sound_run(&b->sound, sound_cycles(b->cpu.cycles));
    sound_command(&b->sound, b->sound_latch);
}

/* ---- memory map ------------------------------------------------------------ */

/* The PIA at an I/O page offset ($C000 = 0), or NULL. */
static Pia *io_pia(Board *b, unsigned o)
{
    if ((o & 0xC1C) == 0xC00) return &b->pia[1];
    if ((o & 0xC1C) == 0xC04) return &b->pia[0];
    return NULL;
}

static uint8_t io_read(Board *b, unsigned o)
{
    if (o >= 0xC00) {
        Pia *p = io_pia(b, o);
        if (!p) return 0;
        uint8_t v = pia_read(p, o & 3);
        update_irq(b);
        return v;
    }
    if (o >= 0x800) { /* video counter: the beam's line as the read lands, at the load's last cycle */
        int line = (int)((BOARD_FIRST_LINE + b->cpu.op_end / BOARD_CYCLES_PER_LINE) % BOARD_LINES);
        return (uint8_t)(line & 0xFC);
    }
    if (o >= 0x400) return b->cmos[o & 0xFF];
    return 0; /* palette and video control (write-only) */
}

static void io_write(Board *b, unsigned o, uint8_t v)
{
    if (o >= 0xC00) {
        Pia *p = io_pia(b, o);
        if (p) {
            pia_write(p, o & 3, v);
            update_irq(b);
        }
        return;
    }
    if (o >= 0x800) return;
    if (o >= 0x400) {
        uint8_t nv = v | 0xF0; /* 4-bit CMOS */
        if (b->cmos[o & 0xFF] != nv) b->cmos_dirty = 1;
        b->cmos[o & 0xFF] = nv;
        return;
    }
    /* $C3FC is the watchdog and $C010-$C01F (mirrored) the video control
       (screen flip for cocktail tables): neither is emulated */
    if (!(o & 0x10)) {
        beam_catch_up(b);
        b->palette[o & 0x0F] = v;
    }
}

static uint8_t bus_read(void *ctx, uint16_t a)
{
    Board *b = ctx;
    if (a < WILLIAMS_VRAM_SIZE) return b->ram[a];
    if (a >= 0xD000) return (uint8_t)rom_byte(b->rom, a);
    if (b->bank == 0) return io_read(b, a & 0xFFF);
    return (uint8_t)rom_bank_byte(b->rom, b->bank, a & 0xFFF);
}

static void bus_write(void *ctx, uint16_t a, uint8_t v)
{
    Board *b = ctx;
    if (b->write_hook) b->write_hook(b, a, v);
    if (a < WILLIAMS_VRAM_SIZE) {
        if (a < WILLIAMS_SCREEN_SIZE) beam_catch_up(b);
        b->ram[a] = v;
    } else if (a >= 0xE000) return;
    else if (a >= 0xD000) b->bank = v & 0x0F;
    else if (b->bank == 0) io_write(b, a & 0xFFF, v);
}

/* ---- frame ------------------------------------------------------------------ */

void board_init(Board *b, const RomSet *rom, const uint8_t *cmos)
{
    memset(b, 0, sizeof *b);
    b->rom = rom;
    if (cmos) memcpy(b->cmos, cmos, sizeof b->cmos);
    for (int i = 0; i < 2; i++) {
        pia_reset(&b->pia[i]);
        b->pia[i].ctx = b;
    }
    b->pia[0].read_a = pia0_read_a;
    b->pia[0].read_b = pia0_read_b;
    b->pia[1].read_a = pia1_read_a;
    b->pia[1].write_b = pia1_write_b;
    /* one frame of emulated sound fills one tick of the port's audio */
    sound_init(&b->sound, rom, SOUND_CPU_HZ / BOARD_FRAME_HZ / (BOARD_AUDIO_RATE / BOARD_AUDIO_FPS));
    b->cpu.ctx = b;
    b->cpu.read = bus_read;
    b->cpu.write = bus_write;
    m6809_reset(&b->cpu);
    b->cpu.cycles = 4; /* the reset sequence: MAME's first instruction starts at cycle 4 */
}

static void draw_line(const Board *b, Framebuffer *fb, int line)
{
    int y = line - WILLIAMS_VISIBLE_Y;
    if (y < 0 || y >= FB_HEIGHT) return;
    uint32_t rgb[16];
    for (int i = 0; i < 16; i++) rgb[i] = 0xFF000000u | williams_rgb(b->palette[i]);
    uint32_t *row = fb_row(fb, y);
    for (int x = 0; x < FB_WIDTH; x++) {
        int vx = x + WILLIAMS_VISIBLE_X;
        uint8_t px = b->ram[(vx >> 1) * 256 + line];
        row[x] = rgb[vx & 1 ? px & 0x0F : px >> 4];
    }
}

/* Each line is drawn whole, from RAM and the palette as they are BEAM_LEAD
   cycles before the line starts (measured against MAME's frames: 2 or 3
   match every captured frame, 0-1 and 4 or more do not). A store lands on
   its instruction's last cycle: when that is too late for the next line,
   the line is drawn first, from what RAM held before the store. */
#define BEAM_LEAD 2

static void beam_catch_up(Board *b)
{
    if (b->fb && !b->next_drawn && b->cpu.op_end >= b->next_line_start - BEAM_LEAD) {
        draw_line(b, b->fb, b->next_line);
        b->next_drawn = 1;
    }
}

/* A frame runs from the start of vertical blank (line 247, where MAME's
   screen starts at power-on and where its frames end) to the last visible
   line, 246: the frame's picture is complete when it returns. */
void board_run_frame(Board *b, Framebuffer *fb)
{
    b->sound.out_len = 0;
    b->fb = fb && fb->scale == 1 ? fb : NULL;
    b->next_drawn = 1;
    for (int i = 0; i < BOARD_LINES; i++) {
        int line = (BOARD_FIRST_LINE + i) % BOARD_LINES;
        b->scanline = line;
        if (b->line_hook) b->line_hook(b, line);
        if (line < 256 && !(line & 0x1F)) pia_cb1(&b->pia[1], (line & 0x20) != 0); /* VA11 */
        if (line == 240) pia_ca1(&b->pia[1], 1);                                    /* COUNT240 */
        if (line == 0) pia_ca1(&b->pia[1], 0);
        update_irq(b);
        if (b->fb && !b->next_drawn) draw_line(b, b->fb, line);
        int64_t end = (b->frame * BOARD_LINES + i + 1) * (int64_t)BOARD_CYCLES_PER_LINE;
        /* the next line, which a store late in this one may have to draw early */
        b->next_line = (line + 1) % BOARD_LINES;
        b->next_line_start = end;
        b->next_drawn = i + 1 == BOARD_LINES; /* the next frame's first line belongs to it */
        while (b->cpu.cycles < end) {
            if (b->step_hook) b->step_hook(b);
            m6809_step(&b->cpu);
            if (b->sound_pending) send_sound_command(b);
        }
        sound_run(&b->sound, sound_cycles(end));
    }
    b->frame++;
    b->fb = NULL;
}
