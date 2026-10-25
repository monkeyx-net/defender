#ifndef DEFENDER_VIDEO_H
#define DEFENDER_VIDEO_H

#include <stdbool.h>
#include "gfx/fb.h"

/* How the picture fills the screen:
 *   AUTO     INTEGER when that fills at least 95% of FIT, else FIT
 *   INTEGER  a whole number of output rows per arcade row (FIT if not even 1)
 *   FIT      as large as fits, at the aspect
 *   STRETCH  the whole screen, whatever its shape
 * Sizes that are not a whole multiple of the frame are drawn "sharp bilinear":
 * scaled up by whole numbers (nearest), then down to the size (linear), so
 * pixels stay even and only their edges blend. */
typedef enum { VIDEO_AUTO, VIDEO_INTEGER, VIDEO_FIT, VIDEO_STRETCH, VIDEO_SCALES } VideoScale;
/* 4:3 as the cabinet's monitor showed it (each pixel 320/292 wide), or square pixels. */
typedef enum { VIDEO_4_3, VIDEO_SQUARE, VIDEO_ASPECTS } VideoAspect;

/* Fullscreen-desktop window (windowed if `windowed`), vsync, nearest scaling. */
bool video_init(bool windowed);
/* Scale and aspect (VideoScale, VideoAspect); may be set before video_init. */
void video_set_mode(int scale, int aspect);
void video_get_mode(int *scale, int *aspect);
/* By command-line name ("auto", "integer", "fit", "stretch"; "4:3", "square");
   -1 if unknown. */
int video_parse_scale(const char *name);
int video_parse_aspect(const char *name);
/* "AUTO (FIT) 4:3", ... for the on-screen notice. */
const char *video_describe(void);
/* Device pixels per arcade pixel to render at (>= 1): the picture's height
   over the arcade's, rounded down. */
int video_display_scale(void);
/* Upload the framebuffer (any scale) and present it in the mode, with CRT
   scanlines over it if `scanlines`. */
void video_present(const Framebuffer *fb, bool scanlines);
void video_shutdown(void);

#endif
