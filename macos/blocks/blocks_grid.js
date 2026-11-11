// blocks_grid.js: the Grid blocks (a monome grid, or Verdandi's own on screen) and the arc.
// keys pressed and released, keys held, lights drawn 30 times a second; arc rings turned and lit.
// a kit of blocks.js: browser after blocks.js; node: require(...)(VerdandiBlocks).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory;
  else factory(root.VerdandiBlocks);
}(this, function (VB) {
  'use strict';
  const C = 100;
  const n = VB.num, b = VB.entry;
  const num = name => ({type: 'input_value', name, check: 'Number'});
  const ring = {type: 'field_dropdown', name: 'RING', options: [['1', '1'], ['2', '2'], ['3', '3'], ['4', '4']]};
  const drawsIn = (hat, what) => [[hat, 'procedures_defnoreturn', 'procedures_defreturn'], what];

  VB.kit({
    name: 'grid',
    hats: ['nv_grid_key', 'nv_grid_draw', 'nv_arc_turn', 'nv_arc_draw'],
    defs: [
      {type: 'nv_grid_key', message0: 'when a grid key is %1 %2 %3', args0: [
        {type: 'field_dropdown', name: 'STATE', options: [['pressed', '1'], ['released', '0']]},
        {type: 'input_dummy'}, {type: 'input_statement', name: 'DO'}], colour: C,
       tooltip: '"grid x" (1 to 16, left to right) and "grid y" (1 to 8, top down) say which'},
      {type: 'nv_grid_x', message0: 'grid x', output: 'Number', colour: C},
      {type: 'nv_grid_y', message0: 'grid y', output: 'Number', colour: C},
      {type: 'nv_grid_held', message0: 'grid key x %1 y %2 is held', args0: [num('X'), num('Y')],
       inputsInline: true, output: 'Boolean', colour: C},
      {type: 'nv_grid_draw', message0: 'draw the grid %1 %2', args0: [{type: 'input_dummy'},
        {type: 'input_statement', name: 'DO'}], colour: C,
       tooltip: '30 times a second, all lights off first: light what should be lit'},
      {type: 'nv_grid_led', message0: 'light grid x %1 y %2 brightness %3', args0: [num('X'), num('Y'), num('LEVEL')],
       inputsInline: true, previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'brightness 0 (off) to 15'},
      {type: 'nv_arc_turn', message0: 'when arc ring %1 turns %2 %3', args0: [ring,
        {type: 'input_dummy'}, {type: 'input_statement', name: 'DO'}], colour: C,
       tooltip: '"arc turn amount" says how far, positive clockwise'},
      {type: 'nv_arc_delta', message0: 'arc turn amount', output: 'Number', colour: C},
      {type: 'nv_arc_draw', message0: 'draw the arc %1 %2', args0: [{type: 'input_dummy'},
        {type: 'input_statement', name: 'DO'}], colour: C},
      {type: 'nv_arc_led', message0: 'light arc ring %1 light %2 brightness %3', args0: [ring, num('LED'), num('LEVEL')],
       inputsInline: true, previousStatement: null, nextStatement: null, colour: C, tooltip: '64 lights per ring, 1 at the top'},
      {type: 'nv_arc_segment', message0: 'light arc ring %1 from %2 to %3 (0 to 1) brightness %4', args0: [ring,
        num('FROM'), num('TO'), num('LEVEL')], inputsInline: true, previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'a stretch of the ring: from 0 to the value of a control draws a meter'},
    ],
    context: {
      nv_grid_x: [['nv_grid_key'], 'only works inside "when a grid key is ..."'],
      nv_grid_y: [['nv_grid_key'], 'only works inside "when a grid key is ..."'],
      nv_grid_led: drawsIn('nv_grid_draw', 'lights go inside "draw the grid"'),
      nv_arc_delta: [['nv_arc_turn'], 'only works inside "when arc ring ... turns"'],
      nv_arc_led: drawsIn('nv_arc_draw', 'lights go inside "draw the arc"'),
      nv_arc_segment: drawsIn('nv_arc_draw', 'lights go inside "draw the arc"'),
    },
    generators(F, ctx) {
      const {O, v, body} = ctx;
      const k = () => ctx.S().kits.grid;
      const list = (name, item) => { (k()[name] = k()[name] || []).push(item); };
      F.nv_grid_key = bl => { list('keys', {z: bl.getFieldValue('STATE'), body: body(bl)}); return ''; };
      F.nv_grid_x = () => ['_blk_gx', O.ATOMIC];
      F.nv_grid_y = () => ['_blk_gy', O.ATOMIC];
      F.nv_grid_held = bl => { k().grid = true; return ['(_blk_grid_held[math.floor(' + v(bl, 'X') + ') .. "," .. math.floor(' + v(bl, 'Y') + ')] == true)', O.ATOMIC]; };
      F.nv_grid_draw = bl => { list('draws', body(bl)); return ''; };
      F.nv_grid_led = bl => { k().grid = true; return '_blk_grid:led(math.floor(' + v(bl, 'X') + '), math.floor(' + v(bl, 'Y') + '), util.clamp(math.floor(' + v(bl, 'LEVEL') + '), 0, 15))\n'; };
      F.nv_arc_turn = bl => { list('turns', {ring: bl.getFieldValue('RING'), body: body(bl)}); return ''; };
      F.nv_arc_delta = () => ['_blk_ad', O.ATOMIC];
      F.nv_arc_draw = bl => { list('arcDraws', body(bl)); return ''; };
      F.nv_arc_led = bl => { k().arc = true; return '_blk_arc:led(' + bl.getFieldValue('RING') + ', math.floor(' + v(bl, 'LED') + '), util.clamp(math.floor(' + v(bl, 'LEVEL') + '), 0, 15))\n'; };
      F.nv_arc_segment = bl => {
        k().arc = true;
        return '_blk_arc:segment(' + bl.getFieldValue('RING') + ', util.clamp(' + v(bl, 'FROM') + ', 0, 1) * 2 * math.pi, util.clamp(' +
          v(bl, 'TO') + ', 0, 1) * 2 * math.pi + 0.001, util.clamp(math.floor(' + v(bl, 'LEVEL') + '), 0, 15))\n';
      };
    },
    initLast(s, I, indent) {
      const k = s.kits.grid, out = [];
      const grid = k.grid || k.keys || k.draws, arc = k.arc || k.turns || k.arcDraws;
      if (grid) {
        out.push(I + '_blk_grid = grid.connect()', I + '_blk_grid_held = {}',
          I + '_blk_grid.key = function(_blk_gx, _blk_gy, _blk_gz)',
          I + I + '_blk_grid_held[_blk_gx .. "," .. _blk_gy] = (_blk_gz == 1) or nil');
        for (const key of k.keys || []) {
          out.push(I + I + 'if _blk_gz == ' + key.z + ' then', indent(key.body, 2).replace(/\n$/, ''), I + I + 'end');
        }
        out.push(I + 'end');
      }
      if (arc) {
        out.push(I + '_blk_arc = arc.connect()', I + '_blk_arc.delta = function(_blk_ar, _blk_ad)');
        for (const t of k.turns || []) {
          out.push(I + I + 'if _blk_ar == ' + t.ring + ' then', indent(t.body, 2).replace(/\n$/, ''), I + I + 'end');
        }
        out.push(I + 'end');
      }
      if (k.draws || k.arcDraws) {
        out.push(I + 'clock.run(function()', I + I + 'while true do', I + I + I + 'clock.sleep(1 / 30)');
        if (k.draws) out.push(I + I + I + '_blk_grid_redraw()');
        if (k.arcDraws) out.push(I + I + I + '_blk_arc_redraw()');
        out.push(I + I + 'end', I + 'end)');
      }
      return out;
    },
    functions(s, I) {
      const k = s.kits.grid, out = [];
      if (k.draws) {
        out.push('function _blk_grid_redraw()', I + '_blk_grid:all(0)');
        for (const d of k.draws) if (d) out.push(d.replace(/\n$/, ''));
        out.push(I + '_blk_grid:refresh()', 'end', '');
      }
      if (k.arcDraws) {
        out.push('function _blk_arc_redraw()', I + '_blk_arc:all(0)');
        for (const d of k.arcDraws) if (d) out.push(d.replace(/\n$/, ''));
        out.push(I + '_blk_arc:refresh()', 'end', '');
      }
      return out;
    },
    reserved: '_blk_gx,_blk_gy,_blk_gz,_blk_ar,_blk_ad',
    toolbox: [{name: 'Grid', colour: C, contents: [
      b('nv_grid_key'), b('nv_grid_x'), b('nv_grid_y'), b('nv_grid_held', {X: n(1), Y: n(1)}),
      b('nv_grid_draw'), b('nv_grid_led', {X: n(1), Y: n(1), LEVEL: n(15)}),
      b('nv_arc_turn'), b('nv_arc_delta'), b('nv_arc_draw'),
      b('nv_arc_led', {LED: n(1), LEVEL: n(15)}), b('nv_arc_segment', {FROM: n(0), TO: n(0.5), LEVEL: n(10)})]}],
  });
}));
