/*
 * The ROM set loader against the user's defender set (label rom):
 *   test_rom <defender.zip or folder> <scratch dir>
 * Loads it as given, then from a folder of the same ROMs under other names
 * (matching is by CRC), then checks an empty folder fails with a message.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "assets/rom.h"

#ifdef _WIN32
#include <direct.h>
#define mkdir(dir, mode) _mkdir(dir)
#endif

static int failures;
#define CHECK(cond, ...)                                                    \
    do {                                                                    \
        if (!(cond)) {                                                      \
            failures++;                                                     \
            fprintf(stderr, "FAIL %s:%d: ", __FILE__, __LINE__);            \
            fprintf(stderr, __VA_ARGS__);                                   \
            fputc('\n', stderr);                                            \
        }                                                                   \
    } while (0)

static RomSet a, b;

static void write_rom(const char *dir, const char *name, const uint8_t *data, size_t len)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "wb");
    CHECK(f && fwrite(data, 1, len, f) == len, "cannot write %s", path);
    if (f) fclose(f);
}

int main(int argc, char **argv)
{
    if (argc != 3) {
        fprintf(stderr, "usage: test_rom <defender.zip|folder> <scratch dir>\n");
        return 2;
    }
    char err[512];
    CHECK(!rom_load(&a, argv[1], err, sizeof err), "load %s: %s", argv[1], err);
    CHECK(rom_byte(&a, 0xBFFF) == -1 && rom_byte(&a, 0xCFFF) == -1, "RAM and I/O addresses read as ROM");
    CHECK(rom_byte(&a, 0xD000) == a.fixed[0] && rom_byte(&a, 0xFFFF) == a.fixed[0x2FFF], "fixed ROM mapping");
    CHECK(rom_bank_byte(&a, 0, 0) == -1, "bank 0 (I/O) reads as ROM");
    CHECK(rom_bank_byte(&a, 7, 0xFFF) == a.bank[6][0xFFF] && rom_bank_byte(&a, 8, 0) == 0, "bank mapping");

    /* the same ROMs, renamed and split into 2 KB pieces in a folder (only
       defend.2 and defend.3 are 4 KB files; the rest are 2 KB halves) */
    mkdir(argv[2], 0755);
    char dir[1100];
    snprintf(dir, sizeof dir, "%s/renamed", argv[2]);
    mkdir(dir, 0755);
    write_rom(dir, "a.bin", a.fixed, 0x800);
    write_rom(dir, "b.bin", a.fixed + 0x800, 0x800);
    write_rom(dir, "c.bin", a.fixed + 0x1000, 0x1000);
    write_rom(dir, "d.bin", a.fixed + 0x2000, 0x1000);
    static const int banks[4] = { 0, 1, 2, 6 };
    for (int i = 0; i < 4; i++)
        for (int h = 0; h < (banks[i] == 6 ? 1 : 2); h++) {
            char name[32];
            snprintf(name, sizeof name, "chip%d%d.bin", (i * 3) % 4, h);
            write_rom(dir, name, a.bank[banks[i]] + h * 0x800, 0x800);
        }
    write_rom(dir, "noise.snd", a.sound, sizeof a.sound);
    uint8_t junk[0x800];
    memset(junk, 0x5A, sizeof junk);
    write_rom(dir, "unrelated.bin", junk, sizeof junk);
    CHECK(!rom_load(&b, dir, err, sizeof err), "load renamed folder: %s", err);
    CHECK(!memcmp(a.fixed, b.fixed, sizeof a.fixed) && !memcmp(a.bank, b.bank, sizeof a.bank) &&
              !memcmp(a.sound, b.sound, sizeof a.sound),
          "renamed folder loaded different ROMs");

    snprintf(dir, sizeof dir, "%s/empty", argv[2]);
    mkdir(dir, 0755);
    err[0] = '\0';
    CHECK(rom_load(&b, dir, err, sizeof err) == -1 && strstr(err, "missing defend.1"),
          "empty folder: rc/err '%s'", err);
    CHECK(!b.loaded && rom_byte(&b, 0xD000) == -1, "failed load left the set usable");

    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    puts("rom: all checks passed");
    return 0;
}
