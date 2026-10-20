#include "emu/m6800.h"

#define RD(a) c->read(c->ctx, (uint16_t)(a))
#define WR(a, v) c->write(c->ctx, (uint16_t)(a), (uint8_t)(v))

#define F_C 0x01
#define F_V 0x02
#define F_Z 0x04
#define F_N 0x08
#define F_I 0x10
#define F_H 0x20

/* Cycles per opcode (0 = undefined: run as a 2-cycle no-op). */
static const uint8_t CYCLES[256] = {
    0, 2, 0, 0, 0, 0, 2, 2, 4, 4, 2, 2, 2, 2, 2, 2,  /* 00 */
    2, 2, 0, 0, 0, 0, 2, 2, 0, 2, 0, 2, 0, 0, 0, 0,  /* 10 */
    4, 0, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4,  /* 20 */
    4, 4, 4, 4, 4, 4, 4, 4, 0, 5, 0, 10, 0, 0, 9, 12, /* 30 */
    2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,  /* 40 */
    2, 0, 0, 2, 2, 0, 2, 2, 2, 2, 2, 0, 2, 2, 0, 2,  /* 50 */
    7, 0, 0, 7, 7, 0, 7, 7, 7, 7, 7, 0, 7, 7, 4, 7,  /* 60 */
    6, 0, 0, 6, 6, 0, 6, 6, 6, 6, 6, 0, 6, 6, 3, 6,  /* 70 */
    2, 2, 2, 0, 2, 2, 2, 0, 2, 2, 2, 2, 3, 8, 3, 0,  /* 80 */
    3, 3, 3, 0, 3, 3, 3, 4, 3, 3, 3, 3, 4, 0, 4, 5,  /* 90 */
    5, 5, 5, 0, 5, 5, 5, 6, 5, 5, 5, 5, 6, 8, 6, 7,  /* A0 */
    4, 4, 4, 0, 4, 4, 4, 5, 4, 4, 4, 4, 5, 9, 5, 6,  /* B0 */
    2, 2, 2, 0, 2, 2, 2, 0, 2, 2, 2, 2, 0, 0, 3, 0,  /* C0 */
    3, 3, 3, 0, 3, 3, 3, 4, 3, 3, 3, 3, 0, 0, 4, 5,  /* D0 */
    5, 5, 5, 0, 5, 5, 5, 6, 5, 5, 5, 5, 0, 0, 6, 7,  /* E0 */
    4, 4, 4, 0, 4, 4, 4, 5, 4, 4, 4, 4, 0, 0, 5, 6,  /* F0 */
};

static uint8_t fetch8(M6800 *c) { return RD(c->pc++); }
static uint16_t fetch16(M6800 *c)
{
    uint16_t v = (uint16_t)(RD(c->pc) << 8 | RD((uint16_t)(c->pc + 1)));
    c->pc = (uint16_t)(c->pc + 2);
    return v;
}
static uint16_t rd16(M6800 *c, uint16_t a) { return (uint16_t)(RD(a) << 8 | RD((uint16_t)(a + 1))); }
static void wr16(M6800 *c, uint16_t a, uint16_t v)
{
    WR(a, v >> 8);
    WR((uint16_t)(a + 1), v & 0xFF);
}
static void push8(M6800 *c, uint8_t v)
{
    WR(c->sp, v);
    c->sp--;
}
static uint8_t pull8(M6800 *c)
{
    c->sp++;
    return RD(c->sp);
}
static void push16(M6800 *c, uint16_t v)
{
    push8(c, (uint8_t)v);
    push8(c, (uint8_t)(v >> 8));
}
static uint16_t pull16(M6800 *c)
{
    uint16_t hi = pull8(c);
    return (uint16_t)(hi << 8 | pull8(c));
}

