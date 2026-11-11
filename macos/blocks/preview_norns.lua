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

-- engine: Weave's commands; notes show in the preview, sound comes from Run
local sounding = {}
engine = {
  patch = function() end, set = function() end, load_sample = function() end,
  note_on = function(id, hz, vel) sounding[id] = true; _preview_note_at = util.time(); host:engine("note_on", hz, vel) end,
  note_off = function(id) sounding[id] = nil end,
  all_off = function() sounding = {} end, kill = function() sounding = {} end,
}
poll = {set = function(name, f)
  local p = {time = 0.1}
  function p:start() _preview_polls[#_preview_polls + 1] = function()
    if name == "amp_out_l" then
      -- no audio here: the level of the output rises with each note and falls after it
      local age = util.time() - (_preview_note_at or -10)
      f(math.max(0, 1 - age / 0.6) * 0.3)
      return
    end
    local n = 0
    for _ in pairs(sounding) do n = n + 1 end
    f(n)
  end end
  function p:stop() end
  function p:update() end
  return p
end}
_preview_polls = {}

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
function params:add_option(id, name, options, default)
  info[id] = {id = id, name = name or id, options = options, controlspec = controlspec.new(1, #options, "lin", 1, default or 1)}
  values[id] = default or 1
end
function params:add_binary(id, name, behavior, default)
  info[id] = {id = id, name = name or id, controlspec = controlspec.new(0, 1, "lin", 1, default or 0)}
  values[id] = default or 0
end
function params:set_action(id, f) self:lookup_param(id).action = f end
function params:string(id)
  local p = self:lookup_param(id)
  return p.options and p.options[values[id]] or tostring(values[id])
end
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
  if p.action then p.action(values[id]) end
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
      cc = function() end,
    }
  end,
  to_msg = function() return {} end,
}

-- grid and arc: the grid lights show under the preview, its keys can be clicked there
local grid_dev = {}
function grid_dev:led(x, y, l) host:grid_led(x, y, l) end
function grid_dev:all(l) host:grid_all(l) end
function grid_dev:refresh() host:grid_refresh() end
grid = {connect = function() host:grid_used(); return grid_dev end}
local arc_dev = {}
function arc_dev:led() end
function arc_dev:all() end
function arc_dev:refresh() end
function arc_dev:segment() end
arc = {connect = function() return arc_dev end}
function _preview_test_note()
  _preview_note_at = util.time()
  if _blk_vis then _blk_vis.note_on(60, 100) end
end
function _preview_grid_key(x, y, z)
  if grid_dev.key then grid_dev.key(x, y, z) end
end

-- softcut: the playheads move (loops, rate, position) and report their phase; there is no
-- audio in the page, so a waveform is a stand-in shape
local cut = {}
local function voice(v)
  cut[v] = cut[v] or {play = 0, rate = 1, ls = 0, le = 1, pos = 0, rec = 0}
  return cut[v]
end
local phase_cb, render_cb
softcut = setmetatable({
  play = function(v, x) voice(v).play = x end,
  rec = function(v, x) voice(v).rec = x end,
  rate = function(v, x) voice(v).rate = x end,
  loop_start = function(v, x) voice(v).ls = x end,
  loop_end = function(v, x) voice(v).le = x end,
  position = function(v, x) voice(v).pos = x end,
  event_phase = function(f) phase_cb = f end,
  event_render = function(f) render_cb = f end,
  render_buffer = function(ch, start, dur, n)
    if not render_cb then return end
    local s = {}
    for i = 1, math.floor(n) do
      local t = start + dur * i / n
      s[i] = (math.sin(t * 7.1) * 0.5 + math.sin(t * 23.7) * 0.3) * (0.4 + 0.6 * math.abs(math.sin(t * 1.3)))
    end
    render_cb(ch, start, dur / n, s)
  end,
}, {__index = function() return function() end end})
audio = setmetatable({file_info = function() return 1, 4 * 48000, 48000 end}, {__index = function() return function() end end})
local function softcut_tick(dt)
  for v, c in pairs(cut) do
    if c.play == 1 then
      c.pos = c.pos + dt * c.rate
      local len = c.le - c.ls
      if len > 0 then
        if c.pos >= c.le then c.pos = c.ls + (c.pos - c.le) % len end
        if c.pos < c.ls then c.pos = c.le - (c.ls - c.pos) % len end
      end
      if phase_cb then phase_cb(v, c.pos) end
    end
  end
end

-- data the script remembers: kept while the preview runs
local files = {}
tab.save = function(t, f) files[f] = t end
tab.load = function(f) return files[f] end
util.file_exists = function(f) return files[f] ~= nil end
util.make_dir = function() end
_path = {audio = "/dust/audio/", data = "/dust/data/"}
norns = {state = {data = "/dust/data/preview/", name = "preview"}}

function _preview_init()
  if init then init() end
end
util.time = function() return now end
function _preview_tick(t)
  local dt = t - now
  now = t
  beats = beats + dt / beat_sec()
  softcut_tick(dt)
  for _, p in ipairs(_preview_polls) do p() end
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
