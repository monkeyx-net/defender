#include "assets/extract.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#endif

#include "assets/png.h"
#include "emu/board.h"
#include "emu/sound.h"
#include "gfx/williams.h"

#define GFX_BANK 2            /* the drawing routines select bank 2 */
#define FONT_ADDR 0xC5E3      /* bank 2: 45 x (width bytes, height, data) */
#define FONT_GLYPHS 45
#define PRIME_FRAMES 1800     /* blank CMOS: the game restores its factory settings */
#define LOGO_FRAME 1500       /* after the reboot: both logos drawn */
#define COIN_FRAME (LOGO_FRAME + 10)
#define START_FRAME (LOGO_FRAME + 100)
#define PLAY_FRAME (LOGO_FRAME + 400) /* in play */
#define SOUND_RATE 48000
#define SOUND_BOOT_CYCLES 20000
#define SOUND_STEP_CYCLES 64
#define SOUND_QUIET_SECONDS 1 /* no DAC change this long: the sound has ended */
#define SOUND_MAX_SECONDS 20
#define SOUND_LOOP_SECONDS 4  /* what a looping sound's WAV keeps */
#define SOUND_TAIL (SOUND_RATE / 20)
#define SOUND_COMMANDS 31     /* $00-$1E; $1F is the game's "no command" */

/* The picture descriptors in the fixed ROM. Six-byte or longer entries hold
   even-x and odd-x data; `four` entries hold one data pointer only. */
static const struct {
    unsigned addr;
    const char *name;
    int four;
} pictures[] = {
    { 0xF8CE, "picture", 0 }, { 0xF8D8, "picture", 0 }, { 0xF8E2, "picture", 0 },
    { 0xF8EC, "dot", 0 },     { 0xF8F7, "picture", 0 }, { 0xF901, "picture", 0 },
    { 0xF90B, "picture", 0 }, { 0xF915, "picture", 0 }, { 0xF91F, "picture", 0 },
    { 0xF929, "picture", 0 }, { 0xF933, "picture", 0 }, { 0xF93D, "picture", 0 },
    { 0xF947, "picture", 0 }, { 0xF951, "picture", 0 }, { 0xF95B, "picture", 0 },
    { 0xF965, "picture", 0 }, { 0xF96F, "line", 1 },    { 0xF97B, "picture", 0 },
    { 0xF985, "lander", 0 },  { 0xF98F, "lander", 0 },  { 0xF999, "lander", 0 },
    { 0xF9A3, "baiter", 0 },  { 0xF9AD, "baiter", 0 },  { 0xF9B7, "baiter", 0 },
    { 0xF9C1, "ship", 0 },    { 0xF9CB, "ship", 0 },    { 0xF9D5, "ships_left", 1 },
    { 0xF9D9, "smart_bombs", 1 }, { 0xF9DD, "score_250", 0 }, { 0xF9E7, "score_500", 0 },
    { 0xF9F1, "picture", 0 },
};
#define PICTURES ((int)(sizeof pictures / sizeof pictures[0]))

/* The font's glyph order: the ROM's character routine (bank 2 $CB29) maps
   ASCII to these entries, anything else to entry 3 ('-'). */
static const char font_chars[FONT_GLYPHS + 1] = " !,-./0123456789:?@ABCDEFGHIJKLMNOPQRSTUVWXYZ";

/* ---- files ----------------------------------------------------------------- */

static int mkdir_p(const char *path)
{
    char tmp[1100];
    snprintf(tmp, sizeof tmp, "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p != '/') continue;
        *p = '\0';
        if (mkdir(tmp, 0755) && errno != EEXIST) return -1;
        *p = '/';
    }
    return mkdir(tmp, 0755) && errno != EEXIST ? -1 : 0;
}

/* Write `size` bytes to <dir>/<name> through a temp file, so a failure leaves no partial file. */
static int write_file(const char *dir, const char *name, const void *data, size_t size, char *err, int errlen)
{
    char path[1200], tmp[1210];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f || fwrite(data, 1, size, f) != size || fclose(f)) {
        if (f) fclose(f);
        remove(tmp);
        snprintf(err, (size_t)errlen, "cannot write %s", path);
        return -1;
    }
#ifdef _WIN32
    remove(path); /* Windows rename() does not replace */
#endif
    if (rename(tmp, path)) {
        remove(tmp);
        snprintf(err, (size_t)errlen, "cannot replace %s", path);
        return -1;
    }
    return 0;
}