static void setf(M6800 *c, uint8_t f, int on)
{
    if (on) c->cc |= f;
    else c->cc &= (uint8_t)~f;
}
static void nz8(M6800 *c, uint8_t r)
{
    setf(c, F_N, r & 0x80);
    setf(c, F_Z, !r);
}
static void nz16(M6800 *c, uint16_t r)
{
    setf(c, F_N, r & 0x8000);
    setf(c, F_Z, !r);
}

static uint8_t sub8(M6800 *c, uint8_t a, uint8_t b, int carry)
{
    unsigned r = (unsigned)a - b - (unsigned)carry;
    nz8(c, (uint8_t)r);
    setf(c, F_V, (a ^ b) & (a ^ r) & 0x80);
    setf(c, F_C, r & 0x100);
    return (uint8_t)r;
}
static uint8_t add8(M6800 *c, uint8_t a, uint8_t b, int carry)
{
    unsigned r = (unsigned)a + b + (unsigned)carry;
    setf(c, F_H, (a ^ b ^ r) & 0x10);
    nz8(c, (uint8_t)r);
    setf(c, F_V, (a ^ r) & (b ^ r) & 0x80);
    setf(c, F_C, r & 0x100);
    return (uint8_t)r;
}
static uint8_t logic(M6800 *c, uint8_t r)
{
    nz8(c, r);
    setf(c, F_V, 0);
    return r;
}
/* shifts: V = N xor C after the shift */
static uint8_t shift_flags(M6800 *c, uint8_t r, int carry)
{
    nz8(c, r);
    setf(c, F_C, carry);
    setf(c, F_V, !!(c->cc & F_N) ^ !!carry);
    return r;
}

/* $40-$7F: the unary group on a value; returns the result, or -1 (TST). */
static int unary(M6800 *c, int lo, uint8_t v)
{
    switch (lo) {
    case 0x0: {
        uint8_t r = (uint8_t)-v;
        nz8(c, r);
        setf(c, F_V, r == 0x80);
        setf(c, F_C, r != 0);
        return r;
    }
    case 0x3: {
        uint8_t r = (uint8_t)~v;
        nz8(c, r);
        setf(c, F_V, 0);
        setf(c, F_C, 1);
        return r;
    }
    case 0x4: return shift_flags(c, v >> 1, v & 1);                                       /* LSR */
    case 0x6: return shift_flags(c, (uint8_t)((c->cc & F_C) << 7 | v >> 1), v & 1);         /* ROR */
    case 0x7: return shift_flags(c, (uint8_t)((v & 0x80) | v >> 1), v & 1);                 /* ASR */
    case 0x8: return shift_flags(c, (uint8_t)(v << 1), v & 0x80);                           /* ASL */
    case 0x9: return shift_flags(c, (uint8_t)(v << 1 | (c->cc & F_C)), v & 0x80);           /* ROL */
    case 0xA: {
        uint8_t r = (uint8_t)(v - 1);
        nz8(c, r);
        setf(c, F_V, v == 0x80);
        return r;
    }
    case 0xC: {
        uint8_t r = (uint8_t)(v + 1);
        nz8(c, r);
        setf(c, F_V, v == 0x7F);
        return r;
    }
    case 0xD:
        nz8(c, v);
        setf(c, F_V, 0);
        setf(c, F_C, 0);
        return -1;
    case 0xF:
        c->cc = (uint8_t)((c->cc & ~(F_N | F_V | F_C)) | F_Z);
        return 0;
    default: return v;
    }
}

static int branch(const M6800 *c, uint8_t op)
{
    int n = !!(c->cc & F_N), z = !!(c->cc & F_Z), v = !!(c->cc & F_V), cy = !!(c->cc & F_C);
    switch (op & 0x0F) {
    case 0x0: return 1;
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
    case 0xF: return z | (n ^ v);
    default: return 0;
    }
}

static void push_all(M6800 *c)
{
    push16(c, c->pc);
    push16(c, c->x);
    push8(c, c->a);
    push8(c, c->b);
    push8(c, c->cc);
}

