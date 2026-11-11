/* VerdandiPatcher: the visual patch editor of the Weave engine (plain JS, no libraries).
   usage: const p = new VerdandiPatcher(rootEl, catalog, {presets, onChange, onParam, onNote});
   p.setPatch(patch); p.getPatch(); p.setLive(bool); */
(function (global) {
  'use strict';

  var SVGNS = 'http://www.w3.org/2000/svg';
  var CARD_W = 210;
  var CANVAS_W = 2400, CANVAS_H = 1600;

  function h(tag, attrs, kids) {
    var el = document.createElement(tag);
    if (attrs) for (var k in attrs) {
      var v = attrs[k];
      if (v == null || v === false) continue;
      if (k === 'class') el.className = v;
      else if (k === 'text') el.textContent = v;
      else if (k.slice(0, 2) === 'on') el.addEventListener(k.slice(2), v);
      else el.setAttribute(k, v === true ? '' : v);
    }
    (kids || []).forEach(function (c) { if (c) el.appendChild(typeof c === 'string' ? document.createTextNode(c) : c); });
    return el;
  }
  function svg(tag, attrs) {
    var el = document.createElementNS(SVGNS, tag);
    for (var k in attrs || {}) el.setAttribute(k, attrs[k]);
    return el;
  }
  function clone(o) { return JSON.parse(JSON.stringify(o)); }
  function clamp(v, a, b) { return Math.max(a, Math.min(b, v)); }

  /* ---- param <-> slider maths ---- */
  function isExp(p) { return p.warp === 'exp' && p.min > 0; }
  function toPos(p, v) {
    if (isExp(p)) return Math.log(clamp(v, p.min, p.max) / p.min) / Math.log(p.max / p.min);
    return (v - p.min) / (p.max - p.min);
  }
  function fromPos(p, t) {
    t = clamp(t, 0, 1);
    var v = isExp(p) ? p.min * Math.pow(p.max / p.min, t) : p.min + t * (p.max - p.min);
    if (p.step) v = Math.round((v - p.min) / p.step) * p.step + p.min;
    else if (isExp(p)) v = Math.round(v * 1e4) / 1e4;
    else v = Math.round(v * 1e5) / 1e5;
    return clamp(v, p.min, p.max);
  }
  function fmt(p, v) {
    var s;
    if (p.step && p.step >= 1) s = String(Math.round(v));
    else {
      var a = isExp(p) ? Math.abs(v) : (p.max - p.min);
      s = v.toFixed(a >= 100 ? 0 : a >= 10 ? 1 : 2);
    }
    return p.units ? s + ' ' + p.units : s;
  }

  function VerdandiPatcher(root, catalog, opts) {
    var self = this;
    this.root = root;
    this.opts = opts || {};
    this.presets = this.opts.presets || [];
    this.types = {};
    this.voiceTypes = [];
    this.fxTypes = [];
    this.mainDef = null;
    (catalog.voice || []).forEach(function (t) { self.types[t.type] = t; self.voiceTypes.push(t); });
    (catalog.fx || []).forEach(function (t) {
      if (t.type === 'main') self.mainDef = t; else { self.types[t.type] = t; self.fxTypes.push(t); }
    });
    this.state = { enabled: true, modules: [], cables: [], fx: [], main: {} };
    this.dirty = false;
    this.selected = -1;      // selected cable index
    this.cardEls = {};       // module id -> element
    this.jackEls = {};       // "id.name" -> element
    this.octave = 0;         // 0 -> C3 = 48
    this.heldKeys = {};      // computer key -> note
    this.heldPtr = {};       // pointer id -> note
    this._build();
    this.setPatch(this._emptyPatch());
    this.dirty = false;
  }

  var P = VerdandiPatcher.prototype;

  /* ---------- catalog helpers ---------- */
  P._def = function (type) { return this.types[type]; };
  P._param = function (type, name) {
    var d = this.types[type];
    if (!d) return null;
    for (var i = 0; i < d.params.length; i++) if (d.params[i].name === name) return d.params[i];
    return null;
  };
  P._defaults = function (type) {
    var o = {};
    (this.types[type].params || []).forEach(function (p) { o[p.name] = p.default; });
    return o;
  };
  P._mod = function (id) {
    for (var i = 0; i < this.state.modules.length; i++) if (this.state.modules[i].id === id) return this.state.modules[i];
    return null;
  };
  P._fxById = function (id) {
    for (var i = 0; i < this.state.fx.length; i++) if (this.state.fx[i].id === id) return this.state.fx[i];
    return null;
  };
  P._newId = function (type) {
    var base = type.replace(/^fx_/, ''), n = 1, self = this;
    while (self._mod(base + n) || self._fxById(base + n)) n++;
    return base + n;
  };
  function split(key) { var i = key.indexOf('.'); return i < 0 ? [key, ''] : [key.slice(0, i), key.slice(i + 1)]; }
  P._hasOutput = function (key) {
    var s = split(key), m = this._mod(s[0]);
    return !!(m && this.types[m.type].outputs.some(function (o) { return o.name === s[1]; }));
  };
  P._hasInput = function (key) {
    var s = split(key), m = this._mod(s[0]);
    return !!(m && this.types[m.type].inputs.some(function (o) { return o.name === s[1]; }));
  };

  P._emptyPatch = function () {
    return { enabled: true, modules: [], cables: [], fx: [], main: {} };
  };

  /* ---------- state: patch in / out ---------- */
  P.setPatch = function (patch) {
    var self = this;
    patch = patch || {};
    var st = { enabled: patch.enabled !== false, modules: [], cables: [], fx: [], main: {} };
    var seen = {};
    (patch.modules || []).forEach(function (m) {
      if (!m || !self.types[m.type] || self.types[m.type].group === 'fx') {
        console.warn('patcher: dropped module of unknown type', m && m.type); return;
      }
      if (seen[m.id]) { console.warn('patcher: dropped duplicate module id', m.id); return; }
      var def = self.types[m.type];
      if (def.group === 'fixed' && m.id !== m.type) m = Object.assign({}, m, { id: m.type });
      if (seen[m.id]) return;
      seen[m.id] = true;
      var params = Object.assign(self._defaults(m.type), {});
      Object.keys(m.params || {}).forEach(function (k) {
        if (self._param(m.type, k)) params[k] = m.params[k]; else console.warn('patcher: unknown param', m.id + '.' + k);
      });
      st.modules.push({ id: m.id, type: m.type, x: typeof m.x === 'number' ? m.x : null, y: typeof m.y === 'number' ? m.y : null, params: params });
    });
    this.state = st;
    // fixed modules always exist once
    this.voiceTypes.forEach(function (t) {
      if (t.group === 'fixed' && !self._mod(t.type)) {
        st.modules.push({ id: t.type, type: t.type, x: t.type === 'voice' ? 20 : 700, y: 40, params: self._defaults(t.type) });
      }
    });
    // auto layout for modules without a position
    var col = 0, row = 0;
    st.modules.forEach(function (m) {
      if (m.x == null || m.y == null) {
        m.x = 20 + (col % 5) * (CARD_W + 30); m.y = 30 + Math.floor(col / 5) * 300; col++;
      }
    });
    (patch.cables || []).forEach(function (c) {
      if (!c || typeof c.from !== 'string' || typeof c.to !== 'string') { console.warn('patcher: bad cable', c); return; }
      var r = self._tryConnect(c.from, c.to, true);
      if (!r.ok) console.warn('patcher: dropped cable ' + c.from + ' -> ' + c.to + ': ' + r.reason);
    });
    (patch.fx || []).forEach(function (f) {
      var d = f && self.types[f.type];
      if (!d || d.group !== 'fx') { console.warn('patcher: dropped effect of unknown type', f && f.type); return; }
      var id = f.id && !self._fxById(f.id) && !self._mod(f.id) ? f.id : self._newId(f.type);
      var params = self._defaults(f.type);
      Object.keys(f.params || {}).forEach(function (k) { if (self._param(f.type, k)) params[k] = f.params[k]; });
      st.fx.push({ id: id, type: f.type, params: params });
    });
    var main = {};
    if (this.mainDef) main = (function () { var o = {}; self.mainDef.params.forEach(function (p) { o[p.name] = p.default; }); return o; })();
    Object.keys(patch.main || {}).forEach(function (k) { if (k in main) main[k] = patch.main[k]; });
    st.main = main;
    this.selected = -1;
    this.dirty = false;
    this._say('');
    this._render();
  };

  P.getPatch = function () {
    var s = this.state;
    return clone({
      enabled: s.enabled,
      modules: s.modules.map(function (m) { return { id: m.id, type: m.type, x: Math.round(m.x), y: Math.round(m.y), params: m.params }; }),
      cables: s.cables.map(function (c) { return { from: c.from, to: c.to }; }),
      fx: s.fx.map(function (f) { return { id: f.id, type: f.type, params: f.params }; }),
      main: s.main
    });
  };

  P.setLive = function (on) {
    this.live = !!on;
    this.liveEl.classList.toggle('on', this.live);
    this.liveEl.textContent = this.live ? 'live' : 'not live';
    this.liveEl.title = this.live ? 'edits are heard now' : 'edits are not sent to the engine';
  };

  P._changed = function (structural) {
    this.dirty = true;
    if (this.opts.onChange) this.opts.onChange(this.getPatch(), !!structural);
  };

  /* ---------- editing operations (same paths as the UI) ---------- */
  P.addModule = function (type) {
    var def = this.types[type];
    if (!def || def.group === 'fixed' || def.group === 'fx') { this._say('cannot add ' + type); return null; }
    var pos = this._freeSpot(def);
    var m = { id: this._newId(type), type: type, x: pos.x, y: pos.y, params: this._defaults(type) };
    this.state.modules.push(m);
    this._render();
    this._changed(true);
    return m.id;
  };
  P.removeModule = function (id) {
    var m = this._mod(id);
    if (!m || this.types[m.type].group === 'fixed') return false;
    this.state.modules = this.state.modules.filter(function (x) { return x !== m; });
    this.state.cables = this.state.cables.filter(function (c) { return split(c.from)[0] !== id && split(c.to)[0] !== id; });
    this.selected = -1;
    this._render();
    this._changed(true);
    return true;
  };
  P.addFx = function (type) {
    var d = this.types[type];
    if (!d || d.group !== 'fx') return null;
    var f = { id: this._newId(type), type: type, params: this._defaults(type) };
    this.state.fx.push(f);
    this._renderRack();
    this._changed(true);
    return f.id;
  };
  P.removeFx = function (id) {
    var n = this.state.fx.length;
    this.state.fx = this.state.fx.filter(function (f) { return f.id !== id; });
    if (this.state.fx.length === n) return false;
    this._renderRack(); this._changed(true); return true;
  };
  P.moveFx = function (id, dir) {
    var fx = this.state.fx, i = -1;
    for (var k = 0; k < fx.length; k++) if (fx[k].id === id) i = k;
    var j = i + dir;
    if (i < 0 || j < 0 || j >= fx.length) return false;
    var t = fx[i]; fx[i] = fx[j]; fx[j] = t;
    this._renderRack(); this._changed(true); return true;
  };

  /* would from -> to close a loop? returns the loop as module ids, or null */
  P._loopPath = function (fromMod, toMod) {
    if (fromMod === toMod) return [fromMod, fromMod];
    var next = {}, prev = {}, queue = [toMod], seen = {};
    seen[toMod] = true;
    this.state.cables.forEach(function (c) { var a = split(c.from)[0], b = split(c.to)[0]; (next[a] = next[a] || []).push(b); });
    while (queue.length) {
      var cur = queue.shift();
      if (cur === fromMod) {
        var path = [cur];
        while (prev[path[0]]) path.unshift(prev[path[0]]);
        return path;
      }
      (next[cur] || []).forEach(function (n) { if (!seen[n]) { seen[n] = true; prev[n] = cur; queue.push(n); } });
    }
    return null;
  };

  /* check a cable; {ok, reason, from, to}. a and b may come in either order (output/input) */
  P._check = function (a, b) {
    if (this._hasInput(a) && this._hasOutput(b)) { var t = a; a = b; b = t; }
    if (!this._hasOutput(a)) return { ok: false, reason: a + ' is not an output' };
    if (!this._hasInput(b)) return { ok: false, reason: b + ' is not an input' };
    if (this.state.cables.some(function (c) { return c.from === a && c.to === b; })) return { ok: false, reason: 'already connected' };
    var fm = split(a)[0], tm = split(b)[0];
    if (fm === tm) return { ok: false, reason: 'a module cannot feed itself' };
    var loop = this._loopPath(fm, tm);
    if (loop) return { ok: false, reason: 'that would make a loop: ' + loop.join(' → ') + ' → ' + tm, loop: true };
    return { ok: true, from: a, to: b };
  };
  P._tryConnect = function (a, b, quiet) {
    var r = this._check(a, b);
    if (r.ok) this.state.cables.push({ from: r.from, to: r.to });
    return r;
  };
  /* the code path the pointer uses on release over a jack */
  P.connect = function (a, b) {
    var r = this._tryConnect(a, b);
    if (r.ok) {
      this.selected = -1;
      this._drawCables();
      this._changed(true);
      this._say('');
    } else this._say(r.reason, true);
    return r;
  };
  P.removeCable = function (i) {
    if (i < 0 || i >= this.state.cables.length) return false;
    this.state.cables.splice(i, 1);
    this.selected = -1;
    this._drawCables();
    this._changed(true);
    return true;
  };

  P.setParam = function (kind, id, name, value) {
    var obj = kind === 'main' ? this.state.main : (kind === 'fx' ? this._fxById(id) : this._mod(id));
    if (!obj) return;
    (kind === 'main' ? obj : obj.params)[name] = value;
    if (this.opts.onParam) this.opts.onParam((kind === 'main' ? 'main' : id) + '.' + name, value);
    this._changed(false);
  };

  /* ---------- placement ---------- */
  P._cardSize = function (def) {
    var rows = Math.max(def.inputs ? def.inputs.length : 0, def.outputs ? def.outputs.length : 0);
    return { w: CARD_W, h: 34 + rows * 20 + (def.params || []).length * 26 + 12 };
  };
  P._freeSpot = function (def) {
    var sc = this.scroll, size = this._cardSize(def), self = this;
    var rects = this.state.modules.map(function (m) {
      var el = self.cardEls[m.id];
      return { x: m.x, y: m.y, w: el ? el.offsetWidth : CARD_W, h: el ? el.offsetHeight : 160 };
    });
    var x0 = sc.scrollLeft + 20, y0 = sc.scrollTop + 20;
    var vw = Math.max(sc.clientWidth, 600), vh = Math.max(sc.clientHeight, 400);
    function free(x, y) {
      return rects.every(function (r) {
        return x + size.w + 16 <= r.x || r.x + r.w + 16 <= x || y + size.h + 16 <= r.y || r.y + r.h + 16 <= y;
      });
    }
    for (var y = y0; y < y0 + vh - size.h; y += 20)
      for (var x = x0; x < x0 + vw - size.w; x += 20) if (free(x, y)) return { x: x, y: y };
    for (var y2 = y0; y2 < CANVAS_H - size.h; y2 += 40)
      for (var x2 = 20; x2 < CANVAS_W - size.w; x2 += 40) if (free(x2, y2)) return { x: x2, y: y2 };
    return { x: x0, y: y0 };
  };

  /* ---------- building the static UI ---------- */
  P._build = function () {
    var self = this, root = this.root;
    root.classList.add('vp');
    if (!root.hasAttribute('tabindex')) root.setAttribute('tabindex', '0');
    root.innerHTML = '';

    // toolbar
    var tb = h('div', { class: 'vp-toolbar' });
    var groups = [['source', 'sources'], ['shape', 'shapers'], ['mod', 'modulators']];
    groups.forEach(function (g) {
      var types = self.voiceTypes.filter(function (t) { return t.group === g[0]; });
      if (!types.length) return;
      var wrap = h('div', { class: 'vp-addgroup', role: 'group', 'aria-label': 'add ' + g[1] }, [h('span', { class: 'vp-grouplabel', text: g[1] })]);
      types.forEach(function (t) {
        wrap.appendChild(h('button', { type: 'button', class: 'vp-add vp-g-' + g[0], text: '+ ' + t.label, title: t.about || '', 'data-type': t.type,
          onclick: function () { self.addModule(t.type); } }));
      });
      tb.appendChild(wrap);
    });
    this.presetSel = h('select', { class: 'vp-preset', 'aria-label': 'preset' }, [h('option', { value: '', text: 'preset…' })]);
    this.presets.forEach(function (p, i) { self.presetSel.appendChild(h('option', { value: String(i), text: p.name, title: p.about || '' })); });
    this.presetSel.addEventListener('change', function () {
      var i = self.presetSel.value;
      self.presetSel.value = '';
      if (i === '') return;
      self.loadPreset(+i);
    });
    this.enabledEl = h('input', { type: 'checkbox', checked: true, onchange: function () {
      self.state.enabled = self.enabledEl.checked; self._changed(false); } });
    this.statusEl = h('span', { class: 'vp-status', role: 'status', 'aria-live': 'polite' });
    tb.appendChild(h('div', { class: 'vp-tools' }, [
      this.presets.length ? this.presetSel : null,
      h('label', { class: 'vp-enable' }, [this.enabledEl, h('span', { text: 'use this sound in the script' })])
    ]));
    root.appendChild(tb);
    root.appendChild(this.statusEl);

    // body: patch area + effects rack
    var body = h('div', { class: 'vp-body' });
    this.scroll = h('div', { class: 'vp-scroll' });
    this.canvas = h('div', { class: 'vp-canvas' });
    this.canvas.style.width = CANVAS_W + 'px';
    this.canvas.style.height = CANVAS_H + 'px';
    this.svgEl = svg('svg', { class: 'vp-cables', width: CANVAS_W, height: CANVAS_H });
    this.cableG = svg('g'); this.liveG = svg('g');
    this.svgEl.appendChild(this.cableG);
    this.canvas.appendChild(this.svgEl);
    this.liveSvg = svg('svg', { class: 'vp-livesvg', width: CANVAS_W, height: CANVAS_H });
    this.liveSvg.appendChild(this.liveG);
    this.canvas.appendChild(this.liveSvg);
    this.cardLayer = h('div', { class: 'vp-cards' });
    this.canvas.appendChild(this.cardLayer);
    this.cutBtn = h('button', { type: 'button', class: 'vp-cut', text: '×', title: 'remove cable', 'aria-label': 'remove cable',
      onclick: function () { if (self.cutIndex >= 0) self.removeCable(self.cutIndex); self._hideCut(); } });
    this.cutBtn.addEventListener('pointerenter', function () { clearTimeout(self.cutTimer); });
    this.cutBtn.addEventListener('pointerleave', function () { self._hideCut(true); });
    this.canvas.appendChild(this.cutBtn);
    this.scroll.appendChild(this.canvas);
    this.scroll.addEventListener('pointerdown', function (e) {
      if (e.target === self.scroll || e.target === self.canvas) { self.selected = -1; self._drawCables(); }
    });
    body.appendChild(this.scroll);

    this.rack = h('div', { class: 'vp-rack' });
    body.appendChild(this.rack);
    root.appendChild(body);

    // bottom bar: keyboard + live mark
    var bar = h('div', { class: 'vp-bar' });
    this.octLabel = h('span', { class: 'vp-oct' });
    bar.appendChild(h('div', { class: 'vp-octbtns' }, [
      h('button', { type: 'button', text: '−', title: 'octave down (z)', 'aria-label': 'octave down', onclick: function () { self._octave(-1); } }),
      this.octLabel,
      h('button', { type: 'button', text: '+', title: 'octave up (x)', 'aria-label': 'octave up', onclick: function () { self._octave(1); } })
    ]));
    this.keysEl = h('div', { class: 'vp-keys', role: 'group', 'aria-label': 'keyboard' });
    bar.appendChild(this.keysEl);
    this.liveEl = h('span', { class: 'vp-live' });
    bar.appendChild(this.liveEl);
    root.appendChild(bar);
    this.setLive(false);
    this._buildKeys();

    root.addEventListener('keydown', function (e) { self._keydown(e); });
    root.addEventListener('keyup', function (e) { self._keyup(e); });
    root.addEventListener('blur', function () { self._allNotesOff(); }, true);
    window.addEventListener('resize', function () { self._drawCables(); });
  };

  P.loadPreset = function (i) {
    var p = this.presets[i];
    if (!p) return false;
    if (this.dirty && typeof global.confirm === 'function' && !global.confirm('replace the current patch with "' + p.name + '"?')) return false;
    this.setPatch(p.patch);
    this.enabledEl.checked = this.state.enabled;
    this._changed(true);
    this.dirty = false;
    return true;
  };

  P._say = function (msg, bad) {
    var el = this.statusEl;
    el.textContent = msg || '';
    el.classList.toggle('bad', !!bad);
    el.classList.toggle('show', !!msg);
    clearTimeout(this._sayTimer);
    if (msg) this._sayTimer = setTimeout(function () { el.classList.remove('show'); el.textContent = ''; }, 6000);
  };

  /* ---------- rendering: modules ---------- */
  P._render = function () {
    this._renderModules();
    this._renderRack();
    this.enabledEl.checked = this.state.enabled;
    this._drawCables();
  };

  P._paramRow = function (def, p, obj, kind, idForLabel) {
    var self = this;
    var cur = obj.params ? obj.params[p.name] : obj[p.name];
    if (cur == null) cur = p.default;
    var aria = idForLabel + ' ' + p.label;
    function set(v) { self.setParam(kind, idForLabel, p.name, v); }
    if (p.options) {
      var sel = h('select', { class: 'vp-opt', 'aria-label': aria });
      p.options.forEach(function (o, i) { sel.appendChild(h('option', { value: String(i), text: o })); });
      sel.value = String(clamp(Math.round(cur), 0, p.options.length - 1));
      sel.addEventListener('change', function () { set(+sel.value); });
      return h('div', { class: 'vp-param' }, [h('label', { class: 'vp-plabel', text: p.label }), sel]);
    }
    var val = h('span', { class: 'vp-val', text: fmt(p, cur) });
    var attrs = { type: 'range', class: 'vp-slider', 'aria-label': aria };
    if (p.step || !isExp(p)) {
      attrs.min = p.min; attrs.max = p.max;
      attrs.step = p.step || ((p.max - p.min) / 200);
    } else { attrs.min = 0; attrs.max = 1; attrs.step = 0.005; }
    var inp = h('input', attrs);
    var usesReal = !!(p.step || !isExp(p));
    inp.value = usesReal ? cur : toPos(p, cur);
    inp.addEventListener('input', function () {
      var v = usesReal ? clamp(+inp.value, p.min, p.max) : fromPos(p, +inp.value);
      if (usesReal && !p.step) v = Math.round(v * 1e5) / 1e5;
      val.textContent = fmt(p, v);
      set(v);
    });
    inp.addEventListener('dblclick', function () {
      inp.value = usesReal ? p.default : toPos(p, p.default);
      val.textContent = fmt(p, p.default);
      set(p.default);
    });
    return h('div', { class: 'vp-param' }, [h('label', { class: 'vp-plabel', text: p.label }), inp, val]);
  };

  P._renderModules = function () {
    var self = this;
    this.cardLayer.innerHTML = '';
    this.cardEls = {}; this.jackEls = {};
    this.state.modules.forEach(function (m) {
      var def = self.types[m.type];
      var fixed = def.group === 'fixed';
      var head = h('div', { class: 'vp-head', title: def.about || '' }, [
        h('span', { class: 'vp-id', text: m.id }),
        m.id !== def.label ? h('span', { class: 'vp-type', text: def.label }) : null,
        fixed ? null : h('button', { type: 'button', class: 'vp-x', text: '×', title: 'remove ' + m.id, 'aria-label': 'remove ' + m.id,
          onclick: function (e) { e.stopPropagation(); self.removeModule(m.id); } })
      ]);
      var card = h('div', { class: 'vp-card vp-g-' + def.group, 'data-id': m.id }, [head]);
      card.style.left = m.x + 'px'; card.style.top = m.y + 'px'; card.style.width = CARD_W + 'px';
      self._dragHeader(head, m, card);
      var n = Math.max(def.inputs.length, def.outputs.length);
      if (n) {
        var io = h('div', { class: 'vp-io' });
        for (var i = 0; i < n; i++) {
          var row = h('div', { class: 'vp-iorow' });
          var inp = def.inputs[i], out = def.outputs[i];
          var l = h('div', { class: 'vp-in' }), r = h('div', { class: 'vp-out' });
          if (inp) l.appendChild(self._jack(m.id, inp.name, 'in', inp.label, def.group)), l.appendChild(h('span', { class: 'vp-jlabel', text: inp.label }));
          if (out) r.appendChild(h('span', { class: 'vp-jlabel', text: out.label })), r.appendChild(self._jack(m.id, out.name, 'out', out.label, def.group));
          row.appendChild(l); row.appendChild(r); io.appendChild(row);
        }
        card.appendChild(io);
      }
      if (def.params.length) {
        var pb = h('div', { class: 'vp-params' });
        def.params.forEach(function (p) { pb.appendChild(self._paramRow(def, p, m, 'mod', m.id)); });
        card.appendChild(pb);
      }
      self.cardLayer.appendChild(card);
      self.cardEls[m.id] = card;
    });
  };

  P._jack = function (id, name, dir, label, group) {
    var self = this, key = id + '.' + name;
    var j = h('button', { type: 'button', class: 'vp-jack vp-' + dir + 'jack', 'data-key': key, 'data-dir': dir,
      title: key, 'aria-label': (dir === 'out' ? 'output ' : 'input ') + key });
    this.jackEls[key] = j;
    j.addEventListener('pointerdown', function (e) { self._startCable(e, key, dir); });
    j.addEventListener('keydown', function (e) {
      // keyboard: Enter on an output arms it, Enter on an input connects
      if (e.key !== 'Enter' && e.key !== ' ') return;
      e.preventDefault(); e.stopPropagation();
      if (!self.armed) { self.armed = key; j.classList.add('armed'); self._say('now press Enter on a jack to connect ' + key); return; }
      var a = self.armed; self.armed = null;
      Object.keys(self.jackEls).forEach(function (k) { self.jackEls[k].classList.remove('armed'); });
      if (a !== key) self.connect(a, key);
    });
    return j;
  };

  P._dragHeader = function (head, m, card) {
    var self = this, d = null;
    head.addEventListener('pointerdown', function (e) {
      if (e.target.closest('button') || e.button > 0) return;
      d = { px: e.clientX, py: e.clientY, x: m.x, y: m.y, moved: false };
      head.setPointerCapture(e.pointerId);
      card.classList.add('dragging');
      e.preventDefault();
    });
    head.addEventListener('pointermove', function (e) {
      if (!d) return;
      var nx = clamp(d.x + e.clientX - d.px, 0, CANVAS_W - CARD_W), ny = clamp(d.y + e.clientY - d.py, 0, CANVAS_H - 60);
      if (nx !== m.x || ny !== m.y) d.moved = true;
      m.x = nx; m.y = ny;
      card.style.left = nx + 'px'; card.style.top = ny + 'px';
      self._drawCables();
    });
    function end(e) {
      if (!d) return;
      var moved = d.moved; d = null;
      card.classList.remove('dragging');
      try { head.releasePointerCapture(e.pointerId); } catch (x) {}
      if (moved) self._changed(false);
    }
    head.addEventListener('pointerup', end);
    head.addEventListener('pointercancel', end);
  };

  /* ---------- cables ---------- */
  P._jackPos = function (key) {
    var j = this.jackEls[key];
    if (!j) return null;
    var r = j.getBoundingClientRect(), c = this.canvas.getBoundingClientRect();
    return { x: r.left + r.width / 2 - c.left, y: r.top + r.height / 2 - c.top };
  };
  function bez(a, b) {
    var dx = Math.max(40, Math.abs(b.x - a.x) * 0.5);
    return 'M' + a.x + ' ' + a.y + ' C' + (a.x + dx) + ' ' + a.y + ' ' + (b.x - dx) + ' ' + b.y + ' ' + b.x + ' ' + b.y;
  }
  P._drawCables = function () {
    var self = this;
    this.cableG.innerHTML = '';
    this.state.cables.forEach(function (c, i) {
      var a = self._jackPos(c.from), b = self._jackPos(c.to);
      if (!a || !b) return;
      var m = self._mod(split(c.from)[0]);
      var grp = m ? self.types[m.type].group : 'shape';
      var d = bez(a, b);
      var g = svg('g', { class: 'vp-cable vp-c-' + grp + (i === self.selected ? ' sel' : '') });
      g.appendChild(svg('path', { class: 'vp-cable-line', d: d }));
      var hit = svg('path', { class: 'vp-cable-hit', d: d });
      hit.addEventListener('pointerenter', function () { self._showCut(i, a, b); });
      hit.addEventListener('pointerleave', function () { self._hideCut(true); });
      hit.addEventListener('pointerdown', function (e) {
        e.stopPropagation(); e.preventDefault();
        self.root.focus({ preventScroll: true });
        if (self.selected === i) self.removeCable(i);
        else { self.selected = i; self._drawCables(); self._showCut(i, a, b); }
      });
      g.appendChild(hit);
      self.cableG.appendChild(g);
    });
    if (this.selected >= 0 && this.state.cables[this.selected]) {
      var c = this.state.cables[this.selected];
      var a2 = this._jackPos(c.from), b2 = this._jackPos(c.to);
      if (a2 && b2) this._showCut(this.selected, a2, b2);
    } else if (this.cutIndex >= 0 && this.cutIndex >= this.state.cables.length) this._hideCut();
  };
  P._showCut = function (i, a, b) {
    clearTimeout(this.cutTimer);
    this.cutIndex = i;
    this.cutBtn.style.left = ((a.x + b.x) / 2) + 'px';
    this.cutBtn.style.top = ((a.y + b.y) / 2) + 'px';
    this.cutBtn.classList.add('show');
  };
  P._hideCut = function (later) {
    var self = this;
    clearTimeout(this.cutTimer);
    var f = function () { if (self.selected === self.cutIndex && self.selected >= 0) return; self.cutBtn.classList.remove('show'); self.cutIndex = -1; };
    if (later) this.cutTimer = setTimeout(f, 350); else { this.selected = -1; this.cutBtn.classList.remove('show'); this.cutIndex = -1; }
  };

  P._startCable = function (e, key, dir) {
    if (e.button > 0) return;
    var self = this, jack = e.currentTarget;
    e.preventDefault(); e.stopPropagation();
    jack.setPointerCapture(e.pointerId);
    var start = this._jackPos(key);
    var line = svg('path', { class: 'vp-cable-live' });
    this.liveG.appendChild(line);
    this.root.classList.add('vp-wiring');
    Object.keys(this.jackEls).forEach(function (k) {
      var el = self.jackEls[k];
      if (el.getAttribute('data-dir') === dir) return;
      var ok = self._check(key, k).ok;
      el.classList.add(ok ? 'target' : 'blocked');
    });
    function pt(ev) {
      var c = self.canvas.getBoundingClientRect();
      return { x: ev.clientX - c.left, y: ev.clientY - c.top };
    }
    function move(ev) {
      var p = pt(ev);
      line.setAttribute('d', dir === 'out' ? bez(start, p) : bez(p, start));
    }
    function finish(ev, cancel) {
      jack.removeEventListener('pointermove', move);
      jack.removeEventListener('pointerup', up);
      jack.removeEventListener('pointercancel', cancelH);
      try { jack.releasePointerCapture(ev.pointerId); } catch (x) {}
      self.liveG.innerHTML = '';
      self.root.classList.remove('vp-wiring');
      Object.keys(self.jackEls).forEach(function (k) { self.jackEls[k].classList.remove('target', 'blocked'); });
      if (cancel) return;
      var el = document.elementFromPoint(ev.clientX, ev.clientY);
      var tj = el && el.closest ? el.closest('.vp-jack') : null;
      if (tj && tj !== jack && tj.getAttribute('data-dir') !== dir) self.connect(key, tj.getAttribute('data-key'));
    }
    function up(ev) { finish(ev, false); }
    function cancelH(ev) { finish(ev, true); }
    jack.addEventListener('pointermove', move);
    jack.addEventListener('pointerup', up);
    jack.addEventListener('pointercancel', cancelH);
    move(e);
  };

  /* ---------- effects rack ---------- */
  P._renderRack = function () {
    var self = this, rack = this.rack;
    rack.innerHTML = '';
    rack.appendChild(h('div', { class: 'vp-racktitle', text: 'effects' }));
    var mainCard = h('div', { class: 'vp-fxcard vp-main' }, [h('div', { class: 'vp-fxhead', title: this.mainDef ? this.mainDef.about : '' }, [h('span', { class: 'vp-id', text: 'main' })])]);
    if (this.mainDef) {
      var pb = h('div', { class: 'vp-params' });
      this.mainDef.params.forEach(function (p) { pb.appendChild(self._paramRow(self.mainDef, p, self.state.main, 'main', 'main')); });
      mainCard.appendChild(pb);
    }
    rack.appendChild(mainCard);
    var n = this.state.fx.length;
    this.state.fx.forEach(function (f, i) {
      var def = self.types[f.type];
      var head = h('div', { class: 'vp-fxhead', title: def.about || '' }, [
        h('span', { class: 'vp-id', text: f.id }),
        h('span', { class: 'vp-fxbtns' }, [
          h('button', { type: 'button', text: '↑', title: 'move up', 'aria-label': 'move ' + f.id + ' up', disabled: i === 0, onclick: function () { self.moveFx(f.id, -1); } }),
          h('button', { type: 'button', text: '↓', title: 'move down', 'aria-label': 'move ' + f.id + ' down', disabled: i === n - 1, onclick: function () { self.moveFx(f.id, 1); } }),
          h('button', { type: 'button', class: 'vp-x', text: '×', title: 'remove ' + f.id, 'aria-label': 'remove ' + f.id, onclick: function () { self.removeFx(f.id); } })
        ])
      ]);
      var card = h('div', { class: 'vp-fxcard', 'data-id': f.id }, [head]);
      var pb2 = h('div', { class: 'vp-params' });
      def.params.forEach(function (p) { pb2.appendChild(self._paramRow(def, p, f, 'fx', f.id)); });
      card.appendChild(pb2);
      rack.appendChild(card);
    });
    if (!n) rack.appendChild(h('div', { class: 'vp-empty', text: 'no effects yet' }));
    var adds = h('div', { class: 'vp-fxadd', role: 'group', 'aria-label': 'add effect' }, [h('span', { class: 'vp-grouplabel', text: 'add effect' })]);
    this.fxTypes.forEach(function (t) {
      adds.appendChild(h('button', { type: 'button', class: 'vp-addfx', text: '+ ' + t.label, title: t.about || '', 'data-type': t.type, onclick: function () { self.addFx(t.type); } }));
    });
    rack.appendChild(adds);
  };

  /* ---------- keyboard ---------- */
  var BLACK = { 1: 1, 3: 1, 6: 1, 8: 1, 10: 1 };
  var NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  var KEYMAP = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12 };

  P._base = function () { return 48 + 12 * this.octave; };
  P._buildKeys = function () {
    var self = this, el = this.keysEl;
    el.innerHTML = '';
    this.keyEls = [];
    var whites = 0, i;
    for (i = 0; i < 24; i++) if (!BLACK[i % 12]) whites++;
    whites++; // closing C
    var wi = 0;
    function mk(off, black, left) {
      var k = h('button', { type: 'button', class: 'vp-key' + (black ? ' black' : ''), tabindex: '-1', 'data-off': off });
      k.style.left = left + '%';
      k.style.width = (100 / whites * (black ? 0.62 : 1)) + '%';
      k.addEventListener('pointerdown', function (e) {
        e.preventDefault(); k.setPointerCapture(e.pointerId);
        var note = self._base() + off;
        self.heldPtr[e.pointerId] = note;
        self._note(true, note);
        self.root.focus({ preventScroll: true });
      });
      function up(e) {
        var note = self.heldPtr[e.pointerId];
        if (note == null) return;
        delete self.heldPtr[e.pointerId];
        self._note(false, note);
      }
      k.addEventListener('pointerup', up); k.addEventListener('pointercancel', up);
      self.keyEls[off] = k;
      el.appendChild(k);
      return k;
    }
    var blacks = [];
    for (i = 0; i <= 24; i++) {
      if (BLACK[i % 12]) { blacks.push([i, wi]); continue; }
      mk(i, false, wi * 100 / whites); wi++;
    }
    blacks.forEach(function (b) { mk(b[0], true, b[1] * 100 / whites - 100 / whites * 0.31); });
    this._octLabel();
  };
  P._octLabel = function () {
    var b = this._base();
    this.octLabel.textContent = 'C' + (b / 12 - 2);
  };
  P._octave = function (d) {
    this._allNotesOff();
    this.octave = clamp(this.octave + d, -3, 3);
    this._octLabel();
  };
  P._note = function (on, note) {
    var off = note - this._base();
    if (this.keyEls[off]) this.keyEls[off].classList.toggle('down', on);
    if (this.opts.onNote) this.opts.onNote(on, note, 0.8);
  };
  P._allNotesOff = function () {
    var self = this;
    Object.keys(this.heldKeys).forEach(function (k) { self._note(false, self.heldKeys[k]); });
    Object.keys(this.heldPtr).forEach(function (k) { self._note(false, self.heldPtr[k]); });
    this.heldKeys = {}; this.heldPtr = {};
  };
  P._keydown = function (e) {
    var t = e.target;
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    if (t && (t.tagName === 'SELECT' || (t.tagName === 'INPUT' && t.type !== 'range' && t.type !== 'checkbox'))) return;
    if ((e.key === 'Delete' || e.key === 'Backspace') && this.selected >= 0) {
      e.preventDefault(); this.removeCable(this.selected); return;
    }
    if (e.key === 'Escape') { this.selected = -1; this.armed = null; this._drawCables(); return; }
    var k = e.key.length === 1 ? e.key.toLowerCase() : '';
    if (k === 'z' || k === 'x') { if (!e.repeat) this._octave(k === 'z' ? -1 : 1); e.preventDefault(); return; }
    if (k in KEYMAP) {
      e.preventDefault();
      if (e.repeat || this.heldKeys[k] != null) return;
      var note = this._base() + KEYMAP[k];
      this.heldKeys[k] = note;
      this._note(true, note);
    }
  };
  P._keyup = function (e) {
    var k = e.key.length === 1 ? e.key.toLowerCase() : '';
    if (this.heldKeys[k] != null) { var n = this.heldKeys[k]; delete this.heldKeys[k]; this._note(false, n); }
  };

  global.VerdandiPatcher = VerdandiPatcher;
})(window);
