-- MAME probe: palette, RAM and the sound board's RAM at chosen frames, with
-- scripted input. Only dumps; C does the decoding and comparing.
--
--   PROBE_OUT=<dir> PROBE_FRAMES="600 1500" PROBE_LAST=1800 \
--   PROBE_SCRIPT="1200:coin 1215: 1300:start 1400:thrust,fire ..." mame defender -autoboot_script capture_probe.lua
--
-- PROBE_CYCLE="thrust,fire:4 thrust:10 up:6 down:6" repeats inputs (each
-- held for its frame count) from PROBE_CYCLE_FROM on, after the script.
-- PROBE_POKE="A0BC=05" writes RAM every frame from PROBE_POKE_FROM on (a
-- cheat once a game runs; poking before a game starts upsets the attract mode).
--
-- Writes into PROBE_OUT, at each PROBE_FRAMES frame:
--   frame_<n>.txt      "palette" + 16 bytes, "vram <col>" + 256 bytes for each
--                      of the 192 two-pixel columns ($0000-$BFFF: all of RAM,
--                      the screen being columns 0-151), "bank" + the bank
--                      last written to $D000, "sound" + the sound board's
--                      128 bytes of RAM
--   and a MAME snapshot (snap/defender/NNNN.png, in order)

local out_dir = os.getenv("PROBE_OUT") or "."
local last_frame = tonumber(os.getenv("PROBE_LAST") or "1800")

local snap_frames = {}
for n in string.gmatch(os.getenv("PROBE_FRAMES") or "", "%d+") do snap_frames[tonumber(n)] = true end

-- "frame:input,input frame:..." where input is coin, start, start2, up, down,
-- thrust, reverse, fire, bomb, hyper; each entry holds until the next one.
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

local space = manager.machine.devices[":maincpu"].spaces["program"]
local sound = manager.machine.devices[":soundcpu"].spaces["program"]
local vram = manager.machine.memory.shares[":videoram"]
local pal = manager.machine.memory.shares[":paletteram"]

local bank = 0
-- Kept in a global: MAME removes a tap once its handle is garbage-collected.
_G.capture_bank_tap = space:install_write_tap(0xD000, 0xDFFF, "capture_bank", function(offset, data, mask)
    bank = data & 0x0F
end)

local function dump_frame(n)
    local f = assert(io.open(string.format("%s/frame_%d.txt", out_dir, n), "w"))
    local p = {}
    for i = 0, 15 do p[#p + 1] = string.format("%02X", pal:read_u8(i)) end
    f:write("palette " .. table.concat(p, " ") .. "\n")
    for col = 0, 191 do
        local row = {}
        for y = 0, 255 do row[#row + 1] = string.format("%02X", vram:read_u8(col * 256 + y)) end
        f:write(string.format("vram %d ", col) .. table.concat(row) .. "\n")
    end
    f:write(string.format("bank %d\n", bank))
    local s = {}
    for a = 0, 127 do s[#s + 1] = string.format("%02X", sound:read_u8(a)) end
    f:write("sound " .. table.concat(s) .. "\n")
    f:close()
end

local frame, held = 0, {}
_G.capture_frames = emu.add_machine_frame_notifier(function()
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
    if snap_frames[frame] then
        dump_frame(frame)
        manager.machine.video:snapshot()
    end
    if frame >= last_frame then manager.machine:exit() end
end)
