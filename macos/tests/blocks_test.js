// scenarios for the script builder (macos/blocks): the Lua it writes, checked by luac and run
// against a stand-in norns with the real musicutil and sequins (blocks_norns_stub.lua).
// run from the repo root:
//   node macos/tests/blocks_test.js <blockly package dir> <deps prefix> <dir with node_modules/jsdom>
// for example, from the repo root:
//   node macos/tests/blocks_test.js ../../../deps/src/blockly-13.3.0 ../../../deps/prefix ../../../deps/src/blockly-test
// (jsdom once: npm install --prefix ../../../deps/src/blockly-test jsdom@29.1.1; Blockly needs an
// XML DOM for shadow blocks outside a browser, see its core-node.js)
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const {execFileSync} = require('child_process');

const [blocklyDir, prefix, jsdomDir] = process.argv.slice(2).map(p => path.resolve(p));
if (!blocklyDir || !prefix || !jsdomDir) {
  console.error('usage: node macos/tests/blocks_test.js <blockly package dir> <deps prefix> <dir with node_modules/jsdom>');
  process.exit(2);
}
const repo = path.resolve(__dirname, '../..');
const Blockly = require(path.join(blocklyDir, 'blockly_compressed.js'));
const {JSDOM} = require(path.join(jsdomDir, 'node_modules', 'jsdom'));
Blockly.utils.xml.injectDependencies(new JSDOM('<!DOCTYPE html>').window);
require(path.join(blocklyDir, 'blocks_compressed.js'));
const lua = require(path.join(blocklyDir, 'lua_compressed.js'));
Blockly.setLocale(require(path.join(blocklyDir, 'msg', 'en.js')));
const VB = require(path.join(repo, 'macos', 'blocks', 'blocks.js'));
const api = VB.register(Blockly, lua);

const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'blocks-test-'));
let failed = 0;
function check(name, ok, detail) {
  console.log((ok ? 'ok   ' : 'FAIL ') + name + (ok || !detail ? '' : '\n     ' + detail));
  if (!ok) failed++;
}

function generate(json, name) {
  const ws = new Blockly.Workspace();
  Blockly.serialization.workspaces.load(json, ws);
  const code = api.generate(ws, name);
  ws.dispose();
  return code;
}

// luac, then the stand-in norns; returns its output lines
function run(name, code) {
  const file = path.join(tmp, name + '.lua');
  fs.writeFileSync(file, code);
  try {
    execFileSync(path.join(prefix, 'bin', 'luac'), ['-p', file], {stdio: 'pipe'});
  } catch (e) {
    check(name + ': luac', false, String(e.stderr) + '\n' + code);
    return [];
  }
  try {
    const out = execFileSync(path.join(prefix, 'bin', 'lua'),
      [path.join(repo, 'macos', 'tests', 'blocks_norns_stub.lua'), repo, file], {stdio: 'pipe'});
    return String(out).split('\n');
  } catch (e) {
    check(name + ': runs', false, String(e.stderr) + String(e.stdout).split('\n').slice(-8).join('\n') + '\n' + code);
    return [];
  }
}

// 1. the example the builder opens with
{
  const demo = JSON.parse(fs.readFileSync(path.join(repo, 'macos', 'blocks', 'demo.json')));
  const code = generate(demo.workspace, demo.name);
  const out = run('demo', code);
  check('demo: runs to the end', out.includes('-- ok'), out.slice(-5).join('\n'));
  check('demo: PolyPerc', /^engine\.name = 'PolyPerc'$/m.test(code));
  check('demo: control added with its range', out.includes('params.add_control cutoff cutoff 200 5000 1200'));
  const hz = out.filter(l => l.startsWith('engine.hz')).map(l => l.split(' ')[1]);
  // the loop waits for the clock first: three clock steps play melody steps 1 3 5 of A minor
  // pentatonic from 57 (57 62 67); then K2 stops it, so step 8 (74) never sounds
  check('demo: the melody, then K2 stops it', hz.join(' ') === '220.000 293.665 391.995',
    'engine.hz: ' + hz.join(' '));
  check('demo: E2 changes cutoff, E3 does not', out.includes('params.set cutoff 1250') &&
    out.includes('params.set cutoff 1100') && out.filter(l => l.startsWith('params.set')).length === 2,
    out.filter(l => l.startsWith('params.set')).join(', '));
  check('demo: sequenced cutoff follows the control', out.includes('engine.cutoff 1200'));
  check('demo: redraw clears, draws, updates', /screen\.clear[\s\S]*screen\.text first steps[\s\S]*screen\.text cutoff 1100 Hz[\s\S]*screen\.rect 2 38 27\.500 4[\s\S]*screen\.text K2: play[\s\S]*screen\.update/.test(out.join('\n')),
    out.filter(l => l.startsWith('screen.')).join(', '));
  check('demo: only its own globals', out.filter(l => l.startsWith('global ')).sort().join(',') ===
    'global melody,global playing', out.filter(l => l.startsWith('global ')).join(','));
}

