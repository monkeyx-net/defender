#include "emu/m6809.h"

#include <stddef.h>

#define RD(a) c->read(c->ctx, (uint16_t)(a))
#define WR(a, v) c->write(c->ctx, (uint16_t)(a), (uint8_t)(v))

/* Base cycles of page 0 (no prefix); indexed modes add their postbyte's cost,
   PSH/PUL one per byte, RTI and long branches are adjusted where they run. */
static const uint8_t CYCLES[256] = {
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6, /* 00 */
    0, 0, 2, 4, 2, 2, 5, 9, 2, 2, 3, 2, 3, 2, 8, 6, /* 10 */
    3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, /* 20 */
    4, 4, 4, 4, 5, 5, 5, 5, 3, 5, 3, 6, 20, 11, 2, 19, /* 30 */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 40 */
    2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, 2, /* 50 */
    6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 6, 3, 6, /* 60 */
    7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 7, 4, 7, /* 70 */
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 4, 7, 3, 3, /* 80 */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5, /* 90 */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 6, 7, 5, 5, /* A0 */
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 7, 8, 6, 6, /* B0 */
    2, 2, 2, 4, 2, 2, 2, 2, 2, 2, 2, 2, 3, 3, 3, 3, /* C0 */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, /* D0 */
    4, 4, 4, 6, 4, 4, 4, 4, 4, 4, 4, 4, 5, 5, 5, 5, /* E0 */
    5, 5, 5, 7, 5, 5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, /* F0 */
};

static uint8_t fetch8(M6809 *c) { return RD(c->pc++); }
static uint16_t fetch16(M6809 *c)
{
    uint16_t v = (uint16_t)(RD(c->pc) << 8 | RD((uint16_t)(c->pc + 1)));
    c->pc = (uint16_t)(c->pc + 2);
    return v;
}
static uint16_t rd16(M6809 *c, uint16_t a) { return (uint16_t)(RD(a) << 8 | RD((uint16_t)(a + 1))); }
static void wr16(M6809 *c, uint16_t a, uint16_t v)
{
    WR(a, v >> 8);
    WR((uint16_t)(a + 1), v & 0xFF);
}

static uint16_t getd(const M6809 *c) { return (uint16_t)(c->a << 8 | c->b); }
static void setd(M6809 *c, uint16_t v)
{
    c->a = (uint8_t)(v >> 8);
    c->b = (uint8_t)v;
}

/* stack helpers: `sp` is S or U */
static void push8(M6809 *c, uint16_t *sp, uint8_t v)
{
    *sp = (uint16_t)(*sp - 1);
    WR(*sp, v);
}
static void push16(M6809 *c, uint16_t *sp, uint16_t v)
{
    push8(c, sp, (uint8_t)v);
    push8(c, sp, (uint8_t)(v >> 8));
}
static uint8_t pull8(M6809 *c, uint16_t *sp)
{
    uint8_t v = RD(*sp);
    *sp = (uint16_t)(*sp + 1);
    return v;
}
static uint16_t pull16(M6809 *c, uint16_t *sp)
{
    uint16_t hi = pull8(c, sp);
    return (uint16_t)(hi << 8 | pull8(c, sp));
}

/* ---- flags ------------------------------------------------------------- */

static void nz8(M6809 *c, uint8_t r)
{
    c->cc &= (uint8_t)~(CC_N | CC_Z);
    if (r & 0x80) c->cc |= CC_N;
    if (!r) c->cc |= CC_Z;
}
static void nz16(M6809 *c, uint16_t r)
{
    c->cc &= (uint8_t)~(CC_N | CC_Z);
    if (r & 0x8000) c->cc |= CC_N;
    if (!r) c->cc |= CC_Z;
}
static void setf(M6809 *c, uint8_t flag, int on)
{
    if (on) c->cc |= flag;
    else c->cc &= (uint8_t)~flag;
}

