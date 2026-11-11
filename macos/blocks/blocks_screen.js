// blocks_screen.js: the screen design (the "screen" page) in a script, and blocks that steer it:
// pages, values set by the script, elements shown and hidden, note animations set off.
// a kit of blocks.js: browser after blocks.js; node: require(...)(VerdandiBlocks).
(function (root, factory) {
  if (typeof module === 'object' && module.exports) module.exports = factory;
  else factory(root.VerdandiBlocks);
}(this, function (VB) {
  'use strict';
  const C = 160;
  const n = VB.num, b = VB.entry;
  const num = name => ({type: 'input_value', name, check: 'Number'});

  // the layers of the design and their number props, for the dropdowns
  let source = () => null;
  const layerOptions = () => {
    const s = source();
    const c = s ? s.screen.layerChoices(s.design(), s.catalog) : [];
    return c.length ? c : [['(the screen page is empty)', 'none']];
  };
  const propOptions = function () {
    const s = source();
    const block = this && this.getSourceBlock && this.getSourceBlock();
    const id = block ? block.getFieldValue('ID') : null;
    const c = s && id ? s.screen.propChoices(s.design(), s.catalog, id) : [];
    return c.length ? c : [['x', 'x'], ['y', 'y'], ['brightness', 'level']];
  };
  const layer = {type: 'field_dropdown', name: 'ID', options: layerOptions};
  const prop = {type: 'field_dropdown', name: 'PROP', options: propOptions};

  VB.kit({
    name: 'screen',
    defs: [
      {type: 'nv_design_page', message0: 'show page %1 of the screen design', args0: [num('PAGE')],
       previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'elements on page 0 always show; the others only on their page'},
      {type: 'nv_design_page_get', message0: 'screen design page', output: 'Number', colour: C},
      {type: 'nv_design_set', message0: 'set screen element %1 %2 to %3', args0: [layer, prop, num('VALUE')],
       inputsInline: true, previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'the script decides this value now (it stops moving by itself)'},
      {type: 'nv_design_free', message0: 'let screen element %1 %2 move by itself again', args0: [layer, prop],
       previousStatement: null, nextStatement: null, colour: C},
      {type: 'nv_design_show', message0: '%1 screen element %2', args0: [
        {type: 'field_dropdown', name: 'SHOW', options: [['show', 'show'], ['hide', 'hide']]}, layer],
       previousStatement: null, nextStatement: null, colour: C},
      {type: 'nv_design_note', message0: 'set off the note animations', previousStatement: null, nextStatement: null, colour: C,
       tooltip: 'what moves "on each note" moves now, as if a note played'},
    ],
    generators(F, ctx) {
      const o = ctx.opts;
      source = () => o.screen && o.getDesign ? {screen: o.screen, catalog: o.screenCatalog, design: o.getDesign} : null;
      const used = () => { ctx.S().kits.screen.used = true; };
      const {v, O, luaString} = ctx;
      F.nv_design_page = bl => { used(); return '_blk_vis.page = math.floor(' + v(bl, 'PAGE') + ')\n'; };
      F.nv_design_page_get = () => { used(); return ['_blk_vis.page', O.HIGH]; };
      F.nv_design_set = bl => { used(); return '_blk_vis.set(' + luaString(bl.getFieldValue('ID')) + ', ' + luaString(bl.getFieldValue('PROP')) + ', ' + v(bl, 'VALUE') + ')\n'; };
      F.nv_design_free = bl => { used(); return '_blk_vis.set(' + luaString(bl.getFieldValue('ID')) + ', ' + luaString(bl.getFieldValue('PROP')) + ', nil)\n'; };
      F.nv_design_show = bl => {
        used();
        return '_blk_vis.hide[' + luaString(bl.getFieldValue('ID')) + '] = ' + (bl.getFieldValue('SHOW') === 'hide' ? 'true' : 'nil') + '\n';
      };
      F.nv_design_note = () => { used(); return '_blk_vis.note_on()\n'; };
    },
    // the design's Lua, once per generation
    design(s) {
      const k = s.kits.screen;
      if (k.lua === undefined) {
        const o = s.opts;
        k.lua = o.screen && o.getDesign ? o.screen.lua(o.getDesign(), o.screenCatalog) : null;
      }
      return k.lua;
    },
    functions(s) {
      const d = this.design(s);
      if (!d || (!d.uses.layers && !s.kits.screen.used)) return [];
      s.kits.screen.on = true;
      return [d.runtime, '', d.draw, ''];
    },
    redrawFirst(s, I) {
      const d = this.design(s);
      return d && d.uses.layers ? [I + '_blk_design()'] : [];
    },
    initLast(s, I) {
      const d = this.design(s);
      if (!d || !d.uses.level) return [];
      return [I + 'local level = poll.set("amp_out_l", function(v) _blk_vis.level = math.min(1, v * 3) end)',
        I + 'level.time = 1 / 30', I + 'level:start()'];
    },
    toolbox: [{name: 'Screen', colour: C, contents: [
      b('nv_design_page', {PAGE: n(1)}), b('nv_design_page_get'), b('nv_design_set', {VALUE: n(64)}),
      b('nv_design_free'), b('nv_design_show'), b('nv_design_note')]}],
  });
}));