/* A growing text buffer. */
typedef struct {
    char *s;
    size_t len, cap;
} Text;

static void put(Text *t, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static void put(Text *t, const char *fmt, ...)
{
    va_list ap;
    for (;;) {
        va_start(ap, fmt);
        int n = vsnprintf(t->s ? t->s + t->len : NULL, t->s ? t->cap - t->len : 0, fmt, ap);
        va_end(ap);
        if (n < 0) return;
        if (t->s && t->len + (size_t)n < t->cap) {
            t->len += (size_t)n;
            return;
        }
        size_t cap = t->cap ? t->cap * 2 : 4096;
        while (cap < t->len + (size_t)n + 1) cap *= 2;
        char *s = realloc(t->s, cap);
        if (!s) abort();
        t->s = s;
        t->cap = cap;
    }
}

static int write_text(const char *dir, const char *name, Text *t, char *err, int errlen)
{
    int rc = write_file(dir, name, t->s ? t->s : "", t->len, err, errlen);
    free(t->s);
    memset(t, 0, sizeof *t);
    return rc;
}

/* ---- pictures -------------------------------------------------------------- */

/* A byte as the drawing routines see it: the fixed ROM, or bank 2 at $C000. */
static int gfx_byte(const RomSet *rom, unsigned addr)
{
    if (addr >= 0xD000 && addr <= 0xFFFF) return rom_byte(rom, addr);
    if (addr >= 0xC000 && addr < 0xD000) return rom_bank_byte(rom, GFX_BANK, addr - 0xC000);
    return -1;
}

static int gfx_word(const RomSet *rom, unsigned addr)
{
    int hi = gfx_byte(rom, addr), lo = gfx_byte(rom, addr + 1);
    return hi < 0 || lo < 0 ? -1 : hi << 8 | lo;
}

/* Pixel x, y of a w-byte, h-row picture at `data` (column-major, as the
   screen: byte (x/2)*h + y, high nibble first). */
static int gfx_pixel(const RomSet *rom, unsigned data, int h, int x, int y)
{
    int v = gfx_byte(rom, data + (unsigned)(x / 2 * h + y));
    return x & 1 ? v & 15 : v >> 4;
}

/* An image of palette indices: what the text files and previews hold. */
typedef struct {
    char name[32];
    int w, h;
    uint8_t px[256 * 64];
} Indexed;

static int read_picture(const RomSet *rom, unsigned data, int wbytes, int h, Indexed *out)
{
    if (wbytes < 1 || h < 1 || wbytes * 2 * h > (int)sizeof out->px) return -1;
    for (int i = 0; i < wbytes * h; i++)
        if (gfx_byte(rom, data + (unsigned)i) < 0) return -1;
    out->w = wbytes * 2;
    out->h = h;
    for (int y = 0; y < h; y++)
        for (int x = 0; x < out->w; x++) out->px[y * out->w + x] = (uint8_t)gfx_pixel(rom, data, h, x, y);
    return 0;
}

static void put_rows(Text *t, const Indexed *img)
{
    for (int y = 0; y < img->h; y++) {
        for (int x = 0; x < img->w; x++) put(t, "%X", img->px[y * img->w + x]);
        put(t, "\n");
    }
}

/* Lay images out in a row, 2 pixels apart, through the palette. */
static int write_preview(const char *dir, const char *name, const Indexed *imgs, int n, const uint8_t pal[16],
                         char *err, int errlen)
{
    int w = 2, h = 4;
    for (int i = 0; i < n; i++) {
        w += imgs[i].w + 2;
        if (imgs[i].h + 4 > h) h = imgs[i].h + 4;
    }
    uint8_t *rgba = calloc((size_t)w * (size_t)h, 4);
    if (!rgba) abort();
    for (int i = 0, x0 = 2; i < n; x0 += imgs[i].w + 2, i++)
        for (int y = 0; y < imgs[i].h; y++)
            for (int x = 0; x < imgs[i].w; x++) {
                uint32_t c = williams_rgb(pal[imgs[i].px[y * imgs[i].w + x]]);
                uint8_t *p = rgba + ((size_t)(y + 2) * (size_t)w + (size_t)(x0 + x)) * 4;
                p[0] = (uint8_t)(c >> 16);
                p[1] = (uint8_t)(c >> 8);
                p[2] = (uint8_t)c;
                p[3] = 0xFF;
            }
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    int rc = png_write(path, rgba, w, h);
    free(rgba);
    if (rc) snprintf(err, (size_t)errlen, "cannot write %s", path);
    return rc;
}

static void put_palette(Text *t, const uint8_t pal[16])
{
    for (int i = 0; i < 16; i++) put(t, " %02X", pal[i]);
    put(t, "\n");
}

static int extract_pictures(const RomSet *rom, const char *dir, const uint8_t pal[16], char *err, int errlen)
{
    static Indexed imgs[PICTURES * 2];
    int n = 0;
    Text t = {0};
    put(&t, "# Object pictures, descriptors at ROM $F8CE-$F9F0 (extracted; do not edit)\n"
            "# picture <descriptor> <name> <width> <height> <even|odd>, then <height> rows of\n"
            "# palette indices; odd is the picture drawn at an odd x, from its own data\n");
    for (int i = 0; i < PICTURES; i++) {
        unsigned d = pictures[i].addr;
        int wbytes = gfx_byte(rom, d), h = gfx_byte(rom, d + 1);
        int even = gfx_word(rom, d + 2), odd = pictures[i].four ? -1 : gfx_word(rom, d + 4);
        if (wbytes < 1 || wbytes > 16 || h < 1 || h > 32 || even < 0 || (!pictures[i].four && odd < 0)) {
            snprintf(err, (size_t)errlen, "picture $%04X: unexpected descriptor", d);
            free(t.s);
            return -1;
        }
        for (int v = 0; v < 2; v++) {
            if (v == 1 && (odd < 0 || odd == even)) break;
            Indexed *img = &imgs[n];
            if (read_picture(rom, (unsigned)(v ? odd : even), wbytes, h, img)) {
                snprintf(err, (size_t)errlen, "picture $%04X: data outside the ROM", d);
                free(t.s);
                return -1;
            }
            snprintf(img->name, sizeof img->name, "%s", pictures[i].name);
            put(&t, "picture %04X %s %d %d %s\n", d, pictures[i].name, img->w, img->h, v ? "odd" : "even");
            put_rows(&t, img);
            n++;
        }
    }
    if (write_text(dir, "gfx/pictures.txt", &t, err, errlen)) return -1;
    return write_preview(dir, "gfx/pictures.png", imgs, n, pal, err, errlen);
}

static int extract_font(const RomSet *rom, const char *dir, const uint8_t pal[16], char *err, int errlen)
{
    static Indexed glyphs[FONT_GLYPHS];
    Text t = {0};
    put(&t, "# The game's font, descriptors at ROM bank %d $%04X (extracted; do not edit)\n"
            "# glyph <character> <width> <height>, then <height> rows of palette indices.\n"
            "# The character is the one the ROM maps to the entry; '-' and '/' (and any\n"
            "# character without a glyph, drawn as '-') show the '?' shape, '@' is blank.\n",
            GFX_BANK, FONT_ADDR);
    for (int i = 0; i < FONT_GLYPHS; i++) {
        unsigned d = FONT_ADDR + (unsigned)i * 4;
        int wbytes = gfx_byte(rom, d), h = gfx_byte(rom, d + 1), data = gfx_word(rom, d + 2);
        if (wbytes < 1 || wbytes > 4 || h != 8 || data < 0 || read_picture(rom, (unsigned)data, wbytes, h, &glyphs[i])) {
            snprintf(err, (size_t)errlen, "font glyph %d ($%04X): unexpected descriptor", i, d);
            free(t.s);
            return -1;
        }
        put(&t, "glyph %c %d %d\n", font_chars[i], glyphs[i].w, glyphs[i].h);
        put_rows(&t, &glyphs[i]);
    }
    if (write_text(dir, "gfx/font.txt", &t, err, errlen)) return -1;
    return write_preview(dir, "gfx/font.png", glyphs, FONT_GLYPHS, pal, err, errlen);
}

/* ---- the attract mode and play, on the emulated board --------------------- */

static Board board;

static int screen_pixel(const Board *b, int x, int y)
{
    uint8_t v = b->ram[(x / 2) * 256 + y];
    return x & 1 ? v & 15 : v >> 4;
}

/* The bounding box of what is drawn in rows y0..y1 (screen coordinates). */
static int grab(const Board *b, const char *name, int y0, int y1, Indexed *out)
{
    int x0 = 1 << 30, x1 = -1, top = 1 << 30, bottom = -1;
    for (int y = y0; y <= y1; y++)
        for (int x = 0; x < WILLIAMS_SCREEN_SIZE / 256 * 2; x++)
            if (screen_pixel(b, x, y)) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < top) top = y;
                if (y > bottom) bottom = y;
            }
    if (x1 < 0 || x1 - x0 + 1 > 256 || bottom - top + 1 > 64) return -1;
    snprintf(out->name, sizeof out->name, "%s", name);
    out->w = x1 - x0 + 1;
    out->h = bottom - top + 1;
    for (int y = 0; y < out->h; y++)
        for (int x = 0; x < out->w; x++) out->px[y * out->w + x] = (uint8_t)screen_pixel(b, x0 + x, top + y);
    return 0;
}

