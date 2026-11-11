// guide.js: the guide of the script builder: lessons in small steps, each one shows where its
// blocks are ("show me" opens the toolbox category) and notices by itself when it is done.
// usage: const g = new VerdandiGuide(el, {workspace, getPatch, getView, showView, newDocument, status});
// g.open(lesson, step), g.toggle(), g.update() after every change, g.ran(), g.saved(), g.seen().
(function (root) {
  'use strict';
  const STORE = 'verdandi-guide';

  // the lessons. text may use <b>block or button names</b> and <code>values</code>
  const LESSONS = [
    {title: 'Your first sound', steps: [
      {text: 'This guide builds a script with you, one step at a time. Each step checks itself: when you have done it, the next one comes. It starts with an empty workspace.',
       action: ['start with an empty workspace', (g) => g.ctx.newDocument() && (g.flags.fresh = true)], check: h => h.flags.fresh},
      {text: 'A script is made of <b>event blocks</b>: they say <i>when</i> something happens. Open <b>Events</b> on the left and drag <b>when the script starts</b> into the workspace.',
       category: 'Events', check: h => h.has('nv_init')},
      {text: 'Open <b>Sound</b> and drag <b>play note 60 velocity 100 for 0.5 beats</b> into <b>when the script starts</b>, so it clicks into place inside it.',
       category: 'Sound', check: h => h.inside('nv_play_note', 'nv_init')},
      {text: 'Under the screen preview on the right you see the note (♪ C4). The preview is silent. To hear it: type a <b>name</b> at the top, then press <b>run</b>, with Verdandi running.',
       check: h => h.flags.ran},
    ]},
    {title: 'A melody that repeats', steps: [
      {text: 'From <b>Events</b>, drag <b>every 1/4 beat</b> into the workspace. Everything inside it repeats in time with the clock (tempo: PARAMETERS > CLOCK on the norns).',
       category: 'Events', check: h => h.has('nv_every')},
      {text: 'Open <b>Sequencing</b> and put <b>count step from 1 to 16</b> inside it: each time round, <code>step</code> goes up by one.',
       category: 'Sequencing', check: h => h.inside('nv_counter', 'nv_every')},
      {text: 'Put <b>play note</b> under the counter. Into its note slot drop <b>step &hellip; of 60 . 63 67</b> (Sequencing): the notes of a melody, a dot is a rest.',
       category: 'Sequencing', check: h => h.inside('nv_play_note', 'nv_every') && h.has('nv_pattern_value')},
      {text: 'Click into the text and change the numbers: 60 is middle C, 72 an octave higher. Watch the notes under the preview change. <b>next</b> when you like it.'},
    ]},
    {title: 'Make the sound yours', steps: [
      {text: 'Switch to <b>sound</b> at the top. This page is the instrument your notes play: <b>modules</b> joined by <b>cables</b>.',
       view: 'sound', check: h => h.view === 'sound'},
      {text: 'Pick a preset from the menu in the toolbar: try <b>pad</b> or <b>bell</b>. Each one is just modules and cables you can change.',
       view: 'sound', check: h => h.presetPicked()},
      {text: 'Add an <b>lfo</b> (from the modulators) and drag a cable from its <b>out</b> (right side) to the <b>mod</b> input (left side) of the <b>filter</b>: the filter now moves by itself.',
       view: 'sound', check: h => h.cable('lfo', 'filter', 'mod')},
      {text: 'Every knob is a control in PARAMETERS > sound on the norns, too. Back on <b>blocks</b>, <b>set sound &hellip; to</b> (Sound) changes any of them from your script.',
       view: 'blocks', category: 'Sound', check: h => h.view === 'blocks' && (h.has('nv_sound_set') || h.has('nv_sound_change'))},
      {text: 'Press <b>run</b>. While the script runs, the keyboard at the bottom of the sound page and every knob play live in Verdandi.',
       check: h => h.flags.ran},
    ]},
    {title: 'Turn it with an encoder', steps: [
      {text: 'From <b>Controls</b>, put <b>add control cutoff</b> into <b>when the script starts</b>. A control has a range and shows in PARAMETERS; MIDI can move it.',
       category: 'Controls', check: h => h.inside('nv_param_add', 'nv_init')},
      {text: 'From <b>Events</b>, drag <b>when encoder E2 turns</b>, and put <b>change control cutoff by turn amount</b> (Controls) into it.',
       category: 'Controls', check: h => h.inside('nv_param_change', 'nv_enc')},
      {text: 'Use <b>control cutoff</b> wherever a number goes, for example in <b>set sound filter1 cutoff to</b>. Then turn E2 on the preview: scroll over it, or click - and +.',
       category: 'Controls', check: h => h.has('nv_param_get')},
    ]},
    {title: 'Draw the screen', steps: [
      {text: 'From <b>Events</b>, drag <b>draw the screen</b>. It draws 15 times a second, each time from black.',
       category: 'Events', check: h => h.has('nv_redraw')},
      {text: 'From <b>Screen</b>, put <b>write hello</b> inside it. x goes 0 to 127 across, y 0 to 63 down, brightness 0 to 15.',
       category: 'Screen', check: h => h.inside('nv_screen_text', 'nv_redraw')},
      {text: 'From <b>Widgets</b>, add <b>dial for control cutoff</b>: the norns dial, with the name and value of the control.',
       category: 'Widgets', check: h => h.inside('nv_ui_dial', 'nv_redraw')},
      {text: 'Make something move: put <b>frame number</b> or <b>beats since start</b> (Screen) into a size or a position. <b>remainder of</b> and <b>sin</b> (Math) make it go round.',
       category: 'Screen', check: h => h.has('nv_frame') || h.has('nv_beats')},
    ]},
    {title: 'Make it move: the screen page', steps: [
      {text: 'Switch to <b>screen</b> at the top. Here you put things on the norns screen and make them move, without blocks: the big screen shows what the norns will show.',
       view: 'screen', check: h => h.view === 'screen'},
      {text: 'Add <b>rings on notes</b> (moving). Every note your script plays sends a ring out. Press <b>test note</b> to see one without playing.',
       view: 'screen', check: h => h.layer('rings')},
      {text: 'Add a <b>text</b>, then set its <b>brightness</b> to move <b>with the beat</b>: it now breathes in time. Try <code>{note}</code> or <code>{tempo}</code> in the text.',
       view: 'screen', check: h => h.animated('text', 'beat')},
      {text: 'Add a <b>meter</b> (values): it moves <b>with the sound level</b> already. Drag things around on the big screen, and resize them by their corners.',
       view: 'screen', check: h => h.layer('bar')},
      {text: 'Any number can move: with the beat, over time, on each note, with the sound level, with a control or a looper. Blocks (Screen) can change pages, hide elements and set values, too.'},
    ]},
    {title: 'A looper', steps: [
      {text: 'From <b>Looper</b>, put <b>set up looper 1</b> into <b>when the script starts</b>. It records the audio input (or the sound, or both).',
       category: 'Looper', check: h => h.inside('nv_looper_setup', 'nv_init')},
      {text: 'Drag <b>when key K2 is pressed</b> with <b>looper 1: record a new loop</b> inside, and <b>when key K3 is pressed</b> with <b>looper 1: finish recording</b>.',
       category: 'Looper', check: h => h.count('nv_looper_do', 'nv_key') >= 2},
      {text: 'In <b>draw the screen</b>, add <b>waveform of looper 1</b>: the loop as a picture, with its playhead.',
       category: 'Looper', check: h => h.inside('nv_looper_wave', 'nv_redraw')},
      {text: 'Run it, play into the input, press K2, then K3: the time in between is your loop. Then try <b>looper 1 speed</b> <code>-1</code> (backwards) or <code>0.5</code> on another key.',
       check: h => h.flags.ran},
    ]},
    {title: 'Play it from a grid', steps: [
      {text: 'From <b>Grid</b>, drag <b>when a grid key is pressed</b> and put <b>start note</b> (Sound) in it, with the note <code>40 + grid x + grid y &times; 5</code> (Math, Grid).',
       category: 'Grid', check: h => h.inside('nv_note_on', 'nv_grid_key')},
      {text: 'Add <b>when a grid key is released</b> with <b>stop note</b> and the same number, so the note ends when the key does.',
       category: 'Grid', check: h => h.inside('nv_note_off', 'nv_grid_key')},
      {text: 'Drag <b>draw the grid</b> and light keys with <b>light grid x y brightness</b>. The preview shows a grid under the screen: click its keys.',
       category: 'Grid', check: h => h.inside('nv_grid_led', 'nv_grid_draw')},
    ]},
    {title: 'Save, share, go further', steps: [
      {text: 'Give it a <b>name</b> and press <b>save</b>: the script is <code>code/&lt;name&gt;/&lt;name&gt;.lua</code> in maiden, its blocks next to it. <b>open</b> brings it back here.',
       check: h => h.flags.saved},
      {text: 'The <b>lua</b> tab on the right is the script you made, line by line: a way to learn Lua, too. On a norns (not Verdandi) the sound needs the Weave engine in dust as well: copy <code>Engine_Weave.sc</code> and <code>WeaveGraph.sc</code> to <code>code/weave/lib/</code>.'},
      {text: 'Take the examples apart: <b>open</b> has a looper, drums, a grid keyboard and more. Hover over any block: its tooltip says what it does. Have fun.'},
    ]},
  ];

  class Guide {
    constructor(el, ctx) {
      this.el = el;
      this.ctx = ctx;
      this.flags = {};
      this.lesson = 0;
      this.step = 0;
      this.menu = false;
      this.timer = null;
      try { this.store = JSON.parse(localStorage.getItem(STORE) || '{}'); } catch (e) { this.store = {}; }
      this.store.finished = this.store.finished || [];
      el.addEventListener('keydown', e => { if (e.key === 'Escape') this.close(); });
    }
    save() { try { localStorage.setItem(STORE, JSON.stringify(this.store)); } catch (e) { /* fine */ } }
    seen() { return !!this.store.seen; }
    open(lesson, step) {
      this.store.seen = true;
      if (lesson !== undefined) { this.lesson = Math.max(0, Math.min(LESSONS.length - 1, lesson)); this.step = step || 0; this.menu = false; }
      else if (this.store.at) { [this.lesson, this.step] = this.store.at; }
      this.save();
      this.el.hidden = false;
      this.render();
    }
    close() { this.el.hidden = true; }
    toggle() { this.el.hidden ? this.open() : this.close(); }
    ran() { this.flags.ran = true; this.update(); }
    saved() { this.flags.saved = true; this.update(); }

    // what the checks look at
    helper() {
      const ws = this.ctx.workspace;
      const blocks = ws.getAllBlocks(false).filter(b => b.isEnabled());
      const within = (b, parent) => { let p = b.getSurroundParent(); while (p) { if (p.type === parent) return true; p = p.getSurroundParent(); } return false; };
      const patch = this.ctx.getPatch() || {modules: [], cables: []};
      const design = (this.ctx.getDesign && this.ctx.getDesign()) || {layers: []};
      const typeOf = ref => { const m = (patch.modules || []).find(x => x.id === String(ref).split('.')[0]); return m ? m.type : null; };
      return {
        flags: this.flags,
        view: this.ctx.getView(),
        has: type => blocks.some(b => b.type === type),
        inside: (type, parent) => blocks.some(b => b.type === type && within(b, parent)),
        count: (type, parent) => blocks.filter(b => b.type === type && within(b, parent)).length,
        presetPicked: () => {
          const types = (patch.modules || []).map(m => m.type).sort().join(',');
          return types !== 'env,filter,osc,out,vca,voice';
        },
        layer: type => (design.layers || []).some(l => l.type === type),
        animated: (type, kind) => (design.layers || []).some(l => l.type === type &&
          Object.values(l.props || {}).some(p => p && p.anim && p.anim.kind === kind)),
        cable: (from, to, jack) => (patch.cables || []).some(c => typeOf(c.from) === from && typeOf(c.to) === to && c.to.endsWith('.' + jack)),
      };
    }

    current() { return LESSONS[this.lesson].steps[this.step]; }
    done() {
      const s = this.current();
      try { return !!(s.check && s.check(this.helper())); } catch (e) { return false; }
    }
    update() {
      if (this.el.hidden || this.menu) return;
      const was = this.el.dataset.done === '1';
      this.render();
      // done just now: on to the next step, after a moment to see the tick
      if (!was && this.done() && this.current().check) {
        clearTimeout(this.timer);
        this.timer = setTimeout(() => this.next(), 1100);
      }
    }
    go(lesson, step) {
      clearTimeout(this.timer);
      this.lesson = lesson;
      this.step = step;
      this.menu = false;
      this.store.at = [lesson, step];
      this.save();
      const s = this.current();
      if (s.view && this.ctx.getView() !== s.view && s.view === 'sound') this.ctx.showView('sound');
      this.render();
    }
    next() {
      const steps = LESSONS[this.lesson].steps;
      if (this.step < steps.length - 1) return this.go(this.lesson, this.step + 1);
      if (!this.store.finished.includes(this.lesson)) this.store.finished.push(this.lesson);
      this.save();
      if (this.lesson < LESSONS.length - 1) {
        this.ctx.status('lesson done: ' + LESSONS[this.lesson].title, 'ok');
        return this.go(this.lesson + 1, 0);
      }
      this.menu = true;
      this.render();
    }
    back() {
      if (this.step > 0) this.go(this.lesson, this.step - 1);
      else if (this.lesson > 0) this.go(this.lesson - 1, LESSONS[this.lesson - 1].steps.length - 1);
    }
    showMe() {
      const s = this.current();
      if (s.view && this.ctx.getView() !== s.view) this.ctx.showView(s.view);
      if (s.category) {
        if (this.ctx.getView() !== 'blocks') this.ctx.showView('blocks');
        const tb = this.ctx.workspace.getToolbox();
        const item = tb && tb.getToolboxItems().find(i => i.getName && i.getName() === s.category);
        if (item) tb.setSelectedItem(item);
      }
    }

    render() {
      const el = this.el;
      el.textContent = '';
      const h = (tag, cls, text) => { const e = document.createElement(tag); if (cls) e.className = cls; if (text !== undefined) e.textContent = text; return e; };
      const button = (label, fn, cls) => { const b = h('button', cls, label); b.type = 'button'; b.addEventListener('click', fn); return b; };
      const head = h('header');
      const close = button('×', () => this.close(), 'close');
      close.setAttribute('aria-label', 'close the guide');
      if (this.menu) {
        head.append(h('h2', null, 'Guide: the lessons'), close);
        el.append(head, h('p', null, 'Each lesson builds on the one before; any can be started on its own.'));
        const ol = h('ol');
        LESSONS.forEach((l, i) => {
          const li = h('li', this.store.finished.includes(i) ? 'finished' : '');
          li.append(button(l.title, () => this.go(i, 0)));
          ol.append(li);
        });
        el.append(ol);
        el.dataset.done = '';
        return;
      }
      const lesson = LESSONS[this.lesson], s = this.current();
      head.append(h('h2', null, lesson.title), h('span', 'count', (this.step + 1) + ' / ' + lesson.steps.length), close);
      const text = h('p', 'step-text');
      text.innerHTML = s.text;  // the lessons' own text, above: no user input
      el.append(head, text);
      const done = this.done();
      el.dataset.done = done ? '1' : '';
      if (s.check) el.append(h('p', done ? 'done' : 'waiting', done ? '✓ done' : 'waiting for you …'));
      const row = h('div', 'row');
      if (s.action) row.append(button(s.action[0], () => { s.action[1](this); this.update(); }, 'primary'));
      if (s.category || s.view) row.append(button('show me', () => this.showMe()));
      row.append(h('span', 'spacer'));
      if (this.lesson > 0 || this.step > 0) row.append(button('back', () => this.back()));
      row.append(button(this.step === lesson.steps.length - 1 ? 'finish lesson' : 'next', () => this.next(), done || !s.check ? 'primary' : ''));
      el.append(row);
      const all = h('div', 'row');
      all.append(button('all lessons', () => { this.menu = true; this.render(); }));
      el.append(all);
    }
  }
  Guide.LESSONS = LESSONS;
  root.VerdandiGuide = Guide;
}(this));
