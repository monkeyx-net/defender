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
  +6 the owner process, +8 the hit handler ($E6BA), +12 the scanner
  colours (a lander $4433, a mutant $CC33), +15/+16 a shot's life.
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
- **Landers and mutants** ($EF9C spawns `<$73` landers; every step
  ported: `src/sim/lander.c`, checked by `game`). Process block: +7 its object, +9
  its humanoid, +B that humanoid's slot in the table at $A11A (32 object
  words; `<$9B` the slot the last search stopped at, `<$FA` humanoids
  left), +D ticks to its next shot. Every 6 ticks it keeps $32 lines above
  the terrain (the line under each 64 world units at $B300), moving at the
  wave's speed ($A103); counts down to its next shot ($EEFE: 1..[$A105]+1
  steps, then $EEBA and script $D525) and steps its picture ($F985,
  $F98F, $F999). Right above its humanoid (same x & $FC00, the humanoid's
  handler $ED70: on the ground) the capture starts ($F065): every tick
  ($F074) it moves $20 units towards it and to 12 lines above it; within
  $40 units it grabs it (handlers $F1E0 and $ED91, script $D50C) and both
  rise at the wave's speed every 4 ticks ($F0E6) to line $32 (script
  $D511); then ($F101) the humanoid is pulled up a line a tick (the ROM
  sends sound 0 there: its `LDA #$12` never reaches $D539's B) and on
  reaching the lander dies ($ED77) and the lander becomes a mutant ($F13E:
  picture $F8CE, +12 $CC33, handler $EF6D). With no humanoid left a lander
  becomes a mutant at once; if its humanoid was shot on the way up it
  leaves (`<$FB` +1). The mutant ($F15E, every 3 ticks): sideways at
  [$A10A] towards the ship's world x; within $700 units of it, up or down
  at [$A108] towards its line (and, off screen, nothing more), else away
  from it within 8 lines; jitter of [$A107] lines (wrapping below line
  $2A to $F0); shoots every 1..[$A10B]+1 steps (script $D52A).
  `<$A112` counts landers, `<$A115` mutants.
- **Humanoids** (ported: `src/sim/humanoids.c`): the table at $A11A, `<$FA`
  left. $EDC4 takes one out (the last one starts process $EDEA: the
  planet goes); $ED77 kills one: out of the table, freed and erased
  ($F3FB), picture $F8D8 2 pixels left, an explosion ($FC63), script
  $D4E4.
- **Explosions** (start ported: `src/sim/explosions.c`): $FCCC takes one
  of 16 slots of $40 bytes at $9C00 round robin (`<$E2`); a slot with +0
  negative is skipped (an object appearing), one still exploding is
  erased first ($FDD5: the screen words listed from its +4 to +$40). +0
  $0100, +2 the picture, +4 the end of its list, +6 the centre (the hit
  point `<$F8` when it lies in the picture, else the middle), +8 the top
  left. The main loop's $FD2D grows and draws them (to port).
- **Freeing and erasing** ($F3FB, in `src/sim/game.c`): $D0C7, then bank
  2 (`<$36`) and the picture's erase routine at +4: zeros over its width
  x height.
- **Waves** (process $DCEA, every 15 ticks, ported: `src/sim/wave.c`):
  enemies left ($DDAE) = `<$A112` landers + `<$FB` landers to come +
  $A113 + $A114 + $A116 + `<$A115` mutants + `<$FE`; none: the wave ends
  (`<$BA` = $77, $D07C, $DBB6 saves the wave to the player block, $DDEC,
  the player's +7 + 1, then $D91E: to port). 8 or fewer: the baiter timer
  $A118 is cut to [$A10F]/2+1 (/4+1 with 3 or fewer); when it runs out it
  restarts at [$A10F] (1..[$A10F]/4+1 with under 4 left) and a baiter
  comes ($EA80) while `<$A119` < 12. Every [$A100] steps ($A117), or with
  no lander left, min([$A101], `<$FB`) landers arrive while under 8 are
  alive ($EF9C). `<$AE` counts to $10. `<$24` counts steps; past $F0 (a
  minute) bank 3's $C00F adds 1 to CMOS bookkeeping counter 6, leaves
  bank 3 selected, and it starts again from 0 (ported). Every 40 steps ($DEEC,
  B = 2) the settings from $A0FB ramp: bank 7's table at [$C011]-[$C013],
  8 bytes per setting (max, min, deltas by column B), kept within. Wave
  start ($DC1E): the humanoid walker, the table cleared, the
  humanoids placed ($DBE1: groups of a quarter at x $00/$40/$80/$C0 high,
  the rest at random; line $E0, flags $10, picture $F901 or $F915), the
  player block's wave settings (+B on) copied to $A0FB-$A111, $A112-$A119
  cleared, then the enemies carried over: swarmers (`<$FF`, up to 6 at a
  time around a spare block taken before the count is looked at, so with
  none one block stays taken; its x's low byte is B as the ROM left it,
  then the last spare's x), mutants (`<$FE`, $EF15, which leaves U at its
  last process), pods (`<$FD`, $EB36: their owner word is that U, else the
  player block + $22), bombers (`<$FC`, groups of up to 3). Ported:
  `wave_start` in `src/sim/wave.c`, 1000 random starts equal the ROM's.
- **Arrivals**: landers ($EF9C, ported in `lander.c`): a process
  ($EFF6) and an object (+12 $4433, handler $F20B) at x = `<$E0-<$E1`
  after a random step, line $2C, at the wave's descent ($A103), sideways
  1..[$A102]+1 (odd: the other way, ~), first shot 1..[$A105]+1; with no
  humanoid left mutants instead ($EF15: away from the screen, x =
  `<$E0-<$E1` - (scroll - $2580), + $8000 under $4B00). Appearing ($FC69,
  ported in `explosions.c`): the hidden picture [$FF9D] ($F8EC), the object
  made the screen list's head, a slot after `<$E2` (not moved on) with +0
  $AF00, +2 the picture, +A the object, flags | 2, script [$FFDD] ($D4EE)
  unless `<$BA` bit 7; off screen ($2600) or no slot: at once.
- **Baiters** (ported: `src/sim/baiter.c`): $EA80 a process ($EAB4) and
  an object ($F9A3, handler $EB2B, +12 $3333) at x = (`<$DF` & $1F,
  `<$E0`) + scroll, line its x's low byte / 2 + $2A, first shot in 8;
  steered, then appearing. $EAB4 every 6 ticks: shoots every
  1..[$A110]+1 (script $D52F); at each turn of its picture ($F9A3-$F9B7),
  when `<$DF` > [$A111], steers ($EAEE): beyond $280 units of the ship it
  moves at the ship's speed $40 towards it; more than 10 lines off, at
  (±$100 + the ship's vertical speed) / 2 towards it. Hit: 200 points,
  script $D4FD.
- **Pods and swarmers** (ported: `src/sim/pods.c`). Pods ($EB36) are
  objects without a process of their own ($F8F7, handler $EB74, +12
  $CCCC, owned by the caller's process) at x ((`<$E0` & $3F) + $10,
  `<$E1`), line `<$E1`/2 + $2A, drifting at fixed random speeds. A pod hit
  ($EB74 → $F41D: freed, its owner lives on): 1000 points, script $D4F3,
  1..7 swarmers burst out ($EB9E), `<$A114` - 1. Swarmers ($EB9E, at most
  20 by `<$A116`): a process and an object ($F97B, handler $EBE9, +12
  $2424) at the given object, random speeds ($EB89), +9 a vertical pull
  (random & [$A10E]), a first sleep of 0-31 ticks (+4), +B ticks to the
  first shot; they go straight on the screen list. $EC17 aims them
  sideways at [$A10C] towards the ship; $EC2A every 3 ticks pulls them to
  the ship's line (±$200, damped by a quarter of a 256th, jittered),
  turns them back far off screen ($12C0 / $2580), shoots every
  1..[$A10D]+1 steps when heading at the ship ($EC86: 8 x their speed,
  script $D534). A swarmer hit: 150 points, script $D516, picture $F8E2 2
  lines up and 2 pixels left for its explosion.
- **Bombers** (ported: `src/sim/bombers.c`): $F29D a group of up to 3
  under one large process ($F2F7, every tick): +7/+9/+B its objects ($F929,
  handler $F3BC, +12 $8888), +F how many, +10 the base line ($50); they
  arrive at the ship's x + $8000 + 1.5 x their number, at ±[$A106]
  (alternate groups the other way, `<$AA`). Each tick one picked at random
  (`<$DF` & 6) steps its picture ($F929-$F947), gets a push of
  (`<$DF` & $3F) - $20 and damping; off screen it drifts to the base line
  (which wanders by -2..1 within $40-$68 when `<$DF` <= $40), on screen it
  keeps $10-$20 lines from the ship's; 1 tick in 8 (`<$E1` & 7) it lays a
  mine ($F3DC: a shot $F95B, +12 $E498, under 10 shots, lasting
  (`<$DF` & $1F) + 1). A bomber hit ($F41D): 250 points, script $D502,
  out of its group; the group's process ends with its last bomber.
- **Background processes** (ported: `src/sim/background.c`), started with
  the player's turn ($D9CD-$D9FA): $E782/$E784 palette entry 1 (`<$27`,
  the laser) through the zero-ended table at $E799 every 2 ticks; $E9BF
  steps `<$9F` ($A162-$A182) and `<$A8` ($A1A2-$A1BA) every 4; $F43D /
  $F44A palette 13-15 from $F45B-$F463 every 6; $F464 / $F472 palette 10
  and 12 white and black for 3 ticks, then a random $E799 colour for 6,
  `<$A6` swapping $CCB0 / $CCBC; $F493 / $F4B0 the score blinking until the
  first points (`<$8A`); $E9E3 / $E9EE / $E9FC (2, 2, 4 ticks) the list
  sorting ($EA33: off-screen objects move 8 frames' worth and join the
  screen list within $3E80 of scroll - $0C80; $EA0A: the others leave it),
  shots ageing ($E56A: +15 counts down while +16 is set; then freed,
  erased, `<$99` - 1) and the scanner (bank 1 $CCAD: last drawing's dots
  listed from $B05D to [`<$97`] and the ship's marker cleared; 64 terrain
  columns $30-$6F from bank 1's table at $CD69, 3 bytes each, from
  (scroll - $6D40) >> 10, not while `<$BA` bit 1; the corners; every
  object's +12, its scanner colours, at ((x - (scroll - $6D40)) >> 10,
  line >> 3) + $3007; the ship $9099 at (`<$BF` >> 4, `<$C0` >> 3) +
  $4B07).
- **Enemy shots** (ported: `src/sim/shots.c`): $E3F3 takes a free block
  for a shot from an object on screen below line $2A, at most 20 (`<$99`),
  in screen coordinates (+A column.line, +C line.column), +15 = +16 = $14,
  on the shots list; the three words after the call are +12, picture and
  handler. $EEBA fires picture $F95B ($E4D9, handler $E51E) at the ship
  (`<$BF` column, `<$C0` line) with a random spread of -16..15, plus 4 x
  the ship's speed when `<$DF` > $78; speeds are the 8-bit differences x 4.
- **The ship's collisions** ($E861, main loop; ported in `player.c`):
  with bank 2 selected, the ship's picture ($F9C1 / $F9CB by `<$BD`) at
  where it was drawn (`<$BF-<$C0`) is hit-tested against the screen list,
  then the shots ($E6BD), with `<$DE` incremented meanwhile so the
  handlers know it is the ship. Any hit starts the ship's death (process
  $DA46) and sets `<$BA` bit 3. Enemies touched die as when shot; a
  humanoid on the ground declines (no hit); a falling or carried one
  ($ED91, owner set) is caught: script $D4DA and a 500 bonus ($EE73) unless
  its owner already resumes at $F24C, which it then does (no hit). A shot
  touched ($E51E): 25 points, `<$99` - 1, freed ($D0F2) and erased, then
  at world x (screen x / 4 + scroll), 2 lines up, it explodes as $F951,
  script $D4E4.
