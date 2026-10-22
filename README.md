# Defender - native SDL2 port (work in progress)

A C11 / SDL2 port of the 1981 Williams arcade game for Linux handhelds
(PortMaster: ArkOS, ROCKNIX, muOS, Knulli), desktops (Linux, Windows) and the
web, checked against the original ROM running in MAME.

**Status:** playable. The game is rewritten in C, like the Robotron port,
checked routine by routine against the ROM running on an emulated Williams
board (6809 main board with banked ROM, 6808 sound board; itself checked
against MAME instruction for instruction); `--emulate` runs the ROM's own
code on that board instead. The operator pages are not in C yet: the
board runs them. The port adds enhanced graphics presets, pad controls,
CMOS saving and packaging for handhelds, desktops and the web. See
[DEFENDER_PORT_PLAN.md](DEFENDER_PORT_PLAN.md). Next: the C game reading
only the assets `--extract` writes from your ROM set once (its tables still
come from the ROM set).

You need your own MAME `defender` ROM set (`defender.zip`, red label); none is included.

Converted by monkeyx.

```bash
cmake -S . -B build && cmake --build build -j
build/defender --windowed --rom /path/to/defender.zip
```

## Controls

One control panel, as on the upright cabinet: the players take turns, and
every connected pad drives the panel.

