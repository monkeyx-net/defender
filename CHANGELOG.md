# Defender — native SDL2 port — Changelog

---
## Changelog

### v0.2.1 (2026-10-23)

#### Port

- **Lower CPU use** — the main loop sleeps until the next frame is due
  instead of redrawing and presenting again (no spinning where vsync does
  not block, no extra frames on 120/144 Hz displays); the graphics
  presets' scalers (Scale2x/3x, xBR) read their neighbours without bounds
  checks inside the picture, write flat areas at once and skip the
  premultiply pass for the opaque game frame. Pictures unchanged, bit for
  bit.

### v0.2.0 (2026-10-22)

#### Game

- **The game in C by default** — the ROM program rewritten in C
  (`src/sim/`), routine by routine, each mirroring the ROM's arithmetic and
  order of stores: the random generator, the ship's motion and scrolling,
  the laser and its hit test, the sound sequencer, the object and process
  pools, landers and mutants (with the capture), humanoids (walking, hit,
  falling, landing, carried, the bonuses), baiters, pods and swarmers,
  bombers, enemy shots, explosions, the wave's process and arrivals, the
  background processes (colour cycles, list sorting, shots ageing, the
  scanner), the smart bomb, reverse and HUD icons, the ship's collisions,
  hyperspace, deaths, a wave's end and game over, scoring and extra lives,
  coins, credits and starts, a player's turn and the text printer, the
  attract mode (high-score entry, hall of fame, demo, logos) and power-on
  after the self-test. `--emulate` still runs the ROM on the emulated
  board; the operator pages still run on the board, handed the C game's RAM
  and CMOS.
- **The frame in C** — the interrupt's halves (objects, ship and flame,
  stars, shots, the terrain of bank 7), the main loop's tick, the controls;
  the C game plays on its own from power-on, drawn from its RAM, with the
  extracted sounds.
- **Operator switches** — F2 (Advance) in the C game; F6 is the High Score
  Reset switch, its process in C.

#### Data & ROM

- **The extractor** — `defender --extract` writes the pictures, font,
  logos, palette, sounds and sound scripts from the ROM set to `assets/`
  (dev builds extract after each build). The C game's tables still come
  from the ROM set.

#### Graphics & Sound

- **Faster picture** — video RAM decoded by columns, each byte's two
  colours from a colour-pair table.

#### Build & Platforms

- **macOS build** — Defender builds on macOS with Homebrew's SDL2
  (`brew install sdl2 pkg-config`, then the usual CMake build); saves go to
  `$XDG_DATA_HOME/defender` or `~/.local/share/defender`, as on Linux.
- **macOS package** — new `package_macos` target writes
  `defender-<version>-macos-<arch>.tar.gz` (the program,
  `README-native.txt`, `LICENSE` and `NOTICE`; no ROM data, players add
  their own `defender.zip`).
- **CI** — the version now comes from this changelog's first heading and
  must match `CMakeLists.txt`; a `build-macos` job on GitHub's arm64 macOS
  runners builds with ccache, runs the tests, makes the arm64 tarball and
  adds it to the release beside the Linux, PortMaster, Windows and web
  archives.

#### Tests

- **The C game against the board** — `sim` checks each ported routine at
  every call; `game` and `game_hunter` follow two scripted games process
  step by process step over a model of all of RAM; `routines` runs ROM
  routines against the C ones from chosen states (5000 random scores, 200
  wave ends and deaths, the attract mode); `frame` runs the C game alone
  against the board frame by frame, checks its power-on, and plays 20000
  frames without reaching unported code; `extract` checks the extractor.

### v0.1.0 (2026-10-20)

#### Game

- **Playable on the emulated board** — a 6809 core and Defender's Williams
  board (ROM banking via $D000, the I/O page, interrupts, CMOS, video
  counter, video per line) run the ROM's code, matching MAME; the sound
  board (6808, PIA, DAC) runs beside it.
- **Controls and saves** — Defender's control panel on pads and keyboard;
  the CMOS (settings, high scores) saved to `defender.cmos`.
- **Controls page** — after 30 s without input in the attract mode a page
  lists the pad and keyboard controls.

#### Data & ROM

- **ROM set loader** — the MAME `defender` set (fixed ROM, banks, sound
  ROM) from a zip or folder, matched by CRC; `--check-rom`, `--dump-rom`.

#### Graphics & Sound

- **Platform layer** — from the Joust and Robotron ports: framebuffer at any
  scale, integer scaling, scanline overlay, enhanced graphics presets
  (Scale2x/3x, xBR, glow, CRT), audio streamed from the sound board.
- **Test card** — ROM status, input tester and graphics presets
  (`--test-card`).

#### Build & Platforms

- **Linux, Windows and web builds** — CMake builds for Linux (x86_64 and
  aarch64 handhelds), Windows (MinGW-w64) and the web (Emscripten).
- **Release packages** — `package_linux`, `package_portmaster` and
  `package_windows`.
- **CI workflow** — `.github/workflows/build.yml` builds Linux, Windows and
  web on every push to `main`, runs the tests and publishes a GitHub release
  when the version is new.

#### Tests

- **MAME reference tests** — the ROM map, video RAM and palette captures,
  and the emulated board from power-on through a scripted minute of play
  (main CPU cycle for cycle, sound CPU instruction for instruction; RAM and
  pictures at 12 frames) against MAME.

#### Documentation

- **README, CLAUDE.md and DEFENDER_PORT_PLAN.md** — every key and pad
  button, build options, tests and CI; the port's ground rules and plan.