- **Smart bomb** ($E8C1, ported: `src/sim/player.c`): not while one is
  going (`<$9A`) or with none left (the player block `<$8D` +9,
  decremented; $D680 redraws the HUD); script $D4D2. It calls the hit
  handler (+8) of every object on screen (+4 not 0) with +14 below 2
  (humanoids have $10, appearing objects | 2), from the list's head again
  after each (the handler unlinks it); then inverts palette entry 0
  (`<$26`, the background) 4 times 2 ticks apart ($E900), waits 10 ticks,
  while the button is held 10 more at a time ($E90C), then clears `<$9A`
  ($E91A).
- **HUD** (ported in `player.c`): $D629 the ships left (player block +7,
  up to 5 icons $F9D5 6 columns apart at $0F14 / $7114 after clearing $20
  x 6), $D680 the smart bombs (+9, up to 3 icons $F9D9 4 lines apart at
  $291B / $8B1B after clearing 3 x $0B), both players' when `<$8C` is 2;
  $D665 the whole HUD (to port: its frame $D6BC). $DBB6 saves the wave in
  the player block: +A..+3C cleared, +A humanoids left, +B.. the settings
  from $A0FB, the counts to come ($A0FB-$A100) plus those alive
  ($A112-$A117).
- **Drawing and erasing** (`game_draw_picture`, `game_erase_picture`):
  each picture's draw routine (+6 of its descriptor) copies width x height
  bytes of its even-x data (+2), or of its odd-x data (+4) when the carry
  is set; its erase routine (+8) clears them; the hidden picture's ($F8EC,
  routine $D8DB) are a bare RTS. Most fixed-size routines ($D0F9-$D35F)
  point U at (column, (line + h) & $FF) and push down, so past line $FF a
  picture spills into the column before; $D1AD-$D21E, $F522 and $F57B
  index from the address. They keep S at `<$77-<$78` (not modelled).
  `routines` checks every picture's draw (both parities) and erase at
  every line.
