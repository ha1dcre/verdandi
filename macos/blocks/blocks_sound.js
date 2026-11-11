// blocks_sound.js: the Sound blocks of the script builder: notes, the knobs of the patch (the
// "sound" page, played by the Weave engine), MIDI into the sound, sample slots.
// a kit of blocks.js: browser after blocks.js; node: require(...)(VerdandiBlocks).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory;
  else factory(root.VerdandiBlocks);
}(this, function (VB) {
  'use strict';
  const C = 330;
  const n = VB.num, b = VB.entry;
  const note = (name) => ({type: 'input_value', name, check: 'Number'});

  // the knobs of the patch, for the dropdowns: [label, engine key]
  let choicesOf = () => [];
  const soundChoices = () => {
    const c = choicesOf();
    return c.length ? c : [['(the sound has no knobs)', 'none']];
  };

  VB.kit({
    name: 'sound',
    defs: [
      {type: 'nv_play_note', message0: 'play note %1 velocity %2 for %3 beats',
       args0: [note('NOTE'), note('VEL'), note('LEN')], inputsInline: true,
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'a MIDI note number (60 is middle C), velocity 0 to 127; nothing plays when the note is empty (a rest)'},
      {type: 'nv_play_hz', message0: 'play frequency %1 Hz velocity %2 for %3 beats',
       args0: [note('HZ'), note('VEL'), note('LEN')], inputsInline: true,
       previousStatement: null, nextStatement: null, colour: C},
      {type: 'nv_note_on', message0: 'start note %1 velocity %2', args0: [note('NOTE'), note('VEL')],
       inputsInline: true, previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'the note sounds until "stop note" with the same number: for keys held down'},
      {type: 'nv_note_off', message0: 'stop note %1', args0: [note('NOTE')],
       previousStatement: null, nextStatement: null, colour: C},
      {type: 'nv_notes_off', message0: 'stop all notes', previousStatement: null, nextStatement: null, colour: C},
      {type: 'nv_sound_set', message0: 'set sound %1 to %2', args0: [
        {type: 'field_dropdown', name: 'KEY', options: soundChoices}, note('VALUE')], inputsInline: true,
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'any knob of the sound page; it is also in PARAMETERS > sound'},
      {type: 'nv_sound_change', message0: 'change sound %1 by %2', args0: [
        {type: 'field_dropdown', name: 'KEY', options: soundChoices}, note('DELTA')], inputsInline: true,
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'adds to a knob, kept inside its range: "change sound filter1 cutoff by turn amount x 50"'},
      {type: 'nv_sound_get', message0: 'sound %1', args0: [
        {type: 'field_dropdown', name: 'KEY', options: soundChoices}], output: 'Number', colour: C},
      {type: 'nv_midi_play', message0: 'play MIDI notes with the sound', previousStatement: null,
       nextStatement: null, colour: C,
       tooltip: 'notes from the MIDI device in slot 1 play the sound, with their velocity'},
      {type: 'nv_load_sample', message0: 'load sound file %1 into sample slot %2', args0: [
        {type: 'field_input', name: 'FILE', text: 'my_sound.wav'}, note('SLOT')], inputsInline: true,
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'a file in dust/audio (or a full path) for the sample module of the sound page'},
      {type: 'nv_voices', message0: 'voices sounding', output: 'Number', colour: C,
       tooltip: 'how many notes sound right now (held or ringing out)'},
    ],
    context: {
      nv_midi_play: [['nv_init'], 'put this in "when the script starts"'],
    },
    generators(F, ctx) {
      const {g, O, v, luaString} = ctx;
      choicesOf = () => ctx.opts.weave && ctx.opts.getPatch
        ? ctx.opts.weave.choices(ctx.opts.getPatch(), ctx.opts.catalog) : [];
      const use = () => { ctx.S().kits.sound.used = true; ctx.use('musicutil'); };
      const known = key => choicesOf().some(c => c[1] === key);
      const id = key => luaString(String(key).replace('.', '_'));
      const missing = key => '-- (sound ' + key + ' is not on the sound page any more)\n';
      const player = () => g.provideFunction_('_blk_play', [
        '-- a note for a number of beats: an id of its own, so the same note can overlap itself',
        'function ' + g.FUNCTION_NAME_PLACEHOLDER_ + '(hz, vel, beats)',
        '  if hz == nil then return end',
        '  _blk_note_id = (_blk_note_id or 0) % 100000 + 1',
        '  local id = 1000 + _blk_note_id',
        '  engine.note_on(id, hz, util.clamp(vel, 0, 127) / 127)',
        '  if _blk_vis then _blk_vis.note_on(musicutil.freq_to_note_num(hz), vel) end',
        '  clock.run(function()',
        '    clock.sleep(clock.get_beat_sec() * beats)',
        '    engine.note_off(id)',
        '  end)',
        'end']);
      const hzOf = code => '_blk_hz(' + code + ')';
      const hzFn = () => g.provideFunction_('_blk_hz', [
        'function ' + g.FUNCTION_NAME_PLACEHOLDER_ + '(note)',
        '  if note == nil then return nil end',
        '  return musicutil.note_num_to_freq(note)',
        'end']);
      F.nv_play_note = bl => { use(); hzFn(); return player() + '(' + hzOf(v(bl, 'NOTE')) + ', ' + v(bl, 'VEL') + ', ' + v(bl, 'LEN') + ')\n'; };
      F.nv_play_hz = bl => { use(); return player() + '(' + v(bl, 'HZ') + ', ' + v(bl, 'VEL') + ', ' + v(bl, 'LEN') + ')\n'; };
      F.nv_note_on = bl => {
        use();
        const fn = g.provideFunction_('_blk_note_on', [
          'function ' + g.FUNCTION_NAME_PLACEHOLDER_ + '(note, vel)',
          '  if note == nil then return end',
          '  engine.note_on(note, musicutil.note_num_to_freq(note), util.clamp(vel, 0, 127) / 127)',
          '  if _blk_vis then _blk_vis.note_on(note, vel) end',
          'end']);
        return fn + '(' + v(bl, 'NOTE') + ', ' + v(bl, 'VEL') + ')\n';
      };
      F.nv_note_off = bl => { use(); return 'engine.note_off(' + v(bl, 'NOTE') + ')\n'; };
      F.nv_notes_off = () => { use(); return 'engine.all_off(0)\n'; };
      F.nv_sound_set = bl => {
        const key = bl.getFieldValue('KEY');
        use();
        return known(key) ? 'params:set(' + id(key) + ', ' + v(bl, 'VALUE') + ')\n' : missing(key);
      };
      F.nv_sound_change = bl => {
        const key = bl.getFieldValue('KEY');
        use();
        return known(key) ? 'params:set(' + id(key) + ', params:get(' + id(key) + ') + ' + v(bl, 'DELTA', O.ADDITIVE) + ')\n' : missing(key);
      };
      F.nv_sound_get = bl => {
        const key = bl.getFieldValue('KEY');
        use();
        return [known(key) ? 'params:get(' + id(key) + ')' : '0', O.HIGH];
      };
      F.nv_midi_play = () => {
        use();
        ctx.use('midi');
        const I = g.INDENT;
        ctx.S().midi.push({type: 'on', body: I + 'engine.note_on(note, musicutil.note_num_to_freq(note), vel / 127)\n' +
          I + 'if _blk_vis then _blk_vis.note_on(note, vel) end\n'});
        ctx.S().midi.push({type: 'off', body: I + 'engine.note_off(note)\n'});
        return '';
      };
      F.nv_load_sample = bl => {
        use();
        const fn = g.provideFunction_('_blk_load_sample', [
          '-- a file name alone is looked for in dust/audio',
          'function ' + g.FUNCTION_NAME_PLACEHOLDER_ + '(file, slot)',
          '  local path = string.sub(file, 1, 1) == "/" and file or _path.audio .. file',
          '  engine.load_sample(math.floor(slot), path)',
          'end']);
        return fn + '(' + luaString(bl.getFieldValue('FILE')) + ', ' + v(bl, 'SLOT') + ')\n';
      };
      F.nv_voices = () => { use(); ctx.S().kits.sound.voices = true; return ['_blk_voices', O.ATOMIC]; };
    },
    // the engine and the patch, when the sound page is on or a block plays it
    prologue(s) {
      const k = s.kits.sound;
      const o = s.opts;
      const patch = o.getPatch ? o.getPatch() : null;
      k.on = !!patch && !!o.weave && (patch.enabled !== false || !!k.used);
      if (!k.on) return [];
      return ["engine.name = 'Weave'", '', o.weave.lua(patch, o.catalog), ''];
    },
    initFirst(s, I) {
      const k = s.kits.sound;
      if (!k.on) return [];
      const lines = [I + '_blk_sound_init()'];
      if (k.voices) {
        lines.push(I + '_blk_voices = 0',
          I + 'local voices = poll.set("voices", function(v) _blk_voices = math.floor(v) end)',
          I + 'voices.time = 0.1', I + 'voices:start()');
      }
      return lines;
    },
    toolbox: [{name: 'Sound', colour: C, contents: [
      b('nv_play_note', {NOTE: n(60), VEL: n(100), LEN: n(0.5)}),
      b('nv_play_hz', {HZ: n(440), VEL: n(100), LEN: n(0.5)}),
      b('nv_note_on', {NOTE: n(60), VEL: n(100)}), b('nv_note_off', {NOTE: n(60)}), b('nv_notes_off'),
      b('nv_sound_set', {VALUE: n(1000)}), b('nv_sound_change', {DELTA: n(1)}), b('nv_sound_get'),
      b('nv_midi_play'), b('nv_load_sample', {SLOT: n(1)}), b('nv_voices')]}],
  });
}));