// 1b. the visual example: widgets, animation, both encoders
{
  const doc = JSON.parse(fs.readFileSync(path.join(repo, 'macos', 'blocks', 'pulse.json')));
  const code = generate(doc.workspace, doc.name);
  const out = run('pulse', code);
  const text = out.join('\n');
  check('pulse: runs to the end', out.includes('-- ok'), out.slice(-5).join('\n'));
  // the dial writes its control's name centred under it, through the real ui.lua
  check('pulse: the dial shows its control', /screen\.text_center size/.test(text),
    out.filter(l => /text_center/.test(l)).join(', '));
  check('pulse: E2 and E3 each move their own control', out.filter(l => l.startsWith('params.set size')).length === 2 &&
    out.filter(l => l.startsWith('params.set spread')).length === 2, out.filter(l => l.startsWith('params.set')).join(', '));
  check('pulse: the ring breathes with the beat', /screen\.circle 64 32 \d/.test(text) && new Set(out.filter(l => l.startsWith('screen.circle 64 32') && !l.endsWith(' 2')).map(l => l.split(' ')[3])).size > 1,
    out.filter(l => l.startsWith('screen.circle')).join(', '));
}

// 2. every norns block, each where it belongs, with an input in every slot
{
  const value = (input, check) => {
    if (check === 'Array') return {block: {type: 'lists_create_with', extraState: {itemCount: 3}, inputs: {
      ADD0: {block: {type: 'math_number', fields: {NUM: 1}}}, ADD1: {block: {type: 'math_number', fields: {NUM: 2}}},
      ADD2: {block: {type: 'math_number', fields: {NUM: 3}}}}}};
    if (input === 'TEXT') return {block: {type: 'text', fields: {TEXT: 'x'}}};
    if (check === 'Boolean') return {block: {type: 'logic_boolean', fields: {BOOL: 'TRUE'}}};
    return {block: {type: 'math_number', fields: {NUM: input === 'CH' ? 1 : 2}}};
  };
  const make = (type) => {
    const def = Blockly.Blocks[type];
    const probe = new Blockly.Workspace();
    const b = probe.newBlock(type);
    const inputs = {};
    for (const inp of b.inputList) {
      if (inp.type === Blockly.inputs.inputTypes.VALUE) {
        inputs[inp.name] = value(inp.name, inp.connection.getCheck() && inp.connection.getCheck()[0]);
      }
    }
    const out = {kind: b.outputConnection ? 'value' : b.previousConnection ? 'statement' : 'hat'};
    probe.dispose();
    void def;
    out.json = {type, inputs};
    if (type === 'nv_seq_next') out.json.fields = {VAR: {id: 'v_seq'}};
    return out;
  };
  const chain = list => list.reduceRight((next, b) => next ? Object.assign({}, b, {next: {block: next}}) : b, null);
  const print = v => ({type: 'text_print', inputs: {TEXT: {block: v}}});
  const inHat = {nv_enc_delta: 'nv_enc', nv_midi_note_num: 'nv_midi_note', nv_midi_velocity: 'nv_midi_note',
    nv_param_add: 'nv_init', nv_ui_dial: 'nv_redraw', nv_ui_slider: 'nv_redraw', nv_ui_playback: 'nv_redraw'};
  const bodies = {nv_init: [{type: 'variables_set', fields: {VAR: {id: 'v_seq'}}, inputs: {VALUE: {block:
    {type: 'nv_seq_make', inputs: {LIST: value('LIST', 'Array')}}}}}], nv_every: [], nv_enc: [], nv_midi_note: [],
    nv_redraw: []};
  const seen = [];
  for (const type of api.TYPES) {
    const m = make(type);
    if (m.kind === 'hat') continue;
    if (type === 'nv_seq_make') { seen.push(type); continue; }  // in nv_init above
    const hat = inHat[type] || 'nv_every';
    bodies[hat].push(m.kind === 'value' ? print(m.json) : m.json);
    seen.push(type);
  }
  // controls added first, before anything reads them
  bodies.nv_init.unshift({type: 'nv_param_add', fields: {NAME: 'cutoff', MIN: 0, MAX: 100, DEFAULT: 50}});
  bodies.nv_init = bodies.nv_init.filter((b, i) => i === 0 || b.type !== 'nv_param_add');
  const top = [];
  for (const hat of api.HATS) {
    const list = bodies[hat] || [];
    top.push({type: hat, inputs: list.length ? {DO: {block: chain(list)}} : {}});
  }
  // a key block for releasing, next to the pressed one nv_key got above
  top.push({type: 'nv_key', fields: {KEY: '3', STATE: '0'}, inputs: {DO: {block: print({type: 'text', fields: {TEXT: 'k3 up'}})}}});
  // and the note-off side of MIDI: printed with a marker, so each event shows which side ran
  top.push({type: 'nv_midi_note', fields: {TYPE: 'off'}, inputs: {DO: {block: print(
    {type: 'text_join', extraState: {itemCount: 2}, inputs: {ADD0: {block: {type: 'text', fields: {TEXT: 'off '}}},
      ADD1: {block: {type: 'nv_midi_note_num'}}}})}}});
  const json = {variables: [{name: 'seq', id: 'v_seq'}], blocks: {languageVersion: 0, blocks: top}};
  const code = generate(json, 'every_block');
  const out = run('every_block', code);
  check('every block: runs to the end (' + seen.length + ' blocks)', out.includes('-- ok'), out.slice(-5).join('\n'));
  // note on 64 vel 100, note on 64 vel 0 (= note off), note off 67
  const midiOut = out.slice(out.indexOf('-- midi'), out.lastIndexOf('-- clock'));
  check('every block: midi note on, off and the velocity-0 note off',
    midiOut.filter(l => l === 'off 64').length === 1 && midiOut.filter(l => l === 'off 67').length === 1 &&
    midiOut.filter(l => l === '100').length === 1, midiOut.join(' | '));
  check('every block: sent MIDI note ends after its length', /midi\.note_on 2 2 1[\s\S]*clock\.sleep 1\.000[\s\S]*midi\.note_off 2 0 1/.test(out.join('\n')),
    out.filter(l => l.startsWith('midi.') || l.startsWith('clock.sleep')).join(', '));
  check('every block: "released" runs on release only', out.filter(l => l === 'k3 up').length === 1 &&
    out[out.indexOf('k3 up') - 1] === 'key 3 0', out.slice(out.indexOf('-- keys'), out.indexOf('-- keys') + 12).join(' | '));
  check('every block: wait is in seconds of the tempo', out.includes('clock.sleep 1.000'));
  // the widgets drew through the real lua/lib/ui.lua, twice (clock redraw and the redraw() call)
  check('every block: dial, slider and play symbol drew', out.filter(l => l === 'screen.arc 2 2 2.000 -3.927 -3.927' || l.startsWith('screen.arc')).length >= 2 &&
    /screen\.text_center cutoff/.test(out.join('\n')), out.filter(l => /arc|text_center/.test(l)).slice(0, 6).join(', '));
  check('every block: frame number counts drawings', /_blk_frame = _blk_frame \+ 1/.test(code));
}

