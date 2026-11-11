// record.js: records the script builder video: Chrome builds "glow" in the builder (real pointer
// and keys through the DevTools protocol, with a visible cursor), then runs it in Verdandi; the
// Verdandi window is filmed and norns records its own output on TAPE (the sound muted at the
// speakers meanwhile). writes <out>/draft.mp4 (854 x 480), <out>/scenes.json (when each scene
// starts, for the narration) and <out>/verdandi.png (a still for the manual).
//
//   node macos/manual/video/record.js <out dir> [--full]     (Verdandi running, maiden on :5000)
//
// it loads glow into the running Verdandi and puts back the script that ran before, and the
// output level, when it ends. the page frames go straight into ffmpeg (no image files): keep
// <out dir> on a disk with room (the Verdandi window frames are files, a few hundred MB).
'use strict';
const fs = require('fs');
const path = require('path');
const {spawn, execFileSync, execFile} = require('child_process');
const run = (cmd, args) => new Promise((ok, bad) => execFile(cmd, args, e => e ? bad(e) : ok()));

const OUT = path.resolve(process.argv[2] || 'video-out');
const FULL = process.argv.includes('--full');
const CHROME = '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const W = 1440, H = 860;
const PORT = 9333;
// the window frames of an earlier take would slip into this one: they go first
fs.rmSync(path.join(OUT, 'window'), {recursive: true, force: true});
fs.mkdirSync(path.join(OUT, 'window'), {recursive: true});
for (const f of fs.readdirSync(OUT)) if (f.startsWith('profile-')) fs.rmSync(path.join(OUT, f), {recursive: true, force: true});
const sleep = ms => new Promise(r => setTimeout(r, ms));
const now = () => Date.now() / 1000;
const scenes = [];
const scene = name => { scenes.push({name, t: now()}); console.log('scene', name); };

// ---- matron, the norns lua (a websocket, like maiden's REPL)
let matron, matronOut = '';
async function matronConnect() {
  matron = new WebSocket('ws://127.0.0.1:5555', ['bus.sp.nanomsg.org']);
  await new Promise((ok, bad) => { matron.onopen = ok; matron.onerror = () => bad(new Error('Verdandi (matron :5555) is not running')); });
  matron.onmessage = async e => { matronOut += typeof e.data === 'string' ? e.data : await e.data.text(); };
}
async function lua(code, wait = 300) {
  const mark = '__' + Math.random().toString(36).slice(2);
  matronOut = '';
  matron.send(code + '\n');
  matron.send('print("' + mark + '")\n');
  for (let i = 0; i < 40 && !matronOut.includes(mark); i++) await sleep(50);
  await sleep(wait);
  return matronOut.split(mark)[0];
}

