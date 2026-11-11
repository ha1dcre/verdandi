// blocks_ui.js: more Widgets (page dots, tabs, a list), more Controls (choices, switches, an
// LFO that moves any control, "when a control changes"), MIDI CC in and out, and Data (values
// remembered between sessions). a kit of blocks.js: browser after blocks.js; node:
// require(...)(VerdandiBlocks).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory;
  else factory(root.VerdandiBlocks);
}(this, function (VB) {
  'use strict';
  const W = 130, P = 60, M = 355, D = 190;
  const n = VB.num, t = VB.txt, b = VB.entry;
  const num = name => ({type: 'input_value', name, check: 'Number'});
  const name = text => ({type: 'field_input', name: 'NAME', text});
  const inRedraw = [['nv_redraw', 'procedures_defnoreturn', 'procedures_defreturn'], 'draws inside "draw the screen"'];
  const inInit = what => [['nv_init'], what + ' in "when the script starts"'];

  const LIB = {
    lfo: `-- an LFO: moves a control between lo and hi, once every so many beats
function _blk_lfo(id, lo, hi, beats, shape)
  local seed = math.random()
  local held, last = math.random(), -1
  clock.run(function()
    while true do
      clock.sleep(1 / 30)
      local phase = (clock.get_beats() / math.max(beats, 0.01)) % 1
      local x
      if shape == "sine" then x = 0.5 - 0.5 * math.cos(phase * 2 * math.pi)
      elseif shape == "triangle" then x = phase < 0.5 and phase * 2 or 2 - phase * 2
      elseif shape == "saw" then x = phase
      elseif shape == "square" then x = phase < 0.5 and 1 or 0
      else
        local cycle = math.floor(clock.get_beats() / math.max(beats, 0.01))
        if cycle ~= last then last, held = cycle, math.random() end
        x = held
      end
      params:set(id, lo + (hi - lo) * x)
    end
  end)
end`,
    data: `-- values remembered between sessions, in the script's data folder
function _blk_data_file()
  return (norns.state.data or _path.data) .. "blocks.data"
end
function _blk_data_get(key, default)
  if _blk_data == nil then
    _blk_data = util.file_exists(_blk_data_file()) and tab.load(_blk_data_file()) or {}
  end
  local v = _blk_data[key]
  if v == nil then return default end
  return v
end
function _blk_data_set(key, value)
  _blk_data_get(key)
  _blk_data[key] = value
  util.make_dir(norns.state.data or _path.data)
  tab.save(_blk_data, _blk_data_file())
end`,
    words: `function _blk_words(text)
  local out = {}
  for w in string.gmatch(text, "[^,]+") do out[#out + 1] = (w:gsub("^%s+", ""):gsub("%s+$", "")) end
  return out
end`,
  };

  VB.kit({
    name: 'ui',
    hats: ['nv_param_changed', 'nv_midi_cc'],
    defs: [
      // widgets
      {type: 'nv_ui_pages', message0: 'page dots: page %1 of %2', args0: [num('CUR'), num('N')], inputsInline: true,
       previousStatement: null, nextStatement: null, colour: W,
       tooltip: 'the dots on the right that show which page is on (keep the page in a variable, change it with E1)'},
      {type: 'nv_ui_tabs', message0: 'tabs %1 showing tab %2', args0: [{type: 'field_input', name: 'TEXT', text: 'play, sound, loop'},
        num('CUR')], inputsInline: true, previousStatement: null, nextStatement: null, colour: W,
       tooltip: 'tab names with commas between them, across the top'},
      {type: 'nv_ui_list', message0: 'list %1 at x %2 y %3 with item %4 picked', args0: [
        {type: 'field_input', name: 'TEXT', text: 'one, two, three, four'}, num('X'), num('Y'), num('CUR')],
       inputsInline: true, previousStatement: null, nextStatement: null, colour: W,
       tooltip: 'a scrolling list, the picked item bright'},
      // controls
      {type: 'nv_param_option', message0: 'add choice %1 with %2 starting at %3', args0: [name('mode'),
        {type: 'field_input', name: 'TEXT', text: 'up, down, random'}, {type: 'field_number', name: 'DEFAULT', value: 1, min: 1}],
       previousStatement: null, nextStatement: null, colour: P,
       tooltip: 'a control that picks one of these (commas between them); "control mode" is its number, 1 for the first'},
      {type: 'nv_param_switch', message0: 'add switch %1 starting %2', args0: [name('hold'),
        {type: 'field_dropdown', name: 'ON', options: [['off', '0'], ['on', '1']]}],
       previousStatement: null, nextStatement: null, colour: P, tooltip: 'on (1) or off (0); MIDI mappable'},
      {type: 'nv_param_text', message0: 'shown value of control %1', args0: [name('mode')], output: 'String', colour: P,
       tooltip: 'what PARAMETERS shows: the name of a choice, a value with its unit'},
      {type: 'nv_param_changed', message0: 'when control %1 changes %2 %3', args0: [name('cutoff'),
        {type: 'input_dummy'}, {type: 'input_statement', name: 'DO'}], colour: P,
       tooltip: 'from an encoder, MIDI, an LFO or a block: "new value" is what it is now'},
      {type: 'nv_param_value', message0: 'new value', output: null, colour: P},
      {type: 'nv_lfo', message0: 'LFO moves control %1 from %2 to %3 every %4 beats, %5', args0: [name('cutoff'),
        num('LO'), num('HI'), num('BEATS'),
        {type: 'field_dropdown', name: 'SHAPE', options: [['smoothly', 'sine'], ['as a triangle', 'triangle'],
          ['as a saw', 'saw'], ['as a square', 'square'], ['in random steps', 'random']]}],
       inputsInline: true, previousStatement: null, nextStatement: null, colour: P,
       tooltip: 'any control, sound knobs too (control "filter1 cutoff"); in time with the clock'},
      // MIDI CC
      {type: 'nv_midi_cc', message0: 'when MIDI CC %1 changes %2 %3', args0: [{type: 'field_number', name: 'CC', value: 1, min: 0, max: 127},
        {type: 'input_dummy'}, {type: 'input_statement', name: 'DO'}], colour: M,
       tooltip: 'a knob of a MIDI controller (device slot 1); "CC value" is 0 to 127'},
      {type: 'nv_midi_cc_value', message0: 'CC value', output: 'Number', colour: M},
      {type: 'nv_midi_cc_send', message0: 'send MIDI CC %1 value %2 on channel %3', args0: [num('CC'), num('VALUE'), num('CH')],
       inputsInline: true, previousStatement: null, nextStatement: null, colour: M},
      // data
      {type: 'nv_data_set', message0: 'remember %1 as %2', args0: [{type: 'input_value', name: 'VALUE'},
        {type: 'field_input', name: 'KEY', text: 'pattern'}], inputsInline: true,
       previousStatement: null, nextStatement: null, colour: D,
       tooltip: 'kept in the script\'s data folder: still there the next time the script runs'},
      {type: 'nv_data_get', message0: 'remembered %1, else %2', args0: [{type: 'field_input', name: 'KEY', text: 'pattern'},
        {type: 'input_value', name: 'DEFAULT'}], inputsInline: true, output: null, colour: D},
    ],
    context: {
      nv_ui_pages: inRedraw, nv_ui_tabs: inRedraw, nv_ui_list: inRedraw,
      nv_param_option: inInit('add controls'), nv_param_switch: inInit('add controls'), nv_lfo: inInit('start LFOs'),
      nv_param_value: [['nv_param_changed'], 'only works inside "when control ... changes"'],
      nv_midi_cc_value: [['nv_midi_cc'], 'only works inside "when MIDI CC ... changes"'],
    },
    generators(F, ctx) {
      const {O, v, body, luaString, paramId} = ctx;
      const k = () => ctx.S().kits.ui;
      const need = key => ctx.define('ui_' + key, LIB[key]);
      const words = s => '{' + String(s).split(',').map(w => w.trim()).filter(Boolean).map(luaString).join(', ') + '}';
      F.nv_ui_pages = bl => { ctx.use('ui'); return 'UI.Pages.new(math.floor(' + v(bl, 'CUR') + '), math.floor(' + v(bl, 'N') + ')):redraw()\n'; };
      F.nv_ui_tabs = bl => { ctx.use('ui'); return 'UI.Tabs.new(math.floor(' + v(bl, 'CUR') + '), ' + words(bl.getFieldValue('TEXT')) + '):redraw()\n'; };
      F.nv_ui_list = bl => {
        ctx.use('ui');
        return 'UI.ScrollingList.new(' + v(bl, 'X') + ', ' + v(bl, 'Y') + ', math.floor(' + v(bl, 'CUR') + '), ' + words(bl.getFieldValue('TEXT')) + '):redraw()\n';
      };
      F.nv_param_option = bl => {
        const opts = String(bl.getFieldValue('TEXT')).split(',').map(w => w.trim()).filter(Boolean);
        const def = Math.max(1, Math.min(opts.length || 1, Math.round(Number(bl.getFieldValue('DEFAULT')))));
        return 'params:add_option(' + luaString(paramId(bl.getFieldValue('NAME'))) + ', ' + luaString(bl.getFieldValue('NAME')) +
          ', ' + words(bl.getFieldValue('TEXT')) + ', ' + def + ')\n';
      };
      F.nv_param_switch = bl => 'params:add_binary(' + luaString(paramId(bl.getFieldValue('NAME'))) + ', ' +
        luaString(bl.getFieldValue('NAME')) + ', "toggle", ' + bl.getFieldValue('ON') + ')\n';
      F.nv_param_text = bl => ['params:string(' + luaString(paramId(bl.getFieldValue('NAME'))) + ')', O.HIGH];
      F.nv_param_changed = bl => { (k().changed = k().changed || []).push({id: paramId(bl.getFieldValue('NAME')), body: body(bl)}); return ''; };
      F.nv_param_value = () => ['_blk_value', O.ATOMIC];
      F.nv_lfo = bl => {
        need('lfo');
        return '_blk_lfo(' + luaString(paramId(bl.getFieldValue('NAME'))) + ', ' + v(bl, 'LO') + ', ' + v(bl, 'HI') + ', ' +
          v(bl, 'BEATS') + ', ' + luaString(bl.getFieldValue('SHAPE')) + ')\n';
      };
      F.nv_midi_cc = bl => { ctx.use('midi'); (k().cc = k().cc || []).push({cc: Math.round(Number(bl.getFieldValue('CC'))), body: body(bl)}); return ''; };
      F.nv_midi_cc_value = () => ['_blk_cc', O.ATOMIC];
      F.nv_midi_cc_send = bl => { ctx.use('midi'); return '_blk_midi:cc(math.floor(' + v(bl, 'CC') + '), math.floor(' + v(bl, 'VALUE') + '), math.floor(' + v(bl, 'CH') + '))\n'; };
      F.nv_data_set = bl => { need('data'); return '_blk_data_set(' + luaString(bl.getFieldValue('KEY')) + ', ' + (ctx.g.valueToCode(bl, 'VALUE', O.NONE) || 'nil') + ')\n'; };
      F.nv_data_get = bl => { need('data'); return ['_blk_data_get(' + luaString(bl.getFieldValue('KEY')) + ', ' + (ctx.g.valueToCode(bl, 'DEFAULT', O.NONE) || 'nil') + ')', O.HIGH]; };
    },
    initLast(s, I, indent) {
      const k = s.kits.ui, out = [];
      // after every control exists: keep the action a control had (a sound knob tells the engine)
      for (const c of k.changed || []) {
        out.push(I + 'do', I + I + 'local before = params:lookup_param(' + VB.luaString(c.id) + ').action',
          I + I + 'params:set_action(' + VB.luaString(c.id) + ', function(_blk_value)',
          I + I + I + 'if before then before(_blk_value) end',
          indent(c.body, 2).replace(/\n$/, ''), I + I + 'end)', I + 'end');
      }
      if (k.cc) {
        // the note handler (if any) is set in init already: CC comes on top of it
        out.push(I + 'do', I + I + 'local notes = _blk_midi.event', I + I + '_blk_midi.event = function(data)',
          I + I + I + 'if notes then notes(data) end', I + I + I + 'local msg = midi.to_msg(data)',
          I + I + I + 'if msg.type ~= "cc" then return end', I + I + I + 'local _blk_cc = msg.val');
        for (const c of k.cc) out.push(I + I + I + 'if msg.cc == ' + c.cc + ' then', indent(c.body, 3).replace(/\n$/, ''), I + I + I + 'end');
        out.push(I + I + 'end', I + 'end');
      }
      return out;
    },
    reserved: '_blk_value,_blk_cc',
    toolbox: [
      {name: 'Widgets', colour: W, contents: [
        b('nv_ui_pages', {CUR: n(1), N: n(3)}), b('nv_ui_tabs', {CUR: n(1)}), b('nv_ui_list', {X: n(4), Y: n(20), CUR: n(1)})]},
      {name: 'Controls', colour: P, contents: [
        b('nv_param_option'), b('nv_param_switch'), b('nv_param_text'), b('nv_param_changed'), b('nv_param_value'),
        b('nv_lfo', {LO: n(200), HI: n(3000), BEATS: n(8)})]},
      {name: 'MIDI', colour: M, contents: [
        b('nv_midi_cc'), b('nv_midi_cc_value'), b('nv_midi_cc_send', {CC: n(1), VALUE: n(64), CH: n(1)})]},
      {name: 'Data', colour: D, contents: [
        b('nv_data_set', {VALUE: t('x..x')}), b('nv_data_get', {DEFAULT: t('x..x')})]},
    ],
  });
}));
