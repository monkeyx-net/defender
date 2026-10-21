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
- **Processes** (ported: `src/sim/processes.c`, checked by `sim`; as in
  the other Williams games): list at $A05F, current in
  `<$63`, free blocks in `<$61` (and larger ones in `<$69`). A block is
  +0 link, +2 resume address, +4 ticks to sleep, +5 type, +6 large flag,
  then the process's own data. $D055 creates one (X = address, A = type),
  $D03E a large one, $D000 sleeps (A ticks, X = where to resume), $D00A
  ends the current one, $D015 kills process X (and returns the block before
  it, where the dispatcher carries on). A new process goes right after the
  current one with +4 = 1, so it runs in the same pass. The dispatcher
  ($E84D) counts each block's +4 down and jumps to +2 at zero; $D07C kills
  every process but the current one and those of type 2. Blocks: 75 of 15
  bytes at $AAC5, 5 large ones of 23 bytes at $AF2A ($D7C6, only the
  links).
- **Objects** (ported: `src/sim/objects.c`, checked by `sim`): 95 blocks
  of 23 bytes at $A23C, set up by $D805 at a game's start (only the links:
  blocks keep their old contents). Lists: $A065 on screen (drawn, moved,
  hit tested), $A06B off screen, $A06D shots and bombs, free blocks in
  `<$67`. $D0AD takes a free block and links it ahead of the screen list's
  head without making it the head (the appearing effect $FC69 and others
  do); $D095 does that and sets +6 owner (the calling process), +2 picture,
  +8 hit handler and +12 from the three words after the call; $D0C7 frees
  a block on the screen or off-screen list, $D0F2 a shot; $E376 moves the
  screen list (y wraps between lines $2A and $F0). The attract mode's logo
  pieces are blocks outside the pool ($B304 on). A killed enemy's handler
  calls $F416 with two words after the call: the score ($D360) and the
  sound script; it ends the owner process ($D013), frees the block, erases
  it and starts the explosion ($FC63). Processes: 75 blocks of 15 bytes at
  $AAC5 and 5 of 23 at $AF2A ($D7C6). The palette starts from the 16 bytes
  at $F8BE ($D7F5). A block is +0 link, +2 picture descriptor, +4 screen address where it was
  drawn (0: not drawn), +A world x (16 bits; on screen when x - `<$20` <
  $2580, column = (x - `<$20`) >> 6), +C y (8.8, kept in $2A-$F0), +E x
  velocity, +10 y velocity (both added every frame by $E376), +14 flags.
  +8 is the hit handler ($E6BA); +6 and +12 are still to identify.
- **Random numbers** ($D711): `<$DF` = 3 x `<$DF` + $11 + `<$E1` + `<$E0`
  (with carry), after a 16-bit shift register in `<$E0-<$E1` steps once
  (feedback bit 0 xor bit 3 of `<$E1`); returns `<$DF`. Called once per
  main-loop tick and by the game's routines.
- **Controls** (bottom-half interrupt, $D5CB): `<$7B` = IN0, `<$7D` = IN1
  (up), `<$7C` the previous IN0. A button newly down (up in the last two
  reads) looks up its entry in the 4-byte table at $F882 (one per IN0
  bit: process address, type, and a mask of `<$BA` bits that ignore it)
  and is queued at `<$82` (or `<$86`) for the main loop to start: fire
  $E591, smart bomb $E8C1, hyperspace $E91F, 2-player start $D84E, 1-player
  start $D839, reverse $E897. Thrust, up and down are read as held.
- **The ship** (ported: `src/sim/ship.c`, checked every frame by `sim`).
  $E263 moves it and scrolls: velocity `<$C7-<$C9` (24 bits; world units a
  frame x 256), friction -1/64 a frame, thrust adds `<$BD` ($0300 facing
  right, $FD00 left), the scroll `<$20` moves by at most $100 units (8
  pixels) a frame. World x has 32 units to a pixel: the planet is 2048
  pixels around. The ship's screen x `<$C3` ($2000 facing right, $7000
  left, in 1/256 byte) leads by speed / 8 in the facing's direction and
  slides there by $100 a frame while `<$95` (±$40) holds it still in the
  world; `<$CC` is its world x. Up and down: `<$CA` starts at ∓$100 (a line
  a frame) and adds ∓8 a frame to ∓$200, within lines $2B-$EE of `<$C5`
  (8.8). New ship ($D980): `<$BD` = `<$BB` = $0300, screen x $2000, y
  $8000, at rest.