// ---- Chrome and the DevTools protocol
let cdp, seq = 0;
const pending = new Map(), handlers = [];
function send(method, params = {}) {
  const id = ++seq;
  cdp.send(JSON.stringify({id, method, params}));
  return new Promise((ok, bad) => pending.set(id, {ok, bad, method}));
}
async function chrome() {
  const profile = path.join(OUT, 'profile-' + Date.now());
  const proc = spawn(CHROME, ['--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run',
    '--remote-debugging-port=' + PORT, '--user-data-dir=' + profile, '--window-size=' + W + ',' + H, 'about:blank'],
    {stdio: 'ignore'});
  let target;
  for (let i = 0; i < 100 && !target; i++) {
    await sleep(100);
    try { target = (await (await fetch('http://127.0.0.1:' + PORT + '/json')).json()).find(t => t.type === 'page'); } catch (e) { /* not yet */ }
  }
  cdp = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise(ok => { cdp.onopen = ok; });
  cdp.onmessage = e => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) {
      const p = pending.get(m.id);
      pending.delete(m.id);
      m.error ? p.bad(new Error(p.method + ': ' + m.error.message)) : p.ok(m.result);
    } else if (m.method) for (const h of handlers) h(m);
  };
  await send('Page.enable');
  await send('Runtime.enable');
  await send('Emulation.setDeviceMetricsOverride', {width: W, height: H, deviceScaleFactor: 1, mobile: false});
  // a cursor you can see, and a ring on each click
  await send('Page.addScriptToEvaluateOnNewDocument', {source: `
    addEventListener('DOMContentLoaded', () => {
      const c = document.createElement('div');
      c.style.cssText = 'position:fixed;left:0;top:0;width:22px;height:22px;z-index:2147483647;pointer-events:none;transform:translate(-3px,-2px);transition:none';
      c.innerHTML = '<svg width="22" height="22" viewBox="0 0 22 22"><path d="M3 2 L3 18 L7.5 13.8 L10.6 20.4 L13.2 19.2 L10.2 12.8 L16.2 12.6 Z" fill="#fff" stroke="#000" stroke-width="1.4" stroke-linejoin="round"/></svg>';
      document.documentElement.appendChild(c);
      const move = e => { c.style.left = e.clientX + 'px'; c.style.top = e.clientY + 'px'; };
      addEventListener('pointermove', move, true);
      addEventListener('pointerdown', e => {
        move(e);
        const r = document.createElement('div');
        r.style.cssText = 'position:fixed;width:30px;height:30px;border-radius:50%;border:2px solid #e0913f;z-index:2147483646;pointer-events:none;' +
          'left:' + (e.clientX - 15) + 'px;top:' + (e.clientY - 15) + 'px;transition:transform .45s ease-out,opacity .45s ease-out';
        document.documentElement.appendChild(r);
        requestAnimationFrame(() => { r.style.transform = 'scale(1.8)'; r.style.opacity = '0'; });
        setTimeout(() => r.remove(), 600);
      }, true);
    });`});
  handlers.push(m => {
    if (m.method === 'Page.javascriptDialogOpening') send('Page.handleJavaScriptDialog', {accept: true});
  });
  return proc;
}
// the screencast: every frame Chrome draws goes into ffmpeg, stamped with the time it arrived
let firstFrame = null, frameCount = 0, encoder = null;
let filming = false, casting = false;
function startEncoder() {
  encoder = spawn('ffmpeg', ['-loglevel', 'error', '-y', '-f', 'image2pipe', '-use_wallclock_as_timestamps', '1',
    '-c:v', 'mjpeg', '-i', '-', '-fps_mode', 'vfr', '-c:v', 'libx264', '-preset', 'fast', '-crf', '18',
    '-pix_fmt', 'yuv420p', path.join(OUT, 'page.mp4')], {stdio: ['pipe', 'inherit', 'inherit']});
}
async function film(on) {
  // the screencast starts once; after that, on/off only says whether frames are kept
  if (on && casting) { filming = true; return; }
  if (on && !filming) {
    casting = true;
    handlers.push(m => {
      if (m.method !== 'Page.screencastFrame') return;
      send('Page.screencastFrameAck', {sessionId: m.params.sessionId});
      if (!filming) return;
      if (firstFrame === null) firstFrame = now();
      frameCount++;
      encoder.stdin.write(Buffer.from(m.params.data, 'base64'));
    });
    filming = true;
    await send('Page.startScreencast', {format: 'jpeg', quality: 85, maxWidth: W, maxHeight: H, everyNthFrame: 1});
  } else if (!on && filming) {
    filming = false;
  }
}
async function js(expr) {
  const r = await send('Runtime.evaluate', {expression: expr, awaitPromise: true, returnByValue: true});
  if (r.exceptionDetails) throw new Error('page: ' + (r.exceptionDetails.exception && r.exceptionDetails.exception.description || r.exceptionDetails.text) + '\n' + expr.slice(0, 200));
  return r.result.value;
}

