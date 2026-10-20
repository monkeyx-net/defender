# Defender native port: plan

A C11 / SDL2 port of Defender (Williams, 1981) that plays like the arcade
game, for PortMaster handhelds (aarch64), desktops (x86_64 Linux, Windows)
and the web. Today the game is the player's own ROM running on an emulated
board, with MAME, running the same ROM, as the reference. The goal
(decision 6, 2026-10-20) is the game rewritten in C, like the Robotron port
(`../robotron`), reading only assets extracted once from the ROM set, with
the emulated board as its reference.

The repository started as a copy of the sibling Joust port (`../joust`,
2026-10-20), which runs the same first-generation Williams hardware.

## 1. Decisions (2026-10-20)

1. **The ROM set is read once** (changed 2026-10-20, as Robotron's decision
   1). The binary never embeds ROM-derived data; it loads it at run time
   from an `assets/` folder. On first run, if `assets/` is missing,
   `defender --extract <defender.zip>` (also run by the launcher when it
   finds the zip in the port folder) writes the graphics, fonts, tables and
   sound data there from the player's own ROM set; after that the ROM set is
   no longer needed. The repo, CI and releases still carry no ROM-derived
   data. This only pays off with the game written in C (decision 6): while
   the emulated board runs the ROM's code, it still reads the ROM set.
   Graphics and sounds are extracted (`src/assets/extract.c`, test
   `extract`); tables follow as the C game needs them. Development builds
   extract to `build/assets/` after each build.
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
6. **The game in C** (changed 2026-10-20). The game logic is rewritten in C
   (`src/sim/`, `src/game/`), as Robotron's was, from the ROM's behaviour
   (traces, probes, the disassembly MAME writes to `build/re`). The
   emulated board (`src/emu/`, checked against MAME by `mame_emu`) stays:
   it is the reference the C game is compared with, frame by frame and in
   process, and it runs the game until the C version reaches parity. It
   also records the sounds at extraction (section 2.1). The 6808 is not
   needed at play time once the C game plays the WAVs.

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

### 2.1 Graphics and sound in the ROM (found 2026-10-20, `extract`)

- Object pictures: descriptors at $F8CE-$F9F0 in the fixed ROM, mostly 10
  bytes: width in bytes, height, even-x data, odd-x data, draw routine,
  erase routine (object block +2 points at its descriptor; the object list
  at $A065 draws through `JSR [6,Y]` and erases through `JSR [8,Y]`, $E3AC).
  $F96F, $F9D5 and $F9D9 are 4 bytes (width, height, data). Data is column
  by column as the screen (byte (x/2)*h + y), at $C000-$CFFF in bank 2,
  which the drawing routines select. The draw routines ($D0F9-$D360) copy
  8 bytes at a time with `PULS`/`PSHU`; $F522 copies any descriptor and
  $F57B erases it.
- Named so far: landers $F985/$F98F/$F999, baiters $F9A3/$F9AD/$F9B7, the
  ship $F9C1/$F9CB, the 250 and 500 scores $F9DD/$F9E7, the ships-left
  and smart-bomb icons $F9D5/$F9D9; the others (mutant, pod, swarmer,
  humanoid, bomber frames, shots) still need confirming in play.
- Font: 45 descriptors of 4 bytes (width, height 8, data) at bank 2 $C5E3;
  the character routine (bank 2 $CB29) maps space ! , - . / 0-9 : ? @ A-Z
  to them and anything else to entry 3. Entries 3 and 5 ('-', '/') hold the
  '?' shape, '@' is blank. Scores use the same digits (bank 2 $C000 points
  at '0').
- The Williams logo is drawn as pen strokes from data in bank 1 ($C6BA);
  the DEFENDER logo is built in RAM. The extractor takes both from the
  emulated attract mode at frame 1500 (after the CMOS priming reboot).
  Still to find: the small font of the copyright line, the terrain, the
  scanner and the stars.
- Explosions scatter a picture's pixels ($FDEF).
- Sound commands are 5 bits ($C0 | n; $1F, sent as $3F, is no command):
  $03, $04 and $0C make no sound on their own; $07, $09, $0F, $10, $11 and
  $1B play until the next command (their WAVs hold 4 s); the others end by
  themselves, the longest in about 17 s. The sound CPU's idle loop is at
  $FD12.

### 2.2 The game program (C0, started 2026-10-20)

Read from the disassembly (`dasm_probe.lua`) and the emulated board; items
marked (likely) are not yet confirmed by a trace or a RAM probe. Direct page
is $A0 throughout, so `<$xx` is $A0xx.
- **Interrupt** ($FFF8 → $A08F, a `JMP $DF17` the game writes to RAM;
  $DFC3 is a second handler with a different split (likely the cocktail
  table)). It reads the video counter and works on the half of the screen
  the beam is not on:
  - beam at line $80 or below (once a frame): the sound sequencer ($D568),
    scrolling and the ship's motion ($E263), the scanner ($E07E), then the
    objects, ship and shots in the top half ($E39F, $E213, $E453);
  - beam above line $80: sets the tick flag `<$5D`, feeds the watchdog
    ($C3FC), copies the palette shadow `<$26-<$35` to $C000-$C00F (near the
    top of the frame), reads the coin switches ($D5FD), calls bank 7 $C003
    unless `<$BA` bit 1 is set (likely the terrain), then the ship and the
    objects in the bottom half and moves the objects ($E376).