- **CMOS, coins and credits** (ported: `src/sim/cmos.c`). CMOS ($C400-
  $C4FF) holds nibbles (the board reads them | $F0); $F822 reads a byte
  from two (the first the high), $F838 two bytes from four, $F84E / $F864
  write them, with bank 0 selected and restored. Settings read so far:
  [$C481]-[$C483] the first extra ship's score, [$C485] ships, [$C487 +
  2n] the units for a coin in slot n, [$C48F] units per credit, [$C491]
  units per bonus credit, [$C493] the units before credits count, [$C495]
  free play (1), [$C497] / [$C499] the ramp's start and limit, [$C49D]
  waves between humanoid restores; [$C47D] the credits. Bank 3 $CBE0 /
  $CBE6 add 1 / A (BCD) to bookkeeping counter B at $C3FD + 4B (1-3 coins
  per slot, 4 credits paid, 5 extra ships, 6 minutes, 7 games); $CC0D
  adds credits to `<$37` (BCD, at most 99) and saves them; $CC2C / $CC26
  / $CC20 a coin in slot 1 / 2 / 3: counted, its units added to `<$38` and
  `<$39`, and from [$C493] units on credits `<$38` / [$C48F] (the rest
  kept) plus bonus `<$39` / [$C491] (which clear both), counted (4) and
  added. The coin processes ($D46E slot 1, $D47C slot 2, $D475 slot 3; the
  switch table at $F8A2 also has $D44C the Advance and $D43D the
  high-score reset): not while `<$7E` (slam) or their debounce timer
  (`<$7F`, `<$81`, `<$80`) runs; the timer set to $16, 10 ticks ($D499),
  script $D4AB, bank 3's routine.
