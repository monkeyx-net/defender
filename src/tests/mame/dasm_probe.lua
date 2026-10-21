-- RE helper (not a test): MAME's disassembly of the main CPU's ROM, for
-- reading the game's code. Writes text files only, to DASM_OUT (default
-- build/re); never commit them, they hold the ROM's code.
--
--   DASM_OUT=build/re mame defender -debug -debugger none -video none -sound none \
--     -seconds_to_run 30 -autoboot_script src/tests/mame/dasm_probe.lua
--
-- Writes d000.asm ($D000-$FFFF) and bank<N>.asm ($C000-$CFFF with ROM bank
-- N = 1, 2, 3, 7 selected; the debugger disassembles each before the next
-- bank switch, one bank per frame). DASM_EXTRA="1:ccad:353,7:c000:100"
-- also disassembles from those starts (where data ahead misaligns the
-- whole-bank listing), to bank<N>_<start>.asm. The game's own bank switching resumes
-- once the probe exits; the probe exits some frames later, after the
-- debugger has run its commands.

local out = os.getenv("DASM_OUT") or "build/re"
local dbg = manager.machine.debugger
local space = manager.machine.devices[":maincpu"].spaces["program"]
local banks = { 1, 2, 3, 7 }
local extra = {}
for b, a, n in string.gmatch(os.getenv("DASM_EXTRA") or "", "(%x+):(%x+):(%x+)") do
    extra[#extra + 1] = { tonumber(b), a, n }
end
local frame = 0
_G.dasm_frames = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    if frame == 5 then
        dbg:command("dasm " .. out .. "/d000.asm,d000,3000")
    elseif frame >= 6 and frame < 6 + #banks then
        local n = banks[frame - 5]
        space:write_u8(0xD000, n)
        dbg:command("dasm " .. out .. "/bank" .. n .. ".asm,c000,1000")
    elseif frame >= 6 + #banks and frame < 6 + #banks + #extra then
        local e = extra[frame - 5 - #banks]
        space:write_u8(0xD000, e[1])
        dbg:command("dasm " .. out .. "/bank" .. e[1] .. "_" .. e[2] .. ".asm," .. e[2] .. "," .. e[3])
    elseif frame == 60 then
        manager.machine:exit()
    end
end)
