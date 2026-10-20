/*
 * The framebuffer every frame is drawn into, in software. Drawing always
 * uses arcade coordinates (292x240); `scale` > 1 gives enhanced rendering
 * that many device pixels per arcade pixel. The platform layer uploads it
 * to a texture and scales it to the screen.
 */
#ifndef DEFENDER_FB_H
#define DEFENDER_FB_H

#include <stdint.h>

#define FB_WIDTH  292
#define FB_HEIGHT 240
#define FB_MAX_SCALE 4

typedef struct {
    int scale;    /* device pixels per arcade pixel, 1..FB_MAX_SCALE */
    int w, h;     /* FB_WIDTH * scale, FB_HEIGHT * scale */
    uint32_t *px; /* ARGB8888, w * h, row-major */
} Framebuffer;

/* (Re)allocate for `scale`; returns 0, or -1 when out of memory. */
int fb_init(Framebuffer *fb, int scale);
void fb_free(Framebuffer *fb);

static inline uint32_t *fb_row(const Framebuffer *fb, int y) { return fb->px + (long)y * fb->w; }

void fb_clear(Framebuffer *fb, uint32_t argb);
/* In arcade pixels, clipped to the framebuffer. */
void fb_fill_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t argb);
/* 1-px outline, clipped. */
void fb_rect(Framebuffer *fb, int x, int y, int w, int h, uint32_t argb);

#endif