- **Starting a game** (ported: `src/sim/flow.c`): $D839 / $D84E, in the
  attract mode (`<$BA` negative) with 1 / 2 credits (free play gives 2;
  $D82B): script $D4BD and $D861 / $D861, script $D4C2, $D861. $D861 (not
  while `<$B7` is 0): `<$38` cleared; the first start ends every other
  process ($D07C: not the current one nor type 2), clears the screen
  ($F5D1: $0000-$9BFF), `<$BA` = $7F, player 1 up (`<$8B`), `<$25` = 1,
  both player blocks cleared, ships [$C485] & $0F (+7) and as many smart
  bombs (+9), 10 humanoids (+A), the first wave ($DE7C), the next extra
  ship (+4..+5 and `<$AB-<$AC` from [$C481]/[$C483]), the block copied to
  player 2 ($A1FF) and the turn's process $D919 started; every start
  counts a player (`<$8C`), takes a credit (saved) and, the second,
  redraws the HUD ($D665: the scanner's area $40 x $20 at $3008 cleared,
  the frame $D6BC, ships, smart bombs, each score). $DE7C, a player's
  next wave: +8 + 1; every [$C49D] waves +A = 10; bank 7's settings column
  for waves 1-4 (+3) into +B..; then the ramp's column 3 (wave - 4 +
  [$C497]) times, at most [$C499] (both BCD), on the current player's
  block. $F5F1 clears the playfield only (lines $2A-$FF).
- **The text printer** (bank 2; ported: `src/sim/text.c`): $CAA7 draws a
  message, $CAB2 erases it, $CABD / $CAC8 one character a call (the
  typewriter), at X with U the message (a word pair: list start, end).
  Items: a word at $8000 on points to a '/'-ended word (printed, then a
  space); else a control code 0-8 with its byte(s): 0 / 1 the character /
  line spacing (`<$4C` / `<$4D`, $01 / $0A), 2 / 3 the cursor column from
  the line start / on, 4 / 5 its line, 6 the position (a word), 7 a new
  line, 8 a glyph (drawn at D as the dispatch left it: ($10, the
  descriptor's high byte), a ROM quirk). Characters map to the font at
  $C5E3 as $CB29 (anything else '-'), drawn by $F522 or cleared by $F57B
  at the cursor `<$50-<$51`, which moves on by the width and `<$4C`.
  State: `<$3D` $FFB3 / $FFB6, `<$3F` $CAED (go on) / $CB5F (return),
  `<$4E` the line start, `<$52` the list position (high byte 0: none in
  progress), `<$54` the word, `<$56` the end, `<$58` a space due. $F513
  prints a message with bank 2 selected.
- **A player's turn** (ported: `src/sim/turn.c`): $D919 counts it
  (bookkeeping 7, bank 3 left selected); $D91E (also after a wave or a
  death) resets ($D7B6: the palette from $F8BE, laser colours $E69F,
  stars $E052 (16 at random, colours stepping $11), objects $D805, bomb
  colours $E54B, flame colours $E149 twice over), ends every other
  process, `<$BA` = $7F, and unless it is the only process waits
  ($D93A, 15 ticks at a time) for `<$7F` and `<$80` (the ROM tests `<$7F`
  twice); $D942: a fresh process pool ($D7C6) with only $D950, and the
  main loop starts again ($E7BE: the rest of the tick's pass is dropped).
  $D950: (a cocktail table's player 2 side, IN1 bit 7: not ported) the
  scroll 0, bank 7's terrain ($C006 heights, $C000 buffers), the
  playfield cleared, the ship's state for a new ship, `<$8D` the player's
  block, palette entry 5 (`<$2B`) by the wave & 7 from $DB53, a ship
  taken (+7), the HUD, the background processes; with 2 players at the
  game's start (`<$25`) "PLAYER ONE" / "PLAYER TWO" ($C0EF / $C0F1 at
  $3C80) for $80 ticks; $DA1F: the playfield cleared, `<$BA` = 5 (| 2
  without humanoids), $60 ticks; $DA32: the wave's start, `<$BA` = 0 (|
  2), `<$25` cleared, and the process becomes the wave's ($DCD9: +7 =
  $28, `<$A118` = [$A10F], `<$A117` = 1, then a wave step).
- **Hyperspace, a death, a wave's end, game over** (ported:
  `src/sim/death.c`). $E91F (hyperspace, only with `<$BA` & $FD clear):
  `<$BA` = $77, the playfield cleared, 15 ticks; $E936: the shots freed,
  the scroll `<$20`/`<$22` = the random word `<$DF`, facing by its bit 0
  (screen x $2000 / $7000, speed $0300 / $FD00), line `<$E0` / 2 + $2A,
  bank 7's terrain, `<$BA` = $50, a ship object ($F9C1 or $F9CB, handler
  $EDBC, +6 the U the terrain code left) appearing, $28 ticks; $E9AC: it
  is killed, `<$BA` = 0, and with `<$E1` > $C0 the ship dies. $DA46 (a
  death): `<$BA` = $58, the ship erased, the wave saved, sound $D4B5, a
  white copy of the ship ($DB5C, at $AFDD) flashed with the colours at
  $DB4B into `<$31` (2 ticks each), then `<$BA` = $7F, a white screen
  (`<$26` = $FF) for 2 ticks; $DAB8: every other process ended, bank 7's
  explosion ($C5D0: 128 particles at $B300, 10 bytes each, velocities
  from two 16-bit LFSRs at `<$04`/`<$06`, feedback bit 1, kept within
  $16A; $C649 moves and draws them each tick, fading by the table at
  $C6AB into `<$31` until it reads 0), resumed through $F4BE / $F4CC (a
  sleep that keeps the bank at +C and the address at +D). $DAC9: the
  scores, sound $13; no enemies left: the wave's end; then the next player
  with ships ($DB15, `<$25` + 1, their turn); when the player has none
  left but the other has, $C0EF / $C0F1 and $C075 for $60 ticks, else game
  over ($DB2C: `<$BA` = $FF, $C075, $28 ticks, $DB48 the attract mode:
  not ported). $DDEC (a wave's end, from the wave's step at $DD02 or a
  death at $DADB): the messages $C0F9, the wave's number (by $CBC1, BCD
  by DAA: it wraps at 100), $C0FB, $C0F3 and the wave (at most 5), then
  per humanoid left (`<$FA`) a glyph $F915, $0400 apart and 4 ticks
  apart ($DE66), each scoring $0100 | wave (at most 5) << 4; then the next wave set up ($DE7C) and $80 ticks
  ($DE79): from the wave, the player's ships + 1 and a new turn; from a
  death, the playfield cleared and the next player.