// ---- a hand on the mouse
let mx = W / 2, my = H / 2;
const ease = t => t < 0.5 ? 2 * t * t : 1 - Math.pow(-2 * t + 2, 2) / 2;
async function mouse(type, x, y, extra = {}) {
  await send('Input.dispatchMouseEvent', Object.assign({type, x, y, button: 'left', pointerType: 'mouse'}, extra));
}
async function moveTo(x, y, ms = 500, buttons = 0) {
  const steps = Math.max(2, Math.round(ms / 16)), x0 = mx, y0 = my;
  for (let i = 1; i <= steps; i++) {
    const k = ease(i / steps);
    mx = x0 + (x - x0) * k;
    my = y0 + (y - y0) * k;
    await mouse('mouseMoved', mx, my, {buttons});
    await sleep(16);
  }
}
async function click(x, y, ms = 500) {
  await moveTo(x, y, ms);
  await sleep(120);
  await mouse('mousePressed', x, y, {clickCount: 1, buttons: 1});
  await sleep(90);
  await mouse('mouseReleased', x, y, {clickCount: 1});
  await sleep(250);
}
async function drag(x1, y1, x2, y2, ms = 900) {
  await moveTo(x1, y1, 450);
  await sleep(150);
  await mouse('mousePressed', x1, y1, {clickCount: 1, buttons: 1});
  await sleep(120);
  // a small first move: Blockly starts a drag only past a few pixels
  await moveTo(x1 + 8, y1 + 6, 80, 1);
  await moveTo(x2, y2, ms, 1);
  await sleep(200);
  await mouse('mouseReleased', x2, y2, {clickCount: 1});
  await sleep(350);
}
async function type(text, ms = 70) {
  for (const ch of text) {
    await send('Input.insertText', {text: ch});
    await sleep(ms);
  }
}
async function key(k, code, keyCode, modifiers = 0) {
  await send('Input.dispatchKeyEvent', {type: 'keyDown', key: k, code, windowsVirtualKeyCode: keyCode, modifiers});
  await send('Input.dispatchKeyEvent', {type: 'keyUp', key: k, code, windowsVirtualKeyCode: keyCode, modifiers});
  await sleep(80);
}
// select what is in the focused field (cmd+A through the protocol selects nothing on macOS)
const selectAll = () => js('document.activeElement && document.activeElement.select && document.activeElement.select()');
const enter = () => key('Enter', 'Enter', 13);
const center = r => [r.x + r.w / 2, r.y + r.h / 2];
async function rect(expr) {
  const r = await js('(() => { const e = ' + expr + '; if (!e) return null; const b = e.getBoundingClientRect(); return {x: b.left, y: b.top, w: b.width, h: b.height}; })()');
  if (!r) throw new Error('not on the page: ' + expr);
  return r;
}
async function clickEl(expr, ms) { const [x, y] = center(await rect(expr)); await click(x, y, ms); }