static uint8_t op_neg(M6809 *c, uint8_t v)
{
    uint8_t r = (uint8_t)-v;
    nz8(c, r);
    setf(c, CC_V, v == 0x80);
    setf(c, CC_C, r != 0);
    return r;
}
static uint8_t op_com(M6809 *c, uint8_t v)
{
    uint8_t r = (uint8_t)~v;
    nz8(c, r);
    setf(c, CC_V, 0);
    setf(c, CC_C, 1);
    return r;
}
static uint8_t op_lsr(M6809 *c, uint8_t v)
{
    setf(c, CC_C, v & 1);
    uint8_t r = v >> 1;
    nz8(c, r);
    return r;
}
static uint8_t op_ror(M6809 *c, uint8_t v)
{
    uint8_t r = (uint8_t)((c->cc & CC_C) << 7 | v >> 1);
    setf(c, CC_C, v & 1);
    nz8(c, r);
    return r;
}
static uint8_t op_asr(M6809 *c, uint8_t v)
{
    setf(c, CC_C, v & 1);
    uint8_t r = (uint8_t)((v & 0x80) | v >> 1);
    nz8(c, r);
    return r;
}
static uint8_t op_asl(M6809 *c, uint8_t v)
{
    uint8_t r = (uint8_t)(v << 1);
    setf(c, CC_C, v & 0x80);
    setf(c, CC_V, (v ^ r) & 0x80);
    nz8(c, r);
    return r;
}
static uint8_t op_rol(M6809 *c, uint8_t v)
{
    uint8_t r = (uint8_t)(v << 1 | (c->cc & CC_C));
    setf(c, CC_C, v & 0x80);
    setf(c, CC_V, (v ^ r) & 0x80);
    nz8(c, r);
    return r;
}
static uint8_t op_dec(M6809 *c, uint8_t v)
{
    uint8_t r = (uint8_t)(v - 1);
    nz8(c, r);
    setf(c, CC_V, v == 0x80);
    return r;
}
static uint8_t op_inc(M6809 *c, uint8_t v)
{
    uint8_t r = (uint8_t)(v + 1);
    nz8(c, r);
    setf(c, CC_V, v == 0x7F);
    return r;
}
static void op_tst(M6809 *c, uint8_t v)
{
    nz8(c, v);
    setf(c, CC_V, 0);
}
static uint8_t op_clr(M6809 *c)
{
    c->cc = (uint8_t)((c->cc & ~(CC_N | CC_V | CC_C)) | CC_Z);
    return 0;
}

static uint8_t op_sub8(M6809 *c, uint8_t a, uint8_t b, int carry)
{
    unsigned r = (unsigned)a - b - (unsigned)carry;
    nz8(c, (uint8_t)r);
    setf(c, CC_V, (a ^ b) & (a ^ r) & 0x80);
    setf(c, CC_C, r & 0x100);
    return (uint8_t)r;
}
static uint8_t op_add8(M6809 *c, uint8_t a, uint8_t b, int carry)
{
    unsigned r = (unsigned)a + b + (unsigned)carry;
    setf(c, CC_H, (a ^ b ^ r) & 0x10);
    nz8(c, (uint8_t)r);
    setf(c, CC_V, (a ^ r) & (b ^ r) & 0x80);
    setf(c, CC_C, r & 0x100);
    return (uint8_t)r;
}
static uint16_t op_sub16(M6809 *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a - b;
    nz16(c, (uint16_t)r);
    setf(c, CC_V, (a ^ b) & (a ^ r) & 0x8000);
    setf(c, CC_C, r & 0x10000);
    return (uint16_t)r;
}
static uint16_t op_add16(M6809 *c, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    nz16(c, (uint16_t)r);
    setf(c, CC_V, (a ^ r) & (b ^ r) & 0x8000);
    setf(c, CC_C, r & 0x10000);
    return (uint16_t)r;
}
static uint8_t op_logic(M6809 *c, uint8_t r)
{
    nz8(c, r);
    setf(c, CC_V, 0);
    return r;
}

/* ---- addressing ------------------------------------------------------------ */

static uint16_t *index_reg(M6809 *c, uint8_t pb)
{
    switch (pb >> 5 & 3) {
    case 0: return &c->x;
    case 1: return &c->y;
    case 2: return &c->u;
    default: return &c->s;
    }
}

