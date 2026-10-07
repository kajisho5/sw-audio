/* SW AUDIO plug-in screen runtime. One screen is built from a product's parameter table (ui/specs.json) and its meta (ui/products.json):
   SWUI.mount(element, {product, params, traits, bridge}).  The bridge is the only link to the plug-in:
     bridge.values()            -> array of plain values, host order (the product's parameters, then Auto gain, then Delta when the product has them)
     bridge.set(i, plain)       a value change;  bridge.begin(i) / bridge.end(i)  a gesture (an automation write starts and ends)
     bridge.call(name, ...args) a button (methods of the core: "tap", "learnNoise", ...)
     bridge.onChange(cb)        cb(i, plain) when the host or the core changes a value by itself
     bridge.info()              -> {latencyMs, cpu}  (optional)
   The curves here are the same as sw::ParamSpec (core/include/sw/param.hpp); tests/ui/curves.test.js checks them against values the C++ code produced. */
(function (global) {
  'use strict';
  const clamp = (x, a, b) => Math.min(b, Math.max(a, x));

  function makeCurve(p) {
    const rev = !!p.rev && p.curve !== 'step';
    const base = {
      lin: { v: x => p.min + x * (p.max - p.min), n: v => clamp((v - p.min) / (p.max - p.min), 0, 1) },
      log: { v: x => p.min * Math.pow(p.max / p.min, x), n: v => clamp(Math.log(v / p.min) / Math.log(p.max / p.min), 0, 1) },
      skew: { v: x => p.min + (p.max - p.min) * Math.pow(x, p.skew), n: v => clamp(Math.pow(clamp((v - p.min) / (p.max - p.min), 0, 1), 1 / p.skew), 0, 1) },
      fader: {
        v: x => { if (x <= 0) return p.min; if (x < 0.25) return -100 + 200 * x; if (x < 0.5) return -50 + 120 * (x - 0.25); if (x < 0.75) return -20 + 80 * (x - 0.5); return Math.min(p.max, 40 * (x - 0.75)); },
        n: v => { if (v <= p.min) return 0; if (v < -50) return clamp((v + 100) / 200, 0, 0.25); if (v < -20) return 0.25 + (v + 50) / 120; if (v < 0) return 0.5 + (v + 20) / 80; return clamp(0.75 + v / 40, 0, 1); }
      },
      symlog: {
        v: x => { const k = p.skew > 1 ? p.skew : 2000, u = 2 * x - 1; return (u < 0 ? -1 : 1) * p.max * (Math.pow(1 + k, Math.abs(u)) - 1) / k; },
        n: v => { const k = p.skew > 1 ? p.skew : 2000, a = clamp(Math.abs(v) / p.max, 0, 1), u = (v < 0 ? -1 : 1) * Math.log(1 + k * a) / Math.log(1 + k); return clamp(0.5 * (u + 1), 0, 1); }
      },
      step: {
        v: x => { const s = p.steps || []; if (!s.length) return p.min; const i = Math.round(x * (s.length - 1)); return s[clamp(i, 0, s.length - 1)]; },
        n: v => { const s = p.steps || []; if (s.length < 2) return 0; let b = 0; for (let i = 1; i < s.length; ++i) if (Math.abs(s[i] - v) < Math.abs(s[b] - v)) b = i; return b / (s.length - 1); }
      }
    }[p.curve] || null;
    return {
      value: x => { x = clamp(x, 0, 1); if (rev) x = 1 - x; return base.v(x); },
      norm: v => rev ? 1 - base.n(v) : base.n(v)
    };
  }

  function trim(n, d) { return Number(n.toFixed(d)).toString(); }
  function format(p, v, c) {
    if (p.curve === 'step') {
      const i = Math.round(c.norm(v) * ((p.steps || []).length - 1));
      return (p.labels && p.labels[i] !== undefined) ? p.labels[i] : trim(v, 2);
    }
    const nrm = c.norm(v);
    if (p.minLabel && nrm <= 1e-9) return p.minLabel;
    if (p.maxLabel && nrm >= (p.maxLabelNorm || 1) - 1e-9) return p.maxLabel;
    const a = Math.abs(v), u = p.unit || '';
    if (u === 'Hz' && a >= 1000) return trim(v / 1000, a >= 10000 ? 1 : 2) + ' kHz';
    const d = a >= 100 ? 0 : a >= 10 ? 1 : 2;
    if (u === ':1') return trim(v, d) + ':1';
    if (u === '%' ) return trim(v, d) + '%';
    return trim(v, d) + (u ? ' ' + u : '');
  }
  function parseValue(p, text) {
    let t = String(text).trim().toLowerCase().replace(',', '.'); let mul = 1;
    if (/khz/.test(t) || (/k$/.test(t) && p.unit === 'Hz')) mul = 1000;
    const m = t.match(/-?\d+(\.\d+)?/); if (!m) return null;
    return parseFloat(m[0]) * mul;
  }

  const el = (tag, cls, txt) => { const e = document.createElement(tag); if (cls) e.className = cls; if (txt !== undefined) e.textContent = txt; return e; };
  const isOnOff = p => p.curve === 'step' && (p.steps || []).length === 2 && /^(off|on)$/i.test((p.labels || [])[0] || '') ;
  const isMomentary = p => p.curve === 'step' && (p.steps || []).length === 2 && p.auto === false && /^(hold to|mute$)/i.test(p.name);

  function groupParams(list) {
    // list: [{p, i}] in host order -> sections [{title, items}]
    const items = list.slice(), used = new Set(), sections = [];
    // many identical faders (a graphic EQ): one row
    const sig = it => [it.p.curve, it.p.min, it.p.max, it.p.def, it.p.unit].join('|');
    const bySig = new Map(); items.forEach(it => { if (it.p.curve === 'lin' && it.p.auto !== false && /^(Band|R)\s/.test(it.p.name) && it.p.unit === 'dB') { const k = sig(it); (bySig.get(k) || bySig.set(k, []).get(k)).push(it); } });
    const faderGroups = [...bySig.values()].filter(g => g.length >= 12);
    const prefixOf = n => { const w = n.split(' '); return w.length > 1 ? w.slice(0, -1).join(' ') : ''; };
    const pc = new Map(); items.forEach(it => { const k = prefixOf(it.p.name); if (k) pc.set(k, (pc.get(k) || 0) + 1); });
    const order = [];
    items.forEach(it => {
      const fg = faderGroups.find(g => g.includes(it));
      let key, kind = 'row';
      if (fg) { key = 'faders:' + sig(it) + ':' + (it.p.name.startsWith('R ') ? 'R' : 'L'); kind = 'faders'; }
      else { const pf = prefixOf(it.p.name); key = pf && pc.get(pf) >= 2 ? 'g:' + pf : 'main'; }
      let s = order.find(o => o.key === key);
      if (!s) { s = { key, kind, title: key === 'main' ? '' : kind === 'faders' ? (it.p.name.startsWith('R ') ? 'Right' : 'Bands') : key.slice(2), items: [] }; order.push(s); }
      s.items.push(it);
    });
    // the ungrouped params first, then the groups in order of appearance
    return order.sort((a, b) => (a.key === 'main' ? -1 : 0) - (b.key === 'main' ? -1 : 0));
  }

  function mount(root, opt) {
    const prod = opt.product, specs = opt.params, tr = opt.traits || {}, bridge = opt.bridge;
    const host = specs.map((p, i) => ({ p, i, c: makeCurve(p) }));
    if (tr.autoGain) host.push({ p: { id: 'common.autogain', name: 'Auto gain', min: 0, max: 1, def: 0, curve: 'step', steps: [0, 1], labels: ['Off', 'On'], auto: true }, i: host.length, extra: true });
    if (tr.delta) host.push({ p: { id: 'common.delta', name: 'Delta', min: 0, max: 1, def: 0, curve: 'step', steps: [0, 1], labels: ['Off', 'On'], auto: true }, i: host.length, extra: true });
    host.forEach(h => { if (!h.c) h.c = makeCurve(h.p); });
    const vals = bridge.values().slice();
    const widgets = new Map();       // host index -> update(plain)
    const undo = [], redo = []; let ab = 'A'; const slots = { A: null, B: null };

    root.classList.add('p');
    root.style.setProperty('--acc', prod.acc); root.style.setProperty('--acc2', prod.hi); root.style.setProperty('--ring', prod.ring);
    root.innerHTML = '';
    const box = el('div', 'root'); root.appendChild(box);

    function setValue(i, v, record) {
      const h = host[i]; v = h.p.curve === 'step' ? h.c.value(h.c.norm(v)) : clamp(v, Math.min(h.p.min, h.p.max), Math.max(h.p.min, h.p.max));
      if (record !== false && vals[i] !== v) { undo.push({ i, from: vals[i], to: v }); if (undo.length > 200) undo.shift(); redo.length = 0; refreshTb(); }
      vals[i] = v; bridge.set(i, v); const w = widgets.get(i); if (w) w(v);
    }
    function applyAll(values) { values.forEach((v, i) => { if (i < host.length && vals[i] !== v) { vals[i] = v; bridge.begin(i); bridge.set(i, v); bridge.end(i); const w = widgets.get(i); if (w) w(v); } }); }

    // ---- toolbar
    const tb = el('div', 'tb');
    tb.append(el('span', 'logo', 'SEVENTHWELL'), el('span', 'code', prod.code), el('span', 'nm', prod.name.toUpperCase()), el('span', 'sp'));
    const live = el('span', 'live'); live.append(el('span', 'dot'), el('span', '', ''));
    tb.insertBefore(live, tb.querySelector('.sp'));
    const bA = el('button', 'on', 'A'), bB = el('button', '', 'B'), bU = el('button', '', '↶'), bR = el('button', '', '↷');
    bU.title = 'Undo'; bR.title = 'Redo';
    tb.append(bA, bB, bU, bR);
    const extraBtn = {};
    host.filter(h => h.extra).forEach(h => { const b = el('button', '', h.p.name); b.onclick = () => setValue(h.i, vals[h.i] > 0.5 ? 0 : 1); extraBtn[h.i] = b; widgets.set(h.i, v => b.classList.toggle('on', v > 0.5)); tb.append(b); });
    box.append(tb, el('div', 'strip'));
    function refreshTb() { bU.disabled = !undo.length; bR.disabled = !redo.length; }
    bU.onclick = () => { const e = undo.pop(); if (!e) return; redo.push(e); vals[e.i] = e.from; bridge.begin(e.i); bridge.set(e.i, e.from); bridge.end(e.i); const w = widgets.get(e.i); if (w) w(e.from); refreshTb(); };
    bR.onclick = () => { const e = redo.pop(); if (!e) return; undo.push(e); vals[e.i] = e.to; bridge.begin(e.i); bridge.set(e.i, e.to); bridge.end(e.i); const w = widgets.get(e.i); if (w) w(e.to); refreshTb(); };
    function pickAB(which) {
      if (which === ab) return; slots[ab] = vals.slice(); if (!slots[which]) slots[which] = vals.slice();
      ab = which; bA.classList.toggle('on', ab === 'A'); bB.classList.toggle('on', ab === 'B'); applyAll(slots[which]);
    }
    bA.onclick = () => pickAB('A'); bB.onclick = () => pickAB('B'); refreshTb();

    // ---- body
    const body = el('div', 'body'); box.appendChild(body);
    const sections = groupParams(host.filter(h => !h.extra));
    for (const s of sections) {
      const sec = el('div', 'sec' + (s.kind === 'faders' ? ' wide' : '')); if (s.title) sec.append(el('div', 'sect', s.title));
      const row = el('div', 'row' + (s.kind === 'faders' ? ' faders' : '')); sec.append(row); body.append(sec);
      for (const it of s.items) row.append(s.kind === 'faders' ? fader(it) : control(it, s.title && it.p.name.startsWith(s.title + ' ') ? it.p.name.slice(s.title.length + 1) : it.p.name));
    }

    if ((prod.actions || []).length) {
      const sec = el('div', 'sec'); sec.append(el('div', 'sect', 'Actions')); const row = el('div', 'row'); sec.append(row); body.insertBefore(sec, body.firstChild);
      prod.actions.forEach(a => {
        const b = el('button', 'dbtn', a.label); let on = false;
        b.onclick = () => { if (a.toggle) { on = !on; b.classList.toggle('on', on); bridge.call(a.call, on ? '1' : '0'); } else { bridge.call(a.call, a.arg === undefined ? '' : a.arg); b.classList.add('on'); setTimeout(() => b.classList.remove('on'), 150); } };
        row.append(b);
      });
    }

    function control(it, label) {
      const p = it.p, i = it.i, c = it.c; label = label || p.name;
      if (p.curve === 'step') {
        const n = (p.steps || []).length;
        if (isMomentary(p)) {
          const w = el('div', 'ctl'); const b = el('button', 'dbtn', label);
          const down = e => { b.setPointerCapture(e.pointerId); bridge.begin(i); setValue(i, p.steps[1], false); b.classList.add('on'); };
          const up = () => { setValue(i, p.steps[0], false); bridge.end(i); b.classList.remove('on'); };
          b.addEventListener('pointerdown', down); b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up); w.append(b);
          widgets.set(i, v => b.classList.toggle('on', v > 0.5)); return w;
        }
        if (n === 2 && isOnOff(p)) {
          const w = el('div', 'ctl tog'); const b = el('button', 'dbtn', label); w.append(b);
          b.onclick = () => { bridge.begin(i); setValue(i, vals[i] > 0.5 ? p.steps[0] : p.steps[1]); bridge.end(i); };
          widgets.set(i, v => b.classList.toggle('on', c.norm(v) > 0.5)); b.classList.toggle('on', c.norm(vals[i]) > 0.5); return w;
        }
        if (n <= 6) {
          const w = el('div', 'ctl'); const seg = el('div', 'seg'); const bs = [];
          p.steps.forEach((s, k) => { const b = el('button', 'dbtn', (p.labels && p.labels[k]) || String(s)); b.onclick = () => { bridge.begin(i); setValue(i, s); bridge.end(i); }; seg.append(b); bs.push(b); });
          w.append(seg, el('div', 'lbl', label));
          const upd = v => { const k = Math.round(c.norm(v) * (n - 1)); bs.forEach((b, j) => b.classList.toggle('on', j === k)); }; widgets.set(i, upd); upd(vals[i]); return w;
        }
        const w = el('div', 'ctl sel'); const b = el('button', 'dbtn'); const t = el('span'), ar = el('span', '', '▾'); b.append(t, ar); w.append(b, el('div', 'lbl', label));
        b.onclick = e => {
          e.stopPropagation(); const m = el('div', 'menu'); const r = b.getBoundingClientRect(), rr = root.getBoundingClientRect();
          p.steps.forEach((s, k) => { const d = el('div', Math.abs(vals[i] - s) < 1e-9 ? 'cur' : '', (p.labels && p.labels[k]) || String(s)); d.onclick = () => { bridge.begin(i); setValue(i, s); bridge.end(i); m.remove(); }; m.append(d); });
          m.style.left = (r.left - rr.left) + 'px'; m.style.top = (r.bottom - rr.top + 2) + 'px'; root.append(m);
          const close = () => { m.remove(); document.removeEventListener('pointerdown', close, true); }; setTimeout(() => document.addEventListener('pointerdown', close, true), 0);
        };
        const upd = v => { t.textContent = format(p, v, c); }; widgets.set(i, upd); upd(vals[i]); return w;
      }
      // continuous: a digital arc knob
      const w = el('div', 'ctl'); const dk = el('div', 'dk'); dk.style.setProperty('--s', '56px'); const ptr = el('div', 'ptr'); ptr.append(el('i')); dk.append(ptr);
      const val = el('div', 'val'), lbl = el('div', 'lbl', label); w.append(dk, val, lbl);
      if (p.auto === false) dk.classList.add('dis');
      const upd = v => { const x = c.norm(v), deg = x * 270; dk.style.setProperty('--v', deg + 'deg'); ptr.style.transform = 'rotate(' + (deg - 135) + 'deg)'; val.textContent = format(p, v, c); };
      widgets.set(i, upd); upd(vals[i]);
      let drag = null;
      dk.addEventListener('pointerdown', e => { dk.setPointerCapture(e.pointerId); drag = { y: e.clientY, x0: c.norm(vals[i]) }; bridge.begin(i); });
      dk.addEventListener('pointermove', e => { if (!drag) return; const k = e.shiftKey ? 1000 : 180; setValue(i, c.value(clamp(drag.x0 + (drag.y - e.clientY) / k, 0, 1)), false); });
      const end = () => { if (!drag) return; const was = drag; drag = null; bridge.end(i); if (vals[i] !== was.v0) { /* undo entry for the whole gesture */ } };
      dk.addEventListener('pointerup', end); dk.addEventListener('pointercancel', end);
      dk.addEventListener('dblclick', () => { bridge.begin(i); setValue(i, p.def); bridge.end(i); });
      dk.addEventListener('wheel', e => { e.preventDefault(); bridge.begin(i); setValue(i, c.value(clamp(c.norm(vals[i]) - Math.sign(e.deltaY) * (e.shiftKey ? 0.005 : 0.02), 0, 1))); bridge.end(i); }, { passive: false });
      val.addEventListener('click', () => {
        const inp = el('input'); inp.value = format(p, vals[i], c); val.textContent = ''; val.append(inp); inp.focus(); inp.select();
        let fin = false; const done = ok => { if (fin) return; fin = true; const t = inp.value; if (inp.parentNode) inp.remove(); if (ok) { const v = parseValue(p, t); if (v !== null) { bridge.begin(i); setValue(i, v); bridge.end(i); } } upd(vals[i]); };
        inp.addEventListener('keydown', e => { if (e.key === 'Enter') done(true); else if (e.key === 'Escape') done(false); }); inp.addEventListener('blur', () => done(true));
      });
      return w;
    }

    function fader(it) {
      const p = it.p, i = it.i, c = it.c; const w = el('div', 'fader'); const ft = el('div', 'ft'), cap = el('i'); ft.append(cap);
      const val = el('div', 'val'), lbl = el('div', 'lbl', p.name.replace(/^(Band|R)\s+/, '')); w.append(ft, val, lbl);
      const upd = v => { cap.style.top = ((1 - c.norm(v)) * 100) + '%'; val.textContent = format(p, v, c).replace(/ dB$/, ''); }; widgets.set(i, upd); upd(vals[i]);
      const move = e => { const r = ft.getBoundingClientRect(); setValue(i, c.value(clamp(1 - (e.clientY - r.top) / r.height, 0, 1)), false); };
      ft.addEventListener('pointerdown', e => { ft.setPointerCapture(e.pointerId); bridge.begin(i); move(e); ft._d = true; });
      ft.addEventListener('pointermove', e => { if (ft._d) move(e); });
      ft.addEventListener('pointerup', () => { if (ft._d) { ft._d = false; bridge.end(i); } });
      ft.addEventListener('dblclick', () => { bridge.begin(i); setValue(i, p.def); bridge.end(i); });
      return w;
    }

    // ---- EVO bar and live readouts
    const bar = el('div', 'evobar'); const evoT = el('span', 'evot', prod.evo || ''); const cpu = el('span', 'cpu');
    const mk = lab => { const m = el('span', 'mtr'); const bars = [el('i'), el('i')]; bars.forEach(b => m.append(b)); m.title = lab; return { m, bars }; };
    const mi = mk('IN'), mo = mk('OUT');
    bar.append(el('span', 'evo', 'EVO'), evoT, el('span', 'sp'), el('span', 'ml', 'IN'), mi.m, el('span', 'ml', 'OUT'), mo.m, cpu); box.append(bar);
    const lvl = db => Math.max(0, Math.min(1, (db + 60) / 60)) * 100 + '%';
    function refreshInfo() {
      const inf = (bridge.info && bridge.info()) || {}; const lat = inf.latencyMs || 0;
      if (prod.line === 'LIVE' || lat > 0) { live.style.display = ''; live.lastChild.textContent = (prod.line === 'LIVE' ? 'LIVE ' : '') + lat.toFixed(1) + ' ms'; live.classList.toggle('lat', lat > 0); } else live.style.display = 'none';
      const mt = inf.meters; if (mt) { [mi.bars[0], mi.bars[1], mo.bars[0], mo.bars[1]].forEach((b, k) => { b.style.width = lvl(mt[k]); b.classList.toggle('hot', mt[k] > -1); }); }
      cpu.textContent = inf.cpu !== undefined ? 'CPU ' + inf.cpu.toFixed(1) + ' %' : '';
    }
    refreshInfo(); const timer = setInterval(refreshInfo, 60);
    bridge.onChange((i, v) => { if (i < host.length) { vals[i] = v; const w = widgets.get(i); if (w) w(v); } });
    return { destroy() { clearInterval(timer); root.innerHTML = ''; }, values: () => vals.slice(), host };
  }

  global.SWUI = { mount, makeCurve, format, parseValue };
  if (typeof module !== 'undefined') module.exports = global.SWUI;
})(typeof window !== 'undefined' ? window : globalThis);