- **Laser** (ported: `src/sim/laser.c`, checked by `sim`): fire starts
  process $E591 unless 4 are out (`<$B5`); sound $D51B. It lives on the
  screen, not in the world: from the ship's screen position (`<$C1-<$C2`)
  +$0704 facing right (+$0004 left), each tick ($E5CB right, $E638 left)
  the head writes $11 to 4 columns and a $99 tip (8 pixels a tick, ending
  at column $98 or $05), the 3 bytes behind the previous head take colours
  from the table at $A142 (pointer `<$A4`, wrapping at $A15F; filled by
  $E69F with random bytes of palette index 0 or 1 per nibble: index 1
  cycles), and the tail clears a byte a tick. Then the hit test: the 8 x 1
  line $F96F at the head (6 columns back facing right) against the objects
  ($E5AB → $E6BA). A hit, the edge or `<$BA` bit 6 erases it tail to head.
- **Landers** ($EF9C spawns `<$73` of them; process $EFF6 ported:
  `src/sim/lander.c`, checked by `game`). Process block: +7 its object, +9
  its humanoid, +B that humanoid's slot in the table at $A11A (32 object
  words; `<$9B` the slot the last search stopped at, `<$FA` humanoids
  left), +D ticks to its next shot. Every 6 ticks it keeps $32 lines above
  the terrain (the line under each 64 world units at $B300), moving at the
  wave's speed ($A103); counts down to its next shot ($EEFE: 1..[$A105]+1
  steps, then $EEBA and script $D525) and steps its picture ($F985,
  $F98F, $F999). Right above its humanoid (same x & $FC00, the humanoid's
  handler $ED70: on the ground) it starts the capture ($F065, to port);
  with no humanoid left it becomes a mutant ($F13E, picture $F8CE, handler
  $EF6D, process $F15E, to port). `<$A112` counts landers, `<$A115`
  mutants.
- **Enemy shots** (ported: `src/sim/shots.c`): $E3F3 takes a free block
  for a shot from an object on screen below line $2A, at most 20 (`<$99`),
  in screen coordinates (+A column.line, +C line.column), +15 = +16 = $14,
  on the shots list; the three words after the call are +12, picture and
  handler. $EEBA fires picture $F95B ($E4D9, handler $E51E) at the ship
  (`<$BF` column, `<$C0` line) with a random spread of -16..15, plus 4 x
  the ship's speed when `<$DF` > $78; speeds are the 8-bit differences x 4.
- **Smart bomb** ($E8C1): not while one is going (`<$9A`) or with none
  left (the player block `<$8D` +9, decremented; $D680 redraws the HUD);
  script $D4D2. It calls the hit handler (+8) of every object on screen
  (+4 not 0) with +14 below 2, from the list's head again after each (the
  handler unlinks it); then inverts palette entry 0 (`<$26`, the
  background) 4 times 2 ticks apart, waits 10 ticks, while the button is
  held 10 more at a time, then 10 more before another bomb. Ported with
  the objects.
- **Sound** (ported: `src/sim/sounds.c`, checked by `sim`): $D539 sends a
  sound number B to the board as $3F then ~B & $3F, so the board plays
  command ~B & $1F (the extractor's WAV). $D54D starts a script (`<$AD`
  cleared even when the script does not start); 26 scripts sit back to
  back at $D4AB-$D538, extracted to `sounds/scripts.txt`: a priority, then
  (count, ticks, sound) entries ending in a 0 count. A script starts at or
  above the playing one's priority (`<$B2`); `<$B0` points at the entry
  (less 1), `<$B3` counts ticks, `<$B4` repeats. $D568, once a frame in the
  interrupt, steps it; with no script it sends $16 (command $09, a loop)
  when thrust is pressed (unless `<$BA` has bits $98) and $0F (command
  $10) when released (`<$AD`). Sounds play on the board until the next
  command replaces them. Laser: $D51B; smart bomb: $D4D2.