static uint16_t ea_indexed(M6809 *c, int *cyc)
{
    uint8_t pb = fetch8(c);
    uint16_t *r = index_reg(c, pb);
    uint16_t ea;
    if (!(pb & 0x80)) { /* 5-bit offset */
        int off = pb & 0x1F;
        if (off & 0x10) off -= 0x20;
        *cyc += 1;
        return (uint16_t)(*r + off);
    }
    switch (pb & 0x0F) {
    case 0x0: ea = *r; *r = (uint16_t)(*r + 1); *cyc += 2; break;
    case 0x1: ea = *r; *r = (uint16_t)(*r + 2); *cyc += 3; break;
    case 0x2: *r = (uint16_t)(*r - 1); ea = *r; *cyc += 2; break;
    case 0x3: *r = (uint16_t)(*r - 2); ea = *r; *cyc += 3; break;
    case 0x4: ea = *r; break;
    case 0x5: ea = (uint16_t)(*r + (int8_t)c->b); *cyc += 1; break;
    case 0x6: ea = (uint16_t)(*r + (int8_t)c->a); *cyc += 1; break;
    case 0x8: ea = (uint16_t)(*r + (int8_t)fetch8(c)); *cyc += 1; break;
    case 0x9: ea = (uint16_t)(*r + fetch16(c)); *cyc += 4; break;
    case 0xB: ea = (uint16_t)(*r + getd(c)); *cyc += 4; break;
    case 0xC: { int8_t off = (int8_t)fetch8(c); ea = (uint16_t)(c->pc + off); *cyc += 1; break; }
    case 0xD: { uint16_t off = fetch16(c); ea = (uint16_t)(c->pc + off); *cyc += 5; break; }
    case 0xF: ea = fetch16(c); *cyc += 2; break; /* [n16]: 5 with the indirection below */
    default: ea = *r; *cyc += 1; break;         /* 7, A, E: undefined */
    }
    if (pb & 0x10) {
        ea = rd16(c, ea);
        *cyc += 3;
    }
    return ea;
}

/* Effective address for the mode in bits 4-5 of opcodes $60-$FF (and $00-$0F: direct). */
static uint16_t ea_mode(M6809 *c, int mode, int *cyc)
{
    switch (mode) {
    case 1: return (uint16_t)(c->dp << 8 | fetch8(c));
    case 2: return ea_indexed(c, cyc);
    default: return fetch16(c);
    }
}

static int branch_taken(const M6809 *c, uint8_t op)
{
    int n = !!(c->cc & CC_N), z = !!(c->cc & CC_Z), v = !!(c->cc & CC_V), cy = !!(c->cc & CC_C);
    switch (op & 0x0F) {
    case 0x0: return 1;
    case 0x1: return 0;
    case 0x2: return !(cy | z);
    case 0x3: return cy | z;
    case 0x4: return !cy;
    case 0x5: return cy;
    case 0x6: return !z;
    case 0x7: return z;
    case 0x8: return !v;
    case 0x9: return v;
    case 0xA: return !n;
    case 0xB: return n;
    case 0xC: return !(n ^ v);
    case 0xD: return n ^ v;
    case 0xE: return !(z | (n ^ v));
    default: return z | (n ^ v);
    }
}

/* ---- TFR / EXG (MAME's rules for mixed sizes) ------------------------------ */

static uint16_t exg_read(const M6809 *c, int reg)
{
    switch (reg & 0x0F) {
    case 0: return getd(c);
    case 1: return c->x;
    case 2: return c->y;
    case 3: return c->u;
    case 4: return c->s;
    case 5: return c->pc;
    case 8: return (uint16_t)(0xFF00 | c->a);
    case 9: return (uint16_t)(0xFF00 | c->b);
    case 10: return (uint16_t)(c->cc << 8 | c->cc);
    case 11: return (uint16_t)(c->dp << 8 | c->dp);
    default: return 0xFFFF;
    }
}

static void exg_write(M6809 *c, int reg, uint16_t v)
{
    switch (reg & 0x0F) {
    case 0: setd(c, v); break;
    case 1: c->x = v; break;
    case 2: c->y = v; break;
    case 3: c->u = v; break;
    case 4: c->s = v; c->nmi_armed = 1; break;
    case 5: c->pc = v; break;
    case 8: c->a = (uint8_t)v; break;
    case 9: c->b = (uint8_t)v; break;
    case 10: c->cc = (uint8_t)v; break;
    case 11: c->dp = (uint8_t)v; break;
    default: break;
    }
}

/* ---- push / pull lists ----------------------------------------------------- */

