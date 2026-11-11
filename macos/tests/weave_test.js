// scenarios for Weave, the modular engine of the script builder: every module and effect of
// macos/blocks/weave.json built by sc/engines/WeaveGraph.sc (each catalog param a control), every
// preset rendered offline and listened to (sound, no clipping, pitch, voices that end), and
// patches that must be refused. run from the repo root:
//   node macos/tests/weave_test.js <sclang> <sc extensions dir>
// for example:
//   node macos/tests/weave_test.js ../../../deps/prefix/SuperCollider/SuperCollider.app/Contents/MacOS/sclang \
//     "../Library/Application Support/SuperCollider/Extensions"
'use strict';
const fs = require('fs');
const os = require('os');
const path = require('path');
const {execFileSync} = require('child_process');

const [sclang, extensions] = process.argv.slice(2).map(p => path.resolve(p));
if (!sclang || !extensions) {
  console.error('usage: node macos/tests/weave_test.js <sclang> <sc extensions dir>');
  process.exit(2);
}
const repo = path.resolve(__dirname, '../..');
const catalog = JSON.parse(fs.readFileSync(path.join(repo, 'macos/blocks/weave.json')));
const presets = JSON.parse(fs.readFileSync(path.join(repo, 'macos/blocks/weave_presets.json')));
const W = require(path.join(repo, 'macos/blocks/weave.js'));
const types = {};
for (const d of catalog.voice.concat(catalog.fx)) types[d.type] = d;

let failed = 0;
function check(name, ok, detail) {
  console.log((ok ? 'ok   ' : 'FAIL ') + name + (ok || !detail ? '' : '\n     ' + detail));
  if (!ok) failed++;
}

const hz = n => 440 * Math.pow(2, (n - 69) / 12);
const chord = (len) => [[0, len, hz(48), 0.8], [0, len, hz(55), 0.8], [0, len, hz(64), 0.8]];
const controls = patch => W.params(patch, catalog).map(p => p.id);
const manifest = [];
const add = (name, patch, extra) => manifest.push(Object.assign({name, text: W.text(patch, catalog), controls: controls(patch)}, extra));
const mod = (id, type, params) => ({id, type, params: params || {}});

// 1. every voice module, where it belongs: sources into the output, shapers fed by a saw,
//    modulators as the amp of a saw
for (const d of catalog.voice) {
  if (d.group === 'fixed') continue;
  const id = d.type + '1';
  const modules = [mod(id, d.type)];
  const cables = [];
  const takes = d.inputs.filter(i => ['in', 'a', 'b', 'in1'].includes(i.name));
  if (d.group === 'mod') {
    modules.push(mod('osc1', 'osc'), mod('vca1', 'vca'));
    cables.push({from: 'osc1.out', to: 'vca1.in'}, {from: id + '.out', to: 'vca1.cv'}, {from: 'vca1.out', to: 'out.in'});
  } else {
    if (takes.length) {
      modules.push(mod('osc1', 'osc'));
      for (const t of takes) cables.push({from: 'osc1.out', to: id + '.' + t.name});
    }
    cables.push({from: id + '.out', to: 'out.in'});
  }
  add('module_' + d.type, {modules, cables, fx: []}, d.type === 'sample' ? {} : {notes: [[0, 0.6, hz(60), 0.8]], dur: 1.2});
}
// 2. every effect, behind the default pluck
for (const d of catalog.fx) {
  if (d.group === 'fixed') continue;
  add('fx_' + d.type, Object.assign({}, W.DEFAULT_PATCH, {fx: [{id: 'e1', type: d.type, params: {}}]}), {notes: chord(0.4), dur: 1.5});
}
// 3. the presets, a chord each (the sampler has no file loaded: built, not heard)
for (const p of presets) {
  const notes = p.name === 'kick' ? [[0, 0.3, hz(36), 1]] : p.name === 'bass' || p.name === 'acid'
    ? [[0, 0.3, hz(36), 0.8], [0.3, 0.6, hz(43), 0.8], [0.6, 0.9, hz(48), 0.8]] : chord(1);
  add('preset_' + p.name, p.patch, p.name === 'sampler' ? {} : {notes, dur: p.name === 'drone' || p.name === 'pad' ? 6 : 3.5});
}
// 4. pitch: a sine at A4 and A3
const sine = {modules: [mod('osc1', 'osc', {wave: 0}), mod('env1', 'env', {a: 0.001, s: 1, vel: 0}), mod('vca1', 'vca')],
  cables: [{from: 'osc1.out', to: 'vca1.in'}, {from: 'env1.out', to: 'vca1.cv'}, {from: 'vca1.out', to: 'out.in'}], fx: []};
add('pitch_440', sine, {notes: [[0, 1, 440, 1]], dur: 1.2});
add('pitch_220_bend', Object.assign({}, sine, {modules: sine.modules.concat([mod('voice', 'voice', {bend: -12})])}), {notes: [[0, 1, 440, 1]], dur: 1.2});
// 5. what must be refused
manifest.push({name: 'refuse_loop', text: 'm osc1 osc\nm filter1 filter\nc osc1.out filter1.in\nc filter1.out osc1.fm\nc filter1.out out.in\n'});
manifest.push({name: 'refuse_unknown', text: 'm x1 nosuchthing\n'});
manifest.push({name: 'refuse_missing_module', text: 'c ghost1.out out.in\n'});
manifest.push({name: 'refuse_missing_output', text: 'm osc1 osc\nc osc1.nothing out.in\n'});

