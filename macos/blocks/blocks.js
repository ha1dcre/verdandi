// blocks.js: the norns blocks of the Verdandi script builder and the Lua they write.
//
// the builder writes an ordinary norns script: every event block ("when script starts",
// "every 1/4 beat", "when key K2 is pressed", ...) becomes part of init(), key(), enc(),
// redraw() or a clock coroutine; the rest is Blockly's own Lua generator (logic, loops,
// math, text, lists, variables, functions). the script runs on any norns, not only here.
//
// browser: loaded after Blockly, defines window.VerdandiBlocks.
// node (macos/tests/blocks_test.js): module.exports = {register}.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.VerdandiBlocks = factory();
}(this, function () {
  'use strict';

  // the event blocks: each one is a top-level block with a statement input DO
  const HATS = ['nv_init', 'nv_every', 'nv_key', 'nv_enc', 'nv_redraw', 'nv_midi_note'];

  // names a script must not take for its own variables and functions: norns globals,
  // the arguments of the generated callbacks and the builder's own helpers (_blk_*)
  const RESERVED = 'init,key,enc,redraw,cleanup,engine,screen,clock,params,midi,grid,arc,' +
    'softcut,audio,poll,metro,osc,crow,norns,util,tab,musicutil,sequins,controlspec,lattice,' +
    'hid,keyboard,_norns,_path,UI,n,z,d,msg,note,vel,data';

  const SCALES = ['Major', 'Natural Minor', 'Harmonic Minor', 'Melodic Minor', 'Dorian',
    'Phrygian', 'Lydian', 'Mixolydian', 'Locrian', 'Major Pentatonic', 'Minor Pentatonic',
    'Blues Scale', 'Whole Tone', 'Chromatic'];

  const DIVISIONS = [['1/4 beat', '1/4'], ['1/8 beat', '1/8'], ['1/2 beat', '1/2'],
    ['1 beat', '1'], ['2 beats', '2'], ['4 beats (a bar)', '4'], ['1/3 beat (triplets)', '1/3'],
    ['1/16 beat', '1/16']];

  const C = {event: 20, sound: 330, music: 290, time: 200, screen: 160, widget: 130, control: 60, midi: 355};

  // a value input with a number shadow, for toolbox entries
  function num(n) { return {shadow: {type: 'math_number', fields: {NUM: n}}}; }
  function txt(s) { return {shadow: {type: 'text', fields: {TEXT: s}}}; }

  const DEFS = [
    // events
    {type: 'nv_init', message0: 'when the script starts %1 %2', args0: [{type: 'input_dummy'},
      {type: 'input_statement', name: 'DO'}], colour: C.event,
     tooltip: 'runs once when the script loads: set things up here'},
    {type: 'nv_every', message0: 'every %1 %2 %3', args0: [
      {type: 'field_dropdown', name: 'DIV', options: DIVISIONS}, {type: 'input_dummy'},
      {type: 'input_statement', name: 'DO'}], colour: C.event,
     tooltip: 'repeats in time with the norns clock (tempo in PARAMETERS > CLOCK)'},
    {type: 'nv_key', message0: 'when key %1 is %2 %3 %4', args0: [
      {type: 'field_dropdown', name: 'KEY', options: [['K2', '2'], ['K3', '3'], ['K1', '1']]},
      {type: 'field_dropdown', name: 'STATE', options: [['pressed', '1'], ['released', '0']]},
      {type: 'input_dummy'}, {type: 'input_statement', name: 'DO'}], colour: C.event,
     tooltip: 'runs when a key of the norns is pressed or released'},
    {type: 'nv_enc', message0: 'when encoder %1 turns %2 %3', args0: [
      {type: 'field_dropdown', name: 'ENC', options: [['E2', '2'], ['E3', '3'], ['E1', '1']]},
      {type: 'input_dummy'}, {type: 'input_statement', name: 'DO'}], colour: C.event,
     tooltip: 'runs on every step of an encoder; "turn amount" is +1 or -1 (more when turned fast)'},
    {type: 'nv_redraw', message0: 'draw the screen %1 %2', args0: [{type: 'input_dummy'},
      {type: 'input_statement', name: 'DO'}], colour: C.event,
     tooltip: 'draws the 128 x 64 screen, 15 times a second; it is cleared before each drawing'},
    {type: 'nv_midi_note', message0: 'when a MIDI note %1 arrives %2 %3', args0: [
      {type: 'field_dropdown', name: 'TYPE', options: [['starts', 'on'], ['ends', 'off']]},
      {type: 'input_dummy'}, {type: 'input_statement', name: 'DO'}], colour: C.midi,
     tooltip: 'notes from the MIDI device in slot 1 (SYSTEM > DEVICES > MIDI)'},
    {type: 'nv_enc_delta', message0: 'turn amount', output: 'Number', colour: C.event,
     tooltip: 'how far the encoder turned: positive clockwise, negative counter-clockwise'},
    {type: 'nv_midi_note_num', message0: 'MIDI note number', output: 'Number', colour: C.midi},
    {type: 'nv_midi_velocity', message0: 'MIDI velocity', output: 'Number', colour: C.midi},

    // MIDI out
    {type: 'nv_midi_send', message0: 'send MIDI note %1 velocity %2 for %3 beats on channel %4',
     args0: [{type: 'input_value', name: 'NOTE', check: 'Number'},
       {type: 'input_value', name: 'VEL', check: 'Number'},
       {type: 'input_value', name: 'LEN', check: 'Number'},
       {type: 'input_value', name: 'CH', check: 'Number'}], inputsInline: true,
     previousStatement: null, nextStatement: null, colour: C.midi,
     tooltip: 'to the MIDI device in slot 1; the note ends by itself'},

    // music
    {type: 'nv_scale_note', message0: 'note %1 of scale %2 from %3', args0: [
      {type: 'input_value', name: 'DEGREE', check: 'Number'},
      {type: 'field_dropdown', name: 'SCALE', options: SCALES.map(s => [s, s])},
      {type: 'input_value', name: 'ROOT', check: 'Number'}], inputsInline: true,
     output: 'Number', colour: C.music,
     tooltip: 'step 1 is the root note; higher steps climb the scale through the octaves'},
    {type: 'nv_note_name', message0: 'name of note %1', args0: [{type: 'input_value',
      name: 'NOTE', check: 'Number'}], output: 'String', colour: C.music,
     tooltip: 'for example 60 gives C4'},
    {type: 'nv_seq_make', message0: 'sequence of %1', args0: [{type: 'input_value', name: 'LIST',
      check: 'Array'}], output: 'Sequence', colour: C.music,
     tooltip: 'a sequins: store it in a variable, then take "next from" it'},
    {type: 'nv_seq_next', message0: 'next from %1', args0: [{type: 'field_variable', name: 'VAR',
      variable: 'melody'}], output: null, colour: C.music,
     tooltip: 'the next value of a sequence; after the last one it starts over'},

    // time
    {type: 'nv_wait', message0: 'wait %1 beats', args0: [{type: 'input_value', name: 'BEATS',
      check: 'Number'}], previousStatement: null, nextStatement: null, colour: C.time,
     tooltip: 'only inside "every ..." blocks'},
    {type: 'nv_tempo_set', message0: 'set tempo to %1 BPM', args0: [{type: 'input_value',
      name: 'BPM', check: 'Number'}], previousStatement: null, nextStatement: null, colour: C.time},
    {type: 'nv_tempo_get', message0: 'tempo (BPM)', output: 'Number', colour: C.time},

    // screen
    {type: 'nv_screen_text', message0: 'write %1 at x %2 y %3 brightness %4 %5', args0: [
      {type: 'input_value', name: 'TEXT'}, {type: 'input_value', name: 'X', check: 'Number'},
      {type: 'input_value', name: 'Y', check: 'Number'},
      {type: 'input_value', name: 'LEVEL', check: 'Number'},
      {type: 'field_dropdown', name: 'ALIGN', options: [['from the left', 'text'],
        ['centred', 'text_center'], ['from the right', 'text_right']]}], inputsInline: true,
     previousStatement: null, nextStatement: null, colour: C.screen,
     tooltip: 'x 0 to 127, y 0 to 63 (y is the bottom of the letters), brightness 0 to 15'},
    {type: 'nv_screen_rect', message0: '%1 rectangle at x %2 y %3 width %4 height %5 brightness %6',
     args0: [{type: 'field_dropdown', name: 'MODE', options: [['filled', 'fill'], ['outlined', 'stroke']]},
       {type: 'input_value', name: 'X', check: 'Number'}, {type: 'input_value', name: 'Y', check: 'Number'},
       {type: 'input_value', name: 'W', check: 'Number'}, {type: 'input_value', name: 'H', check: 'Number'},
       {type: 'input_value', name: 'LEVEL', check: 'Number'}], inputsInline: true,
     previousStatement: null, nextStatement: null, colour: C.screen},
    {type: 'nv_screen_circle', message0: '%1 circle at x %2 y %3 radius %4 brightness %5',
     args0: [{type: 'field_dropdown', name: 'MODE', options: [['filled', 'fill'], ['outlined', 'stroke']]},
       {type: 'input_value', name: 'X', check: 'Number'}, {type: 'input_value', name: 'Y', check: 'Number'},
       {type: 'input_value', name: 'R', check: 'Number'},
       {type: 'input_value', name: 'LEVEL', check: 'Number'}], inputsInline: true,
     previousStatement: null, nextStatement: null, colour: C.screen},
    {type: 'nv_screen_line', message0: 'line from x %1 y %2 to x %3 y %4 brightness %5', args0: [
      {type: 'input_value', name: 'X1', check: 'Number'}, {type: 'input_value', name: 'Y1', check: 'Number'},
      {type: 'input_value', name: 'X2', check: 'Number'}, {type: 'input_value', name: 'Y2', check: 'Number'},
      {type: 'input_value', name: 'LEVEL', check: 'Number'}], inputsInline: true,
     previousStatement: null, nextStatement: null, colour: C.screen},

    {type: 'nv_screen_pixel', message0: 'pixel at x %1 y %2 brightness %3', args0: [
      {type: 'input_value', name: 'X', check: 'Number'}, {type: 'input_value', name: 'Y', check: 'Number'},
      {type: 'input_value', name: 'LEVEL', check: 'Number'}], inputsInline: true,
     previousStatement: null, nextStatement: null, colour: C.screen},
    {type: 'nv_screen_font_size', message0: 'text size %1', args0: [{type: 'input_value',
      name: 'SIZE', check: 'Number'}], previousStatement: null, nextStatement: null, colour: C.screen,
     tooltip: 'for the "write" blocks after it; 8 is the norns size'},
    {type: 'nv_frame', message0: 'frame number', output: 'Number', colour: C.screen,
     tooltip: 'counts up with every drawing (15 a second): use it to move things'},
    {type: 'nv_beats', message0: 'beats since start', output: 'Number', colour: C.screen,
     tooltip: 'the clock position in beats: animations that move with the tempo'},

    // widgets: the ready-made norns ones (lua/lib/ui.lua), each showing a control
    {type: 'nv_ui_dial', message0: 'dial for control %1 at x %2 y %3 size %4', args0: [
      {type: 'field_input', name: 'NAME', text: 'cutoff'},
      {type: 'input_value', name: 'X', check: 'Number'}, {type: 'input_value', name: 'Y', check: 'Number'},
      {type: 'input_value', name: 'SIZE', check: 'Number'}], inputsInline: true,
     previousStatement: null, nextStatement: null, colour: C.widget,
     tooltip: 'the norns dial, with the control\'s name and value'},
    {type: 'nv_ui_slider', message0: '%1 slider for control %2 at x %3 y %4 width %5 height %6', args0: [
      {type: 'field_dropdown', name: 'DIR', options: [['upright', 'up'], ['sideways', 'right']]},
      {type: 'field_input', name: 'NAME', text: 'cutoff'},
      {type: 'input_value', name: 'X', check: 'Number'}, {type: 'input_value', name: 'Y', check: 'Number'},
      {type: 'input_value', name: 'W', check: 'Number'}, {type: 'input_value', name: 'H', check: 'Number'}],
     inputsInline: true, previousStatement: null, nextStatement: null, colour: C.widget},
    {type: 'nv_ui_playback', message0: 'play symbol at x %1 y %2 size %3 showing %4', args0: [
      {type: 'input_value', name: 'X', check: 'Number'}, {type: 'input_value', name: 'Y', check: 'Number'},
      {type: 'input_value', name: 'SIZE', check: 'Number'},
      {type: 'input_value', name: 'PLAYING', check: 'Boolean'}], inputsInline: true,
     previousStatement: null, nextStatement: null, colour: C.widget,
     tooltip: 'a play triangle when the value is true, a stop square when it is false'},

    // controls: norns parameters (PARAMETERS menu, MIDI mappable, saved in PSETs)
    {type: 'nv_param_add', message0: 'add control %1 from %2 to %3 starting at %4', args0: [
      {type: 'field_input', name: 'NAME', text: 'cutoff'},
      {type: 'field_number', name: 'MIN', value: 0}, {type: 'field_number', name: 'MAX', value: 100},
      {type: 'field_number', name: 'DEFAULT', value: 50}], previousStatement: null,
     nextStatement: null, colour: C.control,
     tooltip: 'a parameter in the PARAMETERS menu; it can be mapped to MIDI and saved in PSETs. ' +
       'add controls in "when the script starts"'},
    {type: 'nv_param_get', message0: 'control %1', args0: [{type: 'field_input', name: 'NAME',
      text: 'cutoff'}], output: 'Number', colour: C.control},
    {type: 'nv_param_set', message0: 'set control %1 to %2', args0: [{type: 'field_input',
      name: 'NAME', text: 'cutoff'}, {type: 'input_value', name: 'VALUE', check: 'Number'}],
     previousStatement: null, nextStatement: null, colour: C.control},
    {type: 'nv_param_change', message0: 'change control %1 by %2', args0: [{type: 'field_input',
      name: 'NAME', text: 'cutoff'}, {type: 'input_value', name: 'DELTA', check: 'Number'}],
     previousStatement: null, nextStatement: null, colour: C.control,
     tooltip: 'adds to a control, kept inside its range: "change control cutoff by turn amount"'},
  ];
  const TYPES = DEFS.map(d => d.type);

  // blocks that only make sense inside a certain event block, with the warning shown otherwise
  const CONTEXT = {
    nv_enc_delta: [['nv_enc'], '"turn amount" only works inside "when encoder ... turns"'],
    nv_midi_note_num: [['nv_midi_note'], 'only works inside "when a MIDI note ... arrives"'],
    nv_midi_velocity: [['nv_midi_note'], 'only works inside "when a MIDI note ... arrives"'],
    nv_wait: [['nv_every', 'procedures_defnoreturn', 'procedures_defreturn'],
      '"wait" only works inside "every ..." (it would stop the whole script elsewhere)'],
    nv_param_add: [['nv_init'], 'add controls inside "when the script starts"'],
    nv_ui_dial: [['nv_redraw', 'procedures_defnoreturn', 'procedures_defreturn'], 'widgets draw inside "draw the screen"'],
    nv_ui_slider: [['nv_redraw', 'procedures_defnoreturn', 'procedures_defreturn'], 'widgets draw inside "draw the screen"'],
    nv_ui_playback: [['nv_redraw', 'procedures_defnoreturn', 'procedures_defreturn'], 'widgets draw inside "draw the screen"'],
  };

  // a control name -> the parameter id ("Filter Cutoff" -> "filter_cutoff")
  function paramId(name) {
    const id = String(name).trim().toLowerCase().replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '');
    return id || 'control';
  }

  function luaString(s) {
    return '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"').replace(/\n/g, '\\n') + '"';
  }

  // kits: more blocks from their own files (blocks_sound.js, blocks_looper.js ...). a kit is
  // {name, defs, hats, context, generators(F, ctx), prologue(s), initFirst(s, I), initLast(s, I,
  // indent), functions(s, I, indent), toolbox: [{name, colour, contents}]}; register() takes them all.
  const KITS = [];
  function kit(k) { if (!KITS.some(x => x.name === k.name)) KITS.push(k); }

  function register(Blockly, lua, opts) {
    opts = opts || {};
    const g = lua.luaGenerator;
    const O = lua.Order;
    g.addReservedWords(RESERVED + ',' + KITS.map(k => k.reserved || '').filter(Boolean).join(','));
    const ALL = DEFS.concat(...KITS.map(k => k.defs || []));
    const ALL_HATS = HATS.concat(...KITS.map(k => k.hats || []));
    const ALL_CONTEXT = Object.assign({}, CONTEXT, ...KITS.map(k => k.context || {}));

    for (const def of ALL) {
      const ctx = ALL_CONTEXT[def.type];
      Blockly.Blocks[def.type] = {
        init: function () {
          this.jsonInit(def);
          if (ALL_HATS.includes(def.type)) this.hat = 'cap';
        },
        onchange: ctx ? function (e) {
          if (!this.workspace || this.workspace.isFlyout || this.isInFlyout) return;
          if (e && e.type === Blockly.Events.UI) return;
          let p = this.getSurroundParent();
          while (p && !ctx[0].includes(p.type)) p = p.getSurroundParent();
          if (this.setWarningText) this.setWarningText(p ? null : ctx[1]);
        } : undefined,
      };
    }

    // the sections of the script the event blocks fill while the workspace is generated
    let S = null;
    const use = what => { if (S) S.uses.add(what); };
    const v = (block, name, order) => g.valueToCode(block, name, order === undefined ? O.NONE : order) || '0';
    const body = block => g.statementToCode(block, 'DO');
    // whole numbers where norns wants them; a number typed into the block needs no rounding
    const int = code => /^-?\d+$/.test(code) ? code : 'math.floor(' + code + ')';
    // a text typed into the block is a string already
    const asText = code => /^'(?:[^'\\]|\\.)*'$|^"(?:[^"\\]|\\.)*"$/.test(code) ? code : 'tostring(' + code + ')';

    const F = g.forBlock;
    F.nv_init = b => { S.init.push(body(b)); return ''; };
    F.nv_every = b => {
      S.loops.push({div: b.getFieldValue('DIV'), body: body(b)});
      return '';
    };
    F.nv_key = b => {
      S.keys.push({n: b.getFieldValue('KEY'), z: b.getFieldValue('STATE'), body: body(b)});
      return '';
    };
    F.nv_enc = b => { S.encs.push({n: b.getFieldValue('ENC'), body: body(b)}); return ''; };
    F.nv_redraw = b => { S.redraw.push(body(b)); return ''; };
    F.nv_midi_note = b => {
      use('midi');
      S.midi.push({type: b.getFieldValue('TYPE'), body: body(b)});
      return '';
    };
    F.nv_enc_delta = () => ['d', O.ATOMIC];
    F.nv_midi_note_num = () => ['note', O.ATOMIC];
    F.nv_midi_velocity = () => ['vel', O.ATOMIC];

    F.nv_midi_send = b => {
      use('midi');
      const fn = g.provideFunction_('_blk_midi_note', [
        'function ' + g.FUNCTION_NAME_PLACEHOLDER_ + '(note, vel, beats, ch)',
        '  note, ch = math.floor(note), math.floor(ch)',
        '  _blk_midi:note_on(note, math.floor(vel), ch)',
        '  clock.run(function()',
        '    clock.sleep(clock.get_beat_sec() * beats)',
        '    _blk_midi:note_off(note, 0, ch)',
        '  end)',
        'end']);
      return fn + '(' + v(b, 'NOTE') + ', ' + v(b, 'VEL') + ', ' + v(b, 'LEN') + ', ' + v(b, 'CH') + ')\n';
    };

    F.nv_scale_note = b => {
      use('musicutil');
      const fn = g.provideFunction_('_blk_scale_note', [
        'function ' + g.FUNCTION_NAME_PLACEHOLDER_ + '(step, scale, root)',
        '  local notes = musicutil.generate_scale_of_length(math.floor(root), scale, 128)',
        '  step = math.max(1, math.min(#notes, math.floor(step)))',
        '  return notes[step]',
        'end']);
      return [fn + '(' + v(b, 'DEGREE') + ', ' + luaString(b.getFieldValue('SCALE')) + ', ' +
        v(b, 'ROOT') + ')', O.HIGH];
    };
    F.nv_note_name = b => {
      use('musicutil');
      return ['musicutil.note_num_to_name(math.floor(' + v(b, 'NOTE') + '), true)', O.HIGH];
    };
    F.nv_seq_make = b => {
      use('sequins');
      return ['sequins(' + (g.valueToCode(b, 'LIST', O.NONE) || '{}') + ')', O.HIGH];
    };
    F.nv_seq_next = b => [g.getVariableName(b.getFieldValue('VAR')) + '()', O.HIGH];

    F.nv_wait = b => 'clock.sleep(clock.get_beat_sec() * ' + v(b, 'BEATS', O.MULTIPLICATIVE) + ')\n';
    F.nv_tempo_set = b => 'params:set("clock_tempo", ' + v(b, 'BPM') + ')\n';
    F.nv_tempo_get = () => ['clock.get_tempo()', O.HIGH];

    const level = b => 'screen.level(' + int(v(b, 'LEVEL')) + ')\n';
    F.nv_screen_text = b => level(b) + 'screen.move(' + v(b, 'X') + ', ' + v(b, 'Y') + ')\n' +
      'screen.' + (b.getFieldValue('ALIGN') || 'text') + '(' + asText(g.valueToCode(b, 'TEXT', O.NONE) || '""') + ')\n';
    F.nv_screen_rect = b => level(b) + 'screen.rect(' + v(b, 'X') + ', ' + v(b, 'Y') + ', ' +
      v(b, 'W') + ', ' + v(b, 'H') + ')\nscreen.' + b.getFieldValue('MODE') + '()\n';
    F.nv_screen_circle = b => level(b) + 'screen.circle(' + v(b, 'X') + ', ' + v(b, 'Y') + ', ' +
      v(b, 'R') + ')\nscreen.' + b.getFieldValue('MODE') + '()\n';
    F.nv_screen_line = b => level(b) + 'screen.move(' + v(b, 'X1') + ', ' + v(b, 'Y1') + ')\n' +
      'screen.line(' + v(b, 'X2') + ', ' + v(b, 'Y2') + ')\nscreen.stroke()\n';

    F.nv_screen_pixel = b => level(b) + 'screen.pixel(' + v(b, 'X') + ', ' + v(b, 'Y') + ')\nscreen.fill()\n';
    F.nv_screen_font_size = b => 'screen.font_size(' + v(b, 'SIZE') + ')\n';
    F.nv_frame = () => { use('frame'); return ['_blk_frame', O.ATOMIC]; };
    F.nv_beats = () => ['clock.get_beats()', O.HIGH];

    // one widget object per block and control, made on first drawing and kept after that
    const widget = (b, kind, make) => {
      use('ui');
      const fn = g.provideFunction_('_blk_widget', [
        'function ' + g.FUNCTION_NAME_PLACEHOLDER_ + '(key, id, make)',
        '  _blk_widgets = _blk_widgets or {}',
        '  local w = _blk_widgets[key]',
        '  if not w then',
        '    local p = params:lookup_param(id)',
        '    w = make(p.controlspec.minval, p.controlspec.maxval, p.name)',
        '    _blk_widgets[key] = w',
        '  end',
        '  w:set_value(params:get(id))',
        '  w:redraw()',
        'end']);
      return fn + '(' + luaString(kind + ' ' + b.id) + ', ' + luaString(paramId(b.getFieldValue('NAME'))) +
        ', function(lo, hi, name) return ' + make + ' end)\n';
    };
    F.nv_ui_dial = b => widget(b, 'dial', 'UI.Dial.new(' + v(b, 'X') + ', ' + v(b, 'Y') + ', ' + v(b, 'SIZE') +
      ', lo, lo, hi, (hi - lo) > 10 and 1 or 0.01, lo, {}, "", name)');
    F.nv_ui_slider = b => widget(b, 'slider', 'UI.Slider.new(' + v(b, 'X') + ', ' + v(b, 'Y') + ', ' + v(b, 'W') +
      ', ' + v(b, 'H') + ', lo, lo, hi, {}, ' + luaString(b.getFieldValue('DIR')) + ')');
    F.nv_ui_playback = b => {
      use('ui');
      const fn = g.provideFunction_('_blk_playback', [
        'function ' + g.FUNCTION_NAME_PLACEHOLDER_ + '(key, x, y, size, playing)',
        '  _blk_widgets = _blk_widgets or {}',
        '  local w = _blk_widgets[key] or UI.PlaybackIcon.new(x, y, size, 1)',
        '  _blk_widgets[key] = w',
        '  w.x, w.y, w.size = x, y, size',
        '  w:set_status(playing and 1 or 4)',
        '  w:redraw()',
        'end']);
      return fn + '(' + luaString('play ' + b.id) + ', ' + v(b, 'X') + ', ' + v(b, 'Y') + ', ' + v(b, 'SIZE') +
        ', ' + (g.valueToCode(b, 'PLAYING', O.NONE) || 'false') + ')\n';
    };

    F.nv_param_add = b => {
      const id = paramId(b.getFieldValue('NAME'));
      let lo = Number(b.getFieldValue('MIN')), hi = Number(b.getFieldValue('MAX'));
      if (hi < lo) [lo, hi] = [hi, lo];
      const def = Math.max(lo, Math.min(hi, Number(b.getFieldValue('DEFAULT'))));
      return 'params:add_control(' + luaString(id) + ', ' + luaString(b.getFieldValue('NAME')) +
        ', controlspec.new(' + lo + ', ' + hi + ', "lin", 0, ' + def + '))\n';
    };
    F.nv_param_get = b => ['params:get(' + luaString(paramId(b.getFieldValue('NAME'))) + ')', O.HIGH];
    F.nv_param_set = b => 'params:set(' + luaString(paramId(b.getFieldValue('NAME'))) + ', ' + v(b, 'VALUE') + ')\n';
    F.nv_param_change = b => {
      const id = luaString(paramId(b.getFieldValue('NAME')));
      // params:delta works in the control's 0..1 range; set the value itself, clamped by params
      return 'params:set(' + id + ', params:get(' + id + ') + ' + v(b, 'DELTA', O.ADDITIVE) + ')\n';
    };

    // what the kits' generators get to work with
    const kitCtx = {Blockly, g, O, v, body, use, luaString, paramId, int, asText, opts,
      S: () => S,
      // a piece of Lua the script needs once (a helper library), above init()
      define: (key, code) => { g.definitions_['_blk_' + key] = code; }};
    for (const k of KITS) if (k.generators) k.generators(F, kitCtx);

    // the whole workspace -> the text of a norns script
    function generate(workspace, name) {
      S = {init: [], loops: [], keys: [], encs: [], redraw: [], midi: [], uses: new Set(), kits: {}, name: name || 'untitled', opts};
      for (const k of KITS) S.kits[k.name] = {};
      g.init(workspace);
      for (const block of workspace.getTopBlocks(true)) {
        if (!block.isEnabled()) continue;
        // a loose block outside every event block never runs: leave it out
        if (!ALL_HATS.includes(block.type) && !block.type.startsWith('procedures_def')) continue;
        g.blockToCode(block);
      }
      const definitions = g.finish('').trim();
      const s = S;
      S = null;

      const I = g.INDENT;
      const indent = (code, n) => code.replace(/^(?=.)/gm, I.repeat(n));
      const out = [];
      const title = name || 'untitled';
      out.push('-- ' + title,
        '-- made with the Verdandi script builder (' + title + '.blocks.json holds the blocks)',
        '-- the builder writes this file again on every save: edit the blocks, not the text', '');
      for (const k of KITS) if (k.prologue) out.push(...k.prologue(s));
      if (s.uses.has('musicutil')) out.push("musicutil = require 'musicutil'");
      if (s.uses.has('sequins')) out.push("sequins = require 'sequins'");
      if (s.uses.has('ui')) out.push("UI = require 'ui'");
      if (out[out.length - 1] !== '') out.push('');
      if (definitions) out.push(definitions, '');

      out.push('function init()');
      for (const k of KITS) if (k.initFirst) out.push(...k.initFirst(s, I, indent));
      if (s.uses.has('frame')) out.push(I + '_blk_frame = 0');
      if (s.uses.has('midi')) out.push(I + '_blk_midi = midi.connect(1)');
      const lines = code => code.replace(/\n$/, '');
      for (const b of s.init) if (b) out.push(lines(b));
      if (s.midi.length) {
        out.push(I + '_blk_midi.event = function(data)', I + I + 'local msg = midi.to_msg(data)',
          I + I + 'local note, vel = msg.note, msg.vel');
        for (const m of s.midi) {
          const cond = m.type === 'on' ? 'msg.type == "note_on" and vel > 0'
            : '(msg.type == "note_off" or (msg.type == "note_on" and vel == 0))';
          out.push(I + I + 'if ' + cond + ' then', indent(m.body, 2).replace(/\n$/, ''), I + I + 'end');
        }
        out.push(I + 'end');
      }
      for (const l of s.loops) {
        out.push(I + 'clock.run(function()', I + I + 'while true do', I + I + I + 'clock.sync(' + l.div + ')',
          indent(l.body, 2).replace(/\n$/, ''), I + I + 'end', I + 'end)');
      }
      // what kits draw before the blocks do (the screen design): a redraw even without "draw the screen"
      const redrawFirst = [].concat(...KITS.map(k => k.redrawFirst ? k.redrawFirst(s, I) : []));
      const drawing = s.redraw.length > 0 || redrawFirst.length > 0;
      if (drawing) {
        out.push(I + 'clock.run(function()', I + I + 'while true do', I + I + I + 'clock.sleep(1 / 15)');
        if (s.uses.has('frame')) out.push(I + I + I + '_blk_frame = _blk_frame + 1');
        out.push(I + I + I + 'redraw()', I + I + 'end', I + 'end)');
      }
      for (const k of KITS) if (k.initLast) out.push(...k.initLast(s, I, indent));
      out.push('end', '');
      for (const k of KITS) if (k.functions) out.push(...k.functions(s, I, indent));

      if (s.keys.length) {
        out.push('function key(n, z)');
        for (const k of s.keys) {
          out.push(I + 'if n == ' + k.n + ' and z == ' + k.z + ' then', indent(k.body, 1).replace(/\n$/, ''), I + 'end');
        }
        out.push('end', '');
      }
      if (s.encs.length) {
        out.push('function enc(n, d)');
        for (const e of s.encs) {
          out.push(I + 'if n == ' + e.n + ' then', indent(e.body, 1).replace(/\n$/, ''), I + 'end');
        }
        out.push('end', '');
      }
      if (drawing) {
        out.push('function redraw()', I + 'screen.clear()', ...redrawFirst);
        for (const r of s.redraw) if (r) out.push(lines(r));
        out.push(I + 'screen.update()', 'end', '');
      }
      // blank lines left by empty event blocks
      return out.filter((l, i, a) => !(l === '' && a[i - 1] === '')).join('\n').replace(/\n+$/, '\n');
    }

    return {generate, HATS: ALL_HATS, TYPES: ALL.map(d => d.type)};
  }

  // the toolbox: built-in Blockly categories plus the norns ones, with ready-made inputs
  const ORDER = ['Events', 'Sound', 'Music', 'Sequencing', 'Time', 'Looper', 'MIDI', 'Grid', 'Screen',
    'Widgets', 'Controls', 'Data'];
  function toolbox() {
    const tb = coreToolbox();
    const ours = tb.contents.slice(0, tb.contents.findIndex(c => c.kind === 'sep'));
    const rest = tb.contents.slice(ours.length);
    for (const k of KITS) {
      for (const cat of k.toolbox || []) {
        const have = ours.find(c => c.name === cat.name);
        if (have) have.contents = have.contents.concat(cat.contents);
        else ours.push({kind: 'category', name: cat.name, colour: cat.colour, contents: cat.contents});
      }
    }
    const rank = c => { const i = ORDER.indexOf(c.name); return i < 0 ? ORDER.length : i; };
    ours.sort((a, c) => rank(a) - rank(c));
    return {kind: 'categoryToolbox', contents: ours.concat(rest)};
  }
  // a toolbox entry, for kits too
  function entry(type, inputs, fields) { return {kind: 'block', type, inputs, fields}; }

  function coreToolbox() {
    const b = entry;
    return {kind: 'categoryToolbox', contents: [
      {kind: 'category', name: 'Events', colour: C.event, contents: [
        b('nv_init'), b('nv_every'), b('nv_key'), b('nv_enc'), b('nv_enc_delta'), b('nv_redraw')]},
      {kind: 'category', name: 'MIDI', colour: C.midi, contents: [
        b('nv_midi_note'), b('nv_midi_note_num'), b('nv_midi_velocity'),
        b('nv_midi_send', {NOTE: num(60), VEL: num(100), LEN: num(0.5), CH: num(1)})]},
      {kind: 'category', name: 'Music', colour: C.music, contents: [
        b('nv_scale_note', {DEGREE: num(1), ROOT: num(60)}, {SCALE: 'Major'}),
        b('nv_note_name', {NOTE: num(60)}),
        b('nv_seq_make', {LIST: {block: {type: 'lists_create_with', extraState: {itemCount: 4},
          inputs: {ADD0: num(1), ADD1: num(3), ADD2: num(5), ADD3: num(8)}}}}),
        b('nv_seq_next')]},
      {kind: 'category', name: 'Time', colour: C.time, contents: [
        b('nv_wait', {BEATS: num(0.5)}), b('nv_tempo_set', {BPM: num(120)}), b('nv_tempo_get')]},
      {kind: 'category', name: 'Screen', colour: C.screen, contents: [
        b('nv_screen_text', {TEXT: txt('hello'), X: num(10), Y: num(30), LEVEL: num(15)}),
        b('nv_screen_rect', {X: num(10), Y: num(10), W: num(20), H: num(10), LEVEL: num(8)}),
        b('nv_screen_circle', {X: num(64), Y: num(32), R: num(10), LEVEL: num(8)}),
        b('nv_screen_line', {X1: num(0), Y1: num(63), X2: num(127), Y2: num(0), LEVEL: num(4)}),
        b('nv_screen_pixel', {X: num(64), Y: num(32), LEVEL: num(15)}),
        b('nv_screen_font_size', {SIZE: num(16)}),
        b('nv_frame'), b('nv_beats')]},
      {kind: 'category', name: 'Widgets', colour: C.widget, contents: [
        b('nv_ui_dial', {X: num(4), Y: num(16), SIZE: num(22)}),
        b('nv_ui_slider', {X: num(100), Y: num(10), W: num(3), H: num(40)}),
        b('nv_ui_playback', {X: num(116), Y: num(4), SIZE: num(8), PLAYING: {shadow: {type: 'logic_boolean', fields: {BOOL: 'TRUE'}}}})]},
      {kind: 'category', name: 'Controls', colour: C.control, contents: [
        b('nv_param_add'), b('nv_param_get'), b('nv_param_set', {VALUE: num(50)}),
        b('nv_param_change', {DELTA: {block: {type: 'nv_enc_delta'}}})]},
      {kind: 'sep'},
      {kind: 'category', name: 'Logic', categorystyle: 'logic_category', contents: [
        b('controls_if'), b('logic_compare'), b('logic_operation'), b('logic_negate'),
        b('logic_boolean'), b('logic_ternary')]},
      {kind: 'category', name: 'Loops', categorystyle: 'loop_category', contents: [
        b('controls_repeat_ext', {TIMES: num(4)}), b('controls_whileUntil'),
        b('controls_for', {FROM: num(1), TO: num(8), BY: num(1)}), b('controls_forEach'),
        b('controls_flow_statements')]},
      {kind: 'category', name: 'Math', categorystyle: 'math_category', contents: [
        b('math_number'), b('math_arithmetic', {A: num(1), B: num(1)}), b('math_single'),
        b('math_random_int', {FROM: num(1), TO: num(8)}), b('math_random_float'),
        b('math_modulo', {DIVIDEND: num(9), DIVISOR: num(8)}),
        b('math_constrain', {VALUE: num(50), LOW: num(0), HIGH: num(100)}), b('math_round'),
        b('math_number_property'), b('math_trig', {NUM: num(45)})]},
      {kind: 'category', name: 'Text', categorystyle: 'text_category', contents: [
        b('text'), b('text_join'), b('text_length'), b('text_print', {TEXT: txt('hello')})]},
      {kind: 'category', name: 'Lists', categorystyle: 'list_category', contents: [
        b('lists_create_with'), b('lists_length'), b('lists_getIndex'), b('lists_setIndex')]},
      {kind: 'category', name: 'Variables', categorystyle: 'variable_category', custom: 'VARIABLE'},
      {kind: 'category', name: 'Functions', categorystyle: 'procedure_category', custom: 'PROCEDURE'},
    ]};
  }

  // every block definition, the kits' too (for the manual's reference)
  function definitions() { return DEFS.concat(...KITS.map(k => k.defs || [])); }

  return {register, toolbox, kit, entry, num, txt, paramId, luaString, definitions, HATS, RESERVED, C};
}));
