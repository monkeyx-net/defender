#include "emu/sound.h"

#include <string.h>

#define VOLUME 0.28
#define DC_POLE 0.997

static void update_irq(Sound *s)
{
    s->cpu.irq = pia_irq_a(&s->pia) || pia_irq_b(&s->pia);
}

static uint8_t pia_cmd(void *ctx) { return ((Sound *)ctx)->command; }
static void pia_dac(void *ctx, uint8_t v) { ((Sound *)ctx)->dac = v; }

static uint8_t bus_read(void *ctx, uint16_t a)
{
    Sound *s = ctx;
    if (a < 0x0080) return s->ram[a];
    if ((a & 0x7FFC) == 0x0400) {
        uint8_t v = pia_read(&s->pia, a & 3);
        update_irq(s);
        return v;
    }
    if (a >= 0xF800) return s->rom->sound[a & 0x07FF];
    return 0; /* unmapped, and $B000-$F7FF (empty ROM sockets) */
}

static void bus_write(void *ctx, uint16_t a, uint8_t v)
{
    Sound *s = ctx;
    if (a < 0x0080) {
        s->ram[a] = v;
        return;
    }
    if ((a & 0x7FFC) == 0x0400) {
        pia_write(&s->pia, a & 3, v);
        update_irq(s);
    }
}

void sound_init(Sound *s, const RomSet *rom, double cycles_per_sample)
{
    memset(s, 0, sizeof *s);
    s->rom = rom;
    s->cycles_per_sample = cycles_per_sample;
    s->sample_left = cycles_per_sample;
    s->command = 0xFF;
    pia_reset(&s->pia);
    s->pia.ctx = s;
    s->pia.read_b = pia_cmd;
    s->pia.write_a = pia_dac;
    s->cpu.ctx = s;
    s->cpu.read = bus_read;
    s->cpu.write = bus_write;
    m6800_reset(&s->cpu);
}

void sound_command(Sound *s, uint8_t cmd)
{
    s->command = cmd;
    pia_cb1(&s->pia, cmd != 0xFF);
    update_irq(s);
}

static void emit(Sound *s)
{
    double level = s->acc / s->cycles_per_sample - 128.0;
    double out = level - s->dc_in + DC_POLE * s->dc_out; /* DC blocker */
    s->dc_in = level;
    s->dc_out = out;
    double v = out * 256.0 * VOLUME;
    if (v > 32767) v = 32767;
    if (v < -32768) v = -32768;
    if (s->out_len < SOUND_MAX_SAMPLES) s->out[s->out_len++] = (int16_t)v;
    s->acc = 0;
}

void sound_run(Sound *s, int64_t until)
{
    while (s->cpu.cycles < until) {
        uint8_t level = s->dac; /* the level before this instruction's writes */
        if (s->step_hook) s->step_hook(s->user, &s->cpu);
        double cyc = m6800_step(&s->cpu);
        while (cyc >= s->sample_left) {
            s->acc += level * s->sample_left;
            cyc -= s->sample_left;
            s->sample_left = s->cycles_per_sample;
            emit(s);
        }
        s->acc += level * cyc;
        s->sample_left -= cyc;
    }
}
