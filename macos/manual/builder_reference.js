// builder_reference.js: the script builder reference (the appendix of macos/manual/manual.html), written from the
// builder's own definitions (blocks, macos/blocks/weave.json, macos/blocks/screen.json), so the
// manual lists exactly what the builder has. run from the repo root after changing blocks or
// catalogs:   node macos/manual/builder_reference.js
'use strict';
const fs = require('fs');
const path = require('path');
const repo = path.resolve(__dirname, '../..');
const B = p => path.join(repo, 'macos/blocks', p);
const VB = require(B('blocks.js'));
for (const k of ['sound', 'seq', 'looper', 'grid', 'ui', 'screen']) require(B('blocks_' + k + '.js'))(VB);
const weave = JSON.parse(fs.readFileSync(B('weave.json')));
const screen = JSON.parse(fs.readFileSync(B('screen.json')));
const esc = s => String(s).replace(/&/g, '&amp;').replace(/</g, '&lt;').replace(/>/g, '&gt;');

// a block as text: its words, ( ) for a slot, [..] for a choice or field
const defs = Object.fromEntries(VB.definitions().map(d => [d.type, d]));
function blockText(def) {
  const args = def.args0 || [];
  return esc(def.message0.replace(/%(\d+)/g, (_, i) => {
    const a = args[Number(i) - 1];
    if (!a) return '';
    switch (a.type) {
      case 'input_value': return '( )';
      case 'field_dropdown': return Array.isArray(a.options) ? '[' + a.options[0][0] + ']' : '[…]';
      case 'field_input': return '[' + a.text + ']';
      case 'field_number': return '[' + a.value + ']';
      case 'field_variable': return '[' + a.variable + ']';
      default: return '';
    }
  }).replace(/\s+/g, ' ').trim()).replace(/\( \)/g, '<code>( )</code>');
}
let blocks = '';
for (const cat of VB.toolbox().contents) {
  if (cat.kind !== 'category' || !cat.contents || cat.categorystyle) continue;
  const rows = [];
  const seen = new Set();
  for (const e of cat.contents) {
    const d = defs[e.type];
    if (!d || seen.has(e.type)) continue;
    seen.add(e.type);
    rows.push('<tr><td class="blockname">' + blockText(d) + '</td><td>' + esc(d.tooltip || '') + '</td></tr>');
  }
  blocks += '<h4>' + esc(cat.name) + '</h4>\n<table>\n<tr><th>block</th><th>what it does</th></tr>\n' + rows.join('\n') + '\n</table>\n';
}

const paramList = ps => ps.map(p => esc(p.label) + (p.options ? ' (' + p.options.map(esc).join(', ') + ')' : '')).join(', ');
const groups = {source: 'sources', shape: 'shapers', mod: 'modulators', fixed: 'always there'};
let sound = '<p class="small">every module of the sound page; its knobs are its params. option params are choices.</p>\n';
for (const g of ['fixed', 'source', 'shape', 'mod']) {
  sound += '<h4>' + groups[g] + '</h4>\n<table>\n<tr><th>module</th><th>what it does</th><th>in / out</th><th>knobs</th></tr>\n';
  for (const m of weave.voice.filter(x => x.group === g)) {
    sound += '<tr><td><b>' + esc(m.label) + '</b></td><td>' + esc(m.about) + '</td><td>' +
      (m.inputs.map(i => esc(i.label)).join(', ') || '&ndash;') + ' / ' + (m.outputs.map(o => esc(o.label)).join(', ') || '&ndash;') +
      '</td><td>' + paramList(m.params) + '</td></tr>\n';
  }
  sound += '</table>\n';
}
sound += '<h4>effects</h4>\n<table>\n<tr><th>effect</th><th>what it does</th><th>knobs</th></tr>\n';
for (const f of weave.fx) sound += '<tr><td><b>' + esc(f.label) + '</b></td><td>' + esc(f.about) + '</td><td>' + paramList(f.params) + '</td></tr>\n';
sound += '</table>\n';

let scr = '<table>\n<tr><th>element</th><th>what it draws</th><th>properties</th></tr>\n';
for (const e of screen.elements) {
  scr += '<tr><td><b>' + esc(e.label) + '</b> <span class="small">(' + esc(e.group) + ')</span></td><td>' + esc(e.about) +
    '</td><td>' + paramList(e.props.concat(screen.common)) + '</td></tr>\n';
}
scr += '</table>\n<h4>how a number moves</h4>\n<table>\n<tr><th>moves</th><th>how</th><th>settings</th></tr>\n';
for (const a of screen.animations) {
  scr += '<tr><td><b>' + esc(a.label) + '</b></td><td>' + esc(a.about) + '</td><td>from, to' +
    (a.fields.length ? ', ' + paramList(a.fields) : '') + '</td></tr>\n';
}
scr += '</table>\n';

const file = path.join(repo, 'macos/manual/manual.html');
let html = fs.readFileSync(file, 'utf8');
for (const [key, body] of [['blocks', blocks], ['sound', sound], ['screen', scr]]) {
  const re = new RegExp('(<!-- begin:' + key + ' -->)[\\s\\S]*?(<!-- end:' + key + ' -->)');
  if (!re.test(html)) throw new Error('no ' + key + ' markers in manual.html');
  html = html.replace(re, '$1\n' + body + '$2');
}
fs.writeFileSync(file, html);
console.log('manual.html: script builder reference of ' + Object.keys(defs).length + ' blocks, ' + weave.voice.length + ' modules, ' +
  weave.fx.length + ' effects, ' + screen.elements.length + ' screen elements');
