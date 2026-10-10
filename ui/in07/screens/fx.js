/* SWINGBY — FX: the six effects after the voices, in the order of the chain. Above, the signal's path from the voice past each effect
   (a swing-by: alternately over and under it; each effect is its own turning world, greyed when bypassed) out to the right; below, one
   card an effect in the same order: on / off, its three settings, < > to move it one place (the six order slots of the plug-in).
   The design's SW_FX. The order is read as the plug-in reads it: the slots in turn, an effect the first time it appears, missing ones
   at the end; a move writes the whole order back (two slots normally). */
(function () {
  'use strict';
  const SW = window.SW, P = SW.P, el = SW.el, ui = SW.ui;
  const KEYS = ['drive', 'chorus', 'delay', 'reverb', 'eq', 'limit'];   // the plug-in's FxId order
  const NAMES = ['DRIVE', 'CHORUS', 'DELAY', 'REVERB', 'EQ', 'LIMIT'];
  const ROWS = [[['amount', 'AMOUNT'], ['tone', 'TONE'], ['mix', 'MIX']], [['rate', 'RATE'], ['depth', 'DEPTH'], ['mix', 'MIX']],
    [['time', 'TIME'], ['feedback', 'FEEDBACK'], ['mix', 'MIX']], [['size', 'SIZE'], ['damp', 'DAMP'], ['mix', 'MIX']],
    [['low', 'LOW'], ['mid', 'MID'], ['high', 'HIGH']], [['gain', 'GAIN'], ['ceiling', 'CEILING'], ['release', 'RELEASE']]];
  const SPIN = [0.10, 0.16, 0.08, 0.06, 0.12, 0.09];   // turns a second (tools/blender/in07_orbital.py fx_*: 96 frames, 12 columns)
  const SZ = 84, TOP = 56;                             // the sprite's size; the path's drawing starts under the header
  const slotId = i => 'in07.fx.slot' + (i + 1);
  const onId = e => 'in07.fx.' + KEYS[e] + '.on';
  const pid = (e, k) => 'in07.fx.' + KEYS[e] + '.' + ROWS[e][k][0];
  const isOn = e => P.get(onId(e)) > 0.5;
  // the order in use (as products/in07 Processor::updateFx)
  const order = () => {
    const used = new Set(), o = [];
    for (let i = 0; i < 6; i++) { const e = P.stepIndex(slotId(i)); if (!used.has(e)) { used.add(e); o.push(e); } }
    for (let e = 0; e < 6; e++) if (!used.has(e)) o.push(e);
    return o;
  };
  const move = (pos, d) => {
    const o = order(), j = pos + d;
    if (j < 0 || j > 5) return;
    [o[pos], o[j]] = [o[j], o[pos]];
    for (let i = 0; i < 6; i++) if (P.stepIndex(slotId(i)) !== o[i]) P.tap(slotId(i), o[i]);
  };
  const f1 = x => Math.round(x * 10) / 10;

  // the path: from the voice past each station (over, under, over ...) to OUT; Catmull-Rom through the points, as cubic Beziers
  const geometry = () => {
    const pts = [[150, 214]], st = [];
    for (let i = 0; i < 6; i++) {
      const x = 270 + i * 160, up = i % 2 === 0, y = up ? 150 : 278;
      st.push({ x, y, up });
      pts.push([x - 34, y + (up ? 44 : -44)]); pts.push([x + 34, y + (up ? 44 : -44)]);
    }
    pts.push([1200, 214]);
    let d = 'M' + pts[0][0] + ' ' + pts[0][1];
    const samples = [];
    for (let i = 1; i < pts.length; i++) {
      const p0 = pts[Math.max(0, i - 2)], p1 = pts[i - 1], p2 = pts[i], p3 = pts[Math.min(pts.length - 1, i + 1)];
      const c1 = [p1[0] + (p2[0] - p0[0]) / 6, p1[1] + (p2[1] - p0[1]) / 6], c2 = [p2[0] - (p3[0] - p1[0]) / 6, p2[1] - (p3[1] - p1[1]) / 6];
      d += 'C' + f1(c1[0]) + ' ' + f1(c1[1]) + ' ' + f1(c2[0]) + ' ' + f1(c2[1]) + ' ' + p2[0] + ' ' + p2[1];
      for (let q = 0; q < 24; q++) {
        const u = q / 24, w = 1 - u;
        samples.push([w * w * w * p1[0] + 3 * w * w * u * c1[0] + 3 * w * u * u * c2[0] + u * u * u * p2[0], w * w * w * p1[1] + 3 * w * w * u * c1[1] + 3 * w * u * u * c2[1] + u * u * u * p2[1]]);
      }
    }
    samples.push(pts[pts.length - 1]);
    return { d, st, samples };
  };
  const G = geometry();
  const at = ph => {
    const s = G.samples, k = Math.min(s.length - 1, Math.max(0, ph * (s.length - 1))), k0 = Math.floor(k), fr = k - k0;
    const a = s[k0], b = s[Math.min(s.length - 1, k0 + 1)];
    return [a[0] + (b[0] - a[0]) * fr, a[1] + (b[1] - a[1]) * fr];
  };

  let view = null;
  const build = root => {
    // ---- the path (drawn once), the stations' glows and rings (follow on / off), the probe and its trail (move)
    const glows = [], rings = [];
    const back = el('svg', { width: 1280, height: 380, viewBox: '0 0 1280 380', role: 'img', style: { position: 'absolute', left: '0', top: TOP + 'px', display: 'block' } },
      el('defs', {}, el('radialGradient', { id: 'swFxGlow', cx: '50%', cy: '50%', r: '50%' },
        el('stop', { offset: '0', style: { stopColor: '#8de0c3', stopOpacity: '.55' } }), el('stop', { offset: '1', style: { stopColor: '#8de0c3', stopOpacity: '0' } }))),
      el('path', { d: G.d, fill: 'none', style: { stroke: 'var(--cat)', strokeOpacity: '.35', strokeWidth: '1.4' } }));
    const flow = el('path', { d: G.d, fill: 'none', style: { stroke: 'var(--acc)', strokeOpacity: '.5', strokeWidth: '1.4', strokeDasharray: '2 10' } });
    back.appendChild(flow);
    G.st.forEach(s => {
      const g = el('circle', { cx: s.x, cy: s.y, r: 52, fill: 'url(#swFxGlow)' }), r = el('circle', { cx: s.x, cy: s.y, r: 40, fill: 'none', style: { stroke: 'var(--acc)', strokeWidth: '1' } });
      back.appendChild(g); back.appendChild(r); glows.push(g); rings.push(r);
    });
    back.appendChild(el('circle', { cx: 1200, cy: 214, r: 6, fill: 'none', style: { stroke: 'var(--acc)', strokeOpacity: '.6', strokeWidth: '1.4' } }));
    root.appendChild(back);
    // the voice: the core of the orbit screens
    const core = el('div', { 'aria-hidden': 'true', style: { position: 'absolute', left: '30px', top: '210px', width: '120px', height: '120px', backgroundSize: 'cover' } });
    const showCore = () => { const u = SW.url[SW.settings.theme === 'light' ? 'daycore' : 'core']; core.style.backgroundImage = u ? 'url(' + u + ')' : 'none'; };
    root.appendChild(core);
    root.appendChild(el('span', { class: 'cap2', style: { position: 'absolute', left: '40px', top: '336px', letterSpacing: '.3em', color: 'var(--label)' }, text: 'THE VOICE' }));
    root.appendChild(el('span', { class: 'mono', style: { position: 'absolute', left: '1180px', top: '284px', width: '40px', textAlign: 'center', fontSize: '11px', color: 'var(--label)' }, text: 'OUT' }));
    // the stations: one turning world and one label per place
    const sprites = G.st.map(s => {
      const im = el('i');
      const box = el('div', { class: 'fxst', 'aria-hidden': 'true', style: { left: (s.x - SZ / 2) + 'px', top: (s.y + TOP - SZ / 2) + 'px' } }, im);
      root.appendChild(box);
      return { box, im, kind: -1, fr: -1 };
    });
    const labels = G.st.map(s => {
      const n = el('span', { class: 'n' }), sub = el('span', { class: 's' });
      const l = el('div', { class: 'fxlbl', style: { left: (s.x - 70) + 'px', top: (s.up ? s.y + TOP - 98 : s.y + TOP + 50) + 'px' } }, n, sub);
      root.appendChild(l);
      return { l, n, sub };
    });
    const front = el('svg', { width: 1280, height: 380, viewBox: '0 0 1280 380', 'aria-hidden': 'true', style: { position: 'absolute', left: '0', top: TOP + 'px', display: 'block', pointerEvents: 'none' } });
    const trail = [];
    for (let n = 1; n <= 12; n++) { const c = el('circle', { r: f1(3 - n * 0.2), cx: -20, cy: -20, style: { fill: 'var(--trail)', fillOpacity: String(Math.round((0.6 - n * 0.045) * 100) / 100) } }); front.appendChild(c); trail.push(c); }
    const probeGlow = el('circle', { r: 14, cx: -20, cy: -20, fill: 'url(#swFxGlow)' }), probe = el('circle', { r: 4, cx: -20, cy: -20, style: { fill: 'var(--probe)' } });
    front.appendChild(probeGlow); front.appendChild(probe);
    root.appendChild(front);

    // ---- the cards
    root.appendChild(el('span', { class: 'cap', style: { position: 'absolute', left: '24px', top: '446px', color: 'var(--label)' }, text: 'EFFECTS · AFTER THE VOICES, IN THIS ORDER' }));
    const grid = el('section', { 'aria-label': 'Effect slots', style: { position: 'absolute', left: '24px', right: '24px', top: '470px', display: 'grid', gridTemplateColumns: 'repeat(6, minmax(0, 1fr))', gap: '12px' } });
    const chev = d => el('svg', { width: 12, height: 12, viewBox: '0 0 12 12', 'aria-hidden': 'true' }, el('path', { d, fill: 'none', stroke: 'currentColor', 'stroke-width': '1.4', 'stroke-linecap': 'round', 'stroke-linejoin': 'round' }));
    const cards = KEYS.map((k, e) => {
      const no = el('span', { class: 'mono', style: { fontSize: '11px', color: 'var(--acc)' } });
      const onb = ui.onButton(onId(e), { style: { width: '52px', height: '28px', padding: '0', borderRadius: '14px', fontFamily: "'Space Mono', monospace", fontSize: '9px', letterSpacing: '.06em' } });
      onb.setAttribute('aria-label', NAMES[e]);
      const left = el('button', { type: 'button', class: 'mv', 'aria-label': 'Move ' + NAMES[e] + ' earlier' }, chev('M7.5 2.5 4 6l3.5 3.5'));
      const right = el('button', { type: 'button', class: 'mv', 'aria-label': 'Move ' + NAMES[e] + ' later' }, chev('M4.5 2.5 8 6l-3.5 3.5'));
      const card = el('div', { class: 'fxcard', role: 'group', 'aria-label': NAMES[e] },
        el('div', { style: { display: 'flex', alignItems: 'center', justifyContent: 'space-between' } }, no, onb),
        el('span', { class: 'nm', text: NAMES[e] }),
        ROWS[e].map((r, i) => ui.slider(pid(e, i), { label: r[1], stack: true })),
        el('div', { style: { marginTop: 'auto', display: 'flex', gap: '6px' } }, left, right));
      left.addEventListener('click', () => move(card._pos, -1));
      right.addEventListener('click', () => move(card._pos, 1));
      grid.appendChild(card);
      return { card, no, left, right };
    });
    root.appendChild(grid);

    // ---- following the plug-in: the order and the switches move the cards, the stations and the labels
    let ord = order();
    const layout = () => {
      ord = order();
      ord.forEach((e, pos) => {
        const c = cards[e], on = isOn(e);
        c.card._pos = pos; c.card.style.order = String(pos);
        c.no.textContent = String(pos + 1);
        c.card.classList.toggle('off', !on);
        c.left.disabled = pos === 0; c.right.disabled = pos === 5;
        const sp = sprites[pos];
        if (sp.kind !== e) { sp.kind = e; const u = SW.url['fx_' + KEYS[e]]; sp.im.style.backgroundImage = u ? 'url(' + u + ')' : 'none'; }
        sp.box.classList.toggle('off', !on);
        glows[pos].setAttribute('opacity', on ? '0.8' : '0');
        rings[pos].style.strokeOpacity = on ? '.45' : '.18'; rings[pos].style.strokeDasharray = on ? 'none' : '3 5';
        const lb = labels[pos];
        lb.l.classList.toggle('off', !on);
        lb.n.textContent = (pos + 1) + ' · ' + NAMES[e];
        lb.sub.textContent = on ? P.text(pid(e, 0)) : 'BYPASS';
      });
      back.setAttribute('aria-label', 'Signal path: from the voice through ' + (ord.filter(isOn).map(e => NAMES[e].toLowerCase()).join(', ') || 'no effect') + ' to the output');
    };
    const ids = [];
    for (let e = 0; e < 6; e++) { ids.push(slotId(e), onId(e), pid(e, 0)); }
    ui.watch(ids, SW.throttle(layout));
    layout();
    SW.on('image', name => { if (/^fx_/.test(name)) { sprites.forEach(s => { s.kind = -1; }); layout(); } if (name === 'core' || name === 'daycore') showCore(); });
    SW.on('setting', k => { if (k === 'theme') showCore(); });
    showCore();

    // ---- the motion: the worlds turn, the dashes flow, the probe flies along the path (MOTION 60 / 30 / OFF, as the orbit views)
    const draw = t => {
      ord.forEach((e, pos) => {
        const sp = sprites[pos], fr = Math.floor((((SPIN[e] * t * (SW.TURN || 1) + pos * 0.17) % 1) + 1) % 1 * 96) % 96;   // SW.TURN: core.js
        if (sp.fr !== fr) { sp.fr = fr; sp.im.style.transform = 'translate3d(' + (-(fr % 12) * SZ) + 'px,' + (-Math.floor(fr / 12) * SZ) + 'px,0)'; }
      });
      flow.style.strokeDashoffset = String(f1(-(t * 30) % 12));
      const ph = (t * 0.16) % 1, pp = at(ph);
      probe.setAttribute('cx', f1(pp[0])); probe.setAttribute('cy', f1(pp[1]));
      probeGlow.setAttribute('cx', f1(pp[0])); probeGlow.setAttribute('cy', f1(pp[1]));
      trail.forEach((c, n) => {
        const tp = ph - (n + 1) * 0.006;
        if (tp < 0) { c.setAttribute('cx', -20); return; }
        const q = at(tp); c.setAttribute('cx', f1(q[0])); c.setAttribute('cy', f1(q[1]));
      });
    };
    view = { visible: false, raf: 0, last: 0 };
    const tick = ts => {
      view.raf = 0;
      const m = SW.settings.motion;
      if (!view.visible || m === 'off') return;
      if (!document.hidden && (m === '60' || ts - view.last >= 1000 / 30 - 4)) { view.last = ts; draw(ts / 1000); }
      view.raf = requestAnimationFrame(tick);
    };
    view.run = () => {
      if (!view.visible) return;
      if (SW.settings.motion === 'off') { if (view.raf) cancelAnimationFrame(view.raf); view.raf = 0; draw(2.6); }
      else if (!view.raf) view.raf = requestAnimationFrame(tick);
    };
    SW.on('setting', k => { if (k === 'motion') view.run(); });
    SW.on('image', () => { if (SW.settings.motion === 'off' && view.visible) draw(2.6); });
    document.addEventListener('visibilitychange', () => { if (!document.hidden) view.run(); });
    draw(2.6);
  };
  SW.screens.push({ id: 'fx', label: 'FX', build, show: on => { if (!view) return; view.visible = on; if (on) view.run(); } });
})();
