--- virtual crow firmware (macOS)
-- the module side of the virtual crow: what a crow runs, in a lua state of its own on the
-- virtual crow's thread (matron/src/platform/macos/vcrow_sim.c), never inside norns. norns
-- talks to it over a pseudo terminal exactly as to a crow on usb: lua text and ^^ commands
-- in, ^^event(...) lines and printed text out (lua/core/crow.lua does not know the
-- difference).
--
-- written from crow's scripting reference: output[1-4] with volts / slew / shape / scale and
-- asl actions (to, loop, held, lock, times, dyn; lfo, pulse, ar, adsr, oscillate, ramp),
-- input[1-2] and their modes (stream, change, window, scale, volume, peak, freq, clock),
-- metro, delay, clock, public, crow.reset, script upload (^^s ^^e ^^w). there is no ii bus:
-- ii calls are accepted and go nowhere. outputs are voltages to look at in the window, not
-- sound.
--
-- the chunk is called with one argument, emit(text), and returns {receive, tick}.
-- @module core.crow.virtual

local emit = ...

local VERSION = 'v4.0.4-virtual'
local IDENTITY = '0x56435257'

local floor = math.floor
local unpack = table.unpack

local now = 0 -- seconds, from the C side
local in_v = {0, 0} -- the input voltages, set in the window
local pass = 0 -- tick count, so timers made during a tick wait for the next one

local E -- the script environment

---------------------------------------------------------------- text to norns

local function num(v)
  if math.type(v) == 'integer' then return tostring(v) end
  if v ~= v then return 'nil' end
  if v == math.huge then return 'math.huge' end
  if v == -math.huge then return '-math.huge' end
  return string.format('%.6g', v)
end