static void boot(const RomSet *rom)
{
    static uint8_t cmos[BOARD_CMOS_SIZE];
    board_init(&board, rom, NULL);
    for (int i = 0; i < PRIME_FRAMES; i++) board_run_frame(&board, NULL);
    memcpy(cmos, board.cmos, sizeof cmos);
    board_init(&board, rom, cmos);
}

static int extract_screens(const RomSet *rom, const char *dir, uint8_t play_pal[16], char *err, int errlen)
{
    static Indexed logos[2];
    uint8_t logo_pal[16];
    boot(rom);
    for (int f = 1; f <= LOGO_FRAME; f++) board_run_frame(&board, NULL);
    memcpy(logo_pal, board.palette, sizeof logo_pal);
    if (grab(&board, "williams", 40, 85, &logos[0]) || grab(&board, "defender", 130, 180, &logos[1])) {
        snprintf(err, (size_t)errlen, "the attract mode did not draw the logos at frame %d", LOGO_FRAME);
        return -1;
    }
    /* then a coin and a 1-player start: the palette in play */
    for (int f = LOGO_FRAME + 1; f <= PLAY_FRAME; f++) {
        board.in2 = f > COIN_FRAME && f <= COIN_FRAME + 15 ? IN2_COIN1 : 0;
        board.in0 = f > START_FRAME && f <= START_FRAME + 15 ? IN0_START1 : 0;
        board_run_frame(&board, NULL);
    }
    if (!(board.ram[0xA0BF] & 0x20)) {
        snprintf(err, (size_t)errlen, "the game did not start by frame %d", PLAY_FRAME);
        return -1;
    }
    memcpy(play_pal, board.palette, 16);

    Text t = {0};
    put(&t, "# The Williams and DEFENDER logos as the attract mode draws them, frame %d\n"
            "# after power-on (extracted; do not edit). Their colours cycle; the palette\n"
            "# line is that frame's.\n"
            "palette", LOGO_FRAME);
    put_palette(&t, logo_pal);
    for (int i = 0; i < 2; i++) {
        put(&t, "image %s %d %d\n", logos[i].name, logos[i].w, logos[i].h);
        put_rows(&t, &logos[i]);
    }
    if (write_text(dir, "gfx/logos.txt", &t, err, errlen)) return -1;
    if (write_preview(dir, "gfx/williams.png", &logos[0], 1, logo_pal, err, errlen) ||
        write_preview(dir, "gfx/defender.png", &logos[1], 1, logo_pal, err, errlen))
        return -1;

    put(&t, "# The palette in play, frame %d after power-on, a coin and a 1-player start\n"
            "# (extracted; do not edit). One byte per index, BBGGGRRR; some entries cycle.\n"
            "palette", PLAY_FRAME);
    put_palette(&t, play_pal);
    return write_text(dir, "gfx/palette.txt", &t, err, errlen);
}

