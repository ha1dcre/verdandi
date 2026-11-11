-- runs a script the script builder wrote against a stand-in norns, with the real musicutil,
-- sequins, util, tabutil, er and ui from lua/lib. called by macos/tests/blocks_test.js:
--   <lua 5.3> macos/tests/blocks_norns_stub.lua <repo> <script.lua> <data dir>
-- prints one line per call into the engine, the screen, params, midi, softcut, grid, arc and the
-- clock; calls outside the norns api and any error in the script stop it with exit status 1.
local repo, script, datadir = arg[1], arg[2], arg[3]
package.path = repo .. "/lua/lib/?.lua;" .. repo .. "/lua/core/?.lua;" .. package.path

local function fmt(v)
  if type(v) == "number" then
    if math.type(v) == "integer" then return tostring(v) end
    return string.format("%.3f", v)
  end
  return tostring(v)
end
local function log(name, ...)
  local parts = {}
  for i = 1, select("#", ...) do parts[#parts + 1] = fmt((select(i, ...))) end
  print(name .. " " .. table.concat(parts, " "))
end
local function recorder(prefix, allowed)
  return setmetatable({}, {__index = function(_, k)
    if allowed and not allowed[k] then error(prefix .. "." .. k .. " is not part of the norns api", 2) end
    return function(...) log(prefix .. "." .. k, ...) end
  end})
end
-- a device whose methods are called with ':' (midi, grid, arc); the device itself is not logged
local function device(prefix, allowed)
  return setmetatable({}, {__index = function(self, k)
    if not allowed[k] then error(prefix .. ":" .. k .. " is not used by the builder", 2) end
    return function(dev, ...)
      assert(dev == self, prefix .. "." .. k .. " called with . instead of :")
      log(prefix .. "." .. k, ...)
    end
  end})
end

util = require "util"
tab = require "tabutil"
local now = 0
util.time = function() now = now + 0.1; return now end
-- (norns makes folders through _norns)
util.make_dir = function(path) os.execute('mkdir -p "' .. path .. '"') end
_path = {audio = "/dust/audio/", data = datadir .. "/"}
norns = {state = {data = datadir .. "/script/", name = "script"}}

engine = recorder("engine", {name = 1, patch = 1, note_on = 1, note_off = 1, set = 1, all_off = 1,
  kill = 1, load_sample = 1})
screen = recorder("screen", {clear = 1, update = 1, level = 1, move = 1, text = 1, rect = 1,
  circle = 1, line = 1, fill = 1, stroke = 1, pixel = 1, font_size = 1, text_center = 1,
  text_right = 1, arc = 1, close = 1, line_width = 1, line_rel = 1})

controlspec = {new = function(min, max, warp, step, default, units)
  assert(warp == "lin" or warp == "exp", "warp " .. tostring(warp))
  assert(warp ~= "exp" or min > 0, "an exp control must start above 0")
  assert(type(default) == "number", "controlspec default")
  return {minval = min, maxval = max, default = default, step = step}
end}
local values, specs = {clock_tempo = 120}, {}
local function add(id, info, value)
  assert(values[id] == nil, "control " .. id .. " added twice")
  specs[id] = info
  values[id] = value
end
params = {
  add_control = function(self, id, name, spec)
    assert(self == params, "params:add_control called with . instead of :")
    add(id, {spec = spec, name = name}, spec.default)
    log("params.add_control", id, name, spec.minval, spec.maxval, spec.default)
  end,
  add_option = function(self, id, name, options, default)
    assert(self == params, "params:add_option called with . instead of :")
    assert(type(options) == "table" and #options > 0, "options")
    add(id, {spec = {minval = 1, maxval = #options}, name = name, options = options}, default or 1)
    log("params.add_option", id, name, #options, default)
  end,
  add_binary = function(self, id, name, behavior, default)
    assert(behavior == "toggle" or behavior == "momentary" or behavior == "trigger", "binary behavior")
    add(id, {spec = {minval = 0, maxval = 1}, name = name}, default or 0)
    log("params.add_binary", id, name, default)
  end,
  add_group = function(self, id, name, n) log("params.add_group", id, name, n) end,
  get = function(self, id)
    assert(self == params, "params:get called with . instead of :")
    assert(values[id] ~= nil, "no control " .. tostring(id))
    return values[id]
  end,
  string = function(self, id)
    local s = assert(specs[id], "no control " .. tostring(id))
    return s.options and s.options[values[id]] or tostring(values[id])
  end,
  set = function(self, id, v)
    assert(self == params, "params:set called with . instead of :")
    assert(type(v) == "number", "params:set " .. id .. " with a " .. type(v))
    local s = specs[id]
    if s then v = math.max(s.spec.minval, math.min(s.spec.maxval, v)) end
    assert(values[id] ~= nil, "no control " .. tostring(id))
    values[id] = v
    log("params.set", id, v)
    if s and s.action then s.action(v) end
  end,
  set_action = function(self, id, f)
    local s = assert(specs[id], "set_action: no control " .. tostring(id))
    s.action = f
  end,
  lookup_param = function(self, id)
    assert(self == params, "params:lookup_param called with . instead of :")
    local s = assert(specs[id], "no control " .. tostring(id))
    return {id = id, name = s.name, controlspec = s.spec, action = s.action}
  end,
}

local coroutines = {}
local beats = 0
clock = {
  get_beats = function() return beats end,
  run = function(f)
    local co = coroutine.create(f)
    coroutines[#coroutines + 1] = co
    return #coroutines
  end,
  sync = function(b) log("clock.sync", b); beats = beats + b; coroutine.yield() end,
  sleep = function(s) log("clock.sleep", s); beats = beats + s * values.clock_tempo / 60; coroutine.yield() end,
  get_beat_sec = function() return 60 / values.clock_tempo end,
  get_tempo = function() return values.clock_tempo end,
}

poll = {set = function(name, f)
  assert(name == "voices", "poll " .. tostring(name))
  return {start = function() log("poll.start", name); f(2) end, time = 0}
end}

local mdev = device("midi", {note_on = 1, note_off = 1, cc = 1})
midi = {
  connect = function(n) log("midi.connect", n); return mdev end,
  to_msg = function(data)
    local t = ({[0x90] = "note_on", [0x80] = "note_off", [0xb0] = "cc"})[data[1] & 0xf0] or "other"
    if t == "cc" then return {type = t, cc = data[2], val = data[3], ch = (data[1] & 0x0f) + 1} end
    return {type = t, note = data[2], vel = data[3], ch = (data[1] & 0x0f) + 1}
  end,
}
local gdev = device("grid", {led = 1, all = 1, refresh = 1})
grid = {connect = function() log("grid.connect"); return gdev end}
local adev = device("arc", {led = 1, all = 1, refresh = 1, segment = 1})
arc = {connect = function() log("arc.connect"); return adev end}

-- softcut: every call logged; the phase and render callbacks kept and called back
local phase_cb, render_cb
softcut = setmetatable({
  event_phase = function(f) phase_cb = f end,
  event_render = function(f) render_cb = f end,
  render_buffer = function(ch, start, dur, n)
    log("softcut.render_buffer", ch, start, dur, n)
    if render_cb then
      local s = {}
      for i = 1, math.floor(n) do s[i] = math.sin(i / 3) * 0.5 end
      render_cb(ch, start, dur / n, s)
    end
  end,
}, {__index = function(_, k)
  local known = {enable = 1, buffer = 1, level = 1, pan = 1, rate = 1, loop = 1, loop_start = 1, loop_end = 1,
    position = 1, play = 1, rec = 1, rec_level = 1, pre_level = 1, fade_time = 1, level_slew_time = 1,
    rate_slew_time = 1, recpre_slew_time = 1, level_input_cut = 1, phase_quant = 1, poll_start_phase = 1,
    buffer_clear_region_channel = 1, buffer_read_mono = 1}
  if not known[k] then error("softcut." .. k .. " is not part of the softcut api", 2) end
  return function(...) log("softcut." .. k, ...) end
end})
audio = recorder("audio", {level_adc_cut = 1, level_eng_cut = 1})
audio.file_info = function(path) log("audio.file_info", path); return 1, 96000, 48000 end

-- every global the script defines must be a norns callback or something it made itself
local before = {}
for k in pairs(_G) do before[k] = true end

dofile(script)
assert(type(init) == "function", "no init()")
print("-- init")
init()

local function run_clocks(times)
  for _ = 1, times do
    for i, co in ipairs(coroutines) do
      if coroutine.status(co) == "suspended" then
        local ok, err = coroutine.resume(co)
        if not ok then error("clock coroutine " .. i .. ": " .. tostring(err)) end
      end
    end
  end
end

-- the clock coroutines: a few steps each (redraw's included)
print("-- clock")
run_clocks(4)

if key then
  print("-- keys")
  for n = 1, 3 do
    for _, z in ipairs({1, 0}) do print("key " .. n .. " " .. z); key(n, z) end
  end
end
if enc then
  print("-- encoders")
  for n = 1, 3 do enc(n, 1); enc(n, -3) end
end
if phase_cb then
  print("-- softcut phase")
  for v = 1, 6 do phase_cb(v, ((v - 1) % 3) * 116 + 1 + 1.5) end
end
if redraw then
  print("-- redraw")
  redraw()
end
if rawget(mdev, "event") then
  print("-- midi")
  mdev.event({0x90, 64, 100})
  mdev.event({0x90, 64, 0})
  mdev.event({0x80, 67, 0})
  mdev.event({0xb0, 1, 99})
end
if rawget(gdev, "key") then
  print("-- grid")
  gdev.key(3, 2, 1)
  gdev.key(3, 2, 0)
end
if rawget(adev, "delta") then
  print("-- arc")
  adev.delta(1, 5)
end
-- the clock again: what key/enc/midi/grid changed shows up in the loops
print("-- clock")
run_clocks(1)

local allowed = {init = 1, key = 1, enc = 1, redraw = 1, cleanup = 1, musicutil = 1, sequins = 1, er = 1, UI = 1}
for k in pairs(_G) do
  if not before[k] and not allowed[k] and not k:match("^_blk_") then print("global " .. k) end
end
print("-- ok")
