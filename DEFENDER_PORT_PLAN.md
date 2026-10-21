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
  minute) bank 3's $C00F adds to a CMOS bookkeeping counter (B = 6; to
  port with the CMOS) and leaves bank 3 selected. Every 40 steps ($DEEC,
  B = 2) the settings from $A0FB ramp: bank 7's table at [$C011]-[$C013],
  8 bytes per setting (max, min, deltas by column B), kept within. Wave
  start ($DC1E, to port): the humanoid walker, the table cleared, the
  humanoids placed ($DBE1: groups of a quarter at x $00/$40/$80/$C0 high,
  the rest at random; line $E0, flags $10, picture $F901 or $F915), the
  player block's wave settings (+B on) copied to $A0FB-$A111, $A112-$A119
  cleared, then $EB9E, $EF15 (mutants carried over), $EB36, $F29D.
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
- **Erasing**: each picture's erase routine (+8 of its descriptor) clears
  its width x height, as `game_erase_object`; the hidden picture's ($F8EC,
  routine $D8DB) is a bare RTS (`routines` checks all of them).
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
  $D4B0 and bank 3's $C00F (to port). $D3D9 draws the 6 digits ($0F1C for
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
  had made (`Game.on_write`).
  The scripted minute: all 3611 steps match (9 interrupted); `game_hunter`
  (rapid fire, sweeping, 12000 frames): all 12850 (19 interrupted, 3 on
  paths not ported: a humanoid landing safely). Not reached yet: a lander
  leaving, the planet going, a mutant's jitter wrapping. Limit: a byte the C step does
  not write goes unnoticed when the ROM writes the value it already held.
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
  hit handler on them (about 5600). The C model reads
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
| S1 | (started 2026-10-21) Process steps in C on a model of all of RAM (`src/sim/game.c`): the smart bomb, reverse and HUD icons (`player.c`), the background processes and the scanner (`background.c`), the wave's process and arrivals (`wave.c`), landers and mutants with their arrival (`lander.c`), baiters (`baiter.c`), pods and swarmers (`pods.c`), bombers (`bombers.c`), enemy shots (`shots.c`), humanoids walking, dying, falling, landing, carried by the ship, their bonuses (`humanoids.c`), starting explosions (`explosions.c`), the laser as a process with the hit test (`hit.c`), hit handlers and kills (`enemies.c`), scoring (`score.c`) | `game`, `game_hunter`, `routines` |
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