void m6800_reset(M6800 *c)
{
    c->cc = 0xC0 | F_I;
    c->waiting = 0;
    c->nmi_prev = 0;
    c->pc = rd16(c, 0xFFFE);
}

static int interrupts(M6800 *c)
{
    int nmi_edge = c->nmi && !c->nmi_prev;
    c->nmi_prev = c->nmi;
    uint16_t vec = 0;
    if (nmi_edge) vec = 0xFFFC;
    else if (c->irq && !(c->cc & F_I)) vec = 0xFFF8;
    if (!vec) return 0;
    if (!c->waiting) push_all(c);
    c->waiting = 0;
    c->cc |= F_I;
    c->pc = rd16(c, vec);
    return 12;
}

int m6800_step(M6800 *c)
{
    int taken = interrupts(c);
    if (taken) {
        c->cycles += taken;
        return taken;
    }
    if (c->waiting) {
        c->cycles += 1;
        return 1;
    }
    uint8_t op = fetch8(c);
    int cyc = CYCLES[op] ? CYCLES[op] : 2;
    int hi = op >> 4, lo = op & 0x0F;

    if (hi == 0x2) { /* branches */
        int8_t off = (int8_t)fetch8(c);
        if (branch(c, op)) c->pc = (uint16_t)(c->pc + off);
    } else if (hi >= 0x4 && hi <= 0x7) { /* unary: A, B, indexed, extended */
        if (hi == 0x4 || hi == 0x5) {
            uint8_t *acc = hi == 0x4 ? &c->a : &c->b;
            int r = unary(c, lo, *acc);
            if (r >= 0) *acc = (uint8_t)r;
        } else {
            uint16_t ea = hi == 0x6 ? (uint16_t)(c->x + fetch8(c)) : fetch16(c);
            if (lo == 0xE) c->pc = ea; /* JMP */
            else {
                int r = unary(c, lo, RD(ea));
                if (r >= 0) WR(ea, r);
            }
        }
    } else if (hi >= 0x8) { /* accumulator ops: immediate, direct, indexed, extended */
        int mode = hi & 3, side_b = hi >= 0xC;
        uint8_t *acc = side_b ? &c->b : &c->a;
        if (!side_b && lo == 0xD) { /* BSR / JSR */
            if (mode == 0) {
                int8_t off = (int8_t)fetch8(c);
                push16(c, c->pc);
                c->pc = (uint16_t)(c->pc + off);
            } else {
                uint16_t ea = mode == 1 ? fetch8(c) : mode == 2 ? (uint16_t)(c->x + fetch8(c)) : fetch16(c);
                push16(c, c->pc);
                c->pc = ea;
            }
        } else {
            int wide = lo == 0xC || lo == 0xE || lo == 0xF; /* CPX, LDS/LDX, STS/STX */
            int store = lo == 0x7 || lo == 0xF;
            uint16_t ea = 0, v16 = 0;
            uint8_t v8 = 0;
            if (mode == 0) {
                if (wide) v16 = fetch16(c);
                else v8 = fetch8(c);
            } else {
                ea = mode == 1 ? fetch8(c) : mode == 2 ? (uint16_t)(c->x + fetch8(c)) : fetch16(c);
                if (!store) {
                    if (wide) v16 = rd16(c, ea);
                    else v8 = RD(ea);
                }
            }
            switch (lo) {
            case 0x0: *acc = sub8(c, *acc, v8, 0); break;
            case 0x1: sub8(c, *acc, v8, 0); break;
            case 0x2: *acc = sub8(c, *acc, v8, c->cc & F_C); break;
            case 0x4: *acc = logic(c, *acc & v8); break;
            case 0x5: logic(c, *acc & v8); break;
            case 0x6: *acc = logic(c, v8); break;
            case 0x7: if (mode) { WR(ea, *acc); logic(c, *acc); } break;
            case 0x8: *acc = logic(c, *acc ^ v8); break;
            case 0x9: *acc = add8(c, *acc, v8, c->cc & F_C); break;
            case 0xA: *acc = logic(c, *acc | v8); break;
            case 0xB: *acc = add8(c, *acc, v8, 0); break;
            case 0xC:
                if (!side_b) { /* CPX: N, Z, V from the 16-bit compare; C untouched */
                    uint32_t r = (uint32_t)c->x - v16;
                    nz16(c, (uint16_t)r);
                    setf(c, F_V, (c->x ^ v16) & (c->x ^ r) & 0x8000);
                }
                break;
            case 0xE:
                if (side_b) c->x = v16;
                else c->sp = v16;
                nz16(c, v16);
                setf(c, F_V, 0);
                break;
            case 0xF:
                if (mode) {
                    uint16_t r = side_b ? c->x : c->sp;
                    wr16(c, ea, r);
                    nz16(c, r);
                    setf(c, F_V, 0);
                }
                break;
            default: break;
            }
        }
    } else {
        switch (op) {
        case 0x06: c->cc = c->a | 0xC0; break;                 /* TAP */
        case 0x07: c->a = c->cc; break;                        /* TPA */
        case 0x08: c->x++; setf(c, F_Z, !c->x); break;         /* INX */
        case 0x09: c->x--; setf(c, F_Z, !c->x); break;         /* DEX */
        case 0x0A: setf(c, F_V, 0); break;
        case 0x0B: setf(c, F_V, 1); break;
        case 0x0C: setf(c, F_C, 0); break;
        case 0x0D: setf(c, F_C, 1); break;
        case 0x0E: setf(c, F_I, 0); break;
        case 0x0F: setf(c, F_I, 1); break;
        case 0x10: c->a = sub8(c, c->a, c->b, 0); break;       /* SBA */
        case 0x11: sub8(c, c->a, c->b, 0); break;              /* CBA */
        case 0x16: c->b = logic(c, c->a); break;               /* TAB */
        case 0x17: c->a = logic(c, c->b); break;               /* TBA */
        case 0x19: {                                           /* DAA */
            uint8_t msn = c->a & 0xF0, lsn = c->a & 0x0F;
            unsigned cf = 0;
            if (lsn > 9 || (c->cc & F_H)) cf |= 0x06;
            if (msn > 0x80 && lsn > 9) cf |= 0x60;
            if (msn > 0x90 || (c->cc & F_C)) cf |= 0x60;
            unsigned t = cf + c->a;
            nz8(c, (uint8_t)t);
            setf(c, F_V, 0);
            if (t & 0x100) c->cc |= F_C;
            c->a = (uint8_t)t;
            break;
        }
        case 0x1B: c->a = add8(c, c->a, c->b, 0); break;       /* ABA */
        case 0x30: c->x = (uint16_t)(c->sp + 1); break;        /* TSX */
        case 0x31: c->sp++; break;                             /* INS */
        case 0x32: c->a = pull8(c); break;                     /* PULA */
        case 0x33: c->b = pull8(c); break;                     /* PULB */
        case 0x34: c->sp--; break;                             /* DES */
        case 0x35: c->sp = (uint16_t)(c->x - 1); break;        /* TXS */
        case 0x36: push8(c, c->a); break;                      /* PSHA */
        case 0x37: push8(c, c->b); break;                      /* PSHB */
        case 0x39: c->pc = pull16(c); break;                   /* RTS */
        case 0x3B:                                             /* RTI */
            c->cc = pull8(c) | 0xC0;
            c->b = pull8(c);
            c->a = pull8(c);
            c->x = pull16(c);
            c->pc = pull16(c);
            break;
        case 0x3E: push_all(c); c->waiting = 1; break;         /* WAI */
        case 0x3F: push_all(c); c->cc |= F_I; c->pc = rd16(c, 0xFFFA); break; /* SWI */
        default: break; /* NOP and undefined */
        }
    }
    c->cycles += cyc;
    return cyc;
}
