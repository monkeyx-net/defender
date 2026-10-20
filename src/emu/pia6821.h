/*
 * Motorola MC6821 PIA, as the Williams boards use it: two 8-bit ports with
 * data direction registers, C1 edge inputs that raise IRQ flags, C2 as an
 * edge input or a manually set output. (The handshake/strobe C2 modes are
 * not emulated.) Register order as in MAME's
 * pia6821_device::read/write: 0 port A or DDRA, 1 CRA, 2 port B or DDRB, 3 CRB.
 */
#ifndef DEFENDER_PIA6821_H
#define DEFENDER_PIA6821_H

#include <stdint.h>

typedef struct {
    uint8_t ddr, out, cr; /* cr bits 6-7: the IRQ2/IRQ1 flags */
    int c1, c2_in;        /* input line levels */
    int c2_out;           /* C2 when it is an output */
} PiaSide;

typedef struct Pia {
    PiaSide a, b;
    void *ctx;
    uint8_t (*read_a)(void *ctx);
    uint8_t (*read_b)(void *ctx);
    void (*write_a)(void *ctx, uint8_t pins); /* undriven (input) pins read as 1 */
    void (*write_b)(void *ctx, uint8_t pins);
    void (*c2b_changed)(void *ctx, int level);
} Pia;

void pia_reset(Pia *p);
uint8_t pia_read(Pia *p, int reg);
void pia_write(Pia *p, int reg, uint8_t v);
void pia_ca1(Pia *p, int level);
void pia_cb1(Pia *p, int level);
int pia_irq_a(const Pia *p);
int pia_irq_b(const Pia *p);

#endif
