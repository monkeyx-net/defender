Defender (native SDL2 build)
============================

Converted by monkeyx.

Run ./defender, or defender.exe on Windows (add --windowed for a window on a
desktop). The game is rewritten in C from the arcade's (--emulate runs your
ROM's own code on an emulated Williams board instead); the port adds the
graphics presets, pad controls and saving.
On the first start the arcade restores its factory settings (a moment's
work, done before the window opens). Your settings and high scores (the
arcade's CMOS) are saved as defender.cmos in $DEFENDER_SAVE_DIR, else
$XDG_DATA_HOME/defender, %APPDATA%\defender on Windows, or ~/.local/share/defender.
./defender --test-card shows the ROM status and an input tester.
In the attract mode, when nobody touches the controls for 30 seconds, a
page lists the controls (and comes back every 40 seconds); any button
hides it.

ROM set
  The game needs your own MAME defender ROM set (red label). Put
  defender.zip (or a defender/ folder) next to the program, set DEFENDER_ROM
  to its path, or start with --rom /path/to/defender.zip. FBNeo sets and old file names work too: ROMs
  are matched by checksum. Check a set with: ./defender --check-rom defender.zip
  ./defender --extract defender.zip writes the game's pictures, font, logos
  and sounds from your set into assets/ next to the program (the PortMaster
  launcher does this on the first run). The game plays its sounds from
  there; it still reads its tables from the ROM set (later only assets/).

Controls (one control panel: the players take turns, as on the cabinet)
  Up/down: left stick / D-pad; keyboard Up Down
  Fire: A; keyboard Space or Ctrl
  Thrust: B or R1; keyboard X or Alt
  Reverse: L1; keyboard Z
  Smart bomb: X; keyboard C
  Hyperspace: Y; keyboard V or Shift
  Coin: Select / 5
  Start: Start / 1 or Enter;  2-player start: L1+Start / 2
  Operator menu: Select+R1 / F2 (the cabinet's Advance button); in its
    pages it steps to the next, and up/down change the value shown
  Reset today's high scores: F6 (the cabinet's High Score Reset button,
    in the attract mode)
  Graphics: Select+L1 / F3 steps the presets, F4 glow, F5 CRT scanlines
  Screen: Select+R2 / F7 steps the scale mode, Select+L2 / F8 the aspect
  Exit: hold Select+Start for 1 second / Esc
  Every connected pad drives the panel.

Graphics presets (--gfx <preset>)
  classic, classic-crt, crisp, smooth, hd-crisp, hd-smooth, hd-glow, hd-glow-crt
  (in the game the hd- presets upscale the whole picture, like crisp/smooth)

Screen (--scale <mode>, --aspect 4:3|square)
  auto     whole-number scaling if it fills at least 95% of the screen, else fit
  integer  a whole number of screen rows per arcade row
  fit      as large as fits (sizes between whole numbers stay sharp)
  stretch  the whole screen, whatever its shape
  4:3 is the shape of the arcade's monitor (the default); square keeps square
  pixels. The preset, scale and aspect are saved (defender.cfg) and come back
  next time; the command line overrides them.

See NOTICE for the third-party material this project refers to.