static int push_list(M6809 *c, uint16_t *sp, uint16_t *other, uint8_t mask)
{
    int n = 0;
    if (mask & 0x80) { push16(c, sp, c->pc); n += 2; }
    if (mask & 0x40) { push16(c, sp, *other); n += 2; }
    if (mask & 0x20) { push16(c, sp, c->y); n += 2; }
    if (mask & 0x10) { push16(c, sp, c->x); n += 2; }
    if (mask & 0x08) { push8(c, sp, c->dp); n++; }
    if (mask & 0x04) { push8(c, sp, c->b); n++; }
    if (mask & 0x02) { push8(c, sp, c->a); n++; }
    if (mask & 0x01) { push8(c, sp, c->cc); n++; }
    return n;
}

static int pull_list(M6809 *c, uint16_t *sp, uint16_t *other, uint8_t mask)
{
    int n = 0;
    if (mask & 0x01) { c->cc = pull8(c, sp); n++; }
    if (mask & 0x02) { c->a = pull8(c, sp); n++; }
    if (mask & 0x04) { c->b = pull8(c, sp); n++; }
    if (mask & 0x08) { c->dp = pull8(c, sp); n++; }
    if (mask & 0x10) { c->x = pull16(c, sp); n += 2; }
    if (mask & 0x20) { c->y = pull16(c, sp); n += 2; }
    if (mask & 0x40) { *other = pull16(c, sp); n += 2; }
    if (mask & 0x80) { c->pc = pull16(c, sp); n += 2; }
    return n;
}

static void push_all(M6809 *c)
{
    push16(c, &c->s, c->pc);
    push16(c, &c->s, c->u);
    push16(c, &c->s, c->y);
    push16(c, &c->s, c->x);
    push8(c, &c->s, c->dp);
    push8(c, &c->s, c->b);
    push8(c, &c->s, c->a);
    push8(c, &c->s, c->cc);
}

/* ---- reset and interrupts ---------------------------------------------- */

void m6809_reset(M6809 *c)
{
    c->dp = 0;
    c->cc = CC_I | CC_F;
    c->waiting = 0;
    c->nmi_armed = 0;
    c->nmi_prev = 0;
    c->pc = rd16(c, 0xFFFE);
}

static int take_interrupts(M6809 *c)
{
    int nmi_edge = c->nmi && !c->nmi_prev;
    c->nmi_prev = c->nmi;
    if (nmi_edge && c->nmi_armed) {
        int cyc = c->waiting == 1 ? 7 : 19;
        if (c->waiting != 1) {
            c->cc |= CC_E;
            push_all(c);
        }
        c->cc |= CC_I | CC_F;
        c->waiting = 0;
        c->pc = rd16(c, 0xFFFC);
        return cyc;
    }
    if (c->firq && !(c->cc & CC_F)) {
        int cyc = c->waiting == 1 ? 7 : 10;
        if (c->waiting != 1) {
            c->cc &= (uint8_t)~CC_E;
            push16(c, &c->s, c->pc);
            push8(c, &c->s, c->cc);
        }
        c->cc |= CC_I | CC_F;
        c->waiting = 0;
        c->pc = rd16(c, 0xFFF6);
        return cyc;
    }
    if (c->irq && !(c->cc & CC_I)) {
        int cyc = c->waiting == 1 ? 7 : 19;
        if (c->waiting != 1) {
            c->cc |= CC_E;
            push_all(c);
        }
        c->cc |= CC_I;
        c->waiting = 0;
        c->pc = rd16(c, 0xFFF8);
        return cyc;
    }
    if (c->waiting == 2 && (c->irq || c->firq || c->nmi)) c->waiting = 0; /* SYNC ends on a masked line too */
    return 0;
}

/* ---- the opcodes ----------------------------------------------------------- */

/* $00-$0F, $40-$7F: the unary group, on a value; returns the result (or -1 for TST/JMP). */
static int unary(M6809 *c, uint8_t op, uint8_t v)
{
    switch (op & 0x0F) {
    case 0x0: case 0x1: return op_neg(c, v);
    case 0x2: return c->cc & CC_C ? op_com(c, v) : op_neg(c, v); /* undefined: MAME's XNC */
    case 0x3: return op_com(c, v);
    case 0x4: case 0x5: return op_lsr(c, v);
    case 0x6: return op_ror(c, v);
    case 0x7: return op_asr(c, v);
    case 0x8: return op_asl(c, v);
    case 0x9: return op_rol(c, v);
    case 0xA: case 0xB: return op_dec(c, v);
    case 0xC: return op_inc(c, v);
    case 0xD: op_tst(c, v); return -1;
    default: return op_clr(c); /* $xF CLR (and $4E/$5E, undefined: XCLR) */
    }
}