- **The number printer** (bank 2, `src/sim/text.c`): $CBC1 prints the
  BCD digits of D at X, leading zeros dropped (0 prints "0"), a digit
  per call through `<$59` like the typewriter.
- **The attract mode and high scores** (bank 1; ported:
  `src/sim/attract.c`). Power-on's last step creates $D825 (type 1),
  which game over's $DB48 also reaches: bank 1 selected, $C006. Bank 1's
  processes start in bank 1 (created at $Cxxx, the bank left selected)
  and sleep through $F4BE / $F4CC (+C the bank, +D the address); their
  text goes through $FFCE (`JSR` then the routine's address and bank
  inline: $F4D3 selects it, calls, and restores the bank), mostly bank
  2's printer ($C002) and number printer ($C00E). $C006: every other
  process ended, `<$BA` = $FF, the stars, `<$A183` = the credits; with
  `<$B7` clear (power-on) the logo, else for each player block ($A1C2,
  $A1FF; `<$06` 1, 2) whose score (+1..+3) beats today's eighth best
  ($C147: the first two bytes, then the third): the screen cleared, the
  interrupt's handler set (`<$8F-<$91` = JMP $DF17, or $DFC3 for player
  2 on a cocktail table), palette entry 1 $85, sounds $1B and 1 (2 when it
  beats today's best; written to the sound PIA directly), "PLAYER
  ONE/TWO" ($C0EF / $C0F1 at $3E38) and $C0FD at $1458; the initials at
  `<$00`, `<$02`, `<$04` ('/'-ended, from "A@@"), drawn by $C158 (cleared
  $14 x 8 at $46AC, message $C0FF); bars under them ($C16B, the current
  one $DDDD, others $1111); `<$A17D` = $28 seconds ($C1DC takes one every
  $3C ticks), $C1E7 blinks palette entry 13 every $0F ticks, $C1FA steps
  the current letter (IN0 bit 7 down, IN1 bit 0 up; first after 3 ticks,
  then every `<$A17E` = `<$A17E` / 2 + 5 from $37; '?' wraps to 'Z', '['
  to '@'). Fire, once released for 5 ticks, takes the next letter ($14
  seconds again). After 3 or the time out: every other process ended, the
  entry sorted into today's table (8 entries of 12 nibbles at $B260:
  score bytes 1-3, then the initials; from $B2A8 up, each beaten entry
  moved a place down, $C194) and, if it beats the last there, the all-time
  table in CMOS ($C41D-$C47C, from $C465). With no entry, $FF ticks of
  game over first. The hall of fame ($C263): the logo unpacked ($C86F:
  bank 1 $CAA0-$CC0D, a nibble a run: 2 bits of length (more nibbles with
  no colour add 2 bits each), 2 of colour from $CC0D, pixel pairs into
  $B412 by rows of $3C columns $18 bytes apart), the screen cleared,
  the credits' process, the scores, $C101 at $3854 underlined, both
  tables ($C306: rank '1'-'8' at `<$06`, initials at `<$08`, digits at
  `<$0C` with leading zeros '@', each a line $0A further: today's at
  $1886, all time at $5986), the logo (descriptor $3C18 at $B300) at
  $3038, the laser's colour cycle; $3C x $0A ticks (sooner with a credit,
  `<$A184`), then the demo ($C351): the playfield and terrain at scroll 0,
  `<$BA` = $D9, the HUD's frame, the colour processes, the housekeeping
  and the names ($C64F: bank 1's list at $CC61, one each 6 ticks up to
  `<$A196`); a humanoid, the ship and a lander appearing, the lander
  rising with the humanoid ($E6 ticks), the laser ($C5F5: from the ship's
  screen position + $0704, 4 bytes $11 and a $99 a tick, colours from
  `<$A4`, its tail cleared; $C460 clears it and kills it), the lander
  exploding, the humanoid falling, caught (500 shown), carried down; then
  six enemies (bank 1 $CC7D: lines, pictures +$0C, columns +$18, colours
  +$24) appearing, shot and lined up with their names; $1FE ticks, then
  the logo ($C677): `<$BA` = $FB, the screen cleared, the colour
  processes; "WILLIAMS" drawn ($C6B0, bank 1 $C941: bytes up to $AA two
  moves each (bits 7-4: x - 1, x + 1, y - 1, y + 1, then a pixel), $FE x y
  a jump, $FF nothing, others the end; `<$A16A` bytes a pass every 2
  ticks, 3 then 10; at the first end $C730: message $C0ED at $3258,
  every 5 ticks), after $30 ticks the
  DEFENDER logo's 15 blocks (4 x $C at $B304, 14 bytes apart, their
  descriptors at $B3D6) appear left to right (links left 0: only the last
  is listed), after $2E ticks $C848 draws the whole logo at $3090 until
  both first explosion slots are free, then ($C921) "WILLIAMS" finishes at
  once; after $28 more the copyright's bitmap ($CC11, 40 columns of 8
  lines at $3BD0) and `<$B7` = 1; then $3C x $0A ticks and the hall of
  fame. $C8F4, every 16 ticks with credits: $C0E9 at $28E5 and the number
  at $48E5 (`<$A184` + 1 when they rose). Then $C80B: when palette entry
  [$C6F8] has bits 0-2 set, `<$B8` = `<$A168` (where "WILLIAMS" ended;
  purpose unknown).
