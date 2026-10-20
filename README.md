# Defender - native SDL2 port (work in progress)

A C11 / SDL2 port of the 1981 Williams arcade game for Linux handhelds
(PortMaster: ArkOS, ROCKNIX, muOS, Knulli), desktops (Linux, Windows) and the
web, checked against the original ROM running in MAME.

**Status:** playable. The game is your ROM's own code on an emulated Williams
board (6809 main board with banked ROM, 6808 sound board), written in C and
checked against MAME instruction for instruction; the port adds enhanced
graphics presets, pad controls, CMOS saving and packaging for handhelds,
desktops and the web. See [DEFENDER_PORT_PLAN.md](DEFENDER_PORT_PLAN.md).

You need your own MAME `defender` ROM set (`defender.zip`, red label); none is included.

```bash
cmake -S . -B build && cmake --build build -j
build/defender --windowed --rom /path/to/defender.zip
```
