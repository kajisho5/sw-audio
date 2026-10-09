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
    const prod = opt.product, specs = opt.params, tr = opt.traits || {}, rawBridge = opt.bridge;
    const host = specs.map((p, i) => ({ p, i, c: makeCurve(p) }));
    if (tr.autoGain) host.push({ p: { id: 'common.autogain', name: 'Auto gain', min: 0, max: 1, def: 0, curve: 'step', steps: [0, 1], labels: ['Off', 'On'], auto: true }, i: host.length, extra: true });
    if (tr.delta) host.push({ p: { id: 'common.delta', name: 'Delta', min: 0, max: 1, def: 0, curve: 'step', steps: [0, 1], labels: ['Off', 'On'], auto: true }, i: host.length, extra: true });
    if (tr.bypass) host.push({ p: { id: 'common.bypass', name: 'Bypass', min: 0, max: 1, def: 0, curve: 'step', steps: [0, 1], labels: ['Off', 'On'], auto: true }, i: host.length, extra: true });
    host.forEach(h => { if (!h.c) h.c = makeCurve(h.p); });
    const vals = rawBridge.values().slice();
    // ---- undo / redo: one entry per gesture (everything between begin and end of the same parameters, not one per movement); a preset, A/B or Init is one entry for all the parameters it moved.
    // The window's bridge is wrapped to see the gestures: whatever the controls send goes through here. Undo and redo themselves talk to the raw bridge (they are not recorded).
    const undo = [], redo = [], gest = new Map(), gsets = new Map(); let pend = [], pendScheduled = false, lastRec = 0;
    const kUndo = 100;
    function record(items, label) {
      if (!items.length) return;
      const now = Date.now(), top = undo[undo.length - 1];
      // turning a wheel or clicking the same control again and again (gestures of one movement at most) is one step, within 0.6 s; a drag is always a step of its own
      const fine = items.length === 1 && !label && items[0].fine === true;
      if (fine && top && top.fine && top.items[0].i === items[0].i && now - lastRec < 600) top.items[0].to = items[0].to;
      else { undo.push({ items, label, fine }); if (undo.length > kUndo) undo.shift(); }
      lastRec = now; redo.length = 0; refreshTb();
    }
    function flushPending() { pendScheduled = false; const items = pend; pend = []; if (items.length) record(items); }
    function endGesture(i) {
      const from = gest.get(i), fine = (gsets.get(i) || 0) <= 1; gest.delete(i); gsets.delete(i);
      if (from === undefined || vals[i] === from) return;
      pend.push({ i, from, to: vals[i], fine }); if (!pendScheduled) { pendScheduled = true; Promise.resolve().then(flushPending); }   // the ends of one drag (an EQ dot: frequency and gain) arrive together
    }
    const bridge = Object.assign({}, rawBridge, { begin: i => { gest.set(i, vals[i]); gsets.set(i, 0); rawBridge.begin(i); }, set: (i, v) => { if (gsets.has(i)) gsets.set(i, gsets.get(i) + 1); rawBridge.set(i, v); }, end: i => { rawBridge.end(i); endGesture(i); } });
    const widgets = new Map();       // host index -> update(plain)
    const addWidget = (i, f) => { const prev = widgets.get(i); widgets.set(i, prev ? v => { prev(v); f(v); } : f); };   // several widgets may show one parameter (the design's control and the all-parameters drawer)
    let ab = 'A'; const slots = { A: null, B: null };

    root.classList.add('p');
    root.style.setProperty('--acc', prod.acc); root.style.setProperty('--acc2', prod.hi); root.style.setProperty('--ring', prod.ring);
    root.innerHTML = '';
    const box = el('div', 'root'); root.appendChild(box);

    function setValue(i, v, rec) {
      const h = host[i]; v = h.p.curve === 'step' ? h.c.value(h.c.norm(v)) : clamp(v, Math.min(h.p.min, h.p.max), Math.max(h.p.min, h.p.max));
      if (rec !== false && vals[i] !== v && !gest.has(i)) record([{ i, from: vals[i], to: v }]);   // outside a gesture: one step of its own
      vals[i] = v; bridge.set(i, v); const w = widgets.get(i); if (w) w(v);
    }
    function applyAll(values, label) {   // label: this is one step for undo ("Preset", "A / B", "Init"); without it nothing is recorded (the morph slider records its own gesture)
      const items = [];
      values.forEach((v, i) => { if (i < host.length && vals[i] !== v) { items.push({ i, from: vals[i], to: v }); vals[i] = v; rawBridge.begin(i); rawBridge.set(i, v); rawBridge.end(i); const w = widgets.get(i); if (w) w(v); } });
      if (label) record(items, label);
    }

    // ---- toolbar (the design's own in skin mode)
    let bA, bB, bU, bR, skinBox = null;
    const extraBtn = {};
    if (opt.skin) {
      const sh = root.attachShadow ? (root.shadowRoot || root.attachShadow({ mode: 'open' })) : root;
      const st = document.createElement('style'); st.textContent = opt.skin.css;
      const hold = document.createElement('div'); hold.innerHTML = opt.skin.html; skinBox = hold.firstElementChild;
      sh.append(st, skinBox);
      root.removeChild(box);
      const q = a => skinBox.querySelector('[data-act="' + a + '"]') || el('button');
      bA = q('A'); bB = q('B'); bU = q('undo'); bR = q('redo');
    } else {
      const tb = el('div', 'tb');
      tb.append(el('span', 'logo', 'SEVENTHWELL'), el('span', 'code', prod.code), el('span', 'nm', prod.name.toUpperCase()), el('span', 'sp'));
      const live = el('span', 'live'); live.append(el('span', 'dot'), el('span', '', ''));
      tb.insertBefore(live, tb.querySelector('.sp'));
      bA = el('button', 'on', 'A'); bB = el('button', '', 'B'); bU = el('button', '', '↶'); bR = el('button', '', '↷');
      bU.title = 'Undo'; bR.title = 'Redo';
      tb.append(bA, bB, bU, bR);
      host.filter(h => h.extra).forEach(h => { const b = el('button', '', h.p.name); b.onclick = () => setValue(h.i, vals[h.i] > 0.5 ? 0 : 1); extraBtn[h.i] = b; widgets.set(h.i, v => b.classList.toggle('on', v > 0.5)); tb.append(b); });
      box.append(tb, el('div', 'strip'));
    }
    function refreshTb() { bU.disabled = !undo.length; bR.disabled = !redo.length; if (skinBox) { bU.style.opacity = undo.length ? '' : '.4'; bR.style.opacity = redo.length ? '' : '.4'; } }
    const putItems = (items, key) => items.forEach(it => { vals[it.i] = it[key]; rawBridge.begin(it.i); rawBridge.set(it.i, it[key]); rawBridge.end(it.i); const w = widgets.get(it.i); if (w) w(it[key]); });
    const stepBack = () => { const e = undo.pop(); if (!e) return; redo.push(e); putItems(e.items, 'from'); lastRec = 0; refreshTb(); };
    const stepForward = () => { const e = redo.pop(); if (!e) return; undo.push(e); putItems(e.items, 'to'); lastRec = 0; refreshTb(); };
    bU.onclick = stepBack; bR.onclick = stepForward;
    // Ctrl / Cmd + Z undoes, Shift + Ctrl / Cmd + Z and Ctrl + Y redo (while the window has the keyboard; a text box keeps its own undo)
    document.addEventListener('keydown', e => {
      if (!(e.ctrlKey || e.metaKey) || e.altKey) return; const t = e.target, tag = t && t.tagName ? t.tagName.toLowerCase() : '';
      if (tag === 'input' || tag === 'textarea' || (t && t.isContentEditable)) return;
      const k = (e.key || '').toLowerCase();
      if (k === 'z' && !e.shiftKey) { e.preventDefault(); stepBack(); } else if ((k === 'z' && e.shiftKey) || k === 'y') { e.preventDefault(); stepForward(); }
    });
    let morphed = false, drawMorph = () => {};
    function pickAB(which) {
      if (which === ab && !morphed) return; if (!morphed) slots[ab] = vals.slice(); if (!slots[which]) slots[which] = vals.slice();
      morphed = false; ab = which; bA.classList.toggle('on', ab === 'A'); bB.classList.toggle('on', ab === 'B'); applyAll(slots[which], 'A / B ' + which); drawMorph(ab === 'A' ? 0 : 1);
    }
    bA.onclick = () => pickAB('A'); bB.onclick = () => pickAB('B'); if (skinBox) { bA.classList.add('on'); bB.classList.remove('on'); } refreshTb();
    // ---- morph slider of the design: between the A and B settings (continuous parameters move in their own scale, stepped ones switch at the middle); the slots themselves stay as they were
    const mph = skinBox && skinBox.querySelector('.morph');
    if (mph && mph.firstElementChild) {
      const knob = mph.firstElementChild, hit = document.createElement('span'); hit.style.cssText = 'position:absolute;inset:-9px -6px;cursor:ew-resize;touch-action:none'; mph.append(hit);
      drawMorph = t => { knob.style.left = (t * 100).toFixed(1) + '%'; }; drawMorph(0);
      let base = null, active = [];
      const put = e => {
        const r = mph.getBoundingClientRect(), t = clamp((e.clientX - r.left) / r.width, 0, 1); drawMorph(t);
        active.forEach(i => { const h = host[i], a = base.A[i], b = base.B[i]; const v = h.p.curve === 'step' ? (t < 0.5 ? a : b) : h.c.value(h.c.norm(a) + (h.c.norm(b) - h.c.norm(a)) * t); if (vals[i] !== v) { vals[i] = v; bridge.set(i, v); const w = widgets.get(i); if (w) w(v); } });
        return t;
      };
      hit.addEventListener('pointerdown', e => {
        hit.setPointerCapture(e.pointerId); if (!morphed) slots[ab] = vals.slice(); if (!slots.A) slots.A = vals.slice(); if (!slots.B) slots.B = slots.A.slice();
        base = { A: slots.A, B: slots.B }; active = host.filter(h => !h.extra && base.A[h.i] !== base.B[h.i] && h.p.auto !== false).map(h => h.i); active.forEach(i => bridge.begin(i)); morphed = true; hit.dataset.on = '1'; put(e);
      });
      hit.addEventListener('pointermove', e => { if (hit.dataset.on) put(e); });
      const end = e => { if (!hit.dataset.on) return; delete hit.dataset.on; const t = put(e); active.forEach(i => bridge.end(i)); ab = t < 0.5 ? 'A' : 'B'; bA.classList.toggle('on', ab === 'A'); bB.classList.toggle('on', ab === 'B'); };
      hit.addEventListener('pointerup', end); hit.addEventListener('pointercancel', end);
    }

    // ---- presets (the design's preset menu): the person's own settings, saved as files by the plug-in (bridge.call presetlist / presetsave / presetload / presetdelete; replies through bridge.onPreset)
    // A preset is the product's own parameters as "id=value" pairs; one that lacks a parameter leaves it as it is. The label shows the preset last loaded or saved ("*" once something moved),
    // "Init" while every parameter is at its default, otherwise "Custom".
    let presetDraw = null, bandHook = null;
    const own = host.filter(h => !h.extra), idx = new Map(own.map(h => [h.p.id, h.i]));
    const bodyOfValues = () => own.map(h => h.p.id + '=' + vals[h.i]).join(';');
    const applyValues = body => {   // the pairs of a body onto the current values (a parameter the body lacks stays); what is not a number or an id of this product is ignored
      const next = vals.slice();
      body.split(';').forEach(kv => { const e = kv.indexOf('='); if (e < 1) return; const i = idx.get(kv.slice(0, e)), v = parseFloat(kv.slice(e + 1)); if (i !== undefined && isFinite(v)) { const h = host[i]; next[i] = h.p.curve === 'step' ? h.c.value(h.c.norm(v)) : clamp(v, Math.min(h.p.min, h.p.max), Math.max(h.p.min, h.p.max)); } });
      morphed = false; applyAll(next, 'Preset');
    };
    const presetBtn = skinBox && bridge.onPreset && skinBox.querySelector('button[data-preset]');
    // History (the clock of the EVO bar): the last changes, newest first. Pressing one goes back to the state before it (as many undo steps as that takes); Redo brings them back.
    const histBtn = skinBox && skinBox.querySelector('button[data-history]');
    if (histBtn) {
      let pop = null;
      const labelOf = e => {
        if (e.label) return e.label;
        const it = e.items[0], h = host[it.i]; if (!h) return 'Change';
        return e.items.length === 1 ? h.p.name + ': ' + format(h.p, it.from, h.c) + ' \u2192 ' + format(h.p, it.to, h.c) : h.p.name + ' and ' + (e.items.length - 1) + ' more';
      };
      const close = () => { if (pop) { pop.remove(); pop = null; histBtn.classList.remove('on'); } };
      histBtn.addEventListener('click', ev => {
        ev.stopPropagation(); if (pop) { close(); return; }
        const a = skinBox.getBoundingClientRect(), b = histBtn.getBoundingClientRect(), k = skinBox.offsetWidth ? a.width / skinBox.offsetWidth : 1;
        pop = document.createElement('div');
        pop.style.cssText = 'position:absolute;z-index:70;min-width:220px;max-width:340px;max-height:260px;overflow:auto;background:#1c1d21;border:1px solid #2f3137;border-radius:6px;box-shadow:0 6px 18px rgba(0,0,0,.5);padding:4px 0;font:12px "Barlow Condensed",sans-serif;color:#e8e8e8;'
          + 'right:' + Math.max(4, (a.right - b.right) / k).toFixed(0) + 'px;bottom:' + ((a.bottom - b.top) / k + 6).toFixed(0) + 'px';
        const head = document.createElement('div'); head.textContent = undo.length ? 'Go back to before ...' : 'Nothing has been changed yet'; head.style.cssText = 'padding:3px 10px;color:#8a8c92;font-size:11px'; pop.append(head);
        undo.slice().reverse().forEach((e, n) => {
          const row = document.createElement('div'); row.textContent = labelOf(e); row.title = labelOf(e);
          row.style.cssText = 'padding:4px 10px;cursor:pointer;white-space:nowrap;overflow:hidden;text-overflow:ellipsis';
          row.onmouseenter = () => { row.style.background = '#2a2c31'; }; row.onmouseleave = () => { row.style.background = ''; };
          row.onclick = () => { for (let j = 0; j <= n; j++) stepBack(); close(); };
          pop.append(row);
        });
        skinBox.append(pop); histBtn.classList.add('on');
      });
      document.addEventListener('pointerdown', e => { if (pop && !(e.target && histBtn.contains(e.target))) { const p = e.composedPath ? e.composedPath() : []; if (!p.includes(pop)) close(); } });
    }
    // Lock (the LIVE header): a see-through sheet over everything below the header takes the presses, so a stray touch during a show changes nothing; the host's own automation still moves the parameters
    const lockBtn = skinBox && skinBox.querySelector('button[data-lock]');
    if (lockBtn) {
      const hdr = lockBtn.closest('.tb'), sheet = document.createElement('div'); let locked = false;
      sheet.style.cssText = 'position:absolute;left:0;right:0;bottom:0;z-index:60;display:none;cursor:not-allowed;background:rgba(0,0,0,.12)'; sheet.title = 'Locked: press Lock to unlock';
      skinBox.append(sheet);
      const place = () => { const a = skinBox.getBoundingClientRect(), h = hdr.getBoundingClientRect(); sheet.style.top = Math.max(0, h.bottom - a.top) + 'px'; };
      lockBtn.addEventListener('click', () => { locked = !locked; lockBtn.classList.toggle('on', locked); if (locked) place(); sheet.style.display = locked ? 'block' : 'none'; });
    }
    // copy / paste (LV03): the settings of this product as a text kept by the plug-in for the next window of the same product
    const copyBtn = skinBox && bridge.onPreset && skinBox.querySelector('button[data-copy]'), pasteBtn = skinBox && bridge.onPreset && skinBox.querySelector('button[data-paste]');
    if (copyBtn || pasteBtn) {
      const flash = (b, t) => { const x = b.textContent; b.textContent = t; setTimeout(() => { b.textContent = x; }, 1200); };
      if (copyBtn) copyBtn.addEventListener('click', () => { bridge.call('presetcopy', bodyOfValues()); flash(copyBtn, 'Copied'); });
      if (pasteBtn) pasteBtn.addEventListener('click', () => bridge.call('presetpaste', ''));
      bridge.onPreset((kind, a) => { if (kind === 'pasted') { applyValues(a); if (pasteBtn) flash(pasteBtn, 'Pasted'); } else if (kind === 'error' && pasteBtn && /copied/i.test(a || '')) flash(pasteBtn, 'Nothing copied'); });
    }
    if (presetBtn) {
      let names = [], current = null, snap = null, menu = null, note = '';
      const labelEl = (() => { const w = document.createTreeWalker(presetBtn, NodeFilter.SHOW_TEXT); let n; while ((n = w.nextNode())) if (n.nodeValue.trim()) return n; return null; })();
      const same = (a, b) => own.every(h => Math.abs(a[h.i] - b[h.i]) < 1e-9);
      const draw = () => {
        const isDef = own.every(h => Math.abs(vals[h.i] - h.p.def) < 1e-9);
        const t = current ? current + (snap && !same(vals, snap) ? ' *' : '') : (isDef ? 'Init' : 'Custom');
        if (labelEl) labelEl.nodeValue = t; else presetBtn.textContent = t;
        presetBtn.title = 'Presets';
      };
      const applyBody = (name, body) => { applyValues(body); current = name; snap = vals.slice(); draw(); };
      const closeMenu = () => { if (menu) { menu.remove(); menu = null; document.removeEventListener('pointerdown', outside, true); } };
      const outside = e => { if (menu && e.composedPath && (e.composedPath().includes(menu) || e.composedPath().includes(presetBtn))) return; closeMenu(); };
      const row = (text, onClick, cur) => { const d = el('div', cur ? 'cur' : '', text); d.style.cssText = 'padding:5px 12px;cursor:pointer;white-space:nowrap;font-size:13px;letter-spacing:.04em;border-radius:3px' + (cur ? ';color:var(--acc)' : ''); d.onmouseenter = () => { d.style.background = 'var(--acc)'; d.style.color = '#0c0c0d'; }; d.onmouseleave = () => { d.style.background = ''; d.style.color = cur ? 'var(--acc)' : ''; }; d.onclick = onClick; return d; };
      const buildMenu = () => {
        if (!menu) return; menu.innerHTML = '';
        menu.append(row('Init (default settings)', () => { morphed = false; applyAll(vals.map((v, i) => (host[i] && !host[i].extra ? host[i].p.def : v)), 'Init'); current = null; snap = null; draw(); closeMenu(); }));
        if (names.length) {
          const sep = el('div'); sep.style.cssText = 'height:1px;background:#3f4045;margin:4px 0;padding:0'; menu.append(sep);
          names.forEach(n => {
            const r = row(n, () => { bridge.call('presetload', encodeURIComponent(n)); closeMenu(); }, n === current);
            const x = el('span', '', '×'); x.title = 'Delete'; x.style.cssText = 'float:right;margin-left:14px;opacity:.55'; let armed = 0;
            x.onclick = e => { e.stopPropagation(); if (armed) { bridge.call('presetdelete', encodeURIComponent(n)); return; } armed = 1; x.textContent = 'Delete?'; x.style.opacity = '1'; setTimeout(() => { armed = 0; x.textContent = '×'; x.style.opacity = '.55'; }, 2500); };
            r.append(x); menu.append(r);
          });
        } else { const e = el('div', '', 'No saved presets yet'); e.style.cssText = 'padding:5px 12px;font-size:12px;opacity:.55'; menu.append(e); }
        const sep2 = el('div'); sep2.style.cssText = 'height:1px;background:#3f4045;margin:4px 0;padding:0'; menu.append(sep2);
        const sv = el('div'); sv.style.cssText = 'display:flex;gap:6px;padding:4px 8px;align-items:center';
        const inp = document.createElement('input'); inp.type = 'text'; inp.placeholder = 'Preset name'; inp.maxLength = 60; inp.value = current || '';
        inp.style.cssText = 'flex:1;min-width:110px;background:#0c0c0d;color:#e6e6e6;border:1px solid #3f4045;border-radius:3px;padding:4px 6px;font:12px "Space Mono",monospace;outline:none';
        const sb = el('button', '', 'Save'); sb.style.cssText = 'background:#17181b;color:#e6e6e6;border:1px solid #3f4045;border-radius:3px;padding:4px 10px;font:600 11px "Space Mono",monospace;cursor:pointer';
        const save = () => { const nm = inp.value.trim(); if (!nm) { inp.focus(); return; } bridge.call('presetsave', encodeURIComponent(nm), bodyOfValues()); };
        sb.onclick = save; inp.addEventListener('keydown', e => { if (e.key === 'Enter') save(); e.stopPropagation(); }); inp.addEventListener('keyup', e => e.stopPropagation());
        sv.append(inp, sb); menu.append(sv);
        if (note) { const n = el('div', '', note); n.style.cssText = 'padding:4px 12px;font-size:12px;color:#e8a05a'; menu.append(n); }
        const f = el('div', '', 'Saved in Documents/SW AUDIO/Presets'); f.style.cssText = 'padding:4px 12px 2px;font-size:11px;opacity:.45'; menu.append(f);
      };
      bridge.onPreset((kind, a, b) => {
        if (kind === 'list') { names = a || []; note = ''; if (b && names.includes(b)) { current = b; snap = vals.slice(); draw(); } buildMenu(); }
        else if (kind === 'loaded') { note = ''; applyBody(a, b); buildMenu(); }
        else if (kind === 'error') { note = a || 'Presets are not available'; buildMenu(); }
      });
      presetBtn.addEventListener('click', e => {
        e.stopPropagation(); if (menu) { closeMenu(); return; }
        menu = el('div', 'menu'); menu.style.cssText = 'position:absolute;z-index:50;background:#17181b;border:1px solid #3f4045;border-radius:5px;box-shadow:0 8px 20px rgba(0,0,0,.7);padding:4px;max-height:360px;overflow:auto;min-width:220px;color:#e6e6e6;font-family:"Barlow Condensed",sans-serif';
        const r = presetBtn.getBoundingClientRect(), rr = skinBox.getBoundingClientRect(); menu.style.left = Math.max(0, r.right - rr.left - 230) + 'px'; menu.style.top = (r.bottom - rr.top + 2) + 'px'; skinBox.append(menu);
        buildMenu(); setTimeout(() => document.addEventListener('pointerdown', outside, true), 0); bridge.call('presetlist', '');
      });
      presetBtn.style.cursor = 'pointer'; presetBtn.style.opacity = ''; presetBtn.removeAttribute('data-inert');
      skinBox.querySelectorAll('button[data-presetsave]').forEach(b => b.addEventListener('click', () => { if (!menu) presetBtn.click(); setTimeout(() => { const i = menu && menu.querySelector('input'); if (i) { i.focus(); i.select(); } }, 30); }));   // CS04 "Save chain"
      presetDraw = draw; draw();   // redrawn with the screen's timer (the "*" once a value moved)
    }
    // ---- body
    const body = el('div', 'body'); if (!skinBox) box.appendChild(body);
    const sections = skinBox ? [] : groupParams(host.filter(h => !h.extra));
    for (const s of sections) {
      const sec = el('div', 'sec' + (s.kind === 'faders' ? ' wide' : '')); if (s.title) sec.append(el('div', 'sect', s.title));
      const row = el('div', 'row' + (s.kind === 'faders' ? ' faders' : '')); sec.append(row); body.append(sec);
      for (const it of s.items) row.append(s.kind === 'faders' ? fader(it) : control(it, s.title && it.p.name.startsWith(s.title + ' ') ? it.p.name.slice(s.title.length + 1) : it.p.name));
    }

    if ((prod.actions || []).length && !skinBox) {
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
          addWidget(i, v => b.classList.toggle('on', v > 0.5)); return w;
        }
        if (n === 2 && isOnOff(p)) {
          const w = el('div', skinBox ? 'ctl onoff' : 'ctl tog'); const b = el('button', 'dbtn', label); w.append(b);   // (the designs' own .tog is the 3-D switch)
          b.onclick = () => { bridge.begin(i); setValue(i, vals[i] > 0.5 ? p.steps[0] : p.steps[1]); bridge.end(i); };
          addWidget(i, v => b.classList.toggle('on', c.norm(v) > 0.5)); b.classList.toggle('on', c.norm(vals[i]) > 0.5); return w;
        }
        if (n <= 6) {
          const w = el('div', 'ctl'); const seg = el('div', 'seg'); const bs = [];
          p.steps.forEach((s, k) => { const b = el('button', 'dbtn', (p.labels && p.labels[k]) || String(s)); b.onclick = () => { bridge.begin(i); setValue(i, s); bridge.end(i); }; seg.append(b); bs.push(b); });
          w.append(seg, el('div', 'lbl', label));
          const upd = v => { const k = Math.round(c.norm(v) * (n - 1)); bs.forEach((b, j) => b.classList.toggle('on', j === k)); }; addWidget(i, upd); upd(vals[i]); return w;
        }
        const w = el('div', 'ctl sel'); const b = el('button', 'dbtn'); const t = el('span'), ar = el('span', '', '▾'); b.append(t, ar); w.append(b, el('div', 'lbl', label));
        b.onclick = e => {
          e.stopPropagation(); const m = el('div', 'menu'); const host_ = skinBox || root, r = b.getBoundingClientRect(), rr = host_.getBoundingClientRect();
          p.steps.forEach((s, k) => { const d = el('div', Math.abs(vals[i] - s) < 1e-9 ? 'cur' : '', (p.labels && p.labels[k]) || String(s)); d.onclick = () => { bridge.begin(i); setValue(i, s); bridge.end(i); m.remove(); }; m.append(d); });
          m.style.left = (r.left - rr.left) + 'px'; m.style.top = (r.bottom - rr.top + 2) + 'px'; host_.append(m);
          const close = e => { if (e && e.composedPath && e.composedPath().includes(m)) return; m.remove(); document.removeEventListener('pointerdown', close, true); }; setTimeout(() => document.addEventListener('pointerdown', close, true), 0);   // a press inside the menu is the choice itself
        };
        const upd = v => { t.textContent = format(p, v, c); }; addWidget(i, upd); upd(vals[i]); return w;
      }
      // continuous: a digital arc knob
      const w = el('div', 'ctl'); const dk = el('div', 'dk'); dk.style.setProperty('--s', '56px'); const ptr = el('div', 'ptr'); ptr.append(el('i')); dk.append(ptr);
      const val = el('div', 'val'), lbl = el('div', 'lbl', label); w.append(dk, val, lbl);
      if (p.auto === false) dk.classList.add('dis');
      const upd = v => { const x = c.norm(v), deg = x * 270; dk.style.setProperty('--v', deg + 'deg'); ptr.style.transform = 'rotate(' + (deg - 135) + 'deg)'; val.textContent = format(p, v, c); };
      addWidget(i, upd); upd(vals[i]);
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
      const upd = v => { cap.style.top = ((1 - c.norm(v)) * 100) + '%'; val.textContent = format(p, v, c).replace(/ dB$/, ''); }; addWidget(i, upd); upd(vals[i]);
      const move = e => { const r = ft.getBoundingClientRect(); setValue(i, c.value(clamp(1 - (e.clientY - r.top) / r.height, 0, 1)), false); };
      ft.addEventListener('pointerdown', e => { ft.setPointerCapture(e.pointerId); bridge.begin(i); move(e); ft._d = true; });
      ft.addEventListener('pointermove', e => { if (ft._d) move(e); });
      ft.addEventListener('pointerup', () => { if (ft._d) { ft._d = false; bridge.end(i); } });
      ft.addEventListener('dblclick', () => { bridge.begin(i); setValue(i, p.def); bridge.end(i); });
      return w;
    }


    // ---- skin: tie the design's controls to the parameters (data-p, data-v, data-toggle, data-dial)
    let selBand = 0;                            // the band chip that is on (shared with the displays through ctx.band)
    function bindSkin() {
      let band = 0; const dyn = [];            // dyn: controls whose parameter depends on the chosen band
      const hostOf = i => host[i];
      // one control: cur() gives the host index it drives now; draw(v) shows a value
      function attachDial(ctl, cur) {
        const dial = ctl.querySelector('[data-dial]'), ptr = ctl.querySelector('.ptr, .kn'), val = ctl.querySelector('.val, .rv');   // .rc/.rk/.kn/.rv: the LIVE line's knobs
        const draw = () => { const i = cur(), h = hostOf(i); if (!h) return; const x = h.c.norm(vals[i]), deg = x * 270; dial.style.setProperty('--v', deg + 'deg'); if (ptr) ptr.style.transform = 'rotate(' + (deg - 135) + 'deg)'; if (val) val.textContent = ctl.dataset.zerotext && Math.abs(vals[i]) < 1e-9 ? ctl.dataset.zerotext : (ctl.dataset.valprefix || '') + format(h.p, vals[i], h.c); };
        let drag = null; dial.style.touchAction = 'none'; dial.style.cursor = 'ns-resize';
        dial.addEventListener('pointerdown', e => { const i = cur(); if (!hostOf(i)) return; dial.setPointerCapture(e.pointerId); drag = { i, y: e.clientY, x0: hostOf(i).c.norm(vals[i]) }; bridge.begin(i); });
        dial.addEventListener('pointermove', e => { if (!drag) return; setValue(drag.i, hostOf(drag.i).c.value(clamp(drag.x0 + (drag.y - e.clientY) / (e.shiftKey ? 1000 : 180), 0, 1)), false); });
        const end = () => { if (!drag) return; const i = drag.i; drag = null; bridge.end(i); };
        dial.addEventListener('pointerup', end); dial.addEventListener('pointercancel', end);
        dial.addEventListener('dblclick', () => { const i = cur(); if (!hostOf(i)) return; bridge.begin(i); setValue(i, hostOf(i).p.def); bridge.end(i); });
        dial.addEventListener('wheel', e => { e.preventDefault(); const i = cur(); if (!hostOf(i)) return; const c = hostOf(i).c; bridge.begin(i); setValue(i, c.value(clamp(c.norm(vals[i]) - Math.sign(e.deltaY) * (e.shiftKey ? 0.005 : 0.02), 0, 1))); bridge.end(i); }, { passive: false });
        return draw;
      }
      function attachButton(b, cur) {
        if (b.dataset.hold) {                        // press and hold: on while the button is down (Hold to mute / cough)
          b.style.touchAction = 'none';
          b.addEventListener('pointerdown', e => { b.setPointerCapture(e.pointerId); const i = cur(), h = hostOf(i); bridge.begin(i); setValue(i, h.p.steps[1], false); });
          const up = () => { const i = cur(), h = hostOf(i); if (vals[i] === h.p.steps[0]) return; setValue(i, h.p.steps[0], false); bridge.end(i); };
          b.addEventListener('pointerup', up); b.addEventListener('pointercancel', up);
          return () => { const i = cur(), h = hostOf(i); if (h) b.classList.toggle('on', h.c.norm(vals[i]) > 0.5); };
        }
        if (b.dataset.cycle !== undefined) {         // a chip that prints a parameter and its option ("Dither off"): a click steps to the next option
          const pre = b.dataset.cycle, low = !!b.dataset.lower;
          b.addEventListener('click', () => { const i = cur(), h = hostOf(i), st = h.p.steps, k = st.findIndex(v => Math.abs(v - vals[i]) < 1e-9); bridge.begin(i); setValue(i, st[(k + 1) % st.length]); bridge.end(i); });
          return () => { const i = cur(), h = hostOf(i); if (!h) return; const k = h.p.steps.findIndex(v => Math.abs(v - vals[i]) < 1e-9), lab = (h.p.labels && h.p.labels[k]) || String(vals[i]); b.textContent = b.dataset.fmt === 'os' ? lab.replace('x', '×') + ' OS' : pre + ' ' + (low ? lab.toLowerCase() : lab); };
        }
        const toggle = !!b.dataset.toggle, t = +b.dataset.v, inv = !!b.dataset.inv;   // inv: lit when the parameter is 0 (the power button of a product that has its own In parameter)
        b.addEventListener('click', () => { const i = cur(), h = hostOf(i); bridge.begin(i); setValue(i, toggle ? (vals[i] > 0.5 ? h.p.steps[0] : h.p.steps[1]) : t); bridge.end(i); });
        const tile = b.dataset.tile, tv = tile && b.querySelector('.tv'), dot = tile && b.querySelector('.dot, .offd');   // LIVE tiles: the value text and the lamp follow the parameter
        return () => { const i = cur(), h = hostOf(i); if (!h) return; const on = toggle ? (h.c.norm(vals[i]) > 0.5) !== inv : Math.abs(vals[i] - t) < 1e-9; b.classList.toggle('on', on); if (tv && toggle) tv.textContent = (h.p.labels && h.p.labels[on ? 1 : 0]) || (on ? 'On' : 'Off'); if (dot) dot.className = on ? 'dot' : 'offd'; };
      }
      // switches without a parameter (DY02 meter mode): a setting of the screen only, until the live meters are wired
      skinBox.querySelectorAll('[data-seg]').forEach(seg => {
        const bs = [...seg.querySelectorAll('button')], acc = 'var(--acc)';
        const show = k => { seg.dataset.sel = k; bs.forEach((b, j) => { const on = j === k; b.style.background = on ? acc : '#161617'; b.style.color = on ? '#0c0c0d' : '#cfcfcf'; b.style.borderColor = on ? acc : '#3a3a3d'; }); };
        bs.forEach((b, k) => b.addEventListener('click', () => show(k))); show(Math.max(0, bs.findIndex(b => b.dataset.on)));
      });
      // a stomp box: its type (None, Comp ... Reverb) sets the shell and the name; a click on the name steps to the next type
      skinBox.querySelectorAll('[data-typep]').forEach(el => {
        const i = +el.dataset.typep, names = JSON.parse(el.dataset.names), nm = el.querySelector('.pname'), h = hostOf(i);
        const draw = () => { const v = Math.round(h.c.norm(vals[i]) * (names.length - 1)); el.className = 'pedal t' + v; nm.textContent = v ? names[v] : 'Empty'; };
        nm.addEventListener('click', () => { const v = (Math.round(h.c.norm(vals[i]) * (names.length - 1)) + 1) % names.length; bridge.begin(i); setValue(i, h.p.steps[v]); bridge.end(i); });
        draw(); const prev = widgets.get(i); widgets.set(i, v => { if (prev) prev(v); draw(); });
      });
      const draws = new Map();                  // host index -> [draw functions]
      const reg = (i, f) => { (draws.get(i) || draws.set(i, []).get(i)).push(f); };
      // a vertical fader (LV03 Out): the cap (data-fader = the host index) slides inside its parent (top = 0 at the highest value), the module's .rv prints the value
      skinBox.querySelectorAll('[data-fader]').forEach(cap => {
        const i = +cap.dataset.fader, h = hostOf(i); if (!h) return;
        const track = cap.parentElement, rv = (cap.closest('.mod') || skinBox).querySelector('.rv'), capH = cap.style.height || '26px';
        const span = () => Math.max(1, track.clientHeight - parseFloat(capH));
        const draw = () => { cap.style.top = 'calc((100% - ' + capH + ') * ' + (1 - h.c.norm(vals[i])).toFixed(4) + ')'; if (rv) rv.textContent = format(h.p, vals[i], h.c); };
        cap.style.touchAction = 'none'; cap.style.cursor = 'ns-resize'; let drag = null;
        cap.addEventListener('pointerdown', e => { cap.setPointerCapture(e.pointerId); drag = { y: e.clientY, x0: h.c.norm(vals[i]) }; bridge.begin(i); });
        cap.addEventListener('pointermove', e => { if (drag) setValue(i, h.c.value(clamp(drag.x0 + (drag.y - e.clientY) / span() * (e.shiftKey ? 0.2 : 1), 0, 1)), false); });
        const end = () => { if (!drag) return; drag = null; bridge.end(i); }; cap.addEventListener('pointerup', end); cap.addEventListener('pointercancel', end);
        cap.addEventListener('dblclick', () => { bridge.begin(i); setValue(i, h.p.def); bridge.end(i); });
        reg(i, draw);
      });
      // a chip or tile that prints a parameter and its value ("Mix 50%", "NOM limit 4 mics"): a stepped parameter steps on a click, a continuous one is dragged like a knob (wheel and double click work too)
      skinBox.querySelectorAll('[data-valchip]').forEach(el => {
        const i = +el.dataset.valchip, h = hostOf(i); if (!h) return;
        const tv = el.querySelector('.tv'), pre = el.dataset.prefix || '', suf = el.dataset.suffix || '', stepped = h.p.curve === 'step';
        const draw = () => { const t = (pre ? pre + ' ' : '') + format(h.p, vals[i], h.c) + suf; if (tv) tv.textContent = t; else el.textContent = t; };
        el.style.touchAction = 'none'; el.style.cursor = stepped ? 'pointer' : 'ns-resize'; let drag = null;
        if (stepped) el.addEventListener('click', () => { const st = h.p.steps, k = st.findIndex(v => Math.abs(v - vals[i]) < 1e-9); bridge.begin(i); setValue(i, st[(k + 1) % st.length]); bridge.end(i); });
        else {
          el.addEventListener('pointerdown', e => { el.setPointerCapture(e.pointerId); drag = { y: e.clientY, x0: h.c.norm(vals[i]) }; bridge.begin(i); });
          el.addEventListener('pointermove', e => { if (drag) setValue(i, h.c.value(clamp(drag.x0 + (drag.y - e.clientY) / (e.shiftKey ? 1000 : 180), 0, 1)), false); });
          const end = () => { if (!drag) return; drag = null; bridge.end(i); }; el.addEventListener('pointerup', end); el.addEventListener('pointercancel', end);
          el.addEventListener('dblclick', () => { bridge.begin(i); setValue(i, h.p.def); bridge.end(i); });
          el.addEventListener('wheel', e => { e.preventDefault(); bridge.begin(i); setValue(i, h.c.value(clamp(h.c.norm(vals[i]) - Math.sign(e.deltaY) * (e.shiftKey ? 0.005 : 0.02), 0, 1))); bridge.end(i); }, { passive: false });
        }
        reg(i, draw);
      });
      const list = o => JSON.parse(o.dataset.pb), list2 = t => JSON.parse(t);
      skinBox.querySelectorAll('.ctl[data-p], .rc[data-p]').forEach(ctl => { const i = +ctl.dataset.p; if (hostOf(i)) reg(i, attachDial(ctl, () => i)); });
      skinBox.querySelectorAll('.ctl[data-pb], .rc[data-pb]').forEach(ctl => { const l = list(ctl), cur = () => l[Math.min(band, l.length - 1)], f = attachDial(ctl, cur); dyn.push(f); l.forEach(i => reg(i, f)); });
      skinBox.querySelectorAll('button[data-p], .btn[data-p], .chip[data-p], .bigbtn[data-p], .evo[data-p]').forEach(b => { const i = +b.dataset.p; if (hostOf(i)) reg(i, attachButton(b, () => i)); });
      // EQ02 "Dynamic": the selected band's Dyn Range is 0 (static) or not (dynamic); a press switches between 0 and -6 dB (data-dynpb = the Dyn Range of every band)
      skinBox.querySelectorAll('button[data-dynpb]').forEach(b => {
        const l = list2(b.dataset.dynpb), cur = () => l[Math.min(band, l.length - 1)];
        b.addEventListener('click', () => { const i = cur(); if (!hostOf(i)) return; bridge.begin(i); setValue(i, Math.abs(vals[i]) > 1e-9 ? 0 : -6); bridge.end(i); });
        const f = () => { const i = cur(); if (hostOf(i)) b.classList.toggle('on', Math.abs(vals[i]) > 1e-9); }; dyn.push(f); l.forEach(i => reg(i, f)); f();
      });
      // EQ02 "+": the first band that is off is switched on and selected (data-addband = the On parameter of every band)
      skinBox.querySelectorAll('button[data-addband]').forEach(b => b.addEventListener('click', () => {
        const on = list2(b.dataset.addband), k = on.findIndex(i => Math.abs(vals[i]) < 1e-9); if (k < 0) { b.title = 'All bands are in use'; return; }
        bridge.begin(on[k]); setValue(on[k], 1); bridge.end(on[k]);
        const sel = skinBox.querySelector('button[data-band="' + k + '"]'); if (sel) sel.click(); else if (bandHook) bandHook(k);
      }));
      skinBox.querySelectorAll('button[data-pb]').forEach(b => { const l = list(b), cur = () => l[Math.min(band, l.length - 1)], f = attachButton(b, cur); dyn.push(f); l.forEach(i => reg(i, f)); });
      // the 3-D toggle switches: lever up = the value in data-up, down = the other one (class dn)
      skinBox.querySelectorAll('.tog[data-tog]').forEach(t => {
        const i = +t.dataset.p, up = +t.dataset.up, h = hostOf(i); if (!h) return; t.style.cursor = 'pointer';
        t.addEventListener('click', () => { const other = h.p.steps.find(v => Math.abs(v - up) > 1e-9), isUp = Math.abs(vals[i] - up) < 1e-9; bridge.begin(i); setValue(i, isUp ? other : up); bridge.end(i); });
        reg(i, () => t.classList.toggle('dn', Math.abs(vals[i] - up) > 1e-9));
      });
      // one button, several parameters (MS01 Character corners): data-set = [[host index, value], ...]; lit while all of them are there
      skinBox.querySelectorAll('button[data-set]').forEach(b => {
        const set = JSON.parse(b.dataset.set), ids = set.map(x => x[0]);
        b.addEventListener('click', () => { ids.forEach(i => bridge.begin(i)); set.forEach(([i, v]) => setValue(i, v)); ids.forEach(i => bridge.end(i)); });
        const f = () => b.classList.toggle('on', set.every(([i, v]) => hostOf(i) && Math.abs(vals[i] - v) < 1e-6)); ids.forEach(i => reg(i, f));
      });
      // tap tempo (DL01): the average interval of the last taps (a gap over 2.5 s starts again) is written to the Time parameter (ms); data-tapoff = a switch (Sync) that is turned off
      // because Time would be snapped to a note length while it is on
      skinBox.querySelectorAll('button[data-tap]').forEach(b => {
        const ti = +b.dataset.tap, oi = b.dataset.tapoff === undefined ? -1 : +b.dataset.tapoff, h = hostOf(ti); if (!h) return; let taps = [];
        b.addEventListener('click', () => {
          const now = performance.now(); if (taps.length && now - taps[taps.length - 1] > 2500) taps = []; taps = taps.concat(now).slice(-5);
          b.classList.add('on'); setTimeout(() => b.classList.remove('on'), 120);
          if (taps.length < 2) return;
          const ms = clamp((taps[taps.length - 1] - taps[0]) / (taps.length - 1), h.p.min, h.p.max);
          bridge.begin(ti); setValue(ti, ms); bridge.end(ti);
          if (oi >= 0 && hostOf(oi) && Math.round(vals[oi]) !== 0) { bridge.begin(oi); setValue(oi, 0); bridge.end(oi); }
        });
      });
      // buttons that call a method of the core (ui/actions.json): Randomize, Ring out, Learn noise, Reset, Tap ...
      skinBox.querySelectorAll('button[data-call]').forEach(b => {
        const name = b.dataset.call, arg = b.dataset.arg === undefined ? '' : b.dataset.arg, tog = !!b.dataset.calltoggle; let on = false;
        b.addEventListener('click', () => { if (tog) { on = !on; b.classList.toggle('on', on); bridge.call(name, on ? '1' : '0'); } else { bridge.call(name, arg); b.classList.add('on'); setTimeout(() => b.classList.remove('on'), 150); } });
      });
      // GT03 "Add pedal": the first empty slot becomes a Comp pedal and is scrolled into view
      skinBox.querySelectorAll('button[data-addslot]').forEach(b => b.addEventListener('click', () => {
        const slots = JSON.parse(b.dataset.addslot), k = slots.findIndex(i => Math.round(vals[i]) === 0); if (k < 0) return; const i = slots[k];
        bridge.begin(i); setValue(i, hostOf(i).p.steps[1]); bridge.end(i);
        const peds = skinBox.querySelectorAll('.pedal'); if (peds[k] && peds[k].scrollIntoView) peds[k].scrollIntoView({ inline: 'center', block: 'nearest', behavior: 'smooth' });
      }));
      // GT03: drag a pedal (by its body) onto another one to swap their slots (type, on, A, B, C)
      const pedals = [...skinBox.querySelectorAll('.pedal[data-slot]')];
      pedals.forEach(ped => {
        let drag = null;
        ped.addEventListener('pointerdown', e => { if (e.target !== ped) return; ped.setPointerCapture(e.pointerId); drag = { x: e.clientX }; });
        ped.addEventListener('pointermove', e => { if (drag && Math.abs(e.clientX - drag.x) > 6) { ped.style.opacity = '.6'; ped.style.cursor = 'grabbing'; } });
        const end = e => {
          if (!drag) return; const moved = Math.abs(e.clientX - drag.x) > 6; drag = null; ped.style.opacity = ''; ped.style.cursor = 'grab'; if (!moved) return;
          const to = pedals.find(p => { if (p === ped) return false; const r = p.getBoundingClientRect(); return e.clientX >= r.left && e.clientX <= r.right; }); if (!to) return;
          const A = JSON.parse(ped.dataset.slot), B = JSON.parse(to.dataset.slot), va = A.map(i => vals[i]), vb = B.map(i => vals[i]);
          A.concat(B).forEach(i => bridge.begin(i)); A.forEach((i, k) => setValue(i, vb[k])); B.forEach((i, k) => setValue(i, va[k])); A.concat(B).forEach(i => bridge.end(i));
        };
        ped.addEventListener('pointerup', end); ped.addEventListener('pointercancel', () => { drag = null; ped.style.opacity = ''; });
      });
      const sel = [...skinBox.querySelectorAll('button[data-band]')];
      const drawSel = () => sel.forEach(b => b.classList.toggle('on', +b.dataset.band === band));
      sel.forEach(b => b.addEventListener('click', () => { band = +b.dataset.band; selBand = band; drawSel(); dyn.forEach(f => f()); }));
      bandHook = k => { band = k; selBand = k; drawSel(); dyn.forEach(f => f()); };   // a band that has no selector button of its own (EQ02 has 24 bands and 5 buttons)
      // the band shown first is the one the design marks as selected
      const first = sel.find(b => b.classList.contains('on')); if (first) { band = +first.dataset.band; selBand = band; } drawSel();
      draws.forEach((fs, i) => { const prev = widgets.get(i); widgets.set(i, v => { if (prev) prev(v); fs.forEach(f => f()); }); });
      draws.forEach(fs => fs.forEach(f => f())); dyn.forEach(f => f());
    }
    if (skinBox) bindSkin();

    // ---- EVO bar and live readouts
    const bar = el('div', 'evobar'); const evoT = el('span', 'evot', prod.evo || ''); const cpu = el('span', 'cpu');
    const mk = lab => { const m = el('span', 'mtr'); const bars = [el('i'), el('i')]; bars.forEach(b => m.append(b)); m.title = lab; return { m, bars }; };
    const mi = mk('IN'), mo = mk('OUT');
    if (skinBox) {
      const strip = document.createElement('div'); strip.style.cssText = 'height:22px;background:#0c0c0d;display:flex;align-items:center;justify-content:flex-end;gap:6px;padding:0 10px;border-top:1px solid #000;box-sizing:border-box';
      strip.append(el('span', 'ml', 'IN'), mi.m, el('span', 'ml', 'OUT'), mo.m, cpu);
      const st2 = document.createElement('style'); st2.textContent = '.ml{font-size:9px;color:#8e8e8e;font-family:Barlow Condensed,sans-serif}.mtr{display:inline-flex;flex-direction:column;gap:2px;width:70px}.mtr i{display:block;height:4px;width:0;background:#6fbf73;border-radius:1px}.mtr i.hot{background:#d9534f}.cpu{font-family:Space Mono,monospace;font-size:10px;color:#8e8e8e}';
      skinBox.parentNode.append(st2, strip);
    } else { bar.append(el('span', 'evo', 'EVO'), evoT, el('span', 'sp'), el('span', 'ml', 'IN'), mi.m, el('span', 'ml', 'OUT'), mo.m, cpu); box.append(bar); }
    const lvl = db => Math.max(0, Math.min(1, (db + 60) / 60)) * 100 + '%';
    // the design's own graphs, drawn from the parameters and the measured levels (ui/displays.js)
    const disp = skinBox && global.SWDISP ? global.SWDISP.attach(prod.code, skinBox, {
      value: name => { const h = host.find(x => x.p.name === name); return h ? vals[h.i] : undefined; },
      params: host.map(h => ({ name: h.p.name, i: h.i, p: h.p, c: h.c })), get: i => vals[i], set: (i, v) => setValue(i, v, false), begin: i => bridge.begin(i), end: i => bridge.end(i),
      band: () => selBand,
      call: (name, arg) => bridge.call(name, arg === undefined ? '' : arg),
      selectBand: k => { const b = skinBox.querySelector('button[data-band="' + k + '"]'); if (b) b.click(); else if (bandHook) bandHook(k); } }) : null;
    // the SW Link lamp of the bottom bar: lit while other SW AUDIO instances are in this host process (info.link[0], see plugin/clap/swlink.hpp)
    const linkLamp = skinBox ? skinBox.querySelector('.evr[data-link]') : null, linkDot = linkLamp ? linkLamp.querySelector('.evd') : null;
    function refreshLink(inf) {
      if (!linkLamp) return;
      const n = inf.link ? inf.link[0] : -2, on = n > 0;
      if (linkDot) { linkDot.style.background = on ? '' : '#55575c'; linkDot.style.boxShadow = on ? '' : 'none'; }
      const t = n === -2 ? 'SW Link' : n < 0 ? 'SW Link: this instance is not connected' : n === 0 ? 'SW Link: no other SW AUDIO plug-in in this host process (a host that runs plug-ins in separate processes cannot connect them)' : 'SW Link: ' + n + ' other SW AUDIO instance' + (n === 1 ? '' : 's') + ' in this host process';
      if (linkLamp.title !== t) linkLamp.title = t;
    }
    function refreshInfo() {
      const inf = (bridge.info && bridge.info()) || {}; const lat = inf.latencyMs || 0;
      refreshLink(inf);
      if (disp) disp.update(inf);
      if (presetDraw) presetDraw();
      if (skinBox) {   // the LIVE designs' own chip ("LIVE 0.0 ms", with the CPU in LV03's) and the CPU text of their bar show the real latency and CPU
        const lc = skinBox.querySelector('.live'), ev = [...skinBox.querySelectorAll('.evr')].find(e => /^CPU/.test(e.textContent.trim())), cp = inf.cpu !== undefined ? inf.cpu.toFixed(1) + '%' : null;
        if (lc && lc.lastChild && lc.lastChild.nodeType === 3) lc.lastChild.textContent = 'LIVE ' + lat.toFixed(1) + ' ms' + (/CPU/.test(lc.textContent) && cp ? '  CPU ' + cp : '');
        if (ev && cp) ev.textContent = 'CPU ' + cp;
      } else if (prod.line === 'LIVE' || lat > 0) { live.style.display = ''; live.lastChild.textContent = (prod.line === 'LIVE' ? 'LIVE ' : '') + lat.toFixed(1) + ' ms'; live.classList.toggle('lat', lat > 0); } else live.style.display = 'none';
      const mt = inf.meters; if (mt) { [mi.bars[0], mi.bars[1], mo.bars[0], mo.bars[1]].forEach((b, k) => { b.style.width = lvl(mt[k]); b.classList.toggle('hot', mt[k] > -1); }); }
      cpu.textContent = inf.cpu !== undefined ? 'CPU ' + inf.cpu.toFixed(1) + ' %' : '';
    }
    refreshInfo(); const timer = setInterval(refreshInfo, 60);
    bridge.onChange((i, v) => { if (i < host.length) { vals[i] = v; const w = widgets.get(i); if (w) w(v); } });
    // ---- all parameters: a drawer with a generic control for every parameter. The designs show a part of the parameters; the rest (a band's slope or placement, the FFT length, the order of modules ...)
    // could only be reached from the host's parameter list. The button sits at the right end of the EVO bar; the search box filters by name.
    if (skinBox) {
      const evob = skinBox.querySelector('.evob, .evobar') || skinBox.querySelector('.tb');   // (five rack-style designs have no EVO bar: the top bar then)
      if (evob) {
        const btn = el('button', '', 'All parameters'); btn.title = 'Every parameter of this plug-in'; btn.style.cssText = 'margin-left:8px;height:22px;padding:0 9px;border:1px solid #34363b;border-radius:4px;background:#1a1b1e;color:#cfcfcf;font:500 11px "Barlow Condensed",sans-serif;letter-spacing:.06em;cursor:pointer;white-space:nowrap';
        evob.append(btn);
        const st = document.createElement('style');
        // the generic controls' own rules (.p .dk, .p .ctl ...) copied for the drawer, because the designs' styles do not define them (and the shadow DOM does not see the page's)
        let gen = ''; for (const sh of document.styleSheets) { try { for (const r of sh.cssRules) { if (r.cssText && r.cssText.startsWith('.p ')) gen += r.cssText.replace(/\.p /g, '.allp ') + '\n'; } } catch (e) { /* a sheet from another origin */ } }
        st.textContent = gen + '.allp{position:absolute;inset:0;z-index:9999;isolation:isolate;background:#0c0c0d;display:flex;flex-direction:column;color:#e6e6e6;font-family:"Barlow Condensed",sans-serif}.allp-hd{display:flex;gap:10px;align-items:center;padding:10px 14px;border-bottom:1px solid #2a2a2e}.allp-hd b{font-size:13px;letter-spacing:.14em;text-transform:uppercase}.allp-hd .sp{flex:1}.allp-hd input{flex:1;max-width:260px;height:26px;background:#141416;border:1px solid #34363b;border-radius:4px;color:#e6e6e6;padding:0 8px;font:12px "Space Mono",monospace}.allp-hd button{height:26px;padding:0 12px;border:1px solid #34363b;border-radius:4px;background:#1a1b1e;color:#cfcfcf;cursor:pointer;font:500 12px "Barlow Condensed",sans-serif}.allp-body{overflow:auto;padding:12px 14px;flex:1;display:flex;flex-direction:column;gap:16px}.allp .menu{z-index:10000}';
        skinBox.parentNode.append(st);
        skinBox.style.position = skinBox.style.position || 'relative';
        let drawer = null;
        const build = () => {
          drawer = el('div', 'allp'); const hd = el('div', 'allp-hd'), q = el('input'), x = el('button', '', 'Close'), body2 = el('div', 'allp-body'); q.placeholder = 'Search'; hd.append(el('b', '', 'All parameters'), q, el('span', 'sp'), x); drawer.append(hd, body2);
          const secs = [];
          // parameters of one band / tap / voice share their names ("Freq" x 24): the id says which ("eq08.b3.freq" -> Band 3), so they are grouped by that
          const kinds = { b: 'Band', t: 'Tap', v: 'Voice', f: 'Filter', s: 'Step', m: 'Module', c: 'Channel', n: 'Node' }, byId = new Map(), rest = [];
          host.filter(h => !h.extra).forEach(h => { const seg = (h.p.id || '').split('.'), g = seg.length >= 3 && /^[a-z]\d+$/.test(seg[1]) ? seg[1] : null; if (g) { if (!byId.has(g)) byId.set(g, []); byId.get(g).push(h); } else rest.push(h); });
          const idSecs = [...byId.entries()].map(([g, items]) => ({ key: 'id:' + g, kind: 'row', title: (kinds[g[0]] || g[0].toUpperCase()) + ' ' + parseInt(g.slice(1), 10), items, byId: true }));
          const secList = groupParams(rest).concat(idSecs);
          secList.forEach(s => {
            const sec = el('div', 'sec'); if (s.title) sec.append(el('div', 'sect', s.title)); const row = el('div', 'row'); sec.append(row); body2.append(sec);
            const ctls = s.items.map(it => { const w = s.kind === 'faders' ? fader(it) : control(it, s.byId ? it.p.name : s.title && it.p.name.startsWith(s.title + ' ') ? it.p.name.slice(s.title.length + 1) : it.p.name); w.dataset.name = (s.title + ' ' + it.p.name).toLowerCase(); row.append(w); return w; });
            secs.push({ sec, ctls });
          });
          q.addEventListener('input', () => { const t = q.value.trim().toLowerCase(); secs.forEach(s => { let any = false; s.ctls.forEach(w => { const show = !t || w.dataset.name.includes(t); w.style.display = show ? '' : 'none'; if (show) any = true; }); s.sec.style.display = any ? '' : 'none'; }); });
          x.onclick = () => { drawer.style.display = 'none'; }; skinBox.append(drawer);
        };
        btn.onclick = () => { if (!drawer) build(); else drawer.style.display = drawer.style.display === 'none' ? 'flex' : 'none'; };
      }
    }
    return { destroy() { clearInterval(timer); root.innerHTML = ''; }, values: () => vals.slice(), host };
  }

  global.SWUI = { mount, makeCurve, format, parseValue };
  if (typeof module !== 'undefined') module.exports = global.SWUI;
})(typeof window !== 'undefined' ? window : globalThis);
