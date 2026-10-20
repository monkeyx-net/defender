#include "sim/rng.h"

uint8_t rng_step(Rng *r)
{
    unsigned b = (unsigned)r->value * 3 + 0x11;
    /* the shift register: bit 0 xor bit 3 of <$E1 goes in at the top */
    unsigned in = (r->lo ^ (r->lo >> 3)) & 1;
    unsigned out = r->hi & 1;
    r->hi = (uint8_t)(r->hi >> 1 | in << 7);
    r->lo = (uint8_t)(r->lo >> 1 | out << 7);
    unsigned sum = (b & 0xFF) + r->lo;
    sum = (sum & 0xFF) + r->hi + (sum >> 8);
    r->value = (uint8_t)sum;
    return r->value;
}
