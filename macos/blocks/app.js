// app.js: the script builder page. the blocks (what the script does) and the sound page (the
// patch the Weave engine plays) make one document; it saves through maiden's file api
// (/api/v1/dust/...), runs through matron's websocket (port 5555, the one maiden's REPL uses)
// and previews in the page. while a script made here runs, the sound page plays live.
(async function () {
  'use strict';
  const API = '/api/v1/dust/';
  const FORMAT = 'verdandi-blocks';
  const DRAFT = 'verdandi-blocks-draft';
  const $ = id => document.getElementById(id);
  const clone = o => JSON.parse(JSON.stringify(o));

  const catalog = await (await fetch('weave.json')).json();
  const presets = await (await fetch('weave_presets.json')).json();
  const W = window.VerdandiWeave;

  // ---- the sound page
  let patch = clone(W.DEFAULT_PATCH);
  const isOption = key => W.params(patch, catalog).some(p => p.key === key && p.options);
  const patcher = new VerdandiPatcher($('patcher'), catalog, {
    presets,
    onChange: (p, structural) => {
      patch = p;
      changed();
      if (structural && liveName()) rerunSoon();
    },
    onParam: (key, value) => {
      if (!liveName()) return;
      send('params:set("' + key.replace('.', '_') + '", ' + (isOption(key) ? value + 1 : value) + ')', true);
    },
    onNote: (on, note, vel) => {
      if (!liveName()) {
        if (on) status('run the script to hear the sound: the keys then play it in Verdandi');
        return;
      }
      const hz = 440 * Math.pow(2, (note - 69) / 12);
      send(on ? 'engine.note_on(' + (5000 + note) + ', ' + hz.toFixed(3) + ', ' + vel + ')' : 'engine.note_off(' + (5000 + note) + ')', true);
    },
  });
  patcher.setPatch(patch);

  const gen = VerdandiBlocks.register(Blockly, window.lua, {getPatch: () => patch, weave: W, catalog});

  // ---- workspace
  const dark = window.matchMedia('(prefers-color-scheme: dark)');
  const themes = {
    light: Blockly.Theme.defineTheme('verdandi-light', {base: Blockly.Themes.Classic, name: 'verdandi-light',
      componentStyles: {workspaceBackgroundColour: '#f4f4f2', toolboxBackgroundColour: '#ffffff',
        flyoutBackgroundColour: '#ebebe7', flyoutOpacity: 1, scrollbarColour: '#c8c8c2'}}),
    dark: Blockly.Theme.defineTheme('verdandi-dark', {base: Blockly.Themes.Classic, name: 'verdandi-dark',
      componentStyles: {workspaceBackgroundColour: '#151515', toolboxBackgroundColour: '#1d1d1d',
        toolboxForegroundColour: '#ddd', flyoutBackgroundColour: '#242424', flyoutForegroundColour: '#ccc',
        flyoutOpacity: 1, scrollbarColour: '#444', insertionMarkerColour: '#fff', cursorColour: '#ddd'}}),
  };
  const ws = Blockly.inject('blockly', {
    toolbox: VerdandiBlocks.toolbox(),
    media: 'vendor/blockly/media/',
    renderer: 'zelos',
    theme: dark.matches ? themes.dark : themes.light,
    grid: {spacing: 24, length: 2, colour: dark.matches ? '#2a2a2a' : '#deded8', snap: true},
    zoom: {controls: true, wheel: true, startScale: 0.75, maxScale: 2, minScale: 0.3},
    trashcan: true,
    sounds: false,
  });
  dark.addEventListener('change', () => ws.setTheme(dark.matches ? themes.dark : themes.light));
  ws.addChangeListener(Blockly.Events.disableOrphans);

  // ---- views: blocks or sound
  let view = 'blocks';
  function showView(which) {
    view = which;
    for (const t of document.querySelectorAll('[data-view]')) t.setAttribute('aria-selected', String(t.dataset.view === which));
    $('blockly').hidden = which !== 'blocks';
    $('patcher').hidden = which !== 'sound';
    if (which === 'blocks') Blockly.svgResize(ws);
    guide.update();
  }
  for (const t of document.querySelectorAll('[data-view]')) t.addEventListener('click', () => showView(t.dataset.view));

  // ---- name
  const nameInput = $('name');
  const cleanName = s => String(s).trim().toLowerCase().replace(/[^a-z0-9_-]+/g, '_').replace(/^[_-]+|[_-]+$/g, '').slice(0, 40);
  nameInput.addEventListener('change', () => { nameInput.value = cleanName(nameInput.value); changed(); });
  const scriptName = () => cleanName(nameInput.value);

  // ---- status line and console
  function status(text, kind) {
    const f = $('status');
    f.textContent = text;
    f.className = kind || '';
  }
  function consoleLine(text, kind) {
    const pre = $('console');
    const line = document.createElement('div');
    if (kind) line.className = kind;
    line.textContent = text;
    pre.appendChild(line);
    while (pre.childNodes.length > 500) pre.removeChild(pre.firstChild);
    pre.scrollTop = pre.scrollHeight;
  }

  // ---- the Lua
  let code = '';
  function regenerate() {
    try {
      code = gen.generate(ws, scriptName() || 'untitled');
    } catch (e) {
      code = '-- the blocks could not be turned into Lua: ' + e.message + '\n';
    }
    $('lua').textContent = code;
  }
  function documentJson() {
    return {format: FORMAT, version: 2, name: scriptName(), workspace: Blockly.serialization.workspaces.save(ws),
      patch: patcher.getPatch()};
  }

  // ---- changes: Lua view, draft, preview, guide
  // unsaved: the blocks, sound and name differ from what was last saved or opened
  let clean = '';
  const snapshot = () => JSON.stringify(documentJson());
  const isDirty = () => snapshot() !== clean;
  const markClean = () => { clean = snapshot(); };
  let pending = null;
  function changed() {
    clearTimeout(pending);
    pending = setTimeout(() => {
      regenerate();
      try { localStorage.setItem(DRAFT, JSON.stringify(documentJson())); } catch (e) { /* no storage: fine */ }
      if (preview.running() || previewWanted) startPreview();
      guide.update();
    }, 350);
  }
  ws.addChangeListener(e => { if (!e.isUiEvent) changed(); });

  // examples: the logic in a column on the left, the drawing next to it, all in view
  function arrange() {
    const tops = ws.getTopBlocks(true);
    const gap = 40;
    let y = 20, w = 0;
    for (const b of tops.filter(b => b.type !== 'nv_redraw')) {
      b.moveTo(new Blockly.utils.Coordinate(20, y));
      const hw = b.getHeightWidth();
      y += hw.height + gap;
      w = Math.max(w, hw.width);
    }
    y = 20;
    for (const b of tops.filter(b => b.type === 'nv_redraw')) {
      b.moveTo(new Blockly.utils.Coordinate(20 + w + gap * 1.5, y));
      y += b.getHeightWidth().height + gap;
    }
    // all of it when that stays readable, else readable from the top left
    ws.zoomToFit();
    const scale = Math.max(0.6, Math.min(0.8, ws.scale));
    ws.setScale(scale);
    ws.scroll(16 - 20 * scale, 16 - 20 * scale);
  }

  function loadDocument(doc, opts) {
    // the sound first: the "sound ..." blocks offer its knobs
    patch = clone((doc && doc.patch) || W.DEFAULT_PATCH);
    patcher.setPatch(patch);
    patch = patcher.getPatch();
    ws.clear();
    if (doc && doc.workspace) Blockly.serialization.workspaces.load(doc.workspace, ws);
    if (opts && opts.arrange) arrange();
    nameInput.value = (opts && opts.name !== undefined) ? opts.name : (doc && doc.name) || '';
    regenerate();
    clean = (opts && opts.dirty) ? '' : snapshot();
    if (!(opts && opts.arrange)) ws.scrollCenter();
    if (previewWanted) startPreview();
    guide.update();
  }

  // ---- maiden's file api
  async function listing(path) {
    const r = await fetch(API + path);
    if (r.status === 404) return null;
    if (!r.ok) throw new Error('maiden: ' + path + ': ' + r.status);
    return r.json();
  }
  async function put(path, text) {
    const form = new FormData();
    form.append('value', new Blob([text], {type: 'text/utf-8'}));
    const r = await fetch(API + path, {method: 'PUT', body: form});
    if (!r.ok) throw new Error('maiden could not write ' + path + ' (' + r.status + ')');
  }

  async function save() {
    const name = scriptName();
    if (!name) {
      status('give the script a name first', 'error');
      nameInput.focus();
      return false;
    }
    nameInput.value = name;
    regenerate();
    const dir = 'code/' + name;
    const blocksFile = name + '.blocks.json';
    try {
      const d = await listing(dir);
      if (d) {
        // a folder of the same name the builder did not make: never write into it
        const ours = d.entries.some(e => e.name === blocksFile);
        if (!ours && d.entries.length) {
          status(dir + ' is a script that was not made here: choose another name', 'error');
          return false;
        }
      } else {
        const r = await fetch(API + dir + '?kind=directory', {method: 'PUT'});
        if (!r.ok) throw new Error('maiden could not make ' + dir + ' (' + r.status + ')');
      }
      await put(dir + '/' + name + '.lua', code);
      await put(dir + '/' + blocksFile, JSON.stringify(documentJson(), null, 1) + '\n');
      markClean();
      status('saved ' + dir + '/' + name + '.lua', 'ok');
      guide.saved();
      return true;
    } catch (e) {
      status(e.message, 'error');
      return false;
    }
  }

  // ---- open: scripts made here, and the examples
  const EXAMPLES = [
    ['first_steps', 'demo.json', 'a melody, a control on E2, K2 to start and stop'],
    ['pulse', 'pulse.json', 'a ring breathing with the beat, a dial, a slider'],
    ['looper', 'looper.json', 'record a loop with K2 and K3, overdub, change its speed'],
    ['drums', 'drums.json', 'euclidean rhythms and a pattern on the kick patch, tempo on E2'],
    ['grid_keys', 'grid_keys.json', 'a grid keyboard for the sound, lit where it is held'],
  ];
  async function openDialog() {
    const list = $('open-list');
    list.textContent = '';
    const item = (label, kind, action) => {
      const li = document.createElement('li');
      const b = document.createElement('button');
      b.type = 'button';
      const l = document.createElement('span');
      l.textContent = label;
      const k = document.createElement('span');
      k.className = 'kind';
      k.textContent = kind;
      b.append(l, k);
      b.addEventListener('click', async () => {
        $('open-dialog').close();
        try { await action(); } catch (e) { status(e.message, 'error'); }
      });
      li.appendChild(b);
      list.appendChild(li);
    };
    for (const [name, file, about] of EXAMPLES) {
      item(name, 'example: ' + about, async () => {
        const doc = await (await fetch(file)).json();
        loadDocument(doc, {name: '', dirty: true, arrange: true});
        status('example ' + name + ': give it a name of your own to save it');
      });
    }
    $('open-dialog').showModal();
    try {
      const code = await listing('code');
      const dirs = (code ? code.entries : []).filter(e => e.children);
      const found = await Promise.all(dirs.map(async e => {
        const d = await listing('code/' + e.name).catch(() => null);
        return d && d.entries.some(f => f.name === e.name + '.blocks.json') ? e.name : null;
      }));
      for (const name of found.filter(Boolean).sort()) {
        item(name, 'code/' + name, async () => {
          const r = await fetch(API + 'code/' + name + '/' + name + '.blocks.json');
          const doc = await r.json();
          if (doc.format !== FORMAT) throw new Error(name + ': not a script builder file');
          loadDocument(doc, {name});
          status('opened code/' + name);
        });
      }
    } catch (e) {
      status('could not list the scripts: ' + e.message, 'error');
    }
  }

  // ---- matron (the norns lua): run, stop, its output, the live sound page
  let matron = null;
  let retry = 500;
  let running = null;  // the name of the script this page started, while it runs
  const liveName = () => (running && matron && running === scriptName()) ? running : null;
  function connect() {
    let sock;
    try {
      sock = new WebSocket('ws://' + (location.hostname || '127.0.0.1') + ':5555', ['bus.sp.nanomsg.org']);
    } catch (e) {
      return setTimeout(connect, retry);
    }
    sock.onopen = () => { matron = sock; retry = 500; $('link').className = 'link on'; };
    sock.onclose = () => {
      matron = null;
      running = null;
      patcher.setLive(false);
      $('link').className = 'link off';
      setTimeout(connect, retry);
      retry = Math.min(retry * 2, 5000);
    };
    sock.onmessage = async e => {
      const text = typeof e.data === 'string' ? e.data : await e.data.text();
      for (const l of text.replace(/\n$/, '').split('\n')) {
        if (/^<ok>$/.test(l.trim())) continue;
        consoleLine(l, /error|traceback/i.test(l) ? 'error' : '');
      }
    };
  }
  function send(cmd, quiet) {
    if (!matron) {
      if (!quiet) status('norns is not reachable: is Verdandi running?', 'error');
      return false;
    }
    matron.send(cmd + '\n');
    return true;
  }
  async function run() {
    if (!(await save())) return;
    const name = scriptName();
    if (send('norns.script.load("code/' + name + '/' + name + '.lua")')) {
      running = name;
      patcher.setLive(true);
      showTab('console');
      consoleLine('-- run code/' + name + '/' + name + '.lua', 'info');
      status('playing ' + name + ' in Verdandi: the sound page plays live now', 'ok');
      guide.ran();
    }
  }
  let rerunTimer = null;
  function rerunSoon() {
    clearTimeout(rerunTimer);
    rerunTimer = setTimeout(() => { if (liveName()) run(); }, 700);
  }
  function stopScript() {
    if (send('norns.script.clear()')) {
      running = null;
      patcher.setLive(false);
      status('stopped');
    }
  }

  // ---- the preview
  let previewWanted = true;
  const notes = $('notes');
  let noteTimer = null;
  const noteName = hz => {
    const n = Math.round(69 + 12 * Math.log2(hz / 440));
    return ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'][((n % 12) + 12) % 12] + (Math.floor(n / 12) - 1);
  };
  const flash = text => {
    notes.textContent = text;
    clearTimeout(noteTimer);
    noteTimer = setTimeout(() => { notes.textContent = ''; }, 600);
  };
  // the grid of the preview: 16 x 8 keys, lit as the script lights them
  const gridCanvas = $('grid');
  const gridLeds = new Array(128).fill(0);
  let gridShown = new Array(128).fill(0);
  function drawGrid() {
    const g = gridCanvas.getContext('2d');
    g.fillStyle = '#0b0b0b';
    g.fillRect(0, 0, gridCanvas.width, gridCanvas.height);
    for (let y = 0; y < 8; y++) {
      for (let x = 0; x < 16; x++) {
        const l = gridShown[y * 16 + x];
        g.fillStyle = l ? 'rgb(' + (60 + l * 13) + ',' + (45 + l * 11) + ',' + (20 + l * 5) + ')' : '#222';
        g.fillRect(1 + x * 20, 1 + y * 20, 18, 18);
      }
    }
  }
  const preview = new VerdandiPreview($('screen'), {
    print: text => consoleLine('[preview] ' + text),
    error: text => {
      $('preview-state').textContent = 'stopped by an error';
      $('preview-state').className = 'error';
      consoleLine('[preview] ' + text, 'error');
      status('preview: ' + text, 'error');
    },
    engine: (cmd, hz) => { if (cmd === 'note_on' && hz) flash('♪ ' + noteName(hz) + '  (' + Math.round(hz) + ' Hz)'); },
    midi: (type, note) => { if (type === 'note_on') flash('MIDI ' + note); },
    param: () => {},
    grid_used: () => { gridCanvas.hidden = false; drawGrid(); },
    grid_led: (x, y, l) => { if (x >= 1 && x <= 16 && y >= 1 && y <= 8) gridLeds[(y - 1) * 16 + (x - 1)] = l; },
    grid_all: l => gridLeds.fill(l),
    grid_refresh: () => { gridShown = gridLeds.slice(); drawGrid(); },
  }, 'lib/');
  async function startPreview() {
    gridCanvas.hidden = true;
    gridLeds.fill(0);
    gridShown.fill(0);
    const ok = await preview.start(code);
    if (ok) {
      $('preview-state').textContent = 'running (no sound: run plays it in Verdandi)';
      $('preview-state').className = 'running';
    }
  }
  function stopPreview() {
    preview.stop();
    $('preview-state').textContent = 'off';
    $('preview-state').className = '';
  }
  $('preview-toggle').addEventListener('click', () => {
    previewWanted = !preview.running();
    previewWanted ? startPreview() : stopPreview();
  });
  for (const b of document.querySelectorAll('[data-key]')) {
    const n = Number(b.dataset.key);
    b.addEventListener('pointerdown', () => preview.key(n, 1));
    b.addEventListener('pointerup', () => preview.key(n, 0));
    b.addEventListener('pointerleave', e => { if (e.buttons) preview.key(n, 0); });
  }
  for (const e of document.querySelectorAll('[data-enc]')) {
    const n = Number(e.dataset.enc);
    for (const b of e.querySelectorAll('button')) b.addEventListener('click', () => preview.enc(n, Number(b.dataset.d)));
    let acc = 0;
    e.addEventListener('wheel', ev => {
      ev.preventDefault();
      acc += ev.deltaY;
      const steps = Math.trunc(acc / 20);
      if (steps) { acc -= steps * 20; preview.enc(n, -steps); }
    }, {passive: false});
  }
  let gridHeld = null;
  const gridKeyAt = ev => {
    const r = gridCanvas.getBoundingClientRect();
    const x = Math.floor((ev.clientX - r.left) / r.width * 16) + 1, y = Math.floor((ev.clientY - r.top) / r.height * 8) + 1;
    return (x >= 1 && x <= 16 && y >= 1 && y <= 8) ? [x, y] : null;
  };
  gridCanvas.addEventListener('pointerdown', ev => {
    gridHeld = gridKeyAt(ev);
    if (gridHeld) preview.grid(gridHeld[0], gridHeld[1], 1);
  });
  const gridUp = () => { if (gridHeld) preview.grid(gridHeld[0], gridHeld[1], 0); gridHeld = null; };
  gridCanvas.addEventListener('pointerup', gridUp);
  gridCanvas.addEventListener('pointerleave', gridUp);

  // ---- tabs, buttons
  function showTab(which) {
    for (const t of document.querySelectorAll('[data-tab]')) t.setAttribute('aria-selected', String(t.dataset.tab === which));
    $('lua').hidden = which !== 'lua';
    $('console').hidden = which !== 'console';
  }
  for (const t of document.querySelectorAll('[data-tab]')) t.addEventListener('click', () => showTab(t.dataset.tab));
  $('copy').addEventListener('click', async () => {
    try { await navigator.clipboard.writeText(code); status('Lua copied'); } catch (e) { status('could not copy', 'error'); }
  });
  function newDocument() {
    loadDocument(null, {name: ''});
    status('empty workspace: drag an event from "Events" to begin');
  }
  $('new').addEventListener('click', () => {
    if (isDirty() && !confirm('leave the unsaved blocks?')) return;
    newDocument();
  });
  $('open').addEventListener('click', () => {
    if (isDirty() && !confirm('leave the unsaved blocks?')) return;
    openDialog();
  });
  $('save').addEventListener('click', save);
  $('run').addEventListener('click', run);
  $('stop').addEventListener('click', stopScript);
  document.addEventListener('keydown', e => {
    if ((e.metaKey || e.ctrlKey) && e.key === 's') { e.preventDefault(); save(); }
    if ((e.metaKey || e.ctrlKey) && e.key === 'Enter') { e.preventDefault(); run(); }
  });

  // ---- the guide
  const guide = new VerdandiGuide($('guide'), {
    workspace: ws,
    getPatch: () => patch,
    getView: () => view,
    showView,
    newDocument: () => {
      if (isDirty() && !confirm('the guide starts with an empty workspace: leave the unsaved blocks?')) return false;
      newDocument();
      return true;
    },
    status,
  });
  $('guide-open').addEventListener('click', () => guide.toggle());

  // ---- start: an example the address names (?example=pulse), the draft of last time, or the
  // first example (with the guide open, the first time)
  const query = new URLSearchParams(location.search);
  const wanted = EXAMPLES.find(x => x[0] === query.get('example'));
  let draft = null;
  try { draft = JSON.parse(localStorage.getItem(DRAFT) || 'null'); } catch (e) { draft = null; }
  let started = false;
  if (wanted) {
    loadDocument(await (await fetch(wanted[1])).json(), {name: '', dirty: true, arrange: true});
    status('example ' + wanted[0] + ': give it a name of your own to save it');
    started = true;
  } else if (draft && draft.format === FORMAT) {
    try {
      loadDocument(draft, {dirty: true});
      status('your blocks from last time');
      started = true;
    } catch (e) {
      started = false;
    }
  }
  if (!started) {
    loadDocument(await (await fetch('demo.json')).json(), {name: '', dirty: false, arrange: true});
    status('the example first_steps: change it, name it, run it, or open the guide');
    if (!guide.seen()) guide.open();
  }
  if (query.get('view') === 'sound') showView('sound');
  if (query.has('guide')) guide.open(Number(query.get('guide')) || 0, Number(query.get('step')) || 0);
  connect();
})();
