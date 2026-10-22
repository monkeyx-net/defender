#include "gfx/williams.h"

#include <math.h>
#include <string.h>


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

/* Video RAM is by column, a byte for two pixels: at scale 1 it is read in
   that order, each byte's pair of colours from a table (from the Joust
   port). Larger scales go a row at a time, each pixel and row repeated. */
_Static_assert(WILLIAMS_VISIBLE_X % 2 == 0 && FB_WIDTH % 2 == 0, "whole bytes across");

void williams_draw_vram(Framebuffer *fb, const uint8_t *vram, const uint8_t palette[16])
{
    uint32_t rgb[16];
    for (int i = 0; i < 16; i++) rgb[i] = 0xFF000000u | williams_rgb(palette[i]);
    const int s = fb->scale;
    if (s == 1) {
        uint32_t pair[256][2];
        for (int b = 0; b < 256; b++) {
            pair[b][0] = rgb[b >> 4];
            pair[b][1] = rgb[b & 0x0F];
        }
        const long pitch = fb->w; /* locals: the stores could alias fb and vram */
        for (int cx = 0; cx < FB_WIDTH / 2; cx++) {
            const uint8_t *col = vram + (cx + WILLIAMS_VISIBLE_X / 2) * 256 + WILLIAMS_VISIBLE_Y;
            uint32_t *d = fb_row(fb, 0) + cx * 2;
            for (int y = 0; y < FB_HEIGHT; y++, d += pitch) {
                const uint32_t *p = pair[col[y]];
                d[0] = p[0];
                d[1] = p[1];
            }
        }
        return;
    }
    for (int y = 0; y < FB_HEIGHT; y++) {
        int vy = y + WILLIAMS_VISIBLE_Y;
        uint32_t *row = fb_row(fb, y * s);
        for (int x = 0; x < FB_WIDTH; x++) {
            int vx = x + WILLIAMS_VISIBLE_X;
            uint8_t b = vram[(vx >> 1) * 256 + vy];
            uint32_t c = rgb[vx & 1 ? b & 0x0F : b >> 4];
            for (int i = 0; i < s; i++) row[x * s + i] = c;
        }
        for (int i = 1; i < s; i++) memcpy(fb_row(fb, y * s + i), row, (size_t)fb->w * sizeof *row);
    }
}