| Action | Pad | Keyboard |
|---|---|---|
| Up / down | left stick or D-pad | Up Down |
| Fire | A | Space or Ctrl |
| Thrust | B or R1 | X or Alt |
| Reverse | L1 | Z |
| Smart bomb | X | C |
| Hyperspace | Y | V or Shift |
| Coin | Select | 5 |
| 1-player start | Start | 1 or Enter |
| 2-player start | L1 + Start | 2 |
| Operator menu (the cabinet's Advance button) | Select + R1 | F2 |
| Next graphics preset | Select + L1 | F3 |
| Glow on / off | | F4 |
| CRT scanlines on / off | | F5 |
| Quit | hold Select + Start for 1 s | Esc |

In the operator pages (bookkeeping, adjustments, tests) the operator menu
button steps to the next page and up / down change the value shown (the
cabinet's High Score Reset switch, with the Auto Up toggle on / off); the
pages end in a restart.

In Select or Start combinations, R1 and L1 do not thrust or reverse. In the
attract mode, after 30 seconds without input, a page lists these controls
(for 10 seconds, every 40 seconds; any input hides it).

Pads use SDL's game controller database; set `DEFENDER_GAMECONTROLLERDB` to
a `gamecontrollerdb.txt` to add mappings.

## Running

```bash
build/defender [--windowed] [--gfx preset] [--rom path] [--test-card] [--emulate]
               [--input script.txt] [--frames N] [--screenshot out.png]
build/defender --check-rom roms/defender.zip            # verify a ROM set
build/defender --dump-rom roms/defender.zip C000 FFFF   # ROM bytes: "B:ADDR BYTE" per bank, then "ADDR BYTE"
build/defender --extract roms/defender.zip [dir]  # pictures, font, logos, sounds into assets/
```

- `--gfx`: `classic` (default), `classic-crt`, `crisp`, `smooth`, `hd-crisp`,
  `hd-smooth`, `hd-glow`, `hd-glow-crt` (in the game the `hd-` presets
  upscale the whole picture, like `crisp` / `smooth`).
- `--test-card`: ROM status, an input tester and the presets.
- `--emulate`: the ROM's own code on the emulated board, not the game in C.
- `--input`, `--frames`, `--screenshot`: scripted, headless runs
  (`SDL_VIDEODRIVER=dummy`); see the comment at the top of `src/main.c`.

The ROM set is found from `--rom`, else `$DEFENDER_ROM`, else `defender.zip`
or a `defender/` folder next to the program or in the current folder
(development builds also look in the source tree's `roms/`). Files are
matched by CRC, so MAME, older MAME and FBNeo sets all work.

The arcade's CMOS (settings, high scores) is saved as `defender.cmos` in
`$DEFENDER_SAVE_DIR`, else `$XDG_DATA_HOME/defender`, `%APPDATA%\defender`
on Windows, or `~/.local/share/defender`. On the first start (no CMOS yet)
the game restores its factory settings before the window opens.

## Building

Needs CMake 3.13+, a C11 compiler and the SDL2 development files (2.0.10 or
newer; releases build against 2.0.10 in Ubuntu 20.04).

### Linux desktops and the Steam Deck

```bash
sudo apt install build-essential cmake pkg-config libsdl2-dev   # Debian / Ubuntu
cmake -S . -B build && cmake --build build -j
ctest --test-dir build
```

On aarch64 handhelds build natively on the device or in an aarch64 Ubuntu
20.04 container (as CI does) so the binary runs on their older glibc.

### CMake options

| Option | Default | Effect |
|---|---|---|
| `CMAKE_BUILD_TYPE` | `Release` | the usual CMake build types |
| `DEFENDER_DEV_ASSETS` | `ON` | development builds fall back to the source tree (`roms/`, `assets/`); set `OFF` for releases |
| `DEFENDER_SANITIZE` | `OFF` | build with ASan and UBSan |
| `DEFENDER_ROM` | `roms/defender.zip` (or `roms/defender/`) | the ROM set the `rom` and `mame` tests use |
| `DEFENDER_WEB_ROM` | empty | web only: bundle this `defender.zip` into the page, for local testing only (never publish such a build) |
| `DEFENDER_WEB_PORT` | `8000` | web only: port of the `serve` target |

```bash
cmake -S . -B build-san -DDEFENDER_SANITIZE=ON && cmake --build build-san -j   # sanitizers
```

### Windows (MinGW-w64 in MSYS2)

From the MSYS2 MINGW64 shell:

```bash
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
          mingw-w64-x86_64-pkgconf mingw-w64-x86_64-SDL2
cmake -S . -B build -G Ninja -DDEFENDER_DEV_ASSETS=OFF
cmake --build build --target defender
cp /mingw64/bin/SDL2.dll build/          # the package takes every DLL next to the exe
cmake --build build --target package_windows
```

The tests use POSIX calls, so on Windows only the game is built.

### Web (Emscripten)

With the [Emscripten SDK](https://emscripten.org/docs/getting_started/downloads.html)
active:

```bash
emcmake cmake -S . -B build-web
cmake --build build-web --target serve    # builds, then serves http://localhost:8000/
```

The page is `build-web/web/index.{html,js,wasm}`. No ROM is bundled, so a
plain web build shows the test card; for local play only, add your own set
with `-DDEFENDER_WEB_ROM=/path/to/defender.zip` (do not publish that build).
CMOS saving is not persisted in the browser yet.

### Release packages

```bash
cmake -S . -B build -DDEFENDER_DEV_ASSETS=OFF && cmake --build build -j
cmake --build build --target package_linux package_portmaster
```

Writes `build/packages/defender-<version>-linux-<arch>.tar.gz` and
`defender-<version>-portmaster-<arch>.zip` (the PortMaster launcher
`Defender.sh` and a `defender/` folder; players add their `defender.zip`
there). Packages hold the program, `README-native.txt`, `LICENSE` and
`NOTICE`, never ROM data.

## Tests

```bash
ctest --test-dir build              # everything available
ctest --test-dir build -L rom       # needs the ROM set (roms/defender.zip)
ctest --test-dir build -L mame      # needs MAME 0.289 and the ROM set
```

- `enhance` (always): scalers, presets, glow.
- `rom_load` (`rom`): the loader, from the zip and from renamed files.
- `extract` (`rom`): the extractor; repeatable, complete, and its font
  matches what the emulated attract mode draws.
- `mame_rom_map` (`mame`): every ROM bank and the fixed ROM as MAME maps them.
- `mame_capture` (`mame`): video and palette decoding against MAME snapshots.
- `mame_emu` (`mame`): the emulated board from power-on through a scripted
  minute of play against MAME's instruction traces (main CPU cycle for
  cycle, sound CPU instruction for instruction), and RAM and pictures at 12
  frames.

## Continuous integration

`.github/workflows/build.yml` runs on every push and pull request to `main`,
and by hand (Actions > Build > Run workflow):

1. **Version:** reads `project(defender VERSION x.y.z)` from `CMakeLists.txt`.
   On a push, if the release `vx.y.z` already exists, the builds are skipped.
2. **Linux x86_64 and aarch64:** in an Ubuntu 20.04 container (GCC 9,
   SDL 2.0.10, glibc 2.31 for PortMaster) on GitHub's x86_64 and arm64
   runners, with ccache: build with `DEFENDER_DEV_ASSETS=OFF`, run `ctest`
   (CI has no ROM set, so only `enhance` runs), make the Linux and
   PortMaster archives.
3. **Windows x86_64:** MSYS2 MinGW64 with Ninja; builds the game, copies the
   MinGW DLLs it links (SDL2.dll) next to it and makes the Windows zip.
4. **Web:** Emscripten; zips `build-web/web` as `defender-<version>-web.zip`
   (no ROM: the page shows the test card until a ROM is added).
5. **Release:** on a push whose version has no release yet, downloads every
   build's archives and publishes GitHub release `vx.y.z` with generated
   notes.

Every job uploads its archives as workflow artifacts. To publish a release,
bump the version in `CMakeLists.txt` and push to `main`. (This repo has no
GitHub remote yet, so the workflow has not run.)

## License

Apache 2.0 for the source code ([LICENSE](LICENSE)). Defender itself, its
ROMs, artwork and sounds are not covered; see [NOTICE](NOTICE).