// ---- sclang
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), 'weave-test-'));
const classes = path.join(tmp, 'classes');
fs.mkdirSync(classes);
for (const f of ['WeaveGraph.sc']) fs.symlinkSync(path.join(repo, 'sc/engines', f), path.join(classes, f));
fs.writeFileSync(path.join(tmp, 'conf.yaml'),
  'includePaths:\n    - "' + classes + '"\n    - "' + extensions + '"\nexcludePaths:\n    []\npostInlineWarnings: false\n');
fs.writeFileSync(path.join(tmp, 'manifest.json'), JSON.stringify(manifest));
let log = '';
try {
  log = String(execFileSync(sclang, ['-l', path.join(tmp, 'conf.yaml'), path.join(repo, 'macos/tests/weave_test.scd'),
    path.join(tmp, 'manifest.json'), tmp], {stdio: 'pipe', timeout: 600000}));
} catch (e) {
  log = String(e.stdout) + String(e.stderr);
}
const resultsFile = path.join(tmp, 'results.json');
if (!fs.existsSync(resultsFile)) {
  console.log(log.split('\n').filter(l => /ERROR|error|Exception/.test(l)).slice(0, 20).join('\n'));
  check('sclang ran the manifest', false, 'no results.json');
  process.exit(1);
}
const results = JSON.parse(fs.readFileSync(resultsFile));
const byName = Object.fromEntries(results.map(r => [r.name, r]));
const scErrors = log.split('\n').filter(l => /^ERROR|FAILURE IN SERVER|exception in GraphDef/.test(l));
check('sclang: no errors in its log', scErrors.length === 0, scErrors.slice(0, 6).join('\n     '));

// ---- the wav files
function readWav(file) {
  const b = fs.readFileSync(file);
  let p = 12, fmt = null, data = null;
  while (p < b.length - 8) {
    const id = b.toString('ascii', p, p + 4), size = b.readUInt32LE(p + 4);
    if (id === 'fmt ') fmt = {channels: b.readUInt16LE(p + 10), rate: b.readUInt32LE(p + 12), bits: b.readUInt16LE(p + 22)};
    if (id === 'data') { data = b.subarray(p + 8, p + 8 + size); break; }
    p += 8 + size + (size % 2);
  }
  const n = data.length / 4;
  const all = new Float32Array(n);
  for (let i = 0; i < n; i++) all[i] = data.readFloatLE(i * 4);
  const left = new Float32Array(n / fmt.channels);
  for (let i = 0; i < left.length; i++) left[i] = all[i * fmt.channels];
  return {rate: fmt.rate, all, left};
}
const peak = (a, from, to) => { let m = 0; for (let i = from || 0; i < (to || a.length); i++) m = Math.max(m, Math.abs(a[i])); return m; };
const finite = a => a.every(Number.isFinite);
function frequency(x, rate, from, to) {
  let crossings = 0;
  for (let i = from + 1; i < to; i++) if (x[i - 1] <= 0 && x[i] > 0) crossings++;
  return crossings / ((to - from) / rate);
}

for (const item of manifest) {
  const r = byName[item.name];
  if (!r) { check(item.name + ': in the results', false); continue; }
  if (item.name.startsWith('refuse_')) {
    check(item.name + ': refused with a reason', !!r.error && /weave:/.test(r.error), r.error || 'it was built');
    continue;
  }
  check(item.name + ': builds', !r.error, r.error);
  if (r.error) continue;
  check(item.name + ': every catalog param is a control', r.missing.length === 0, r.missing.join(', '));
  if (!item.notes) continue;
  const w = readWav(r.wav);
  const pk = peak(w.all);
  check(item.name + ': sounds, finite, not clipping', finite(w.all) && pk > 0.01 && pk <= 0.99, 'peak ' + pk.toFixed(4) + (finite(w.all) ? '' : ', NaN/inf'));
  if (item.name.startsWith('pitch_')) {
    const f = frequency(w.left, w.rate, Math.round(0.2 * w.rate), Math.round(0.9 * w.rate));
    const want = item.name === 'pitch_440' ? 440 : 220;
    check(item.name + ': plays ' + want + ' Hz', Math.abs(f - want) < 3, f.toFixed(1) + ' Hz');
  }
  // voices that end: after the release, nothing is left of a plucked sound
  if (['preset_pluck', 'preset_kick'].includes(item.name)) {
    const tail = peak(w.all, Math.round((item.dur - 0.25) * w.rate) * 2);
    check(item.name + ': silent after its release', tail < 0.002, 'tail peak ' + tail.toFixed(5));
  }
}

fs.rmSync(tmp, {recursive: true, force: true});
console.log(failed ? failed + ' failed' : 'all passed (' + manifest.length + ' patches)');
process.exit(failed ? 1 : 0);
