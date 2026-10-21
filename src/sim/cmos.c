#include "sim/cmos.h"

#define CREDITS 0xA037
#define UNITS 0xA038
#define BONUS_UNITS 0xA039

static uint8_t nibble(const Game *g, uint16_t addr) { return g->cmos[(uint8_t)addr]; }

uint8_t cmos_read(const Game *g, uint16_t addr)
{
    return (uint8_t)((nibble(g, addr) << 4) | (nibble(g, (uint16_t)(addr + 1)) & 0x0F));
}

void cmos_write(Game *g, uint16_t addr, uint8_t value)
{
    g->cmos[(uint8_t)(addr + 1)] = (uint8_t)(value | 0xF0);
    g->cmos[(uint8_t)addr] = (uint8_t)((value >> 4) | 0xF0);
}

/* ADDA then DAA, as score.c: a + b + carry in BCD. */
static uint8_t bcd_add(uint8_t a, uint8_t b, int *carry)
{
    unsigned sum = (unsigned)a + b + (unsigned)*carry;
    int half = ((a & 0xF) + (b & 0xF) + *carry) > 0xF;
    int c = sum > 0xFF;
    uint8_t r = (uint8_t)sum;
    unsigned lsn = r & 0xF, msn = r >> 4, fix = 0;
    if (half || lsn > 9) fix |= 0x06;
    if (c || msn > 9 || (msn > 8 && lsn > 9)) fix |= 0x60;
    unsigned t = (unsigned)r + fix;
    *carry = c || t > 0xFF;
    return (uint8_t)t;
}

void bookkeep(Game *g, int counter, uint8_t amount)
{
    counter &= 7;
    if (!counter) return;
    uint16_t x = (uint16_t)(0xC3FD + counter * 4);
    uint8_t hi = cmos_read(g, x), lo = cmos_read(g, (uint16_t)(x + 2));
    int carry = 0;
    lo = bcd_add(amount, lo, &carry);
    hi = bcd_add(hi, 0, &carry);
    cmos_write(g, x, hi);
    cmos_write(g, (uint16_t)(x + 2), lo);
}

void credits_add(Game *g, uint8_t amount)
{
    int carry = 0;
    uint8_t a = bcd_add(amount, g->ram[CREDITS], &carry);
    if (carry) a = 0x99;
    g->ram[CREDITS] = a;
    cmos_write(g, 0xC47D, a);
}

uint8_t bcd_to_binary(uint8_t a)
{
    uint8_t tens = 0;
    while (a >= 0x10) {
        tens = (uint8_t)(tens + 10);
        a = (uint8_t)(a - 0x10);
    }
    return (uint8_t)(a + tens);
}

/* $CC9F: a BCD count as binary, by counting it down. */
static uint8_t count_down(uint8_t bcd)
{
    uint8_t n = 0;
    while (bcd) {
        int carry = 0;
        bcd = bcd_add(bcd, 0x99, &carry);
        n++;
    }
    return n;
}

/* $CC89: a / b, the quotient in BCD; *rest the remainder. */
static uint8_t divide(uint8_t a, uint8_t b, uint8_t *rest)
{
    if (!b) {
        *rest = b;
        return 0;
    }
    uint8_t q = 0x99, r = a;
    int borrow;
    do {
        int carry = 0;
        q = bcd_add(q, 1, &carry);
        borrow = r < b;
        r = (uint8_t)(r - b);
    } while (!borrow);
    *rest = (uint8_t)(r + b);
    return q;
}

void coin_inserted(Game *g, int slot)
{
    uint8_t *r = g->ram;
    bookkeep(g, slot, 1);
    uint8_t units = count_down(cmos_read(g, (uint16_t)(0xC487 + 2 * slot)));
    r[BONUS_UNITS] = (uint8_t)(r[BONUS_UNITS] + units);
    r[UNITS] = (uint8_t)(r[UNITS] + units);
    if (r[UNITS] < count_down(cmos_read(g, 0xC493))) return;
    uint8_t rest;
    uint8_t credits = divide(r[UNITS], count_down(cmos_read(g, 0xC48F)), &rest);
    r[UNITS] = rest;
    uint8_t bonus = divide(r[BONUS_UNITS], count_down(cmos_read(g, 0xC491)), &rest);
    if (bonus) r[UNITS] = r[BONUS_UNITS] = 0;
    int carry = 0;
    uint8_t total = bcd_add(bonus, credits, &carry);
    bookkeep(g, 4, total);
    credits_add(g, total);
}
