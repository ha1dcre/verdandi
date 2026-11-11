-- runs a script the script builder wrote against a stand-in norns, with the real musicutil,
-- sequins, util and tabutil from lua/lib. called by macos/tests/blocks_test.js:
--   <lua 5.3> macos/tests/blocks_norns_stub.lua <repo> <script.lua>
-- prints one line per call into the engine, the screen, params, midi and the clock; any error
-- in the script stops it with exit status 1.
local repo, script = arg[1], arg[2]
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

util = require "util"
tab = require "tabutil"

engine = recorder("engine", {hz = 1, amp = 1, pw = 1, release = 1, cutoff = 1, gain = 1, pan = 1})
screen = recorder("screen", {clear = 1, update = 1, level = 1, move = 1, text = 1, rect = 1,
  circle = 1, line = 1, fill = 1, stroke = 1, pixel = 1, font_size = 1, text_center = 1,
  text_right = 1, arc = 1, close = 1, line_width = 1, line_rel = 1})

controlspec = {new = function(min, max, warp, step, default)
  assert(warp == "lin", "warp")
  return {minval = min, maxval = max, default = default}
end}
local values, specs = {clock_tempo = 120}, {}
params = {
  add_control = function(self, id, name, spec)
    assert(self == params, "params:add_control called with . instead of :")
    assert(values[id] == nil, "control " .. id .. " added twice")
    specs[id] = {spec = spec, name = name}
    values[id] = spec.default
    log("params.add_control", id, name, spec.minval, spec.maxval, spec.default)
  end,
  get = function(self, id)
    assert(self == params, "params:get called with . instead of :")
    assert(values[id] ~= nil, "no control " .. tostring(id))
    return values[id]
  end,
  set = function(self, id, v)
    assert(self == params, "params:set called with . instead of :")
    assert(type(v) == "number", "params:set " .. id .. " with a " .. type(v))
    local s = specs[id]
    if s then v = math.max(s.spec.minval, math.min(s.spec.maxval, v)) end
    assert(values[id] ~= nil, "no control " .. tostring(id))
    values[id] = v
    log("params.set", id, v)
  end,
  lookup_param = function(self, id)
    assert(self == params, "params:lookup_param called with . instead of :")
    local s = assert(specs[id], "no control " .. tostring(id))
    return {id = id, name = s.name, controlspec = s.spec}
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
  sleep = function(s) log("clock.sleep", s); coroutine.yield() end,
  get_beat_sec = function() return 60 / values.clock_tempo end,
  get_tempo = function() return values.clock_tempo end,
}

-- a midi device: its methods are called with ':', the device itself is not logged
local device = setmetatable({}, {__index = function(self, k)
  if k ~= "note_on" and k ~= "note_off" then error("midi device:" .. k .. " is not used by the builder", 2) end
  return function(dev, ...)
    assert(dev == self, "midi device." .. k .. " called with . instead of :")
    log("midi." .. k, ...)
  end
end})
midi = {
  connect = function(n) log("midi.connect", n); return device end,
  to_msg = function(data)
    local t = ({[0x90] = "note_on", [0x80] = "note_off"})[data[1] & 0xf0] or "other"
    return {type = t, note = data[2], vel = data[3], ch = (data[1] & 0x0f) + 1}
  end,
}

-- every global the script defines must be a norns callback or something it made itself
local before = {}
for k in pairs(_G) do before[k] = true end

dofile(script)
assert(type(init) == "function", "no init()")
print("-- init")
init()

-- the clock coroutines: a few steps each (redraw's included)
print("-- clock")
for _ = 1, 4 do
  for i, co in ipairs(coroutines) do
    if coroutine.status(co) == "suspended" then
      local ok, err = coroutine.resume(co)
      if not ok then error("clock coroutine " .. i .. ": " .. tostring(err)) end
    end
  end
end

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
if redraw then
  print("-- redraw")
  redraw()
end
if rawget(device, "event") then
  print("-- midi")
  device.event({0x90, 64, 100})
  device.event({0x90, 64, 0})
  device.event({0x80, 67, 0})
end
-- the clock again: what key/enc/midi changed shows up in the loops
print("-- clock")
for i, co in ipairs(coroutines) do
  if coroutine.status(co) == "suspended" then
    local ok, err = coroutine.resume(co)
    if not ok then error("clock coroutine " .. i .. ": " .. tostring(err)) end
  end
end

local allowed = {init = 1, key = 1, enc = 1, redraw = 1, cleanup = 1, musicutil = 1, sequins = 1}
for k in pairs(_G) do
  if not before[k] and not allowed[k] and not k:match("^_blk_") then print("global " .. k) end
end
print("-- ok")