// 3. names a script takes for itself must not replace norns' own
{
  const json = {variables: [{name: 'key', id: 'a'}, {name: 'n', id: 'b'}, {name: 'screen', id: 'c'}],
    blocks: {languageVersion: 0, blocks: [
      {type: 'nv_init', inputs: {DO: {block: {type: 'variables_set', fields: {VAR: {id: 'a'}},
        inputs: {VALUE: {block: {type: 'math_number', fields: {NUM: 1}}}},
        next: {block: {type: 'variables_set', fields: {VAR: {id: 'c'}},
          inputs: {VALUE: {block: {type: 'math_number', fields: {NUM: 2}}}}}}}}}},
      {type: 'nv_key', fields: {KEY: '2', STATE: '1'}, inputs: {DO: {block: {type: 'variables_set',
        fields: {VAR: {id: 'b'}}, inputs: {VALUE: {block: {type: 'math_number', fields: {NUM: 3}}}}}}}},
      {type: 'nv_redraw', inputs: {}},
      // a loose block outside every event block: never runs, left out
      {type: 'nv_play_note', x: 400, y: 400, inputs: {NOTE: {block: {type: 'math_number', fields: {NUM: 99}}}}},
    ]}};
  const code = generate(json, 'names');
  const out = run('names', code);
  check('names: runs (key and screen still norns\')', out.includes('-- ok'), out.slice(-5).join('\n') + '\n' + code);
  check('names: renamed', !/^\s*(key|n|screen) = /m.test(code), code);
  check('names: loose block left out', !code.includes('99') && !code.includes('PolyPerc'), code);
}

// 4. the toolbox only offers defined blocks and offers every norns block
{
  const tb = VB.toolbox();
  const types = [];
  const walk = c => { for (const x of c.contents || []) { if (x.type) types.push(x.type); walk(x); } };
  walk(tb);
  const undefinedTypes = types.filter(t => !Blockly.Blocks[t]);
  check('toolbox: every entry is a defined block', undefinedTypes.length === 0, undefinedTypes.join(', '));
  const missing = api.TYPES.filter(t => !types.includes(t));
  check('toolbox: every norns block is offered', missing.length === 0, missing.join(', '));
  // the toolbox entries themselves load (their inputs and fields are valid)
  const ws = new Blockly.Workspace();
  let bad = [];
  for (const t of types) {
    const entry = (function find(c) { for (const x of c.contents || []) { if (x.type === t) return x; const f = find(x); if (f) return f; } })(tb);
    try { Blockly.serialization.blocks.append({type: t, inputs: entry.inputs, fields: entry.fields}, ws); } catch (e) { bad.push(t + ': ' + e.message); }
  }
  ws.dispose();
  check('toolbox: every entry loads', bad.length === 0, bad.join('\n     '));
}

fs.rmSync(tmp, {recursive: true, force: true});
console.log(failed ? failed + ' failed' : 'all passed');
process.exit(failed ? 1 : 0);
