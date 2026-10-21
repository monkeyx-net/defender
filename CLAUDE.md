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

- **The game is being rewritten in C** (plan decision 6), as the Robotron
  port's was: game logic from the ROM's behaviour, checked frame by frame
  against the emulated board (`src/emu/`). Until it reaches parity the
  emulated board runs the game. MAME is the reference for the emulation:
  changes to `emu/` must keep `mame_emu` (and the other `mame` tests)
  passing. Write all emulation from the hardware's behaviour, not from
  MAME's source.
- **The ROM set is read once** (plan decision 1): `defender --extract`
  writes pictures, font, logos, palette and sounds (and later tables) to
  `assets/`, which the C game reads instead of the ROM. No ROM-derived data
  in the repo, the binary or the packages: not sprites, fonts, tables or
  sounds; the extractor's tables name ROM addresses only.
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
build/defender --extract roms/defender.zip [dir]  # assets from the ROM set (dev builds: build/assets after each build)
cmake --build build --target package_linux package_portmaster   # release archives
cmake -S . -B build-san -DDEFENDER_SANITIZE=ON    # ASan/UBSan build
emcmake cmake -S . -B build-web && cmake --build build-web --target serve   # web build on http://localhost:8000/
```

Release builds: `-DDEFENDER_DEV_ASSETS=OFF` (no source-tree fallbacks).

## Layout (`src/`)

- `main.c` - SDL setup, fixed 60 Hz loop running one board frame per tick,
  the graphics pipeline, CMOS saving, the controls page over the attract
  mode (after 30 s idle; `$A0BF` bit 5 tells a game from the attract mode),
  the test card (`--test-card`),
  `--input`, `--frames`, `--screenshot`, `--rom`, `--check-rom`, `--dump-rom`,
  `--extract`
- `emu/` - the emulated hardware: `m6809.c` (main CPU), `pia6821.c`,
  `board.c` (Defender's main board: memory map with ROM banks and the I/O
  page, interrupts, CMOS, video counter, video per line), `m6800.c` +
  `sound.c` (the sound board and its DAC)
- `platform/` - the only code that includes SDL: `video.c` (292x240 framebuffer
  at any scale, integer scaling, scanline overlay), `input.c` (Defender's
  control panel; pads and keyboard), `audio.c` (streams the board's sound)
- `sim/` - the game in C, piece by piece (plan decision 6): `rng.c` (the
  random generator, $D711), `ship.c` (the ship's motion and scrolling,
  $E263), `laser.c` (the laser, drawn into arcade-layout video RAM),
  `sounds.c` (the sound sequencer: scripts, thrust sound), `objects.c`
  (the object pool and lists, in the ROM's layout and values),
  `processes.c` (the process pool: create, sleep, kill, the dispatcher's
  countdown), `game.c` (the C game's state: a model of all of RAM, the
  pools typed, the rest raw; `game_step` runs a process step in C),
  `lander.c` (landers and mutants, every step), `shots.c` (enemy shots),
  `humanoids.c` (the humanoids: walking, hit, falling, landing, carried,
  the bonuses),
  `explosions.c` (starting one, an object appearing), `wave.c` (the
  wave's process: arrivals, baiters, the ramp), `baiter.c` (baiters), `pods.c` (pods and
  swarmers), `bombers.c` (bombers), `background.c` (the
  processes running all game: colour cycles, list sorting, shots ageing,
  the scanner), `player.c` (the ship's collisions, smart bomb, reverse,
  HUD icons, saving a wave), `irq.c` (the interrupt's drawing: objects, ship, stars, shots),
  `terrain.c` (bank 7's terrain: heights, buffers, drawing), `frame.c`
  (a frame: the interrupt's halves, the main loop's tick, the controls), `hit.c` (the hit test), `enemies.c` (hit
  handlers, kills), `score.c` (scoring and the score digits); `laser.c`
  also runs the laser as a process step;
  each mirrors a ROM routine's arithmetic exactly
- `gfx/` - draws into the framebuffer in arcade coordinates (`fb.scale` > 1 for
  HD): `fb.c`, `blit.c`, `scale.c` (Scale2x/3x, xBR), `enhance.c` (graphics
  presets, HD image registry, whole-frame filter, glow), `debugfont.c` (3x5
  font for diagnostics, not the ROM's), `williams.c` (video RAM and palette
  decoding)
- `assets/` - `savefile.c` (the save folder: defender.cmos), `rom.c` (the
  defender set from a zip or folder, matched by CRC; fixed ROM and banks),
  `zip.c`, `inflate.c`, `png.c` (RGB and RGBA), `assets.c` (asset folder lookup),
  `extract.c` (the assets from the ROM set: pictures, font, logos and the
  palette from the ROM and the emulated attract mode, WAVs from the
  emulated sound board, the sound scripts; `manifest.txt` written last)
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
- `extract` - label `rom`: two extractions are byte-identical; every file
  is there (31 pictures, 45 glyphs, 2 logos, 31 sound commands, 26 sound
  scripts); the font
  matches what the attract mode draws on the emulated board; no manifest
  without a ROM set
- `sim` - label `rom`: plays a scripted game on the emulated board and
  checks each `sim/` routine against the ROM's at every call: state, and
  the screen bytes it writes. For shared state (the object and process pools) writes
  from unported code are copied to the C model and ported routines run in
  C; compare when the routine returns (return address and stack pointer),
  not when the PC leaves it (an interrupt). Otherwise (writes from
  code not yet ported resynchronise it; the board calls the step hook also
  before an instruction the CPU then puts off for an interrupt: ignore
  calls with an IRQ or FIRQ pending and unmasked)
- `game` - label `rom`: a C model of all of RAM follows the board's
  scripted game (writes copied); ported process steps run in C and the
  model must equal RAM when the ROM's step yields (a step an interrupt
  broke into must match with the interrupt's writes all before or all
  after it, or replayed where they fell among its raw writes; a race the
  C game cannot have, the ROM reading partly before the interrupt, is
  counted and must stay under 1 in 1000). Main-loop calls ($E861) are
  followed like steps, until they return. Ported
  interrupt routines are checked per call: a model loaded from RAM runs
  the C routine and must equal RAM when the ROM's returns (bar <$77-<$78
  and <$13-<$14, where routines keep S). The scratch byte `<$73` and the like are RAM
  too: port the stores to them. The model's `data` is the fixed ROM with
  bank 2 (pictures, font) at $C000; `bank[]` holds banks 1, 2, 3 and 7
  for the tables the code reads with that bank selected.
- `game_hunter` - label `rom`: `game` on a second scripted game (rapid
  fire, sweeping up and down, turning; 12000 frames)
- `routines` - label `rom`: ROM routines run on the emulated CPU from
  chosen RAM and registers until they return, against the C ones on the
  same RAM (`score_add`, 5000 random cases), and process steps from a
  state built in the model (copied to RAM) until the ROM's step is back at
  the dispatcher (the humanoids' steps); use it where scripted play rarely
  reaches a path, and aim cases at the edges (thresholds, clamps)
- `frame` - label `rom`: the C game on its own (`sim/frame.h`) against the
  board, compared after every frame; the board's band split and where its
  bottom half came in the tick are given; runs are short (timing), at
  least 1000 frames must match
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
  `build/re`, `DASM_EXTRA` for listings from a chosen start where data
  misaligns a bank's; never commit it, nor traces). `capture_probe.lua` also takes
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
