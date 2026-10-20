#include "assets/rom.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "assets/zip.h"

/* MAME `defender` (0.289): name, CRC32, size, and where the file goes. */
#define ROM_FILES 12
static const struct {
    const char *mame;
    uint32_t crc;
    unsigned size;
    int bank;        /* 0: the fixed ROM, 1-7: a bank, -1: the sound ROM */
    unsigned offset; /* within the fixed ROM ($D000 = 0) or the bank */
} files[ROM_FILES] = {
    { "defend.1", 0xc3e52d7e, 0x800, 0, 0x0000 },
    { "defend.4", 0x9a72348b, 0x800, 0, 0x0800 },
    { "defend.2", 0x89b75984, 0x1000, 0, 0x1000 },
    { "defend.3", 0x94f51e9b, 0x1000, 0, 0x2000 },
    { "defend.9", 0x6870e8a5, 0x800, 1, 0x000 },
    { "defend.12", 0xf1f88938, 0x800, 1, 0x800 },
    { "defend.8", 0xb649e306, 0x800, 2, 0x000 },
    { "defend.11", 0x9deaf6d9, 0x800, 2, 0x800 },
    { "defend.7", 0x339e092e, 0x800, 3, 0x000 },
    { "defend.10", 0xa543b167, 0x800, 3, 0x800 },
    { "defend.6", 0x65f4efd1, 0x800, 7, 0x000 },
    { "video_sound_rom_1.ic12", 0xfefd5b48, 0x800, -1, 0x000 },
};
#define MAX_FILE_SIZE 0x1000

static uint32_t crc32(const uint8_t *p, size_t n)
{
    uint32_t c = 0xFFFFFFFFu;
    while (n--) {
        c ^= *p++;
        for (int k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320u ^ (c >> 1) : c >> 1;
    }
    return c ^ 0xFFFFFFFFu;
}

static uint8_t *slot(RomSet *rom, int i)
{
    if (files[i].bank < 0) return rom->sound + files[i].offset;
    if (files[i].bank == 0) return rom->fixed + files[i].offset;
    return rom->bank[files[i].bank - 1] + files[i].offset;
}

/* Every 2 or 4 KB file in a folder, kept where its CRC matches a set file. */
static void scan_folder(RomSet *rom, const char *dir, int *found)
{
    DIR *d = opendir(dir);
    if (!d) return;
    struct dirent *e;
    uint8_t buf[MAX_FILE_SIZE];
    while ((e = readdir(d))) {
        char path[1100];
        snprintf(path, sizeof path, "%s/%s", dir, e->d_name);
        struct stat st;
        if (stat(path, &st) || !S_ISREG(st.st_mode) || (st.st_size != 0x800 && st.st_size != 0x1000)) continue;
        FILE *f = fopen(path, "rb");
        if (!f) continue;
        size_t got = fread(buf, 1, (size_t)st.st_size, f);
        fclose(f);
        if (got != (size_t)st.st_size) continue;
        uint32_t crc = crc32(buf, got);
        for (int i = 0; i < ROM_FILES; i++)
            if (!found[i] && crc == files[i].crc && got == files[i].size) {
                memcpy(slot(rom, i), buf, got);
                found[i] = 1;
            }
    }
    closedir(d);
}

int rom_load(RomSet *rom, const char *src, char *err, int errlen)
{
    memset(rom, 0, sizeof *rom);
    struct stat st;
    if (stat(src, &st) != 0) {
        snprintf(err, (size_t)errlen, "%s: not found", src);
        return -1;
    }
    int found[ROM_FILES] = { 0 };
    if (S_ISREG(st.st_mode)) {
        for (int i = 0; i < ROM_FILES; i++) {
            uint8_t *data;
            size_t len;
            int rc = zip_read_crc(src, files[i].crc, &data, &len, err, errlen);
            if (rc == -1) return -1;
            if (rc == 0) {
                if (len == files[i].size) {
                    memcpy(slot(rom, i), data, len);
                    found[i] = 1;
                }
                free(data);
            }
        }
    } else {
        scan_folder(rom, src, found);
    }
    for (int i = 0; i < ROM_FILES; i++)
        if (!found[i]) {
            snprintf(err, (size_t)errlen, "%s: missing %s (CRC %08x); the MAME 'defender' set is required", src,
                     files[i].mame, (unsigned)files[i].crc);
            memset(rom, 0, sizeof *rom);
            return -1;
        }
    rom->loaded = 1;
    return 0;
}

int rom_byte(const RomSet *rom, unsigned addr)
{
    if (!rom->loaded || addr < 0xD000 || addr > 0xFFFF) return -1;
    return rom->fixed[addr - 0xD000];
}

int rom_bank_byte(const RomSet *rom, int bank, unsigned offset)
{
    if (!rom->loaded || bank <= 0 || bank > 15 || offset >= ROM_BANK_SIZE) return -1;
    return bank <= ROM_BANKS ? rom->bank[bank - 1][offset] : 0;
}

/* want_dir: a folder; else a regular file */
static int exists(const char *path, int want_dir)
{
    struct stat st;
    return stat(path, &st) == 0 && (want_dir ? S_ISDIR(st.st_mode) : S_ISREG(st.st_mode));
}

int rom_locate(const char *argv0, char *out, size_t outlen)
{
    const char *env = getenv("DEFENDER_ROM");
    if (env && *env && (exists(env, 0) || exists(env, 1))) {
        snprintf(out, outlen, "%s", env);
        return 0;
    }
    char exe_dir[900] = ".";
    if (argv0) {
        snprintf(exe_dir, sizeof exe_dir, "%s", argv0);
        char *slash = strrchr(exe_dir, '/');
#ifdef _WIN32
        char *bslash = strrchr(exe_dir, '\\');
        if (bslash > slash) slash = bslash;
#endif
        if (slash) *slash = '\0';
        else snprintf(exe_dir, sizeof exe_dir, ".");
    }
    const char *const dirs[2] = { exe_dir, "." };
    /* the folder must be a folder: the program itself is called defender too */
    const char *const names[2] = { "defender.zip", "defender" };
    for (int d = 0; d < 2; d++)
        for (int n = 0; n < 2; n++) {
            snprintf(out, outlen, "%s/%s", dirs[d], names[n]);
            if (exists(out, n == 1)) return 0;
        }
#ifdef DEFENDER_SOURCE_DIR
    for (int n = 0; n < 2; n++) { /* development builds: the source tree's roms/ */
        snprintf(out, outlen, "%s/roms/%s", DEFENDER_SOURCE_DIR, names[n]);
        if (exists(out, n == 1)) return 0;
    }
#endif
    out[0] = '\0';
    return -1;
}
