// screen.js: a screen design (the "screen" page) -> the Lua that draws it: a small runtime
// (_blk_vis: the animations, text with live values, the moving elements) and _blk_design(),
// called first in redraw(). browser: window.VerdandiScreen; node: module.exports.
// the elements and their props are listed in macos/blocks/screen.json.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.VerdandiScreen = factory();
}(this, function () {
  'use strict';

  const RUNTIME = `-- the screen design: its animations, live text and moving elements
_blk_vis = {notes = 0, note_at = -100, note = 60, level = 0, page = 1, over = {}, hide = {}, state = {}, t = 0, dt = 0}
do
  local V = _blk_vis
  local mu = require "musicutil"
  local UI = require "ui"
  local TAU = 2 * math.pi

  local function noise(n)
    local s = math.sin(n * 12.9898 + 78.233) * 43758.5453
    return s - math.floor(s)
  end
  -- a wave from 0 to 1 over x cycles; random holds a value per cycle
  local function wave(shape, x, seed)
    local phase = x % 1
    if shape == "triangle" then return phase < 0.5 and phase * 2 or 2 - phase * 2
    elseif shape == "saw" then return phase
    elseif shape == "square" then return phase < 0.5 and 1 or 0
    elseif shape == "random" then return noise(math.floor(x) + seed * 7.31)
    end
    return 0.5 - 0.5 * math.cos(phase * TAU)
  end
  local function seed(s)
    local n = 0
    for i = 1, #s do n = n + string.byte(s, i) * i end
    return n
  end

  function V.note_on(note, vel)
    V.notes = V.notes + 1
    V.note_at = util.time()
    V.note = note or V.note
  end
  function V.beat(key, beats, shape, from, to)
    return from + (to - from) * wave(shape, clock.get_beats() / math.max(beats, 0.01), seed(key))
  end
  function V.time(key, seconds, shape, from, to)
    return from + (to - from) * wave(shape, V.t / math.max(seconds, 0.01), seed(key))
  end
  function V.on_note(decay, from, to)
    local age = V.t - V.note_at
    local e = age < 0 and 0 or math.max(0, 1 - age / math.max(decay, 0.01)) ^ 2
    return from + (to - from) * e
  end
  function V.with_level(from, to) return from + (to - from) * util.clamp(V.level, 0, 1) end
  local function control_range(id)
    local ok, p = pcall(params.lookup_param, params, id)
    if not ok or not p then return nil end
    if p.controlspec then return p.controlspec.minval, p.controlspec.maxval end
    if p.count then return 1, p.count end
    if p.min then return p.min, p.max end
    return 0, 1
  end
  function V.ctl(id, from, to)
    local lo, hi = control_range(id)
    if not lo or hi == lo then return from end
    return from + (to - from) * util.clamp((params:get(id) - lo) / (hi - lo), 0, 1)
  end
  function V.loop(n, from, to)
    local L = _blk_loopers and _blk_loopers[n]
    if not L or L.len <= 0 then return from end
    return from + (to - from) * util.clamp(L.pos / L.len, 0, 1)
  end
  -- a block can set a value (or let it move by itself again)
  function V.p(id, prop, value)
    local o = V.over[id]
    if o and o[prop] ~= nil then return o[prop] end
    return value
  end
  function V.set(id, prop, value)
    V.over[id] = V.over[id] or {}
    V.over[id][prop] = value
  end

  local function pid(name) return (string.gsub(string.lower(name), "[^%w_]+", "_")) end
  function V.text(s)
    return (string.gsub(s, "{(.-)}", function(k)
      local key = string.lower(k)
      if key == "tempo" then return tostring(math.floor(clock.get_tempo() + 0.5)) end
      if key == "note" then return mu.note_num_to_name(math.floor(V.note), true) end
      if key == "beat" then return tostring(math.floor(clock.get_beats()) % 4 + 1) end
      if key == "page" then return tostring(V.page) end
      local ok, text = pcall(params.string, params, pid(k))
      if ok and text then return text end
      local v = rawget(_G, k)
      if v ~= nil then return tostring(v) end
      return "{" .. k .. "}"
    end))
  end

  local function level(l) screen.level(util.clamp(math.floor(l + 0.5), 0, 15)) end
  local function state(id, make)
    local s = V.state[id]
    if not s then s = make(); V.state[id] = s end
    return s
  end
  local function new_notes(s)
    local n = V.notes - (s.seen or V.notes)
    s.seen = V.notes
    return n
  end

  local D = {}
  V.draw = D
  function D.text(id, p)
    screen.font_size(math.floor(p.size))
    level(p.level)
    screen.move(p.x, p.y)
    local t = V.text(p.text)
    if p.align == "centre" then screen.text_center(t) elseif p.align == "right" then screen.text_right(t) else screen.text(t) end
    screen.font_size(8)
  end
  function D.rect(id, p)
    level(p.level)
    screen.rect(p.x, p.y, p.w, p.h)
    if p.fill == "filled" then screen.fill() else screen.stroke() end
  end
  function D.circle(id, p)
    level(p.level)
    screen.circle(p.x, p.y, math.max(p.r, 0.5))
    if p.fill == "filled" then screen.fill() else screen.stroke() end
  end
  function D.line(id, p)
    screen.line_width(math.max(1, math.floor(p.width)))
    level(p.level)
    screen.move(p.x1, p.y1)
    screen.line(p.x2, p.y2)
    screen.stroke()
    screen.line_width(1)
  end
  function D.arc(id, p)
    local a1, a2 = -math.pi / 2 + p.from * TAU, -math.pi / 2 + p.to * TAU
    if a2 <= a1 then return end
    screen.line_width(math.max(1, math.floor(p.width)))
    level(p.level)
    screen.move(p.x + p.r * math.cos(a1), p.y + p.r * math.sin(a1))
    screen.arc(p.x, p.y, p.r, a1, a2)
    screen.stroke()
    screen.line_width(1)
  end
  function D.bar(id, p)
    local v = util.clamp(p.value, 0, 1)
    level(math.max(1, p.level / 5))
    screen.rect(p.x, p.y, p.w, p.h)
    screen.stroke()
    level(p.level)
    if p.dir == "up" then screen.rect(p.x, p.y + p.h * (1 - v), p.w, p.h * v)
    else screen.rect(p.x, p.y, p.w * v, p.h) end
    screen.fill()
  end
  local function widget(id, p, make)
    local lo, hi = control_range(pid(p.control))
    if not lo then return end
    local s = V.state[id]
    if not s or s.control ~= p.control then
      s = {control = p.control, w = make(lo, hi, pid(p.control))}
      V.state[id] = s
    end
    s.w:set_value(params:get(pid(p.control)))
    s.w:redraw()
  end
  function D.dial(id, p)
    widget(id, p, function(lo, hi, cid)
      local ok, info = pcall(params.lookup_param, params, cid)
      return UI.Dial.new(p.x, p.y, p.size, lo, lo, hi, (hi - lo) > 10 and 1 or 0.01, lo, {}, "", ok and info.name or p.control)
    end)
    local s = V.state[id]
    if s then s.w.x, s.w.y, s.w.size = p.x, p.y, p.size end
  end
  function D.slider(id, p)
    widget(id, p, function(lo, hi)
      return UI.Slider.new(p.x, p.y, p.w, p.h, lo, lo, hi, {}, p.dir == "across" and "right" or "up")
    end)
    local s = V.state[id]
    if s then s.w.x, s.w.y, s.w.width, s.w.height = p.x, p.y, p.w, p.h end
  end
  function D.waveform(id, p)
    if _blk_looper_wave then _blk_looper_wave(math.floor(p.looper), p.x, p.y, p.w, p.h) else
      level(2)
      screen.move(p.x, p.y + p.h / 2)
      screen.line(p.x + p.w, p.y + p.h / 2)
      screen.stroke()
    end
  end
  function D.steps(id, p)
    local n = math.max(1, math.floor(p.count))
    local cur = tonumber(rawget(_G, p.var)) or 0
    local w = p.w / n
    for i = 1, n do
      level(i == cur and p.level or math.max(1, p.level / 5))
      screen.rect(p.x + (i - 1) * w, p.y, math.max(1, w - 1), p.h)
      screen.fill()
    end
  end
  function D.rings(id, p)
    local s = state(id, function() return {rings = {}} end)
    for _ = 1, math.min(new_notes(s), 4) do table.insert(s.rings, V.t) end
    local keep = {}
    for _, t0 in ipairs(s.rings) do
      local a = (V.t - t0) / math.max(p.time, 0.05)
      if a < 1 then
        keep[#keep + 1] = t0
        level(p.level * (1 - a))
        screen.circle(p.x, p.y, math.max(0.5, p.r * a))
        screen.stroke()
      end
    end
    s.rings = keep
  end
  function D.sparks(id, p)
    local s = state(id, function() return {sparks = {}} end)
    for _ = 1, math.min(new_notes(s), 3) do
      for _ = 1, math.floor(p.count) do
        local a, v = math.random() * TAU, (0.4 + math.random() * 0.6) * p.r * 1.5
        table.insert(s.sparks, {x = p.x, y = p.y, vx = math.cos(a) * v, vy = math.sin(a) * v, t0 = V.t})
      end
    end
    local keep = {}
    for _, k in ipairs(s.sparks) do
      local a = V.t - k.t0
      if a < 1 then
        k.vy = k.vy + p.gravity * V.dt * 2
        k.x, k.y = k.x + k.vx * V.dt, k.y + k.vy * V.dt
        keep[#keep + 1] = k
        level(p.level * (1 - a))
        screen.rect(math.floor(k.x), math.floor(k.y), 1, 1)
        screen.fill()
      end
    end
    s.sparks = keep
  end
  function D.orbit(id, p)
    local turn = clock.get_beats() / math.max(p.beats, 0.01)
    level(p.level)
    for k = 0, math.floor(p.dots) - 1 do
      local a = (turn + k / math.floor(p.dots)) * TAU - math.pi / 2
      screen.circle(p.x + p.r * math.cos(a), p.y + p.r * math.sin(a), math.max(0.5, p.size / 2))
      screen.fill()
    end
  end
  function D.wave(id, p)
    local shift = clock.get_beats() / math.max(p.beats, 0.01)
    level(p.level)
    for px = 0, 128, 2 do
      local y = p.y + p.height * math.sin(TAU * (p.cycles * px / 128 - shift))
      if px == 0 then screen.move(px, y) else screen.line(px, y) end
    end
    screen.stroke()
  end
  function D.spiral(id, p)
    local rot = clock.get_beats() / math.max(p.beats, 0.01) * TAU
    local n = math.floor(p.turns * 24)
    level(p.level)
    for i = 0, n do
      local t = i / n
      local a, r = t * p.turns * TAU + rot, t * p.r
      if i == 0 then screen.move(p.x + r * math.cos(a), p.y + r * math.sin(a)) else screen.line(p.x + r * math.cos(a), p.y + r * math.sin(a)) end
    end
    screen.stroke()
  end
  function D.lissajous(id, p)
    local d = clock.get_beats() / math.max(p.beats, 0.01) * TAU
    level(p.level)
    for i = 0, 126 do
      local t = i / 126 * TAU
      local x = p.x + p.w / 2 + p.w / 2 * math.sin(math.floor(p.a) * t + d)
      local y = p.y + p.h / 2 + p.h / 2 * math.sin(math.floor(p.b) * t)
      if i == 0 then screen.move(x, y) else screen.line(x, y) end
    end
    screen.stroke()
  end
  function D.stars(id, p)
    local s = state(id, function() return {stars = {}} end)
    local n = math.floor(p.count)
    while #s.stars < n do table.insert(s.stars, {x = math.random() * 2 - 1, y = math.random() * 2 - 1, z = math.random()}) end
    while #s.stars > n do table.remove(s.stars) end
    for _, k in ipairs(s.stars) do
      k.z = k.z - p.speed * V.dt * 0.4
      local sx, sy = 64 + k.x / math.max(k.z, 0.01) * 32, 32 + k.y / math.max(k.z, 0.01) * 32
      if k.z <= 0.02 or sx < 0 or sx > 127 or sy < 0 or sy > 63 then
        k.x, k.y, k.z = math.random() * 2 - 1, math.random() * 2 - 1, 1
      else
        level(p.level * (1 - k.z))
        screen.rect(math.floor(sx), math.floor(sy), 1, 1)
        screen.fill()
      end
    end
  end
  function D.rain(id, p)
    local s = state(id, function() return {drops = {}} end)
    local n = math.floor(p.count)
    while #s.drops < n do table.insert(s.drops, {x = math.random() * 128, y = math.random() * 64, v = 0.5 + math.random()}) end
    while #s.drops > n do table.remove(s.drops) end
    level(p.level)
    for _, k in ipairs(s.drops) do
      k.y = k.y + p.speed * k.v * V.dt * 60
      if k.y > 64 + p.length then k.y, k.x = -p.length, math.random() * 128 end
      screen.move(k.x, k.y)
      screen.line(k.x, k.y + p.length)
      screen.stroke()
    end
  end
  function D.noise(id, p)
    local n = math.min(300, math.floor(p.w * p.h * p.density))
    level(p.level)
    for _ = 1, n do
      screen.rect(math.floor(p.x + math.random() * p.w), math.floor(p.y + math.random() * p.h), 1, 1)
      screen.fill()
    end
  end
  function D.bounce(id, p)
    local s = state(id, function() return {x = 20, y = 20, vx = 1, vy = 0.7} end)
    local step = p.speed * V.dt * 40
    s.x, s.y = s.x + s.vx * step, s.y + s.vy * step
    local r = p.size
    if s.x < r then s.x, s.vx = r, math.abs(s.vx) elseif s.x > 127 - r then s.x, s.vx = 127 - r, -math.abs(s.vx) end
    if s.y < r then s.y, s.vy = r, math.abs(s.vy) elseif s.y > 63 - r then s.y, s.vy = 63 - r, -math.abs(s.vy) end
    level(p.level)
    screen.circle(s.x, s.y, math.max(0.5, r))
    screen.fill()
  end

  -- each frame: the time since the last one, for everything that moves by itself
  function V.frame()
    local now = util.time()
    V.dt = util.clamp(now - (V.t > 0 and V.t or now), 0, 0.2)
    V.t = now
  end
  function V.show(id, page)
    return not V.hide[id] and (page == 0 or page == V.page)
  end
end`;

  const num = v => String(Number(Number(v).toFixed(4)));
  const str = s => '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n') + '"';
  const pid = name => String(name).trim().toLowerCase().replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '') || 'control';

  function index(catalog) {
    const t = {};
    for (const e of catalog.elements) t[e.type] = e;
    return t;
  }

  // the design with every prop filled in, unknown layers left out
  function normalize(design, catalog) {
    const types = index(catalog);
    const layers = ((design && design.layers) || []).filter(l => types[l.type]).map(l => {
      const props = {};
      for (const p of types[l.type].props.concat(catalog.common)) {
        const given = l.props && l.props[p.name];
        const entry = {v: given && given.v !== undefined ? given.v : p.default};
        if (p.type === 'number') {
          entry.v = Math.max(p.min, Math.min(p.max, Number(entry.v)));
          if (given && given.anim && given.anim.kind) entry.anim = given.anim;
        }
        props[p.name] = entry;
      }
      return {id: String(l.id), type: l.type, hidden: !!l.hidden, props};
    });
    return {layers};
  }

  // the Lua of one number prop: fixed, or moving
  function valueLua(layerId, name, entry, uses) {
    const a = entry.anim;
    if (!a) return num(entry.v);
    const from = num(a.from !== undefined ? a.from : entry.v), to = num(a.to !== undefined ? a.to : entry.v);
    const key = str(layerId + '.' + name);
    switch (a.kind) {
      case 'beat': return '_blk_vis.beat(' + key + ', ' + num(a.beats || 4) + ', ' + str(a.shape || 'sine') + ', ' + from + ', ' + to + ')';
      case 'time': return '_blk_vis.time(' + key + ', ' + num(a.seconds || 2) + ', ' + str(a.shape || 'sine') + ', ' + from + ', ' + to + ')';
      case 'note': uses.notes = true; return '_blk_vis.on_note(' + num(a.decay || 0.5) + ', ' + from + ', ' + to + ')';
      case 'level': uses.level = true; return '_blk_vis.with_level(' + from + ', ' + to + ')';
      case 'control': return '_blk_vis.ctl(' + str(pid(a.control || 'cutoff')) + ', ' + from + ', ' + to + ')';
      case 'looper': return '_blk_vis.loop(' + Math.round(Number(a.looper) || 1) + ', ' + from + ', ' + to + ')';
      default: return num(entry.v);
    }
  }

  // {runtime, draw, uses}: the runtime library, function _blk_design(), what it listens to
  function lua(design, catalog) {
    const types = index(catalog);
    const n = normalize(design, catalog);
    const uses = {notes: false, level: false, layers: n.layers.length};
    const lines = ['-- the screen design (the "screen" page), drawn first in redraw()', 'function _blk_design()', '  _blk_vis.frame()'];
    for (const l of n.layers) {
      if (l.hidden) continue;
      const def = types[l.type];
      if (['rings', 'sparks'].includes(l.type)) uses.notes = true;
      const props = def.props.map(p => {
        const e = l.props[p.name];
        const value = p.type === 'number' ? valueLua(l.id, p.name, e, uses) : str(e.v);
        return p.name + ' = _blk_vis.p(' + str(l.id) + ', ' + str(p.name) + ', ' + value + ')';
      });
      lines.push('  if _blk_vis.show(' + str(l.id) + ', ' + Math.round(l.props.page.v) + ') then',
        '    _blk_vis.draw.' + l.type + '(' + str(l.id) + ', {' + props.join(', ') + '})', '  end');
    }
    lines.push('end');
    return {runtime: RUNTIME, draw: lines.join('\n'), uses};
  }

  // [label, id] of the layers, and [label, prop] of a layer's number props: for the blocks
  function layerChoices(design, catalog) {
    return normalize(design, catalog).layers.map(l => [l.id, l.id]);
  }
  function propChoices(design, catalog, id) {
    const types = index(catalog);
    const l = normalize(design, catalog).layers.find(x => x.id === id);
    if (!l) return [];
    return types[l.type].props.concat(catalog.common).filter(p => p.type === 'number').map(p => [p.label, p.name]);
  }

  return {normalize, lua, layerChoices, propChoices, RUNTIME};
}));
