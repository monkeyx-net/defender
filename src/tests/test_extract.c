/*
 * The extractor (label rom):  test_extract <defender.zip|folder> <work dir>
 *   - extracts twice (<work>/a, <work>/b): the folders are byte-identical
 *   - each file is there: manifest, 31 picture descriptors, 45 glyphs, both
 *     logos, the palette, a WAV of the stated length for every sound, and
 *     the 26 sound scripts
 *   - the font is the ROM's: each letter of "PRESENTS" appears, pixel for
 *     pixel, where the attract mode wrote the word (the emulated board)
 *   - with no ROM set loaded it fails and writes no manifest
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "assets/extract.h"
#include "assets/rom.h"
#include "emu/board.h"

#define PRESENTS_TOP 100   /* the rows of "PRESENTS" in the attract mode, frame 1500 */
#define PRESENTS_BOTTOM 120

static RomSet rom;
static Board board;
static int failures;

#define CHECK(cond, ...)                                    \
    do {                                                    \
        if (!(cond)) {                                      \
            fprintf(stderr, "test_extract: " __VA_ARGS__);  \
            fprintf(stderr, "\n");                          \
            failures++;                                     \
        }                                                   \
    } while (0)

static char *slurp(const char *dir, const char *name, long *size)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n + 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    buf[n] = '\0';
    if (size) *size = n;
    return buf;
}

static int count_lines(const char *text, const char *prefix)
{
    int n = 0;
    size_t len = strlen(prefix);
    for (const char *p = text; p && *p; p = strchr(p, '\n'), p = p ? p + 1 : NULL)
        if (!strncmp(p, prefix, len)) n++;
    return n;
}

static void same_file(const char *a, const char *b, const char *name)
{
    long na = 0, nb = 0;
    char *fa = slurp(a, name, &na), *fb = slurp(b, name, &nb);
    CHECK(fa && fb, "%s missing", name);
    if (fa && fb) CHECK(na == nb && !memcmp(fa, fb, (size_t)na), "%s differs between two extractions", name);
    free(fa);
    free(fb);
}

static void check_sounds(const char *dir)
{
    char *list = slurp(dir, "sounds/sounds.txt", NULL);
    CHECK(list, "sounds/sounds.txt missing");
    if (!list) return;
    int commands = 0, recorded = 0;
    for (char *p = list; *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : p + strlen(p)) {
        if (*p == '#') continue;
        unsigned cmd;
        char kind[8];
        long samples;
        if (sscanf(p, "%x %7s %ld", &cmd, kind, &samples) != 3) continue;
        commands++;
        if (!strcmp(kind, "none")) continue;
        char name[32];
        long size = 0;
        snprintf(name, sizeof name, "sounds/%02X.wav", cmd);
        char *wav = slurp(dir, name, &size);
        CHECK(wav && size == 44 + samples * 2 && !memcmp(wav, "RIFF", 4) && !memcmp(wav + 8, "WAVE", 4),
              "%s: not a WAV of %ld samples", name, samples);
        if (wav) {
            int peak = 0;
            for (long i = 0; i < samples; i++) {
                int v = (int16_t)((uint8_t)wav[44 + i * 2] | (uint8_t)wav[45 + i * 2] << 8);
                if (abs(v) > peak) peak = abs(v);
            }
            CHECK(peak > 1000, "%s is silent", name);
            recorded++;
        }
        free(wav);
    }
    CHECK(commands == 31, "sounds.txt lists %d commands, not 31", commands);
    CHECK(recorded >= 25, "only %d sounds recorded", recorded);
    free(list);
}

/* The glyph for `c` in font.txt, as rows of hex digits; 0 if absent. */
static int find_glyph(const char *font, char c, int *w, int *h, const char **rows)
{
    for (const char *p = font; p && *p; p = strchr(p, '\n'), p = p ? p + 1 : NULL) {
        char g;
        if (sscanf(p, "glyph %c %d %d", &g, w, h) == 3 && g == c) {
            *rows = strchr(p, '\n') + 1;
            return 1;
        }
    }
    return 0;
}

static int screen_pixel(int x, int y)
{
    uint8_t v = board.ram[(x / 2) * 256 + y];
    return x & 1 ? v & 15 : v >> 4;
}

