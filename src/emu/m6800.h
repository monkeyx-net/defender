/*
 * Motorola 6800 CPU core (the 6808 on the Williams sound board runs the same
 * instruction set): documented opcodes with the datasheet's cycle counts,
 * IRQ, NMI, SWI and WAI. Memory goes through the board's callbacks.
 */
#ifndef DEFENDER_M6800_H
#define DEFENDER_M6800_H

#include <stdint.h>

typedef struct M6800 {
    uint8_t a, b, cc; /* cc: 11HINZVC */
    uint16_t x, sp, pc;
    int irq, nmi, nmi_prev;
    int waiting;      /* after WAI: state pushed, idle until an interrupt */
    int64_t cycles;
    void *ctx;
    uint8_t (*read)(void *ctx, uint16_t addr);
    void (*write)(void *ctx, uint16_t addr, uint8_t value);
} M6800;

void m6800_reset(M6800 *c);
/* Execute one instruction (or take an interrupt); returns cycles used. */
int m6800_step(M6800 *c);

#endif
