/* SWINGBY — MOD: the eight modulation slots (on, source -> target, amount; drawn in the orbit view as gravity lines: thicker = more,
   flowing from source to target, a negative amount flows back as dots), the flyby and the voice settings. The design's SW_Mod. */
(function () {
  'use strict';
  const SW = window.SW, P = SW.P, el = SW.el, ui = SW.ui;
  let orbit = null;
  // the control kit's glyphs (18 x 18): [stroke path, fill path]
  const G = {
    none: ['M5 9h8', ''], lfo: ['M2 9a7 3.2 0 1 0 14 0a7 3.2 0 1 0 -14 0', 'M15.6 7.4a1.7 1.7 0 1 1 -3.4 0a1.7 1.7 0 1 1 3.4 0'], env: ['M2 15L5.5 3.5L8.5 9.5H12L15.5 15', ''],
    vel: ['M4 15v-3.5M9 15V8M14 15V3.5', ''], wheel: ['M6.5 2.5h5a1.5 1.5 0 0 1 1.5 1.5v10a1.5 1.5 0 0 1 -1.5 1.5h-5a1.5 1.5 0 0 1 -1.5 -1.5V4a1.5 1.5 0 0 1 1.5 -1.5zM7 6.5h4M7 9h4M7 11.5h4', ''],
    press: ['M9 2v7.5M6 6.5l3 3 3-3M3 14.5h12', ''], key: ['M2.5 4h13v10h-13zM7 10v4M11 10v4', 'M5.6 4h2.6v6H5.6zM9.8 4h2.6v6H9.8z'], macro: ['M15 9a6 6 0 1 1 -12 0a6 6 0 1 1 12 0M9 9l3.2 -3.2', ''],
    cutoff: ['M2 6h7.5c3 0 4 2.5 5 9', ''], res: ['M2 7h6.5c1.6 0 2-4 3.5-4s2 9 4 12', ''], pitch: ['M9 2.5v13M5.5 6L9 2.5 12.5 6M5.5 12L9 15.5 12.5 12', ''],
    drive: ['M2 12l3-6 3 8 3-10 3 8 2-3', ''], pan: ['M2.5 9h13M5.5 6l-3 3 3 3M12.5 6l3 3-3 3', ''], level: ['M4 15v-2M7 15v-5M10 15V7M13 15V4', ''],
    rate: ['M3 9a6 6 0 1 1 6 6M9 15l-2.2-1.6M9 15l-1.6 2.2', ''], planet: ['M13 9a4 4 0 1 1 -8 0a4 4 0 1 1 8 0M1.5 12c4.5 0 11.5-2.8 15-6.5', ''],
    pw: ['M2 13V5h5v8h9', ''], detune: ['M2 9c2-5 4-5 6 0s4 5 6 0M2 11c2-3 4-3 6 0', ''], gravity: ['M9 9m-2 0a2 2 0 1 0 4 0a2 2 0 1 0 -4 0M2 5c4 0 6 2 6 4M16 13c-4 0-6-2-6-4', ''],
    table: ['M2 13c2 0 2-8 4-8s2 8 4 8 2-8 4-8 2 8 2 8', ''], index: ['M3 15V9M7 15V5M11 15V8M15 15V3M2 15h14', '']
  };
  // the plug-in's sources and targets (products/in07: ModSource, ModDest) in order
  const SRC_G = ['none', 'lfo', 'lfo', 'env', 'vel', 'wheel', 'press', 'key', 'macro', 'macro', 'macro', 'macro', 'macro', 'macro', 'macro', 'macro'].map(k => G[k]);
  const DST_G = ['none', 'cutoff', 'res', 'pitch', 'drive', 'pan', 'level', 'planet', 'planet', 'planet', 'planet', 'rate', 'rate', 'pw', 'detune', 'gravity', 'table', 'index'].map(k => G[k]);
  const SRC_KEY = ['', 'lfo1', 'lfo2', 'env', 'vel', 'mw', 'at', 'key', 'm1', 'm2', 'm3', 'm4', 'm5', 'm6', 'm7', 'm8'];
  const DST_KEY = ['', 'cutoff', 'res', 'pitch', 'drive', 'pan', 'level', 'L1', 'L2', 'L3', 'L4', 'lforate', 'lforate', 'osc', 'osc', 'osc', 'osc', 'osc'];
  const mid = (s, k) => 'in07.mod' + (s + 1) + '.' + k;
  const mods = () => [0, 1, 2, 3, 4, 5, 6, 7].map(s => ({ on: P.get(mid(s, 'on')) > 0.5, from: SRC_KEY[P.stepIndex(mid(s, 'src'))], to: DST_KEY[P.stepIndex(mid(s, 'dst'))], amt: P.get(mid(s, 'amount')) / 100 }))
    .filter(m => m.on && m.from && m.to && m.amt !== 0);

  const build = root => {
    const ov = el('div', { class: 'orbit', style: { left: '24px', top: '72px', width: '582px', height: '525px' } });
    root.appendChild(ov);
    orbit = new SW.OrbitView(ov, Object.assign(SW.orbitProps(), { view: 'mod', mods: mods() }));
    root.appendChild(el('div', { style: { position: 'absolute', left: '40px', top: '640px', width: '560px', display: 'flex', flexDirection: 'column', gap: '8px' } },
      el('span', { class: 'cap', text: 'GRAVITY LINES' }),
      el('p', { class: 'note', style: { margin: '0' }, text: 'Each slot is a line from its source to what it moves. The line is thicker for more amount and flows from source to target; a negative amount flows back as dots. Sources along the bottom: velocity, mod wheel, aftertouch and the eight macros; the LFO moon and the envelope at the core reach out from where they are.' })));

    // ---- the matrix
    const head = el('div', { class: 'cap2', style: { display: 'grid', gridTemplateColumns: '24px 52px 118px 22px 118px minmax(0, 1fr)', gap: '8px', alignItems: 'center', height: '30px', letterSpacing: '.24em', fontSize: '10px', borderBottom: '1px solid var(--line)' } },
      el('span', { text: '#' }), el('span', { text: 'ON' }), el('span', { text: 'SOURCE' }), el('span'), el('span', { text: 'TARGET' }), el('span', { text: 'AMOUNT' }));
    const rows = [0, 1, 2, 3, 4, 5, 6, 7].map(s => {
      const arrow = el('svg', { width: 26, height: 12, viewBox: '0 0 26 12', 'aria-hidden': 'true' }, el('path', { d: 'M2 6h20M17 2l5 4-5 4', fill: 'none', stroke: 'var(--acc)', 'stroke-width': '1.3', 'stroke-linecap': 'round', 'stroke-linejoin': 'round' }));
      const r = el('div', { style: { display: 'grid', gridTemplateColumns: '24px 52px 118px 22px 118px minmax(0, 1fr)', gap: '8px', alignItems: 'center', height: '58px', borderBottom: s < 7 ? '1px solid var(--line)' : 'none' } },
        el('span', { class: 'mono', style: { fontSize: '11px', color: 'var(--sub)' }, text: String(s + 1) }),
        ui.onButton(mid(s, 'on'), { style: { width: '52px', height: '28px', padding: '0', borderRadius: '14px', fontFamily: "'Space Mono', monospace", fontSize: '9px', letterSpacing: '.06em' } }),
        ui.dropdown(mid(s, 'src'), { glyphs: SRC_G, label: 'Slot ' + (s + 1) + ' source' }), arrow,
        ui.dropdown(mid(s, 'dst'), { glyphs: DST_G, label: 'Slot ' + (s + 1) + ' target' }),
        ui.slider(mid(s, 'amount'), { label: '', compact: true, labelWidth: 0, valWidth: 40, fmt: v => (v > 0 ? '+' : v < 0 ? '−' : '') + Math.abs(Math.round(v)), dim: () => P.get(mid(s, 'on')) < 0.5, also: [mid(s, 'on')] }));
      ui.watch([mid(s, 'on')], () => { r.style.opacity = P.get(mid(s, 'on')) > 0.5 ? '1' : '.55'; });
      return r;
    });
    root.appendChild(el('div', { class: 'panel', style: { left: '630px', top: '72px', width: '626px', height: '522px', padding: '12px 20px' } }, head, rows));

    // ---- the flyby and the voice
    const sl = (id, label) => ui.slider(id, { label, compact: true, labelWidth: 78, valWidth: 64 });
    const row = (label, ctl) => el('div', { class: 'row2', style: { gridTemplateColumns: '78px minmax(0, 1fr)', minHeight: '34px' } }, el('span', { class: 'k', text: label }), ctl);
    root.appendChild(el('div', { class: 'panel', style: { left: '630px', top: '610px', width: '305px', height: '230px', padding: '12px 16px', display: 'flex', flexDirection: 'column', gap: '2px' } },
      el('div', { class: 'sec' }, el('span', { class: 't', text: 'FLYBY' })),
      row('MODE', ui.seg('in07.flyby.mode', { labels: ['OFF', 'ARRIVE', 'PASS', 'LEAVE'] })),
      sl('in07.flyby.depth', 'DEPTH'), sl('in07.flyby.time', 'TIME'), sl('in07.flyby.near', 'NEAR'),
      row('SIDE', ui.seg('in07.flyby.side', { labels: ['L>R', 'R>L', 'ALT'] }))));
    root.appendChild(el('div', { class: 'panel', style: { left: '951px', top: '610px', width: '305px', height: '230px', padding: '12px 16px', display: 'flex', flexDirection: 'column', gap: '2px' } },
      el('div', { class: 'sec' }, el('span', { class: 't', text: 'VOICE' })),
      row('PLAY', ui.seg('in07.mode', { labels: ['POLY', 'MONO', 'LEGATO'] })),
      row('VOICES', ui.dropdown('in07.voices')),
      sl('in07.glide', 'GLIDE'), sl('in07.bend', 'BEND'), ui.slider('in07.level', { label: 'LEVEL', compact: true, labelWidth: 78, valWidth: 64, bipolar: false })));

    const showOrbit = SW.throttle(() => orbit.set(Object.assign(SW.orbitProps(), { view: 'mod', mods: mods() })));
    SW.on('param', showOrbit);
  };
  SW.screens.push({ id: 'mod', label: 'MOD', build, show: on => { if (orbit) { orbit.show(on); if (on) orbit.set(Object.assign(SW.orbitProps(), { view: 'mod', mods: mods() })); } } });
})();
