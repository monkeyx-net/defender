-- MAME probe: the main CPU's view of program ROM, as hex lines in the format
-- of `defender --dump-rom`: "B:ADDR BYTE" for $C000-$CFFF with each ROM bank
-- B (1-7) selected by a write to $D000, then "ADDR BYTE" for the fixed ROM at
-- $D000-$FFFF. Bank 0 (the I/O page) is not read.
--
--   PROBE_OUT=out.txt mame defender -autoboot_script rom_map_probe.lua ...

local out_path = os.getenv("PROBE_OUT") or "rom_map_probe.txt"

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]

local out = assert(io.open(out_path, "w"))
for bank = 1, 7 do
    space:write_u8(0xD000, bank)
    for addr = 0xC000, 0xCFFF do
        out:write(string.format("%d:%04X %02X\n", bank, addr, space:read_u8(addr)))
    end
end
for addr = 0xD000, 0xFFFF do
    out:write(string.format("%04X %02X\n", addr, space:read_u8(addr)))
end
out:close()
manager.machine:exit()
