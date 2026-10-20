-- MAME probe: instruction traces of both CPUs over chosen frames of a
-- scripted run (needs -debug -debugger none). Only dumps; test_emu compares.
--
--   PROBE_OUT=<dir> TRACE_FROM=1600 TRACE_TO=1660 PROBE_SCRIPT="1200:coin ..." \
--   mame defender -debug -debugger none -autoboot_script trace_probe.lua
--
-- Writes <dir>/main.tr ("<total cycles> <PC>: <disassembly>" per instruction)
-- and <dir>/sound.tr ("<PC>: <disassembly>"; MAME does not stamp the sound
-- CPU's lines with cycles). Traces hold disassembled ROM code: they stay in
-- the build folder, never commit them. Inputs as in capture_probe.lua.

local out_dir = os.getenv("PROBE_OUT") or "."
local from = tonumber(os.getenv("TRACE_FROM") or "1600")
local to = tonumber(os.getenv("TRACE_TO") or "1660")

local script = {}
for f, what in string.gmatch(os.getenv("PROBE_SCRIPT") or "", "(%d+):([%w,]*)") do
    table.insert(script, { frame = tonumber(f), inputs = what })
end
local fields = {}
for _, port in pairs(manager.machine.ioport.ports) do
    for name, field in pairs(port.fields) do fields[name] = field end
end
local NAMES = { coin = "Coin 1", start = "1 Player Start", start2 = "2 Players Start",
                up = "P1 Up", down = "P1 Down", thrust = "Thrust", reverse = "Reverse",
                fire = "Fire", bomb = "Smart Bomb", hyper = "Hyperspace" }

local dbg = manager.machine.debugger
local frame, held = 0, {}
_G.trace_frames = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    for _, s in ipairs(script) do
        if s.frame == frame then
            held = {}
            for what in string.gmatch(s.inputs, "%a+") do held[what] = true end
        end
    end
    for what, name in pairs(NAMES) do
        if fields[name] then fields[name]:set_value(held[what] and 1 or 0) end
    end
    if frame == from then
        dbg:command('trace ' .. out_dir .. '/main.tr,maincpu,noloop,{tracelog "%d ",totalcycles}')
        dbg:command('trace ' .. out_dir .. '/sound.tr,soundcpu,noloop')
    elseif frame == to then
        dbg:command("trace off,maincpu")
        dbg:command("trace off,soundcpu")
    elseif frame == to + 2 then
        manager.machine:exit()
    end
end)