// ---- Blockly: the toolbox, the flyout, connections in screen pixels
const WS = 'Blockly.getMainWorkspace()';
async function category(name) {
  await clickEl(`[...document.querySelectorAll('.blocklyToolbox *, .blocklyToolboxDiv *')].find(e => !e.children.length && e.textContent.trim() === ${JSON.stringify(name)})`);
  await sleep(500);
}
// where the n-th flyout block of a type is, and where to hold it
async function flyoutBlock(type, n = 0) {
  return js(`(() => {
    const fw = ${WS}.getFlyout().getWorkspace();
    const b = fw.getTopBlocks(true).filter(b => b.type === ${JSON.stringify(type)})[${n}];
    if (!b) return null;
    const r = b.getSvgRoot().getBoundingClientRect();
    const scale = ${WS}.scale;
    const prev = b.previousConnection || b.outputConnection;
    const off = prev ? prev.getOffsetInBlock() : {x: 0, y: 0};
    return {x: r.left, y: r.top, w: r.width, h: r.height, gx: r.left + 14, gy: r.top + Math.min(18, r.height / 2), offx: off.x * scale, offy: off.y * scale, hat: !prev};
  })()`);
}
// the screen point of a connection: an input of a block in the workspace, or the one below it
async function connectionPoint(blockExpr, input) {
  return js(`(() => {
    const b = ${blockExpr};
    const c = ${input ? `b.getInput(${JSON.stringify(input)}).connection` : 'b.nextConnection'};
    const p = Blockly.utils.svgMath.wsToScreenCoordinates(${WS}, new Blockly.utils.Coordinate(c.x, c.y));
    return {x: p.x, y: p.y};
  })()`);
}
const blockOf = (type, n = 0) => `${WS}.getAllBlocks(false).filter(b => b.type === ${JSON.stringify(type)})[${n}]`;
// drag a block of the open flyout to a free spot, or onto a connection
async function place(type, to, n = 0) {
  const f = await flyoutBlock(type, n);
  if (!f) throw new Error('no ' + type + ' in the flyout');
  let tx, ty;
  if (to.at) [tx, ty] = to.at;
  else {
    const c = await connectionPoint(to.block, to.input);
    tx = c.x + (f.gx - f.x) - f.offx;
    ty = c.y + (f.gy - f.y) - f.offy;
  }
  await drag(f.gx, f.gy, tx, ty, 900);
  await sleep(300);
}
async function attached(childType, parentType) {
  return js(`${WS}.getAllBlocks(false).some(b => b.type === ${JSON.stringify(childType)} && b.getSurroundParent() && (${JSON.stringify(parentType)} === '' || (() => { let p = b.getSurroundParent(); while (p) { if (p.type === ${JSON.stringify(parentType)}) return true; p = p.getSurroundParent(); } return false; })()))`);
}
async function must(ok, what) { if (!(await ok)) throw new Error('did not work: ' + what); }
async function editField(blockExpr, field, text) {
  const [x, y] = center(await rect(`${blockExpr}.getField(${JSON.stringify(field)}).getSvgRoot()`));
  await click(x, y, 400);
  await sleep(200);
  await selectAll();
  await type(text, 90);
  await enter();
  await sleep(300);
}
async function chooseDropdown(blockExpr, field, label) {
  const [x, y] = center(await rect(`${blockExpr}.getField(${JSON.stringify(field)}).getSvgRoot()`));
  await click(x, y, 400);
  await sleep(400);
  const item = `[...document.querySelectorAll('.blocklyMenuItem')].find(e => e.textContent.trim() === ${JSON.stringify(label)})`;
  await js(`${item}.scrollIntoView({block: 'center'})`);
  await sleep(300);
  await clickEl(item, 400);
}

// ---- the window of Verdandi
function windowId() {
  const src = path.join(OUT, 'winid.swift');
  fs.writeFileSync(src, `import CoreGraphics
let list = CGWindowListCopyWindowInfo([.optionAll], kCGNullWindowID) as! [[String: Any]]
for w in list where (w[kCGWindowName as String] as? String) == "Verdandi" && (w[kCGWindowLayer as String] as? Int) == 0 {
  print(w[kCGWindowNumber as String]!); break
}`);
  return String(execFileSync('swift', [src])).trim();
}
const windowFrames = [];
let filmingWindow = false;
// frames until filmingWindow goes off; asynchronous, so the scene goes on meanwhile (a blocking
// loop here once froze the picture while E2 turned)
async function filmWindow(id) {
  filmingWindow = true;
  while (filmingWindow) {
    const file = path.join(OUT, 'window', String(windowFrames.length).padStart(5, '0') + '.jpg');
    const t = now();
    await run('screencapture', ['-l' + id, '-x', '-o', '-t', 'jpg', file]);
    windowFrames.push({file, t});
  }
}


