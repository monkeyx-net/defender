# Defender native port: plan

A C11 / SDL2 port of Defender (Williams, 1981) that plays like the arcade
game, for PortMaster handhelds (aarch64), desktops (x86_64 Linux, Windows)
and the web. The game is the player's own ROM running on an emulated board;
MAME, running the same ROM, is the reference.

The repository started as a copy of the sibling Joust port (`../joust`,
2026-10-20), which runs the same first-generation Williams hardware.

## 1. Decisions (2026-10-20)

1. **No ROM-derived data in the repo, the packages or the binary.** The
   program reads the player's ROM set at run time. CI and releases carry none.
2. **MAME is the reference.** Behaviour is established by probing the ROM in
   MAME 0.289 and checked by CTest against the port. Probes are Lua and only
   dump; C or CMake compares.
3. **Shared code is copied, not linked.** The platform layer, loaders,
   framebuffer, enhanced graphics and the emulated CPUs, PIA and sound board
   came from the Joust port (itself from Robotron). Fixes found here that
   apply there are listed in section 5, to carry across once agreed.
4. **Controls:** one control panel (the players take turns), as the
   upright cabinet: up/down, Thrust, Reverse, Fire, Smart Bomb, Hyperspace.
   Pad: stick or D-pad up/down, A fire, B or R1 thrust, L1 reverse, X smart
   bomb, Y hyperspace; every pad drives the panel. Select = coin, Start =
   1P, L1+Start = 2P, Select+R1 = operator menu (Advance), Select+L1 =
   graphics presets, hold Select+Start 1 s = exit. Keyboard: arrows up/down,
   Space/Ctrl fire, X/Alt thrust, Z reverse, C smart bomb, V/Shift
   hyperspace, 5 coin, 1/2 start, F2 Advance.
5. **Enhanced graphics** as in the Joust port (FILTER and HD presets, glow,
   scanlines), cosmetic only, classic by default. The game's frame is only
   pixels, so the HD presets upscale it like FILTER.
6. **Run the ROM's code** on an emulated board (`src/emu/`), written in C for
   this port (no MAME code); `mame_emu` checks it against MAME.

## 2. Hardware (Williams first generation, before the blitter)

Confirmed in this repo (each by a `mame` test):
- Program ROM (`mame_rom_map`): $D000-$FFFF fixed (12 KB: defend.1, .4, .2,
  .3). $C000-$CFFF shows the bank last written to $D000-$DFFF (value & $0F):
  bank 0 is the I/O page; banks 1-3 and 7 are ROM (defend.9/.12, .8/.11,
  .7/.10, .6 in the lower half of 7); banks 4-6 and 8-15 read 0 in MAME. The
  two decoder PROMs are not needed.
- I/O page (bank 0, offsets from $C000): palette $000-$00F (write-only,
  mirrored every $20 to $3FF; $010-$01F the video control for cocktail
  flipping, $3FC the watchdog); CMOS $400-$4FF (256 nibbles, mirrored to
  $7FF); video counter $800-$BFF; PIAs at $C00 (coins in, sound commands
  out, the interrupts) and $C04 (controls), mirrored every $20 to $FFF.
- Video (`mame_capture`): RAM $0000-$97FF is the screen, 152 columns of 256
  bytes, byte (x/2)*256 + y holding pixel x in the high nibble; $9800-$BFFF
  is work RAM. The visible area is x 12-303, y 7-246 (292 x 240). Palette
  bytes are BBGGGRRR through the same resistor weights as Joust.
- Controls: PIA $C04 port A = IN0 (fire 01, thrust 02, smart bomb 04,
  hyperspace 08, 2P start 10, 1P start 20, reverse 40, down 80), port B =
  IN1 (up 01); PIA $C00 port A = IN2 (as Joust: auto up 01, advance 02,
  coins 04/10/20, high score reset 08, tilt 40).
- Sound board: a 6808 at 894886 Hz, 128 bytes of RAM ($0000-$007F), the
  2 KB ROM at $F800-$FFFF ($B000-$F7FF read 0), the PIA at $0400 (mirrored
  at $8400). Its interrupt handler ($FCB6) resets the stack and starts each
  sound afresh.

