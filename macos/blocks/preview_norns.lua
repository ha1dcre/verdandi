-- preview_norns.lua: a small norns for the screen preview of the script builder.
--
-- runs in the browser (fengari) before the script: the screen draws into the preview's canvas
-- (__screen, preview.js), the clock is a scheduler preview.js ticks 60 times a second, params
-- keeps controls, and the engine and MIDI only report what they would play. the real norns
-- libraries (util, tabutil, musicutil, sequins, ui) load next to this file.
local S, host = __screen, __host
package.path = __lib .. "?.lua"

print = function(...)
  local parts = {}
  for i = 1, select("#", ...) do parts[#parts + 1] = tostring((select(i, ...))) end
  host:print(table.concat(parts, "\t"))
end

util = require "util"
tab = require "tabutil"

-- screen: every call goes to the canvas
screen = {}
for _, k in ipairs({"clear", "update", "level", "line_width", "line_cap", "line_join", "aa",
    "move", "move_rel", "line", "line_rel", "curve", "curve_rel", "arc", "circle", "rect",
    "close", "stroke", "fill", "pixel", "font_size", "font_face", "rotate", "translate", "save",
    "restore", "blend_mode", "invert"}) do
  screen[k] = function(...) S[k](S, ...) end
end
for _, k in ipairs({"text", "text_right", "text_center"}) do
  screen[k] = function(s) S[k](S, tostring(s)) end
end
screen.text_extents = function(s) return S:text_width(tostring(s)), 6 end
screen.text_trim = function(s) return tostring(s) end
screen.ping = function() end

-- engine: PolyPerc's commands; notes show in the preview, sound comes from Run
local engine_commands = {hz = 1, amp = 1, pw = 1, release = 1, cutoff = 1, gain = 1, pan = 1}
engine = setmetatable({}, {__index = function(_, k)
  if engine_commands[k] then
    return function(v) host:engine(k, v) end
  end
  return function() end
end})

controlspec = {new = function(min, max, warp, step, default, units)
  return {minval = min, maxval = max, warp = warp, step = step, default = default or min, units = units}
end}

-- params: controls and numbers, with clock_tempo like norns
local values, info = {clock_tempo = 120}, {clock_tempo = {name = "tempo",
  controlspec = controlspec.new(1, 300, "lin", 0, 120)}}
params = {}
function params:add_control(id, name, spec)
  info[id] = {id = id, name = name or id, controlspec = spec or controlspec.new(0, 1, "lin", 0, 0)}
  values[id] = info[id].controlspec.default
end
function params:add_number(id, name, min, max, default)
  self:add_control(id, name, controlspec.new(min, max, "lin", 1, default or min))
end
function params:add_separator() end
function params:add_group() end
function params:bang() end
function params:lookup_param(id)
  return info[id] or error("no control " .. tostring(id), 2)
end
function params:get(id)
  if values[id] == nil then error("no control " .. tostring(id), 2) end
  return values[id]
end
function params:set(id, v)
  local p = self:lookup_param(id)
  if type(v) ~= "number" then error("control " .. id .. " set to a " .. type(v), 2) end
  values[id] = util.clamp(v, p.controlspec.minval, p.controlspec.maxval)
  host:param(id, values[id])
end
function params:delta(id, d)
  local p = self:lookup_param(id)
  self:set(id, values[id] + d * (p.controlspec.maxval - p.controlspec.minval) / 100)
end

-- clock: coroutines woken by _preview_tick, in seconds and in beats of the tempo
local now, beats, threads, next_id = 0, 0, {}, 1
local function beat_sec() return 60 / values.clock_tempo end
local function resume(id, ...)
  local t = threads[id]
  if not t then return end
  local ok, kind, v = coroutine.resume(t.co, ...)
  if not ok then
    threads[id] = nil
    error(kind, 0)
  end
  if coroutine.status(t.co) == "dead" then
    threads[id] = nil
  elseif kind == "sleep" then
    t.wake = now + math.max(0, v or 0)
  elseif kind == "sync" then
    local div = (v and v > 0) and v or 1
    local target = (math.floor(beats / div + 1e-6) + 1) * div
    t.wake = now + (target - beats) * beat_sec()
  else
    t.wake = now
  end
end
clock = {}
function clock.run(f, ...)
  local id = next_id
  next_id = next_id + 1
  threads[id] = {co = coroutine.create(f), wake = now}
  resume(id, ...)
  return id
end
function clock.cancel(id) threads[id] = nil end
function clock.sleep(s) coroutine.yield("sleep", s) end
function clock.sync(b) coroutine.yield("sync", b) end
function clock.get_beats() return beats end
function clock.get_tempo() return values.clock_tempo end
function clock.get_beat_sec() return beat_sec() end

-- MIDI: a device that reports what it sends; nothing arrives in the preview
midi = {
  connect = function()
    return {
      note_on = function(_, note, vel, ch) host:midi("note_on", note, vel, ch or 1) end,
      note_off = function(_, note, vel, ch) host:midi("note_off", note, vel, ch or 1) end,
    }
  end,
  to_msg = function() return {} end,
}

function _preview_init()
  if init then init() end
end
function _preview_tick(t)
  local dt = t - now
  now = t
  beats = beats + dt / beat_sec()
  -- the threads due now, oldest first
  local due = {}
  for id, th in pairs(threads) do
    if th.wake <= now then due[#due + 1] = id end
  end
  table.sort(due)
  for _, id in ipairs(due) do resume(id) end
end
function _preview_key(n, z)
  if key then key(n, z) end
end
function _preview_enc(n, d)
  if enc then enc(n, d) end
end
