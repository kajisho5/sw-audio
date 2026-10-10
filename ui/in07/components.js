/* SWINGBY plug-in window — the control kit (docs/design/in07 SW_Controls): each control is bound to one parameter of the plug-in,
   follows it when the host or a preset moves it, and edits it as one gesture (begin / values / end) so the host can record automation.
   Slider: the groove cut into the glass, the fill from 0 (or from the middle when the range crosses 0), the moon bead as the handle;
   drag (Shift = fine), double-click = default, wheel, arrow keys. Switch: the jewel lamp. Seg: one button per step. Dropdown: a list.
   Knob: the macro knob, the moon rides the arc (vertical drag). Every control is 44 px or more to hit where the layout allows. */
(function () {
  'use strict';
  const SW = window.SW, P = SW.P, el = SW.el;
  const ui = SW.ui = {};
  // a function run now and whenever one of the ids changes
  ui.watch = (ids, fn) => {
    const set = new Set(ids.map(id => P.idx(id)));
    fn();
    SW.on('param', i => { if (set.has(i)) fn(); });
  };
  const moonUrl = () => SW.url.bead ? 'url(' + SW.url.bead + ')' : 'radial-gradient(circle at 35% 30%, #ffffff, #bff3df 40%, #3fd1a0 75%, #0b5a41)';
  const moons = new Set();
  SW.on('image', name => { if (name === 'bead') moons.forEach(m => { m.style.backgroundImage = moonUrl(); }); });
  const moon = cls => { const m = el('span', { class: cls }); m.style.backgroundImage = moonUrl(); moons.add(m); return m; };

  // drag helper: begin / move(dx, dy, event) / end, with pointer capture
  const drag = (target, h) => {
    target.addEventListener('pointerdown', e => {
      if (e.button !== 0) return;
      e.preventDefault();
      target.setPointerCapture(e.pointerId);
      let lx = e.clientX, ly = e.clientY;
      h.begin(e);
      const mv = ev => { h.move(ev.clientX - lx, ev.clientY - ly, ev); lx = ev.clientX; ly = ev.clientY; };
      let done = false;
      const up = () => {
        if (done) return;
        done = true;
        target.removeEventListener('pointermove', mv); target.removeEventListener('pointerup', up); target.removeEventListener('pointercancel', up); target.removeEventListener('lostpointercapture', up);
        h.end();
      };
      target.addEventListener('pointermove', mv); target.addEventListener('pointerup', up); target.addEventListener('pointercancel', up); target.addEventListener('lostpointercapture', up);
    });
  };
  // the wheel: how far it turned, in notches (a mouse notch is about 100 px in Chromium, 3 lines elsewhere; a trackpad sends small
  // amounts often). Up = positive. Shift+wheel arrives sideways on Windows, so with Shift a sideways turn counts; without, it is ignored.
  // Stepped controls collect the turn until it makes a whole notch.
  const notches = e => {
    const k = e.deltaMode === 1 ? 1 / 3 : e.deltaMode === 2 ? 1 : 1 / 100;
    const d = Math.abs(e.deltaY) >= Math.abs(e.deltaX) ? e.deltaY : (e.shiftKey ? e.deltaX : 0);
    return SW.clamp(-d * k, -4, 4);
  };
  ui.notches = notches;
  const wheel = (target, s, apply) => {
    let acc = 0, at = 0;
    target.addEventListener('wheel', e => {
      e.preventDefault();
      const n = notches(e);
      if (!n) return;
      if (s.curve === 'step') {
        if (performance.now() - at > 400) acc = 0;
        at = performance.now(); acc += n;
        const whole = acc >= 0 ? Math.floor(acc + 1e-9) : Math.ceil(acc - 1e-9);
        if (!whole) return;
        acc -= whole;
        apply(whole / Math.max(1, s.steps.length - 1));
      } else apply(n * (e.shiftKey ? 0.002 : 0.01));
    }, { passive: false });
  };
  const zoomOf = node => { const r = node.getBoundingClientRect(); return r.width / (node.offsetWidth || r.width || 1) || 1; };

  ui.slider = (id, o) => {
    o = o || {};
    const s = P.spec(id);
    if (!s) return el('div');
    const bip = o.bipolar !== undefined ? o.bipolar : (s.min < 0 && s.max > 0);
    const fill = el('span', { class: 'f' }), m = moon('m');
    const tr = el('div', { class: 'tr', tabindex: '0', role: 'slider', 'aria-label': o.label || s.name }, el('span', { class: 'g' }), fill, m);
    const val = el('span', { class: 'v' });
    const root = el('div', { class: 'sl' + (o.big ? ' big' : '') + (o.compact ? ' compact' : '') + (o.stack ? ' stack' : '') }, o.label === '' ? el('span') : el('span', { class: 'k', text: o.label || s.name }), tr, val);
    if (o.labelWidth !== undefined) root.style.setProperty('--sl-label', o.labelWidth + 'px');
    if (o.valWidth !== undefined) root.style.setProperty('--sl-val', o.valWidth + 'px');
    const show = () => {
      const n = P.norm(id), v = P.get(id);
      const c = bip ? P.norm(id, 0) : 0, lo = Math.min(c, n), hi = Math.max(c, n);
      fill.style.left = (lo * 100) + '%'; fill.style.width = ((hi - lo) * 100) + '%';
      fill.classList.toggle('neg', bip && n < c);
      m.style.left = (n * 100) + '%';
      val.textContent = o.fmt ? o.fmt(v) : P.text(id, v);
      tr.setAttribute('aria-valuetext', val.textContent);
      if (o.dim) root.classList.toggle('off', !!o.dim());
    };
    ui.watch([id].concat(o.also || []), show);
    let n0 = 0, w = 1;
    drag(tr, {
      begin: e => { const r = tr.getBoundingClientRect(); w = r.width || 1; n0 = SW.clamp((e.clientX - r.left) / w, 0, 1); P.begin(id); P.set(id, P.fromNorm(id, n0)); show(); },
      move: (dx, dy, e) => { n0 = SW.clamp(n0 + dx / w * (e.shiftKey ? 0.1 : 1), 0, 1); P.set(id, P.fromNorm(id, n0)); show(); },
      end: () => P.end(id)
    });
    tr.addEventListener('dblclick', () => { P.tap(id, s.def); show(); });
    wheel(tr, s, dn => { P.tap(id, P.fromNorm(id, P.norm(id) + dn)); show(); });
    tr.addEventListener('keydown', e => {
      const d = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1 }[e.key];
      if (!d) return;
      e.preventDefault();
      P.tap(id, P.fromNorm(id, P.norm(id) + d * (s.curve === 'step' ? 1 / Math.max(1, s.steps.length - 1) : (e.shiftKey ? 0.002 : 0.01)))); show();
    });
    root.refresh = show;
    return root;
  };

  ui.switch = (id, o) => {
    o = o || {};
    const b = el('button', { type: 'button', class: 'sw', role: 'switch', 'aria-label': o.label || P.spec(id).name }, el('i'));
    ui.watch([id], () => b.setAttribute('aria-checked', P.get(id) > 0.5 ? 'true' : 'false'));
    b.addEventListener('click', () => P.tap(id, P.get(id) > 0.5 ? 0 : 1));
    return b;
  };

  // a pill button that switches a parameter on and off ("ON" / "OFF" written on it)
  ui.onButton = (id, o) => {
    o = o || {};
    const b = el('button', { type: 'button', class: 'btn' + (o.small ? ' small' : ''), role: 'switch' });
    if (o.style) Object.assign(b.style, o.style);
    ui.watch([id], () => { const on = P.get(id) > 0.5; b.classList.toggle('on', on); b.setAttribute('aria-checked', on ? 'true' : 'false'); b.textContent = on ? (o.on || 'ON') : (o.off || 'OFF'); });
    b.addEventListener('click', () => P.tap(id, P.get(id) > 0.5 ? 0 : 1));
    return b;
  };

  // one button per step of a stepped parameter (labels: the plug-in's, or o.labels; o.order: which steps, in which order)
  ui.seg = (id, o) => {
    o = o || {};
    const s = P.spec(id);
    const order = o.order || s.steps.map((_, k) => k);
    const root = el('div', { class: 'seg' + (o.cols ? ' wrap' : ''), role: 'radiogroup', 'aria-label': o.label || s.name });
    if (o.cols) root.style.setProperty('--cols', o.cols);
    if (o.title) root.title = o.title;
    const btns = order.map(k => {
      const b = el('button', { type: 'button', role: 'radio', text: (o.labels && o.labels[k]) || s.labels[k] });
      b.addEventListener('click', () => P.tap(id, s.steps[k]));
      root.appendChild(b);
      return [k, b];
    });
    ui.watch([id], () => { const cur = P.stepIndex(id); btns.forEach(([k, b]) => b.setAttribute('aria-checked', k === cur ? 'true' : 'false')); });
    return root;
  };

  // a dropdown list (glyphs: optional svg path per step)
  let openMenu = null;
  const closeMenu = () => { if (openMenu) { openMenu.remove(); openMenu = null; document.querySelectorAll('.dd > button[aria-expanded="true"]').forEach(b => b.setAttribute('aria-expanded', 'false')); } };
  document.addEventListener('pointerdown', e => { if (openMenu && !openMenu.contains(e.target) && !e.target.closest('.dd')) closeMenu(); });
  document.addEventListener('keydown', e => { if (e.key === 'Escape') closeMenu(); });
  const glyph = d => {
    const g = el('svg', { width: 18, height: 18, viewBox: '0 0 18 18', 'aria-hidden': 'true' });
    if (d) { const p = el('path', { d: d[0], fill: 'none', stroke: 'currentColor', 'stroke-width': '1.4', 'stroke-linecap': 'round', 'stroke-linejoin': 'round' }); g.appendChild(p); if (d[1]) g.appendChild(el('path', { d: d[1], fill: 'currentColor' })); }
    return g;
  };
  ui.glyph = glyph;
  ui.dropdown = (id, o) => {
    o = o || {};
    const s = P.spec(id);
    const text = el('span', { class: 't' }), g = el('span', { style: { display: 'inline-flex', color: 'var(--acc)' } });
    const caret = el('svg', { width: 12, height: 12, viewBox: '0 0 12 12', 'aria-hidden': 'true' }, el('path', { d: 'M2.5 4.5 6 8l3.5-3.5', fill: 'none', stroke: 'currentColor', 'stroke-width': '1.3', 'stroke-linecap': 'round' }));
    const btn = el('button', { type: 'button', 'aria-haspopup': 'listbox', 'aria-expanded': 'false', 'aria-label': o.label || s.name }, g, text, caret);
    const root = el('div', { class: 'dd' }, btn);
    const glyphs = o.glyphs || [];
    ui.watch([id], () => { const k = P.stepIndex(id); text.textContent = (o.labels && o.labels[k]) || s.labels[k]; g.innerHTML = ''; if (glyphs[k]) g.appendChild(glyph(glyphs[k])); });
    btn.addEventListener('click', () => {
      if (openMenu) { const was = btn.getAttribute('aria-expanded') === 'true'; closeMenu(); if (was) return; }
      const cur = P.stepIndex(id);
      const menu = el('div', { class: 'menu', role: 'listbox' });
      s.steps.forEach((v, k) => {
        if (o.hide && o.hide(k)) return;
        const it = el('button', { type: 'button', role: 'option', 'aria-selected': k === cur ? 'true' : 'false' }, el('span', { style: { display: 'inline-flex', color: 'var(--acc)' } }, glyphs[k] ? glyph(glyphs[k]) : el('span', { style: { width: '18px' } })), el('span', { text: (o.labels && o.labels[k]) || s.labels[k] }));
        it.addEventListener('click', () => { P.tap(id, v); closeMenu(); });
        menu.appendChild(it);
      });
      const app = document.getElementById('sw');
      app.appendChild(menu);
      const z = zoomOf(app), r = btn.getBoundingClientRect(), ar = app.getBoundingClientRect();
      const top = (r.bottom - ar.top) / z + 4, left = (r.left - ar.left) / z;
      menu.style.position = 'absolute'; menu.style.left = left + 'px'; menu.style.top = top + 'px'; menu.style.minWidth = Math.max(200, r.width / z) + 'px';
      if (top + menu.offsetHeight > 850) menu.style.top = Math.max(8, (r.top - ar.top) / z - menu.offsetHeight - 4) + 'px';
      openMenu = menu; btn.setAttribute('aria-expanded', 'true');
      const sel = menu.querySelector('[aria-selected="true"]'); if (sel) sel.scrollIntoView({ block: 'nearest' });
    });
    return root;
  };

  // the macro knob: 270 degrees from the lower left, the arc lit up to the value, the moon riding it; vertical drag
  ui.knob = (id, o) => {
    o = o || {};
    const s = P.spec(id);
    const arc = el('div', { class: 'arc' }), face = el('div', { class: 'face' }), m = moon('moon');
    const kb = el('div', { class: 'kb', tabindex: '0', role: 'slider', 'aria-label': o.label || s.name }, el('div', { class: 'ring' }), arc, face, m);
    const root = el('div', { class: 'knob' }, kb, el('span', { class: 'lb', text: o.label || s.name }));
    const show = () => {
      const n = P.norm(id), deg = n * 270;
      arc.style.background = 'conic-gradient(from 225deg, var(--acc) 0deg ' + deg + 'deg, rgba(0,0,0,0) ' + deg + 'deg 360deg)';
      const a = (225 + deg - 90) * Math.PI / 180;
      m.style.left = (30 + 30 * Math.cos(a)) + 'px'; m.style.top = (30 + 30 * Math.sin(a)) + 'px';
      face.textContent = o.fmt ? o.fmt(P.get(id)) : String(Math.round(P.get(id)));
      kb.setAttribute('aria-valuetext', P.text(id));
    };
    ui.watch([id], show);
    let n0 = 0;
    drag(kb, {
      begin: () => { n0 = P.norm(id); P.begin(id); },
      move: (dx, dy, e) => { n0 = SW.clamp(n0 + (dx - dy) / 200 * (e.shiftKey ? 0.1 : 1), 0, 1); P.set(id, P.fromNorm(id, n0)); show(); },
      end: () => P.end(id)
    });
    kb.addEventListener('dblclick', () => { P.tap(id, s.def); show(); });
    wheel(kb, s, dn => { P.tap(id, P.fromNorm(id, P.norm(id) + dn)); show(); });
    kb.addEventListener('keydown', e => { const d = { ArrowRight: 1, ArrowUp: 1, ArrowLeft: -1, ArrowDown: -1 }[e.key]; if (d) { e.preventDefault(); P.tap(id, P.fromNorm(id, P.norm(id) + d * 0.01)); show(); } });
    return root;
  };

  // an ADSR drawn as the plug-in shapes it (exponential segments), times on a log scale so short and long both read
  ui.envPath = (a, d, s, r, W, H) => {
    const lt = ms => Math.log10(1 + ms / 2) / Math.log10(1 + 20000 / 2);
    const wa = 6 + (W * 0.32) * lt(a), wd = 6 + (W * 0.32) * lt(d), wr = 6 + (W * 0.32) * lt(r), ws = Math.max(10, W - wa - wd - wr - 4);
    const y = v => H - 2 - v * (H - 6), sus = s / 100;
    let p = 'M2 ' + y(0);
    for (let k = 1; k <= 8; k++) { const u = k / 8; p += 'L' + (2 + wa * u).toFixed(1) + ' ' + y(1 - Math.pow(1 - u, 2)).toFixed(1); }
    for (let k = 1; k <= 10; k++) { const u = k / 10; p += 'L' + (2 + wa + wd * u).toFixed(1) + ' ' + y(sus + (1 - sus) * Math.pow(1e-3, u)).toFixed(1); }
    p += 'L' + (2 + wa + wd + ws).toFixed(1) + ' ' + y(sus).toFixed(1);
    for (let k = 1; k <= 10; k++) { const u = k / 10; p += 'L' + (2 + wa + wd + ws + wr * u).toFixed(1) + ' ' + y(sus * Math.pow(1e-4, u)).toFixed(1); }
    return p;
  };
  ui.envGraph = (ids, W, H) => {
    const path = el('path', { fill: 'none', stroke: 'var(--acc)', 'stroke-width': '1.4', 'stroke-linejoin': 'round' });
    const svg = el('svg', { viewBox: '0 0 ' + W + ' ' + H, width: W, height: H, 'aria-hidden': 'true', style: { display: 'block' } }, el('path', { d: 'M2 ' + (H - 2) + 'H' + (W - 2), stroke: 'var(--track)', 'stroke-width': '1' }), path);
    ui.watch(ids, () => path.setAttribute('d', ui.envPath(P.get(ids[0]), P.get(ids[1]), P.get(ids[2]), P.get(ids[3]), W, H)));
    return svg;
  };
  ui.closeMenu = closeMenu;
  // a small menu at a point (a right click): items [{label, act, on}]; closes on a pick, Escape or a press elsewhere
  ui.popup = (x, y, items) => {
    closeMenu();
    const menu = el('div', { class: 'menu', role: 'menu' });
    items.forEach(it => {
      const b = el('button', { type: 'button', role: 'menuitem', text: it.label });
      if (it.on) b.setAttribute('aria-selected', 'true');
      b.addEventListener('click', () => { closeMenu(); it.act(); });
      menu.appendChild(b);
    });
    const host = document.getElementById('sw') || document.body;
    host.appendChild(menu);
    const r = host.getBoundingClientRect(), z = r.width / (host.offsetWidth || r.width || 1) || 1;   // the window's size setting scales #sw
    menu.style.left = Math.min((x - r.left) / z, host.offsetWidth - menu.offsetWidth - 8) + 'px';
    menu.style.top = Math.min((y - r.top) / z, host.offsetHeight - menu.offsetHeight - 8) + 'px';
    menu.style.position = 'absolute';
    openMenu = menu;
    const f = menu.querySelector('button'); if (f) f.focus();
  };
})();
