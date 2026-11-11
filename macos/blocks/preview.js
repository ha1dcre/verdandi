// preview.js: the screen preview of the script builder.
//
// runs the Lua the builder wrote in the browser (fengari, Lua 5.3) on top of preview_norns.lua,
// draws its screen into a 128 x 64 canvas the way the norns screen draws (16 brightness levels,
// paths filled or stroked, the norns font), ticks its clock and passes keys and encoders in.
// the sound is not previewed: Run plays the script in Verdandi.
(function (root) {
  'use strict';
  const {lua, lauxlib, lualib, interop, to_luastring} = root.fengari;

  // ---- the norns screen on a canvas
  class NornsScreen {
    constructor(canvas) {
      this.out = canvas.getContext('2d');
      this.buf = document.createElement('canvas');
      this.buf.width = 128;
      this.buf.height = 64;
      this.g = this.buf.getContext('2d');
      this.reset();
    }
    reset() {
      const g = this.g;
      g.setTransform(1, 0, 0, 1, 0, 0);
      g.fillStyle = '#000';
      g.fillRect(0, 0, 128, 64);
      g.beginPath();
      g.lineWidth = 1;
      this.lvl = 15;
      this.size = 8;
      this.x = 0;
      this.y = 0;
      this.update();
    }
    colour() {
      const v = Math.round(Math.max(0, Math.min(15, this.lvl)) * 17);
      return 'rgb(' + v + ',' + v + ',' + v + ')';
    }
    clear() {
      const g = this.g;
      g.save();
      g.setTransform(1, 0, 0, 1, 0, 0);
      g.fillStyle = '#000';
      g.fillRect(0, 0, 128, 64);
      g.restore();
      g.beginPath();
    }
    update() { this.out.drawImage(this.buf, 0, 0); }
    level(l) { this.lvl = Math.floor(Number(l) || 0); }
    line_width(w) { this.g.lineWidth = Number(w) || 1; }
    line_cap(c) { this.g.lineCap = String(c); }
    line_join(j) { this.g.lineJoin = String(j); }
    aa() {}
    move(x, y) { this.g.moveTo(x, y); this.x = x; this.y = y; }
    move_rel(dx, dy) { this.move(this.x + dx, this.y + dy); }
    line(x, y) { this.g.lineTo(x, y); this.x = x; this.y = y; }
    line_rel(dx, dy) { this.line(this.x + dx, this.y + dy); }
    curve(x1, y1, x2, y2, x3, y3) { this.g.bezierCurveTo(x1, y1, x2, y2, x3, y3); this.x = x3; this.y = y3; }
    curve_rel(x1, y1, x2, y2, x3, y3) {
      const x = this.x, y = this.y;
      this.curve(x + x1, y + y1, x + x2, y + y2, x + x3, y + y3);
    }
    arc(x, y, r, a1, a2) {
      this.g.arc(x, y, r, a1, a2);
      this.x = x + r * Math.cos(a2);
      this.y = y + r * Math.sin(a2);
    }
    circle(x, y, r) { this.g.moveTo(x + r, y); this.g.arc(x, y, r, 0, Math.PI * 2); }
    rect(x, y, w, h) { this.g.rect(x, y, w, h); this.x = x; this.y = y; }
    pixel(x, y) { this.g.rect(Math.floor(x), Math.floor(y), 1, 1); }
    close() { this.g.closePath(); }
    stroke() { this.g.strokeStyle = this.colour(); this.g.stroke(); this.g.beginPath(); }
    fill() { this.g.fillStyle = this.colour(); this.g.fill(); this.g.beginPath(); }
    font_size(n) { this.size = Number(n) || 8; }
    font_face() {}
    rotate(r) { this.g.rotate(r); }
    translate(x, y) { this.g.translate(x, y); }
    save() { this.g.save(); }
    restore() { this.g.restore(); }
    blend_mode() {}
    invert() {}
    font() { return this.size + 'px norns, monospace'; }
    text_width(s) { this.g.font = this.font(); return this.g.measureText(String(s)).width; }
    textAt(s, align) {
      const g = this.g;
      g.font = this.font();
      g.fillStyle = this.colour();
      g.textAlign = align;
      g.textBaseline = 'alphabetic';
      g.fillText(String(s), this.x, this.y);
      if (align === 'left') this.x += g.measureText(String(s)).width;
    }
    text(s) { this.textAt(s, 'left'); }
    text_right(s) { this.textAt(s, 'right'); }
    text_center(s) { this.textAt(s, 'center'); }
  }

  // ---- one run of a script
  const BUDGET = 3e6;  // instructions per call into Lua before it counts as an endless loop

  class Preview {
    // host: {print(text), error(text), engine(cmd, value), midi(...), param(id, value)}
    constructor(canvas, host, libUrl) {
      this.screen = new NornsScreen(canvas);
      this.host = host;
      this.lib = libUrl;
      this.L = null;
      this.timer = null;
      this.prelude = null;
    }
    async load() {
      if (!this.prelude) {
        const r = await fetch('preview_norns.lua');
        this.prelude = await r.text();
      }
      if (document.fonts) await document.fonts.load('8px norns').catch(() => {});
    }
    running() { return this.L !== null; }
    stop() {
      if (this.timer) clearInterval(this.timer);
      this.timer = null;
      this.L = null;
    }
    async start(code) {
      this.stop();
      await this.load();
      this.screen.reset();
      const L = lauxlib.luaL_newstate();
      lualib.luaL_openlibs(L);
      lauxlib.luaL_requiref(L, to_luastring('js'), interop.luaopen_js, 0);
      lua.lua_pop(L, 1);
      interop.push(L, this.screen);
      lua.lua_setglobal(L, to_luastring('__screen'));
      interop.push(L, this.host);
      lua.lua_setglobal(L, to_luastring('__host'));
      lua.lua_pushstring(L, to_luastring(this.lib));
      lua.lua_setglobal(L, to_luastring('__lib'));
      // a count hook stops a script that never waits instead of freezing the page
      let used = 0;
      this.resetBudget = () => { used = 0; };
      lua.lua_sethook(L, (L) => {
        used += 1000;
        if (used > BUDGET) {
          // without luaL_error's position: it would name the preview, not the script
          lua.lua_pushstring(L, to_luastring('the script ran without pausing for too long (an endless loop without "wait"?)'));
          lua.lua_error(L);
        }
      }, lua.LUA_MASKCOUNT, 1000);
      this.L = L;
      if (!this.chunk(this.prelude, '=preview_norns') || !this.chunk(code, '=script') || !this.call('_preview_init')) {
        return false;
      }
      const t0 = performance.now();
      this.timer = setInterval(() => this.call('_preview_tick', (performance.now() - t0) / 1000), 1000 / 60);
      return true;
    }
    fail(msg) {
      this.stop();
      this.host.error(msg);
    }
    message(L) {
      const m = lua.lua_tojsstring(L, -1);
      lua.lua_pop(L, 1);
      return m;
    }
    chunk(code, name) {
      const L = this.L;
      if (!L) return false;
      this.resetBudget();
      if (lauxlib.luaL_loadbuffer(L, to_luastring(code), null, to_luastring(name)) !== lua.LUA_OK ||
          lua.lua_pcall(L, 0, 0, 0) !== lua.LUA_OK) {
        this.fail(this.message(L));
        return false;
      }
      return true;
    }
    call(name, ...args) {
      const L = this.L;
      if (!L) return false;
      this.resetBudget();
      lua.lua_getglobal(L, to_luastring(name));
      // keys and encoders are whole numbers in norns (math.type "integer"), clock time is not
      for (const a of args) Number.isInteger(a) ? lua.lua_pushinteger(L, a) : lua.lua_pushnumber(L, a);
      if (lua.lua_pcall(L, args.length, 0, 0) !== lua.LUA_OK) {
        this.fail(this.message(L));
        return false;
      }
      return true;
    }
    key(n, z) { this.call('_preview_key', n, z); }
    enc(n, d) { this.call('_preview_enc', n, d); }
  }

  root.VerdandiPreview = Preview;
}(this));