- **The frame** (ported: `src/sim/frame.c`). The interrupt comes at lines
  0, 64, 128 and 192 (a few lines late while interrupts are masked); the
  handler ($DF17) works at the first with the beam at or past $80 (`<$92`
  set: the bottom half) and the first after with it above (`<$92`
  cleared: the top half). Top half (line 0): `<$5D` + 1 (the tick), the
  watchdog, the palette shadow `<$26-<$35` to the hardware (beam at line 8
  or above), the coin switches ($D5FD: IN2 & $3F debounced, a new press
  queued from the table at $F8A2), the terrain (bank 7 $C003, not with
  `<$BA` bit 1), then with bank 2 the ship and the objects in the band
  (`<$A2`, `<$A1`] and the objects moved ($E376). Bottom half: the sound
  sequencer, the timers `<$7E` (set to $3C by the coin PIA's CA2 flag),
  `<$7F`, `<$80`, `<$81`, the controls (`<$7B` IN0, `<$7C` the last, `<$7D`
  IN1; a new press queued from $F882), the ship's motion, the stars; then
  the beam is read again, less 8 (at most $A8), into `<$A2`, and the
  objects and ship in (`<$A3`, `<$A2`] and the shots. The main loop's tick
  ($E7C7, when `<$5D` is set): load shedding (ticks missed: one object
  off screen, `<$AE` = 3), bank 2, the ship's collisions, the explosions
  ($FD2D), a random number, the queued switches' processes started (not
  while `<$BA` has a bit of their mask), then the dispatcher. On the board
  the main loop is still running when the bottom half comes in about 70%
  of frames (the top half's drawing takes much of the frame), and the
  split `<$A2` depends on the bottom half's own cycles: the C game runs
  top half, tick, bottom half, with the split at $90. `frame` gives the C
  game the board's split and where in the tick its bottom half came (the
  tick's writes below $A000 before it), and measures how long the two stay
  identical: runs of up to 39 frames, parted by that timing (the flame's
  colours step, a scratch byte) or by what is not ported yet.
- **Explosions and appearances** (ported: `explosions_update`,
  `explosion_draw` in `src/sim/explosions.c`): $FD2D, every tick, for the
  16 slots: with `<$BA` bit 2, explosions stop (not erased) and
  appearances end; an explosion grows $AA a tick (ended past $30, erased)
  and follows the scroll's change in `<$22`/`<$20` & $FFC0; an appearance
  counts down $100 a tick (done at 0, or off screen: the object gets its
  picture back, flags & ~2), its corner and centre following the object.
  $FDEF draws a slot: the picture's columns from the centre at size
  (`+0 & $7F`) apart, its rows in pairs (a word each) 2 x size lines apart
  from the first row at line $2A or below, through the unrolled code at
  [$FF48 + rows & $FE] and an odd row by [`<$ED`]; columns past $98 and
  rows past line $FF are left out; every word drawn is listed down from
  +$40, and at the end the centre's word is cleared. With no pairs left,
  the odd row depends on the carry the ROM left (from `ASL <$F5`, then
  `CMPA #$98`).
- **The terrain** (bank 7; ported: `src/sim/terrain.c`): its shape is
  the bit stream at $C350-$C44F, a bit per 32 world units (1 down, 0 up).
  Cursors for the screen's left edge (`<$09` byte, `<$0F` bit, `<$0D` the
  byte rotated, `<$11` its line) and right (`<$0B`, `<$10`, `<$0E`,
  `<$12`) walk it; two circular buffers of 152 entries (line, colour word
  $7007 up / $0770 down; a copy $1C8 on) at $B700 (`<$05`) and $BA90
  (`<$07`) serve the two halves of each step (scroll bit 5). $C006 ($C262)
  builds the heights at $B300-$B6FF (a line per 64 units, from $E0);
  $C000 ($C015) the cursors and buffers for the scroll (`<$17`, `<$15`
  the scroll they are at); $C003 ($C092, in the interrupt) moves them with
  the scroll a step at a time and draws columns $98 down to 1, each
  clearing its last pixel pair (addresses at $BE20-$BF4F), S kept at
  `<$13-<$14`; $C009 ($C29A) clears the last drawing. The variables live at
  $A000-$A017.
