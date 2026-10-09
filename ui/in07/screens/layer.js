/* SWINGBY — LAYER: the four layers (cards: the world, its type and name, its level; LAYER ON), the selected layer's world close up
   (PLAY NOTE plays it), its envelopes (amp and filter) and the two LFOs, and on the right its oscillator and filter.
   The layout is the design's (docs/design/in07 SW_Layer). Each layer has its own set of controls; the selected one is shown. */
(function () {
  'use strict';
  const SW = window.SW, P = SW.P, el = SW.el, ui = SW.ui;
  const lid = (l, k) => 'in07.l' + (l + 1) + '.' + k;
  let orbit = null, refresh = null;

  const build = root => {
    const caption = el('span', { class: 'cap', style: { position: 'absolute', left: '28px', top: '72px' } });
    root.appendChild(caption);
    SW.on('preset', () => { caption.textContent = 'LAYERS · ' + String(SW.current.name || 'INIT').toUpperCase(); });
    caption.textContent = 'LAYERS · ' + String(SW.current.name || 'INIT').toUpperCase();

    // ---- the layer cards (left)
    const cards = [0, 1, 2, 3].map(l => {
      const th = SW.thumb(SW.layerKind(l), 48);
      const tag = el('span', { class: 'mono', style: { fontSize: '10px', color: 'var(--acc)', letterSpacing: '.06em' } });
      const name = el('span', { style: { fontSize: '17px', letterSpacing: '.06em', color: 'var(--strong)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' } });
      const bar = el('span', { style: { position: 'absolute', left: '0', top: '0', height: '2px', background: 'var(--acc)' } });
      const b = el('button', { type: 'button', role: 'radio', class: 'panel', style: { position: 'absolute', left: '24px', top: (92 + l * 96) + 'px', width: '228px', height: '88px', display: 'flex', alignItems: 'center', gap: '14px', padding: '0 14px', textAlign: 'left' } },
        th, el('span', { style: { flex: '1', minWidth: '0', display: 'flex', flexDirection: 'column', gap: '4px' } }, tag, name, el('span', { style: { position: 'relative', height: '2px', background: 'var(--track)' } }, bar)));
      b.addEventListener('click', () => { SW.focus = l; SW.emit('focus-set'); show(); });
      root.appendChild(b);
      return { b, th, tag, name, bar };
    });
    const layerOn = el('button', { type: 'button', class: 'btn', role: 'switch', style: { position: 'absolute', left: '24px', top: '478px', width: '228px' } });
    layerOn.addEventListener('click', () => { const id = lid(SW.focus, 'on'); P.tap(id, P.get(id) > 0.5 ? 0 : 1); });
    root.appendChild(layerOn);

    // ---- the world close up, and PLAY NOTE
    const ov = el('div', { class: 'orbit', style: { left: '268px', top: '72px', width: '520px', height: '440px' } });
    root.appendChild(ov);
    orbit = new SW.OrbitView(ov, Object.assign(SW.orbitProps(), { view: 'close' }));
    const play = el('button', { type: 'button', class: 'btn', style: { position: 'absolute', left: '448px', top: '522px', width: '160px', height: '40px' }, text: 'PLAY NOTE' });
    let held = false;
    const on = e => { e.preventDefault(); if (!held) { held = true; SW.noteOn(60, 0.8); } };
    const off = () => { if (held) { held = false; SW.noteOff(60); } };
    play.addEventListener('pointerdown', on); play.addEventListener('pointerup', off); play.addEventListener('pointerleave', off); play.addEventListener('pointercancel', off);
    play.addEventListener('keydown', e => { if (e.key === ' ' || e.key === 'Enter') on(e); });
    play.addEventListener('keyup', e => { if (e.key === ' ' || e.key === 'Enter') off(); });
    root.appendChild(play);

    // ---- envelopes (amp / filter) and the LFOs (bottom)
    const bottom = el('div', { class: 'panel', style: { left: '268px', top: '580px', width: '520px', height: '260px', padding: '16px 18px', display: 'grid', gridTemplateColumns: '236px minmax(0, 1fr)', gap: '22px' } });
    root.appendChild(bottom);
    let envKind = 'amp', lfoN = 0;
    const envTabs = el('div', { class: 'minitabs', role: 'radiogroup' });
    const lfoTabs = el('div', { class: 'minitabs', role: 'radiogroup' });
    const envBox = el('div', { style: { display: 'flex', flexDirection: 'column', gap: '2px' } });
    const lfoBox = el('div', { style: { display: 'flex', flexDirection: 'column', gap: '4px', minWidth: '0' } });
    bottom.append(el('div', { style: { display: 'flex', flexDirection: 'column', gap: '6px', minWidth: '0' } }, el('div', { class: 'sec' }, el('span', { class: 't', text: 'ENVELOPE' }), envTabs), envBox),
      el('div', { style: { display: 'flex', flexDirection: 'column', gap: '6px', minWidth: '0' } }, el('div', { class: 'sec' }, el('span', { class: 't', text: 'LFO' }), lfoTabs), lfoBox));
    [['amp', 'AMP'], ['fenv', 'FILTER']].forEach(([k, t]) => { const b = el('button', { type: 'button', role: 'radio', text: t }); b.addEventListener('click', () => { envKind = k; show(); }); envTabs.appendChild(b); });
    ['1', '2'].forEach((t, k) => { const b = el('button', { type: 'button', role: 'radio', text: t }); b.addEventListener('click', () => { lfoN = k; show(); }); lfoTabs.appendChild(b); });
    const envSets = {};
    [0, 1, 2, 3].forEach(l => ['amp', 'fenv'].forEach(k => {
      const ids = ['a', 'd', 's', 'r'].map(x => lid(l, k + '.' + x));
      const box = el('div', { style: { display: 'flex', flexDirection: 'column', gap: '0' } }, ui.envGraph(ids, 232, 56),
        ['ATTACK', 'DECAY', 'SUSTAIN', 'RELEASE'].map((n, i) => ui.slider(ids[i], { label: n, compact: true, labelWidth: 62, valWidth: 62 })));
      envSets[l + k] = box; envBox.appendChild(box);
    }));
    const lfoSets = [0, 1].map(k => {
      const id = x => 'in07.lfo' + (k + 1) + '.' + x;
      const box = el('div', { style: { display: 'flex', flexDirection: 'column', gap: '6px' } },
        ui.seg(id('shape'), { labels: ['ORBIT', 'TRI', 'SAW', 'SQR', 'S&H'] }),
        ui.slider(id('rate'), { label: 'RATE', compact: true, labelWidth: 50, valWidth: 70, dim: () => P.stepIndex(id('sync')) > 0, also: [id('sync')] }),
        el('div', { class: 'row2', style: { gridTemplateColumns: '50px minmax(0, 1fr)', gap: '10px', minHeight: '36px' } }, el('span', { class: 'k', text: 'SYNC' }), ui.dropdown(id('sync'))),
        ui.slider(id('orbit'), { label: 'ORBIT', compact: true, labelWidth: 50, valWidth: 70 }),
        el('div', { class: 'row2', style: { gridTemplateColumns: '50px minmax(0, 1fr)', gap: '10px', minHeight: '32px' } }, el('span', { class: 'k', text: 'START' }), ui.seg(id('trigger'), { labels: ['FREE', 'NOTE'] })));
      lfoBox.appendChild(box);
      return box;
    });

    // ---- the oscillator and the filter (right)
    const right = el('div', { class: 'panel', style: { left: '805px', top: '72px', width: '451px', height: '768px', padding: '16px 20px', display: 'flex', flexDirection: 'column', gap: '2px' } });
    root.appendChild(right);
    const typeTags = [], fltTags = [];
    const sets = [0, 1, 2, 3].map(l => {
      const id = k => lid(l, k);
      const sl = (k, label, o) => ui.slider(id(k), Object.assign({ label, compact: true, labelWidth: 80, valWidth: 72 }, o || {}));
      const row = (label, ctl) => el('div', { class: 'row2', style: { gridTemplateColumns: '80px minmax(0, 1fr)' } }, el('span', { class: 'k', text: label }), ctl);
      const analog = el('div', {}, row('WAVE', ui.seg(id('osc.wave'), { labels: ['SINE', 'TRI', 'SAW', 'SQR'] })), sl('osc.pw', 'PULSE W', { dim: () => P.stepIndex(id('osc.wave')) !== 3, also: [id('osc.wave')] }));
      const wt = el('div', {}, row('TABLE', ui.seg(id('wt.table'), { cols: 4, labels: ['CLASSIC', 'PULSE', 'SYNC', 'FOLD', 'FORMANT', 'BRIGHT', 'ORGAN', 'GLASS'] })), sl('wt.pos', 'POSITION'));
      const fm = el('div', {}, row('RATIO', ui.dropdown(id('fm.ratio'))), sl('fm.index', 'INDEX'), sl('fm.decay', 'DECAY'), sl('fm.feedback', 'FEEDBACK'));
      const smp = el('div', {}, row('SAMPLE', ui.seg(id('smp.id'), { cols: 4, labels: ['AIR', 'BREATH', 'RAIN', 'STATIC', 'GRIT', 'KNOCK', 'TICK', 'CLICK'] })));
      const kinds = [analog, wt, fm, smp];
      const typeTag = el('span', { class: 's' }), fltTag = el('span', { class: 's' });
      typeTags.push(typeTag); fltTags.push(fltTag);
      ui.watch([id('osc.type')], () => { const t = P.stepIndex(id('osc.type')); kinds.forEach((k, i) => { k.hidden = i !== t; }); });
      const box = el('div', { style: { display: 'flex', flexDirection: 'column', gap: '2px' } },
        el('div', { class: 'sec' }, el('span', { class: 't', text: 'OSCILLATOR' }), typeTag),
        row('TYPE', ui.seg(id('osc.type'), { labels: ['VA', 'WT', 'FM', 'SMP'] })), analog, wt, fm, smp,
        sl('osc.octave', 'OCTAVE'), sl('osc.semi', 'SEMI'), sl('osc.fine', 'FINE'), sl('osc.unison', 'UNISON'), sl('osc.detune', 'DETUNE'), sl('osc.spread', 'SPREAD'),
        sl('osc.gravity', 'GRAVITY'), sl('level', 'LEVEL', { bipolar: false }), sl('pan', 'PAN'), sl('vel', 'VELOCITY'),
        el('div', { class: 'hline', style: { margin: '8px 0 4px' } }),
        el('div', { class: 'sec' }, el('span', { class: 't', text: 'FILTER' }), fltTag),
        row('TYPE', ui.seg(id('flt.type'), { labels: ['LP12', 'LP24', 'BP12', 'HP12'] })),
        sl('flt.cutoff', 'CUTOFF'), sl('flt.res', 'RESO'), sl('flt.drive', 'DRIVE'), sl('flt.env', 'ENV'), sl('flt.key', 'KEY'));
      right.appendChild(box);
      return box;
    });
    right.appendChild(el('p', { class: 'note', style: { margin: 'auto 0 0' }, text: 'The world in the middle follows the layer: its type picks the world, Unison its moonlets, Detune how they drift apart, Cutoff the halo, Resonance its ring, Drive the sparks. Gravity pulls the unison copies into phase.' }));

    const show = () => {
      const f = SW.focus;
      cards.forEach((c, l) => {
        const sel = l === f, on = SW.layerOn(l);
        c.b.setAttribute('aria-checked', sel ? 'true' : 'false');
        c.b.style.borderColor = sel ? 'var(--line2)' : 'var(--line)';
        c.b.style.background = sel ? 'linear-gradient(160deg, rgba(63,209,160,.14), rgba(255,255,255,.02) 70%)' : '';
        c.th.setKind(SW.layerKind(l)); c.th.style.opacity = on ? '1' : '.35';
        c.tag.textContent = 'L' + (l + 1) + ' · ' + SW.layerType(l) + (on ? '' : ' · OFF');
        c.name.textContent = SW.layerName(l).replace(/^L\d /, '');
        c.name.style.color = on ? 'var(--strong)' : 'var(--sub)';
        c.bar.style.width = (on ? SW.layerLvl(l) * 100 : 0) + '%';
      });
      const lon = SW.layerOn(f);
      layerOn.classList.toggle('on', lon); layerOn.textContent = lon ? 'LAYER ON' : 'LAYER OFF'; layerOn.setAttribute('aria-checked', lon ? 'true' : 'false');
      sets.forEach((s, l) => { s.hidden = l !== f; });
      Object.keys(envSets).forEach(k => { envSets[k].hidden = k !== f + envKind; });
      envTabs.querySelectorAll('button').forEach((b, k) => b.setAttribute('aria-checked', ['amp', 'fenv'][k] === envKind ? 'true' : 'false'));
      lfoSets.forEach((s, k) => { s.hidden = k !== lfoN; });
      lfoTabs.querySelectorAll('button').forEach((b, k) => b.setAttribute('aria-checked', k === lfoN ? 'true' : 'false'));
      typeTags[f].textContent = P.label(lid(f, 'osc.type')).toUpperCase();
      fltTags[f].textContent = P.label(lid(f, 'flt.type')).replace(' ', '');
      orbit.set(Object.assign(SW.orbitProps(), { view: 'close' }));
    };
    refresh = show;
    const soon = SW.throttle(show);
    SW.on('param', soon);
    SW.on('focus-set', soon);
    SW.on('info', () => orbit.set({ noteKey: SW.info.note || 0 }));
    show();
  };
  SW.screens.push({ id: 'layer', label: 'LAYER', build, show: on => { if (orbit) { orbit.show(on); if (on) { if (refresh) refresh(); orbit.set(Object.assign(SW.orbitProps(), { view: 'close' })); } } } });
})();