// the film: the page up to "run", the Verdandi window while it plays, the page again (live);
// real time throughout, so the tape lies under it where it was recorded
function assemble() {
  const take = JSON.parse(fs.readFileSync(path.join(OUT, 'take.json')));
  const f0 = take.firstFrame, wins = take.windowFrames;
  const at = name => take.scenes.find(x => x.name === name).t - f0;
  const pageA = wins[0] - f0, pageB = at('live');
  const winRate = (wins.length - 1) / (wins[wins.length - 1] - wins[0]);
  const tapeFile = path.join(OUT, 'glow-video.wav');
  const hasTape = fs.existsSync(tapeFile);
  // (constant 30 fps first: a frame before a gap lasts the whole gap; the window holds its last frame)
  const filter = '[0:v]setpts=PTS-STARTPTS,fps=30,split=2[p1][p2];[p1]trim=0:' + pageA.toFixed(3) + ',setpts=PTS-STARTPTS[a];' +
    '[1:v]scale=-2:' + H + ',pad=' + W + ':' + H + ':(ow-iw)/2:0:color=0x151515,setsar=1,fps=30,tpad=stop_mode=clone:stop_duration=10,trim=0:' +
    (pageB - pageA).toFixed(3) + ',setpts=PTS-STARTPTS[w];' +
    '[p2]trim=' + pageB.toFixed(3) + ',setpts=PTS-STARTPTS[b];[a][w][b]concat=n=3:v=1:a=0,scale=' + (FULL ? W + ':' + H : '854:-2') + '[v]' +
    (hasTape ? ';[2:a]adelay=' + Math.max(0, Math.round((take.tapeAt - f0) * 1000)) + ':all=1,apad[au]' : '');
  const args = ['-loglevel', 'error', '-y', '-i', path.join(OUT, 'page.mp4'),
    '-framerate', winRate.toFixed(3), '-i', path.join(OUT, 'window', '%05d.jpg')];
  if (hasTape) args.push('-i', tapeFile);
  args.push('-filter_complex', filter, '-map', '[v]');
  if (hasTape) args.push('-map', '[au]', '-shortest', '-c:a', 'aac', '-b:a', '192k');
  args.push('-c:v', 'libx264', '-preset', 'medium', '-crf', FULL ? '18' : '24', '-pix_fmt', 'yuv420p',
    path.join(OUT, FULL ? 'full.mp4' : 'draft.mp4'));
  execFileSync('ffmpeg', args, {stdio: 'inherit'});
  fs.writeFileSync(path.join(OUT, 'scenes.json'), JSON.stringify({
    scenes: take.scenes.map(x => ({name: x.name, at: +Math.max(0, x.t - f0).toFixed(2)})),
    tape: hasTape ? +(take.tapeAt - f0).toFixed(2) : null}, null, 1));
}
if (process.argv.includes('--assemble')) { assemble(); process.exit(0); }

