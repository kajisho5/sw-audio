/* SWINGBY browser trial: the page's "native side" (the plug-in window's messages, ui/in07/core.js post) answered in the browser.
   The values live in the page (as in the plug-in's window) and go to the engine in an AudioWorklet (worklet.js, swingby.wasm) once the
   audio is started; until then they wait. Presets: the factory list (the values precomputed: PRESET_VALUES); saving, user presets, MIDI
   learn and the licence are the plug-in's. Keys: the computer keyboard (A W S E D F T G Y H U J K O L, Z / X octave) and MIDI keyboards
   (Web MIDI, where the browser has it). */
(function () {
  'use strict';
  const W = window.SWWEB = { node: null, ctx: null, queue: [], notes: 0, held: new Set(), bpm: 120, peak: 0, started: false };
  const send = m => { if (W.node) W.node.port.postMessage(m); else W.queue.push(m); };
  W.send = send;
  const MORPH = ['in07.morph.on', 'in07.morph.x', 'in07.morph.y', 'in07.morph.b', 'in07.morph.c', 'in07.morph.d'];
  let store = null;
  try { store = window.localStorage; } catch (e) { store = null; }
  const saved = k => { try { return store ? store.getItem('swingby.' + k) : null; } catch (e) { return null; } };
  const keep = (k, v) => { try { if (store) store.setItem('swingby.' + k, v); } catch (e) { /* a private window */ } };
  if (window.SWBOOT && window.SWBOOT.settings) {
    // the window's look: the one chosen here before, else the page's theme (the viewer's choice, or the system's)
    const dt = document.documentElement.getAttribute('data-theme');
    let sys = 'dark';
    try { if (window.matchMedia('(prefers-color-scheme: light)').matches) sys = 'light'; } catch (e) { /* no media queries */ }
    window.SWBOOT.settings.theme = dt === 'light' || dt === 'dark' ? dt : sys;
    ['motion', 'theme', 'zoom'].forEach(k => { const v = saved(k); if (v) window.SWBOOT.settings[k] = v; });
  }
  W.noteOn = (key, vel) => { W.notes++; W.held.add(key); send({ t: 'on', key, vel }); };
  W.noteOff = key => { W.held.delete(key); send({ t: 'off', key }); };
  window.SWMOCK = function (m) {
    const SW = window.SW, parts = m.split(' ');
    const later = f => setTimeout(f, 0);
    switch (parts[0]) {
      case 's': send({ t: 'set', id: Number(parts[1]), v: Number(parts[2]) }); return;
      case 'n': W.noteOn(Number(parts[1]), Number(parts[2]) || 0.8); return;
      case 'o': W.noteOff(Number(parts[1])); return;
      case 'p': later(() => SW.update(SW.P.values().slice(), { bpm: W.bpm, playing: false, beat: 0, note: W.notes, held: W.held.size, learn: -1, cc: [-1, -1, -1, -1, -1, -1, -1, -1] })); return;
      case 'b': case 'e': return;
    }
    if (parts[0] !== 'c') return;
    const name = parts[1], arg = parts[2];
    if (name === 'factory') later(() => {
      const i = Number(arg), v = i >= 0 ? window.PRESET_VALUES[i] : window.INIT_VALUES;
      if (!v) { SW.reply('loaded', { error: 'no such preset' }); return; }
      const vv = v.slice(), sel = SW.P.idx('in07.preset');
      vv[sel] = i + 1;
      MORPH.forEach(id => { const k = SW.P.idx(id); vv[k] = SW.P.get(id); });   // a preset keeps the morph (as the plug-in)
      SW.update(vv);
      send({ t: 'preset', index: i });
      SW.reply('loaded', { kind: i >= 0 ? 'factory' : 'init', index: i });
    });
    if (name === 'users') later(() => SW.reply('users', { folder: 'Saved presets are kept by the plug-in (VST3 / AU / CLAP)', list: [] }));
    if (name === 'user') later(() => SW.reply('loaded', { error: 'User presets are opened by the plug-in' }));
    if (name === 'save') later(() => SW.reply('saved', { error: 'Saving is in the plug-in: this is the browser trial' }));
    if (name === 'lic') later(() => SW.reply('licence', { state: 'demo', machine: 'BROWSER TRIAL — the licence is the plug-in\'s' }));
    if (name === 'licfile') later(() => SW.reply('licence', { state: 'demo', machine: 'BROWSER TRIAL', message: 'The licence is activated in the plug-in', ok: false }));
    if (name === 'set') { try { keep(arg, atob(parts[3] || '')); } catch (e) { /* not base64 */ } }
  };

  // ---- the audio: started by a click (browsers allow sound only after one)
  W.start = async function () {
    if (W.started) return;
    W.started = true;
    const Ctx = window.AudioContext || window.webkitAudioContext;
    const ctx = W.ctx = new Ctx({ latencyHint: 'interactive' });
    const bytes = await (await fetch('swingby.wasm')).arrayBuffer();
    await ctx.audioWorklet.addModule('worklet.js');
    const node = new AudioWorkletNode(ctx, 'swingby', { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2], processorOptions: { bytes } });
    await new Promise(res => { node.port.onmessage = ev => { if (ev.data.t === 'ready') res(); }; });
    node.port.onmessage = ev => { if (ev.data.t === 'meter') { W.peak = ev.data.peak; W.voices = ev.data.notes; } };
    node.connect(ctx.destination);
    W.node = node;
    const SW = window.SW;
    // the sound the page shows: its preset as one patch, then every value as the page has it, then what waited
    const cur = SW.current && SW.current.kind === 'factory' ? SW.current.index : -1;
    node.port.postMessage({ t: 'preset', index: cur });
    node.port.postMessage({ t: 'values', values: SW.P.values().slice(), skip: SW.P.idx('in07.preset') });
    node.port.postMessage({ t: 'tempo', bpm: W.bpm });
    W.queue.splice(0).forEach(m => node.port.postMessage(m));
    if (ctx.state !== 'running') await ctx.resume();
  };
  W.tempo = bpm => { W.bpm = Math.max(30, Math.min(300, bpm)); send({ t: 'tempo', bpm: W.bpm }); };

  // ---- the computer keyboard
  const MAP = { a: 0, w: 1, s: 2, e: 3, d: 4, f: 5, t: 6, g: 7, y: 8, h: 9, u: 10, j: 11, k: 12, o: 13, l: 14, p: 15, ';': 16 };
  W.octave = 4;
  const down = new Map();
  const typing = el => el && (el.tagName === 'INPUT' || el.tagName === 'TEXTAREA' || el.isContentEditable);
  window.addEventListener('keydown', ev => {
    if (ev.repeat || ev.metaKey || ev.ctrlKey || ev.altKey || typing(document.activeElement)) return;
    const k = ev.key.toLowerCase();
    if (k === 'z' || k === 'x') { W.octave = Math.max(1, Math.min(7, W.octave + (k === 'z' ? -1 : 1))); if (W.onOctave) W.onOctave(W.octave); return; }
    if (!(k in MAP) || down.has(k)) return;
    if (!W.started) W.start();
    const key = 12 * (W.octave + 1) + MAP[k];
    down.set(k, key);
    W.noteOn(key, 0.8);
    ev.preventDefault();
  });
  window.addEventListener('keyup', ev => { const k = ev.key.toLowerCase(); if (down.has(k)) { W.noteOff(down.get(k)); down.delete(k); } });
  window.addEventListener('blur', () => { down.forEach(key => W.noteOff(key)); down.clear(); });

  // ---- MIDI keyboards (Chrome, Edge, Opera, Firefox with permission; not Safari)
  W.midi = async function () {
    if (!navigator.requestMIDIAccess) return 'This browser has no Web MIDI';
    try {
      const acc = await navigator.requestMIDIAccess();
      const hook = () => acc.inputs.forEach(inp => { inp.onmidimessage = msg => {
        const [st, d1, d2] = msg.data, c = st & 0xf0;
        if (!W.started) W.start();
        if (c === 0x90 && d2 > 0) W.noteOn(d1, d2 / 127);
        else if (c === 0x80 || (c === 0x90 && d2 === 0)) W.noteOff(d1);
        else if (c === 0xb0 && d1 === 1) send({ t: 'wheel', v: d2 / 127 });
        else if (c === 0xb0 && (d1 === 120 || d1 === 123)) send({ t: 'alloff' });
        else if (c === 0xe0) send({ t: 'bend', v: (((d2 << 7) | d1) - 8192) / 8191 });
        else if (c === 0xc0 && window.SW && window.SW.presets.factory[d1]) window.SW.loadPreset(window.SW.presets.factory[d1]);
      }; });
      hook(); acc.onstatechange = hook;
      return acc.inputs.size ? acc.inputs.size + ' MIDI input' + (acc.inputs.size > 1 ? 's' : '') : 'No MIDI input found';
    } catch (e) { return 'MIDI was not allowed'; }
  };
})();
