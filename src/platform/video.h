#ifndef DEFENDER_VIDEO_H
#define DEFENDER_VIDEO_H

#include <stdbool.h>
#include "gfx/fb.h"

/* Fullscreen-desktop window (windowed if `windowed`), vsync, nearest scaling. */
bool video_init(bool windowed);
/* The largest integer scale of the arcade screen that fits the window (>= 1). */
int video_display_scale(void);
/* Upload the framebuffer (any scale) and present it at the largest integer
   scale, with CRT scanlines over it if `scanlines`. */
void video_present(const Framebuffer *fb, bool scanlines);
void video_shutdown(void);

#endif