local function quote1(v)
  local t = type(v)
  if t == 'string' then
    -- a raw newline would end the line on its way to norns
    return (string.format('%q', v):gsub('\\\n', '\\n'))
  elseif t == 'number' then
    return num(v)
  elseif t == 'table' then
    local parts, n = {}, #v
    for i = 1, n do parts[i] = quote1(v[i]) end
    for k, x in pairs(v) do
      if not (math.type(k) == 'integer' and k >= 1 and k <= n) then
        local key = type(k) == 'number' and ('[' .. num(k) .. ']') or string.format('[%q]', tostring(k))
        parts[#parts + 1] = key .. '=' .. quote1(x)
      end
    end
    return '{' .. table.concat(parts, ',') .. '}'
  end
  return tostring(v)
end

local function quote(...)
  local parts = {}
  for i = 1, select('#', ...) do parts[i] = quote1((select(i, ...))) end
  return table.concat(parts, ',')
end

-- ^^name(args): numbers as numbers, strings as they are (callers quote them), tables quoted
local function tell(name, ...)
  local parts = {}
  for i = 1, select('#', ...) do
    local v = select(i, ...)
    local t = type(v)
    if t == 'number' then parts[i] = num(v)
    elseif t == 'string' then parts[i] = v
    elseif t == 'table' then parts[i] = quote1(v)
    else parts[i] = tostring(v) end
  end
  emit('^^' .. tostring(name) .. '(' .. table.concat(parts, ',') .. ')\n')
end

local function cprint(...)
  local parts = {}
  for i = 1, select('#', ...) do parts[i] = tostring((select(i, ...))) end
  emit(table.concat(parts, '\t') .. '\n')
end

-- call a script function; an error is printed, as crow does
local function try(fn, ...)
  if fn == nil then return false end
  local r = table.pack(pcall(fn, ...))
  if not r[1] then
    cprint('lua error: ' .. tostring(r[2]))
    return false
  end
  return true, unpack(r, 2, r.n)
end

local function clamp(v, lo, hi)
  if v < lo then return lo elseif v > hi then return hi end
  return v
end

---------------------------------------------------------------- shapes

local function back_out(x)
  local c, y = 1.70158, x - 1
  return 1 + (c + 1) * y * y * y + c * y * y
end
local function back_in(x)
  local c = 1.70158
  return (c + 1) * x * x * x - c * x * x
end
local function bounce(x)
  local n, d = 7.5625, 2.75
  if x < 1 / d then return n * x * x
  elseif x < 2 / d then x = x - 1.5 / d; return n * x * x + 0.75
  elseif x < 2.5 / d then x = x - 2.25 / d; return n * x * x + 0.9375 end
  x = x - 2.625 / d
  return n * x * x + 0.984375
end

local SHAPES = {
  linear = function(x) return x end,
  sine = function(x) return (1 - math.cos(math.pi * x)) / 2 end,
  log = function(x) return 1 - (1 - x) ^ 4 end,
  expo = function(x) return x ^ 4 end,
  now = function() return 1 end,
  wait = function(x) return x < 1 and 0 or 1 end,
  over = back_out,
  under = back_in,
  rebound = bounce,
}
SHAPES.lin = SHAPES.linear
SHAPES.logarithmic = SHAPES.log
SHAPES.exponential = SHAPES.expo
SHAPES.exp = SHAPES.expo

local function shape_of(name)
  if type(name) == 'string' then return SHAPES[name:lower()] or SHAPES.linear end
  return SHAPES.linear
end

---------------------------------------------------------------- timers

local timers = {}

local function after(at, fn)
  local t = {at = at, fn = fn, pass = pass}
  timers[#timers + 1] = t
  return t
end

local function cancel(t)
  if t then t.dead = true end
end

-- each due timer runs once per tick; fn returns when to run again, or nothing
local function run_timers()
  local i = 1
  while i <= #timers do
    local t = timers[i]
    if t.dead then
      table.remove(timers, i)
    elseif t.at <= now and t.pass ~= pass then
      local again = t.fn()
      if type(again) == 'number' and not t.dead then
        t.at = again
        t.pass = pass
        i = i + 1
      else
        t.dead = true
        table.remove(timers, i)
      end
    else
      i = i + 1
    end
  end
end

local function next_timer()
  local at
  for _, t in ipairs(timers) do
    if not t.dead and (at == nil or t.at < at) then at = t.at end
  end
  return at
end

---------------------------------------------------------------- clock

local clk = {tempo = 120, t0 = 0, b0 = 0, coros = {}, timer = {}, nextid = 1}

local function beats()
  return clk.b0 + (now - clk.t0) * clk.tempo / 60
end

local function beat_time(b)
  return clk.t0 + (b - clk.b0) * 60 / clk.tempo
end

local function set_tempo(t)
  t = tonumber(t)
  if t and t > 0 then
    clk.b0 = beats()
    clk.t0 = now
    clk.tempo = t
  end
end

local function resume(id, co, ...)
  if clk.coros[id] ~= co then return end -- cancelled
  local ok, kind, val = coroutine.resume(co, ...)
  if not ok then
    cprint('lua error: ' .. tostring(kind))
    clk.coros[id] = nil
    return
  end
  if coroutine.status(co) == 'dead' then
    clk.coros[id] = nil
    return
  end
  local at = now
  if kind == 'sleep' then
    at = now + math.max(tonumber(val) or 0, 0)
  elseif kind == 'sync' then
    local b = tonumber(val) or 1
    if b <= 0 then b = 1 end
    at = beat_time((floor(beats() / b + 1e-9) + 1) * b)
  end
  clk.timer[id] = after(at, function() resume(id, co) end)
end

local function clock_run(f, ...)
  local id = clk.nextid
  clk.nextid = id + 1
  local co = coroutine.create(f)
  clk.coros[id] = co
  resume(id, co, ...)
  return id
end

local function clock_cancel(id)
  if id == nil then return end
  clk.coros[id] = nil
  cancel(clk.timer[id])
  clk.timer[id] = nil
end

local function clock_cleanup()
  for id in pairs(clk.coros) do clock_cancel(id) end
end

local clock_tbl = setmetatable({
  run = clock_run,
  cancel = clock_cancel,
  sleep = function(s) coroutine.yield('sleep', s) end,
  sync = function(b) coroutine.yield('sync', b) end,
  get_beats = function() return beats() end,
  get_tempo = function() return clk.tempo end,
  set_tempo = set_tempo,
  cleanup = clock_cleanup,
  transport = {},
}, {
  __index = function(_, k)
    if k == 'tempo' then return clk.tempo end
  end,
  __newindex = function(t, k, v)
    if k == 'tempo' then set_tempo(v) else rawset(t, k, v) end
  end,
})
clock_tbl.start = function(b)
  if tonumber(b) then clk.b0 = tonumber(b); clk.t0 = now end
  if type(clock_tbl.transport.start) == 'function' then try(clock_tbl.transport.start) end
end
clock_tbl.stop = function()
  if type(clock_tbl.transport.stop) == 'function' then try(clock_tbl.transport.stop) end
end

---------------------------------------------------------------- asl

local resolve

local Lazy = {}
local function lazy(f) return setmetatable({__fn = f}, Lazy) end
local function binop(op)
  return function(a, b)
    return lazy(function(s) return op(resolve(a, s), resolve(b, s)) end)
  end
end
Lazy.__add = binop(function(a, b) return a + b end)
Lazy.__sub = binop(function(a, b) return a - b end)
Lazy.__mul = binop(function(a, b) return a * b end)
Lazy.__div = binop(function(a, b) return a / b end)
Lazy.__mod = binop(function(a, b) return a % b end)
Lazy.__unm = function(a) return lazy(function(s) return -resolve(a, s) end) end

-- dyn{name = default}: a value set from outside while the action runs (output[n].dyn.name)
local Dyn = {}
for k, v in pairs(Lazy) do Dyn[k] = v end
Dyn.__index = Dyn
function Dyn:step(d)
  self.ops[#self.ops + 1] = function(v) return v + d end
  return self
end
function Dyn:mul(m)
  self.ops[#self.ops + 1] = function(v) return v * m end
  return self
end
function Dyn:wrap(lo, hi)
  self.ops[#self.ops + 1] = function(v)
    if hi - lo <= 0 then return lo end
    return lo + (v - lo) % (hi - lo)
  end
  return self
end

local function dyn(t)
  local k, v = next(t or {})
  if k ~= nil then return setmetatable({__dyn = true, name = k, default = v, ops = {}}, Dyn) end
end

-- the value of an asl argument for output state s at this moment
resolve = function(x, s)
  local t = type(x)
  if t == 'table' then
    if x.__dyn then
      local v = s.dyn[x.name]
      if v == nil then v = x.default end
      if #x.ops > 0 then
        local nv = v
        for _, op in ipairs(x.ops) do nv = op(nv) end
        s.dyn[x.name] = nv
      end
      return v
    elseif x.__fn then
      return x.__fn(s)
    end
    local mt = getmetatable(x)
    if mt and mt.__call then -- a sequins
      local ok, v = try(x)
      return ok and v or 0
    end
    return 0
  elseif t == 'function' then
    local ok, v = try(x)
    return ok and v or 0
  end
  return x
end

local function to(v, t, sh) return {k = 'to', v = v, t = t or 0, s = sh} end
local function seq(items, flag, x)
  local n = {k = 'seq', items = items or {}}
  if flag then n[flag] = x or true end
  return n
end
local function loop(t) return seq(t, 'loop') end
local function held(t) return seq(t, 'held') end
local function lock(t) return seq(t) end
local function times(n, t) return seq(t, 'times', n) end

local function lfo(time, level, shape)
  time, level, shape = time or 1, level or 5, shape or 'sine'
  return loop{to(level, time / 2, shape), to(-level, time / 2, shape)}
end
local function pulse(time, level, polarity)
  time, level, polarity = time or 0.01, level or 5, polarity or 1
  local hi, lo = level, 0
  if polarity ~= 1 then hi, lo = 0, level end
  return {to(hi, 0), to(hi, time), to(lo, 0)}
end
local function ar(attack, release, level, shape)
  attack, release, level, shape = attack or 0.05, release or 0.5, level or 7, shape or 'log'
  return {to(level, attack, shape), to(0, release, shape)}
end
local function adsr(attack, decay, sustain, release, shape)
  attack, decay, sustain = attack or 0.05, decay or 0.3, sustain or 2
  release, shape = release or 2, shape or 'linear'
  return {held{to(5, attack, shape), to(sustain, decay, shape)}, to(0, release, shape)}
end
local function oscillate(freq, level, shape)
  freq, level, shape = freq or 1, level or 1, shape or 'sine'
  return loop{to(level, 0.5 / freq, shape), to(-level, 0.5 / freq, shape)}
end
local function ramp(time, skew, level)
  time, skew, level = time or 1, skew or 0.5, level or 5
  return loop{to(level, time * skew), to(-level, time * (1 - skew))}
end

local function norm(a)
  if type(a) == 'table' and a.k == nil and not a.__dyn and not a.__fn then
    return {k = 'seq', items = a}
  end
  return a
end

-- the dyn defaults of an action, set when it is assigned
local function collect_dyns(node, out, depth)
  if type(node) ~= 'table' or depth > 32 then return end
  if node.__dyn then
    out[node.name] = node.default
    return
  end
  if getmetatable(node) == Lazy then return end
  for _, v in pairs(node) do collect_dyns(v, out, depth + 1) end
end

---------------------------------------------------------------- outputs

local function quantize(v, sc)
  local per = sc.scaling ~= 0 and sc.scaling or 1
  local temp = sc.temp > 0 and sc.temp or 12
  local x = v / per
  local oct = floor(x)
  local deg = (x - oct) * temp
  local notes = sc.notes
  if #notes == 0 then
    local d = floor(deg + 0.5)
    return (oct + d / temp) * per, d + 1, oct, d
  end
  local best, bi, bo
  for i, n in ipairs(notes) do
    for o = -1, 1 do
      local dd = math.abs(deg - (n + o * temp))
      if best == nil or dd < best then best, bi, bo = dd, i, oct + o end
    end
  end
  return (bo + notes[bi] / temp) * per, bi, bo, notes[bi]
end

local outputs = {}

local function begin(s, v, t, shape)
  v, t = tonumber(v) or 0, tonumber(t) or 0
  s.from = s.value
  s.to = v
  s.t0 = s.cursor or now
  s.dur = t
  s.ease = type(shape) == 'function' and shape or shape_of(shape)
  if t <= 0 then
    s.value = v
    s.moving = false
  else
    s.moving = true
  end
end

-- on to the next stage of the running action
local function advance(s)
  local guard = 0
  while s.stack do
    local f = s.stack[#s.stack]
    if f == nil then
      s.stack = nil
      if s.done then try(s.done) end
      return
    end
    guard = guard + 1
    if guard > 256 then return end -- a loop of zero-time stages: go on next tick
    local node = f.node
    f.i = f.i + 1
    local item = node.items[f.i]
    if item == nil then
      if node.held and s.gate then
        f.i = f.i - 1
        s.holding = true
        return
      elseif node.loop and #node.items > 0 then
        f.i = 0
      elseif node.times and f.rep + 1 < (tonumber(resolve(node.times, s)) or 1) then
        f.rep = f.rep + 1
        f.i = 0
      else
        table.remove(s.stack)
      end
    else
      item = norm(item)
      if type(item) == 'table' and item.k == 'to' then
        begin(s, resolve(item.v, s), resolve(item.t, s), resolve(item.s, s))
        return
      elseif type(item) == 'table' and item.k == 'seq' then
        if not (item.held and not s.gate) then -- released already: the held part is skipped
          s.stack[#s.stack + 1] = {node = item, i = 0, rep = 0}
        end
      end
    end
  end
end

local function start(s, gate)
  local a = s.action
  if type(a) == 'function' then
    local ok, r = try(a)
    if not ok then return end
    a = r
  end
  a = norm(a)
  if type(a) ~= 'table' then return end
  if a.k == 'to' then a = {k = 'seq', items = {a}} end
  if a.k ~= 'seq' then return end
  s.gate = gate ~= false
  s.holding = false
  s.moving = false
  s.cursor = now
  s.stack = {{node = a, i = 0, rep = 0}}
  advance(s)
end

local function release(s)
  s.gate = false
  if not s.stack then return end
  for i = #s.stack, 1, -1 do
    if s.stack[i].node.held then
      for j = #s.stack, i, -1 do s.stack[j] = nil end
      s.holding = false
      s.moving = false
      s.cursor = now
      advance(s)
      return
    end
  end
end

-- a string action is lua that gives the action: "lfo(1, 5)"
local function compile(v)
  if type(v) == 'string' then
    local f, err = load('return ' .. v, '=action', 't', E)
    if not f then
      cprint('lua error: ' .. err)
      return nil
    end
    local ok, r = try(f)
    if not ok then return nil end
    return r
  end
  return v
end

local function set_action(s, v)
  s.action = compile(v)
  for k in pairs(s.dyn) do s.dyn[k] = nil end
  collect_dyns(s.action, s.dyn, 0)
end

local function set_volts(s, v)
  s.stack = nil
  s.holding = false
  s.cursor = now
  begin(s, tonumber(v) or 0, s.slew, s.shape)
end

local function reset_output(s)
  if s.clk then clock_cancel(s.clk) end
  s.value, s.from, s.to, s.t0, s.dur, s.cursor = 0, 0, 0, 0, 0, nil
  s.ease, s.moving = SHAPES.linear, false
  s.slew, s.shape = 0, 'linear'
  s.action, s.stack, s.gate, s.holding = nil, nil, false, false
  s.scale, s.done, s.clk = nil, nil, nil
  for k in pairs(s.dyn) do s.dyn[k] = nil end
  for k in pairs(s.extra) do s.extra[k] = nil end
  s.out = 0
end

local function new_output(n)
  local s = {n = n, dyn = {}, extra = {}}
  reset_output(s)
  local o = {}
  local m = {}
  m.dyn = s.dyn
  m.query = function() tell('output', n, s.out) end
  m.scale = function(notes, temp, scaling)
    if notes == o then notes, temp, scaling = temp, scaling, nil end
    if notes == 'none' then
      s.scale = nil
      return
    end
    if type(notes) ~= 'table' then notes = {} end
    s.scale = {notes = notes, temp = tonumber(temp) or 12, scaling = tonumber(scaling) or 1}
  end
  m.clock = function(div)
    if div == o then div = nil end
    if s.clk then clock_cancel(s.clk) end
    s.clk = nil
    if div == 'none' then return end
    div = tonumber(div) or 1
    if div <= 0 then div = 1 end
    if s.action == nil then s.action = pulse() end
    s.clk = clock_run(function()
      while true do
        coroutine.yield('sync', div)
        start(s, true)
      end
    end)
  end
  setmetatable(o, {
    __index = function(_, k)
      if k == 'volts' then return s.out
      elseif k == 'slew' then return s.slew
      elseif k == 'shape' then return s.shape
      elseif k == 'action' then return s.action
      elseif k == 'done' then return s.done
      end
      local v = m[k]
      if v ~= nil then return v end
      return s.extra[k]
    end,
    __newindex = function(_, k, v)
      if k == 'volts' then set_volts(s, v)
      elseif k == 'slew' then s.slew = tonumber(v) or 0
      elseif k == 'shape' then s.shape = v
      elseif k == 'action' then set_action(s, v)
      elseif k == 'done' then s.done = v
      elseif k == 'scale' then m.scale(v)
      else s.extra[k] = v end
    end,
    __call = function(_, arg)
      if arg == nil or arg == true then start(s, true)
      elseif arg == false then release(s)
      elseif arg == 'release' then release(s)
      elseif arg == 'restart' or arg == 'attack' or arg == 'start' then start(s, true)
      elseif arg == 'step' or arg == 'unlock' then return
      else
        set_action(s, arg)
        start(s, true)
      end
    end,
  })
  s.o = o
  outputs[n] = s
  return s
end

-- move output s on to the present; true while it changes by itself
local function step_output(s)
  local active = false
  for _ = 1, 64 do
    if s.moving then
      local x = (now - s.t0) / s.dur
      if x < 1 then
        s.value = s.from + (s.to - s.from) * s.ease(x)
        active = true
        break
      end
      s.value = s.to
      s.moving = false
      s.cursor = s.t0 + s.dur
    end
    if not s.stack or s.holding then break end
    if s.cursor == nil or now - s.cursor > 0.1 then s.cursor = now end
    advance(s)
    active = s.stack ~= nil and not s.holding
  end
  local v = s.value
  if v ~= v then v = 0 end
  v = clamp(v, -5, 10)
  if s.scale then v = clamp(quantize(v, s.scale), -5, 10) end
  s.out = v
  return active
end

---------------------------------------------------------------- inputs

local inputs = {}

local PARAMS = {time = true, threshold = true, hysteresis = true, direction = true, windows = true,
                notes = true, temp = true, scaling = true, div = true}
local HANDLERS = {stream = true, change = true, window = true, scale = true, volume = true,
                  peak = true, freq = true, clock = true}

local function window_index(s, v)
  local ws = s.windows
  local i = 1
  for _, w in ipairs(ws) do
    if v > w then i = i + 1 end
  end
  local cur = s.win
  if cur and i ~= cur then
    local hy = s.hysteresis / 2
    if i > cur and ws[cur] and v < ws[cur] + hy then return cur end
    if i < cur and ws[cur - 1] and v > ws[cur - 1] - hy then return cur end
  end
  return i
end

local function set_mode(s, mode, a, b, c)
  mode = type(mode) == 'string' and mode or 'none'
  s.mode = mode
  if mode == 'stream' or mode == 'volume' or mode == 'freq' then
    s.time = tonumber(a) or s.time
  elseif mode == 'change' then
    s.threshold = tonumber(a) or s.threshold
    s.hysteresis = tonumber(b) or s.hysteresis
    s.direction = type(c) == 'string' and c or s.direction
  elseif mode == 'window' then
    s.windows = type(a) == 'table' and a or s.windows
    s.hysteresis = tonumber(b) or s.hysteresis
  elseif mode == 'scale' then
    s.notes = type(a) == 'table' and a or s.notes
    s.temp = tonumber(b) or s.temp
    s.scaling = tonumber(c) or s.scaling
  elseif mode == 'peak' then
    s.threshold = tonumber(a) or s.threshold
    s.hysteresis = tonumber(b) or s.hysteresis
  elseif mode == 'clock' then
    s.div = tonumber(a) or s.div
  end
  if s.time < 0.001 then s.time = 0.001 end
  -- start from the present, without an event
  local v = in_v[s.n]
  s.state = v > s.threshold
  s.win = nil
  s.win = window_index(s, v)
  s.last = nil
  if mode == 'scale' then
    local _, i, o = quantize(v, s)
    s.last = i .. ':' .. o
  end
  s.edge = nil
  s.next = now + s.time
end

local function default_handlers(s)
  local n = s.n
  s.h = {
    stream = function(v) tell('stream', n, v) end,
    change = function(st) tell('change', n, st and 1 or 0) end,
    window = function(w, d) tell('window', n, w, d and 1 or 0) end,
    scale = function(t) tell('scale', n, t.index, t.octave, t.note, t.volts) end,
    volume = function(v) tell('volume', n, v) end,
    peak = function() tell('peak', n) end,
    freq = function(f) tell('freq', n, f) end,
    clock = function() end,
  }
end

local function reset_input(s)
  s.mode = 'none'
  s.time, s.threshold, s.hysteresis, s.direction = 0.1, 1, 0.1, 'both'
  s.windows, s.notes, s.temp, s.scaling, s.div = {}, {}, 12, 1, 1 / 4
  s.next, s.state, s.win, s.last, s.edge = 0, false, nil, nil, nil
  for k in pairs(s.extra) do s.extra[k] = nil end
  default_handlers(s)
end

local function new_input(n)
  local s = {n = n, extra = {}}
  reset_input(s)
  local o = {}
  local mode_fn = function(a, ...)
    if a == o then return set_mode(s, ...) end
    return set_mode(s, a, ...)
  end
  local query = function() tell('stream', n, in_v[n]) end
  setmetatable(o, {
    __index = function(_, k)
      if k == 'volts' then return in_v[n]
      elseif k == 'mode' then return mode_fn
      elseif k == 'query' then return query
      elseif HANDLERS[k] then return s.h[k]
      elseif PARAMS[k] then return s[k]
      end
      return s.extra[k]
    end,
    __newindex = function(_, k, v)
      if k == 'mode' then set_mode(s, v)
      elseif HANDLERS[k] then s.h[k] = v
      elseif PARAMS[k] then s[k] = v
      else s.extra[k] = v end
    end,
    __call = function(_, t)
      if type(t) ~= 'table' then return end
      for k, v in pairs(t) do
        if PARAMS[k] then s[k] = v elseif HANDLERS[k] then s.h[k] = v end
      end
      if t.mode then set_mode(s, t.mode) end
    end,
  })
  s.o = o
  inputs[n] = s
  return s
end

-- the input's events at this moment; returns when it next needs a tick (or nil)
local function tick_input(s)
  local v = in_v[s.n]
  local m = s.mode
  if m == 'stream' or m == 'volume' or m == 'freq' then
    if now >= s.next then
      s.next = s.next + s.time
      if s.next <= now then s.next = now + s.time end
      if m == 'stream' then try(s.h.stream, v)
      elseif m == 'volume' then try(s.h.volume, math.abs(v))
      else try(s.h.freq, 0) end
    end
    return s.next
  elseif m == 'change' or m == 'peak' or m == 'clock' then
    local th = m == 'clock' and 1 or s.threshold
    local hy = s.hysteresis / 2
    if not s.state and v > th + hy then
      s.state = true
      if m == 'change' then
        if s.direction ~= 'falling' then try(s.h.change, true) end
      elseif m == 'peak' then
        try(s.h.peak)
      else
        if s.edge and now - s.edge > 0.01 and s.div > 0 then set_tempo(60 * s.div / (now - s.edge)) end
        s.edge = now
        try(s.h.clock)
      end
    elseif s.state and v < th - hy then
      s.state = false
      if m == 'change' and s.direction ~= 'rising' then try(s.h.change, false) end
    end
  elseif m == 'window' then
    local w = window_index(s, v)
    if w ~= s.win then
      local up = s.win == nil or w > s.win
      s.win = w
      try(s.h.window, w, up)
    end
  elseif m == 'scale' then
    local qv, i, o, note = quantize(v, s)
    local key = i .. ':' .. o
    if key ~= s.last then
      s.last = key
      try(s.h.scale, {index = i, octave = o, note = note, volts = qv})
    end
  end
end

local function mode_label(s)
  local m = s.mode
  if m == 'stream' or m == 'volume' or m == 'freq' then
    return string.format('%s %gs', m, s.time)
  elseif m == 'change' then
    local d = s.direction == 'both' and '' or (' ' .. tostring(s.direction))
    return string.format('change %gV%s', s.threshold, d)
  elseif m == 'peak' then
    return string.format('peak %gV', s.threshold)
  elseif m == 'window' then
    return string.format('window %d', #s.windows + 1)
  elseif m == 'scale' then
    return #s.notes == 0 and 'scale chromatic' or string.format('scale %d notes', #s.notes)
  end
  return m
end

---------------------------------------------------------------- metro, delay

local metros = {}

local function new_metro(id)
  local m = {id = id, time = 1, count = -1, stage = 0, running = false}
  m.start = function(a, b, c)
    if a == m then a, b = b, c end
    if tonumber(a) then m.time = tonumber(a) end
    if tonumber(b) then m.count = tonumber(b) end
    cancel(m._t)
    m.stage = 0
    m.running = true
    local t
    t = after(now + math.max(tonumber(m.time) or 1, 0.001), function()
      if m._t ~= t or not m.running then return nil end
      m.stage = m.stage + 1
      if m.event then try(m.event, m.stage) end
      if m._t ~= t or not m.running then return nil end
      if m.count > 0 and m.stage >= m.count then
        m.running = false
        return nil
      end
      local nx = t.at + math.max(tonumber(m.time) or 1, 0.001)
      if nx < now - 0.05 then nx = now + math.max(tonumber(m.time) or 1, 0.001) end
      return nx
    end)
    m._t = t
  end
  m.stop = function()
    m.running = false
    cancel(m._t)
    m._t = nil
  end
  return m
end

for i = 1, 8 do metros[i] = new_metro(i) end

local metro_tbl = {}
for i = 1, 8 do metro_tbl[i] = metros[i] end
metro_tbl.init = function(a, b, c)
  local ev, time, count = a, b, c
  if type(a) == 'table' then ev, time, count = a.event, a.time, a.count end
  for i = 1, 8 do
    local m = metros[i]
    if not m.used and not m.running then
      m.used = true
      m.event = ev
      m.time = tonumber(time) or 1
      m.count = tonumber(count) or -1
      return m
    end
  end
  cprint('metro.init: no free metro')
end
metro_tbl.free = function(id)
  local m = metros[tonumber(id) or 0]
  if m then
    m.stop()
    m.used = false
  end
end

local function delay(action, time, repeats)
  time = math.max(tonumber(time) or 0, 0)
  repeats = tonumber(repeats) or 0
  local n = 0
  local t
  t = after(now + time, function()
    n = n + 1
    try(action, n)
    if n <= repeats then return t.at + math.max(time, 0.001) end
  end)
  return {stop = function() cancel(t) end}
end

---------------------------------------------------------------- public

local pub = {list = {}, names = {}, views = {input = {}, output = {}}, sent = {input = {}, output = {}}, next = 0}

local function pub_param(name, val, typ, action)
  local p = pub.names[name]
  if not p then
    p = {name = name}
    pub.list[#pub.list + 1] = p
    pub.names[name] = p
  end
  p.val = val
  if type(typ) == 'string' then typ = {typ} end
  p.typ = type(typ) == 'table' and typ or {}
  p.action = action
  return p
end

local function pub_obj(p)
  local o = {}
  o.range = function(_, lo, hi)
    p.typ[1], p.typ[2] = lo, hi
    return o
  end
  o.type = function(_, t)
    if type(p.typ[1]) == 'string' then p.typ = {t} else p.typ[3] = t end
    return o
  end
  o.options = function(_, list)
    p.typ = {'option'}
    for _, v in ipairs(list or {}) do p.typ[#p.typ + 1] = v end
    return o
  end
  o.action = function(_, f)
    p.action = f
    return o
  end
  return o
end

local function view_setter(kind)
  return setmetatable({}, {
    __index = function(_, n)
      return function(on)
        pub.views[kind][n] = on ~= false and on ~= 0
        pub.sent[kind][n] = nil
      end
    end,
  })
end

local public = setmetatable({
  add = function(name, val, typ, action) return pub_obj(pub_param(name, val, typ, action)) end,
  discover = function()
    for _, p in ipairs(pub.list) do tell('pub', quote(p.name, p.val, p.typ)) end
    tell('pub', quote('_end'))
  end,
  update = function(name, val, sub)
    local p = pub.names[name]
    if not p then return end
    if sub and type(p.val) == 'table' then p.val[sub] = val else p.val = val end
    if p.action then try(p.action, p.val) end
  end,
  view = {input = view_setter('input'), output = view_setter('output')},
}, {
  __call = function(_, t)
    local k, v = next(t or {})
    if k ~= nil then return pub_obj(pub_param(k, v)) end
  end,
  __index = function(_, k)
    local p = pub.names[k]
    if p then return p.val end
  end,
  __newindex = function(_, k, v)
    local p = pub.names[k]
    if p then
      p.val = v
      tell('pupdate', quote(k, v))
    else
      pub_param(k, v)
    end
  end,
})

local function tick_views()
  local any = false
  if now < pub.next then
    for _, kind in ipairs{'input', 'output'} do
      for _, on in pairs(pub.views[kind]) do any = any or on end
    end
    return any
  end
  pub.next = now + 0.1
  for _, kind in ipairs{'input', 'output'} do
    for n, on in pairs(pub.views[kind]) do
      if on then
        any = true
        local v = kind == 'input' and in_v[n] or (outputs[n] and outputs[n].out)
        local last = pub.sent[kind][n]
        if v and (last == nil or math.abs(v - last) > 0.005) then
          pub.sent[kind][n] = v
          tell('pubview', quote(kind), n, v)
        end
      end
    end
  end
  return any
end

---------------------------------------------------------------- ii (no bus)

local ii_warned = false
local function ii_proxy()
  local P = {}
  P.__index = function(t, k)
    local c = setmetatable({}, P)
    rawset(t, k, c)
    return c
  end
  P.__call = function()
    if not ii_warned then
      ii_warned = true
      cprint('virtual crow: no ii bus, ii messages go nowhere')
    end
  end
  local ii = setmetatable({}, P)
  rawset(ii, 'pullup', function() end)
  rawset(ii, 'help', function() cprint('virtual crow: no ii bus') end)
  return ii
end

---------------------------------------------------------------- environment

local function reset_hw()
  for _, t in ipairs(timers) do t.dead = true end
  timers = {}
  clock_cleanup()
  for n = 1, 4 do reset_output(outputs[n]) end
  for n = 1, 2 do reset_input(inputs[n]) end
  for i = 1, 8 do
    metros[i].stop()
    metros[i].used = false
    metros[i].event = nil
    metros[i].time = 1
    metros[i].count = -1
  end
  pub.list, pub.names = {}, {}
  pub.views = {input = {}, output = {}}
  pub.sent = {input = {}, output = {}}
  ii_warned = false
end

for n = 1, 4 do new_output(n) end
for n = 1, 2 do new_input(n) end

local sequins_ok, sequins = pcall(require, 'sequins')
if not sequins_ok then sequins = nil end

local SAFE = {'assert', 'error', 'ipairs', 'next', 'pairs', 'pcall', 'select', 'tonumber', 'tostring',
              'type', 'xpcall', 'rawget', 'rawset', 'rawequal', 'rawlen', 'setmetatable', 'getmetatable'}

local function copy(t)
  local c = {}
  for k, v in pairs(t) do c[k] = v end
  return c
end

local function log2(x) return math.log(x, 2) end
local function justvolts(f, off)
  off = off or 0
  if type(f) == 'table' then
    local t = {}
    for i, x in ipairs(f) do t[i] = log2(x) + off end
    return t
  end
  return log2(f) + off
end
local function just12(f, off)
  off = off or 0
  if type(f) == 'table' then
    local t = {}
    for i, x in ipairs(f) do t[i] = log2(x) * 12 + off end
    return t
  end
  return log2(f) * 12 + off
end

local crow_tbl

local function new_env()
  local e = {}
  for _, k in ipairs(SAFE) do e[k] = _G[k] end
  e.math, e.table, e.coroutine = copy(math), copy(table), copy(coroutine)
  e.string, e.utf8 = string, utf8
  e.os = {time = os.time, clock = os.clock, date = os.date}
  e.unpack = table.unpack
  e.load = function(chunk, name, mode, env) return load(chunk, name, mode or 't', env or e) end
  e.print, e.tell, e.quote = cprint, tell, quote
  e._c = {tell = tell}
  e.output, e.input = {}, {}
  for n = 1, 4 do e.output[n] = outputs[n].o end
  for n = 1, 2 do e.input[n] = inputs[n].o end
  e.metro, e.clock, e.ii, e.public, e.crow = metro_tbl, clock_tbl, ii_proxy(), public, crow_tbl
  e.to, e.loop, e.held, e.lock, e.times, e.dyn = to, loop, held, lock, times, dyn
  e.lfo, e.pulse, e.ar, e.adsr, e.oscillate, e.ramp = lfo, pulse, ar, adsr, oscillate, ramp
  e.delay = delay
  e.time = function() return floor(now * 1000) end
  e.cputime = function() return 0 end
  e.unique_id = function() return 0x56, 0x43, 0x52 end
  e.justvolts, e.just12 = justvolts, just12
  e.hztovolts = function(hz, ref) return log2(hz / (ref or 261.63)) end
  e.sequins = sequins
  e._G = e
  E = e
end

crow_tbl = {
  reset = function() reset_hw() end,
  version = function() tell('version', quote(VERSION)) end,
  identity = function() tell('identity', quote(IDENTITY)) end,
}

new_env()

---------------------------------------------------------------- from norns

local rx = '' -- a line still coming
local pending, pending_t -- lua still coming: a chunk that is not complete yet
local upload -- lines of a script being uploaded (^^s ... ^^e / ^^w)
local stored -- the script ^^w kept, run again by ^^r

local function run_script(code)
  new_env()
  reset_hw()
  local f, err = load(code, '=user script', 't', E)
  if not f then
    cprint('lua error: ' .. err)
    return
  end
  if try(f) and type(E.init) == 'function' then try(E.init) end
  tell('ready')
end

local function command(c)
  c = c:match('^%s*(.-)%s*$')
  local k = c:sub(1, 1)
  if k == 'i' then
    tell('identity', quote(IDENTITY))
  elseif k == 'v' then
    tell('version', quote(VERSION))
  elseif k == 's' then
    upload = {}
    pending = nil
  elseif k == 'e' or k == 'w' then
    local code = table.concat(upload or {}, '\n')
    upload = nil
    if k == 'w' then stored = code end
    run_script(code)
  elseif k == 'k' then
    new_env()
    reset_hw()
  elseif k == 'c' then
    stored = nil
  elseif k == 'p' then
    if stored then
      for line in (stored .. '\n'):gmatch('(.-)\n') do cprint(line) end
    else
      cprint('no user script.')
    end
  elseif k == 'r' then
    if stored then run_script(stored) else new_env(); reset_hw() end
  elseif k == 'b' then
    cprint('virtual crow: no bootloader')
  end
end

local function run_chunk(code)
  local f, err = load(code, '=repl', 't', E)
  if f then
    try(f)
    return true
  end
  return false, err
end

local function line_in(line)
  if line:sub(1, 2) == '^^' then
    command(line:sub(3))
    return
  end
  if upload then
    upload[#upload + 1] = line
    return
  end
  local code = pending and (pending .. '\n' .. line) or line
  if code:match('^%s*$') then
    pending = nil
    return
  end
  local ok, err = run_chunk(code)
  if ok then
    pending = nil
  elseif err:find('<eof>', 1, true) and #code < 65536 then
    if not pending then pending_t = now end
    pending = code -- more to come
  else
    pending = nil
    cprint('lua error: ' .. err)
  end
end

local F = {}

-- bytes from norns at time t
function F.receive(data, t)
  if tonumber(t) then now = tonumber(t) end
  data = data:gsub('%z', ''):gsub('\r\n', '\n'):gsub('\r', '\n')
  rx = rx .. data
  while true do
    local i = rx:find('\n', 1, true)
    if not i then break end
    local line = rx:sub(1, i - 1)
    rx = rx:sub(i + 1)
    line_in(line)
  end
  if #rx > 65536 then rx = '' end
end

-- the present is t, the inputs a and b: run what is due. returns the seconds until the
-- next tick is needed, the four output voltages and labels for the two input modes
function F.tick(t, a, b)
  now = t
  in_v[1], in_v[2] = tonumber(a) or 0, tonumber(b) or 0
  pass = pass + 1
  local wait = 1
  if pending then
    if now - pending_t > 0.5 then
      cprint('lua error: incomplete chunk dropped: ' .. pending:sub(1, 40))
      pending = nil
    else
      wait = 0.05
    end
  end
  for n = 1, 2 do
    local nx = tick_input(inputs[n])
    if nx then wait = math.min(wait, nx - now) end
  end
  run_timers()
  local nt = next_timer()
  if nt then wait = math.min(wait, nt - now) end
  for n = 1, 4 do
    if step_output(outputs[n]) then wait = 0.001 end
  end
  if tick_views() then wait = math.min(wait, pub.next - now) end
  return math.max(wait, 0.0005), outputs[1].out, outputs[2].out, outputs[3].out, outputs[4].out,
    mode_label(inputs[1]), mode_label(inputs[2])
end

return F
