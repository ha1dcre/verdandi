// blocks_seq.js: the Sequencing blocks: patterns written as text ("x..x..x." for hits, "60 . 63 67"
// for notes, a dot is a rest), euclidean rhythms, counters, chance, chords, arpeggios and notes
// snapped to a scale. a kit of blocks.js: browser after blocks.js; node: require(...)(VerdandiBlocks).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory;
  else factory(root.VerdandiBlocks);
}(this, function (VB) {
  'use strict';
  const C = 260;
  const n = VB.num, b = VB.entry;
  const num = name => ({type: 'input_value', name, check: 'Number'});
  const SCALES = ['Major', 'Natural Minor', 'Harmonic Minor', 'Melodic Minor', 'Dorian', 'Phrygian', 'Lydian',
    'Mixolydian', 'Locrian', 'Major Pentatonic', 'Minor Pentatonic', 'Blues Scale', 'Whole Tone', 'Chromatic'];
  const CHORDS = ['Major', 'Minor', 'Major 7', 'Minor 7', 'Dominant 7', 'Sus4', 'Diminished', 'Augmented',
    'Major 6', 'Minor 6', 'Major 9', 'Minor 9', 'Half Diminished 7', 'Diminished 7'];

  const LIB = {
    pattern: `-- a pattern written as text, step 1 first, round and round: "x..x" (x is a hit, . a rest)
-- or "60 . 63 67" (numbers, . is a rest)
_blk_patterns = {}
function _blk_pattern(text, step)
  local p = _blk_patterns[text]
  if not p then
    p = {}
    if string.find(text, "%s") then
      for word in string.gmatch(text, "%S+") do p[#p + 1] = tonumber(word) or false end
    else
      for c in string.gmatch(text, ".") do p[#p + 1] = c end
    end
    _blk_patterns[text] = p
  end
  if #p == 0 then return nil end
  return p[(math.floor(step) - 1) % #p + 1]
end`,
    hit: `function _blk_pattern_hit(text, step)
  local s = _blk_pattern(text, step)
  return s ~= nil and s ~= false and s ~= "." and s ~= "-" and s ~= "_" and s ~= " "
end`,
    value: `function _blk_pattern_value(text, step)
  local s = _blk_pattern(text, step)
  if type(s) == "number" then return s end
  if type(s) == "string" then return tonumber(s) end
  return nil
end`,
    euclid: `-- euclidean rhythms: k hits spread as evenly as can be over n steps
_blk_euclids = {}
function _blk_euclid(k, n, shift, step)
  k, n, shift = math.floor(k), math.max(1, math.floor(n)), math.floor(shift)
  local key = k .. " " .. n .. " " .. shift
  if not _blk_euclids[key] then _blk_euclids[key] = er.gen(util.clamp(k, 0, n), n, shift) end
  return _blk_euclids[key][(math.floor(step) - 1) % n + 1] == true
end`,
    nth: `_blk_counts = {}
function _blk_nth(id, n)
  _blk_counts[id] = (_blk_counts[id] or 0) + 1
  return (_blk_counts[id] - 1) % math.max(1, math.floor(n)) == 0
end`,
    arp: `-- the notes of a list in turn: up, down, up and down (no repeats at the ends) or at random
function _blk_arp(list, step, dir)
  local n = #list
  if n == 0 then return nil end
  step = math.floor(step) - 1
  if dir == "down" then return list[n - step % n] end
  if dir == "random" then return list[math.random(n)] end
  if dir == "updown" and n > 1 then
    local i = step % (2 * n - 2)
    return list[i < n and i + 1 or 2 * n - 1 - i]
  end
  return list[step % n + 1]
end`,
    snap: `function _blk_snap(note, scale, root)
  local notes = musicutil.generate_scale_of_length(math.floor(root) % 12, scale, 128)
  return musicutil.snap_note_to_array(note, notes)
end`,
  };

  VB.kit({
    name: 'seq',
    defs: [
      {type: 'nv_pattern_hit', message0: 'step %1 of rhythm %2 is a hit', args0: [num('STEP'),
        {type: 'field_input', name: 'TEXT', text: 'x..x..x.x...x...'}], inputsInline: true, output: 'Boolean', colour: C,
       tooltip: 'x (or any letter) is a hit, a dot a rest; after the last step it starts over'},
      {type: 'nv_pattern_value', message0: 'step %1 of %2', args0: [num('STEP'),
        {type: 'field_input', name: 'TEXT', text: '60 . 63 67 . 70 67 63'}], inputsInline: true, output: 'Number', colour: C,
       tooltip: 'numbers with spaces between them; a dot is a rest: "play note" plays nothing then'},
      {type: 'nv_euclid', message0: 'step %1 of %2 hits in %3 steps, shifted by %4, is a hit', args0: [
        num('STEP'), num('K'), num('N'), num('SHIFT')], inputsInline: true, output: 'Boolean', colour: C,
       tooltip: 'a euclidean rhythm: the hits as evenly spread as they can be (3 in 8 is the tresillo)'},
      {type: 'nv_counter', message0: 'count %1 from 1 to %2, then again', args0: [
        {type: 'field_variable', name: 'VAR', variable: 'step'}, num('N')], inputsInline: true,
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'adds 1 each time, back to 1 after the last: the step of a sequence'},
      {type: 'nv_chance', message0: 'chance of %1 percent', args0: [num('P')], output: 'Boolean', colour: C,
       tooltip: 'true that often, at random: 50 is a coin toss'},
      {type: 'nv_nth', message0: 'every %1. time this runs', args0: [num('N')], output: 'Boolean', colour: C,
       tooltip: 'true the first time, then every n-th time'},
      {type: 'nv_chord', message0: 'notes of the %1 chord on %2', args0: [
        {type: 'field_dropdown', name: 'TYPE', options: CHORDS.map(c => [c, c])}, num('ROOT')], inputsInline: true,
       output: 'Array', colour: C, tooltip: 'a list of note numbers, the root first'},
      {type: 'nv_arp', message0: 'note %1 of %2 going %3', args0: [num('STEP'),
        {type: 'input_value', name: 'LIST', check: 'Array'},
        {type: 'field_dropdown', name: 'DIR', options: [['up', 'up'], ['down', 'down'], ['up and down', 'updown'], ['at random', 'random']]}],
       inputsInline: true, output: 'Number', colour: C, tooltip: 'an arpeggio: step 1, 2, 3 ... walks through the notes'},
      {type: 'nv_snap', message0: '%1 snapped to %2 on %3', args0: [num('NOTE'),
        {type: 'field_dropdown', name: 'SCALE', options: SCALES.map(s => [s, s])}, num('ROOT')], inputsInline: true,
       output: 'Number', colour: C, tooltip: 'the nearest note of the scale: any number becomes a note that fits'},
    ],
    generators(F, ctx) {
      const {g, O, v, luaString} = ctx;
      const need = key => ctx.define('seq_' + key, LIB[key]);
      F.nv_pattern_hit = bl => { need('pattern'); need('hit'); return ['_blk_pattern_hit(' + luaString(bl.getFieldValue('TEXT')) + ', ' + v(bl, 'STEP') + ')', O.HIGH]; };
      F.nv_pattern_value = bl => { need('pattern'); need('value'); return ['_blk_pattern_value(' + luaString(bl.getFieldValue('TEXT')) + ', ' + v(bl, 'STEP') + ')', O.HIGH]; };
      F.nv_euclid = bl => {
        ctx.S().kits.seq.er = true;
        need('euclid');
        return ['_blk_euclid(' + v(bl, 'K') + ', ' + v(bl, 'N') + ', ' + v(bl, 'SHIFT') + ', ' + v(bl, 'STEP') + ')', O.HIGH];
      };
      F.nv_counter = bl => {
        const name = g.getVariableName(bl.getFieldValue('VAR'));
        return name + ' = ((' + name + ' or 0) % math.max(1, math.floor(' + v(bl, 'N') + '))) + 1\n';
      };
      F.nv_chance = bl => ['(math.random() * 100 < ' + v(bl, 'P') + ')', O.ATOMIC];
      F.nv_nth = bl => { need('nth'); return ['_blk_nth(' + luaString(bl.id) + ', ' + v(bl, 'N') + ')', O.HIGH]; };
      F.nv_chord = bl => {
        ctx.use('musicutil');
        return ['musicutil.generate_chord(math.floor(' + v(bl, 'ROOT') + '), ' + luaString(bl.getFieldValue('TYPE')) + ')', O.HIGH];
      };
      F.nv_arp = bl => {
        need('arp');
        return ['_blk_arp(' + (g.valueToCode(bl, 'LIST', O.NONE) || '{}') + ', ' + v(bl, 'STEP') + ', ' + luaString(bl.getFieldValue('DIR')) + ')', O.HIGH];
      };
      F.nv_snap = bl => {
        ctx.use('musicutil');
        need('snap');
        return ['_blk_snap(' + v(bl, 'NOTE') + ', ' + luaString(bl.getFieldValue('SCALE')) + ', ' + v(bl, 'ROOT') + ')', O.HIGH];
      };
    },
    prologue(s) { return s.kits.seq.er ? ["er = require 'er'"] : []; },
    reserved: 'er',
    toolbox: [{name: 'Sequencing', colour: C, contents: [
      b('nv_counter', {N: n(16)}),
      b('nv_pattern_hit', {STEP: {block: {type: 'variables_get', fields: {VAR: {name: 'step'}}}}}),
      b('nv_pattern_value', {STEP: {block: {type: 'variables_get', fields: {VAR: {name: 'step'}}}}}),
      b('nv_euclid', {STEP: {block: {type: 'variables_get', fields: {VAR: {name: 'step'}}}}, K: n(3), N: n(8), SHIFT: n(0)}),
      b('nv_chance', {P: n(50)}), b('nv_nth', {N: n(4)}),
      b('nv_chord', {ROOT: n(60)}, {TYPE: 'Minor 7'}),
      b('nv_arp', {STEP: {block: {type: 'variables_get', fields: {VAR: {name: 'step'}}}},
        LIST: {block: {type: 'nv_chord', inputs: {ROOT: n(60)}, fields: {TYPE: 'Minor 7'}}}}),
      b('nv_snap', {NOTE: n(61), ROOT: n(60)}, {SCALE: 'Minor Pentatonic'})]}],
  });
}));