/* ---- sounds ---------------------------------------------------------------- */

static void le16(uint8_t *p, unsigned v)
{
    p[0] = (uint8_t)v;
    p[1] = (uint8_t)(v >> 8);
}

static void le32(uint8_t *p, unsigned long v)
{
    le16(p, (unsigned)(v & 0xFFFF));
    le16(p + 2, (unsigned)(v >> 16));
}

static int write_wav(const char *dir, const char *name, const int16_t *s, long n, char *err, int errlen)
{
    size_t size = 44 + (size_t)n * 2;
    uint8_t *w = malloc(size);
    if (!w) abort();
    memcpy(w, "RIFF", 4);
    le32(w + 4, (unsigned long)size - 8);
    memcpy(w + 8, "WAVEfmt ", 8);
    le32(w + 16, 16);
    le16(w + 20, 1); /* PCM */
    le16(w + 22, 1); /* mono */
    le32(w + 24, SOUND_RATE);
    le32(w + 28, SOUND_RATE * 2);
    le16(w + 32, 2);
    le16(w + 34, 16);
    memcpy(w + 36, "data", 4);
    le32(w + 40, (unsigned long)n * 2);
    for (long i = 0; i < n; i++) le16(w + 44 + i * 2, (unsigned)(uint16_t)s[i]);
    int rc = write_file(dir, name, w, size, err, errlen);
    free(w);
    return rc;
}