- **The interrupt's routines** (ported: `src/sim/irq.c`): $E39F the
  objects in a band of lines (bottom, top]: an object drawn there is
  erased (+4 = 0; one drawn outside is left alone entirely), one whose line
  is in it, on screen ($2580 units) and not past column $9C (8-bit sum
  with its width) is drawn at ((x - scroll) << 2, line), odd x by bit 7.
  $E213 the ship in the band: its 8 x 6 and its flame erased ($E25E,
  $E1F0 facing right / $E1CD left), `<$BD` = `<$BB`, drawn again at
  `<$C1-<$C2` ($F9C1 / $F9CB, odd by `<$C4` bit 7) with its flame from
  `<$9F`'s colours ($E15C / $E19B; the full flame while thrust is held).
  $E07E the stars: 16 at $AF9D (column, line, colour), cleared, moved at
  half the scroll (`<$6F`; past column $9C: $00 up to $C0, else $9B) and
  drawn masked by `<$71` ($F0 or $0F by scroll bit 6), `<$AE` of them (0:
  256, over RAM); one star's colour + $11 & $77; on an even `<$DF` one star
  moves to column `<$DF` (- $84 from $98 on; at exactly $A0, when the word
  at $A0B8 is not $6245 and `<$BA` bit 7 is clear, `<$E0` is written at
  ($A0, `<$E1`)), a random line without terrain. $E453 the shots: moved by
  their speed and the scroll (`<$9D`), redrawn by their +12 routine ($E498
  mines, two frames from `<$A6`; $E4D9 bombs by pixel parity from `<$A8`);
  at line $2A or column $98 erased with +16 cleared. A shot touching the
  ship ($E51E): 25 points, freed, $F951 exploding, script $D4E4.
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
- **Hit test** (ported: `src/sim/hit.c`; $E6BA, D = a screen position,
  U = a picture descriptor, X = the list, $A065 by default): bounding
  boxes against each drawn object (its +4 screen position and its
  picture's size), then the two pictures' even data byte by byte where
  they overlap (rows counted down with DECB/BPL: signed); on a hit it
  leaves the hit byte's screen address in `<$F8`, calls the object's +8
  routine (its hit handler) and returns A = 1. Scratch: `<$73`,
  `<$CE-<$CF`, `<$D0-<$DD`. The ship's collisions ($E861) use it too.
- **Hit handlers** (ported: `src/sim/enemies.c`): lander $F20B
  (`<$A112` - 1, 150 points, script $D507), mutant $EF6D (`<$A115` - 1,
  150, $D4F8), a lander carrying a humanoid $F1E0 (the humanoid falls:
  process $F216 with +7 the humanoid, script $D4E9, then as $F20B). The
  kill $F416 (the score and script follow the call): the owner process
  ends ($D013), the block is freed ($D0C7; not by $F40D), $D360 scores,
  $F434 erases it and starts the explosion ($FC63), the script plays.
- **Humanoids** (ported: `src/sim/humanoids.c`). One process ($ECC9,
  every 2 ticks) walks them, a table slot a step over the first 16 slots:
  a humanoid on screen with handler $ED70 walks 2 pixels and steps its
  picture ($F901-$F90B going left, $F915-$F91F right), following the
  terrain a line a step (4 lines above it going left, 15 going right,
  never below line $E8); `<$DF` <= 8 turns it. Hit handlers: $ED70 on the
  ground (the laser kills it; the ship, `<$DE` set, passes: the handler
  returns A = 0 by popping the hit test's return), $ED91 carried or
  falling (owner 0: as $ED70; the laser kills it and its owner process;
  the ship catches it: script $D4DA, process $EE73, the owner resumes at
  $F24C; to port with the ship's collisions). Falling ($F216, every 4
  ticks): 8 faster a step up to $300; on the terrain it dies above $E0
  ($ED77, hit point +4 + $0107), else it lands ($EE65). Carried by the
  ship ($F24C, every tick): 10 lines below the ship's y, $80 units on,
  until the terrain: set down ($EE73). Landing and setting down: owner 0,
  handler $ED70, script $D4DF. $EE65 / $EE73: a bonus object (250 $F9DD /
  500 $F9E7, handler $EDBC) at the humanoid, $1800 lower (above line $80)
  or $2000 higher, moving at the ship's speed, flags $11; 50 ticks, then
  freed ($EEB2).
- **Score** (ported: `src/sim/score.c`): $D360 adds a points word to the
  player block ($D6FE: `<$8B` 1 → $A1C2, else $A1FF; +0..+3 BCD score,
  +4..+6 the next extra life, +7 ships, +9 smart bombs): the high byte's
  low bit shifts the low byte up a digit, its other bits pick the byte
  pair; BCD with DAA; `<$8A` gets a 1 shifted in. Every [`<$AB-<$AC`]
  points (BCD; 0 = none) a ship and a smart bomb, the HUD redrawn, script
  $D4B0 and bank 3's $C00F (counter 5, bank 3 left selected); the next
  life's score +4..+5 moves on by `<$AB-<$AC` (+6 kept), once a call. $D3D9 draws the 6 digits ($0F1C for
  player 1, $711C for 2), 4 columns apart, from bank 2's font ($C000
  points at '0'; $F522 copies, $F57B clears leading zeros but the last 2).
