// blocks_looper.js: the Looper blocks: up to six loopers on softcut (norns' tape voices), each
// with its own stretch of a buffer: record a loop of any length (the first recording sets it)
// or a set number of beats, overdub, change speed and direction, load a file, draw the
// waveform. a kit of blocks.js: browser after blocks.js; node: require(...)(VerdandiBlocks).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory;
  else factory(root.VerdandiBlocks);
}(this, function (VB) {
  'use strict';
  const C = 45;
  const n = VB.num, b = VB.entry;
  const num = name => ({type: 'input_value', name, check: 'Number'});
  const which = {type: 'field_dropdown', name: 'N', options: [1, 2, 3, 4, 5, 6].map(i => [String(i), String(i)])};

  // the Lua the looper blocks call, added to a script once
  const LIB = `-- loopers: softcut voices 1 to 6; loopers 1-3 share buffer 1, 4-6 buffer 2, 116 s apart
_blk_loopers = {}
function _blk_looper_setup(n, max, src)
  n = util.clamp(math.floor(n), 1, 6)
  local L = {n = n, buf = n <= 3 and 1 or 2, start = ((n - 1) % 3) * 116 + 1,
    max = util.clamp(max, 1, 110), len = 0, pos = 0, state = "empty", wave = {}}
  _blk_loopers[n] = L
  if src == "in" or src == "both" then audio.level_adc_cut(1) end
  if src == "sound" or src == "both" then audio.level_eng_cut(1) end
  softcut.enable(n, 1)
  softcut.buffer(n, L.buf)
  softcut.level(n, 1)
  softcut.pan(n, 0)
  softcut.rate(n, 1)
  softcut.loop(n, 1)
  softcut.loop_start(n, L.start)
  softcut.loop_end(n, L.start + L.max)
  softcut.position(n, L.start)
  softcut.play(n, 0)
  softcut.rec(n, 0)
  softcut.rec_level(n, 1)
  softcut.pre_level(n, 1)
  softcut.fade_time(n, 0.02)
  softcut.level_slew_time(n, 0.05)
  softcut.rate_slew_time(n, 0.05)
  softcut.recpre_slew_time(n, 0.01)
  softcut.level_input_cut(1, n, src == "none" and 0 or 1)
  softcut.level_input_cut(2, n, src == "none" and 0 or 1)
  softcut.phase_quant(n, 1 / 30)
  softcut.buffer_clear_region_channel(L.buf, L.start, L.max + 1)
  if not _blk_looper_events then
    _blk_looper_events = true
    softcut.event_phase(function(v, pos)
      local L = _blk_loopers[v]
      if L then L.pos = pos - L.start end
    end)
    softcut.event_render(function(ch, start, sec, samples)
      for _, L in pairs(_blk_loopers) do
        if L.buf == ch and math.abs(L.start - start) < 0.01 then L.wave = samples end
      end
    end)
    softcut.poll_start_phase()
  end
end

function _blk_looper_render(L, width)
  L.rendered = util.time()
  local dur = L.state == "recording" and math.max(L.pos, 0.05) or L.len
  if dur > 0 then softcut.render_buffer(L.buf, L.start, dur, width or 64) end
end

function _blk_looper(n, action)
  local L = _blk_loopers[n]
  if not L then return end
  if action == "record" then
    softcut.buffer_clear_region_channel(L.buf, L.start, L.max + 1)
    if L.fixed then softcut.loop_end(n, L.start + L.len) else
      L.len = 0
      softcut.loop_end(n, L.start + L.max)
    end
    softcut.position(n, L.start)
    softcut.pre_level(n, L.keep or 1)
    softcut.rec(n, 1)
    softcut.play(n, 1)
    L.pos, L.wave, L.state = 0, {}, "recording"
  elseif action == "finish" then
    if L.state == "recording" then
      softcut.rec(n, 0)
      if not L.fixed then
        L.len = util.clamp(L.pos, 0.05, L.max)
        softcut.loop_end(n, L.start + L.len)
        softcut.position(n, L.start)
      end
      L.state = "playing"
      _blk_looper_render(L)
    end
  elseif action == "overdub" then
    if L.len > 0 then
      softcut.rec(n, 1)
      softcut.play(n, 1)
      L.state = "overdubbing"
    end
  elseif action == "overdub off" then
    softcut.rec(n, 0)
    if L.state == "overdubbing" then L.state = "playing" end
    _blk_looper_render(L)
  elseif action == "play" then
    if L.len > 0 then
      softcut.play(n, 1)
      if L.state ~= "overdubbing" then L.state = "playing" end
    end
  elseif action == "stop" then
    softcut.play(n, 0)
    softcut.rec(n, 0)
    if L.len > 0 then L.state = "stopped" end
  elseif action == "clear" then
    softcut.play(n, 0)
    softcut.rec(n, 0)
    softcut.buffer_clear_region_channel(L.buf, L.start, L.max + 1)
    if not L.fixed then L.len = 0 end
    L.pos, L.wave, L.state = 0, {}, "empty"
  elseif action == "restart" then
    softcut.position(n, L.start)
  end
end

function _blk_looper_set(n, what, v)
  local L = _blk_loopers[n]
  if not L then return end
  if what == "speed" then softcut.rate(n, v)
  elseif what == "volume" then softcut.level(n, util.clamp(v, 0, 2))
  elseif what == "pan" then softcut.pan(n, util.clamp(v, -1, 1))
  elseif what == "keep" then
    L.keep = util.clamp(v, 0, 1)
    softcut.pre_level(n, L.keep)
  elseif what == "beats" then
    L.fixed = v > 0
    L.len = util.clamp(v * clock.get_beat_sec(), 0.05, L.max)
    softcut.loop_end(n, L.start + L.len)
  end
end

function _blk_looper_jump(n, where)
  local L = _blk_loopers[n]
  if L and L.len > 0 then softcut.position(n, L.start + util.clamp(where, 0, 1) * L.len) end
end

function _blk_looper_load(n, file)
  local L = _blk_loopers[n]
  if not L then return end
  local path = string.sub(file, 1, 1) == "/" and file or _path.audio .. file
  local ch, samples, rate = audio.file_info(path)
  if not samples or samples <= 0 then print("looper: no sound file " .. path) return end
  L.len = util.clamp(samples / rate, 0.05, L.max)
  L.fixed = true
  softcut.buffer_clear_region_channel(L.buf, L.start, L.max + 1)
  softcut.buffer_read_mono(path, 0, L.start, L.len, 1, L.buf)
  softcut.loop_end(n, L.start + L.len)
  softcut.position(n, L.start)
  softcut.play(n, 1)
  L.state = "playing"
  _blk_looper_render(L)
end

function _blk_looper_value(n, what)
  local L = _blk_loopers[n]
  if not L then return what == "is" and false or 0 end
  if what == "position" then return L.len > 0 and util.clamp(L.pos / L.len, 0, 1) or 0 end
  if what == "length" then return L.state == "recording" and not L.fixed and L.pos or L.len end
  return 0
end

function _blk_looper_wave(n, x, y, w, h)
  local L = _blk_loopers[n]
  if not L then return end
  if L.state == "recording" or L.state == "overdubbing" then
    if util.time() - (L.rendered or 0) > 0.25 then _blk_looper_render(L, w) end
  elseif #L.wave ~= math.floor(w) and L.len > 0 and util.time() - (L.rendered or 0) > 0.5 then
    _blk_looper_render(L, w)
  end
  local mid = y + h / 2
  screen.level(4)
  for i, s in ipairs(L.wave) do
    local a = math.max(0.5, math.abs(s) * h / 2)
    screen.move(x + i - 0.5, mid - a)
    screen.line(x + i - 0.5, mid + a)
  end
  screen.stroke()
  screen.level(1)
  screen.move(x, mid)
  screen.line(x + w, mid)
  screen.stroke()
  local len = _blk_looper_value(n, "length")
  if len > 0 then
    local at = L.state == "recording" and not L.fixed and 1 or util.clamp(L.pos / len, 0, 1)
    screen.level(15)
    screen.move(x + at * w, y)
    screen.line(x + at * w, y + h)
    screen.stroke()
  end
end`;

  VB.kit({
    name: 'looper',
    defs: [
      {type: 'nv_looper_setup', message0: 'set up looper %1 for loops up to %2 seconds, recording %3', args0: [
        which, {type: 'field_number', name: 'MAX', value: 30, min: 1, max: 110},
        {type: 'field_dropdown', name: 'SRC', options: [['the audio input', 'in'], ['the sound', 'sound'],
          ['the input and the sound', 'both'], ['nothing (for files)', 'none']]}],
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'a looper on a softcut voice; set it up in "when the script starts"'},
      {type: 'nv_looper_do', message0: 'looper %1: %2', args0: [which,
        {type: 'field_dropdown', name: 'ACTION', options: [['record a new loop', 'record'],
          ['finish recording (sets the length)', 'finish'], ['overdub on', 'overdub'], ['overdub off', 'overdub off'],
          ['play', 'play'], ['stop', 'stop'], ['clear', 'clear'], ['back to the start', 'restart']]}],
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'record, then finish: the time in between is the loop. overdub records on top while it plays'},
      {type: 'nv_looper_set', message0: 'looper %1 %2 %3', args0: [which,
        {type: 'field_dropdown', name: 'WHAT', options: [['speed (1 normal, -1 backwards)', 'speed'],
          ['volume', 'volume'], ['pan', 'pan'], ['overdub keeps (0 to 1)', 'keep'], ['length in beats', 'beats']]},
        num('VALUE')], inputsInline: true, previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'speed 2 is an octave up, 0.5 an octave down, -1 backwards. a length in beats makes every recording that long'},
      {type: 'nv_looper_jump', message0: 'looper %1 jump to %2 (0 start, 1 end)', args0: [which, num('WHERE')],
       inputsInline: true, previousStatement: null, nextStatement: null, colour: C},
      {type: 'nv_looper_load', message0: 'load sound file %1 into looper %2', args0: [
        {type: 'field_input', name: 'FILE', text: 'my_loop.wav'}, which],
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'a file in dust/audio (or a full path); it plays at once, as long as the looper allows'},
      {type: 'nv_looper_pos', message0: 'position of looper %1 (0 to 1)', args0: [which], output: 'Number', colour: C},
      {type: 'nv_looper_len', message0: 'length of looper %1 (seconds)', args0: [which], output: 'Number', colour: C},
      {type: 'nv_looper_is', message0: 'looper %1 is %2', args0: [which,
        {type: 'field_dropdown', name: 'STATE', options: [['recording', 'recording'], ['playing', 'playing'],
          ['overdubbing', 'overdubbing'], ['stopped', 'stopped'], ['empty', 'empty']]}], output: 'Boolean', colour: C},
      {type: 'nv_looper_wave', message0: 'waveform of looper %1 at x %2 y %3 width %4 height %5', args0: [which,
        num('X'), num('Y'), num('W'), num('H')], inputsInline: true,
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'the loop as a picture, with the playhead'},
    ],
    context: {
      nv_looper_setup: [['nv_init'], 'set up loopers in "when the script starts"'],
      nv_looper_wave: [['nv_redraw', 'procedures_defnoreturn', 'procedures_defreturn'], 'draws inside "draw the screen"'],
    },
    generators(F, ctx) {
      const {O, v, luaString} = ctx;
      const lib = () => ctx.define('looper_lib', LIB);
      const nOf = bl => bl.getFieldValue('N');
      F.nv_looper_setup = bl => {
        lib();
        return '_blk_looper_setup(' + nOf(bl) + ', ' + Number(bl.getFieldValue('MAX')) + ', ' + luaString(bl.getFieldValue('SRC')) + ')\n';
      };
      F.nv_looper_do = bl => { lib(); return '_blk_looper(' + nOf(bl) + ', ' + luaString(bl.getFieldValue('ACTION')) + ')\n'; };
      F.nv_looper_set = bl => { lib(); return '_blk_looper_set(' + nOf(bl) + ', ' + luaString(bl.getFieldValue('WHAT')) + ', ' + v(bl, 'VALUE') + ')\n'; };
      F.nv_looper_jump = bl => { lib(); return '_blk_looper_jump(' + nOf(bl) + ', ' + v(bl, 'WHERE') + ')\n'; };
      F.nv_looper_load = bl => { lib(); return '_blk_looper_load(' + nOf(bl) + ', ' + luaString(bl.getFieldValue('FILE')) + ')\n'; };
      F.nv_looper_pos = bl => { lib(); return ['_blk_looper_value(' + nOf(bl) + ', "position")', O.HIGH]; };
      F.nv_looper_len = bl => { lib(); return ['_blk_looper_value(' + nOf(bl) + ', "length")', O.HIGH]; };
      F.nv_looper_is = bl => {
        lib();
        return ['((_blk_loopers[' + nOf(bl) + '] or {}).state == ' + luaString(bl.getFieldValue('STATE')) + ')', O.ATOMIC];
      };
      F.nv_looper_wave = bl => {
        lib();
        return '_blk_looper_wave(' + nOf(bl) + ', ' + v(bl, 'X') + ', ' + v(bl, 'Y') + ', ' + v(bl, 'W') + ', ' + v(bl, 'H') + ')\n';
      };
    },
    toolbox: [{name: 'Looper', colour: C, contents: [
      b('nv_looper_setup'), b('nv_looper_do'), b('nv_looper_set', {VALUE: n(1)}),
      b('nv_looper_jump', {WHERE: n(0)}), b('nv_looper_load'),
      b('nv_looper_pos'), b('nv_looper_len'), b('nv_looper_is'),
      b('nv_looper_wave', {X: n(0), Y: n(20), W: n(128), H: n(30)})]}],
  });
}));
