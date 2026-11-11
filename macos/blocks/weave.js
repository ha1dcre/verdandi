// weave.js: a Weave patch (the patcher's object) -> the patch text the engine builds, the norns
// parameters of every module param, and the Lua that sets both up in a script.
// browser: window.VerdandiWeave; node: module.exports. the catalog is macos/blocks/weave.json.
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory();
  else root.VerdandiWeave = factory();
}(this, function () {
  'use strict';

  function index(catalog) {
    const types = {};
    for (const d of catalog.voice) types[d.type] = d;
    for (const d of catalog.fx) types[d.type] = d;
    return types;
  }

  const num = v => String(Number(Number(v).toFixed(6)));

  function valuesOf(def, given) {
    const out = {};
    for (const p of def.params) {
      const v = given && given[p.name] !== undefined ? Number(given[p.name]) : p.default;
      out[p.name] = p.options ? Math.max(0, Math.min(p.options.length - 1, Math.round(v)))
        : Math.max(p.min, Math.min(p.max, p.step ? Math.round(v) : v));
    }
    return out;
  }

  // the patch with the fixed modules in place and every param filled in
  function normalize(patch, catalog) {
    const types = index(catalog);
    const p = patch || {};
    const modules = (p.modules || []).filter(m => types[m.type] && catalog.voice.includes(types[m.type]));
    for (const fixed of ['voice', 'out']) {
      if (!modules.some(m => m.id === fixed)) modules.push({id: fixed, type: fixed, params: {}});
    }
    const ids = new Set(modules.map(m => m.id));
    const jack = (ref, side) => {
      const [id, name] = String(ref).split('.');
      const m = modules.find(x => x.id === id);
      if (!m) return false;
      return types[m.type][side].some(j => j.name === name);
    };
    return {
      enabled: p.enabled !== false,
      modules: modules.map(m => ({id: m.id, type: m.type, params: valuesOf(types[m.type], m.params)})),
      cables: (p.cables || []).filter(c => jack(c.from, 'outputs') && jack(c.to, 'inputs') &&
        ids.has(c.from.split('.')[0]) && ids.has(c.to.split('.')[0])),
      fx: (p.fx || []).filter(f => types[f.type] && f.type !== 'main').map(f => ({id: f.id, type: f.type, params: valuesOf(types[f.type], f.params)})),
      main: valuesOf(types.main, p.main),
    };
  }

  function text(patch, catalog) {
    const n = normalize(patch, catalog);
    const kv = params => Object.keys(params).map(k => k + '=' + num(params[k])).join(' ');
    const lines = [];
    for (const m of n.modules) lines.push(('m ' + m.id + ' ' + m.type + ' ' + kv(m.params)).trim());
    for (const c of n.cables) lines.push('c ' + c.from + ' ' + c.to);
    for (const f of n.fx) lines.push(('x ' + f.id + ' ' + f.type + ' ' + kv(f.params)).trim());
    lines.push(('m main main ' + kv(n.main)).trim());
    return lines.join('\n') + '\n';
  }

  // one norns parameter per module param: id osc1_tune, name "osc1 tune", engine key osc1.tune
  function params(patch, catalog) {
    const types = index(catalog);
    const n = normalize(patch, catalog);
    const out = [];
    const add = (id, def, values) => {
      for (const p of def.params) {
        const entry = {id: id + '_' + p.name, name: id + ' ' + p.label, key: id + '.' + p.name, value: values[p.name]};
        if (p.options) Object.assign(entry, {options: p.options});
        else Object.assign(entry, {min: p.min, max: p.max, warp: p.warp || 'lin', units: p.units || '', step: p.step || 0});
        out.push(entry);
      }
    };
    for (const m of n.modules) add(m.id, types[m.type], m.params);
    for (const f of n.fx) add(f.id, types[f.type], f.params);
    add('main', types.main, n.main);
    return out;
  }

  // [label, key] for the "set sound ... to" blocks
  function choices(patch, catalog) {
    return params(patch, catalog).map(p => [p.name, p.key]);
  }

  const luaStr = s => '"' + String(s).replace(/\\/g, '\\\\').replace(/"/g, '\\"') + '"';

  // the Lua of a script that uses the sound: the patch, its parameters, _blk_sound_init()
  function lua(patch, catalog) {
    const ps = params(patch, catalog);
    const rows = ps.map(p => p.options
      ? '    {' + [luaStr(p.id), luaStr(p.name), luaStr(p.key), '{' + p.options.map(luaStr).join(', ') + '}', p.value + 1].join(', ') + '},'
      : '    {' + [luaStr(p.id), luaStr(p.name), luaStr(p.key), num(p.min), num(p.max), luaStr(p.warp), num(p.value), luaStr(p.units), num(p.step)].join(', ') + '},');
    return [
      '-- the sound (the patch of the "sound" page): its modules and cables for the Weave engine,',
      '-- and a parameter for every knob (PARAMETERS > sound)',
      '_blk_sound = {',
      '  patch = [[',
      text(patch, catalog).replace(/\n$/, ''),
      ']],',
      '  params = {',
      ...rows,
      '  },',
      '}',
      '',
      'function _blk_sound_init()',
      '  engine.patch(_blk_sound.patch)',
      '  params:add_group("sound", "sound", #_blk_sound.params)',
      '  for _, p in ipairs(_blk_sound.params) do',
      '    local key = p[3]',
      '    if type(p[4]) == "table" then',
      '      params:add_option(p[1], p[2], p[4], p[5])',
      '      params:set_action(p[1], function(v) engine.set(key, v - 1) end)',
      '    else',
      '      params:add_control(p[1], p[2], controlspec.new(p[4], p[5], p[6], p[9], p[7], p[8]))',
      '      params:set_action(p[1], function(v) engine.set(key, v) end)',
      '    end',
      '  end',
      'end',
    ].join('\n');
  }

  // a new script's sound: a plucked saw with a little reverb
  const DEFAULT_PATCH = {
    enabled: true,
    modules: [
      {id: 'voice', type: 'voice', x: 20, y: 40, params: {}},
      {id: 'osc1', type: 'osc', x: 250, y: 40, params: {wave: 2}},
      {id: 'env1', type: 'env', x: 250, y: 330, params: {a: 0.002, d: 0.4, s: 0.2, r: 0.4}},
      {id: 'filter1', type: 'filter', x: 480, y: 40, params: {cutoff: 900, mod: 3, track: 0.3}},
      {id: 'vca1', type: 'vca', x: 710, y: 40, params: {}},
      {id: 'out', type: 'out', x: 900, y: 40, params: {level: 0.5}},
    ],
    cables: [{from: 'osc1.out', to: 'filter1.in'}, {from: 'env1.out', to: 'filter1.mod'},
      {from: 'filter1.out', to: 'vca1.in'}, {from: 'env1.out', to: 'vca1.cv'}, {from: 'vca1.out', to: 'out.in'}],
    fx: [{id: 'reverb1', type: 'fx_reverb', params: {mix: 0.2}}],
    main: {input: 0, level: 1},
  };

  return {normalize, text, params, choices, lua, DEFAULT_PATCH};
}));
