#include "sim/ship.h"

#define IN0_THRUST 0x02
#define IN0_DOWN 0x80
#define IN1_UP 0x01
#define TOP 0x2B    /* up stops at or above this line */
#define BOTTOM 0xEE /* down stops at or below it */

static int32_t sext24(int32_t v)
{
    v &= 0xFFFFFF;
    return (v & 0x800000) ? v - 0x1000000 : v;
}

/* <$C7-<$C8, the velocity's top 16 bits */
static int16_t speed(int32_t v) { return (int16_t)(uint16_t)(((uint32_t)v >> 8) & 0xFFFF); }

void ship_step(Ship *s, const ShipInput *in)
{
    /* friction: -4 x speed (16 bits) into the low 16 bits, sign of -speed */
    uint16_t neg = (uint16_t)-(uint16_t)speed(s->velocity);
    uint16_t d = (uint16_t)(neg << 2);
    s->velocity = sext24(s->velocity + (int32_t)d + ((neg & 0x8000) ? -0x10000 : 0));
    if (in->in0 & IN0_THRUST) s->velocity = sext24(s->velocity + in->thrust);

    /* where the ship should be on screen: ahead of its facing by the speed */
    uint16_t q = (uint16_t)speed(s->velocity);
    q = (uint16_t)((int16_t)q >> 2); /* arithmetic, as every target compiler does */
    uint8_t lead_hi = (uint8_t)((int8_t)(q & 0xFF) >> 1), lead_lo = (uint8_t)((q & 1) << 7);
    uint8_t base;
    if (in->thrust >= 0) {
        base = 0x20;
        if (lead_hi & 0x80) lead_hi = lead_lo = 0;
    } else {
        base = 0x70;
        if (!(lead_hi & 0x80)) lead_hi = lead_lo = 0;
    }
    uint16_t target = (uint16_t)((uint8_t)(base + lead_hi) << 8 | lead_lo);
    uint16_t diff = (uint16_t)(target - s->screen_x);
    s->slide = 0;
    if (diff && target < s->screen_x) {
        if ((int16_t)diff <= -0x100) {
            s->slide = -0x40;
            target = (uint16_t)(s->screen_x - 0x100);
        }
    } else if (diff > 0x100) {
        s->slide = 0x40;
        target = (uint16_t)(s->screen_x + 0x100);
    }
    s->screen_x = target;

    /* scroll, at most $100 units a frame */
    s->last_scroll = s->scroll;
    int16_t v = speed(s->velocity);
    if (v >= 0x100) v = 0x100;
    if (v <= -0x100) v = -0x100;
    s->velocity = sext24((int32_t)v * 256 + (s->velocity & 0xFF));
    s->scroll = (uint16_t)(s->scroll + (uint16_t)v - (uint16_t)s->slide);
    s->world_x = (uint16_t)(s->scroll + ((s->screen_x >> 2) & 0xFFE0));

    /* up and down */
    uint8_t line = (uint8_t)(s->y >> 8);
    int16_t vy;
    if (in->in1 & IN1_UP) {
        if (line <= TOP) return;
        if (s->vy >= 0) vy = -0x100;
        else {
            vy = (int16_t)(s->vy - 8);
            if (vy < -0x200) vy = -0x200;
        }
    } else if (in->in0 & IN0_DOWN) {
        if (line >= BOTTOM) return;
        if (s->vy <= 0) vy = 0x100;
        else {
            vy = (int16_t)(s->vy + 8);
            if ((uint16_t)vy > 0x200) vy = 0x200;
        }
    } else {
        vy = 0;
    }
    s->vy = vy;
    s->y = (uint16_t)(s->y + (uint16_t)vy);
}