Timing, found by comparing instruction traces (`mame_emu`):
- A frame is 260 lines of 64 CPU cycles, starting at line 247; MAME's frame
  callbacks fire 16640 cycles apart but run a few instructions off the
  boundary, so RAM dumped there is a little fuzzy (the stacks, the sound
  RAM). Instruction traces are the exact comparison.
- At power-on the sound CPU enables its command interrupt (43.6 us) just
  before the main CPU's first command (DDRB = $FF at 55 us): the sound CPU
  must catch up before each command. A command reaches it as the main CPU's
  store instruction ends. MAME delivers commands with some jitter (up to a
  dozen sound cycles late), so the sound CPU is compared interrupt to
  interrupt, not cycle for cycle.
- The game sends each command as $3F (no command) and, 9 cycles later, the
  command itself.
- The video counter is read with `LDA $C800` ($DF26, $DF3C); the value is
  the beam's line when the load's read lands, on its last cycle. Sampling
  the line at the instruction's start instead made the game diverge from
  MAME a minute into play.
- Each line is drawn whole from RAM as it is 2 cycles before the line
  starts (2 or 3 match all of MAME's captured frames, nothing else does);
  a store landing after that waits for the line to be drawn.
- With a blank CMOS the game restores its factory settings and stops in the
  bookkeeping pages; the port (and the MAME tests) run that once and boot
  again with the CMOS it wrote.
- The main CPU's idle loop is $E7C3-$E7C7 (waiting on <$5D, direct page
  $A0); the interrupts do the work. The controls are read around line 130
  ($D5D7, $D5DC).

## 3. Test plan

- **Unit** (`ctest`, no ROM): scalers, presets, glow (`enhance`).
- **ROM** (label `rom`): the loader (`rom_load`).
- **MAME** (label `mame`): `mame_rom_map`; `mame_capture` (video RAM and
  palette decoding against MAME snapshots of attract-mode pages); `mame_emu`
  (the board from power-on with a scripted minute of play, against MAME's
  instruction traces of 100 frames, and RAM and pictures at 12 frames).
- **CI** (`.github/workflows/build.yml`): Linux x86_64/aarch64 in Ubuntu
  20.04, Windows, web; no ROM in CI, so only the unit tests run there.

## 4. Phases

| Phase | Work | Exit criterion |
|---|---|---|
| P0 | (done 2026-10-20) Repo from the Joust port: ROM set loader for the defender set (banked ROM), MAME pipeline, platform layer with Defender's controls, test card, packaging, CI | `enhance`, `rom_load`, `mame_rom_map` pass |
| E1 | (done 2026-10-20) Emulated Defender board: the I/O page, ROM banking, video counter and beam timing, sound board; the game plays from the player's ROM with sound | `mame_emu`: main CPU cycle for cycle and sound CPU instruction for instruction over frames 1600-1700 of scripted play; RAM and pictures equal at 12 frames to frame 4800 |
| E2 | Polish: pad play-testing (thrust and reverse placement; an option to thrust in the stick's direction needs the ship's facing from RAM), settings persistence, web build with IndexedDB CMOS, cocktail flip, device testing | Device checklist |
| E3 | Enhanced graphics beyond whole-frame filters (e.g. HD sprites need the game's object lists: reverse engineering) | Presets look right on devices |
| E4 | Release: packages, PortMaster submission, GitHub release | Published |

## 5. Fixes to carry back to the Joust port (ask first)

Found here in code copied from `../joust`; Joust's tests did not exercise them:
- `pia6821.c`: a write to port B's data direction register also changes the
  pins (port A already reported both).
- `board.c`: the sound CPU catches up to the main CPU before a command
  reaches it, and the command lands as the store instruction ends.
- `m6809.c`: `op_end` (an instruction's end cycle, for devices that read
  the beam); the video counter sampled there.
- `board.c`: the beam timing above (each line drawn 2 cycles before it starts,
  stores that land later wait for it).
- `sound.c`: `step_hook` for sound-CPU traces.
