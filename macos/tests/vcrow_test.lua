-- vcrow_test.lua: norns' own lua/core/crow.lua talking to the virtual crow
-- (lua/core/crow/virtual.lua), in simulated time. run by macos/tests/vcrow_test.c, which
-- provides sim_feed, advance(seconds), set_input(n, volts), out(n), mode(n), now()

package.path = 'lua/?.lua;lua/lib/?.lua;' .. package.path

failures = 0
local log = {} -- everything crow sent, as text
local printed = {}

local function check(cond, what, ...)
  if not cond then
    failures = failures + 1
    io.write('FAIL ' .. string.format(what, ...) .. '\n')
  end
end

local function near(a, b, tol) return math.abs(a - b) <= (tol or 1e-3) end

-- the norns side, as much as crow.lua needs
norns = {state = {path = 'nowhere/'}}
params = {lookup = {}}
_path = {code = 'nowhere/'}
tab = {count = function(t) local n = 0; for _ in pairs(t) do n = n + 1 end; return n end}
util = {
  clamp = function(v, lo, hi) return math.min(math.max(v, lo), hi) end,
  wrap = function(v, lo, hi) return lo + (v - lo) % (hi - lo + 1) end,
  file_exists = function() return false end,
}
_norns = {crow_send = function(dev, line) sim_feed(line .. '\n') end} -- device_crow.cc adds the \n
local real_print = print
print = function(...)
  local t = {}
  for i = 1, select('#', ...) do t[i] = tostring((select(i, ...))) end
  printed[#printed + 1] = table.concat(t, ' ')
end

function deliver(text)
  log[#log + 1] = text
  _norns.crow.event(1, text)
end

local function sent(pattern)
  return table.concat(log):find(pattern, 1, true) ~= nil
end
local function said(pattern)
  for _, p in ipairs(printed) do if p:find(pattern, 1, true) then return true end end
  return false
end
local function clear() log = {}; printed = {} end

require 'core/crow'

-- plugged in: device_crow.cc asks who it is, then norns resets it
sim_feed('^^i\n\0')
check(sent('^^identity("0x56435257")'), 'identity: %s', table.concat(log))
check(said('crow identity: 0x56435257'), 'identity reaches norns.crow.events')
_norns.crow.add(1, 'crow (virtual)', 1)
check(norns.crow.connected(), 'connected after add')
crow.version()
check(said('crow version: v4.0.4-virtual'), 'version')

-- outputs: volts, slew, query
crow.output[1].volts = 3
advance(0.01)
check(near(out(1), 3), 'volts: out 1 = %g', out(1))
crow.output[2].slew = 0.5
crow.output[2].volts = 5
advance(0.25)
check(near(out(2), 2.5, 0.05), 'slew: half way at %g', out(2))
advance(0.3)
check(near(out(2), 5), 'slew: arrived at %g', out(2))
crow.output[1].volts = 12
advance(0.01)
check(near(out(1), 10), 'clamped to 10 V: %g', out(1))
crow.output[1].volts = 3
advance(0.01)
clear()
local got
crow.output[1].receive = function(v) got = v end -- norns-side only, as on hardware
crow.output[1].query()
check(sent('^^output(1,3)'), 'query: %s', table.concat(log))

-- an lfo given as a string, as norns scripts do
crow.output[3].action = 'lfo(1, 5)'
crow.output[3]()
local lo, hi = 0, 0
for _ = 1, 2000 do
  advance(0.001)
  lo, hi = math.min(lo, out(3)), math.max(hi, out(3))
end
check(near(lo, -5, 0.01) and near(hi, 5, 0.01), 'lfo swings %g..%g', lo, hi)
-- up to +5 by 0.5 s, -5 by 1 s, +5 by 1.5 s: at -5 again after 2 s, with no drift
check(near(out(3), -5, 0.01), 'lfo period: %g after 2 s', out(3))
crow.output[3].volts = 0
advance(0.01)
check(near(out(3), 0), 'setting volts stops the action: %g', out(3))

-- pulse
crow.output[4].action = 'pulse(0.1, 5)'
crow.output[4]()
advance(0.05)
check(near(out(4), 5), 'pulse high: %g', out(4))
advance(0.1)
check(near(out(4), 0), 'pulse low again: %g', out(4))

-- adsr with a gate, and done
local done = false
crow.output[1].done = function() done = true end
crow.output[1].action = 'adsr(0.1, 0.1, 2, 0.2)'
crow.output[1](true)
advance(0.1)
check(near(out(1), 5, 0.1), 'adsr attack peak %g', out(1))
advance(0.5)
check(near(out(1), 2), 'adsr sustains at %g', out(1))
crow.output[1](false)
advance(0.1)
check(near(out(1), 1, 0.05), 'adsr releasing: %g', out(1))
advance(0.2)
check(near(out(1), 0) and done, 'adsr done: %g, done %s', out(1), tostring(done))

-- an asl table with dyn, times and a shape
crow 'output[2].action = loop{ to(dyn{level=3}, 0.1), to(0, 0.1) }'
crow 'output[2]()'
advance(0.1)
check(near(out(2), 3, 0.05), 'dyn level %g', out(2))
crow.output[2].dyn.level = 7
advance(0.2)
check(near(out(2), 7, 0.1), 'dyn changed to %g', out(2))
crow 'output[2].action = times(2, { to(5, 0.05), to(0, 0.05) })'
crow 'output[2]()'
advance(0.25)
check(near(out(2), 0), 'times ends at 0: %g', out(2))
crow 'output[2].action = { to(10, 1, "expo") }'
crow 'output[2]()'
advance(0.5)
check(out(2) < 1.0 and out(2) > 0.3, 'expo shape is slow at first: %g', out(2))

-- output scale
crow.output[4].scale({0, 4, 7})
crow.output[4].volts = 0.3
advance(0.01)
check(near(out(4), 4 / 12), 'scale: 0.3 V to %g', out(4))
crow.output[4].scale('none')
crow.output[4].volts = 0.3
advance(0.01)
check(near(out(4), 0.3), 'scale none: %g', out(4))

-- input stream, as norns scripts set it up
local streamed = {}
crow.input[1].stream = function(v) streamed[#streamed + 1] = v end
crow.input[1].mode('stream', 0.1)
check(mode(1) == 'stream 0.1s', 'mode label %s', mode(1))
set_input(1, 1.5)
advance(0.35)
check(#streamed == 3 and near(streamed[3], 1.5), 'stream: %d events, last %s', #streamed, tostring(streamed[#streamed]))

-- input change, both ways
local changes = {}
crow.input[2].change = function(s) changes[#changes + 1] = s end
crow.input[2].mode('change', 1, 0.1, 'both')
set_input(2, 2)
advance(0.01)
set_input(2, 0)
advance(0.01)
check(#changes == 2 and changes[1] == true and changes[2] == false, 'change: %d events', #changes)
crow.input[2].mode('change', 1, 0.1, 'rising')
set_input(2, 2)
set_input(2, 0)
set_input(2, 2)
advance(0.01)
check(#changes == 4, 'change rising only: %d events', #changes)
crow.input[2].query()
check(sent('^^stream(2,2)'), 'input query')

-- input window and scale
local wins = {}
crow.input[1].window = function(w, d) wins[#wins + 1] = w end
crow.input[1].mode('window', {0, 2.5, 5}, 0.1)
set_input(1, 3)
set_input(1, 6)
set_input(1, -1)
check(#wins == 3 and wins[1] == 3 and wins[2] == 4 and wins[3] == 1, 'window: %s', table.concat(wins, ','))
local notes = {}
crow.input[1].scale = function(s) notes[#notes + 1] = s end
crow.input[1].mode('scale', {0, 2, 4, 5, 7, 9, 11})
set_input(1, 1 + 4 / 12)
check(#notes == 1 and notes[1].index == 3 and notes[1].octave == 1, 'scale: index %s octave %s',
  tostring(notes[1] and notes[1].index), tostring(notes[1] and notes[1].octave))
crow.input[1].mode('none')

-- crow as norns' clock source: the multi-line chunk norns sends, then a rising edge
set_input(1, 0)
clear()
norns.crow.clock_enable()
set_input(1, 5)
advance(0.01)
check(sent('^^change(1,1)'), 'clock_enable: rising edge sends ^^change(1,1): %s', table.concat(log))

-- metro, clock and delay on crow
crow 'metro[1].event = function(c) output[1].volts = c end'
crow 'metro[1].time = 0.1'
crow 'metro[1]:start()'
advance(0.35)
check(near(out(1), 3), 'metro: 3 events, out 1 = %g', out(1))
crow 'metro[1]:stop()'
crow 'clock.tempo = 120; clock.run(function() while true do clock.sync(1); output[3].volts = output[3].volts + 1 end end)'
crow.output[3].volts = 0
advance(2.05)
check(out(3) == 4 or out(3) == 5, 'clock: 4.1 beats at 120 bpm in 2.05 s, out 3 = %g', out(3))
crow 'delay(function() output[4].volts = 9 end, 0.2)'
advance(0.1)
check(not near(out(4), 9), 'delay: not yet')
advance(0.15)
check(near(out(4), 9), 'delay: done, out 4 = %g', out(4))

-- reset
crow.reset()
advance(0.01)
check(near(out(1), 0) and near(out(3), 0) and near(out(4), 0), 'reset: outputs at 0')
advance(1)
check(near(out(3), 0), 'reset stops clocks: %g', out(3))
check(mode(1) == 'none' and mode(2) == 'none', 'reset: inputs none')

-- a crow script uploaded (as norns.crow.loadscript does), with public params
clear()
local discovered = false
norns.crow.public.discovered = function() discovered = true end
local script = {
  'public{rate = 1}:range(0.1, 10)',
  'public.add("shape", "sine", {"option", "sine", "linear"})',
  'function init()',
  '  output[2].volts = 7',
  '  output[1].action = lfo(public.rate, 3)',
  'end',
}
sim_feed('^^s\n')
for _, l in ipairs(script) do sim_feed(l .. '\n') end
sim_feed('^^e\n')
advance(0.01)
check(near(out(2), 7), 'script: init ran, out 2 = %g', out(2))
check(sent('^^ready()'), 'script: ready sent')
check(discovered, 'public: discovered')
local p = norns.crow.public.lookup('rate')
check(p and p.val == 1 and p.min == 0.1 and p.max == 10, 'public: rate param %s', p and tostring(p.val) or 'missing')
local s = norns.crow.public.lookup('shape')
check(s and s.type == 'option' and #s.option == 2, 'public: option param')
crow.public.rate = 4
advance(0.01)
clear()
crow 'print(public.rate)'
check(said('crow:\t4') or sent('4\n'), 'public: update reached crow: %s', table.concat(log))
crow.public.view.output[1](true)
advance(0.3)
check(sent('^^pubview("output",1,'), 'public view: %s', table.concat(log):sub(1, 80))

-- ii: accepted, goes nowhere, said once
clear()
crow.ii.jf.play_note(1, 5)
crow.ii.jf.play_note(2, 5)
local n = 0
for _, l in ipairs(log) do if l:find('no ii bus', 1, true) then n = n + 1 end end
check(n == 1, 'ii: one note about the missing bus (%d)', n)

-- errors come back as text, the crow goes on
clear()
crow 'output[1].volts = nil + 1'
check(sent('lua error'), 'error printed: %s', table.concat(log))
crow 'output[1].volts = 2'
advance(0.01)
check(near(out(1), 2), 'works after an error: %g', out(1))
clear()
crow 'for i = 1, 3 do'
advance(0.6)
check(sent('incomplete chunk dropped'), 'incomplete chunk dropped: %s', table.concat(log))

-- ^^k kills the script, sequins exist
crow 's = sequins{1, 2, 3}; output[1].volts = s() + s()'
advance(0.01)
check(near(out(1), 3), 'sequins: %g', out(1))
sim_feed('^^k\n')
advance(0.01)
check(near(out(1), 0), 'kill: %g', out(1))

-- an idle crow sleeps: one tick a second
crow.reset()
advance(0.01)
local ticks = advance(5)
check(ticks <= 10, 'idle: %d ticks in 5 s', ticks)
crow.output[1].action = 'lfo(1,1)'
crow.output[1]()
ticks = advance(1)
check(ticks >= 900, 'running: %d ticks in 1 s', ticks)

print = real_print
print(string.format('vcrow scenario: %d failures', failures))