/* $80-$FF: the A/B/D/X/U group on one operand. */
static void accum_op(M6809 *c, uint8_t op, int *cyc)
{
    int mode = op >> 4 & 3, side_b = op >= 0xC0;
    int lo = op & 0x0F;
    uint8_t *acc = side_b ? &c->b : &c->a;
    int wide = lo == 0x3 || lo >= 0xC; /* SUBD/ADDD, CMPX/LDD, STD, LDX/LDU, STX/STU */
    int store = lo == 0x7 || (lo == 0xF) || (side_b && (lo == 0xD || lo == 0xF));

    if (!side_b && lo == 0xD) { /* BSR (immediate column) / JSR */
        if (mode == 0) {
            int8_t off = (int8_t)fetch8(c);
            push16(c, &c->s, c->pc);
            c->pc = (uint16_t)(c->pc + off);
        } else {
            uint16_t ea = ea_mode(c, mode, cyc);
            push16(c, &c->s, c->pc);
            c->pc = ea;
        }
        return;
    }

    uint16_t ea = 0;
    uint16_t v16 = 0;
    uint8_t v8 = 0;
    if (mode == 0) {
        if (store) { /* undefined immediate stores: write to the operand bytes (MAME's XST) */
            ea = c->pc;
            c->pc = (uint16_t)(c->pc + (wide ? 2 : 1));
        } else if (wide) {
            v16 = fetch16(c);
        } else {
            v8 = fetch8(c);
        }
    } else {
        ea = ea_mode(c, mode, cyc);
        if (!store) {
            if (wide) v16 = rd16(c, ea);
            else v8 = RD(ea);
        }
    }

    if (wide) {
        switch (lo) {
        case 0x3: /* SUBD / ADDD */
            setd(c, side_b ? op_add16(c, getd(c), v16) : op_sub16(c, getd(c), v16));
            return;
        case 0xC:
            if (side_b) { setd(c, v16); nz16(c, v16); setf(c, CC_V, 0); } /* LDD */
            else op_sub16(c, c->x, v16);                                   /* CMPX */
            return;
        case 0xD: wr16(c, ea, getd(c)); nz16(c, getd(c)); setf(c, CC_V, 0); return; /* STD */
        case 0xE:
            if (side_b) c->u = v16; else c->x = v16;                       /* LDU / LDX */
            nz16(c, v16);
            setf(c, CC_V, 0);
            return;
        default: { /* $xF STX / STU */
            uint16_t r = side_b ? c->u : c->x;
            wr16(c, ea, r);
            nz16(c, r);
            setf(c, CC_V, 0);
            return;
        }
        }
    }
    switch (lo) {
    case 0x0: *acc = op_sub8(c, *acc, v8, 0); break;
    case 0x1: op_sub8(c, *acc, v8, 0); break;
    case 0x2: *acc = op_sub8(c, *acc, v8, c->cc & CC_C); break;
    case 0x4: *acc = op_logic(c, *acc & v8); break;
    case 0x5: op_logic(c, *acc & v8); break;
    case 0x6: *acc = op_logic(c, v8); break;
    case 0x7: WR(ea, *acc); op_logic(c, *acc); break;
    case 0x8: *acc = op_logic(c, *acc ^ v8); break;
    case 0x9: *acc = op_add8(c, *acc, v8, c->cc & CC_C); break;
    case 0xA: *acc = op_logic(c, *acc | v8); break;
    case 0xB: *acc = op_add8(c, *acc, v8, 0); break;
    default: break;
    }
}