- **Main loop** ($E7BE): waits for `<$5D`; if ticks were missed it drops
  objects from the screen list ($E7C9-$E818: likely load shedding); then the
  ship's collisions ($E861: picture $F9C1 facing right, $F9CB left, by the
  sign of `<$BD`), the explosions ($FC66 → $FD2D), the random generator
  ($D711), switch presses queued at `<$82`/`<$86` become processes, then
  the processes run.
- **Processes** (as in the other Williams games): list at $A05F, current in
  `<$63`, free blocks in `<$61` (and larger ones in `<$69`). A block is
  +0 link, +2 resume address, +4 ticks to sleep, +5 type, +6 large flag,
  then the process's own data. $D055 creates one (X = address, A = type),
  $D03E a large one, $D000 sleeps (A ticks, X = where to resume), $D00A
  ends the current one, $D015 kills process X. The dispatcher ($E84D)
  counts each block's +4 down and jumps to +2 at zero.
- **Objects**: drawn objects on the list at $A065 (shots and bombs on
  $A06D; $A06B (likely) the ones off screen), free blocks in `<$67`. A
  block is +0 link, +2 picture descriptor, +4 screen address where it was
  drawn (0: not drawn), +A world x (16 bits; on screen when x - `<$20` <
  $2580, column = (x - `<$20`) >> 6), +C y (8.8, kept in $2A-$F0), +E x
  velocity, +10 y velocity (both added every frame by $E376), +14 flags.
  +6, +8 and +12 are still to identify.
- **Random numbers** ($D711): `<$DF` = 3 x `<$DF` + $11 + `<$E1` + `<$E0`
  (with carry), after a 16-bit shift register in `<$E0-<$E1` steps once
  (feedback bit 0 xor bit 3 of `<$E1`); returns `<$DF`. Called once per
  main-loop tick and by the game's routines.
- **State**: `<$BA` holds the game's mode bits (bit 5: objects not drawn,
  bit 4: no ship, bit 6: no scrolling (likely), bit 1: no terrain (likely));
  `<$20` is the screen's left edge in world x; the scanner's work area is
  at $AF9D.

### 2.3 Timing

Found by comparing instruction traces (`mame_emu`):
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
- $A0BF is a status byte: $21 at power-on, $00 in the attract mode, $10
  once there are credits or a game has ended, and bit 5 ($20) set from a
  game's start to its end (high-score display included), 1 or 2 players
  (`ram_probe.lua`). The port shows its controls page while bit 5 is clear.
- The main CPU's idle loop is $E7C3-$E7C7 (waiting on <$5D, direct page
  $A0); the interrupts do the work. The controls are read around line 130
  ($D5D7, $D5DC).

## 3. Test plan

- **Unit** (`ctest`, no ROM): scalers, presets, glow (`enhance`).
- **ROM** (label `rom`): the loader (`rom_load`); the extractor
  (`extract`: two extractions are byte-identical, every file is there, the
  font matches the attract mode's "PRESENTS" pixel for pixel on the
  emulated board, no manifest without a ROM set).
- **To add with the C game** (as Robotron's 3.2): the C game and the
  emulated board run the same inputs side by side and their state (objects,
  score, wave, random numbers, sound commands) is compared every frame;
  regression traces of the C game; its pictures against the board's.
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
| X1 | (started 2026-10-20) Extractor: object pictures, font, logos, palette and sounds (done); the small font, terrain, scanner and the game's tables as the C game needs them | `extract`; the C game runs from `assets/` alone |
| C0 | (started 2026-10-20; notes in 2.2) Reverse engineering: the object list and block layout, the main loop and its processes, random numbers, waves and their tables, scoring; notes in section 2 | Each routine named and its RAM documented |
| C1 | C game, first wave: ship, terrain, scanner, landers and humanoids, lasers, explosions, smart bomb, hyperspace, scoring, HUD | Side-by-side test against the emulated board over a scripted first wave |
| C2 | Remaining enemies (mutants, bombers, pods and swarmers, baiters), wave flow, planet destruction, extra lives | Side-by-side test over several waves |
| C3 | Attract mode, high scores and initials, operator pages, 2 players; sounds from the WAVs; the C game becomes the default | Full scripted run matches; release with `assets/` only |

## 5. Fixes carried back to the Joust port (2026-10-20)

Found here in code copied from `../joust` and applied there too (Joust's
tests still pass, and 15 of its MAME frames with movement match):
- `pia6821.c`: a write to port B's data direction register also changes the
  pins (port A already reported both).
- `board.c`: the sound CPU catches up to the main CPU before a command
  reaches it, and the command lands as the store instruction ends.
- `m6809.c`: `op_end` (an instruction's end cycle, for devices that read
  the beam); the video counter sampled there.
- `board.c`: the beam timing above (each line drawn 2 cycles before it starts,
  stores that land later wait for it).
- `sound.c`: `step_hook` for sound-CPU traces.
