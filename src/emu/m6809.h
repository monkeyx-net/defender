/*
 * Motorola 6809 CPU core: the full documented instruction set with the
 * datasheet's cycle counts, IRQ/FIRQ/NMI, CWAI and SYNC, and the undefined
 * opcodes the way MAME's m6809 runs them. Memory goes through the board's
 * callbacks. Checked against MAME by the mame_emu test (the random number
 * trace and screenshots from power-on).
 */
#ifndef DEFENDER_M6809_H
#define DEFENDER_M6809_H

#include <stdint.h>

#define CC_E 0x80
#define CC_F 0x40
#define CC_H 0x20
#define CC_I 0x10
#define CC_N 0x08
#define CC_Z 0x04
#define CC_V 0x02
#define CC_C 0x01

typedef struct M6809 {
    uint8_t a, b, dp, cc;
    uint16_t x, y, u, s, pc;
    int irq, firq, nmi;      /* input lines, 1 = asserted */
    int nmi_armed;           /* NMI is ignored until S is first loaded */
    int nmi_prev;
    int waiting;             /* 1 after CWAI (state pushed), 2 after SYNC */
    int last_interrupt;      /* the last step took an interrupt (traces) */
    int64_t cycles;          /* total executed (64-bit: hours of play on any platform) */
    int64_t op_end;          /* during an instruction: the cycle it ends at (its base cost;
                                indexed modes may add more), when a load's last-cycle read lands */
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
} M6809;

void m6809_reset(M6809 *c);
/* Execute one instruction (or take an interrupt); returns cycles used. */
int m6809_step(M6809 *c);

#endif