/* Page 2 ($10 xx) and 3 ($11 xx). Returns 0 if the opcode is not defined there. */
static int prefixed(M6809 *c, int page, uint8_t op, int *cyc)
{
    if (page == 2 && op >= 0x21 && op <= 0x2F) { /* long branches */
        uint16_t off = fetch16(c);
        *cyc = 5;
        if (branch_taken(c, op)) {
            c->pc = (uint16_t)(c->pc + off);
            *cyc = 6;
        }
        return 1;
    }
    if (op == 0x3F) { /* SWI2 / SWI3 */
        c->cc |= CC_E;
        push_all(c);
        c->pc = rd16(c, page == 2 ? 0xFFF4 : 0xFFF2);
        *cyc = 20;
        return 1;
    }
    int mode = op >> 4 & 3, lo = op & 0x0F;
    static const uint8_t base[4] = { 5, 7, 7, 8 }; /* CMP: imm, dir, idx, ext */
    if (op < 0x80) return 0;
    uint16_t *reg = NULL;
    int kind = 0; /* 1 compare, 2 load, 3 store */
    if (page == 2) {
        int side_b = op >= 0xC0;
        if (!side_b && lo == 0x3) { reg = NULL; kind = 1; }         /* CMPD */
        else if (!side_b && lo == 0xC) { reg = &c->y; kind = 1; }   /* CMPY */
        else if (lo == 0xE) { reg = side_b ? &c->s : &c->y; kind = 2; } /* LDS / LDY */
        else if (lo == 0xF && mode) { reg = side_b ? &c->s : &c->y; kind = 3; } /* STS / STY */
        else return 0;
    } else {
        if (op >= 0xC0) return 0;
        if (lo == 0x3) { reg = &c->u; kind = 1; }      /* CMPU */
        else if (lo == 0xC) { reg = &c->s; kind = 1; } /* CMPS */
        else return 0;
    }
    if (kind == 1) *cyc = base[mode];
    else *cyc = mode == 0 ? 4 : mode == 3 ? 7 : 6;
    uint16_t ea = 0, v = 0;
    if (mode == 0) v = fetch16(c);
    else {
        ea = ea_mode(c, mode, cyc);
        if (kind != 3) v = rd16(c, ea);
    }
    if (kind == 1) op_sub16(c, reg ? *reg : getd(c), v);
    else if (kind == 2) {
        *reg = v;
        if (reg == &c->s) c->nmi_armed = 1;
        nz16(c, v);
        setf(c, CC_V, 0);
    } else {
        wr16(c, ea, *reg);
        nz16(c, *reg);
        setf(c, CC_V, 0);
    }
    return 1;
}