- **Reverse** ($E897, once per press; ported in `player.c`): `<$BB` = -`<$BD`; the ship turns
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
  reads): it must match with the interrupt's writes all before or all
  after the C step: the C step reads the state from before or from after
  the interrupt, and each byte keeps the later write in the ROM's order;
  or (long steps such as the scanner) each interrupt's writes land in a
  rerun of the C step after as many of its raw writes as the ROM's step
  had made (`Game.on_write`). When none matches and the bytes that differ
  include some the interrupt wrote, the ROM read part of the state before
  the interrupt (into registers) and part after: a race the C game, which
  never interrupts a routine, cannot have; counted, at most 1 in 1000
  steps (1 in the scripted minute, in a ship collision). Calls from the
  main loop ($E861) are followed the same way, until they return.
  The scripted minute: all 3611 steps match (9 interrupted); `game_hunter`
  (rapid fire, sweeping, 12000 frames): all 12850 (19 interrupted, 3 on
  paths not ported: a humanoid landing safely). Not reached yet: a lander
  leaving, the planet going, a mutant's jitter wrapping. Limit: a byte the C step does
  not write goes unnoticed when the ROM writes the value it already held.
- **Interrupt routines** (label `rom`): `game` also checks every call of a
  ported interrupt routine: a model loaded from RAM at the call runs the C
  routine, and all of RAM must match when the ROM's returns (no
  interleaving: interrupts are masked inside). The scripted minute: 8937
  object-band draws, 8937 ship draws, 4469 star frames, 4469 shot moves,
  2495 terrain updates; the ship's collisions at each of the 4204 ticks.
- **The C game on its own** (label `rom`): `frame` loads the C game from
  the board's RAM at an idle frame boundary and runs both on the same
  controls, comparing all of RAM after every frame (the board's split and
  bottom-half position given); at least 1000 frames must match (1978 in
  the scripted game: runs up to 39 frames, parted by timing or by
  unported code: the attract mode, the ship's death, the player's start,
  hyperspace).
- **Routines on chosen inputs** (label `rom`): `routines` runs a ROM
  routine on the emulated CPU (interrupts masked) until it returns and the
  C one on a model of the same RAM; all of RAM must match. Process steps
  the same way, from a state built in the model, until the ROM's step is
  back at the dispatcher. So far `score_add` ($D360): 5000 random scores,
  points, players and extra-life intervals (extra lives skipped); the
  humanoid steps (falling at the $E0 edge, carried by the ship, walking on
  any terrain, the bonuses at any line): about 1100 cases; the wave's
  steps with random counts, timers and settings (landers, mutants and
  baiters arriving, the ramp, few enemies left): 2000; baiter steps
  around the ship (its $280 and 10-line edges): 2000; every arrival
  routine called on the ROM and in C from random states (3000), then the
  steps of the swarmers, bombers and baiters made (about 2700) and every
  hit handler on them (about 5600); the ship's collisions among enemies,
  humanoids (falling, caught) and shots (3000; with bank 2 selected, as
  the main loop does); the explosions' tick on real explosions and
  arrivals at random sizes, pictures and scrolls (3000); coins, a
  player's wave set-up and starts with random operator settings, CMOS
  compared too (about 1000 each). The C model reads
  banks 1, 2, 3 and 7 for their tables (`Game.bank`).
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
| I1 | (started 2026-10-21) The interrupt's work in C: objects, ship, stars and shots drawn (`irq.c`), the terrain (`terrain.c`); the frame assembled (`frame.c`: both halves, the main loop's tick, the controls and coins, the explosions' tick) | `game` (every call in both scripted games), `routines` |
| S1 | (started 2026-10-21) Process steps in C on a model of all of RAM (`src/sim/game.c`): the smart bomb, reverse and HUD icons (`player.c`), the background processes and the scanner (`background.c`), the wave's process and arrivals (`wave.c`), landers and mutants with their arrival (`lander.c`), baiters (`baiter.c`), pods and swarmers (`pods.c`), bombers (`bombers.c`), enemy shots (`shots.c`), humanoids walking, dying, falling, landing, carried by the ship, their bonuses (`humanoids.c`), starting explosions (`explosions.c`), the laser as a process with the hit test (`hit.c`), hit handlers and kills (`enemies.c`), scoring (`score.c`); hyperspace, a death and its explosion, a wave's end and its bonus, the next player and game over (`death.c`) | `game`, `game_hunter`, `routines` (with sequences: wave ends and deaths followed step by step) |
| X1 | (started 2026-10-20) Extractor: object pictures, font, logos, palette and sounds (done); the small font, terrain, scanner and the game's tables as the C game needs them | `extract`; the C game runs from `assets/` alone |
| C0 | (started 2026-10-20; notes in 2.2) Reverse engineering: the object list and block layout, the main loop and its processes, random numbers, waves and their tables, scoring; notes in section 2 | Each routine named and its RAM documented |
| C1 | C game, first wave: ship, terrain, scanner, landers and humanoids, lasers, explosions, smart bomb, hyperspace, scoring, HUD | Side-by-side test against the emulated board over a scripted first wave |
| C2 | Remaining enemies (mutants, bombers, pods and swarmers, baiters), wave flow, planet destruction, extra lives | Side-by-side test over several waves |
| C3 | (started 2026-10-21) Attract mode, high scores and initials (`attract.c`: done), operator pages, 2 players; sounds from the WAVs; the C game becomes the default | Full scripted run matches; release with `assets/` only |

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