- **Hit test** ($E6BA; D = a screen position, U = a picture descriptor,
  X = the list, $A065 by default): bounding boxes against each drawn
  object (its +4 screen position and its picture's size), then the two
  pictures' even data byte by byte; on a hit it leaves the screen address
  in `<$F8`, calls the object's +8 routine (its hit handler) and returns
  A = 1. The ship's collisions ($E861) use it too.
- **Reverse** ($E897, once per press): `<$BB` = -`<$BD`; the ship turns
  (`<$BD` = `<$BB`) when it is next drawn ($E237); then it waits for the
  button's release and 5 more ticks (`<$AF` meanwhile).
- **Hyperspace** ($E91F, not while `<$BA` has bits other than 1 set):
  `<$BA` = $77, the screen is cleared ($F5F1), 15 ticks, shots removed; the
  scroll becomes `<$DF-<$E0` (the random state), the facing bit 0 of `<$E0`
  (right: screen x $2000, else left: $7000), y = `<$E0`/2 + $2A, at rest;
  the ship reappears for 40 ticks, then dies ($DA46) when `<$E1` > $C0
  (63 chances in 256).
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
- **Side by side** (label `rom`): `sim` plays a scripted game on the
  emulated board and checks each ported routine at every call: the state
  the C version computed from the previous call equals the ROM's. Writes
  from code not yet ported resynchronise it (counted). So far: the random
  generator (5000 calls), the ship (4400 frames) and the laser (65
  lasers, 1700 ticks, 16000 screen bytes; bytes the interrupt draws over
  between the laser's write and the check are skipped and counted) the
  sound sequencer (4400 frames, 97 scripts, 125 sounds sent) and the
  objects (67 taken, 15 freed, 4264 moves; writes to the pool from code
  not yet ported are copied to the C pool, ported routines run in C, and
  after each returns every list and live block is compared; paused while
  the attract mode's logo blocks are on a list) and the processes (154
  created, 144 killed, 9775 sleeps, 32817 countdowns, compared after each
  call and at each step of the dispatcher). Not covered yet: an
  object's y wrapping (no object crosses in the scripted minute). The board
  calls the step hook also before an instruction the CPU then puts off for
  an interrupt; `sim` ignores those calls (IRQ or FIRQ pending and not
  masked). Where an interrupt can run in the middle of a routine, the
  check follows the ROM's order of stores.
- **Process steps** (label `rom`): `game` keeps a C model of all of RAM
  (`sim/game.h`) on the board's scripted game: the board's writes are
  copied to it, except during a ported process step, which runs in C when
  the ROM's dispatcher starts it; as the ROM's step writes each byte the
  model takes the C value, and when it yields the model must equal RAM.
  Steps an interrupt broke into may differ (it can change what the step
  reads): counted and reloaded. So far 1049 of 1054 lander steps match, 4
  take paths not yet ported, 1 interrupted. Limit: a byte the C step does
  not write goes unnoticed when the ROM writes the value it already held.
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
| S0 | (started 2026-10-20) The C game's first pieces in `src/sim/`, each checked in process against the board as the ROM runs: the random generator (`rng.c`, $D711), the ship's motion (`ship.c`, $E263), the laser (`laser.c`, $E591-$E6B9; drawn into video RAM laid out as the arcade's, so the screen can be compared byte for byte), the sound sequencer (`sounds.c`, $D539-$D5A6), the object pool and lists (`objects.c`), the process pool and dispatcher's bookkeeping (`processes.c`) | `sim` |
| S1 | (started 2026-10-21) Process steps in C on a model of all of RAM (`src/sim/game.c`): the lander's main step (`lander.c`), enemy shots (`shots.c`) | `game` |
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
