/*
 * A MAME capture against the port's decoding (label mame):
 *   test_capture <frame_N.txt> <MAME snapshot of frame N>
 * The frame file holds the palette and video RAM the probe dumped
 * (src/tests/mame/capture_probe.lua); decoded through gfx/williams.c it
 * must give MAME's snapshot pixel for pixel.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/png.h"
#include "gfx/williams.h"

static uint8_t vram[WILLIAMS_VRAM_SIZE], palette[16];

static int hex(char c)
{
    return c >= '0' && c <= '9' ? c - '0' : c >= 'A' && c <= 'F' ? c - 'A' + 10 : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
}

static int load_frame(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    static char line[2048];
    int cols = 0, have_palette = 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "palette ", 8)) {
            unsigned v[16];
            have_palette = sscanf(line + 8, "%x %x %x %x %x %x %x %x %x %x %x %x %x %x %x %x", &v[0], &v[1], &v[2],
                                  &v[3], &v[4], &v[5], &v[6], &v[7], &v[8], &v[9], &v[10], &v[11], &v[12], &v[13],
                                  &v[14], &v[15]) == 16;
            for (int i = 0; i < 16; i++) palette[i] = (uint8_t)v[i];
        } else if (!strncmp(line, "vram ", 5)) {
            char *end;
            long col = strtol(line + 5, &end, 10);
            if (col < 0 || col >= 192 || *end != ' ') continue;
            const char *p = end + 1;
            for (int y = 0; y < 256; y++) {
                int hi = hex(p[2 * y]), lo = hex(p[2 * y + 1]);
                if (hi < 0 || lo < 0) break;
                vram[col * 256 + y] = (uint8_t)(hi << 4 | lo);
            }
            cols++;
        }
    }
    fclose(f);
    return have_palette && cols == 192 ? 0 : -1;
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test_capture <frame_N.txt> <snapshot.png>\n");
        return 2;
    }
    if (load_frame(argv[1])) {
        fprintf(stderr, "%s: not a capture frame\n", argv[1]);
        return 1;
    }
    Image shot;
    const char *err;
    if (png_load(argv[2], &shot, &err)) {
        fprintf(stderr, "%s: %s\n", argv[2], err);
        return 1;
    }
    if (shot.width != FB_WIDTH || shot.height != FB_HEIGHT) {
        fprintf(stderr, "%s: %dx%d, want %dx%d\n", argv[2], shot.width, shot.height, FB_WIDTH, FB_HEIGHT);
        return 1;
    }
    Framebuffer fb = { 0 };
    fb_init(&fb, 1);
    williams_draw_vram(&fb, vram, palette);
    int bad = 0, shown = 0;
    for (int y = 0; y < FB_HEIGHT; y++)
        for (int x = 0; x < FB_WIDTH; x++) {
            const uint8_t *p = shot.rgba + ((size_t)y * FB_WIDTH + x) * 4;
            uint32_t want = (uint32_t)p[0] << 16 | (uint32_t)p[1] << 8 | p[2], got = fb_row(&fb, y)[x] & 0xFFFFFF;
            if (want == got) continue;
            if (shown++ < 8) {
                int vx = x + WILLIAMS_VISIBLE_X, vy = y + WILLIAMS_VISIBLE_Y;
                uint8_t b = vram[(vx >> 1) * 256 + vy];
                int idx = vx & 1 ? b & 0x0F : b >> 4;
                fprintf(stderr, "  %d,%d: entry %d (palette %02X) is %06X, MAME %06X\n", x, y, idx, palette[idx],
                        (unsigned)got, (unsigned)want);
            }
            bad++;
        }
    fb_free(&fb);
    image_free(&shot);
    printf("%s: %d of %d pixels differ\n", argv[1], bad, FB_WIDTH * FB_HEIGHT);
    return bad ? 1 : 0;
}