static int glyph_on_screen(int w, int h, const char *rows)
{
    for (int y0 = PRESENTS_TOP; y0 + h <= PRESENTS_BOTTOM; y0++)
        for (int x0 = 0; x0 + w <= WILLIAMS_SCREEN_SIZE / 256 * 2; x0++) {
            int ok = 1, lit = 0;
            for (int y = 0; ok && y < h; y++)
                for (int x = 0; ok && x < w; x++) {
                    char d = rows[y * (w + 1) + x];
                    int v = d <= '9' ? d - '0' : d - 'A' + 10;
                    lit |= v;
                    ok = screen_pixel(x0 + x, y0 + y) == v;
                }
            if (ok && lit) return 1;
        }
    return 0;
}

static void check_font(const char *dir)
{
    char *font = slurp(dir, "gfx/font.txt", NULL);
    if (!font) return;
    static uint8_t cmos[BOARD_CMOS_SIZE];
    board_init(&board, &rom, NULL);
    for (int i = 0; i < 1800; i++) board_run_frame(&board, NULL);
    memcpy(cmos, board.cmos, sizeof cmos);
    board_init(&board, &rom, cmos);
    for (int f = 0; f < 1500; f++) board_run_frame(&board, NULL);
    for (const char *c = "PRESENTS"; *c; c++) {
        int w, h;
        const char *rows;
        CHECK(find_glyph(font, *c, &w, &h, &rows), "no glyph for '%c'", *c);
        if (find_glyph(font, *c, &w, &h, &rows))
            CHECK(glyph_on_screen(w, h, rows), "glyph '%c' is not what the attract mode draws", *c);
    }
    free(font);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test_extract <defender.zip|folder> <work dir>\n");
        return 2;
    }
    char err[512], a[1024], b[1024], none[1024];
    snprintf(a, sizeof a, "%s/a", argv[2]);
    snprintf(b, sizeof b, "%s/b", argv[2]);
    snprintf(none, sizeof none, "%s/none", argv[2]);

    RomSet empty;
    memset(&empty, 0, sizeof empty);
    CHECK(extract_all(&empty, none, err, sizeof err) != 0, "extracted without a ROM set");
    char *m = slurp(none, EXTRACT_MANIFEST, NULL);
    CHECK(!m, "manifest written without a ROM set");
    free(m);

    if (rom_load(&rom, argv[1], err, sizeof err)) {
        fprintf(stderr, "test_extract: %s\n", err);
        return 1;
    }
    if (extract_all(&rom, a, err, sizeof err) || extract_all(&rom, b, err, sizeof err)) {
        fprintf(stderr, "test_extract: %s\n", err);
        return 1;
    }

    static const char *const files[] = {
        EXTRACT_MANIFEST, "gfx/pictures.txt", "gfx/font.txt", "gfx/logos.txt", "gfx/palette.txt",
        "gfx/pictures.png", "gfx/font.png", "gfx/williams.png", "gfx/defender.png", "sounds/sounds.txt",
        "sounds/scripts.txt",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; i++) same_file(a, b, files[i]);

    char *pictures = slurp(a, "gfx/pictures.txt", NULL), *font = slurp(a, "gfx/font.txt", NULL),
         *logos = slurp(a, "gfx/logos.txt", NULL);
    if (pictures) {
        /* every descriptor once (the even data), some with odd-x data too */
        int even = 0;
        for (const char *p = pictures; (p = strstr(p, " even\n")); p++) even++;
        CHECK(even == 31, "pictures.txt has %d pictures, not 31", even);
    }
    if (font) CHECK(count_lines(font, "glyph ") == 45, "font.txt has %d glyphs, not 45", count_lines(font, "glyph "));
    if (logos) CHECK(count_lines(logos, "image ") == 2, "logos.txt has %d images, not 2", count_lines(logos, "image "));
    free(pictures);
    free(font);
    free(logos);

    char *scripts = slurp(a, "sounds/scripts.txt", NULL);
    CHECK(scripts && count_lines(scripts, "script ") == 26, "scripts.txt has %d scripts, not 26",
          scripts ? count_lines(scripts, "script ") : 0);
    free(scripts);
    check_sounds(a);
    check_font(a);

    if (failures) return 1;
    printf("test_extract: OK\n");
    return 0;
}
