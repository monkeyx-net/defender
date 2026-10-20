#include "gfx/williams.h"

#include <math.h>

#include "gfx/blit.h"

/* MAME compute_resistor_weights(0, 255, -1.0, ...) with no pull-ups or
   pull-downs: each bit's weight is its conductance, scaled so that every
   channel with all its bits set is 255. */
static const double R_RG[3] = { 1200, 560, 330 }, R_B[2] = { 560, 330 };

static long level(int bits, const double *ohms, int n)
{
    double total = 0, v = 0;
    for (int i = 0; i < n; i++) {
        total += 1 / ohms[i];
        if (bits >> i & 1) v += 1 / ohms[i];
    }
    return lround(255 * v / total);
}

uint32_t williams_rgb(uint8_t entry)
{
    return (uint32_t)level(entry & 7, R_RG, 3) << 16 | (uint32_t)level(entry >> 3 & 7, R_RG, 3) << 8 |
           (uint32_t)level(entry >> 6 & 3, R_B, 2);
}

void williams_draw_vram(Framebuffer *fb, const uint8_t *vram, const uint8_t palette[16])
{
    uint32_t rgb[16];
    for (int i = 0; i < 16; i++) rgb[i] = williams_rgb(palette[i]);
    for (int y = 0; y < FB_HEIGHT; y++)
        for (int x = 0; x < FB_WIDTH; x++) {
            int vx = x + WILLIAMS_VISIBLE_X, vy = y + WILLIAMS_VISIBLE_Y;
            uint8_t b = vram[(vx >> 1) * 256 + vy];
            fill_rectf(fb, x, y, 1, 1, rgb[vx & 1 ? b & 0x0F : b >> 4]);
        }
}
