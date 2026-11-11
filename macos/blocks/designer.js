/* the screen designer: place elements on the 128 x 64 norns screen and make any number property move.
   Plain browser JS. See screen.json for the catalog. Usage:
     const d = new VerdandiDesigner(rootEl, catalog, { onChange(design, structural), onTestNote(), getControls() });
     d.setDesign(design); d.getDesign(); d.stage; d.select(id);                                          */
(function () {
  'use strict';
  var SW = 128, SH = 64, NS = 'http://www.w3.org/2000/svg', instances = 0;

  function el(tag, cls, text) {
    var e = document.createElement(tag);
    if (cls) e.className = cls;
    if (text != null) e.textContent = text;
    return e;
  }
  function sv(tag, attrs, cls) {
    var e = document.createElementNS(NS, tag);
    for (var k in attrs) e.setAttribute(k, attrs[k]);
    if (cls) e.setAttribute('class', cls);
    return e;
  }
  function clone(o) { return JSON.parse(JSON.stringify(o)); }
  function clamp(v, a, b) { return Math.min(b, Math.max(a, v)); }
  function isNum(v) { return typeof v === 'number' && isFinite(v); }

  // sensible decimals: step 1 -> integers, range <= 1 -> two decimals, else one
  function decimals(p) {
    if (p.step && p.step >= 1) return 0;
    if ((p.max - p.min) <= 1) return 2;
    return 1;
  }
  function fmt(v, p) {
    if (!isNum(v)) return '';
    var d = decimals(p);
    return String(+v.toFixed(d));
  }
  function stepOf(p) {
    if (p.step) return p.step;
    return (p.max - p.min) <= 1 ? 0.01 : 0.1;
  }
  // drag rounding: integers when the prop has a step of 1 or more, else half pixels
  function quant(p, v) {
    v = (p.step && p.step >= 1) ? Math.round(v) : Math.round(v * 2) / 2;
    return clamp(v, p.min, p.max);
  }

  var ICON = {
    eye: '<svg viewBox="0 0 16 16" width="14" height="14"><path d="M1 8s2.6-4.5 7-4.5S15 8 15 8s-2.6 4.5-7 4.5S1 8 1 8z" fill="none" stroke="currentColor" stroke-width="1.4"/><circle cx="8" cy="8" r="2.2" fill="currentColor"/></svg>',
    eyeoff: '<svg viewBox="0 0 16 16" width="14" height="14"><path d="M1 8s2.6-4.5 7-4.5S15 8 15 8s-2.6 4.5-7 4.5S1 8 1 8z" fill="none" stroke="currentColor" stroke-width="1.4" opacity=".45"/><path d="M2.5 13.5l11-11" stroke="currentColor" stroke-width="1.4"/></svg>',
    copy: '<svg viewBox="0 0 16 16" width="14" height="14"><rect x="5.5" y="5.5" width="8" height="8" rx="1" fill="none" stroke="currentColor" stroke-width="1.4"/><path d="M10.5 3.5v-1h-8v8h1" fill="none" stroke="currentColor" stroke-width="1.4"/></svg>'
  };

  function VerdandiDesigner(root, catalog, opts) {
    this.root = root;
    this.cat = catalog;
    this.opts = opts || {};
    this.uid = ++instances;
    this.defs = {};
    (catalog.elements || []).forEach(function (e) { this.defs[e.type] = e; }, this);
    this.common = catalog.common || [];
    this.kinds = {};
    (catalog.animations || []).forEach(function (a) { this.kinds[a.kind] = a; }, this);
    this.layers = [];
    this.sel = null;
    this.nextN = {};
    this.scale = 4;
    this.drag = null;
    this.hover = null;
    this.over = false;
    this._sync = [];
    this._last = 0;
    this._timer = 0;
    this._build();
  }
  var P = VerdandiDesigner.prototype;

  /* ---------- catalog helpers ---------- */
  P.propDefs = function (type) { return (this.defs[type].props || []).concat(this.common); };
  P.propDef = function (type, name) {
    var ps = this.propDefs(type);
    for (var i = 0; i < ps.length; i++) if (ps[i].name === name) return ps[i];
    return null;
  };
  P.layerById = function (id) {
    for (var i = 0; i < this.layers.length; i++) if (this.layers[i].id === id) return this.layers[i];
    return null;
  };
  P.layerIndex = function (id) {
    for (var i = 0; i < this.layers.length; i++) if (this.layers[i].id === id) return i;
    return -1;
  };
  P.newId = function (type) {
    var n = this.nextN[type] || 1;
    while (this.layerById(type + n)) n++;
    this.nextN[type] = n + 1;
    return type + n;
  };
  P.defaultAnim = function (pdef, kind, v, prev) {
    var k = this.kinds[kind], a = { kind: kind };
    a.from = prev && isNum(prev.from) ? prev.from : v;
    a.to = prev && isNum(prev.to) ? prev.to : (v >= pdef.max ? pdef.min : pdef.max);
    (k.fields || []).forEach(function (f) { a[f.name] = f['default']; });
    return a;
  };
  P.newLayer = function (type) {
    var def = this.defs[type], props = {}, self = this;
    this.propDefs(type).forEach(function (p) {
      var pr = { v: p['default'] };
      if (p.animate && self.kinds[p.animate] && p.type === 'number') {
        pr.anim = self.defaultAnim(p, p.animate, p['default']);
        pr.anim.from = p.min; pr.anim.to = p.max;
      }
      props[p.name] = pr;
    });
    return { id: this.newId(type), type: type, hidden: false, props: props };
  };
  // read a design-form layer (from setDesign) and fill it from the catalog
  P.readLayer = function (src) {
    var self = this;
    if (!src || !this.defs[src.type]) {
      console.warn('designer: dropped a layer of unknown type', src && src.type);
      return null;
    }
    var id = typeof src.id === 'string' && src.id && !this.layerById(src.id) ? src.id : this.newId(src.type);
    var m = /^(.*?)(\d+)$/.exec(id);
    if (m && m[1] === src.type) this.nextN[src.type] = Math.max(this.nextN[src.type] || 1, +m[2] + 1);
    var props = {}, sp = src.props || {};
    this.propDefs(src.type).forEach(function (p) {
      var s = sp[p.name], v = s && s.v !== undefined ? s.v : p['default'], pr = {};
      if (p.type === 'number') {
        v = +v; if (!isFinite(v)) v = p['default'];
      } else if (p.type === 'choice') {
        if ((p.options || []).indexOf(v) < 0) v = p['default'];
      } else v = String(v == null ? p['default'] : v);
      pr.v = v;
      if (p.type === 'number' && s && s.anim && self.kinds[s.anim.kind]) {
        var a = self.defaultAnim(p, s.anim.kind, v);
        if (isNum(+s.anim.from) && s.anim.from !== null) a.from = +s.anim.from;
        if (isNum(+s.anim.to) && s.anim.to !== null) a.to = +s.anim.to;
        (self.kinds[s.anim.kind].fields || []).forEach(function (f) {
          var x = s.anim[f.name];
          if (x === undefined || x === null) return;
          if (f.type === 'number') { if (isFinite(+x)) a[f.name] = +x; }
          else if (f.type === 'choice') { if ((f.options || []).indexOf(x) >= 0) a[f.name] = x; }
          else a[f.name] = String(x);
        });
        pr.anim = a;
      }
      props[p.name] = pr;
    });
    return { id: id, type: src.type, hidden: !!src.hidden, props: props };
  };

  /* ---------- design in / out ---------- */
  P.getDesign = function () {
    return {
      layers: this.layers.map(function (l) {
        return { id: l.id, type: l.type, hidden: !!l.hidden, props: clone(l.props) };
      })
    };
  };
  P.setDesign = function (design) {
    var self = this, keep = this.sel;
    this.layers = [];
    this.nextN = {};
    ((design && design.layers) || []).forEach(function (s) {
      var l = self.readLayer(s);
      if (l) self.layers.push(l);
    });
    this.sel = this.layerById(keep) ? keep : null;
    this.drag = null;
    this._renderList();
    this._buildProps();
    this._draw();
  };
  P._emit = function (structural) {
    if (this.opts.onChange) this.opts.onChange(this.getDesign(), !!structural);
  };
  P._emitDrag = function () {
    var self = this, now = Date.now();
    if (now - this._last >= 33) { this._last = now; clearTimeout(this._timer); this._timer = 0; this._emit(false); return; }
    if (!this._timer) this._timer = setTimeout(function () { self._timer = 0; self._last = Date.now(); self._emit(false); }, 33);
  };
  P._flush = function () {
    if (this._timer) { clearTimeout(this._timer); this._timer = 0; this._emit(false); }
  };

  /* ---------- boxes in screen pixels ---------- */
  P.boxOf = function (l) {
    var def = this.defs[l.type], p = l.props, g = function (n) { return p[n] ? p[n].v : 0; };
    switch (def.box) {
      case 'text':
        var size = g('size'), txt = String(g('text') || '').replace(/\{[^}]*\}/g, '');
        var w = Math.max(1, txt.length) * size * 0.6, x = g('x'), al = g('align');
        if (al === 'centre' || al === 'center') x -= w / 2; else if (al === 'right') x -= w;
        return { x: x, y: g('y') - size, w: w, h: size };
      case 'rect': return { x: g('x'), y: g('y'), w: g('w'), h: g('h') };
      case 'circle': return { x: g('x') - g('r'), y: g('y') - g('r'), w: 2 * g('r'), h: 2 * g('r') };
      case 'square': return { x: g('x'), y: g('y'), w: g('size'), h: g('size') };
      case 'line':
        var x1 = g('x1'), y1 = g('y1'), x2 = g('x2'), y2 = g('y2');
        var b = { x: Math.min(x1, x2), y: Math.min(y1, y2), w: Math.abs(x2 - x1), h: Math.abs(y2 - y1) };
        if (b.w < 3) { b.x -= (3 - b.w) / 2; b.w = 3; }
        if (b.h < 3) { b.y -= (3 - b.h) / 2; b.h = 3; }
        return b;
      default: return { x: 0, y: 0, w: SW, h: SH };
    }
  };
  P.kindOf = function (l) { return this.defs[l.type].box; };
  P.movePairs = function (l) {
    var k = this.kindOf(l);
    if (k === 'full') return null;
    if (k === 'line') return ['x1', 'y1', 'x2', 'y2'];
    return ['x', 'y'];
  };
  P.handlesOf = function (l) {
    var k = this.kindOf(l), b = this.boxOf(l), pd = this, t = l.type;
    function def(n) { return pd.propDef(t, n); }
    function q(n, v) { return quant(def(n), v); }
    switch (k) {
      case 'rect':
        return [
          { x: b.x + b.w, y: b.y + b.h / 2, cur: 'ew-resize', set: function (s, dx) { return { w: q('w', s.w + dx) }; } },
          { x: b.x + b.w / 2, y: b.y + b.h, cur: 'ns-resize', set: function (s, dx, dy) { return { h: q('h', s.h + dy) }; } },
          { x: b.x + b.w, y: b.y + b.h, cur: 'nwse-resize', set: function (s, dx, dy) { return { w: q('w', s.w + dx), h: q('h', s.h + dy) }; } }];
      case 'square':
        return [{ x: b.x + b.w, y: b.y + b.h, cur: 'nwse-resize', set: function (s, dx, dy) { return { size: q('size', s.size + (dx + dy) / 2) }; } }];
      case 'circle':
        return [
          { x: b.x + b.w, y: b.y + b.h / 2, cur: 'ew-resize', set: function (s, dx) { return { r: q('r', s.r + dx) }; } },
          { x: b.x + b.w / 2, y: b.y + b.h, cur: 'ns-resize', set: function (s, dx, dy) { return { r: q('r', s.r + dy) }; } }];
      case 'line':
        var p = l.props;
        return [
          { x: p.x1.v, y: p.y1.v, cur: 'move', set: function (s, dx, dy) { return { x1: q('x1', s.x1 + dx), y1: q('y1', s.y1 + dy) }; } },
          { x: p.x2.v, y: p.y2.v, cur: 'move', set: function (s, dx, dy) { return { x2: q('x2', s.x2 + dx), y2: q('y2', s.y2 + dy) }; } }];
      default: return [];
    }
  };
  P._snap = function (l, names) {
    var s = {}; names.forEach(function (n) { s[n] = l.props[n].v; }); return s;
  };

  /* ---------- editing ---------- */
  P._setV = function (l, name, v) { l.props[name].v = v; };
  // set v and, for an animated prop, shift from/to by the same delta from the drag start (clamped)
  P._put = function (l, name, v, startV, startAnim) {
    var pr = l.props[name];
    if (v === pr.v) return false;
    pr.v = v;
    if (pr.anim && startAnim) {
      var pd = this.propDef(l.type, name), d = v - startV;
      pr.anim.from = clamp(startAnim.from + d, pd.min, pd.max);
      pr.anim.to = clamp(startAnim.to + d, pd.min, pd.max);
    }
    return true;
  };
  P._animSnap = function (l) {
    var a = {};
    for (var n in l.props) if (l.props[n].anim) a[n] = { from: l.props[n].anim.from, to: l.props[n].anim.to };
    return a;
  };
  // move by (dx, dy) from a snapshot; values stay inside min/max
  P._moveFrom = function (l, start, dx, dy, anims) {
    anims = anims || this._animSnap(l);
    var pairs = this.movePairs(l);
    if (!pairs) return false;
    var self = this, t = l.type;
    function range(names, d) {
      var lo = -Infinity, hi = Infinity;
      names.forEach(function (n) {
        var pd = self.propDef(t, n);
        lo = Math.max(lo, pd.min - start[n]); hi = Math.min(hi, pd.max - start[n]);
      });
      return clamp(d, lo, hi);
    }
    var xs = pairs.filter(function (n, i) { return i % 2 === 0; }), ys = pairs.filter(function (n, i) { return i % 2 === 1; });
    dx = range(xs, dx); dy = range(ys, dy);
    var changed = false;
    xs.forEach(function (n) { if (self._put(l, n, quant(self.propDef(t, n), start[n] + dx), start[n], anims[n])) changed = true; });
    ys.forEach(function (n) { if (self._put(l, n, quant(self.propDef(t, n), start[n] + dy), start[n], anims[n])) changed = true; });
    return changed;
  };
  P.nudge = function (dx, dy) {
    var l = this.layerById(this.sel);
    if (!l || !this.movePairs(l)) return;
    var start = this._snap(l, this.movePairs(l));
    if (this._moveFrom(l, start, dx, dy)) { this._afterEdit(); this._emit(false); }
  };
  P._afterEdit = function () {
    this._syncProps();
    this._draw();
  };

  P.addLayer = function (type) {
    if (!this.defs[type]) return null;
    var l = this.newLayer(type);
    this.layers.push(l);
    this.sel = l.id;
    this._renderList();
    this._buildProps();
    this._draw();
    this._emit(true);
    return l.id;
  };
  P.removeLayer = function (id) {
    var i = this.layerIndex(id);
    if (i < 0) return;
    this.layers.splice(i, 1);
    if (this.sel === id) this.sel = null;
    this._renderList(); this._buildProps(); this._draw();
    this._emit(true);
  };
  P.duplicateLayer = function (id) {
    var i = this.layerIndex(id);
    if (i < 0) return null;
    var src = this.layers[i], self = this;
    var l = { id: this.newId(src.type), type: src.type, hidden: src.hidden, props: clone(src.props) };
    this.layers.splice(i + 1, 0, l);
    var pairs = this.movePairs(l);
    if (pairs) {
      var start = this._snap(l, pairs);
      this._moveFrom(l, start, 4, 4);
      if (this._snap(l, pairs)[pairs[0]] === start[pairs[0]]) this._moveFrom(l, start, -4, -4);
    }
    this.sel = l.id;
    this._renderList(); this._buildProps(); this._draw();
    this._emit(true);
    return l.id;
  };
  // dir +1: one step toward the top (drawn later); -1: toward the bottom
  P.moveLayer = function (id, dir) {
    var i = this.layerIndex(id), j = i + (dir > 0 ? 1 : -1);
    if (i < 0 || j < 0 || j >= this.layers.length) return;
    var t = this.layers[i]; this.layers[i] = this.layers[j]; this.layers[j] = t;
    this._renderList(); this._draw();
    this._emit(true);
  };
  P.setHidden = function (id, h) {
    var l = this.layerById(id);
    if (!l) return;
    l.hidden = !!h;
    this._renderList(); this._draw();
    this._emit(true);
  };
  P.select = function (id) {
    if (id != null && !this.layerById(id)) id = null;
    if (id === this.sel) return;
    this.sel = id;
    this._renderList(); this._buildProps(); this._draw();
  };
  P.setAnimKind = function (id, name, kind) {
    var l = this.layerById(id);
    if (!l) return;
    var pr = l.props[name], pd = this.propDef(l.type, name);
    if (!pd || pd.type !== 'number') return;
    if (kind === 'fixed' || !this.kinds[kind]) delete pr.anim;
    else pr.anim = this.defaultAnim(pd, kind, pr.v, pr.anim);
    this._buildProps();
    this._emit(false);
  };

  /* ---------- pointer (screen pixel coordinates; the DOM events convert and call these) ---------- */
  P.hitTest = function (sx, sy) {
    var pad = 1 / this.scale;
    for (var i = this.layers.length - 1; i >= 0; i--) {
      var l = this.layers[i];
      if (l.hidden || this.kindOf(l) === 'full') continue;
      var b = this.boxOf(l);
      if (sx >= b.x - pad && sx <= b.x + b.w + pad && sy >= b.y - pad && sy <= b.y + b.h + pad) return l;
    }
    for (var j = this.layers.length - 1; j >= 0; j--) {
      var f = this.layers[j];
      if (!f.hidden && this.kindOf(f) === 'full') return f;
    }
    return null;
  };
  P.handleAt = function (sx, sy) {
    var l = this.layerById(this.sel);
    if (!l || l.hidden) return null;
    var hs = this.handlesOf(l), r = 6 / this.scale;
    for (var i = hs.length - 1; i >= 0; i--)
      if (Math.abs(sx - hs[i].x) <= r && Math.abs(sy - hs[i].y) <= r) return hs[i];
    return null;
  };
  P.pointerDown = function (sx, sy) {
    var h = this.handleAt(sx, sy), l = this.layerById(this.sel);
    if (h) {
      this.drag = { mode: 'resize', id: l.id, h: h, start: this._allStart(l), anims: this._animSnap(l), sx: sx, sy: sy };
      return 'resize';
    }
    var hit = this.hitTest(sx, sy);
    if (!hit) { this.select(null); return 'none'; }
    this.select(hit.id);
    var pairs = this.movePairs(hit);
    if (!pairs) return 'select';
    this.drag = { mode: 'move', id: hit.id, start: this._snap(hit, pairs), anims: this._animSnap(hit), sx: sx, sy: sy };
    return 'move';
  };
  P._allStart = function (l) {
    var s = {};
    for (var n in l.props) if (typeof l.props[n].v === 'number') s[n] = l.props[n].v;
    return s;
  };
  P.pointerMove = function (sx, sy) {
    var d = this.drag;
    if (!d) { this._hoverAt(sx, sy); return; }
    var l = this.layerById(d.id);
    if (!l) { this.drag = null; return; }
    var dx = sx - d.sx, dy = sy - d.sy, changed = false;
    if (d.mode === 'move') changed = this._moveFrom(l, d.start, dx, dy, d.anims);
    else {
      var set = d.h.set(d.start, dx, dy);
      for (var n in set) if (this._put(l, n, set[n], d.start[n], d.anims[n])) changed = true;
    }
    if (changed) { this._afterEdit(); this._emitDrag(); }
  };
  P.pointerUp = function () {
    if (!this.drag) return;
    this.drag = null;
    this._flush();
    this._draw();
  };
  P._hoverAt = function (sx, sy) {
    var h = this.handleAt(sx, sy), hit = h ? null : this.hitTest(sx, sy);
    var id = hit ? hit.id : null;
    this.overlay.style.cursor = h ? h.cur : (hit && this.movePairs(hit) ? 'move' : (hit ? 'pointer' : 'default'));
    if (id !== this.hover || !this.over) { this.hover = id; this.over = true; this._draw(); }
  };

  /* ---------- DOM ---------- */
  P._build = function () {
    var self = this, root = this.root;
    root.innerHTML = '';
    root.classList.add('vd');
    this.dl = el('datalist'); this.dl.id = 'vd-controls-' + this.uid;

    // toolbar
    var tb = el('div', 'vd-toolbar');
    (this.cat.groups || []).forEach(function (g) {
      var box = el('div', 'vd-group');
      box.appendChild(el('span', 'vd-grouplabel', g.label || g.name));
      var row = el('div', 'vd-addrow');
      (self.cat.elements || []).filter(function (e) { return e.group === g.name; }).forEach(function (e) {
        var b = el('button', 'vd-add', e.label || e.type);
        b.type = 'button'; b.title = e.about || ''; b.setAttribute('data-type', e.type);
        b.addEventListener('click', function () { self.addLayer(e.type); });
        row.appendChild(b);
      });
      box.appendChild(row);
      tb.appendChild(box);
    });
    var test = el('button', 'vd-test primary', 'test note');
    test.type = 'button'; test.title = 'play a note so the note animations can be seen';
    test.addEventListener('click', function () { if (self.opts.onTestNote) self.opts.onTestNote(); });
    tb.appendChild(test);
    this.toolbar = tb;

    // body
    var body = el('div', 'vd-body');
    var main = el('div', 'vd-main');
    this.area = el('div', 'vd-area'); this.area.tabIndex = 0;
    this.box = el('div', 'vd-stagebox');
    this.stage = el('div', 'vd-stage');
    this.overlay = sv('svg', { 'class': 'vd-overlay' });
    this.box.appendChild(this.stage); this.box.appendChild(this.overlay);
    this.area.appendChild(this.box);
    this.hint = el('div', 'vd-hint', 'click to select, drag to move, drag a handle to resize. arrows nudge (shift: 8), delete removes, cmd/ctrl+D duplicates, esc deselects.');
    main.appendChild(this.area); main.appendChild(this.hint);

    var side = el('div', 'vd-side');
    side.appendChild(el('div', 'vd-title', 'layers'));
    this.list = el('div', 'vd-list'); this.list.tabIndex = 0;
    side.appendChild(this.list);
    side.appendChild(el('div', 'vd-title', 'properties'));
    this.props = el('div', 'vd-props');
    side.appendChild(this.props);

    body.appendChild(main); body.appendChild(side);
    root.appendChild(tb); root.appendChild(body); root.appendChild(this.dl);

    // events
    var ov = this.overlay;
    function pt(ev) {
      var r = self.box.getBoundingClientRect();
      return [(ev.clientX - r.left) / r.width * SW, (ev.clientY - r.top) / r.height * SH];
    }
    ov.addEventListener('pointerdown', function (ev) {
      if (ev.button > 0) return;
      ev.preventDefault();
      self.area.focus({ preventScroll: true });
      var p = pt(ev);
      self.pointerDown(p[0], p[1]);
      try { ov.setPointerCapture(ev.pointerId); } catch (e) { /* synthetic events */ }
    });
    ov.addEventListener('pointermove', function (ev) { var p = pt(ev); self.pointerMove(p[0], p[1]); });
    ov.addEventListener('pointerup', function () { self.pointerUp(); });
    ov.addEventListener('pointercancel', function () { self.pointerUp(); });
    ov.addEventListener('pointerleave', function () {
      if (self.drag) return;
      self.over = false; self.hover = null; self._draw();
    });
    function keys(ev) { self._key(ev); }
    this.area.addEventListener('keydown', keys);
    this.list.addEventListener('keydown', keys);

    if (typeof ResizeObserver !== 'undefined') {
      this._ro = new ResizeObserver(function () { self._fit(); });
      this._ro.observe(this.area);
    } else window.addEventListener('resize', function () { self._fit(); });
    this._fit();
    this._renderList();
    this._buildProps();
  };

  P._fit = function () {
    var aw = this.area.clientWidth - 24, ah = this.area.clientHeight - 24;
    if (aw <= 0 || ah <= 0) return;
    var s = Math.min(aw / SW, ah / SH);
    s = s >= 1 ? Math.floor(s) : Math.max(0.25, Math.floor(s * 4) / 4);
    this.scale = s;
    this.box.style.width = SW * s + 'px';
    this.box.style.height = SH * s + 'px';
    this._draw();
  };

  P._key = function (ev) {
    var t = ev.target;
    if (t && /^(INPUT|SELECT|TEXTAREA)$/.test(t.tagName)) return;
    var l = this.layerById(this.sel), k = ev.key, step = ev.shiftKey ? 8 : 1;
    if (k === 'Escape') { this.select(null); ev.preventDefault(); }
    else if (!l) return;
    else if (k === 'ArrowLeft') { this.nudge(-step, 0); ev.preventDefault(); }
    else if (k === 'ArrowRight') { this.nudge(step, 0); ev.preventDefault(); }
    else if (k === 'ArrowUp') { this.nudge(0, -step); ev.preventDefault(); }
    else if (k === 'ArrowDown') { this.nudge(0, step); ev.preventDefault(); }
    else if (k === 'Delete' || k === 'Backspace') { this.removeLayer(l.id); ev.preventDefault(); }
    else if ((ev.metaKey || ev.ctrlKey) && (k === 'd' || k === 'D')) { this.duplicateLayer(l.id); ev.preventDefault(); }
  };

  /* overlay drawing */
  P._draw = function () {
    var s = this.scale, ov = this.overlay, self = this;
    ov.setAttribute('width', SW * s); ov.setAttribute('height', SH * s);
    while (ov.firstChild) ov.removeChild(ov.firstChild);
    function box(b, cls) {
      return sv('rect', { x: b.x * s + 0.5, y: b.y * s + 0.5, width: Math.max(1, b.w * s - 1), height: Math.max(1, b.h * s - 1) }, cls);
    }
    if (this.over || this.drag) {
      this.layers.forEach(function (l) {
        if (l.hidden || l.id === self.sel) return;
        ov.appendChild(box(self.boxOf(l), 'vd-o-other' + (l.id === self.hover ? ' hot' : '')));
      });
    }
    var l = this.layerById(this.sel);
    if (l && !l.hidden) {
      ov.appendChild(box(this.boxOf(l), 'vd-o-sel'));
      this.handlesOf(l).forEach(function (h) {
        ov.appendChild(sv('rect', { x: h.x * s - 4, y: h.y * s - 4, width: 8, height: 8 }, 'vd-o-handle'));
      });
    }
  };

  /* layer list */
  P._renderList = function () {
    var self = this, list = this.list;
    list.innerHTML = '';
    if (!this.layers.length) { list.appendChild(el('div', 'vd-empty', 'no layers yet: add an element above')); return; }
    for (var i = this.layers.length - 1; i >= 0; i--) (function (l, i) {
      var row = el('div', 'vd-row' + (l.id === self.sel ? ' sel' : '') + (l.hidden ? ' hidden' : ''));
      row.setAttribute('data-id', l.id);
      function btn(cls, html, title, fn, dis) {
        var b = el('button', 'vd-ib ' + cls); b.type = 'button'; b.innerHTML = html; b.title = title;
        if (dis) b.disabled = true;
        b.addEventListener('click', function (ev) { ev.stopPropagation(); fn(); });
        return b;
      }
      row.appendChild(btn('vd-eye', l.hidden ? ICON.eyeoff : ICON.eye, l.hidden ? 'show' : 'hide', function () { self.setHidden(l.id, !l.hidden); }));
      row.appendChild(el('span', 'vd-rid', l.id));
      row.appendChild(el('span', 'vd-rtype', self.defs[l.type].label || l.type));
      var acts = el('span', 'vd-acts');
      acts.appendChild(btn('vd-up', '&uarr;', 'move up (drawn later, on top)', function () { self.moveLayer(l.id, 1); }, i === self.layers.length - 1));
      acts.appendChild(btn('vd-down', '&darr;', 'move down', function () { self.moveLayer(l.id, -1); }, i === 0));
      acts.appendChild(btn('vd-dup', ICON.copy, 'duplicate', function () { self.duplicateLayer(l.id); }));
      acts.appendChild(btn('vd-del', '&times;', 'remove', function () { self.removeLayer(l.id); }));
      row.appendChild(acts);
      row.addEventListener('click', function () { self.select(l.id); self.list.focus({ preventScroll: true }); });
      list.appendChild(row);
    })(this.layers[i], i);
  };

  /* properties */
  P._controls = function () {
    var names = [];
    try { names = (this.opts.getControls && this.opts.getControls()) || []; } catch (e) { names = []; }
    this.dl.innerHTML = '';
    var self = this;
    names.forEach(function (n) { var o = el('option'); o.value = n; self.dl.appendChild(o); });
  };
  P._syncProps = function () { this._sync.forEach(function (f) { f(); }); };

  P._buildProps = function () {
    var self = this, host = this.props, l = this.layerById(this.sel);
    host.innerHTML = '';
    this._sync = [];
    if (!l) { host.appendChild(el('div', 'vd-empty', 'select a layer to edit it')); return; }
    this._controls();
    var def = this.defs[l.type];
    host.appendChild(el('div', 'vd-about', (def.label || l.type) + (def.about ? ': ' + def.about : '')));

    this.propDefs(l.type).forEach(function (p) {
      var pr = l.props[p.name], row = el('div', 'vd-prop');
      row.setAttribute('data-prop', p.name);
      var lab = el('label', 'vd-plabel', p.label || p.name);
      lab.title = p.about || '';
      if (p.type === 'number') {
        var line = el('div', 'vd-pline');
        line.appendChild(lab);
        var rng = el('input', 'vd-range'); rng.type = 'range'; rng.min = p.min; rng.max = p.max; rng.step = stepOf(p);
        var num = el('input', 'vd-num'); num.type = 'number'; num.min = p.min; num.max = p.max; num.step = stepOf(p);
        var cell = el('span', 'vd-numcell'); cell.appendChild(num);
        if (p.units) cell.appendChild(el('span', 'vd-units', p.units));
        line.appendChild(rng); line.appendChild(cell);
        row.appendChild(line);
        var put = function (v) {
          pr.v = v; self._syncProps(); self._draw(); self._emit(false);
        };
        rng.addEventListener('input', function () { put(clamp(+rng.value, p.min, p.max)); });
        num.addEventListener('input', function () {
          var v = parseFloat(num.value);
          if (isFinite(v)) put(clamp(v, p.min, p.max));
        });
        num.addEventListener('change', function () { num.value = fmt(pr.v, p); });
        self._sync.push(function () {
          rng.value = pr.v;
          if (document.activeElement !== num) num.value = fmt(pr.v, p);
        });
        // moves
        var mv = el('div', 'vd-moveline');
        mv.appendChild(el('span', 'vd-ml', 'moves:'));
        var sel = el('select', 'vd-moves');
        var fo = el('option', null, 'fixed'); fo.value = 'fixed'; sel.appendChild(fo);
        (self.cat.animations || []).forEach(function (a) { var o = el('option', null, a.label || a.kind); o.value = a.kind; sel.appendChild(o); });
        sel.value = pr.anim ? pr.anim.kind : 'fixed';
        sel.addEventListener('change', function () { self.setAnimKind(l.id, p.name, sel.value); });
        mv.appendChild(sel);
        row.appendChild(mv);
        if (pr.anim) {
          var kd = self.kinds[pr.anim.kind], an = el('div', 'vd-anim');
          var grid = el('div', 'vd-agrid');
          var addNum = function (label, key, min, max, step, units, unitsText) {
            grid.appendChild(el('span', 'vd-al', label));
            var i = el('input', 'vd-anum'); i.type = 'number'; i.step = step;
            if (min != null) i.min = min; if (max != null) i.max = max;
            i.setAttribute('data-field', key);
            var w = el('span', 'vd-numcell'); w.appendChild(i);
            if (unitsText) w.appendChild(el('span', 'vd-units', unitsText));
            grid.appendChild(w);
            i.addEventListener('input', function () {
              var v = parseFloat(i.value);
              if (isFinite(v)) { pr.anim[key] = min != null && max != null ? clamp(v, min, max) : v; self._emit(false); }
            });
            i.addEventListener('change', function () { i.value = String(+(+pr.anim[key]).toFixed(3)); });
            self._sync.push(function () { if (document.activeElement !== i) i.value = String(+(+pr.anim[key]).toFixed(3)); });
          };
          addNum('from', 'from', p.min, p.max, stepOf(p), null, p.units);
          addNum('to', 'to', p.min, p.max, stepOf(p), null, p.units);
          (kd.fields || []).forEach(function (f) {
            if (f.type === 'number') {
              addNum(f.label || f.name, f.name, f.min, f.max, f.step || ((f.max - f.min) <= 1 ? 0.01 : 0.05), null, f.units);
            } else if (f.type === 'choice') {
              grid.appendChild(el('span', 'vd-al', f.label || f.name));
              var s2 = el('select', 'vd-asel'); s2.setAttribute('data-field', f.name);
              (f.options || []).forEach(function (o) { var op = el('option', null, o); op.value = o; s2.appendChild(op); });
              s2.value = pr.anim[f.name];
              s2.addEventListener('change', function () { pr.anim[f.name] = s2.value; self._emit(false); });
              grid.appendChild(s2);
            } else {
              grid.appendChild(el('span', 'vd-al', f.label || f.name));
              var t2 = el('input', 'vd-atext'); t2.type = 'text'; t2.setAttribute('list', self.dl.id);
              t2.setAttribute('data-field', f.name);
              t2.value = pr.anim[f.name] == null ? '' : pr.anim[f.name];
              t2.addEventListener('focus', function () { self._controls(); });
              t2.addEventListener('input', function () { pr.anim[f.name] = t2.value; self._emit(false); });
              grid.appendChild(t2);
            }
          });
          an.appendChild(grid);
          if (kd.about) an.appendChild(el('div', 'vd-about', kd.about));
          row.appendChild(an);
        }
      } else if (p.type === 'choice') {
        var l2 = el('div', 'vd-pline vd-pline-wide');
        l2.appendChild(lab);
        var cs = el('select', 'vd-choice');
        (p.options || []).forEach(function (o) { var op = el('option', null, o); op.value = o; cs.appendChild(op); });
        cs.value = pr.v;
        cs.addEventListener('change', function () { pr.v = cs.value; self._draw(); self._emit(false); });
        l2.appendChild(cs); row.appendChild(l2);
      } else {
        var l3 = el('div', 'vd-pline vd-pline-wide');
        l3.appendChild(lab);
        var ti = el('input', 'vd-text'); ti.type = 'text'; ti.value = pr.v;
        if (p.type === 'control') {
          ti.setAttribute('list', self.dl.id);
          ti.addEventListener('focus', function () { self._controls(); });
        }
        ti.addEventListener('input', function () { pr.v = ti.value; self._draw(); self._emit(false); });
        l3.appendChild(ti); row.appendChild(l3);
      }
      host.appendChild(row);
    });
    this._syncProps();
  };

  window.VerdandiDesigner = VerdandiDesigner;
})();
