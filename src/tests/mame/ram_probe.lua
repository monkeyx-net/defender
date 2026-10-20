-- MAME probe: work RAM at chosen frames, with scripted input and optional pokes.
-- Only dumps (and pokes RAM for cheats); C does the analysis.
--
--   PROBE_OUT=<dir> PROBE_FIRST=1300 PROBE_EVERY=30 PROBE_LAST=6000 \
--   PROBE_SCRIPT="1200:coin 1215: 1300:start 1315:" PROBE_POKE="A0BC=05" \
--   mame defender -autoboot_script ram_probe.lua
--
-- Writes <dir>/ram.txt: "frame <n>" then lines "ADDR <64 hex bytes>" for
-- $9800-$BFFF, every PROBE_EVERY frames from PROBE_FIRST to PROBE_LAST.
-- PROBE_POKE ("ADDR=VV ...", hex) is written every frame from PROBE_POKE_FROM
-- on (e.g. lives, once a game is running).
-- Inputs as in capture_probe.lua: coin, start, start2, up, down, thrust, reverse,
-- fire, bomb, hyper.

local out_dir = os.getenv("PROBE_OUT") or "."
local first = tonumber(os.getenv("PROBE_FIRST") or "0")
local every = tonumber(os.getenv("PROBE_EVERY") or "60")
local last = tonumber(os.getenv("PROBE_LAST") or "600")

local script = {}
for f, what in string.gmatch(os.getenv("PROBE_SCRIPT") or "", "(%d+):([%w,]*)") do
    table.insert(script, { frame = tonumber(f), inputs = what })
end
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

local space = manager.machine.devices[":maincpu"].spaces["program"]
local out = assert(io.open(out_dir .. "/ram.txt", "w"))
local frame, held = 0, {}

_G.ram_frames = emu.add_machine_frame_notifier(function()
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
    if frame >= poke_from then
        for _, p in ipairs(pokes) do space:write_u8(p.addr, p.value) end
    end
    if frame >= first and (frame - first) % every == 0 then
        out:write(string.format("frame %d\n", frame))
        for a = 0x9800, 0xBFFF, 64 do
            local row = {}
            for k = 0, 63 do row[#row + 1] = string.format("%02X", space:read_u8(a + k)) end
            out:write(string.format("%04X %s\n", a, table.concat(row)))
        end
    end
    if frame >= last then
        out:close()
        manager.machine:exit()
    end
end)