static void page0(M6809 *c, uint8_t op, int *cyc)
{
    if (op < 0x10 || (op >= 0x40 && op < 0x80)) { /* unary: direct, A, B, indexed, extended */
        int lo = op & 0x0F;
        if (op >= 0x40 && op < 0x60) {
            uint8_t *acc = op < 0x50 ? &c->a : &c->b;
            int r = unary(c, op, *acc);
            if (r >= 0) *acc = (uint8_t)r;
            return;
        }
        int mode = op < 0x10 ? 1 : op < 0x70 ? 2 : 3;
        uint16_t ea = ea_mode(c, mode, cyc);
        if (lo == 0xE) { c->pc = ea; return; } /* JMP */
        if (lo == 0xF) { (void)RD(ea); WR(ea, op_clr(c)); return; } /* CLR reads first, as on the 6809 */
        int r = unary(c, op, RD(ea));
        if (r >= 0) WR(ea, r);
        return;
    }
    if (op >= 0x20 && op < 0x30) {
        int8_t off = (int8_t)fetch8(c);
        if (branch_taken(c, op)) c->pc = (uint16_t)(c->pc + off);
        return;
    }
    if (op >= 0x80) {
        accum_op(c, op, cyc);
        return;
    }
    switch (op) {
    case 0x12: break;                         /* NOP */
    case 0x13: c->waiting = 2; break;         /* SYNC */
    case 0x16: { uint16_t off = fetch16(c); c->pc = (uint16_t)(c->pc + off); break; } /* LBRA */
    case 0x17: { uint16_t off = fetch16(c); push16(c, &c->s, c->pc); c->pc = (uint16_t)(c->pc + off); break; } /* LBSR */
    case 0x19: {                              /* DAA */
        uint8_t msn = c->a & 0xF0, lsn = c->a & 0x0F;
        unsigned cf = 0;
        if (lsn > 9 || (c->cc & CC_H)) cf |= 0x06;
        if (msn > 0x80 && lsn > 9) cf |= 0x60;
        if (msn > 0x90 || (c->cc & CC_C)) cf |= 0x60;
        unsigned t = cf + c->a;
        c->cc &= (uint8_t)~CC_V;
        nz8(c, (uint8_t)t);
        if (t & 0x100) c->cc |= CC_C;
        c->a = (uint8_t)t;
        break;
    }
    case 0x1A: c->cc |= fetch8(c); break;     /* ORCC */
    case 0x1C: case 0x38: c->cc &= fetch8(c); break; /* ANDCC ($38 undefined: XANDCC) */
    case 0x1D: c->a = c->b & 0x80 ? 0xFF : 0; nz16(c, getd(c)); break; /* SEX */
    case 0x1E: {                              /* EXG */
        uint8_t pb = fetch8(c);
        uint16_t r1 = exg_read(c, pb >> 4), r2 = exg_read(c, pb & 0x0F);
        exg_write(c, pb >> 4, r2);
        exg_write(c, pb & 0x0F, r1);
        break;
    }
    case 0x1F: { uint8_t pb = fetch8(c); exg_write(c, pb & 0x0F, exg_read(c, pb >> 4)); break; } /* TFR */
    case 0x30: c->x = ea_indexed(c, cyc); setf(c, CC_Z, !c->x); break; /* LEAX */
    case 0x31: c->y = ea_indexed(c, cyc); setf(c, CC_Z, !c->y); break; /* LEAY */
    case 0x32: c->s = ea_indexed(c, cyc); c->nmi_armed = 1; break;     /* LEAS */
    case 0x33: c->u = ea_indexed(c, cyc); break;                       /* LEAU */
    case 0x34: *cyc += push_list(c, &c->s, &c->u, fetch8(c)); break;   /* PSHS */
    case 0x35: *cyc += pull_list(c, &c->s, &c->u, fetch8(c)); break;   /* PULS */
    case 0x36: *cyc += push_list(c, &c->u, &c->s, fetch8(c)); break;   /* PSHU */
    case 0x37: *cyc += pull_list(c, &c->u, &c->s, fetch8(c)); break;   /* PULU */
    case 0x39: c->pc = pull16(c, &c->s); break;                        /* RTS */
    case 0x3A: c->x = (uint16_t)(c->x + c->b); break;                  /* ABX */
    case 0x3B:                                                          /* RTI */
        c->cc = pull8(c, &c->s);
        if (c->cc & CC_E) {
            c->a = pull8(c, &c->s);
            c->b = pull8(c, &c->s);
            c->dp = pull8(c, &c->s);
            c->x = pull16(c, &c->s);
            c->y = pull16(c, &c->s);
            c->u = pull16(c, &c->s);
            *cyc = 15;
        }
        c->pc = pull16(c, &c->s);
        break;
    case 0x3C:                                                          /* CWAI */
        c->cc &= fetch8(c);
        c->cc |= CC_E;
        push_all(c);
        c->waiting = 1;
        break;
    case 0x3D: {                                                        /* MUL */
        uint16_t r = (uint16_t)(c->a * c->b);
        setd(c, r);
        setf(c, CC_Z, !r);
        setf(c, CC_C, r & 0x80);
        break;
    }
    case 0x3E: m6809_reset(c); break;                                   /* undefined: XRES */
    case 0x3F:                                                          /* SWI */
        c->cc |= CC_E;
        push_all(c);
        c->cc |= CC_I | CC_F;
        c->pc = rd16(c, 0xFFFA);
        break;
    default: break; /* $14, $15, $18, $1B: undefined, run as NOP */
    }
}

int m6809_step(M6809 *c)
{
    int taken = take_interrupts(c);
    c->last_interrupt = taken > 0;
    if (taken) {
        c->cycles += taken;
        return taken;
    }
    if (c->waiting) { /* CWAI or SYNC: idle until an interrupt */
        c->cycles += 1;
        return 1;
    }
    uint8_t op = fetch8(c);
    int cyc;
    if (op == 0x10 || op == 0x11) {
        int page = op == 0x10 ? 2 : 3;
        uint8_t op2 = fetch8(c);
        while (op2 == 0x10 || op2 == 0x11) op2 = fetch8(c); /* repeated prefixes */
        cyc = 0;
        c->op_end = c->cycles;
        if (!prefixed(c, page, op2, &cyc)) { /* undefined there: runs as page 0 */
            cyc = CYCLES[op2] + 1;
            page0(c, op2, &cyc);
        }
    } else {
        cyc = CYCLES[op];
        c->op_end = c->cycles + cyc;
        page0(c, op, &cyc);
    }
    c->cycles += cyc;
    return cyc;
}
