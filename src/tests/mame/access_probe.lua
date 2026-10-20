-- RE helper (not a test): who reads and writes some addresses. Logs, per
-- address and access type, each program counter with a count, over a
-- scripted run. Only dumps.
--
--   PROBE_OUT=access.txt PROBE_RANGES="A000-A001 A0BC" PROBE_FROM=1300 PROBE_LAST=6000 \
--   PROBE_SCRIPT="1200:coin 1215: 1300:start 1315:" mame defender -autoboot_script access_probe.lua
--
-- Output lines: "R|W addr pc count", sorted. Scripted input, PROBE_CYCLE and
-- PROBE_POKE / PROBE_POKE_FROM work as in capture_probe.lua.

local out_path = os.getenv("PROBE_OUT") or "access.txt"
local from = tonumber(os.getenv("PROBE_FROM") or "0")
local last = tonumber(os.getenv("PROBE_LAST") or "600")

local script = {}
for f, what in string.gmatch(os.getenv("PROBE_SCRIPT") or "", "(%d+):([%w,]*)") do
    table.insert(script, { frame = tonumber(f), inputs = what })
end
local cycle, cycle_len = {}, 0
for what, n in string.gmatch(os.getenv("PROBE_CYCLE") or "", "([%a,]*):(%d+)") do
    table.insert(cycle, { inputs = what, frames = tonumber(n) })
    cycle_len = cycle_len + tonumber(n)
end
local cycle_from = tonumber(os.getenv("PROBE_CYCLE_FROM") or "0")
local poke_from = tonumber(os.getenv("PROBE_POKE_FROM") or "0")
local pokes = {}
for a, v in string.gmatch(os.getenv("PROBE_POKE") or "", "(%x+)=(%x+)") do
    table.insert(pokes, { addr = tonumber(a, 16), value = tonumber(v, 16) })
end

local fields = {}
for _, port in pairs(manager.machine.ioport.ports) do
    for name, field in pairs(port.fields) do fields[name] = field end
end
local NAMES = { coin = "Coin 1", start = "1 Player Start", start2 = "2 Players Start",
                up = "P1 Up", down = "P1 Down", thrust = "Thrust", reverse = "Reverse",
                fire = "Fire", bomb = "Smart Bomb", hyper = "Hyperspace" }

local cpu = manager.machine.devices[":maincpu"]
local space = cpu.spaces["program"]
local frame = 0
local counts = {}
local function note(kind, addr)
    if frame < from then return end
    local key = string.format("%s %04X %04X", kind, addr, cpu.state["PC"].value)
    counts[key] = (counts[key] or 0) + 1
end

-- Kept in globals: MAME removes a tap once its handle is garbage-collected.
_G.access_taps = {}
for a, b in string.gmatch(os.getenv("PROBE_RANGES") or "", "(%x+)%-?(%x*)") do
    local first = tonumber(a, 16)
    local lastaddr = b ~= "" and tonumber(b, 16) or first
    table.insert(_G.access_taps, space:install_read_tap(first, lastaddr, "access_r" .. a, function(offset, data, mask)
        note("R", offset)
    end))
    table.insert(_G.access_taps, space:install_write_tap(first, lastaddr, "access_w" .. a, function(offset, data, mask)
        note("W", offset)
    end))
end

local held = {}
_G.access_frames = emu.add_machine_frame_notifier(function()
    frame = frame + 1
    for _, s in ipairs(script) do
        if s.frame == frame then
            held = {}
            for what in string.gmatch(s.inputs, "%a+") do held[what] = true end
        end
    end
    if cycle_len > 0 and frame >= cycle_from then
        local t = (frame - cycle_from) % cycle_len
        for _, c in ipairs(cycle) do
            if t < c.frames then
                held = {}
                for what in string.gmatch(c.inputs, "%a+") do held[what] = true end
                break
            end
            t = t - c.frames
        end
    end
    for what, name in pairs(NAMES) do
        if fields[name] then fields[name]:set_value(held[what] and 1 or 0) end
    end
    if frame >= poke_from then
        for _, p in ipairs(pokes) do space:write_u8(p.addr, p.value) end
    end
    if frame >= last then
        local keys = {}
        for k in pairs(counts) do keys[#keys + 1] = k end
        table.sort(keys)
        local out = assert(io.open(out_path, "w"))
        for _, k in ipairs(keys) do out:write(string.format("%s %d\n", k, counts[k])) end
        out:close()
        manager.machine:exit()
    end
end)