static Sound snd;
static int16_t samples[SOUND_RATE * SOUND_MAX_SECONDS + SOUND_MAX_SAMPLES];

static int extract_sounds(const char *dir, const RomSet *rom, char *err, int errlen)
{
    Text t = {0};
    put(&t, "# Sound-board commands, recorded from the emulated board (extracted; do not edit)\n"
            "# <command> <once|loop|none> <samples at %d Hz>\n", SOUND_RATE);
    for (int cmd = 0; cmd < SOUND_COMMANDS; cmd++) {
        sound_init(&snd, rom, SOUND_CPU_HZ / SOUND_RATE);
        sound_run(&snd, SOUND_BOOT_CYCLES);
        snd.out_len = 0;
        sound_command(&snd, (uint8_t)(0xC0 | cmd)); /* the top two bits are pulled up */
        long n = 0, last_change = -1;
        uint8_t dac = snd.dac;
        const long max = (long)SOUND_RATE * SOUND_MAX_SECONDS;
        while (n < max && (last_change < 0 ? n < SOUND_RATE * SOUND_QUIET_SECONDS
                                           : n - last_change < SOUND_RATE * SOUND_QUIET_SECONDS)) {
            sound_run(&snd, snd.cpu.cycles + SOUND_STEP_CYCLES);
            if (snd.out_len >= SOUND_MAX_SAMPLES / 2 || snd.dac != dac) {
                memcpy(samples + n, snd.out, (size_t)snd.out_len * sizeof snd.out[0]);
                n += snd.out_len;
                snd.out_len = 0;
            }
            if (snd.dac != dac) {
                dac = snd.dac;
                last_change = n;
            }
        }
        char name[32];
        snprintf(name, sizeof name, "sounds/%02X.wav", cmd);
        if (last_change < 0) {
            put(&t, "%02X none 0\n", cmd);
            continue;
        }
        const char *kind = "once";
        if (n >= max) {
            kind = "loop";
            n = (long)SOUND_RATE * SOUND_LOOP_SECONDS;
        } else {
            n = last_change + SOUND_TAIL;
        }
        if (write_wav(dir, name, samples, n, err, errlen)) {
            free(t.s);
            return -1;
        }
        put(&t, "%02X %s %ld\n", cmd, kind, n);
    }
    return write_text(dir, "sounds/sounds.txt", &t, err, errlen);
}

/* ---- all ------------------------------------------------------------------- */

int extract_all(const RomSet *rom, const char *out_dir, char *err, int errlen)
{
    char path[1100];
    if (!rom->loaded) {
        snprintf(err, (size_t)errlen, "no ROM set loaded");
        return -1;
    }
    snprintf(path, sizeof path, "%s/%s", out_dir, EXTRACT_MANIFEST);
    remove(path); /* until the end, the folder is incomplete */
    snprintf(path, sizeof path, "%s/gfx", out_dir);
    if (mkdir_p(path)) {
        snprintf(err, (size_t)errlen, "cannot create %s", path);
        return -1;
    }
    snprintf(path, sizeof path, "%s/sounds", out_dir);
    if (mkdir_p(path)) {
        snprintf(err, (size_t)errlen, "cannot create %s", path);
        return -1;
    }
    uint8_t play_pal[16];
    if (extract_screens(rom, out_dir, play_pal, err, errlen) ||
        extract_pictures(rom, out_dir, play_pal, err, errlen) ||
        extract_font(rom, out_dir, play_pal, err, errlen) ||
        extract_sounds(out_dir, rom, err, errlen))
        return -1;
    Text t = {0};
    put(&t, "defender assets %d\n", EXTRACT_VERSION);
    return write_text(out_dir, EXTRACT_MANIFEST, &t, err, errlen);
}