(async () => {
  await matronConnect();
  const before = (await lua('print("SCRIPT=" .. tostring(norns.state.script))')).match(/SCRIPT=(\S+)/);
  const previous = before && before[1] !== 'nil' && before[1] !== '' ? before[1] : null;
  console.log('running before:', previous);
  const win = windowId();
  const proc = await chrome();
  startEncoder();
  try {
    await send('Page.navigate', {url: 'http://127.0.0.1:5000/maiden/'});
    await sleep(2500);
    await film(true);
    const t0 = now();
    scene('maiden');
    await sleep(1500);
    await clickEl('document.getElementById("verdandi-blocks")', 900);
    await sleep(3500);

    scene('builder');
    // the guide opens the first time: close it; start from an empty page
    await js('document.querySelector("#guide .close") && document.querySelector("#guide .close").click()');
    await sleep(800);
    await clickEl('document.getElementById("new")');
    await sleep(1200);

    scene('control');
    await category('Events');
    await place('nv_init', {at: [260, 140]});
    await category('Controls');
    await place('nv_param_add', {block: blockOf('nv_init'), input: 'DO'});
    await must(attached('nv_param_add', 'nv_init'), 'add control into when the script starts');
    await editField(blockOf('nv_param_add'), 'MIN', '200');
    await editField(blockOf('nv_param_add'), 'MAX', '4000');
    await editField(blockOf('nv_param_add'), 'DEFAULT', '1200');

    scene('melody');
    await category('Events');
    await place('nv_every', {at: [260, 280]});
    await category('Sequencing');
    await place('nv_counter', {block: blockOf('nv_every'), input: 'DO'});
    await must(attached('nv_counter', 'nv_every'), 'count into every');
    await category('Sound');
    await place('nv_play_note', {block: blockOf('nv_counter'), input: null});
    await must(attached('nv_play_note', 'nv_every'), 'play note under count');
    await category('Sequencing');
    await place('nv_pattern_value', {block: blockOf('nv_play_note'), input: 'NOTE'});
    await must(js(`${blockOf('nv_play_note')}.getInputTargetBlock('NOTE').type === 'nv_pattern_value'`), 'the melody into play note');
    await sleep(2500);

    scene('encoder');
    await category('Events');
    await place('nv_enc', {at: [260, 470]});
    await category('Controls');
    await place('nv_param_change', {block: blockOf('nv_enc'), input: 'DO'});
    await must(attached('nv_param_change', 'nv_enc'), 'change control into the encoder');
    await category('Controls');
    await place('nv_param_changed', {at: [760, 140]});
    await category('Sound');
    await place('nv_sound_set', {block: blockOf('nv_param_changed'), input: 'DO'});
    await must(attached('nv_sound_set', 'nv_param_changed'), 'set sound into control changes');
    await chooseDropdown(blockOf('nv_sound_set'), 'KEY', 'filter1 cutoff');
    await category('Controls');
    await place('nv_param_value', {block: blockOf('nv_sound_set'), input: 'VALUE'});
    await sleep(1500);

    scene('sound');
    await clickEl('document.querySelector(\'[data-view="sound"]\')');
    await sleep(1500);
    await clickEl('document.querySelector(\'#patcher .vp-add[data-type="lfo"]\')');
    await sleep(900);
    const from = center(await rect('document.querySelector(\'#patcher .vp-jack[data-key="lfo1.out"]\')'));
    const to = center(await rect('document.querySelector(\'#patcher .vp-jack[data-key="filter1.mod"]\')'));
    await drag(from[0], from[1], to[0], to[1], 1100);
    await must(js('document.querySelectorAll("#patcher .vp-cables path").length > 0'), 'a cable');
    // the lfo slower and gentler: click its sliders where the value should be
    for (const [label, frac] of [['lfo1 rate', 0.39], ['lfo1 depth', 0.4]]) {
      const r = await rect(`document.querySelector('#patcher input[aria-label="${label}"]')`);
      await click(r.x + 7 + frac * (r.w - 14), r.y + r.h / 2, 500);
    }
    await clickEl('document.querySelector(\'#patcher .vp-addfx[data-type="fx_delay"]\')');
    await sleep(1500);

    scene('screen');
    await clickEl('document.querySelector(\'[data-view="screen"]\')');
    await sleep(1500);
    for (const t of ['stars', 'rings', 'text']) {
      await clickEl(`document.querySelector('#designer button[data-type="${t}"]')`, 500);
      await sleep(600);
    }
    // the new text is selected: show the note, centred, jumping on each note
    const field = '#designer [data-prop="text"] input';
    await clickEl(`document.querySelector('${field}')`);
    await selectAll();
    await type('{note}', 110);
    await js(`(() => { const s = document.querySelector('#designer [data-prop="align"] select'); s.value = 'centre'; s.dispatchEvent(new Event('change', {bubbles: true})); })()`);
    await sleep(500);
    for (const [prop, value] of [['x', '64'], ['y', '36']]) {
      await clickEl(`document.querySelector('#designer [data-prop="${prop}"] .vd-num')`, 450);
      await selectAll();
      await type(value, 120);
      await key('Tab', 'Tab', 9);
      await sleep(300);
    }
    await js(`(() => { const s = document.querySelector('#designer [data-prop="size"] select.vd-moves'); s.value = 'note'; s.dispatchEvent(new Event('change', {bubbles: true})); })()`);
    await sleep(600);
    for (let i = 0; i < 3; i++) { await clickEl('document.querySelector("#designer .vd-test")', 400); await sleep(700); }
    await clickEl('document.querySelector(\'#designer button[data-type="bar"]\')');
    await js(`(() => { const s = document.querySelector('#designer [data-prop="value"] select.vd-moves'); s.value = 'control'; s.dispatchEvent(new Event('change', {bubbles: true})); })()`);
    await sleep(1500);

    scene('run');
    await clickEl('document.getElementById("name")');
    await type('glow', 120);
    await key('Tab', 'Tab', 9);
    await lua('util.make_dir(_path.audio .. "tape")');
    await lua('audio.level_dac(0)');
    await lua('audio.tape_record_open(_path.audio .. "tape/glow-video.wav")', 400);
    await lua('audio.tape_record_start()', 50);
    const tapeAt = now();
    await clickEl('document.getElementById("run")');
    await sleep(800);
    await film(false);

    scene('verdandi');
    const filmed = filmWindow(win);
    await sleep(5000);
    for (let i = 0; i < 6; i++) { await lua('enc(2, 25)', 0); await sleep(700); }
    for (let i = 0; i < 6; i++) { await lua('enc(2, -20)', 0); await sleep(700); }
    await sleep(4000);
    filmingWindow = false;
    await filmed;
    execFileSync('sips', ['-s', 'format', 'png', windowFrames[Math.floor(windowFrames.length / 2)].file, '--out', path.join(OUT, 'verdandi.png')], {stdio: 'ignore'});

    scene('live');
    await film(true);
    await clickEl('document.querySelector(\'[data-view="sound"]\')');
    await sleep(800);
    // the keys say how far they are above the keyboard's lowest C (data-off)
    const keys = await js('[...document.querySelectorAll("#patcher .vp-keys [data-off]")].map(k => { const b = k.getBoundingClientRect(); return [+k.dataset.off, b.left + b.width / 2, b.top + b.height * (k.classList.contains("black") ? 0.5 : 0.8)]; })');
    const pick = n => keys.find(k => k[0] === n) || keys[0];
    for (const n of [7, 10, 14, 17]) {
      const [, x, y] = pick(n);
      await moveTo(x, y, 300);
      await mouse('mousePressed', x, y, {clickCount: 1, buttons: 1});
      await sleep(350);
      await mouse('mouseReleased', x, y, {clickCount: 1});
      await sleep(200);
    }
    const cut = await rect('document.querySelector(\'#patcher input[aria-label="filter1 cutoff"]\')');
    await drag(cut.x + cut.w * 0.55, cut.y + cut.h / 2, cut.x + cut.w * 0.85, cut.y + cut.h / 2, 1500);
    await drag(cut.x + cut.w * 0.85, cut.y + cut.h / 2, cut.x + cut.w * 0.6, cut.y + cut.h / 2, 1500);
    await sleep(1500);

    scene('end');
    await clickEl('document.getElementById("stop")');
    await sleep(1500);
    await film(false);
    await lua('audio.tape_record_stop()', 800);
    const tapeEnd = now();
    encoder.stdin.end();
    await new Promise(ok => encoder.on('close', ok));
    const tape = path.join('/Users/Shared/norns-dev/home/we/dust/audio/tape', 'glow-video.wav');
    if (fs.existsSync(tape)) execFileSync('mv', [tape, path.join(OUT, 'glow-video.wav')]);
    fs.writeFileSync(path.join(OUT, 'take.json'), JSON.stringify({firstFrame, tapeAt,
      scenes: scenes.map(x => ({name: x.name, t: x.t})), windowFrames: windowFrames.map(f => f.t)}));
    assemble();
    console.log('recorded: ' + frameCount + ' page frames, ' + windowFrames.length + ' window frames');
  } finally {
    // Verdandi as it was: the output level and the script that ran before
    try {
      await lua('audio.tape_record_stop()', 200);
      await lua('params:lookup_param("output_level"):bang()', 200);
      if (previous) await lua('norns.script.load("' + previous + '")', 500);
      else await lua('norns.script.clear()', 300);
    } catch (e) { console.error('restoring Verdandi:', e.message); }
    proc.kill();
  }
  process.exit(0);
})().catch(e => { console.error(e.stack || e.message); process.exit(1); });
