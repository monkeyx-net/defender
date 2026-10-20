# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

A native C11 / SDL2 port of Defender (Williams, 1981) for Linux handhelds
(aarch64: ArkOS, ROCKNIX, muOS, Knulli), x86_64 (Steam Deck, desktops),
Windows and the web. **The plan, decisions and test plan live in
[DEFENDER_PORT_PLAN.md](DEFENDER_PORT_PLAN.md)**; read it before larger changes and
keep its phase table up to date.

The repo started as a copy of the sibling Joust port (`../joust`), whose
platform layer, loaders, framebuffer and enhanced graphics came from the
Robotron port (`../robotron`, branch `sdl2`). Improvements to that shared
code may be worth carrying across (the plan lists some), but ask first.

## Ground Rules

- **The game is the ROM's own code** on the emulated board (`src/emu/`, plan
  decision 6). MAME is the reference for the emulation: changes to `emu/`
  must keep `mame_emu` (and the other `mame` tests) passing. Write all
  emulation from the hardware's behaviour, not from MAME's source.
- **No ROM-derived data in the repo or the binary** (plan, decision 1). Not
  sprites, fonts, tables or sounds: the program reads the player's ROM set at
  run time.
- **All new code and tooling is C** (built with CMake). The one exception is
  MAME probe scripts, which must be Lua (MAME's scripting language); they only
  dump to text files, and C or CMake does the comparing.
- New files go in `src/`, `CMakeLists.txt`, `.github/` or `DEFENDER_PORT_PLAN.md`.
  Ask before adding files elsewhere.
- The user's ROM set lives in `roms/` (git-ignored, here a symlink); never
  commit ROMs or zips, and never send them anywhere.

## Commands

```bash
cmake -S . -B build && cmake --build build -j     # build
ctest --test-dir build                            # all tests
ctest --test-dir build -L rom                     # tests needing the ROM set (roms/defender.zip)
ctest --test-dir build -L mame                    # tests needing MAME + the ROM set
build/defender --windowed                         # play (finds roms/defender.zip in dev builds)
build/defender --windowed --test-card             # the test card (ROM status, input tester)
build/defender --windowed --gfx hd-glow           # enhanced graphics (presets: classic ... hd-glow-crt)
SDL_VIDEODRIVER=dummy build/defender --frames 60 --screenshot out.png   # headless screenshot
build/defender --check-rom roms/defender.zip      # verify a ROM set
build/defender --dump-rom roms/defender.zip C000 FFFF   # ROM bytes: "B:ADDR BYTE" per bank, then "ADDR BYTE"
cmake --build build --target package_linux package_portmaster   # release archives
cmake -S . -B build-san -DDEFENDER_SANITIZE=ON    # ASan/UBSan build
emcmake cmake -S . -B build-web && cmake --build build-web --target serve   # web build on http://localhost:8000/
```

Release builds: `-DDEFENDER_DEV_ASSETS=OFF` (no source-tree fallbacks).

## Layout (`src/`)

- `main.c` - SDL setup, fixed 60 Hz loop running one board frame per tick,
  the graphics pipeline, CMOS saving, the test card (`--test-card`),
  `--input`, `--frames`, `--screenshot`, `--rom`, `--check-rom`, `--dump-rom`
- `emu/` - the emulated hardware: `m6809.c` (main CPU), `pia6821.c`,
  `board.c` (Defender's main board: memory map with ROM banks and the I/O
  page, interrupts, CMOS, video counter, video per line), `m6800.c` +
  `sound.c` (the sound board and its DAC)
- `platform/` - the only code that includes SDL: `video.c` (292x240 framebuffer
  at any scale, integer scaling, scanline overlay), `input.c` (Defender's
  control panel; pads and keyboard), `audio.c` (streams the board's sound)
- `gfx/` - draws into the framebuffer in arcade coordinates (`fb.scale` > 1 for
  HD): `fb.c`, `blit.c`, `scale.c` (Scale2x/3x, xBR), `enhance.c` (graphics
  presets, HD image registry, whole-frame filter, glow), `debugfont.c` (3x5
  font for diagnostics, not the ROM's), `williams.c` (video RAM and palette
  decoding)
- `assets/` - `savefile.c` (the save folder: defender.cmos), `rom.c` (the
  defender set from a zip or folder, matched by CRC; fixed ROM and banks),
  `zip.c`, `inflate.c`, `png.c` (RGB and RGBA), `assets.c` (asset folder lookup)
- `tests/` - C tests, MAME probes (`tests/mame/`)
- `packaging/` - release layout script, PortMaster launcher and metadata
- `web/` - page shell for the Emscripten build

## Conventions

- C11, `-Wall -Wextra`, no warnings. Builds must also pass in Ubuntu 20.04
  (GCC 9, SDL 2.0.10, the PortMaster base): use only the SDL 2.0.10 API.
- The board is deterministic: one frame per tick, no wall clock.
- Enhanced graphics are cosmetic: classic output stays the arcade frame.
- Match the surrounding code: comment density, naming, idiom.

## Testing

Every change keeps `ctest` green.

- `enhance` - always runs (scalers, presets, glow)
- `rom_load` - label `rom`: loads the set from the zip, from renamed copies
  in a folder (CRC matching), and fails cleanly on an empty folder
- `mame_rom_map` - label `mame`: MAME's view of the main CPU's ROM (each
  bank at $C000, then $D000-$FFFF) equals `defender --dump-rom`
- `mame_capture` - label `mame`: runs the scripted game in
  `src/tests/mame/script.cmake` (coin, start, a minute of play), dumping RAM,
  palette, bank and sound RAM with a snapshot at 12 frames; the attract-mode
  frames decoded by `gfx/williams.c` equal MAME's snapshots pixel for pixel
- `mame_emu` - label `mame`: the emulated board replays that script from
  power-on with the primed CMOS. MAME's instruction traces of frames
  1600-1700 (`trace_probe.lua`): the main CPU cycle for cycle, the sound CPU
  instruction for instruction between interrupts (MAME delivers sound
  commands with jitter). At every captured frame RAM (bar the stack page),
  palette and bank equal MAME's, and the picture pixel for pixel. Inputs a
  probe sets in a frame callback reach the game at the start of the frame
  after next. MAME's frame callbacks run a few instructions off the frame
  boundary: dumps there are fuzzy, traces are exact.
- To debug a difference, compare instruction traces: `trace_probe.lua` with
  `TRACE_FROM`/`TRACE_TO` (needs `-debug -debugger none`; MAME stamps only
  the main CPU's lines with `totalcycles`) against `Board.step_hook` and
  `Sound.step_hook`; then `access_probe.lua` (which code reads and writes
  given addresses) or a read/write tap to find the data that differs. MAME's
  frames start at line 247, reset takes 4 cycles.
- Probes that are not tests: `ram_probe.lua` (work RAM at chosen frames, with
  scripted input and cheat pokes), `dasm_probe.lua` (MAME's disassembly to
  `build/re`; never commit it, nor traces). `capture_probe.lua` also takes
  `PROBE_CYCLE` (repeating input) and `PROBE_POKE` / `PROBE_POKE_FROM`.
- MAME tests share `build/mame` (one at a time) and start each run from a
  fresh copy of the primed CMOS (`src/tests/mame/mame_common.cmake`); MAME
  needs `-frameskip 0` for snapshots to show the frame they are taken in,
  and takes an extra snapshot when `-seconds_to_run` ends
- Tests that run the game use their own save dir; never write to the user's
  home folder.

## Reference

- ROM: MAME `defender` (red label, 0.289 names); CRCs in `src/assets/rom.c`.
- Hardware: Williams first generation without the blitter (6809 main CPU,
  banked ROM, 6808 sound CPU, 16-colour palette). Notes go in the plan as
  they are confirmed.
- MAME 0.289 runs it headless; see `src/tests/mame/mame_common.cmake` for the options.
