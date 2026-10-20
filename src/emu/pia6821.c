#include "emu/pia6821.h"

#include <string.h>

#define CR_C1_IRQ_ENABLE 0x01
#define CR_C1_RISING 0x02
#define CR_DDR_ACCESS 0x04 /* 1: port register, 0: DDR */
#define CR_C2_IRQ_ENABLE 0x08 /* C2 input mode */
#define CR_C2_RISING 0x10     /* C2 input mode */
#define CR_C2_OUTPUT 0x20
#define CR_IRQ2 0x40
#define CR_IRQ1 0x80

void pia_reset(Pia *p)
{
    memset(&p->a, 0, sizeof p->a);
    memset(&p->b, 0, sizeof p->b);
}

static int irq(const PiaSide *s)
{
    return ((s->cr & CR_IRQ1) && (s->cr & CR_C1_IRQ_ENABLE)) ||
           ((s->cr & CR_IRQ2) && !(s->cr & CR_C2_OUTPUT) && (s->cr & CR_C2_IRQ_ENABLE));
}

int pia_irq_a(const Pia *p) { return irq(&p->a); }
int pia_irq_b(const Pia *p) { return irq(&p->b); }

uint8_t pia_read(Pia *p, int reg)
{
    switch (reg & 3) {
    case 0:
        if (!(p->a.cr & CR_DDR_ACCESS)) return p->a.ddr;
        p->a.cr &= (uint8_t)~(CR_IRQ1 | CR_IRQ2);
        return (uint8_t)(((p->read_a ? p->read_a(p->ctx) : 0xFF) & ~p->a.ddr) | (p->a.out & p->a.ddr));
    case 1:
        return p->a.cr;
    case 2:
        if (!(p->b.cr & CR_DDR_ACCESS)) return p->b.ddr;
        p->b.cr &= (uint8_t)~(CR_IRQ1 | CR_IRQ2);
        return (uint8_t)(((p->read_b ? p->read_b(p->ctx) : 0xFF) & ~p->b.ddr) | (p->b.out & p->b.ddr));
    default:
        return p->b.cr;
    }
}

static void write_cr(Pia *p, PiaSide *s, int is_b, uint8_t v)
{
    s->cr = (uint8_t)((s->cr & (CR_IRQ1 | CR_IRQ2)) | (v & 0x3F));
    if ((v & CR_C2_OUTPUT) && (v & 0x10)) { /* manual C2 output: follows bit 3 */
        int level = !!(v & 0x08);
        if (level != s->c2_out) {
            s->c2_out = level;
            if (is_b && p->c2b_changed) p->c2b_changed(p->ctx, level);
        }
    }
}

void pia_write(Pia *p, int reg, uint8_t v)
{
    switch (reg & 3) {
    case 0:
        if (p->a.cr & CR_DDR_ACCESS) p->a.out = v;
        else p->a.ddr = v;
        if (p->write_a) p->write_a(p->ctx, (uint8_t)((p->a.out & p->a.ddr) | ~p->a.ddr));
        break;
    case 1:
        write_cr(p, &p->a, 0, v);
        break;
    case 2:
        if (p->b.cr & CR_DDR_ACCESS) p->b.out = v;
        else p->b.ddr = v;
        if (p->write_b) p->write_b(p->ctx, (uint8_t)((p->b.out & p->b.ddr) | ~p->b.ddr));
        break;
    default:
        write_cr(p, &p->b, 1, v);
        break;
    }
}

static void c1(PiaSide *s, int level)
{
    if (level == s->c1) return;
    int rising = level && !s->c1;
    s->c1 = level;
    if (rising == !!(s->cr & CR_C1_RISING)) s->cr |= CR_IRQ1;
}

void pia_ca1(Pia *p, int level) { c1(&p->a, level); }
void pia_cb1(Pia *p, int level) { c1(&p->b, level); }
