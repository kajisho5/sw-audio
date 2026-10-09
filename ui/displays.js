/* SW AUDIO — the live parts of the product screens (the design's graphs, drawn from the parameters and the measured levels).
   SWDISP.attach(code, skinBox, ctx) -> { update(info) } or null.
   ctx: { value(name) -> current plain value of the parameter with that name, or undefined }
   info.meters: [in L, in R, out L, out R] in dBFS (measured by the plug-in, every ~60 ms).
   The designs' own drawings are kept; what changes is the path data and the markers. */
(function (global) {
  'use strict';
  const NS = 'http://www.w3.org/2000/svg';
  const clamp = (x, a, b) => Math.min(b, Math.max(a, x));
  const peakDb = m => (m ? Math.max(m[0], m[1]) : -100);
  const outDb = m => (m ? Math.max(m[2], m[3]) : -100);
  // gain reduction in dB (>= 0): the core's own value when the plug-in sends it (readouts[0], <= 0), otherwise estimated from the input and output peaks (+ the make-up the output carries)
  const grOf = (info, makeup) => { const r = info && info.readouts; if (r && r.length) return Math.max(0, -r[0]); const m = info && info.meters; return m ? Math.max(0, peakDb(m) + (makeup || 0) - outDb(m)) : 0; };

  // ring of the last n values
  function Ring(n, init) { const a = new Array(n).fill(init); return { push(v) { a.shift(); a.push(v); }, a }; }

  // static compressor curve: input dB -> output dB (before make-up), soft knee
  function compCurve(inDb, thr, ratio, knee) {
    const over = inDb - thr;
    if (knee > 0 && Math.abs(over) <= knee / 2) { const t = over + knee / 2; return inDb + (1 / ratio - 1) * t * t / (2 * knee); }
    return over < 0 ? inDb : thr + over / ratio;
  }

  // the DY08 drawing: transfer curve (left square), level history and gain-reduction history (right)
  function compressorDisplay(box, ctx, names) {
    const disp = box.querySelector('.disp'); if (!disp) return null;
    const svg = disp.querySelector('svg'); if (!svg) return null;
    const paths = svg.querySelectorAll(':scope > path'), dot = svg.querySelector(':scope > circle');
    const hist = svg.querySelector(':scope > svg');
    if (paths.length < 1 || !dot || !hist) return null;                    // DY08 draws the curve twice (glow + line), MS06 once
    const hp = hist.querySelectorAll(':scope > path');                    // level (up), level (down), gain reduction
    if (hp.length < 3) return null;
    const X0 = 14, X1 = 216, Y0 = 216, Y1 = 14, LO = -60, HI = 0;         // the square: input and output -60 .. 0 dB
    const W = 692, H = 230, N = 36, dx = W / (N - 1);
    const lvl = Ring(N, -90), gr = Ring(N, 0);
    const px = d => X0 + (clamp(d, LO, HI) - LO) / (HI - LO) * (X1 - X0), py = d => Y0 - (clamp(d, LO, HI) - LO) / (HI - LO) * (Y0 - Y1);
    let tick = 0;
    return {
      update(info) {
        const m = info && info.meters, thr = ctx.value(names.thr), ratio = ctx.value(names.ratio), knee = ctx.value(names.knee) || 0, mk = ctx.value(names.makeup) || 0;
        if (thr === undefined || ratio === undefined) return;
        let d = '';
        for (let i = 0; i <= 40; i++) { const x = LO + (HI - LO) * i / 40, y = compCurve(x, thr, ratio, knee) + mk; d += (i ? ' L' : 'M') + px(x).toFixed(1) + ' ' + py(y).toFixed(1); }
        paths.forEach(p => p.setAttribute('d', d));
        const i = peakDb(m), o = outDb(m);
        dot.setAttribute('cx', px(i).toFixed(1)); dot.setAttribute('cy', py(o > -99 ? o : compCurve(i, thr, ratio, knee) + mk).toFixed(1));
        if (++tick % 2) return;                                          // the history moves at ~30 Hz
        lvl.push(i); gr.push(grOf(info, mk));
        let up = '', dn = '', g = '';
        for (let k = 0; k < N; k++) {
          const x = (k * dx).toFixed(1), a = clamp((lvl.a[k] + 60) / 60, 0, 1) * (H * 0.42);   // amplitude around the centre line
          up += (k ? ' L' : 'M') + x + ' ' + (H / 2 - a).toFixed(1); dn += (k ? ' L' : 'M') + x + ' ' + (H / 2 + a).toFixed(1);
          g += (k ? ' L' : 'M') + x + ' ' + (clamp(gr.a[k], 0, 24) / 24 * (H * 0.45)).toFixed(1);
        }
        hp[0].setAttribute('d', up + ' L' + W + ' ' + (H / 2) + ' L0 ' + (H / 2) + ' Z'); hp[1].setAttribute('d', dn + ' L' + W + ' ' + (H / 2) + ' L0 ' + (H / 2) + ' Z');
        hp[2].setAttribute('d', 'M0 0 ' + g.replace(/^M\S+ /, 'L0 ') + ' L' + W + ' 0 Z');
      }
    };
  }

  // ---- VU meters: the needle of the design's own face, moved by the measured level.
  // The face (viewBox 214 x 124, pivot 107,140) is the same in every design: the needle angle follows its printed scale.
  const VU_SCALE = [[-20, -50], [-10, -34], [-7, -22], [-5, -13], [-3, -3], [-1, 10], [0, 22], [1, 30], [2, 40], [3, 50]];   // VU -> degrees
  // gain-reduction face (DY03): 0 dB at the right end, 20 dB at the left; GR (dB, positive) -> degrees (the printed ticks 0, 2, 4, 6, 10, 20)
  const GR_SCALE = [[0, 50], [2, 30], [4, 10], [6, -10], [10, -30], [20, -50]];
  function vuAngle(vu, scale) {
    scale = scale || VU_SCALE;
    if (vu <= scale[0][0]) return scale[0][1];
    for (let k = 1; k < scale.length; k++) if (vu <= scale[k][0]) { const a = scale[k - 1], b = scale[k]; return a[1] + (b[1] - a[1]) * (vu - a[0]) / (b[0] - a[0]); }
    return scale[scale.length - 1][1];
  }
  // modes: 'gr' (needle falls from 0 with gain reduction), 'out' (output level, 0 VU = -15 dBFS peak), 'outLR' (one needle per channel)
  function vuDisplay(box, ctx, mode) {
    const needles = [...box.querySelectorAll('svg line[transform^="rotate("]')].filter(l => l.getAttribute('x1') === '107' && l.getAttribute('y1') === '140');
    if (!needles.length) return null;
    needles.forEach(n => { n.removeAttribute('transform'); n.style.transformOrigin = '107px 140px'; n.style.transition = 'transform 70ms linear'; });
    const cur = needles.map(() => (mode === 'gr3' ? 50 : -50));
    const seg = box.querySelector('[data-seg]');
    // the design's own meter buttons (DY01: GR, +8, +4, Off): they choose what the needle shows; a setting of the screen only
    const mb = seg ? [] : [...box.querySelectorAll('button')].filter(b => ['GR', '+4', '+8', '+10', 'OFF'].includes(b.textContent.trim().toUpperCase()));
    let label = null;
    if (mb.length >= 3) {
      const on = mb.find(b => b.classList.contains('on')); label = on ? on.textContent.trim().toUpperCase() : 'GR';
      mb.forEach(b => b.addEventListener('click', () => { mb.forEach(x => x.classList.toggle('on', x === b)); label = b.textContent.trim().toUpperCase(); }));
    }
    const REF = { '+4': 15, '+8': 11, '+10': 9 };
    return {
      update(info) {
        const m = info && info.meters; if (!m) return;
        const sel = seg ? +(seg.dataset.sel || 0) : (mode === 'gr' ? 0 : 1);       // DY02's own switch: GR, +4, +10
        if (mode === 'gr3') {                                                                       // DY03: its own face, GR only (peak in minus peak out)
          needles.forEach((n, k) => { const target = vuAngle(Math.min(20, grOf(info, ctx.value('Makeup') || 0)), GR_SCALE); cur[k] += (target - cur[k]) * (target < cur[k] ? 0.45 : 0.22); n.style.transform = 'rotate(' + cur[k].toFixed(1) + 'deg)'; });
          return;
        }
        needles.forEach((n, k) => {
          let vu;
          if (label === 'OFF') vu = -20;
          else if (label && label !== 'GR') vu = outDb(m) + REF[label];
          else if (label === 'GR') vu = -grOf(info);
          else if (mode === 'outLR') vu = (m[2 + (k % 2)] > -99 ? m[2 + (k % 2)] : -100) + 15;
          else if (sel === 0 && mode !== 'out') vu = -grOf(info);                                   // gain reduction (the core's own value)
          else vu = outDb(m) + (sel === 2 ? 9 : 15);                                                // +4 and +10 reference
          const target = vuAngle(Math.max(-20, Math.min(3, vu)));
          cur[k] += (target - cur[k]) * (target > cur[k] ? 0.45 : 0.22);                            // 300 ms ballistics at the 60 ms update
          n.style.transform = 'rotate(' + cur[k].toFixed(1) + 'deg)';
        });
      }
    };
  }

  // ---- tape reels (DL02, SA01): the Blender reels turn while sound goes through; the take-up reel turns faster (its pack is smaller)
  function reelDisplay(box, ctx, speedOf) {
    const reels = [...box.querySelectorAll('.reel')];
    if (!reels.length) return null;
    let rate = 0, want = 0, ang = reels.map(() => 0), last = 0, alive = true;
    const frame = t => {
      if (!alive || !box.isConnected) return;
      const dt = last ? Math.min(0.1, (t - last) / 1000) : 0; last = t;
      rate += (want - rate) * Math.min(1, dt * 3);                           // the reels spin up and coast down
      reels.forEach((r, k) => { ang[k] = (ang[k] + rate * (k ? 1.4 : 1) * dt) % 360; r.style.transform = 'rotate(' + ang[k].toFixed(1) + 'deg)'; });
      requestAnimationFrame(frame);
    };
    requestAnimationFrame(frame);
    return {
      update(info) {
        const m = info && info.meters; if (!m) return;
        want = peakDb(m) > -70 ? 110 * (speedOf ? speedOf(ctx) : 1) : 0;     // degrees per second
      },
      destroy() { alive = false; }
    };
  }

  // ---- rotary speaker (MD05): the horn and the drum turn at the speeds of the model in products/md05 (README: Speed, Accel); the Hz read-outs follow
  function rotaryDisplay(box, ctx) {
    const rot = { horn: box.querySelector('[data-rotor="horn"]'), drum: box.querySelector('[data-rotor="drum"]') };
    if (!rot.horn || !rot.drum) return null;
    const texts = [...box.querySelectorAll('svg text')], hornT = texts.find(t => /^Horn/.test(t.textContent)), drumT = texts.find(t => /^Drum/.test(t.textContent));
    const hz = { horn: 0, drum: 0 }, ang = { horn: 0, drum: 0 }; let last = 0, shown = 0, alive = true;
    const TARGET = [[0, 0], [0.8, 0.67], [6.7, 5.7]];                       // Stop, Slow, Fast (horn, drum)
    const frame = t => {
      if (!alive || !box.isConnected) return;
      const dt = last ? Math.min(0.1, (t - last) / 1000) : 0; last = t;
      const sp = Math.round(ctx.value('Speed') || 0), acc = ctx.value('Accel'), tau = 0.3 + 0.2 * (acc === undefined ? 5 : acc);
      hz.horn += (TARGET[sp][0] - hz.horn) * (1 - Math.exp(-dt / tau)); hz.drum += (TARGET[sp][1] - hz.drum) * (1 - Math.exp(-dt / (3 * tau)));
      for (const k of ['horn', 'drum']) { ang[k] = (ang[k] + hz[k] * 360 * dt) % 360; rot[k].style.transform = 'rotate(' + ang[k].toFixed(1) + 'deg)'; }
      if ((shown += dt) > 0.15) { shown = 0; if (hornT) hornT.textContent = 'Horn ' + hz.horn.toFixed(1) + ' Hz'; if (drumT) drumT.textContent = 'Drum ' + hz.drum.toFixed(1) + ' Hz'; }
      requestAnimationFrame(frame);
    };
    requestAnimationFrame(frame);
    return { update() {}, destroy() { alive = false; } };
  }

  // ---- parametric EQ curve (EQ02, EQ07, EQ08): the response of the bands, drawn on the design's own graph; the band dots can be dragged
  const FS = 48000;
  function biquadMag(type, f0, gainDb, q, f) {                        // RBJ cookbook, magnitude in dB at f
    const w0 = 2 * Math.PI * Math.min(f0, FS * 0.45) / FS, c = Math.cos(w0), sn = Math.sin(w0), A = Math.pow(10, gainDb / 40), al = sn / (2 * Math.max(0.05, q));
    let b0, b1, b2, a0, a1, a2;
    if (type === 'bell') { b0 = 1 + al * A; b1 = -2 * c; b2 = 1 - al * A; a0 = 1 + al / A; a1 = -2 * c; a2 = 1 - al / A; }
    else if (type === 'notch') { b0 = 1; b1 = -2 * c; b2 = 1; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; }
    else if (type === 'lowshelf') { const t = 2 * Math.sqrt(A) * al; b0 = A * ((A + 1) - (A - 1) * c + t); b1 = 2 * A * ((A - 1) - (A + 1) * c); b2 = A * ((A + 1) - (A - 1) * c - t); a0 = (A + 1) + (A - 1) * c + t; a1 = -2 * ((A - 1) + (A + 1) * c); a2 = (A + 1) + (A - 1) * c - t; }
    else if (type === 'highshelf') { const t = 2 * Math.sqrt(A) * al; b0 = A * ((A + 1) + (A - 1) * c + t); b1 = -2 * A * ((A - 1) + (A + 1) * c); b2 = A * ((A + 1) + (A - 1) * c - t); a0 = (A + 1) - (A - 1) * c + t; a1 = 2 * ((A - 1) - (A + 1) * c); a2 = (A + 1) - (A - 1) * c - t; }
    else if (type === 'lowcut') { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; }
    else { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; a0 = 1 + al; a1 = -2 * c; a2 = 1 - al; }   // highcut
    const w = 2 * Math.PI * f / FS, cw = Math.cos(w), sw = Math.sin(w), c2 = Math.cos(2 * w), s2 = Math.sin(2 * w);
    const nr = b0 + b1 * cw + b2 * c2, ni = -(b1 * sw + b2 * s2), dr = a0 + a1 * cw + a2 * c2, di = -(a1 * sw + a2 * s2);
    return 10 * Math.log10((nr * nr + ni * ni) / (dr * dr + di * di));
  }
  function eqDisplay(box, ctx) {
    const disp = box.querySelector('.disp'); const svg = disp && disp.querySelector('svg');
    if (!svg || !svg.getAttribute('viewBox') && !svg.getAttribute('viewbox')) return null;
    const vb = (svg.getAttribute('viewBox') || svg.getAttribute('viewbox')).split(/\s+/).map(Number), W = vb[2], H = vb[3];
    // the curve: the accent-coloured paths (area, glow, line); the design's example bands and notes after them are removed
    const kids = [...svg.children], isAxis = e => e.tagName === 'g' && /^(#6b6d72|#5b6470)$/i.test(e.getAttribute('fill') || '');
    const accent = (kids.filter(e => e.tagName === 'path' && e.getAttribute('fill') === 'none' && !/^#fff/i.test(e.getAttribute('stroke') || '')).pop() || {}).getAttribute && kids.filter(e => e.tagName === 'path' && e.getAttribute('fill') === 'none' && !/^#fff/i.test(e.getAttribute('stroke') || '')).pop().getAttribute('stroke');
    if (!accent) return null;
    const curve = kids.filter(e => e.tagName === 'path' && (e.getAttribute('stroke') === accent || e.getAttribute('fill') === accent));
    const area = curve.find(e => e.getAttribute('fill') === accent), line = curve.find(e => e.getAttribute('fill') === 'none' && +e.getAttribute('stroke-width') < 4), glow = curve.find(e => e.getAttribute('fill') === 'none' && +e.getAttribute('stroke-width') >= 4) || line;
    if (!area || !line) return null;
    for (let n = curve[curve.length - 1].nextElementSibling; n && !isAxis(n);) { const nx = n.nextElementSibling; n.remove(); n = nx; }
    // the bands: every 'On' starts one; Type, Freq, Gain, Q follow
    const bands = []; let cur = null;
    ctx.params.forEach(q => {
      if (q.name === 'On') { cur = { on: q.i }; bands.push(cur); }
      else if (cur && /^(Type|Freq|Gain|Q|Slope)$/.test(q.name) && cur[q.name] === undefined) cur[q.name] = q.i;
    });
    if (!bands.length) {                                                      // names with a band number and no On (LV13: Band 1 Type, Band 1 Freq ...): every band is always on
      const by = {}; ctx.params.forEach(q => { const m = /^Band (\d+) (Type|Freq|Gain|Q)$/.exec(q.name); if (m) { (by[m[1]] = by[m[1]] || { on: -1 })[m[2]] = q.i; } });
      Object.keys(by).sort((a, b) => a - b).forEach(k => { if (by[k].Freq !== undefined && by[k].Gain !== undefined) bands.push(by[k]); });
    }
    if (bands.length < 2) return null;
    const isOn = b => b.on < 0 || ctx.get(b.on) > 0.5;
    // the axes: from the printed labels when the design has them (50 ... 10k, +12 ... -12), otherwise the whole graph (20 Hz - 20 kHz, +-18 dB)
    const lab = {}; svg.querySelectorAll(':scope > g text').forEach(t => { lab[t.textContent.trim()] = t; });
    const FMIN = 20, FMAX = 20000; let X0 = 10, X1 = W - 10, YC = H / 2, DB = 18, YS = (H / 2 - 12) / DB;
    if (lab['50'] && lab['10k']) { const xa = +lab['50'].getAttribute('x'), xb = +lab['10k'].getAttribute('x'), k = Math.log(10000 / 50); const per = (xb - xa) / k; X0 = xa - per * Math.log(50 / FMIN); X1 = xb + per * Math.log(FMAX / 10000); }
    if (lab['+12'] && lab['-12']) { const ya = +lab['+12'].getAttribute('y') - 3, yb = +lab['-12'].getAttribute('y') - 3; YC = (ya + yb) / 2; DB = 12; YS = (yb - ya) / 24; }
    const fx = f => X0 + Math.log(f / FMIN) / Math.log(FMAX / FMIN) * (X1 - X0), xf = x => FMIN * Math.pow(FMAX / FMIN, (x - X0) / (X1 - X0));
    const gy = g => YC - g * YS, yg = y => (YC - y) / YS;
    const typeOf = b => {
      const lab = (ctx.params.find(q => q.i === b.Type).p.labels || [])[Math.round(ctx.get(b.Type))] || 'Bell', l = lab.toLowerCase(), f = ctx.get(b.Freq);
      if (l.includes('notch')) return 'notch';
      if (l.includes('cut')) return l.includes('lo') ? 'lowcut' : l.includes('hi') ? 'highcut' : (f < 1000 ? 'lowcut' : 'highcut');
      if (l.includes('shelf')) return l.includes('lo') ? 'lowshelf' : l.includes('hi') ? 'highshelf' : (f < 1000 ? 'lowshelf' : 'highshelf');
      return 'bell';
    };
    const NS_ = 'http://www.w3.org/2000/svg', acc = line.getAttribute('stroke') || '#5f9bff';
    const dots = bands.map((b, k) => {
      const c = document.createElementNS(NS_, 'circle'), t = document.createElementNS(NS_, 'text');
      c.setAttribute('r', '7'); c.setAttribute('fill', acc); c.setAttribute('stroke', '#0a0b0c'); c.setAttribute('stroke-width', '2'); c.style.cursor = 'grab';
      t.setAttribute('fill', '#0a0b0c'); t.setAttribute('font-size', '10'); t.setAttribute('text-anchor', 'middle'); t.setAttribute('font-family', 'Barlow Condensed, sans-serif'); t.style.pointerEvents = 'none'; t.textContent = String(k + 1);
      svg.append(c, t); return { c, t };
    });
    const pt = e => { const r = svg.getBoundingClientRect(); return [(e.clientX - r.left) / r.width * W, (e.clientY - r.top) / r.height * H]; };
    const norm = (i, v) => { const q = ctx.params.find(x => x.i === i); return q.c.value(q.c.norm(v)); };
    dots.forEach((d, k) => {
      const b = bands[k]; let drag = false;
      d.c.addEventListener('pointerdown', e => { e.stopPropagation(); d.c.setPointerCapture(e.pointerId); drag = true; ctx.selectBand(k); [b.Freq, b.Gain].forEach(i => ctx.begin(i)); });
      d.c.addEventListener('pointermove', e => { if (!drag) return; const [x, y] = pt(e); ctx.set(b.Freq, norm(b.Freq, xf(x))); ctx.set(b.Gain, norm(b.Gain, yg(y))); });
      const end = () => { if (!drag) return; drag = false; [b.Freq, b.Gain].forEach(i => ctx.end(i)); };
      d.c.addEventListener('pointerup', end); d.c.addEventListener('pointercancel', end);
      d.c.addEventListener('dblclick', e => { e.stopPropagation(); if (b.on < 0) return; ctx.begin(b.on); ctx.set(b.on, 0); ctx.end(b.on); });
      d.c.addEventListener('wheel', e => { e.preventDefault(); const q = ctx.params.find(x => x.i === b.Q); ctx.begin(b.Q); ctx.set(b.Q, q.c.value(Math.min(1, Math.max(0, q.c.norm(ctx.get(b.Q)) - Math.sign(e.deltaY) * 0.03)))); ctx.end(b.Q); }, { passive: false });
    });
    svg.addEventListener('dblclick', e => {                               // a double click on an empty place turns on the next free band there
      const free = bands.find(b => b.on >= 0 && ctx.get(b.on) < 0.5); if (!free) return; const [x, y] = pt(e);
      [free.on, free.Freq, free.Gain].forEach(i => ctx.begin(i)); ctx.set(free.on, 1); ctx.set(free.Freq, norm(free.Freq, xf(x))); ctx.set(free.Gain, norm(free.Gain, yg(y))); [free.on, free.Freq, free.Gain].forEach(i => ctx.end(i));
    });
    let last = null;
    return {
      fx, bands, svg,   // for what is drawn over the graph (EQ02's resonance marks)
      update() {
        const act = bands.filter(isOn).map(b => ({ type: typeOf(b), f: ctx.get(b.Freq), g: ctx.get(b.Gain), q: ctx.get(b.Q) || 1, slope: b.Slope !== undefined ? ctx.get(b.Slope) : 12 }));
        const key = act.map(a => a.type + a.f + a.g + a.q + a.slope).join('|'); if (key === last) { return; } last = key;
        let d = '';
        for (let i = 0; i <= 200; i++) {
          const f = xf(X0 + (X1 - X0) * i / 200); let db = 0;
          act.forEach(a => { const stages = (a.type === 'lowcut' || a.type === 'highcut') ? Math.max(1, Math.round((a.slope || 12) / 12)) : 1; for (let s = 0; s < stages; s++) db += biquadMag(a.type, a.f, a.g, a.type === 'lowcut' || a.type === 'highcut' ? 0.707 : a.q, f); });
          d += (i ? ' L' : 'M') + (X0 + (X1 - X0) * i / 200).toFixed(1) + ' ' + gy(Math.max(-DB * 1.3, Math.min(DB * 1.3, db))).toFixed(1);
        }
        line.setAttribute('d', d); glow.setAttribute('d', d); area.setAttribute('d', d + ' L' + X1 + ' ' + YC + ' L' + X0 + ' ' + YC + ' Z');
        bands.forEach((b, k) => { const on = isOn(b), x = fx(Math.min(FMAX, Math.max(FMIN, ctx.get(b.Freq)))), y = gy(Math.max(-DB, Math.min(DB, ctx.get(b.Gain)))); const ds = dots[k];
          ds.c.style.display = ds.t.style.display = on ? '' : 'none'; ds.c.setAttribute('cx', x.toFixed(1)); ds.c.setAttribute('cy', y.toFixed(1)); ds.t.setAttribute('x', x.toFixed(1)); ds.t.setAttribute('y', (y + 3.5).toFixed(1)); });
      }
    };
  }

  // ---- fader bank (LV12): the 31 faders of the design are the bands; Edit (Left / Right / Both) chooses the side that is shown and written
  function faderBank(box, ctx) {
    const tracks = [...box.querySelectorAll('[data-fader]')]; if (!tracks.length) return null;
    const editIdx = +tracks[0].dataset.edit, side = () => { const e = editIdx >= 0 ? Math.round(ctx.get(editIdx)) : 0; return e === 1 ? 'r' : 'l'; };   // 0 Left, 1 Right, 2 Both
    const idx = (t, which) => +(which === 'r' ? t.dataset.faderR : t.dataset.fader), q = i => ctx.params.find(x => x.i === i);
    const targets = t => (editIdx >= 0 && Math.round(ctx.get(editIdx)) === 2) ? [idx(t, 'l'), idx(t, 'r')] : [idx(t, side())];
    const cap = t => t.children[2];
    const val = (t, e) => { const r = t.getBoundingClientRect(), x = Math.min(1, Math.max(0, 1 - (e.clientY - r.top - 5) / (r.height - 10))); return x; };
    tracks.forEach(t => {
      let drag = false;
      const put = e => { targets(t).forEach(i => { const m = q(i); ctx.set(i, m.c.value(val(t, e))); }); };
      t.addEventListener('pointerdown', e => { t.setPointerCapture(e.pointerId); drag = true; targets(t).forEach(i => ctx.begin(i)); put(e); });
      t.addEventListener('pointermove', e => { if (drag) put(e); });
      const end = () => { if (!drag) return; drag = false; targets(t).forEach(i => ctx.end(i)); };
      t.addEventListener('pointerup', end); t.addEventListener('pointercancel', end);
      t.addEventListener('dblclick', () => targets(t).forEach(i => { ctx.begin(i); ctx.set(i, q(i).p.def); ctx.end(i); }));
    });
    let last = '';
    return { update() {
      const sd = side(), key = tracks.map(t => ctx.get(idx(t, sd))).join(',') + sd; if (key === last) return; last = key;
      tracks.forEach(t => { const m = q(idx(t, sd)), x = m.c.norm(ctx.get(m.i)); cap(t).style.top = 'calc((100% - 10px) * ' + (1 - x).toFixed(4) + ')'; });
    } };
  }


  // ---- multiband (DY10, DY11): the design's bands drawn at the real frequencies; the dividers / centres can be dragged.
  // The example spectrum of the design is removed (it is not measured). The height of a band bar is its Range (the deepest cut it may make).
  function multibandDisplay(box, ctx, mode) {
    const disp = box.querySelector('.disp'); const svg = disp && disp.querySelector('svg'); if (!svg) return null;
    const W = 932, FMIN = 20, FMAX = 20000, X0 = 3, X1 = W - 3, TOP = 2, BOT = 242;
    const fx = f => X0 + Math.log(clamp(f, FMIN, FMAX) / FMIN) / Math.log(FMAX / FMIN) * (X1 - X0), xf = x => FMIN * Math.pow(FMAX / FMIN, (x - X0) / (X1 - X0));
    const rects = [...svg.querySelectorAll(':scope > rect')], texts = [...svg.querySelectorAll(':scope > text')], lines = [...svg.querySelectorAll(':scope > line')].filter(l => !l.getAttribute('stroke-dasharray') && l.getAttribute('x1') === l.getAttribute('x2')), dots = [...svg.querySelectorAll(':scope > circle')];
    const sp = svg.querySelector(':scope > path'); if (sp) sp.remove();
    const byName = re => ctx.params.filter(q => re.test(q.name)).map(q => q.i);
    const freq = mode === 'xover' ? byName(/^Crossover \d$/) : byName(/^Band \d Freq$/);
    const range = byName(/^Band \d Range$/), n = rects.length;
    if (!freq.length || rects.length < 2 || (mode === 'xover' && freq.length !== n - 1) || (mode === 'centre' && freq.length !== n)) return null;
    const q = i => ctx.params.find(x => x.i === i), rh = k => 6 + (range[k] !== undefined ? Math.abs(ctx.get(range[k])) / 24 : 0.25) * (BOT - TOP - 40);
    // grid labels
    [[100, '100'], [1000, '1k'], [10000, '10k']].forEach(([f, t]) => { const g = document.createElementNS(NS, 'text'); g.setAttribute('x', fx(f).toFixed(1)); g.setAttribute('y', String(vbOf(svg)[3] - 16)); g.setAttribute('text-anchor', 'middle'); g.setAttribute('font-family', 'Barlow Condensed, sans-serif'); g.setAttribute('font-size', '10'); g.setAttribute('fill', '#6b6d72'); g.textContent = t; svg.prepend(g); });
    if (mode === 'centre') lines.forEach((l, k) => { l.style.display = 'none'; });
    const markers = mode === 'xover' ? lines.map((l, k) => ({ l, c: dots[k], i: freq[k] })) : freq.map((i, k) => ({ l: null, c: dots[k] || null, i }));
    if (mode === 'centre') { dots.forEach(c => { c.style.display = 'none'; }); markers.forEach((m, k) => { const c = document.createElementNS(NS, 'circle'); c.setAttribute('r', '5'); c.setAttribute('fill', '#f0ad3d'); c.setAttribute('cy', '128'); svg.append(c); m.c = c; }); }
    const pt = e => { const r = svg.getBoundingClientRect(); return (e.clientX - r.left) / r.width * W; };
    markers.forEach(m => {
      const el = [m.c, m.l].filter(Boolean); let drag = false;
      el.forEach(e => { e.style.cursor = 'ew-resize'; e.style.pointerEvents = 'all';
        e.addEventListener('pointerdown', ev => { ev.stopPropagation(); e.setPointerCapture(ev.pointerId); drag = true; ctx.begin(m.i); });
        e.addEventListener('pointermove', ev => { if (!drag) return; const pp = q(m.i); ctx.set(m.i, pp.c.value(pp.c.norm(xf(pt(ev))))); });
        const end = () => { if (!drag) return; drag = false; ctx.end(m.i); }; e.addEventListener('pointerup', end); e.addEventListener('pointercancel', end);
        e.addEventListener('dblclick', () => { ctx.begin(m.i); ctx.set(m.i, q(m.i).p.def); ctx.end(m.i); }); });
    });
    let last = '';
    return { update() {
      const fs = freq.map(i => ctx.get(i)), rs = range.map(i => ctx.get(i)), key = fs.join(',') + '|' + rs.join(','); if (key === last) return; last = key;
      let edges;
      if (mode === 'xover') { const xs = [...fs].sort((a, b) => a - b); edges = [FMIN, ...xs, FMAX]; markers.forEach((m, k) => { const x = fx(fs[k]).toFixed(1); m.l.setAttribute('x1', x); m.l.setAttribute('x2', x); m.c.setAttribute('cx', x); }); }
      else { const w = ctx.params.filter(p => /^Band \d Width$/.test(p.name)).map(p => ctx.get(p.i)); edges = null;
        markers.forEach((m, k) => { const x = fx(fs[k]); m.c.setAttribute('cx', x.toFixed(1)); const half = Math.pow(2, (w[k] || 1) / 2), a = fx(fs[k] / half), b = fx(fs[k] * half);
          rects[k].setAttribute('x', a.toFixed(1)); rects[k].setAttribute('width', Math.max(4, b - a).toFixed(1)); rects[k].setAttribute('height', rh(k).toFixed(1)); texts[k].setAttribute('x', x.toFixed(1)); }); return; }
      for (let k = 0; k < n; k++) { const a = fx(edges[k]) + (k ? 3 : 0), b = fx(edges[k + 1]) - (k < n - 1 ? 3 : 0), mid = fx(Math.sqrt(edges[k] * edges[k + 1]));
        rects[k].setAttribute('x', a.toFixed(1)); rects[k].setAttribute('width', Math.max(2, b - a).toFixed(1)); if (range.length) rects[k].setAttribute('height', rh(k).toFixed(1)); texts[k].setAttribute('x', mid.toFixed(1)); }
    } };
  }


  // ---- maximizer (MS01): the design's faders are Gain and Ceiling (the design called the first one Threshold; the spec has Gain), the bars are the
  // measured input / output peaks and the gain reduction (the core's own: slow stage + limiter, from the readouts trait; the estimate in - out + gain without it), the history is drawn from the same values.
  // Integrated is the core's (10 s memory, readouts[0]); short-term and true peak are not published by the core: a dash. Max GR is tracked; click it to reset.
  function maximizerDisplay(box, ctx) {
    const tracks = [...box.querySelectorAll('.track')], cols = [...box.querySelectorAll('.col')];
    const gainI = (ctx.params.find(q => q.name === 'Gain') || {}).i, ceilI = (ctx.params.find(q => q.name === 'Ceiling') || {}).i;
    if (tracks.length < 2 || gainI === undefined || ceilI === undefined) return null;
    const q = i => ctx.params.find(x => x.i === i), caps = tracks.map(t => t.querySelector('.fcap'));
    [[0, gainI, 'Gain'], [1, ceilI, 'Ceiling']].forEach(([k, i, nm]) => {
      const t = tracks[k], col = t.parentElement, lbl = col.querySelector('.lbl'); if (lbl) lbl.textContent = nm;
      t.style.cursor = 'ns-resize'; t.style.touchAction = 'none'; let drag = false;
      const put = e => { const r = t.getBoundingClientRect(), x = clamp(1 - (e.clientY - r.top - 11) / (r.height - 22), 0, 1), m = q(i); ctx.set(i, m.c.value(x)); };
      t.addEventListener('pointerdown', e => { t.setPointerCapture(e.pointerId); drag = true; ctx.begin(i); put(e); });
      t.addEventListener('pointermove', e => { if (drag) put(e); });
      const end = () => { if (!drag) return; drag = false; ctx.end(i); }; t.addEventListener('pointerup', end); t.addEventListener('pointercancel', end);
      t.addEventListener('dblclick', () => { ctx.begin(i); ctx.set(i, q(i).p.def); ctx.end(i); });
    });
    const colOf = name => cols.find(c => { const l = c.querySelector('.lbl'); return l && l.textContent.trim() === name; });
    const inC = colOf('In'), outC = colOf('Out'), grC = colOf('GR');
    const bars = c => c ? [...c.querySelectorAll('.bar')] : [], ro = c => c && c.querySelector('.ro');
    const COL = 'linear-gradient(to top,#2bd14a 0 70%,#f0c93d 70% 88%,#e0443e 88%)';
    const setBar = (b, db) => { const pct = clamp((db + 30) / 30, 0, 1) * 100; b.style.background = 'linear-gradient(to top,transparent 0 ' + pct.toFixed(1) + '%,rgba(20,21,23,.9) ' + pct.toFixed(1) + '%),' + COL; };
    const grFill = grC && grC.querySelector('div[style*="overflow"] > div');
    const svg = box.querySelector('.disp svg'), hp = svg ? svg.querySelectorAll(':scope > path') : [];
    const stats = [...box.querySelectorAll('.stat')], stat = n => stats.find(e => e.firstElementChild && e.firstElementChild.textContent.trim().toUpperCase() === n);
    ['INTEGRATED', 'SHORT-TERM', 'TRUE PEAK'].forEach(n => { const e = stat(n); if (e) { e.lastElementChild.textContent = '—'; e.title = 'Not measured by the screen'; } });
    const intE = stat('INTEGRATED');
    const maxE = stat('MAX GR'); let maxGr = 0; if (maxE) { maxE.style.cursor = 'pointer'; maxE.addEventListener('click', () => { maxGr = 0; }); }
    const W = 436, H = 150, N = 42, dx = W / (N - 1), lvl = Ring(N, -90), gr = Ring(N, 0); let tick = 0, shown = 0;
    return { update(info) {
      [[0, gainI], [1, ceilI]].forEach(([k, i]) => { const m = q(i), x = m.c.norm(ctx.get(i)); caps[k].style.top = 'calc((100% - 22px) * ' + (1 - x).toFixed(4) + ')'; const r = ro(tracks[k].parentElement); if (r) r.textContent = ctx.get(i).toFixed(1); });
      const m = info && info.meters; if (!m) return;
      const bi = bars(inC), bo = bars(outC); if (bi[0]) { setBar(bi[0], m[0]); setBar(bi[1], m[1]); } if (bo[0]) { setBar(bo[0], m[2]); setBar(bo[1], m[3]); }
      const ri = ro(inC), rO = ro(outC); if (ri) ri.textContent = Math.max(m[0], m[1]) > -99 ? Math.max(m[0], m[1]).toFixed(1) : '-∞'; if (rO) rO.textContent = outDb(m) > -99 ? outDb(m).toFixed(1) : '-∞';
      const rd = info.readouts && info.readouts.length >= 3 ? info.readouts : null, g = rd ? Math.max(0, -(rd[1] + rd[2])) : Math.max(0, peakDb(m) + (ctx.get(gainI) || 0) - outDb(m)), gv = rd ? Math.min(30, g) : (peakDb(m) > -70 ? Math.min(30, g) : 0);   // the core's own gain reduction (slow + limiter) when it is sent
      shown += (gv - shown) * 0.4; if (grFill) grFill.style.height = (clamp(shown / 25, 0, 1) * 100).toFixed(1) + '%'; const rg = ro(grC); if (rg) rg.textContent = shown.toFixed(1);
      if (intE && rd) intE.lastElementChild.textContent = rd[0] > -150 ? rd[0].toFixed(1) + ' LUFS' : '—';
      maxGr = Math.max(maxGr, gv); if (maxE) maxE.lastElementChild.textContent = (maxGr > 0.05 ? '-' : '') + maxGr.toFixed(1) + ' dB';
      if (hp.length >= 3 && ++tick % 4 === 0) {
        lvl.push(peakDb(m)); gr.push(gv); let up = '', dn = '', gg = '';
        for (let k = 0; k < N; k++) { const x = (k * dx).toFixed(1), a = clamp((lvl.a[k] + 40) / 40, 0, 1) * 66; up += (k ? ' L' : 'M') + x + ' ' + (75 - a).toFixed(1); dn += (k ? ' L' : 'M') + x + ' ' + (75 + a).toFixed(1); gg += ' L' + x + ' ' + (clamp(gr.a[k], 0, 25) / 25 * 70).toFixed(1); }
        hp[0].setAttribute('d', up + ' L' + W + ' 75 L0 75 Z'); hp[1].setAttribute('d', dn + ' L' + W + ' 75 L0 75 Z'); hp[2].setAttribute('d', 'M0 0' + gg + ' L' + W + ' 0 Z');
      }
    } };
  }


  // ======== displays drawn from the parameters only (and the measured level where noted) ========
  const svgOf = box => { const d = box.querySelector('.disp'); return d && d.querySelector('svg'); };
  const vbOf = svg => (svg.getAttribute('viewBox') || svg.getAttribute('viewbox') || '0 0 100 100').split(/\s+/).map(Number);
  const mkEl = (tag, attrs) => { const e = document.createElementNS(NS, tag); for (const k in attrs) e.setAttribute(k, attrs[k]); return e; };

  // ---- reverb decay (RV06, LV24, RS06): the tail falls 60 dB over Decay (straight in dB), after the pre-delay; the time axis grows when the decay is longer than 4 s
  function decayDisplay(box, ctx, cfg) {
    const svg = svgOf(box); if (!svg) return null;
    const paths = [...svg.querySelectorAll(':scope > path')], area = paths.find(e => e.getAttribute('fill') !== 'none'), glow = paths.find(e => +e.getAttribute('stroke-width') >= 5), line = paths.find(e => e.getAttribute('fill') === 'none' && +e.getAttribute('stroke-width') > 0 && +e.getAttribute('stroke-width') < 5);
    const texts = [...svg.querySelectorAll(':scope > text')], endT = texts.find(t => t.getAttribute('text-anchor') === 'end'), startT = texts.find(t => t.getAttribute('text-anchor') === 'start');
    if (!area || !line || !endT || !startT) return null;
    const [, , W, H] = vbOf(svg), X0 = +startT.getAttribute('x'), X1 = +endT.getAttribute('x'), BASE = H - 18, TOP = 40, ER = W * 0.05;
    let last = '';
    return { update() {
      const dec = Math.max(0.1, ctx.value(cfg.decay) || 1), pre = cfg.pre ? (ctx.value(cfg.pre) || 0) / 1000 : 0, key = dec + '|' + pre; if (key === last) return; last = key;
      const axis = Math.max(4, Math.ceil(pre + dec * 1.05)), tx = t => X0 + t / axis * (X1 - X0), x0 = cfg.pre ? tx(pre) : X0 + ER;
      let d = 'M' + x0.toFixed(1) + ' ' + TOP;
      for (let i = 0; i <= 60; i++) { const t = pre + dec * 1.05 * i / 60; if (t > axis) break; const a = clamp(1 - (t - pre) / dec, 0, 1) * 1.0; d += ' L' + (cfg.pre ? tx(t) : x0 + (tx(t) - X0)).toFixed(1) + ' ' + (BASE - a * (BASE - TOP)).toFixed(1); }
      d += ' L' + (cfg.pre ? tx(pre + dec * 1.05) : x0 + (tx(dec * 1.05) - X0)).toFixed(1) + ' ' + BASE;
      area.setAttribute('d', d + ' L' + x0.toFixed(1) + ' ' + BASE + ' Z'); line.setAttribute('d', d); if (glow) glow.setAttribute('d', d);
      endT.textContent = axis + ' s';
    } };
  }

  // ---- delay taps (DL04: six taps with their own time and level; LV25: repeats of Time with Feedback): bar = one repeat
  function delayTapsDisplay(box, ctx, mode) {
    const svg = svgOf(box); if (!svg) return null;
    const rects = [...svg.querySelectorAll(':scope > rect')]; if (rects.length < 7) return null;
    const dry = rects[0], taps = rects.slice(1, 7), endT = [...svg.querySelectorAll(':scope > text')].find(t => t.getAttribute('text-anchor') === 'end');
    const BASE = +dry.getAttribute('y') + +dry.getAttribute('height'), FULL = +dry.getAttribute('height'), X0 = +dry.getAttribute('x') + 5, [, , W] = vbOf(svg), X1 = W - 20;
    let last = '';
    return { update() {
      const items = [];
      for (let n = 1; n <= 6; n++) {
        if (mode === 'taps') { const on = ctx.value('Tap ' + n + ' On'), t = ctx.value('Tap ' + n + ' Time'), lv = ctx.value('Tap ' + n + ' Level'); items.push(on > 0.5 && t !== undefined ? { t, a: Math.pow(10, (lv || 0) / 20) } : null); }
        else { const t = ctx.value('Time'), fb = (ctx.value('Feedback') || 0) / 100; items.push(t ? { t: t * n, a: Math.pow(fb, n) } : null); }
      }
      const key = JSON.stringify(items); if (key === last) return; last = key;
      const tmax = Math.max(100, ...items.filter(Boolean).map(i => i.t)) * 1.05;
      taps.forEach((r, k) => { const it = items[k]; r.style.display = it ? '' : 'none'; if (!it) return; const h = clamp(it.a, 0.02, 1) * FULL, x = X0 + it.t / tmax * (X1 - X0); r.setAttribute('x', (x - 5).toFixed(1)); r.setAttribute('height', h.toFixed(1)); r.setAttribute('y', (BASE - h).toFixed(1)); });
      if (endT) endT.textContent = mode === 'taps' ? items.filter(Boolean).length + ' taps, ' + Math.round(tmax / 1.05) + ' ms' : '6 repeats, ' + Math.round(tmax / 1.05) + ' ms';
    } };
  }

  // ---- gate / ducker (LV16): the threshold line (drag it), the input level history on the same scale, Open / Closed from the measured input peak
  function gateDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const rects = [...svg.querySelectorAll(':scope > rect')], thrLine = [...svg.querySelectorAll(':scope > line')].find(l => l.getAttribute('stroke-dasharray')), wave = svg.querySelector(':scope > path');
    const texts = [...svg.querySelectorAll(':scope > text')], tT = texts.find(t => t.getAttribute('text-anchor') === 'start'), oT = texts.find(t => t.getAttribute('text-anchor') === 'end');
    if (!thrLine || !wave || !tT || !oT) return null;
    const [, , W, H] = vbOf(svg), yOf = db => 140 - (clamp(db, -60, 0) + 60) / 60 * 130, dbOf = y => (140 - y) / 130 * 60 - 60;
    rects.forEach(r => { r.style.display = 'none'; });
    const bg = mkEl('rect', { x: 0, y: 0, width: W, height: H, fill: '#f0ad3d', 'fill-opacity': '0' }); svg.prepend(bg);
    const hit = mkEl('rect', { x: 0, y: 0, width: W, height: H, fill: 'transparent' }); hit.style.cursor = 'ns-resize'; svg.append(hit);
    const thrI = (ctx.params.find(q => q.name === 'Threshold') || {}).i; let drag = false;
    const q = () => ctx.params.find(x => x.i === thrI), pt = e => { const r = svg.getBoundingClientRect(); return (e.clientY - r.top) / r.height * H; };
    hit.addEventListener('pointerdown', e => { hit.setPointerCapture(e.pointerId); drag = true; ctx.begin(thrI); ctx.set(thrI, q().c.value(q().c.norm(dbOf(pt(e))))); });
    hit.addEventListener('pointermove', e => { if (drag) ctx.set(thrI, q().c.value(q().c.norm(dbOf(pt(e))))); });
    const end = () => { if (!drag) return; drag = false; ctx.end(thrI); }; hit.addEventListener('pointerup', end); hit.addEventListener('pointercancel', end);
    const N = 60, hist = Ring(N, -90); let tick = 0;
    return { update(info) {
      const thr = ctx.get(thrI), duck = Math.round(ctx.value('Mode') || 0) === 1, y = yOf(thr);
      thrLine.setAttribute('y1', y.toFixed(1)); thrLine.setAttribute('y2', y.toFixed(1)); tT.textContent = 'Threshold ' + (+thr.toFixed(0)) + ' dB';
      const m = info && info.meters; if (!m) return;
      const lv = peakDb(m), open = lv > thr; bg.setAttribute('fill-opacity', open ? '0.12' : '0'); oT.textContent = duck ? (open ? 'Ducking' : 'Idle') : (open ? 'Open' : 'Closed');
      if (++tick % 2) return; hist.push(lv); let d = ''; for (let k = 0; k < N; k++) d += (k ? ' L' : 'M') + (k * W / (N - 1)).toFixed(1) + ' ' + yOf(hist.a[k]).toFixed(1); wave.setAttribute('d', d);
    } };
  }

  // ---- stereo width per band (ST01): each bar is centred; its width is the band's width setting (drag the bar sideways)
  function stereoBandsDisplay(box, ctx, names) {
    const svg = svgOf(box); if (!svg) return null;
    const rects = [...svg.querySelectorAll(':scope > rect')]; if (rects.length < names.length) return null;
    const [, , W] = vbOf(svg), CX = W / 2, SPAN = W - 20, idx = names.map(n => (ctx.params.find(q => q.name === n) || {}).i); if (idx.some(i => i === undefined)) return null;
    const q = i => ctx.params.find(x => x.i === i);
    idx.forEach((i, k) => { const r = rects[k]; r.style.cursor = 'ew-resize'; r.style.pointerEvents = 'all'; let drag = false;
      const put = e => { const b = svg.getBoundingClientRect(), x = (e.clientX - b.left) / b.width * W, m = q(i), v = m.p.min + (m.p.max - m.p.min) * clamp(2 * Math.abs(x - CX) / SPAN, 0, 1); ctx.set(i, m.c.value(m.c.norm(v))); };
      r.addEventListener('pointerdown', e => { r.setPointerCapture(e.pointerId); drag = true; ctx.begin(i); put(e); }); r.addEventListener('pointermove', e => { if (drag) put(e); });
      const end = () => { if (!drag) return; drag = false; ctx.end(i); }; r.addEventListener('pointerup', end); r.addEventListener('pointercancel', end); r.addEventListener('dblclick', () => { ctx.begin(i); ctx.set(i, q(i).p.def); ctx.end(i); }); });
    let last = '';
    return { update() { const vs = idx.map(i => ctx.get(i)), key = vs.join(','); if (key === last) return; last = key;
      vs.forEach((v, k) => { const m = q(idx[k]), w = Math.max(6, (v - m.p.min) / (m.p.max - m.p.min) * SPAN); rects[k].setAttribute('width', w.toFixed(1)); rects[k].setAttribute('x', (CX - w / 2).toFixed(1)); }); } };
  }

  // ---- the chips of ST01: Low / Low mid / High mid / High highlight that band's bar (the others dim; press it again to show all); Correlation shows / hides the stereo scope
  function st01Chips(box, ctx) {
    const btns = [...box.querySelectorAll('button')], names = ['low', 'low mid', 'high mid', 'high'], chips = names.map(n => btns.find(b => !b.hasAttribute('data-p') && b.textContent.trim().toLowerCase() === n));
    const svg = svgOf(box), rects = svg ? [...svg.querySelectorAll(':scope > rect')] : [];
    if (chips.every(Boolean) && rects.length >= 4) {
      let sel = chips.findIndex(c => c.classList.contains('on'));
      const draw = () => { chips.forEach((c, k) => c.classList.toggle('on', k === sel)); rects.slice(0, 4).forEach((r, k) => { r.style.opacity = sel < 0 || k === sel ? '' : '0.4'; }); };
      chips.forEach((c, k) => c.addEventListener('click', () => { sel = sel === k ? -1 : k; draw(); })); draw();
    }
    const corr = btns.find(b => b.textContent.trim() === 'Correlation'), sc = [...box.querySelectorAll('.disp svg')].find(v => v.querySelectorAll(':scope > circle').length >= 100), panel = sc && sc.closest('.disp');
    if (corr && panel) { let on = corr.classList.contains('on'); corr.addEventListener('click', () => { on = !on; corr.classList.toggle('on', on); panel.style.visibility = on ? '' : 'hidden'; }); }
    return null;
  }

  // ---- clipper (MS04): the transfer curve and the clipped waveform from Drive, Ceiling, Knee — the same curve as products/ms04 (clipCurve), without the oversampler
  function clipCurve(u, k) {
    const soft = (v, h) => { const a = Math.abs(v), s = v < 0 ? -1 : 1; if (h <= 0) return clamp(v, -1, 1); if (a <= 1 - h) return v; if (a >= 1 + h) return s; const d = a - (1 - h); return s * (a - d * d / (4 * h)); };
    k = clamp(k, 0, 1); if (k <= 0.5) return soft(u, k);
    const t = (k - 0.5) * 2, sv = soft(u, 0.5); return sv + t * (Math.tanh(u) - sv);
  }
  function clipperDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')], lines = [...svg.querySelectorAll(':scope > line')]; if (ps.length < 5 || lines.length < 2) return null;
    const ceilLine = lines[lines.length - 1];
    const CY = 100, SC = 86, LX0 = 14, LX1 = 186, RX0 = 202, RX1 = 726, A = 0.9;
    let last = '';
    return { update() {
      const dr = ctx.value('Drive') || 0, ce = ctx.value('Ceiling'), kn = (ctx.value('Knee') || 0) / 100, match = ctx.value('Gain match') > 0.5; if (ce === undefined) return;
      const key = [dr, ce, kn, match].join('|'); if (key === last) return; last = key;
      const dg = Math.pow(10, dr / 20), c = Math.pow(10, ce / 20), f = x => { let y = c * clipCurve(x * dg / c, kn); return match ? y / dg : y; };
      let d = ''; for (let i = 0; i <= 60; i++) { const x = -1 + 2 * i / 60; d += (i ? ' L' : 'M') + (LX0 + (x + 1) / 2 * (LX1 - LX0)).toFixed(1) + ' ' + (CY - clamp(f(x), -1.1, 1.1) * SC).toFixed(1); }
      ps[0].setAttribute('d', d); ps[1].setAttribute('d', d);
      let di = '', dy = ''; for (let i = 0; i <= 120; i++) { const x = RX0 + (RX1 - RX0) * i / 120, v = A * Math.sin(i / 120 * 4 * 2 * Math.PI); di += (i ? ' L' : 'M') + x.toFixed(1) + ' ' + (CY - v * SC).toFixed(1); dy += (i ? ' L' : 'M') + x.toFixed(1) + ' ' + (CY - clamp(f(v), -1.1, 1.1) * SC).toFixed(1); }
      ps[2].setAttribute('d', di); ps[3].setAttribute('d', dy); ps[4].setAttribute('d', dy);
      const yc = CY - c * SC; ceilLine.setAttribute('y1', yc.toFixed(1)); ceilLine.setAttribute('y2', yc.toFixed(1));
    } };
  }


  // ======== spectrum (the plug-in sends 64 log-spaced bands of the output, 20 Hz - 20 kHz, in dB: plugin/clap/gui_spectrum.hpp) ========
  function Smooth() { const a = new Array(64).fill(-120); return { a, feed(sp) { for (let b = 0; b < 64; b++) { const v = sp[b] === undefined ? -120 : sp[b]; a[b] += (v - a[b]) * (v > a[b] ? 0.6 : 0.2); } return a; } }; }
  const specDb = (v, lo) => clamp((v - lo) / -lo, 0, 1);       // lo .. 0 dB -> 0 .. 1

  // the design's grey spectrum area is replaced by the measured one (MT02, MD06, LV09, LV08, LV02, LO01, SA05, DY05)
  function spectrumPath(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const cand = [...svg.querySelectorAll(':scope > path')].filter(e => e.getAttribute('fill') && e.getAttribute('fill') !== 'none');
    const area = cand.sort((a, b) => (b.getAttribute('d') || '').length - (a.getAttribute('d') || '').length)[0];
    if (!area || (area.getAttribute('d') || '').split('L').length < 20) return null;
    const [, , W, H] = vbOf(svg), sm = Smooth(), TOP = H * 0.06;
    return { update(info) {
      const sp = info && info.spectrum; if (!sp) return;
      const a = sm.feed(sp); let d = 'M0 ' + H;
      for (let b = 0; b < 64; b++) d += ' L' + ((b + 0.5) / 64 * W).toFixed(1) + ' ' + (H - specDb(a[b], -90) * (H - TOP)).toFixed(1);
      area.setAttribute('d', d + ' L' + W + ' ' + H + ' Z');
    } };
  }

  // ---- "Compare A" of MT02: the first press keeps the long-term average (about 3 s) of the measured spectrum as the reference curve (a dashed line); the shading goes on showing the signal now. Press again to clear.
  // (The core has captureReference() / compareDb() over its own FFT bins; the screen's spectrum is the 64 bands every product has, so the reference is kept here.)
  function compareReference(box, ctx) {
    const svg = svgOf(box), btn = box.querySelector('button[data-compare]'); if (!svg || !btn) return null;
    const [, , W, H] = vbOf(svg), TOP = H * 0.06, slow = new Array(64).fill(-120), line = mkEl('path', { fill: 'none', stroke: '#ffffff', 'stroke-opacity': 0.85, 'stroke-width': 1.4, 'stroke-dasharray': '5 4' }); svg.append(line);
    let ref = null, seen = false;
    btn.addEventListener('click', () => { if (ref) { ref = null; line.setAttribute('d', ''); btn.classList.remove('on'); return; } if (!seen) return; ref = slow.slice(); btn.classList.add('on'); let d = ''; for (let b = 0; b < 64; b++) d += (b ? ' L' : 'M') + ((b + 0.5) / 64 * W).toFixed(1) + ' ' + (H - specDb(ref[b], -90) * (H - TOP)).toFixed(1); line.setAttribute('d', d); });
    return { update(info) { const sp = info && info.spectrum; if (!sp) return; seen = true; for (let b = 0; b < 64; b++) { const v = sp[b] === undefined ? -120 : sp[b]; slow[b] += (v - slow[b]) * 0.02; } } };
  }

  // ---- "Analyzer" of EQ08: the measured spectrum (the 64 bands every product has, 20 Hz - 20 kHz) as a soft area behind the EQ curve; the button shows / hides it (off until pressed)
  function analyzerBackdrop(box, ctx) {
    const svg = svgOf(box), btn = box.querySelector('button[data-analyzer]'); if (!svg || !btn) return null;
    const [, , W, H] = vbOf(svg), sm = Smooth(), area = mkEl('path', { fill: '#8a8c92', 'fill-opacity': 0.22 }); svg.insertBefore(area, svg.firstChild);
    let on = false; btn.addEventListener('click', () => { on = !on; btn.classList.toggle('on', on); if (!on) area.setAttribute('d', ''); });
    return { update(info) {
      const sp = info && info.spectrum; if (!sp || !on) return;
      const a = sm.feed(sp); let d = 'M10 ' + H;
      for (let b = 0; b < 64; b++) d += ' L' + (10 + (b + 0.5) / 64 * (W - 20)).toFixed(1) + ' ' + (H - specDb(a[b], -90) * (H - 12)).toFixed(1);
      area.setAttribute('d', d + ' L' + (W - 10) + ' ' + H + ' Z');
    } };
  }

  // ---- "Auto thresh" of EQ07 (core: learnThresholds; readouts = listening, progress): while it listens the button says how far it is
  function learnButton(box, ctx) {
    const btn = box.querySelector('button[data-call="learn"]'); if (!btn) return null; const label = btn.textContent.trim();
    return { update(info) { const r = info && info.readouts; if (!r || r.length < 2) return; const t = r[0] > 0.5 ? (r.length >= 3 ? 'Listening: ' + Math.round(r[2]) + ' hits (press to finish)' : 'Listening ' + Math.round(r[1] * 100) + ' %') : label; if (btn.textContent !== t) btn.textContent = t; btn.classList.toggle('on', r[0] > 0.5); } };
  }

  // ---- EQ02 "Assist" (core: setAssist, resonances; readouts = assist on, then 6 x [Hz, dB it sticks out]): the button switches the listening; the resonances are marked on the EQ graph (small triangles
  // with their frequency); pressing a mark puts a narrow Bell there on the first band that is off, cutting a part of what sticks out (Q 6, -0.7 x the excess, at most -12 dB)
  function assistMarks(box, ctx, eq) {
    const btn = box.querySelector('button[data-call="assist"]'); if (btn) btn.classList.remove('on');   // the design shows it lit: the core starts with it off
    if (!eq || !eq.svg) return btn ? { update(info) { const r = info && info.readouts; if (r && r.length >= 13) btn.classList.toggle('on', r[0] > 0.5); } } : null;
    const g = mkEl('g', {}); eq.svg.append(g); let marks = [], key = '';
    const place = k => {
      const m = marks[k], free = eq.bands.find(b => b.on >= 0 && ctx.get(b.on) < 0.5); if (!m || !free) return;
      const q = i => ctx.params.find(x => x.i === i), v = (i, x) => { const p = q(i); return p.c.value(p.c.norm(x)); };
      const ids = [free.on, free.Type, free.Freq, free.Gain, free.Q]; ids.forEach(i => ctx.begin(i));
      ctx.set(free.on, 1); ctx.set(free.Type, 0); ctx.set(free.Freq, v(free.Freq, m.hz)); ctx.set(free.Gain, v(free.Gain, -clamp(m.db * 0.7, 3, 12))); ctx.set(free.Q, v(free.Q, 6)); ids.forEach(i => ctx.end(i));
      ctx.selectBand(eq.bands.indexOf(free));
    };
    g.addEventListener('pointerdown', e => { const t = e.target.closest ? e.target.closest('[data-k]') : null; if (t) { e.stopPropagation(); place(+t.dataset.k); } });
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 13) return; const on = r[0] > 0.5; if (btn) btn.classList.toggle('on', on);
      marks = []; if (on) for (let i = 0; i < 6; i++) { const hz = r[1 + 2 * i], db = r[2 + 2 * i]; if (hz > 0) marks.push({ hz, db }); }
      const k = marks.map(m => m.hz.toFixed(0) + ':' + m.db.toFixed(1)).join('|'); if (k === key) return; key = k;
      g.innerHTML = marks.map((m, i) => { const x = eq.fx(m.hz); return '<path data-k="' + i + '" d="M' + (x - 6).toFixed(1) + ' 6 L' + (x + 6).toFixed(1) + ' 6 L' + x.toFixed(1) + ' 18 Z" fill="#f0ad3d" style="cursor:pointer"><title>Resonance ' + (m.hz >= 1000 ? (m.hz / 1000).toFixed(2) + ' kHz' : m.hz.toFixed(0) + ' Hz') + ', ' + m.db.toFixed(0) + ' dB: press to cut it</title></path><text x="' + x.toFixed(1) + '" y="30" text-anchor="middle" font-family="Barlow Condensed, sans-serif" font-size="10" fill="#f0ad3d" style="pointer-events:none">' + (m.hz >= 1000 ? (m.hz / 1000).toFixed(2) + 'k' : m.hz.toFixed(0)) + '</text>'; }).join('');
    } };
  }

  // ---- EQ02 "Unmask" (SW Link, plugin/clap/swlink.hpp): info.link = [the other SW AUDIO instances in this process, then - once the button asked for it ("c linkwatch 1") - the sum of their output spectra,
  // 64 dB values]. Where this track and the others are both strong, their energy overlaps and one masks the other: those bands are shaded red behind the curve (the darker, the more both are at their
  // loudest there). "Strong" is judged against each side's own loudest band, so a quiet track against a loud one still shows where it competes: a band counts when both are within 12 dB of their own peak
  // (a design value; spec: "両方のエネルギーが高い帯域を重なりとして表示する"), and at least -70 dBFS.
  function unmaskOverlay(box, ctx, eq) {
    const btn = box.querySelector('button[data-call="linkwatch"]'); if (btn) btn.classList.remove('on');
    if (!btn || !eq || !eq.svg) return null;
    const svg = eq.svg, [, , W, H] = vbOf(svg), g = mkEl('g', { 'pointer-events': 'none' }), note = mkEl('text', { x: 16, y: H - 26, fill: '#9a9ca2', 'font-size': 11, 'font-family': 'Barlow Condensed, sans-serif' });
    svg.insertBefore(g, svg.firstChild); svg.append(note);
    const own = Smooth(), oth = Smooth(); let key = '', msg = '';
    const clear = () => { if (key !== '') { g.innerHTML = ''; key = ''; } if (msg !== '') { note.textContent = ''; msg = ''; } };
    const say = t => { if (msg !== t) { msg = t; note.textContent = t; } };
    return { update(info) {
      if (!btn.classList.contains('on')) { clear(); return; }
      const lk = info && info.link, sp = info && info.spectrum;
      if (!lk || lk[0] < 0) { g.innerHTML = ''; key = ''; say('Unmask: this instance is not connected to SW Link'); return; }
      if (lk[0] === 0) { g.innerHTML = ''; key = ''; say('Unmask: no other SW AUDIO plug-in found (a host that runs plug-ins in separate processes cannot connect them)'); return; }
      if (lk.length < 65 || !sp) { say('Unmask: listening ...'); return; }
      const a = own.feed(sp), x = oth.feed(lk.slice(1, 65)), ma = Math.max(...a), mx = Math.max(...x);
      if (ma < -70 || mx < -70) { g.innerHTML = ''; key = ''; say('Unmask: nothing to compare yet (this track or the others are silent)'); return; }
      const s = []; let n = 0;
      for (let b = 0; b < 64; b++) { const o = Math.min(a[b] - ma, x[b] - mx), v = a[b] > -70 && x[b] > -70 ? Math.max(0, (o + 12) / 12) : 0; s.push(v); if (v > 0.05) n++; }
      const k = s.map(v => Math.round(v * 10)).join(''); say('Unmask: ' + lk[0] + ' other instance' + (lk[0] === 1 ? '' : 's') + (n ? ', overlap in ' + n + ' of 64 bands (red)' : ', no overlap'));
      if (k === key) return; key = k;
      let h = ''; for (let b = 0; b < 64; b++) if (s[b] > 0.05) { const x0 = eq.fx(20 * Math.pow(1000, b / 64)), x1 = eq.fx(20 * Math.pow(1000, (b + 1) / 64)); h += '<rect x="' + x0.toFixed(1) + '" y="8" width="' + (x1 - x0 + 0.5).toFixed(1) + '" height="' + (H - 34) + '" fill="#e5484d" fill-opacity="' + (0.06 + 0.34 * s[b]).toFixed(2) + '"/>'; }
      g.innerHTML = h;
    } };
  }

  // ---- UT01 EVO "Remembers gain staging per track type" (core: setTrackName / setTrackKind / rememberGain / suggestedGainDb; the host's track information comes through CLAP track-info; readouts = the kind of
  // track (-1: the host has not said), whether a Gain is remembered for it, that Gain in dB). The design has only this line of text, so the line is the control: a click remembers the current Gain for this
  // kind of track, a shift-click puts the remembered one on the Gain knob.
  function trackGain(box, ctx) {
    const evt = box.querySelector('.evob .evt'); if (!evt) return null;
    const kinds = ['Vocal', 'Drums', 'Bass', 'Guitar', 'Keys', 'Bus', 'Other'], gain = ctx.params.find(q => q.name === 'Gain');
    let last = { k: -1, has: false, db: 0 };
    evt.style.cursor = 'pointer'; evt.title = 'Click: remember the current Gain for this kind of track. Shift-click: use the Gain remembered for it';
    evt.addEventListener('click', e => {
      if (e.shiftKey) { if (last.has && gain) { ctx.begin(gain.i); ctx.set(gain.i, last.db); ctx.end(gain.i); } }
      else ctx.call('remember');
    });
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 3) return;
      last = { k: Math.round(r[0]), has: r[1] > 0.5, db: r[2] };
      const db = (last.db >= 0 ? '+' : '\u2212') + Math.abs(last.db).toFixed(1) + ' dB';
      const t = (last.k < 0 ? 'Track kind unknown (the host does not tell)' : kinds[Math.min(6, last.k)] + ' track') + ': ' + (last.has ? 'Gain ' + db + ' remembered (shift-click to use it)' : 'no Gain remembered yet (click to remember this one)');
      if (evt.textContent !== t) evt.textContent = t;
    } };
  }

  // ---- VO03 EVO "Harmony follows chords from a MIDI track" (core: noteOn / noteOff; readouts[6] = the chord held on the MIDI track, bit k = pitch class k): the line says which notes are held
  function chordLine(box, ctx) {
    const evt = box.querySelector('.evob .evt'); if (!evt) return null;
    const base = evt.textContent, names = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 7) return;
      const m = Math.round(r[6]), midi = ctx.value('Source') === 0;
      const t = m ? 'MIDI chord: ' + names.filter((_, k) => m & (1 << k)).join(' ') + (midi ? '' : ' (Source is not MIDI: the chord is not used)') : base;
      if (evt.textContent !== t) evt.textContent = t;
    } };
  }

  // 31 third-octave bars with peak holds (LV20)
  function spectrumBars(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const rs = [...svg.querySelectorAll(':scope > rect')], bars = rs.filter(r => +r.getAttribute('height') > 4), ticks = rs.filter(r => +r.getAttribute('height') <= 4);
    if (bars.length !== 31 || (ticks.length !== 31 && ticks.length !== 0)) return null;      // LV20: 31 bars + 31 peak ticks; LV13: 31 bars behind the EQ curve
    bars.sort((a, b) => +a.getAttribute('x') - +b.getAttribute('x')); ticks.sort((a, b) => +a.getAttribute('x') - +b.getAttribute('x'));
    const base = +bars[0].getAttribute('y') + +bars[0].getAttribute('height'), full = Math.max(...bars.map(r => +r.getAttribute('height'))) * 1.05, th = ticks.length ? +ticks[0].getAttribute('height') : 0;
    const sm = Smooth(), hold = new Array(31).fill(0);
    return { update(info) {
      const sp = info && info.spectrum; if (!sp) return; const a = sm.feed(sp);
      for (let k = 0; k < 31; k++) {                                          // third-octave k covers 20 * 10^((k-0.5)/10) .. 20 * 10^((k+0.5)/10); our band b spans 20 * 1000^(b/64)
        const f0 = 20 * Math.pow(10, (k - 0.5) / 10), f1 = 20 * Math.pow(10, (k + 0.5) / 10), b0 = Math.max(0, Math.floor(Math.log(f0 / 20) / Math.log(1000) * 64)), b1 = Math.min(63, Math.max(b0, Math.ceil(Math.log(f1 / 20) / Math.log(1000) * 64) - 1));
        let v = -120; for (let b = b0; b <= b1; b++) v = Math.max(v, a[b]);
        const x = specDb(v, -80), h = Math.max(1, x * full); bars[k].setAttribute('y', (base - h).toFixed(1)); bars[k].setAttribute('height', h.toFixed(1));
        hold[k] = Math.max(x, hold[k] - 0.012); if (ticks[k]) ticks[k].setAttribute('y', (base - hold[k] * full - th).toFixed(1));
      }
    } };
  }

  // a grid of cells lit up to the level of each column (MT03, RS04, RS07): columns = frequency, rows = level; the design's cell colours are kept
  function spectrumCells(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const cells = [...svg.querySelectorAll(':scope > rect')].filter(r => r.getAttribute('fill') !== 'none' && !r.getAttribute('stroke'));
    if (cells.length < 100) return null;
    const xs = [...new Set(cells.map(r => r.getAttribute('x')))].sort((a, b) => a - b), ys = [...new Set(cells.map(r => r.getAttribute('y')))].sort((a, b) => a - b);
    if (xs.length * ys.length !== cells.length) return null;
    const grid = xs.map(() => new Array(ys.length)); cells.forEach(r => { grid[xs.indexOf(r.getAttribute('x'))][ys.indexOf(r.getAttribute('y'))] = { e: r, o: r.getAttribute('fill-opacity') || r.style.opacity || '1' }; });
    const C = xs.length, R = ys.length, sm = Smooth(), lit = grid.map(c => c.map(() => true));
    return { update(info) {
      const sp = info && info.spectrum; if (!sp) return; const a = sm.feed(sp);
      for (let j = 0; j < C; j++) {
        const b0 = Math.floor(j * 64 / C), b1 = Math.max(b0, Math.floor((j + 1) * 64 / C) - 1); let v = -120; for (let b = b0; b <= b1; b++) v = Math.max(v, a[b]);
        for (let r = 0; r < R; r++) { const rowDb = -80 + (R - 1 - r) / (R - 1) * 80, on = v >= rowDb; if (on !== lit[j][r]) { lit[j][r] = on; const c = grid[j][r]; c.e.setAttribute('fill-opacity', on ? c.o : '0.04'); } }
      }
    } };
  }


  // ======== values the core measures (info.readouts: the product's `readouts` trait) ========
  // loudness meter (MT01, LV23): readouts = momentary, short-term, integrated, range, true peak, target, difference, in band [, dead air, true peak over]  (-200 = nothing measured yet)
  function loudnessDisplay(box, ctx) {
    const minis = [...box.querySelectorAll('.mini')]; if (minis.length < 4) return null;
    const big = box.querySelector('span[style*="font-size:64px"], span[style*="font-size:54px"]'); if (!big) return null;
    const unit = big.nextElementSibling, delta = big.parentElement.querySelector('span[style*="margin-left:auto"]');
    const tgtT = [...box.querySelectorAll('span')].find(e => /^Target\s/.test(e.textContent.trim()));
    const unitName = (unit && unit.textContent.trim()) || 'LUFS';
    const fm = v => v > -150 ? v.toFixed(1) : '—', val = (e, t) => { const sp = e.querySelectorAll('span'); sp[sp.length - 1].textContent = t; };
    // the three bars (M, S, I): -36 .. -12 LUFS; the white line is the target
    const lbs = [...box.querySelectorAll('.lb')], tick = lbs.map(l => l.parentElement.lastElementChild);
    const setBar = (b, v) => { const pct = clamp((v + 36) / 24, 0, 1) * 100; b.style.background = 'linear-gradient(to top,transparent 0 ' + pct.toFixed(1) + '%,rgba(22,23,25,.9) ' + pct.toFixed(1) + '%),linear-gradient(to top,#2bd14a 0 54%,#f0c93d 54% 75%,#e0443e 75%)'; };
    // history of the short-term value, once a second, last 10 minutes
    const svg = box.querySelector('.disp svg[preserveAspectRatio="none"]'), hist = Ring(600, null); let hpath = null, band = null, dash = null, last = 0;
    if (svg) { const ps = [...svg.querySelectorAll(':scope > path')]; hpath = ps[ps.length - 1]; const rs = svg.querySelectorAll(':scope > rect'); band = rs[0]; dash = svg.querySelector(':scope > line'); ps.slice(0, -1).forEach(p => p.remove()); }
    const dots = [...box.querySelectorAll('.stat .dot')];
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 8) return;
      big.textContent = fm(r[2]); if (tgtT) tgtT.textContent = 'Target ' + r[5].toFixed(1) + ' ' + unitName;
      if (delta) { delta.textContent = r[2] > -150 ? (r[6] >= 0 ? '+' : '') + r[6].toFixed(1) + ' LU' : '— LU'; delta.style.color = r[2] <= -150 ? '#8d8d8d' : (r[7] ? '#2bd14a' : (Math.abs(r[6]) > 3 ? '#e0443e' : '#f0c93d')); }
      val(minis[0], fm(r[0])); val(minis[1], fm(r[1])); val(minis[2], r[3] > 0 ? r[3].toFixed(1) + ' LU' : '—'); val(minis[3], r[4] > -150 ? r[4].toFixed(1) + (/dBTP/.test(minis[3].textContent) ? ' dBTP' : '') : '—');
      [r[0], r[1], r[2]].forEach((v, k) => { if (lbs[k]) setBar(lbs[k], v); });
      tick.forEach(t => { if (t) t.style.top = ((1 - clamp((r[5] + 36) / 24, 0, 1)) * 200 - 1).toFixed(1) + 'px'; });
      if (dots.length >= 2) { const set = (d, bad) => { d.style.background = bad ? '#e0443e' : '#2bd14a'; d.style.boxShadow = '0 0 6px ' + (bad ? '#e0443e' : '#2bd14a'); }; if (r.length >= 10) { set(dots[0], r[8] > 0.5); set(dots[1], r[9] > 0.5); dots[0].parentElement.lastChild.textContent = r[8] > 0.5 ? 'Dead air seen' : 'Dead air OK'; dots[1].parentElement.lastChild.textContent = r[9] > 0.5 ? 'TP over' : 'No TP over'; } }
      const now = Date.now(); if (hpath && now - last >= 1000) { last = now; hist.push(r[1] > -150 ? r[1] : null);
        const W = 732, H = 160, S = 6, y = v => clamp(80 - (v - r[5]) * S, 4, 156); let d = '', open = false;
        hist.a.forEach((v, k) => { if (v === null) { open = false; return; } d += (open ? ' L' : ' M') + (k / 599 * W).toFixed(1) + ' ' + y(v).toFixed(1); open = true; });
        hpath.setAttribute('d', d.trim()); const tol = ctx.value('Tolerance') || 1;
        if (band) { band.setAttribute('y', (80 - tol * S).toFixed(1)); band.setAttribute('height', (2 * tol * S).toFixed(1)); } }
    } };
  }


  // stream master (LV06): readouts = input short-term (LUFS), auto gain (dB), limiter reduction (dB), target (LUFS). The core measures the input; the integrated / true peak / range of
  // the design are not published, so the screen shows what the core has: short-term, auto gain, limiter, and the output estimated as input + gain.
  function streamMasterDisplay(box, ctx) {
    const big = box.querySelector('span[style*="font-size:46px"]'), minis = [...box.querySelectorAll('.mini')]; if (!big || minis.length < 3) return null;
    const row = big.parentElement, head = row.previousElementSibling, delta = row.querySelector('span[style*="margin-left:auto"]'), tgtT = head && head.lastElementChild, lblT = head && head.firstElementChild;
    const lab = (m, t) => { m.firstElementChild.textContent = t; }, val = (m, t) => { m.lastElementChild.textContent = t; };
    if (lblT) lblT.textContent = 'Short-term (input)'; lab(minis[0], 'Auto gain'); lab(minis[1], 'Limiter'); lab(minis[2], 'Output ≈');
    const bar = [...box.querySelectorAll('div[style*="border-radius:3px"][style*="height:16px"]')].find(e => e.children.length >= 2), fill = bar && bar.children[1], gRead = box.querySelector('.rv[style*="font-size:18px"]');
    const svg = box.querySelector('.disp svg'), hp = svg && svg.querySelector(':scope > path'), band = svg && svg.querySelector(':scope > rect'), txt = svg && svg.querySelector(':scope > text');
    const hist = Ring(600, null); let last = 0;
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 4) return; const fm = v => v > -150 ? v.toFixed(1) : '—', t = r[3];
      big.textContent = fm(r[0]); if (tgtT) tgtT.textContent = 'Target ' + t.toFixed(1);
      if (delta) { const d = r[0] - t; delta.textContent = r[0] > -150 ? (d >= 0 ? '+' : '') + d.toFixed(1) + ' LU' : '— LU'; delta.style.color = r[0] <= -150 ? '#8d8d8d' : (Math.abs(d) <= 1 ? '#2bd14a' : Math.abs(d) > 4 ? '#e0443e' : '#f0c93d'); }
      val(minis[0], (r[1] >= 0 ? '+' : '') + r[1].toFixed(1) + ' dB'); val(minis[1], r[2].toFixed(1) + ' dB'); val(minis[2], r[0] > -150 ? (r[0] + r[1]).toFixed(1) : '—');
      if (fill) { const w = clamp(Math.abs(r[1]) / 6, 0, 1) * 50; fill.style.width = w.toFixed(1) + '%'; fill.style.left = (r[1] >= 0 ? 50 : 50 - w) + '%'; fill.style.borderRadius = r[1] >= 0 ? '0 3px 3px 0' : '3px 0 0 3px'; }
      if (gRead) gRead.textContent = (r[1] >= 0 ? '+' : '') + r[1].toFixed(1) + ' dB';
      const now = Date.now(); if (hp && now - last >= 1000) { last = now; hist.push(r[0] > -150 ? r[0] : null); const W = 704, y = v => clamp(46 - (v - t) * 3, 4, 88); let d = '', open = false;
        hist.a.forEach((v, k) => { if (v === null) { open = false; return; } d += (open ? ' L' : ' M') + (k / 599 * W).toFixed(1) + ' ' + y(v).toFixed(1); open = true; }); hp.setAttribute('d', d.trim());
        if (band) { band.setAttribute('y', 40); band.setAttribute('height', 12); } if (txt) txt.textContent = 'Short-term loudness of the input, last 10 min'; }
    } };
  }

  // speech leveler (LV07): readouts = input momentary loudness (LUFS), applied gain (dB); the output line is the input plus the applied gain (an estimate), the dashed line is Target
  function speechLevelerDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')], dash = svg.querySelector(':scope > line[stroke-dasharray]'), tt = [...svg.querySelectorAll(':scope > text')].find(t => t.getAttribute('text-anchor') === 'end');
    if (ps.length < 3 || !dash) return null;
    const N = 50, W = 704, inH = Ring(N, null), gH = Ring(N, 0); let last = 0;
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 2) return; const t = ctx.value('Target'); if (t === undefined) return;
      const y = v => clamp(63 - (v - t) * 4, 6, 134); dash.setAttribute('y1', '63'); dash.setAttribute('y2', '63'); if (tt) tt.textContent = 'Target ' + t + ' LUFS';
      const now = Date.now(); if (now - last < 400) return; last = now; inH.push(r[0] > -150 ? r[0] : null); gH.push(r[1]);
      let di = '', dout = '', oi = false; for (let k = 0; k < N; k++) { const v = inH.a[k]; if (v === null) { oi = false; continue; } const x = (k / (N - 1) * W).toFixed(1); di += (oi ? ' L' : ' M') + x + ' ' + y(v).toFixed(1); dout += (oi ? ' L' : ' M') + x + ' ' + y(v + gH.a[k]).toFixed(1); oi = true; }
      ps[0].setAttribute('d', di.trim()); ps[1].setAttribute('d', dout.trim()); ps[2].setAttribute('d', dout.trim());
    } };
  }


  // ---- stereo scope (MT04, ST01, UT02, LV26): the dots are 160 recent output samples (L, R) on the diamond: up = both channels together, sideways = difference.
  // info.stereo = [correlation of the last 2048 samples, L0, R0, L1, R1, ...]; MT04's correlation bar follows the correlation.
  function stereoScope(box, ctx) {
    const svgs = [...box.querySelectorAll('.disp svg')], svg = svgs.find(v => v.querySelectorAll(':scope > circle').length >= 100); if (!svg) return null;
    const dots = [...svg.querySelectorAll(':scope > circle')], dia = svg.querySelector(':scope > path');
    const nums = (dia.getAttribute('d') || '').match(/-?\d+(\.\d+)?/g).map(Number);   // M cx cy-h  L cx+h cy ...
    const cx = nums[0], cy = nums[3], h = nums[2] - nums[0];
    const bar = box.querySelector('div[style*="linear-gradient(90deg,#5a1f1d"]'), mark = bar && bar.firstElementChild;
    let corr = 0;
    return { update(info) {
      const st = info && info.stereo; if (!st) return; const zm = clamp(ctx.value('Zoom') || 1, 1, 8);   // MT04 Zoom: 1x .. 8x
      for (let k = 0; k < dots.length && 2 + 2 * k < st.length; k++) {
        const l = clamp(st[1 + 2 * k], -1.2, 1.2), r = clamp(st[2 + 2 * k], -1.2, 1.2);
        dots[k].setAttribute('cx', (cx + clamp((r - l) / 2 * zm, -1, 1) * h).toFixed(1)); dots[k].setAttribute('cy', (cy - clamp((l + r) / 2 * zm, -1, 1) * h).toFixed(1));
        dots[k].style.opacity = (0.25 + 0.75 * k / dots.length).toFixed(2);                    // older dots fade
      }
      corr += (st[0] - corr) * 0.3; if (mark) mark.style.left = ((corr + 1) / 2 * 100).toFixed(1) + '%';
    } };
  }


  // ---- de-esser (DY05): the measured output spectrum 1 - 20 kHz, the detection band (drag it: Freq), the threshold line (drag it), the gain reduction (the core's own value)
  function deesserDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const rects = [...svg.querySelectorAll(':scope > rect')], band = rects[0], grBg = rects.find(r => r.getAttribute('x') === '394' && r.getAttribute('height') === '310'), grFill = rects.find(r => r !== grBg && r.getAttribute('x') === '394');
    const edges = [...svg.querySelectorAll(':scope > line')].filter(l => l.getAttribute('x1') === l.getAttribute('x2')), thr = [...svg.querySelectorAll(':scope > line')].find(l => l.getAttribute('stroke-dasharray'));
    const area = [...svg.querySelectorAll(':scope > path')].sort((a, b) => b.getAttribute('d').length - a.getAttribute('d').length)[0], tT = [...svg.querySelectorAll(':scope > text')].find(t => /^Threshold/.test(t.textContent)), peak = svg.querySelector(':scope > circle');
    const freqI = (ctx.params.find(q => q.name === 'Freq') || {}).i, thrI = (ctx.params.find(q => q.name === 'Threshold') || {}).i;
    if (!band || edges.length < 2 || !thr || !area || freqI === undefined || thrI === undefined) return null;
    const X0 = 10, X1 = 380, F0 = 1000, F1 = 20000, fx = f => X0 + Math.log(clamp(f, F0, F1) / F0) / Math.log(F1 / F0) * (X1 - X0), xf = x => F0 * Math.pow(F1 / F0, (x - X0) / (X1 - X0));
    const yd = db => 10 + clamp(-db, 0, 60) / 60 * 320, dy = y => -(y - 10) / 320 * 60, W = 1.3, sm = Smooth(), q = i => ctx.params.find(x => x.i === i);
    const redL = [...box.querySelectorAll('span')].find(e => /^Reduction$/i.test(e.textContent.trim())), redV = redL && redL.nextElementSibling;
    const drag = (els, apply, begin, end) => els.forEach(e => { let on = false; e.style.pointerEvents = 'all'; e.addEventListener('pointerdown', ev => { ev.stopPropagation(); e.setPointerCapture(ev.pointerId); on = true; begin(); apply(ev); }); e.addEventListener('pointermove', ev => { if (on) apply(ev); }); const stop = () => { if (!on) return; on = false; end(); }; e.addEventListener('pointerup', stop); e.addEventListener('pointercancel', stop); });
    const pt = ev => { const r = svg.getBoundingClientRect(); return [(ev.clientX - r.left) / r.width * 420, (ev.clientY - r.top) / r.height * 349]; };
    band.style.cursor = 'ew-resize'; thr.style.cursor = 'ns-resize'; edges.forEach(e => { e.style.cursor = 'ew-resize'; });
    drag([band, ...edges], ev => { const m = q(freqI); ctx.set(freqI, m.c.value(m.c.norm(xf(pt(ev)[0])))); }, () => ctx.begin(freqI), () => ctx.end(freqI));
    const hit = mkEl('line', { x1: X0, x2: X1, y1: 0, y2: 0, stroke: 'transparent', 'stroke-width': 14 }); svg.append(hit); hit.style.cursor = 'ns-resize';
    drag([hit], ev => { const m = q(thrI); ctx.set(thrI, m.c.value(m.c.norm(dy(pt(ev)[1])))); }, () => ctx.begin(thrI), () => ctx.end(thrI));
    return { update(info) {
      const f = ctx.get(freqI), t = ctx.get(thrI), a = fx(f / W), b = fx(f * W);
      band.setAttribute('x', a.toFixed(1)); band.setAttribute('width', (b - a).toFixed(1)); edges[0].setAttribute('x1', a.toFixed(1)); edges[0].setAttribute('x2', a.toFixed(1)); edges[1].setAttribute('x1', b.toFixed(1)); edges[1].setAttribute('x2', b.toFixed(1));
      const y = yd(t); thr.setAttribute('y1', y.toFixed(1)); thr.setAttribute('y2', y.toFixed(1)); hit.setAttribute('y1', y.toFixed(1)); hit.setAttribute('y2', y.toFixed(1)); if (tT) { tT.textContent = 'Threshold ' + (+t.toFixed(0)) + ' dB'; tT.setAttribute('y', (y - 6).toFixed(1)); }
      const sp = info && info.spectrum; if (sp) {
        const v = sm.feed(sp); let d = 'M' + X0 + ' 330', bi = -1, bv = -200;
        for (let k = 0; k < 64; k++) { const fk = 20 * Math.pow(1000, (k + 0.5) / 64); if (fk < F0 || fk > F1) continue; d += ' L' + fx(fk).toFixed(1) + ' ' + yd(v[k]).toFixed(1); if (fk >= f / W && fk <= f * W && v[k] > bv) { bv = v[k]; bi = k; } }
        area.setAttribute('d', d + ' L' + X1 + ' 330 Z');
        if (peak) { if (bi >= 0 && bv > -90) { peak.style.display = ''; peak.setAttribute('cx', fx(20 * Math.pow(1000, (bi + 0.5) / 64)).toFixed(1)); peak.setAttribute('cy', yd(bv).toFixed(1)); } else peak.style.display = 'none'; }
      }
      { const g = grOf(info), h = clamp(g / 20, 0, 1) * 310; if (grFill) grFill.setAttribute('height', h.toFixed(1)); if (redV) redV.textContent = (g > 0.05 ? '-' : '') + g.toFixed(1) + ' dB'; }
    } };
  }


  // ---- gain rider (MS05, VO05): readouts = ride (dB) [, music listening]. Orange = the ride over the last 12 s (the grey area is the measured output peak), the line is 0 dB
  const NOTE = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  function riderDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')], txt = svg.querySelector(':scope > text'); if (ps.length < 3) return null;
    const music = box.querySelector('[data-readout] span');
    const N = 120, W = 732, ride = Ring(N, 0), lvl = Ring(N, -90); let last = 0;
    return { update(info) {
      const r = info && info.readouts, m = info && info.meters; if (!r || r.length < 1) return;
      if (music && r.length >= 2) music.textContent = r[1] > 0.5 ? 'Listening' : 'Not listening';
      const now = Date.now(); if (now - last < 100) return; last = now; ride.push(r[0]); lvl.push(m ? outDb(m) : -90);
      const R = Math.max(6, ctx.value('Range') || 6), y = v => 100 - clamp(v / R, -1, 1) * 80; let d = '', up = 'M0 100';
      for (let k = 0; k < N; k++) { const x = (k / (N - 1) * W).toFixed(1); d += (k ? ' L' : 'M') + x + ' ' + y(ride.a[k]).toFixed(1); up += ' L' + x + ' ' + (100 - clamp((lvl.a[k] + 60) / 60, 0, 1) * 60).toFixed(1); }
      ps[0].setAttribute('d', up + ' L' + W + ' 100 Z'); ps[1].setAttribute('d', d); ps[2].setAttribute('d', d); if (txt) txt.textContent = 'Gain ride, +' + R + ' to -' + R + ' dB';
    } };
  }
  // the guitar tuner read-out of GT03 (the design's part has no parameter): readouts = Hz, MIDI note, cents
  function tunerReadout(box, ctx) {
    const el = box.querySelector('[data-readout] span'); if (!el) return null;
    return { update(info) { const r = info && info.readouts; if (!r || r.length < 3) return; el.textContent = r[0] > 20 ? NOTE[((Math.round(r[1]) % 12) + 12) % 12] + (Math.floor(Math.round(r[1]) / 12) - 1) + ' ' + (r[2] >= 0 ? '+' : '') + r[2].toFixed(0) + '¢ ' + r[0].toFixed(1) + ' Hz' : '—'; } };
  }


  // ---- polarity gauge (LV22): readouts = result, correlation (-1 .. +1), lag (ms). The needle: -1 at the left end of the arc, 0 up, +1 right
  function polarityGauge(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const needle = svg.querySelector(':scope > line'); if (!needle) return null;
    const x0 = +needle.getAttribute('x1'), y0 = +needle.getAttribute('y1'), len = 140;
    const t = mkEl('text', { x: 14, y: 22, 'text-anchor': 'start', 'font-family': 'Space Mono, monospace', 'font-size': 13, fill: '#e6e6e6' }); svg.append(t);
    let a = 0;
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 3) return; a += (r[1] - a) * 0.3; const th = clamp(a, -1, 1) * Math.PI / 2;
      needle.setAttribute('x2', (x0 + len * Math.sin(th)).toFixed(1)); needle.setAttribute('y2', (y0 - len * Math.cos(th)).toFixed(1));
      t.textContent = 'r ' + (r[1] >= 0 ? '+' : '') + r[1].toFixed(2) + (Math.abs(r[2]) > 0.001 ? '   lag ' + r[2].toFixed(2) + ' ms' : '');
    } };
  }
  // ---- gain trace with blocks (LV05 BGM ducking, LV29 floor ducking): readouts = gain (dB), key active (1 / 0); the last 30 s
  function gainTraceDisplay(box, ctx, cfg) {
    const svg = svgOf(box); if (!svg) return null;
    const path = svg.querySelector(':scope > path'), demo = [...svg.querySelectorAll('rect')]; if (!path || !demo.length) return null;
    const [, , W] = vbOf(svg), ry = demo[0].getAttribute('y'), rh = demo[0].getAttribute('height'), parent = demo[0].parentElement, rx = demo[0].getAttribute('rx') || '3', fo = demo[0].getAttribute('fill-opacity') || parent.getAttribute('fill-opacity') || '0.3', fill = demo[0].getAttribute('fill') || parent.getAttribute('fill') || '#f2f2f2';
    demo.forEach(r => r.remove()); const g = mkEl('g', { fill, 'fill-opacity': fo }); svg.append(g);
    [...svg.querySelectorAll(':scope > text')].filter(t => /gap held/i.test(t.textContent)).forEach(t => t.remove());
    const N = 150, gain = Ring(N, 0), key = Ring(N, 0); let last = 0;
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 2) return; const now = Date.now(); if (now - last < 200) return; last = now; gain.push(r[0]); key.push(r[1] > 0.5 ? 1 : 0);
      let d = '', rects = ''; for (let k = 0; k < N; k++) d += (k ? ' L' : 'M') + (k / (N - 1) * W).toFixed(1) + ' ' + cfg.y(gain.a[k]).toFixed(1);
      for (let k = 0; k < N;) { if (!key.a[k]) { k++; continue; } let e = k; while (e < N && key.a[e]) e++; rects += '<rect x="' + (k / (N - 1) * W).toFixed(1) + '" y="' + ry + '" width="' + Math.max(2, (e - k) / (N - 1) * W).toFixed(1) + '" height="' + rh + '" rx="' + rx + '"/>'; k = e; }
      path.setAttribute('d', d); g.innerHTML = rects;
    } };
  }


  // ---- LFO wave (MD02 flanger, MD04 tremolo / auto pan): readouts = rate in use (Hz) [, LFO phase]. The window is 1.5 s with "now" at the right edge; amplitude = Depth; MD04 draws its Shape.
  function lfoDisplay(box, ctx, kind) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')]; if (ps.length < 2) return null;
    const W = 732, T = 1.5, shape = (p, k) => { const f = p - Math.floor(p); return k === 1 ? 1 - 4 * Math.abs(f - 0.5) : k === 2 ? (f < 0.5 ? 1 : -1) : k === 3 ? 2 * f - 1 : Math.sin(2 * Math.PI * p); };
    let phase0 = 0, lastKey = '';
    return { update(info) {
      const r = info && info.readouts; if (!r) return; const rate = r[0], ph = r.length > 1 ? r[1] : 0, dep = (ctx.value('Depth') || 0) / 100, sh = kind === 'shape' ? Math.round(ctx.value('Shape') || 0) : 0;
      const key = [rate.toFixed(3), ph.toFixed(2), dep, sh].join('|'); if (key === lastKey) return; lastKey = key;
      let d = ''; for (let i = 0; i <= 240; i++) { const x = i / 240 * W, p = ph - rate * T * (1 - i / 240); d += (i ? ' L' : 'M') + x.toFixed(1) + ' ' + (100 - dep * 85 * shape(p, sh)).toFixed(1); }
      ps.forEach(p => p.setAttribute('d', d));
    } };
  }


  // ---- tape stop (CR05): the speed over the action, drawn from Action, Curve and the times (the same formulas as products/cr05: Stop 1-w, Start w, Spin back 1-3w, w = u, u*u or sqrt(u))
  function tapeStopDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')], texts = [...svg.querySelectorAll(':scope > text')], endT = texts.find(t => t.getAttribute('text-anchor') === 'end'); if (ps.length < 2) return null;
    const X0 = 10, X1 = 722, YT = 20, YB = 185; let last = '';
    return { update() {
      const act = Math.round(ctx.value('Action') || 0), cv = Math.round(ctx.value('Curve') || 0), key = act + '|' + cv; if (key === last) return; last = key;
      const w = u => cv === 1 ? u * u : cv === 2 ? Math.sqrt(u) : u, sp = u => act === 0 ? 1 - w(u) : act === 1 ? w(u) : 1 - 3 * w(u), smin = act === 2 ? -2 : 0, y = s => YT + (1 - s) / (1 - smin) * (YB - YT);
      let d = ''; for (let i = 0; i <= 60; i++) { const u = i / 60; d += (i ? ' L' : 'M') + (X0 + u * (X1 - X0)).toFixed(1) + ' ' + y(sp(u)).toFixed(1); }
      ps.forEach(p => p.setAttribute('d', d)); if (endT) { endT.textContent = ['Stop', 'Normal speed', 'Spin back'][act]; endT.setAttribute('y', act === 1 ? '16' : '192'); }
    } };
  }


  // a read-out the design has no parameter for (LV14 Distance, LV19 Frames): the value comes from the core (readouts[0])
  function derivedReadout(box, ctx, fmt) {
    const el = box.querySelector('[data-readout] span'); if (!el) return null;
    return { update(info) { const r = info && info.readouts; if (r) el.textContent = fmt(r[0]); } };
  }


  // ---- reverb (RV01): the decay from Pre-delay to Decay (RT60, -60 dB, straight in dB), the pre-delay and RT60 markers; the design's "High band decay" curve is dropped (it would need the Damping model)
  function reverbDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')], area = ps.find(e => e.getAttribute('fill') !== 'none'), line = ps.find(e => e.getAttribute('fill') === 'none' && !e.getAttribute('stroke-dasharray')), dashed = ps.find(e => e.getAttribute('stroke-dasharray'));
    const texts = [...svg.querySelectorAll(':scope > text')], find = re => texts.find(t => re.test(t.textContent)), preT = find(/^Pre-delay/), rtT = find(/^RT60/), hb = find(/High band/);
    const lines = [...svg.querySelectorAll(':scope > line')], preL = lines.find(l => l.getAttribute('x1') === '35'), rtL = lines.find(l => l.getAttribute('x1') === '646'), axis = svg.querySelector(':scope > g[font-size="11"]');
    if (!area || !line || !preL || !rtL) return null;
    if (dashed) dashed.remove(); if (hb) hb.remove(); const lt = find(/^Late tail/); if (lt) lt.remove();
    const X0 = 30, X1 = 910, TOP = 82, BASE = 196; let last = '';
    return { update() {
      const dec = Math.max(0.1, ctx.value('Decay') || 1), pre = (ctx.value('Pre-delay') || 0) / 1000, key = dec + '|' + pre; if (key === last) return; last = key;
      const axisMax = Math.max(4, Math.ceil(dec * 1.05)), tx = t => X0 + t / axisMax * (X1 - X0), xp = tx(pre), xr = tx(dec);
      let d = 'M' + xp.toFixed(1) + ' ' + TOP; for (let i = 1; i <= 40; i++) { const t = pre + (dec - pre) * i / 40; d += ' L' + tx(t).toFixed(1) + ' ' + (TOP + (BASE - TOP) * i / 40).toFixed(1); }
      line.setAttribute('d', d); area.setAttribute('d', d + ' L' + xp.toFixed(1) + ' ' + BASE + ' Z');
      preL.setAttribute('x1', (xp - 0).toFixed(1)); preL.setAttribute('x2', xp.toFixed(1)); if (preT) { preT.setAttribute('x', (xp + 6).toFixed(1)); preT.textContent = 'Pre-delay ' + Math.round(pre * 1000) + ' ms'; }
      rtL.setAttribute('x1', xr.toFixed(1)); rtL.setAttribute('x2', xr.toFixed(1)); if (rtT) { rtT.setAttribute('x', (xr + 6).toFixed(1)); rtT.textContent = 'RT60 ' + dec.toFixed(1) + ' s'; }
      if (axis) { const t = [...axis.querySelectorAll('text')]; t.forEach((e, k) => { const sec = axisMax * k / 4; e.textContent = k === 0 ? '0' : (Number.isInteger(sec) ? sec : sec.toFixed(1)) + (k === 4 ? ' s' : ' s'); }); }
    } };
  }


  // ---- pitch / formant pad (LV10, VO06): the dot is Pitch (left-right) and Formant (up-down); drag it, double-click for the defaults
  function xyPadDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const xi = (ctx.params.find(q => q.name === 'Pitch') || {}).i, yi = (ctx.params.find(q => q.name === 'Formant') || {}).i; if (xi === undefined || yi === undefined) return null;
    const cs = [...svg.querySelectorAll(':scope > circle')], ls = [...svg.querySelectorAll(':scope > line')].filter(l => (l.getAttribute('stroke') || '') !== '#ffffff'); if (cs.length < 2 || ls.length < 2) return null;
    const vl = ls.find(l => l.getAttribute('x1') === l.getAttribute('x2')), hl = ls.find(l => l.getAttribute('y1') === l.getAttribute('y2')); if (!vl || !hl) return null;
    const [, , W, H] = vbOf(svg), q = i => ctx.params.find(x => x.i === i), pad = 12;
    const px = v => { const m = q(xi).p; return pad + (v - m.min) / (m.max - m.min) * (W - 2 * pad); }, py = v => { const m = q(yi).p; return H - pad - (v - m.min) / (m.max - m.min) * (H - 2 * pad); };
    const at = e => { const r = svg.getBoundingClientRect(); return [clamp((e.clientX - r.left) / r.width * W, pad, W - pad), clamp((e.clientY - r.top) / r.height * H, pad, H - pad)]; };
    const set = e => { const [x, y] = at(e), mx = q(xi), my = q(yi); ctx.set(xi, mx.c.value(mx.c.norm(mx.p.min + (x - pad) / (W - 2 * pad) * (mx.p.max - mx.p.min)))); ctx.set(yi, my.c.value(my.c.norm(my.p.min + (H - pad - y) / (H - 2 * pad) * (my.p.max - my.p.min)))); };
    const hit = mkEl('rect', { x: 0, y: 0, width: W, height: H, fill: 'transparent' }); svg.append(hit); hit.style.cursor = 'crosshair'; let on = false;
    hit.addEventListener('pointerdown', e => { hit.setPointerCapture(e.pointerId); on = true; ctx.begin(xi); ctx.begin(yi); set(e); });
    hit.addEventListener('pointermove', e => { if (on) set(e); });
    const end = () => { if (!on) return; on = false; ctx.end(xi); ctx.end(yi); }; hit.addEventListener('pointerup', end); hit.addEventListener('pointercancel', end);
    hit.addEventListener('dblclick', () => { ctx.begin(xi); ctx.begin(yi); ctx.set(xi, q(xi).p.def); ctx.set(yi, q(yi).p.def); ctx.end(xi); ctx.end(yi); });
    return { update() { const x = px(ctx.get(xi)), y = py(ctx.get(yi)); vl.setAttribute('x1', x.toFixed(1)); vl.setAttribute('x2', x.toFixed(1)); hl.setAttribute('y1', y.toFixed(1)); hl.setAttribute('y2', y.toFixed(1)); cs.forEach(c => { c.setAttribute('cx', x.toFixed(1)); c.setAttribute('cy', y.toFixed(1)); }); } };
  }


  // ---- gain-reduction history (LV17): the design's two grey level areas (mirrored around the centre) and the orange gain reduction from the top; readouts[0] = gain reduction (dB, <= 0)
  function grHistoryDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')]; if (ps.length < 3) return null;
    const [, , W, H] = vbOf(svg), C = H * 0.55, N = 40, dx = W / (N - 1), lvl = Ring(N, -90), gr = Ring(N, 0); let tick = 0;
    return { update(info) {
      const m = info && info.meters, r = info && info.readouts; if (!m || !r || r.length < 1 || ++tick % 4) return;
      lvl.push(peakDb(m)); gr.push(Math.max(0, -r[0])); let up = 'M0 ' + C, dn = 'M0 ' + C, g = 'M0 0';
      for (let k = 0; k < N; k++) { const x = (k * dx).toFixed(1), a = clamp((lvl.a[k] + 60) / 60, 0, 1) * (C - 8); up += ' L' + x + ' ' + (C - a).toFixed(1); dn += ' L' + x + ' ' + (C + a * 0.9).toFixed(1); g += ' L' + x + ' ' + (clamp(gr.a[k], 0, 24) / 24 * C * 0.6).toFixed(1); }
      ps[0].setAttribute('d', up + ' L' + W + ' ' + C + ' L0 ' + C + ' Z'); ps[1].setAttribute('d', dn + ' L' + W + ' ' + C + ' L0 ' + C + ' Z'); ps[2].setAttribute('d', g + ' L' + W + ' 0 Z');
    } };
  }


  // ---- true-peak limiter (MS02): the measured output level as a mirrored envelope (last ~10 s) and the Ceiling line; the design's "inter-sample peaks caught" markers and count are dropped (the core does not publish them)
  function ceilingDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const area = svg.querySelector(':scope > path[fill="#8a8c92"]'), cl = svg.querySelector(':scope > line[stroke-dasharray]'), texts = [...svg.querySelectorAll(':scope > text')];
    const ct = texts.find(t => /^Ceiling/.test(t.textContent)), isp = texts.find(t => /Inter-sample/.test(t.textContent)); if (!area || !cl) return null;
    [...svg.querySelectorAll(':scope > path[fill="#f2f2f2"]')].forEach(e => e.remove()); if (isp) isp.remove();
    const [, , W, H] = vbOf(svg), C = H / 2, N = 80, dx = W / (N - 1), hist = Ring(N, -90); let tick = 0;
    const yOf = db => C - clamp((db + 36) / 36, 0, 1) * (C - 14);
    return { update(info) {
      const ce = ctx.value('Ceiling'); if (ce !== undefined) { const y = yOf(ce).toFixed(1); cl.setAttribute('y1', y); cl.setAttribute('y2', y); if (ct) { ct.textContent = 'Ceiling ' + ce.toFixed(1) + ' dBTP'; ct.setAttribute('y', (yOf(ce) - 4).toFixed(1)); } }
      const m = info && info.meters; if (!m || ++tick % 6) return; hist.push(outDb(m));
      let up = '', dn = ''; for (let k = 0; k < N; k++) { const x = (k * dx).toFixed(1), a = C - yOf(hist.a[k]); up += (k ? ' L' : 'M') + x + ' ' + (C - a).toFixed(1); dn = ' L' + x + ' ' + (C + a).toFixed(1) + dn; }
      area.setAttribute('d', up + dn + ' Z');
    } };
  }


  // ---- test-signal generator (LV21): the chosen signal (Sine, Pink, White, Sweep, Polarity pulses) at the Level, 4 cycles of the sine shown whatever the frequency
  function generatorDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')]; if (ps.length < 2) return null;
    const [, , W, H] = vbOf(svg), C = H / 2; let last = '', seed = 1; const rnd = () => { seed = (seed * 1664525 + 1013904223) >>> 0; return seed / 4294967296 - 0.5; };
    return { update() {
      const sg = Math.round(ctx.value('Signal') || 0), lv = ctx.value('Level'), key = sg + '|' + lv; if (key === last) return; last = key;
      const A = clamp(Math.pow(10, (lv === undefined ? -18 : lv) / 20) * 1.0, 0.03, 1) * (C - 8); let d = '', lp = 0; seed = 7;
      for (let i = 0; i <= 240; i++) {
        const x = i / 240 * W, u = i / 240; let v;
        if (sg === 0) v = Math.sin(2 * Math.PI * 4 * u);
        else if (sg === 1) { lp = 0.82 * lp + 0.18 * rnd() * 2; v = clamp(lp * 3.2, -1, 1); }                         // pink-ish: low-passed noise
        else if (sg === 2) v = clamp(rnd() * 2, -1, 1);
        else if (sg === 3) v = Math.sin(2 * Math.PI * (2 * u + 14 * u * u));                                         // a sweep rising in pitch
        else v = (i % 40 === 2) ? 1 : (i % 40 === 3 ? -0.3 : 0);                                                     // polarity: positive clicks
        d += (i ? ' L' : 'M') + x.toFixed(1) + ' ' + (C - v * A).toFixed(1);
      }
      ps.forEach(p => p.setAttribute('d', d));
    } };
  }


  // ---- handling-noise catcher (LV18): readouts = events caught (Plug pop, Wind, Handling, Plosive). The three markers show the counts, dim when the type is off, and flash when one is caught; the grey area is the output level (last ~10 s)
  function catcherDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const area = svg.querySelector(':scope > path[fill="#8a8c92"]'), texts = [...svg.querySelectorAll(':scope > text')], lines = [...svg.querySelectorAll(':scope > line[stroke-dasharray]')], tris = [...svg.querySelectorAll(':scope > path[fill="#f2f2f2"]')];
    const names = ['Plug pop', 'Wind', 'Handling']; if (!area || texts.length < 3 || lines.length < 3) return null;
    const [, , W, H] = vbOf(svg), C = H * 0.62, N = 80, dx = W / (N - 1), hist = Ring(N, -90); let tick = 0; const seen = [0, 0, 0], flash = [0, 0, 0];
    // the box "Caught today | 3": the core counts since the instance started, so the label says so; the number is the sum of the three markers; press the box to count again
    const rbox = box.querySelector('.rbox'), rs = rbox ? [...rbox.querySelectorAll('span')] : [];
    if (rs.length >= 2) { rs[0].textContent = 'Caught'; rbox.style.cursor = 'pointer'; rbox.title = 'Events caught since the plug-in started. Press to count again'; rbox.addEventListener('click', () => { ctx.call('resetcounts', ''); }); }
    return { update(info) {
      const r = info && info.readouts, m = info && info.meters; if (!r || r.length < 3) return;
      if (rs.length >= 2) rs[1].textContent = String(Math.round(r[0] + r[1] + r[2]));
      names.forEach((nm, k) => { const on = ctx.value(nm) > 0.5; if (r[k] > seen[k]) flash[k] = 12; seen[k] = r[k]; if (flash[k] > 0) flash[k]--;
        texts[k].textContent = nm + ' · ' + r[k]; const op = on ? (flash[k] > 0 ? 1 : 0.8) : 0.25; lines[k].style.opacity = op; if (tris[k]) tris[k].style.opacity = op; texts[k].style.opacity = on ? 1 : 0.4; lines[k].setAttribute('stroke-width', flash[k] > 0 ? '2.5' : '1'); });
      if (!m || ++tick % 6) return; hist.push(outDb(m)); let up = '', dn = '';
      for (let k = 0; k < N; k++) { const x = (k * dx).toFixed(1), a = clamp((hist.a[k] + 60) / 60, 0, 1) * (C - 24); up += (k ? ' L' : 'M') + x + ' ' + (C - a).toFixed(1); dn = ' L' + x + ' ' + (C + a * 0.6).toFixed(1) + dn; }
      area.setAttribute('d', up + dn + ' Z');
    } };
  }


  // ---- bit crusher (SA08): two cycles of a sine as the crusher makes them: Bits quantises the level (2^Bits steps, shown up to 64), Rate holds the value (the window is 2 ms, so 48 kHz = 96 points)
  function crusherDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')]; if (ps.length < 2) return null;
    const [, , W, H] = vbOf(svg), C = H / 2, A = C - 14; let last = '';
    return { update() {
      const bits = ctx.value('Bits') || 8, rate = ctx.value('Rate') || 48000, key = bits + '|' + rate; if (key === last) return; last = key;
      const levels = Math.min(64, Math.pow(2, Math.max(1, Math.round(bits)))), n = clamp(Math.round(rate * 0.002), 2, 240), q = v => levels <= 2 ? (v >= 0 ? 1 : -1) : Math.round((v * 0.5 + 0.5) * (levels - 1)) / (levels - 1) * 2 - 1;
      let d = ''; for (let i = 0; i < n; i++) { const u0 = i / n, u1 = (i + 1) / n, v = q(Math.sin(2 * Math.PI * 2 * u0)), y = (C - v * A).toFixed(1); d += (i ? ' L' : 'M') + (u0 * W).toFixed(1) + ' ' + y + ' L' + (u1 * W).toFixed(1) + ' ' + y; }
      ps.forEach(p => p.setAttribute('d', d));
    } };
  }


  // ---- speaker triangle (ST05): the two speakers at the Angle between them (0 - 60 degrees, symmetric about the listener's forward direction), fixed distance on the drawing
  function speakerTriangleDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const lis = svg.querySelector(':scope > circle'), rects = [...svg.querySelectorAll(':scope > rect')].filter(r => +r.getAttribute('width') < 40), lines = [...svg.querySelectorAll(':scope > line[stroke-dasharray]')];
    if (!lis || rects.length < 2 || lines.length < 2) return null;
    const cx = +lis.getAttribute('cx'), cy = +lis.getAttribute('cy'), rw = +rects[0].getAttribute('width'), rh = +rects[0].getAttribute('height'), D = 84; let last = null;
    return { update() {
      const a = ctx.value('Angle'); if (a === undefined || a === last) return; last = a; const half = a / 2 * Math.PI / 180;
      [-1, 1].forEach((sgn, k) => { const x = cx + sgn * D * Math.sin(half), y = cy - D * Math.cos(half);
        rects[k].setAttribute('x', (x - rw / 2).toFixed(1)); rects[k].setAttribute('y', (y - rh / 2).toFixed(1));
        lines[k].setAttribute('x1', x.toFixed(1)); lines[k].setAttribute('y1', (y + rh / 2).toFixed(1)); lines[k].setAttribute('x2', (cx + sgn * 14 * Math.sin(half)).toFixed(1)); lines[k].setAttribute('y2', (cy - 20 * Math.cos(half)).toFixed(1)); });
    } };
  }


  // ---- cabinet and mic (GT02): the mic dot across the speaker cone from Off axis (0 = centre of the dust cap, 90 = the rim), named by the ring it falls in, with Mic distance
  function micPositionDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const cs = [...svg.querySelectorAll(':scope > circle')]; if (cs.length < 6) return null;
    const rings = cs.slice(0, 4), dot = cs[4], halo = cs[5], cx = +rings[0].getAttribute('cx'), cy = +rings[0].getAttribute('cy'), R = +rings[0].getAttribute('r');
    const t = [...svg.querySelectorAll(':scope > text')].find(e => e.getAttribute('fill') === '#e8e8e8'); let last = '';
    const zones = [[0.22, 'Centre of dust cap'], [0.55, 'Edge of dust cap'], [0.8, 'Cone'], [1.01, 'Surround']];
    return { update() {
      const off = ctx.value('Off axis'), dist = ctx.value('Mic distance'); if (off === undefined) return; const key = off + '|' + dist; if (key === last) return; last = key;
      const f = clamp(off / 90, 0, 1), r = f * R * 0.95, ang = -30 * Math.PI / 180, x = cx + r * Math.cos(ang), y = cy + r * Math.sin(ang);
      [dot, halo].forEach(c => { c.setAttribute('cx', x.toFixed(1)); c.setAttribute('cy', y.toFixed(1)); });
      if (t) t.textContent = zones.find(z => f < z[0])[1] + ', ' + (dist === undefined ? '' : Math.round(dist * 10) / 10 + ' cm');
    } };
  }


  // ---- SA06 saturator: the transfer curve and a test sine through it, for the band that is selected. shapeFn is the core's own function (products/sa06/sa06.cpp), ported
  const sa06Tape = (u, k) => u / Math.pow(1 + Math.pow(Math.abs(u), k), 1 / k);
  function sa06Shape(type, shape, u) {
    switch (type) {
      case 0: { const k = [2, 4, 8][shape], c = [1, 0.7, 0.5][shape]; return c * sa06Tape(u / c, k); }
      case 1: { const h = [2, 1.4, 1][shape], b = 0.3, tb = Math.tanh(b), s = 1 / (1 - tb * tb); return h * s * (Math.tanh(u / h + b) - tb); }
      case 2: { const a = [1, 3, 8][shape]; return u >= 0 ? Math.log1p(a * u) / a : -Math.log1p(0.5 * a * -u) / (0.5 * a); }
      case 3: { const k = [1, 2, 4][shape]; return Math.sin(k * u) / k; }
      default: { const k = [2, 4, 12][shape]; return u >= 0 ? sa06Tape(u, k) : 0.6 * sa06Tape(u / 0.6, k); }
    }
  }
  // out(x) for the band's settings: x + mix * (shaped - x), the shape taken at its operating point (bias) with a small-signal gain of 1
  function sa06Transfer(type, shape, drive, bias, mix) {
    const g = Math.pow(10, clamp(drive, -12, 36) / 20), off = 0.5 * bias; let f0 = 0, slope = 1;
    if (off !== 0) { f0 = sa06Shape(type, shape, off); slope = (sa06Shape(type, shape, off + 1e-4) - sa06Shape(type, shape, off - 1e-4)) / 2e-4; if (Math.abs(slope) < 0.05) slope = 0.05; }
    return { g, wet: x => (sa06Shape(type, shape, g * x + off) - f0) / slope, out(x) { return x + mix * (this.wet(x) - x); } };
  }
  function saturatorDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const ps = [...svg.querySelectorAll(':scope > path')]; if (ps.length < 5) return null;
    const [glowC, curve, , glowW, wave] = ps; let last = '';
    const X0 = 14, Y0 = 14, S = 202, WX = 232, WW = 694, CY = 115, AMP = 85, A_IN = 0.9;
    return { update() {
      const n = (ctx.band ? ctx.band() : 0) + 1, v = k => ctx.value('Band ' + n + ' ' + k);
      const type = Math.round(v('Type')), drive = v('Drive'), shape = Math.round(v('Shape')), bias = v('Bias'), mix = v('Mix') / 100;
      if ([type, drive, shape, bias, mix].some(x => !Number.isFinite(x))) return;
      const key = [n, type, drive, shape, bias, mix].join('|'); if (key === last) return; last = key;
      const t = sa06Transfer(type, shape, drive, bias, mix);
      let d = ''; for (let i = 0; i <= 100; i++) { const x = -1 + i / 50, y = clamp(t.out(x), -1, 1); d += (i ? ' L' : 'M') + (X0 + (x + 1) / 2 * S).toFixed(1) + ' ' + (Y0 + S - (y + 1) / 2 * S).toFixed(1); }
      glowC.setAttribute('d', d); curve.setAttribute('d', d);
      // the test sine (4 periods): the shaped wave minus its mean (the core removes the DC the shape adds), then the mix
      const N = 240, xs = [], wet = []; let mean = 0;
      for (let i = 0; i < N; i++) { const x = A_IN * Math.sin(2 * Math.PI * 4 * i / N); xs.push(x); const w = t.wet(x); wet.push(w); mean += (w - t.g * x) / N; }
      d = ''; for (let i = 0; i <= N; i++) { const k = i % N, y = clamp(xs[k] + mix * ((wet[k] - mean) - xs[k]), -1.25, 1.25); d += (i ? ' L' : 'M') + (WX + i / N * WW).toFixed(1) + ' ' + (CY - y * AMP).toFixed(1); }
      glowW.setAttribute('d', d); wave.setAttribute('d', d);
      // the input drawn from the same sine (the design's grey line)
      const grey = ps[2]; let g = ''; for (let i = 0; i <= N; i++) g += (i ? ' L' : 'M') + (WX + i / N * WW).toFixed(1) + ' ' + (CY - A_IN * Math.sin(2 * Math.PI * 4 * (i % N) / N) * AMP).toFixed(1); grey.setAttribute('d', g);
    } };
  }


  // ---- RV04 convolution: the impulse response's envelope (one bar per 5 px), from the category's reverberation times (products/rv04/rv04.cpp kCat), Length, Size, Reverse and Pre-delay
  // the IR is synthesised from seven octave bands (125 Hz - 8 kHz) that decay at their own RT60; the bar is the band-average amplitude, 0 dB = the highest bar, 60 dB range
  const RV04_CAT = [
    { name: 'Halls', rt: [3.4, 3.2, 2.9, 2.6, 2.0, 1.4, 0.8], sec: 5.0 }, { name: 'Rooms', rt: [0.9, 0.85, 0.75, 0.65, 0.55, 0.4, 0.25], sec: 1.6 },
    { name: 'Churches', rt: [7.0, 6.8, 6.2, 5.5, 4.2, 2.8, 1.4], sec: 8.0 }, { name: 'Gear', rt: [2.8, 2.6, 2.4, 2.2, 2.0, 1.8, 1.4], sec: 3.5 }];
  function rv04Envelope(cat, lengthPct, sizePct, s) {   // amplitude at time s (s) of the IR as the plug-in uses it (before Reverse)
    const c = RV04_CAT[cat], size = sizePct / 100, lt = c.sec * lengthPct / 100, fade = Math.max(0.1 * lt, 0.02);
    const pos = s / size; if (pos < 0 || pos > lt) return 0;
    let e = 0; for (const rt of c.rt) { const a = Math.exp(-6.907755 * pos / rt); e += a * a; }
    let a = Math.sqrt(e / c.rt.length); const f0 = lt - fade; if (pos > f0) a *= 0.5 * (1 + Math.cos(Math.PI * (pos - f0) / fade)); return a;
  }
  function convolutionDisplay(box, ctx, cu) {   // cu: the IR this window sent (cu.env overview, cu.sec seconds, cu.v counter), when there is one
    const svg = svgOf(box); if (!svg) return null;
    const bars = [...svg.querySelectorAll(':scope > line')].filter(l => (l.getAttribute('stroke') || '').toLowerCase() === '#9a8df0'); if (bars.length < 20) return null;
    const endT = [...svg.querySelectorAll(':scope > text')].find(t => t.getAttribute('text-anchor') === 'end'), title = [...box.querySelectorAll('.val')].find(e => /, [\d.]+ s$/.test(e.textContent));
    const base = +bars[0].getAttribute('y1'), full = base - 12, x0 = 14, x1 = 924; let last = '';
    return { update() {
      const cat = Math.round(ctx.value('Category')), pre = (ctx.value('Pre-delay') || 0) / 1000, len = ctx.value('Length') || 100, size = ctx.value('Size') || 100, rev = (ctx.value('Reverse') || 0) > 0.5;
      if (!Number.isFinite(cat)) return; const key = [cat, pre, len, size, rev, cu ? cu.v : 0].join('|'); if (key === last) return; last = key;
      if (cat < 0 || cat > 3) {   // Custom: the overview of the IR this window sent (an IR the window did not send is not known to it)
        if (!(cu && cu.env)) { bars.forEach(b => b.setAttribute('y2', base)); if (title) title.textContent = 'Custom IR'; if (endT) endT.textContent = ''; return; }
        const irLen = Math.min(10, cu.sec * len / 100 * size / 100), T = pre + irLen, E = cu.env.length, hs = [];
        bars.forEach(b => { const t = ((+b.getAttribute('x1')) - x0) / (x1 - x0) * T, s = t - pre; if (s < 0 || s > irLen) { hs.push(0); return; } const src = (rev ? irLen - s : s) / (size / 100), v = cu.env[Math.min(E - 1, Math.max(0, Math.round(src / cu.sec * (E - 1))))]; hs.push(Math.pow(10, (v * 50 - 50) / 20)); });
        const mx = Math.max(...hs, 1e-9);
        bars.forEach((b, i) => { const db = hs[i] > 0 ? 20 * Math.log10(hs[i] / mx) : -999; b.setAttribute('y2', (base - clamp((db + 60) / 60, 0, 1) * full).toFixed(1)); });
        if (endT) endT.textContent = T.toFixed(1) + ' s'; if (title) title.textContent = 'Custom IR, ' + irLen.toFixed(1) + ' s'; return;
      }
      const c = RV04_CAT[cat], irLen = c.sec * len / 100 * size / 100, T = pre + irLen, hs = [];
      bars.forEach(b => { const t = ((+b.getAttribute('x1')) - x0) / (x1 - x0) * T, s = t - pre; hs.push(s < 0 || s > irLen ? 0 : rv04Envelope(cat, len, size, rev ? irLen - s : s)); });
      const mx = Math.max(...hs, 1e-9);
      bars.forEach((b, i) => { const db = hs[i] > 0 ? 20 * Math.log10(hs[i] / mx) : -999; b.setAttribute('y2', (base - clamp((db + 60) / 60, 0, 1) * full).toFixed(1)); });
      if (endT) endT.textContent = T.toFixed(1) + ' s'; if (title) title.textContent = c.name + ', ' + irLen.toFixed(1) + ' s';
    } };
  }


  // ---- the "Load IR" button of RV04: the page decodes the file (any format the web view knows, up to 30 s) and sends it as 32 bit float samples (interleaved) in base64 pieces: irbegin <channels> <rate>,
  // irdata ..., irend. readouts = IRs that worked / failed, an IR is in place. After it worked the Category is set to Custom. cu = what convolutionDisplay draws (the overview of this file)
  function irLoader(box, ctx, cu) {
    const btn = [...box.querySelectorAll('button')].find(b => /^Load IR$/i.test(b.textContent.trim())); if (!btn) return null;
    const input = document.createElement('input'); btn.parentNode.append(input);
    let busy = false, lastR = null, pend = null; const label = btn.textContent.trim();
    const say = (t, ms) => { btn.textContent = t; if (ms) setTimeout(() => { if (!busy) btn.textContent = label; }, ms); };
    pickFile(input, 'audio/*,.wav,.wave,.aif,.aiff,.mp3,.flac,.ogg,.m4a,.aac', async file => {
      if (busy) return; busy = true; say('Decoding …');
      try {
        const d = await decodeFile(file, 30), inter = new Float32Array(d.n * d.channels);
        if (d.channels === 2) for (let i = 0; i < d.n; i++) { inter[2 * i] = d.l[i]; inter[2 * i + 1] = d.r[i]; } else inter.set(d.l);
        ctx.call('irbegin', d.channels + ' ' + d.rate);
        await sendPieces(ctx, 'irdata', new Uint8Array(inter.buffer), f => say('Sending ' + Math.round(100 * f) + ' %'));
        pend = { done0: lastR ? lastR[0] : 0, failed0: lastR ? lastR[1] : 0, t: Date.now(), env: overviewOf(d.l, d.r, d.n, 121), sec: d.n / d.rate };
        say('Reading …'); ctx.call('irend', '');
      } catch (e) { busy = false; pend = null; say('Could not load', 3000); ctx.call('irabort', ''); }
    });
    btn.addEventListener('click', () => { if (!busy) input.click(); });
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 3) return; lastR = r; if (!pend) return;
      if (r[0] > pend.done0) {
        cu.env = pend.env; cu.sec = pend.sec; cu.v = (cu.v || 0) + 1; pend = null; busy = false; say('IR loaded', 2500);
        const c = ctx.params.find(p => p.name === 'Category'); if (c) { ctx.begin(c.i); ctx.set(c.i, 4); ctx.end(c.i); }
      } else if (r[1] > pend.failed0) { pend = null; busy = false; say('Could not read it', 3000); }
      else if (Date.now() - pend.t > 60000) { pend = null; busy = false; say('No answer', 3000); }
    } };
  }

  // ---- RV07 early reflections: the room from above (back wall at the bottom, the listener faces up). The circle is the listener, the square the source at Distance and Angle (+ = right);
  // the room is the one the plug-in uses (products/rv07/rv07.cpp geometry(): the same shape, grown when the source is farther than the room allows). Press or drag in the room to place the source (sets Distance and Angle).
  // The design drew two squares; the plug-in has one source, so the second square and its line are hidden.
  const RV07_ROOMS = [{ n: 'Small', d: [4.0, 3.0, 2.6] }, { n: 'Medium', d: [8.0, 6.0, 3.5] }, { n: 'Large', d: [20.0, 14.0, 6.0] }];
  function rv07Geometry(room, dist, angleDeg) {
    const r = RV07_ROOMS[clamp(room, 0, 2)].d, th = angleDeg * Math.PI / 180;
    const k = Math.max(1, dist * Math.max(Math.cos(th), 0) / (0.65 * r[0]), dist * Math.abs(Math.sin(th)) / (0.45 * r[1]));
    return { lx: r[0] * k, ly: r[1] * k, lis: 0.3 * r[0] * k, fwd: dist * Math.cos(th), right: dist * Math.sin(th) };
  }
  function earlyRoomDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const rects = [...svg.querySelectorAll(':scope > rect')], room = rects.find(r => r.getAttribute('rx') === '10'), sqs = rects.filter(r => r !== room), lis = svg.querySelector(':scope > circle');
    const lines = [...svg.querySelectorAll(':scope > line')], label = svg.querySelector(':scope > text');
    const P = n => ctx.params.find(q => q.name === n); const pd = P('Distance'), pa = P('Angle'); if (!room || !lis || sqs.length < 2 || lines.length < 2 || !pd || !pa) return null;
    const [, , W, H] = vbOf(svg), TOP = 10, BOT = H - 10, CX = W / 2, SZ = [16, 18]; let last = '', scale = 1, lisY = 0;
    sqs[1].style.display = 'none'; lines[1].style.display = 'none';
    const layout = () => {
      const n = Math.round(ctx.value('Room size')), d = ctx.value('Distance'), a = ctx.value('Angle'); if (![n, d, a].every(Number.isFinite)) return null;
      const g = rv07Geometry(n, d, a); scale = (BOT - TOP) / g.lx; return { n, d, a, g };
    };
    const set = (e, s0) => {
      const r = svg.getBoundingClientRect(), x = (e.clientX - r.left) / r.width * W, y = (e.clientY - r.top) / r.height * H;
      const right = (x - CX) / s0, fwd = (lisY - y) / s0;
      const ang = clamp(Math.atan2(right, Math.max(fwd, 0)) * 180 / Math.PI, pa.p.min, pa.p.max), dist = clamp(Math.hypot(right, Math.max(fwd, 0)), pd.p.min, pd.p.max);
      ctx.set(pd.i, pd.c.value(pd.c.norm(dist))); ctx.set(pa.i, pa.c.value(pa.c.norm(ang)));
    };
    let on = false, s0 = 1;
    svg.addEventListener('pointerdown', e => { if (!layout()) return; svg.setPointerCapture(e.pointerId); on = true; s0 = scale; ctx.begin(pd.i); ctx.begin(pa.i); set(e, s0); });
    svg.addEventListener('pointermove', e => { if (on) set(e, s0); });
    const end = () => { if (!on) return; on = false; ctx.end(pd.i); ctx.end(pa.i); }; svg.addEventListener('pointerup', end); svg.addEventListener('pointercancel', end);
    svg.style.cursor = 'crosshair';
    return { update() {
      const L = layout(); if (!L) return; const key = [L.n, L.d, L.a].join('|'); if (key === last) return; last = key;
      const { g } = L, w = g.ly * scale, h = g.lx * scale; lisY = BOT - g.lis * scale;
      room.setAttribute('x', (CX - w / 2).toFixed(1)); room.setAttribute('width', w.toFixed(1)); room.setAttribute('y', TOP); room.setAttribute('height', h.toFixed(1));
      lis.setAttribute('cx', CX); lis.setAttribute('cy', lisY.toFixed(1)); lis.setAttribute('r', 9);
      const sx = CX + g.right * scale, sy = lisY - g.fwd * scale;
      sqs[0].setAttribute('width', SZ[0]); sqs[0].setAttribute('height', SZ[1]); sqs[0].setAttribute('x', (sx - SZ[0] / 2).toFixed(1)); sqs[0].setAttribute('y', (sy - SZ[1] / 2).toFixed(1));
      lines[0].setAttribute('x1', sx.toFixed(1)); lines[0].setAttribute('y1', sy.toFixed(1)); lines[0].setAttribute('x2', CX); lines[0].setAttribute('y2', lisY.toFixed(1));
      if (label) { label.setAttribute('x', 20); label.textContent = RV07_ROOMS[clamp(L.n, 0, 2)].n + ' room, ' + g.lx.toFixed(1) + ' × ' + g.ly.toFixed(1) + ' m (listener faces up)'; }
    } };
  }


  // ---- VO01 pitch graph: what the singer sang (grey) and the corrected pitch (pink) over the last ~12 s. Rows are the notes of the chosen scale (Key, Scale; as sw::scaleBits), eight at a time;
  // the window moves by whole notes when the pitch leaves it. readouts: [voiced (1 / 0), the singer's pitch, the corrected pitch] as MIDI note numbers (the lines break where the voice is unvoiced)
  const NOTE_NAMES = ['C', 'C#', 'D', 'D#', 'E', 'F', 'F#', 'G', 'G#', 'A', 'A#', 'B'];
  const noteName = m => NOTE_NAMES[((m % 12) + 12) % 12] + (Math.floor(m / 12) - 1);
  function scaleNotes(scale, key, custom) {   // every MIDI note (24 .. 108) of the scale, ascending
    let mask = scale === 2 ? (custom & 0xFFF) : scale === 1 ? 0xFFF : [0, 2, 4, 5, 7, 9, 11].reduce((m, d) => m | (1 << ((d + key) % 12)), 0);
    if (!mask) mask = 0xFFF; const out = []; for (let n = 24; n <= 108; n++) if ((mask >> (n % 12)) & 1) out.push(n); return out;
  }
  function pitchGraphDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const texts = [...svg.querySelectorAll(':scope > text')], paths = [...svg.querySelectorAll(':scope > path')]; if (texts.length < 8 || paths.length < 3) return null;
    const [, , W, H] = vbOf(svg), ROWS = 8, RH = H / ROWS, N = 200, X0 = 40, X1 = W - 12, [grey, glow, line] = paths, hist = Ring(N, null);
    let notes = [], lo = 0, key = '', primed = false;
    const rowOf = m => { const r = notes.slice(lo, lo + ROWS); const k = r.findIndex((n, i) => i === r.length - 1 || m < r[i + 1]); const g = k >= r.length - 1 ? (r[r.length - 1] - r[r.length - 2]) : (r[k + 1] - r[k]); return k + (m - r[k]) / Math.max(1, g); };
    const yOf = m => clamp(H - RH * (rowOf(m) + 0.5), 2, H - 2);
    const nearestIdx = m => { let b = 0; notes.forEach((n, i) => { if (Math.abs(n - m) < Math.abs(notes[b] - m)) b = i; }); return b; };
    const label = () => texts.slice(0, ROWS).forEach((t, i) => { const n = notes[lo + ROWS - 1 - i]; t.textContent = n === undefined ? '' : noteName(n); });
    return { update(info) {
      const r = info && info.readouts; const sc = Math.round(ctx.value('Scale')), ky = Math.round(ctx.value('Key')), cu = ctx.value('Custom scale');
      if (![sc, ky].every(Number.isFinite)) return;
      const k = [sc, ky, cu].join('|'); if (k !== key) { key = k; notes = scaleNotes(sc, ky, cu || 0); lo = Math.max(0, Math.min(notes.length - ROWS, nearestIdx(60))); primed = false; label(); }
      if (!r || r.length < 3) return;
      const voiced = r[0] > 0.5 && r[1] > 20 && r[1] < 120; hist.push(voiced ? { m: r[1], c: r[2] } : null);
      if (voiced) {   // centre the rows on the first pitch heard, then only when the pitch leaves them
        const top = notes[lo + ROWS - 1], bot = notes[lo];
        if (!primed || r[1] < bot - 0.5 || r[1] > top + 0.5) { primed = true; lo = Math.max(0, Math.min(notes.length - ROWS, nearestIdx(r[1]) - 3)); label(); }
      }
      const poly = f => { let d = '', pen = false; hist.a.forEach((h, i) => { if (!h) { pen = false; return; } d += (pen ? ' L' : ' M') + (X0 + i / (N - 1) * (X1 - X0)).toFixed(1) + ' ' + yOf(f(h)).toFixed(1); pen = true; }); return d.trim() || 'M0 0'; };
      grey.setAttribute('d', poly(h => h.m)); const pd = poly(h => h.c); glow.setAttribute('d', pd); line.setAttribute('d', pd);
    } };
  }


  // ---- VO03 harmony: the singer's pitch (white), the selected voice (pink) and the next voice that is On (dim pink) over the last ~12 s, on a window of 24 semitones that moves in steps of 6
  // readouts: [voiced, the singer's pitch, voices 1 - 4] as MIDI note numbers (the lines break where the voice is unvoiced)
  function harmonyGraphDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const paths = [...svg.querySelectorAll(':scope > path')]; if (paths.length < 3) return null;
    const [, , W, H] = vbOf(svg), N = 200, X0 = 14, X1 = W - 14, SPAN = 24, hist = Ring(N, null); let centre = null;
    const yOf = m => clamp(H / 2 - (m - centre) / SPAN * H, 3, H - 3);
    const poly = f => { let d = '', pen = false; hist.a.forEach((h, i) => { const m = h && f(h); if (m === null || m === undefined) { pen = false; return; } d += (pen ? ' L' : ' M') + (X0 + i / (N - 1) * (X1 - X0)).toFixed(1) + ' ' + yOf(m).toFixed(1); pen = true; }); return d.trim() || 'M0 0'; };
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 6) return;
      const voiced = r[0] > 0.5 && r[1] > 20 && r[1] < 120, sel = clamp(ctx.band ? ctx.band() : 0, 0, 3), on = v => ctx.value('Voice ' + (v + 1) + ' On') > 0.5;
      const other = [0, 1, 2, 3].find(v => v !== sel && on(v));
      hist.push(voiced ? { lead: r[1], a: on(sel) ? r[2 + sel] : null, b: other === undefined ? null : r[2 + other] } : null);
      if (voiced && (centre === null || Math.abs(r[1] - centre) > 7)) centre = Math.round(r[1] / 6) * 6;
      if (centre === null) return;
      paths[0].setAttribute('d', poly(h => h.lead)); paths[1].setAttribute('d', poly(h => h.a)); paths[2].setAttribute('d', poly(h => h.b));
    } };
  }


  // ---- VO08 breath: the input level over the last ~12 s (grey area) with the output (line), and a pink band wherever the plug-in decided "breath"
  // readouts: [in a breath now (1 / 0), the gain it gets (dB), breaths counted]; levels from the measured input / output peaks. The design's three example bands are replaced by the real ones.
  function breathDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const rects = [...svg.querySelectorAll(':scope > rect')], texts = [...svg.querySelectorAll(':scope > text')], area = svg.querySelector(':scope > path'); if (!rects.length || !texts.length || !area) return null;
    const bandAttr = { fill: rects[0].getAttribute('fill'), 'fill-opacity': rects[0].getAttribute('fill-opacity') }, textAttr = {}; for (const a of texts[0].attributes) textAttr[a.name] = a.value;
    rects.forEach(r => r.remove()); texts.forEach(t => t.remove());
    const [, , W, H] = vbOf(svg), N = 200, CY = H / 2, AMP = CY - 10, lvl = Ring(N, -90), outl = Ring(N, -90), flag = Ring(N, 0), pool = [];
    const out = mkEl('path', { fill: 'none', stroke: '#f2f2f2', 'stroke-width': 1.2, 'stroke-opacity': 0.8 }); svg.append(out);
    const band = k => pool[k] || (pool[k] = { r: svg.insertBefore(mkEl('rect', Object.assign({ y: 0, height: H }, bandAttr)), area), t: svg.insertBefore(mkEl('text', Object.assign({}, textAttr, { y: 14 })), area) });
    const hgt = db => clamp((db + 60) / 60, 0, 1) * AMP;
    return { update(info) {
      const m = info && info.meters, r = info && info.readouts; if (!m || !r || r.length < 3) return;
      lvl.push(Math.max(m[0], m[1])); outl.push(Math.max(m[2], m[3])); flag.push(r[0] > 0.5 ? 1 : 0);
      const dx = W / (N - 1); let up = '', dn = '', o = '';
      for (let i = 0; i < N; i++) { const x = (i * dx).toFixed(1); up += (i ? ' L' : 'M') + x + ' ' + (CY - hgt(lvl.a[i])).toFixed(1); o += (i ? ' L' : 'M') + x + ' ' + (CY - hgt(outl.a[i])).toFixed(1); }
      for (let i = N - 1; i >= 0; i--) dn += ' L' + (i * dx).toFixed(1) + ' ' + (CY + hgt(lvl.a[i]) * 0.9).toFixed(1);
      area.setAttribute('d', up + dn + ' Z'); out.setAttribute('d', o);
      let k = 0, i = 0;
      while (i < N) {
        if (!flag.a[i]) { i++; continue; }
        let j = i; while (j < N && flag.a[j]) j++;
        const b = band(k++), x0 = i * dx, w = Math.max(3, (j - i) * dx);
        b.r.setAttribute('x', x0.toFixed(1)); b.r.setAttribute('width', w.toFixed(1)); b.r.style.display = '';
        b.t.setAttribute('x', (x0 + w / 2).toFixed(1)); b.t.textContent = w > 34 ? 'Breath' : ''; b.t.style.display = '';
        i = j;
      }
      for (; k < pool.length; k++) { pool[k].r.style.display = 'none'; pool[k].t.style.display = 'none'; }
    } };
  }


  // ---- CR01 filter: the response of the two-pole state-variable filter (Type, Resonance) at the cutoff that is in use (the core's modulated cutoff), and the modulator over the last ~12 s (grey)
  // H(w) with w = tan(pi f / fsOs) / tan(pi fc / fsOs), damping k = max(0.1, 2 (1 - 0.95 r)); LP 1/D, BP k jw/D, HP -w^2/D, Notch (1 - w^2)/D with D = 1 - w^2 + j k w (fsOs = 2 x 48 kHz).
  // readouts: [the modulator 0 .. 1, the cutoff in use (Hz)]; the drive's saturation is not drawn.
  function cr01Gain(type, res, fc, f, fsOs) {   // dB
    const k = Math.max(0.1, 2 * (1 - 0.95 * res)), g = Math.tan(Math.PI * Math.min(fc, 0.45 * fsOs) / fsOs), w = Math.tan(Math.PI * Math.min(f, 0.45 * fsOs) / fsOs) / g, re = 1 - w * w, im = k * w;
    const num = [[1, 0], [0, k * w], [-w * w, 0], [re, 0]][type] || [1, 0];
    return 20 * Math.log10(Math.max(Math.hypot(num[0], num[1]) / Math.sqrt(re * re + im * im), 1e-3));
  }
  function filterResponseDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const paths = [...svg.querySelectorAll(':scope > path')], label = svg.querySelector(':scope > text'); if (paths.length < 3) return null;
    const [glow, line, mod] = paths, [, , W, H] = vbOf(svg), N = 160, M = 200, fsOs = 96000, hist = Ring(M, 0), names = ['Envelope', 'LFO', 'Sidechain']; let last = '', Y0 = 96.5;
    const yDb = db => clamp(Y0 - db * 2.6, 3, H - 3);
    return { update(info) {
      const r = info && info.readouts, type = Math.round(ctx.value('Type')), res = ctx.value('Resonance') / 100, src = Math.round(ctx.value('Mod source'));
      if (!Number.isFinite(type) || !Number.isFinite(res)) return;
      const fc = r && r.length >= 2 && r[1] > 5 ? r[1] : ctx.value('Cutoff'); if (r && r.length >= 2) hist.push(clamp(r[0], 0, 1));
      const key = [type, res, Math.round(fc), src].join('|');
      if (key !== last) {
        last = key; let d = '';
        for (let i = 0; i <= N; i++) d += (i ? ' L' : 'M') + (i / N * W).toFixed(1) + ' ' + yDb(cr01Gain(type, res, fc, 20 * Math.pow(1000, i / N), fsOs)).toFixed(1);
        glow.setAttribute('d', d); line.setAttribute('d', d); if (label && names[src]) label.textContent = names[src];
      }
      let m = ''; hist.a.forEach((v, i) => { m += (i ? ' L' : 'M') + (i / (M - 1) * W).toFixed(1) + ' ' + (H - 6 - v * 70).toFixed(1); }); mod.setAttribute('d', m);
    } };
  }


  // ---- CR02 stutter grid: the first row is the 16-step pattern (cr02.step01 .. step16; a click on a pad turns the step on or off). The design's other two rows (Reverse, Pitch) are not per-step
  // in the plug-in: they show what the global Reverse and Pitch do to the steps that are On (lit = that setting is active), and cannot be clicked.
  function stutterGridDisplay(box, ctx) {
    const d = box.querySelector('.disp'); if (!d) return null;
    const rows = [...d.querySelectorAll('div')].filter(r => r.querySelectorAll(':scope > div').length === 16), pads = rows.map(r => [...r.querySelectorAll(':scope > div')]);
    if (rows.length < 3) return null;
    const idx = ctx.params.filter(q => /^Step \d+$/.test(q.name)).sort((a, b) => parseInt(a.name.slice(5)) - parseInt(b.name.slice(5))).map(q => q.i); if (idx.length !== 16) return null;
    const off = i => (((i >> 2) & 1) ? '#1d1e22' : '#24262a'), ON = 'var(--acc)'; let last = '';
    pads[0].forEach((p, i) => { p.style.cursor = 'pointer'; p.title = 'Step ' + (i + 1); p.addEventListener('click', () => { const v = ctx.get(idx[i]) > 0.5 ? 0 : 1; ctx.begin(idx[i]); ctx.set(idx[i], v); ctx.end(idx[i]); }); });
    pads[1].forEach(p => { p.title = 'Reverse (a setting for every step that is on)'; }); pads[2].forEach(p => { p.title = 'Pitch (a setting for every step that is on)'; });
    return { update() {
      const on = idx.map(i => ctx.get(i) > 0.5), rev = ctx.value('Reverse') > 0.5, pit = Math.abs(ctx.value('Pitch') || 0) > 1e-9, key = on.join('') + rev + pit; if (key === last) return; last = key;
      for (let i = 0; i < 16; i++) { pads[0][i].style.background = on[i] ? ON : off(i); pads[1][i].style.background = on[i] && rev ? ON : off(i); pads[2][i].style.background = on[i] && pit ? ON : off(i); }
    } };
  }


  // ---- LO03 low focus: the two bells the plug-in applies (the Focus bell, Q 1.2, at the gain the core has right now; the Mud cut bell, Q 1.0, cut 6 dB x Tight) on 20 Hz - 2 kHz,
  // and the "Mono below" corner as a dashed line. Drag a numbered dot sideways to move its frequency (1 = Focus, 2 = Mud cut). readouts: [the Focus bell's gain (dB, <= 0)]
  function lowFocusDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const grid = [...svg.querySelectorAll(':scope > line')], paths = [...svg.querySelectorAll(':scope > path')], dots = [...svg.querySelectorAll(':scope > circle')], nums = [...svg.querySelectorAll(':scope > text')].filter(t => t.getAttribute('text-anchor') === 'middle');
    const [area, glow, line] = paths; if (paths.length < 3 || dots.length < 2 || nums.length < 2 || grid.length < 6) return null;
    const P = n => ctx.params.find(q => q.name === n), pf = P('Focus'), pm = P('Mud cut'); if (!pf || !pm) return null;
    const [, , W, H] = vbOf(svg), Y0 = 100, PXDB = 8, F0 = 20, F1 = 2000, N = 220, xOf = f => Math.log10(f / F0) / Math.log10(F1 / F0) * W, fOf = x => F0 * Math.pow(F1 / F0, x / W);
    // grid at 50, 100, 200, 500, 1000 Hz
    [50, 100, 200, 500, 1000].forEach((f, i) => { const l = grid[i]; l.setAttribute('x1', xOf(f).toFixed(1)); l.setAttribute('x2', xOf(f).toFixed(1)); });
    const caption = [...svg.querySelectorAll(':scope > text')].find(t => t.getAttribute('text-anchor') === 'start'), roleText = ['Kick: the tail is tightened', 'Bass: ducked by the key input', "Kick and bass in one track: tail and ducking from its own lows"];
    const mono = mkEl('line', { y1: 0, y2: H, stroke: '#f2f2f2', 'stroke-opacity': 0.35, 'stroke-dasharray': '3 4' }), monoT = mkEl('text', { fill: '#8d8d8d', 'font-family': 'Barlow Condensed, sans-serif', 'font-size': 10, y: H - 8 }); svg.append(mono, monoT);
    const yOf = db => clamp(Y0 - db * PXDB, 4, H - 4); let last = '', drag = null;
    dots.slice(0, 2).forEach((c, k) => {
      c.style.cursor = 'ew-resize'; nums[k].style.pointerEvents = 'none';
      c.addEventListener('pointerdown', e => { c.setPointerCapture(e.pointerId); drag = k; const p = k ? pm : pf; ctx.begin(p.i); });
      c.addEventListener('pointermove', e => { if (drag !== k) return; const r = svg.getBoundingClientRect(), p = k ? pm : pf, f = fOf(clamp((e.clientX - r.left) / r.width * W, 0, W)); ctx.set(p.i, p.c.value(p.c.norm(clamp(f, p.p.min, p.p.max)))); });
      const end = () => { if (drag !== k) return; drag = null; ctx.end((k ? pm : pf).i); }; c.addEventListener('pointerup', end); c.addEventListener('pointercancel', end);
    });
    return { update(info) {
      const r = info && info.readouts, fcut = r && r.length >= 1 && Number.isFinite(r[0]) ? Math.min(0, r[0]) : 0, ff = ctx.value('Focus'), fm = ctx.value('Mud cut'), tight = ctx.value('Tight') / 100, fmono = ctx.value('Mono below');
      if (![ff, fm, tight, fmono].every(Number.isFinite)) return; const key = [Math.round(fcut * 20), ff, fm, tight, fmono, ctx.value('Role')].join('|'); if (key === last) return; last = key;
      const mud = -6 * tight; let d = ''; const role = Math.round(ctx.value('Role')); if (caption && roleText[role]) caption.textContent = roleText[role];
      for (let i = 0; i <= N; i++) { const f = F0 * Math.pow(F1 / F0, i / N), db = biquadMag('bell', ff, fcut, 1.2, f) + biquadMag('bell', fm, mud, 1.0, f), x = (i / N * W).toFixed(1); d += (i ? ' L' : 'M') + x + ' ' + yOf(db).toFixed(1); }
      glow.setAttribute('d', d); line.setAttribute('d', d); area.setAttribute('d', d + ' L' + W + ' ' + Y0 + ' L0 ' + Y0 + ' Z');
      [[ff, fcut], [fm, mud]].forEach(([f, g], k) => { const x = xOf(f), y = yOf(g); dots[k].setAttribute('cx', x.toFixed(1)); dots[k].setAttribute('cy', y.toFixed(1)); nums[k].setAttribute('x', x.toFixed(1)); nums[k].setAttribute('y', (y + 3.5).toFixed(1)); });
      const mx = xOf(clamp(fmono, F0, F1)).toFixed(1); mono.setAttribute('x1', mx); mono.setAttribute('x2', mx); monoT.setAttribute('x', (+mx + 4).toFixed(1)); monoT.textContent = 'Mono below ' + Math.round(fmono) + ' Hz';
    } };
  }


  // ---- LIVE strips with level ladders (LV03, LV04): the design's `.bar` ladders follow the measured peaks (two bars = output L / R; four = input L / R, output L / R).
  // LV03: the GR bar is the compressor's gain reduction (readouts: [gate gain, compressor gain, de-ess gain] in dB, from the core) and the gate lamp shows whether the gate is open (gain above -3 dB).
  // LV04: the GR bar is the drop from the input peak to the output peak (no gain reduction readout in the core); Overs = limit events (readouts[1]); Max peak and Max GR are the largest values seen
  // since the screen opened (click to reset).
  const BAR_COL = 'linear-gradient(to top,#2bd14a 0 70%,#f0c93d 70% 88%,#e0443e 88%)';
  const ladder = (b, db) => { const pct = clamp((db + 30) / 30, 0, 1) * 100; b.style.background = 'linear-gradient(to top,transparent 0 ' + pct.toFixed(1) + '%,rgba(30,31,34,.92) ' + pct.toFixed(1) + '%),' + BAR_COL; };
  function liveStripDisplay(box, ctx, kind) {
    const bars = [...box.querySelectorAll('.bar')]; if (bars.length < 2) return null;
    const grFill = [...box.querySelectorAll('div')].find(d => /position:absolute/.test(d.getAttribute('style') || '') && /top:0/.test(d.getAttribute('style') || '') && /background:#f0ad3d/.test(d.getAttribute('style') || '') && /height:\d+%/.test(d.getAttribute('style') || ''));
    const lamp = kind === 'LV03' ? [...box.querySelectorAll('.dot')].find(e => e.nextElementSibling && /^open$/i.test(e.nextElementSibling.textContent.trim())) : null, lampT = lamp && lamp.nextElementSibling;
    const boxes = [...box.querySelectorAll('.box')], val = n => { const b = boxes.find(x => x.firstElementChild && x.firstElementChild.textContent.trim().toLowerCase() === n); return b && b.querySelector('.rv'); };
    const eOver = val('overs'), ePk = val('max peak'), eGr = val('max gr'); let maxPk = -200, maxGr = 0;
    [ePk, eGr].forEach(e => { if (e) { e.parentElement.style.cursor = 'pointer'; e.parentElement.title = 'Click to reset'; e.parentElement.addEventListener('click', () => { maxPk = -200; maxGr = 0; }); } });
    return { update(info) {
      const m = info && info.meters, r = info && info.readouts; if (!m) return;
      if (bars.length >= 4) { ladder(bars[0], m[0]); ladder(bars[1], m[1]); ladder(bars[2], m[2]); ladder(bars[3], m[3]); } else { ladder(bars[0], m[2]); ladder(bars[1], m[3]); }
      let gr = 0;
      if (kind === 'LV03' && r && r.length >= 3) { gr = Math.max(0, -r[1]); if (lamp) { const open = r[0] > -3; lamp.style.background = open ? '' : '#3a3b3f'; lamp.style.boxShadow = open ? '' : 'none'; lampT.textContent = open ? 'Open' : 'Closed'; } }
      else { const inP = Math.max(m[0], m[1]), outP = Math.max(m[2], m[3]); gr = inP > -70 ? Math.max(0, inP - outP) : 0; maxPk = Math.max(maxPk, outP); maxGr = Math.max(maxGr, gr); }
      if (grFill) grFill.style.height = clamp(gr / (kind === 'LV03' ? 20 : 12), 0, 1) * 100 + '%';
      if (ePk) ePk.textContent = maxPk > -150 ? maxPk.toFixed(1) : '—'; if (eGr) eGr.textContent = maxGr > 0.05 ? '-' + maxGr.toFixed(1) : '0.0';
      if (eOver && r && r.length >= 2) eOver.textContent = String(Math.round(r[1]));
    } };
  }


  // ---- LV30 recorder: the recording badge (a click starts or stops the recording), the input level of the last minute with a triangle for every mark, the file format the plug-in really writes
  // readouts: [recording (1 / 0), seconds recorded, low disk (1 / 0), files written, marks made, the host's sample rate]. The design's example numbers (Rec 01:23:45, Disk free 412 GB) are gone:
  // the disk is shown as OK / LOW while recording (the core only knows "under 200 MB"), not as a size.
  function recorderDisplay(box, ctx) {
    const svg = svgOf(box), stat = box.querySelector('.stat'); if (!svg || !stat) return null;
    const dot = stat.querySelector('.dot'), txt = [...stat.childNodes].find(n => n.nodeType === 3);
    const area = svg.querySelector(':scope > path'), now = svg.querySelector(':scope > line'), tris = [...svg.querySelectorAll(':scope > path')].slice(1);
    const rb = [...box.querySelectorAll('.rbox')].map(b => [...b.querySelectorAll('span')]), disk = rb.find(x => /^Disk/i.test(x[0].textContent)), fmt = rb.find(x => /^Format/i.test(x[0].textContent));
    if (!area || !now || !txt) return null;
    tris.forEach(t => t.remove()); if (disk) disk[0].textContent = 'Disk';
    const [, , W, H] = vbOf(svg), XN = +now.getAttribute('x1'), X0 = 14, N = 120, STEP = 500, SPAN = N * STEP, CY = (H - 12) / 2, AMP = CY - 6, hist = Ring(N, -90), pool = [], marks = [];
    let t0 = 0, peak = -90, lastMarks = 0, lastRec = false; stat.style.cursor = 'pointer';
    stat.addEventListener('click', () => { if (ctx.call) ctx.call('record', lastRec ? '0' : '1'); });
    const hms = sec => { const s = Math.floor(sec); return [Math.floor(s / 3600), Math.floor(s / 60) % 60, s % 60].map(v => String(v).padStart(2, '0')).join(':'); };
    const tri = k => pool[k] || (pool[k] = svg.insertBefore(mkEl('path', { fill: '#f0c93d' }), svg.querySelector(':scope > text:last-of-type')));
    return { update(info) {
      const m = info && info.meters, r = info && info.readouts; if (!m || !r || r.length < 6) return; const t = performance.now(); if (!t0) t0 = t;
      peak = Math.max(peak, m[0], m[1]); if (t - t0 >= STEP) { hist.push(peak); peak = -90; t0 = t; }
      const rec = r[0] > 0.5; lastRec = rec;
      if (r[4] > lastMarks) for (let k = lastMarks; k < r[4]; k++) marks.push(t); lastMarks = r[4]; if (r[4] === 0) marks.length = 0;
      if (dot) { dot.style.background = rec ? '#e0443e' : '#55575c'; dot.style.boxShadow = rec ? '0 0 6px #e0443e' : 'none'; }
      txt.textContent = rec ? 'Rec ' + hms(r[1]) : 'Stopped';
      stat.title = rec ? 'Click to stop the recording' : 'Click to start recording (into Documents/SW AUDIO until a folder has been chosen)';
      let up = '', dn = '';
      for (let i = 0; i < N; i++) { const x = X0 + i / (N - 1) * (XN - X0), a = clamp((hist.a[i] + 60) / 60, 0, 1) * AMP; up += (i ? ' L' : 'M') + x.toFixed(1) + ' ' + (CY - a).toFixed(1); dn = ' L' + x.toFixed(1) + ' ' + (CY + a * 0.9).toFixed(1) + dn; }
      area.setAttribute('d', up + dn + ' Z');
      while (marks.length && t - marks[0] > SPAN) marks.shift();
      marks.forEach((mt, k) => { const x = XN - (t - mt) / SPAN * (XN - X0), p = tri(k); p.setAttribute('d', 'M' + (x - 5).toFixed(1) + ' ' + (H - 4) + ' L' + (x + 5).toFixed(1) + ' ' + (H - 4) + ' L' + x.toFixed(1) + ' ' + (H - 12) + ' Z'); p.style.display = ''; });
      for (let k = marks.length; k < pool.length; k++) pool[k].style.display = 'none';
      if (disk) disk[1].textContent = rec ? (r[2] > 0.5 ? 'LOW' : 'OK') : '—';
      if (fmt) { const flac = ctx.value('Format') > 0.5, bits = ['16-bit', '24-bit', '32-bit float'][Math.round(ctx.value('Bit depth'))] || '', fs = r[5] > 0 ? r[5] : 48000;
        fmt[1].textContent = 'WAV ' + (fs / 1000).toFixed(fs % 1000 ? 1 : 0) + 'k ' + bits + (flac ? '*' : ''); fmt[1].title = flac ? 'FLAC is not written yet: the file is WAV' : 'The file has the host\'s sample rate'; }
    } };
  }


  // ---- LV02 feedback suppressor: the notch filters the plug-in has now (F1..F12, FIXED = kept, LIVE = found by the detector) as a curve on 20 Hz - 20 kHz, and in the twelve chips
  // readouts: for each slot frequency (Hz, 0 = unused) and depth (dB, +100 = FIXED), then ring out (1 / 0). The curve is the sum of the bells the core sets (Q from Width as in sw/feedback.hpp).
  // The design's example filters and "Watching 4.0k" are gone (the core does not report what it is watching).
  function feedbackFiltersDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const chips = [...box.querySelectorAll('.slot')]; if (chips.length !== 12) return null;
    const curve = [...svg.querySelectorAll(':scope > path')].find(p => p.getAttribute('fill') === 'none' && p.getAttribute('stroke') === '#f2f2f2'), g = [...svg.querySelectorAll(':scope > g')].find(e => e.getAttribute('font-weight') === '700');
    const watchPath = [...svg.querySelectorAll(':scope > path')].find(p => p.getAttribute('fill') === '#f0c93d'), watchText = [...svg.querySelectorAll(':scope > text')].find(t => /^Watching/.test(t.textContent));
    if (!curve || !g) return null;
    if (watchPath) watchPath.style.display = 'none'; if (watchText) watchText.style.display = 'none';
    const [, , W, H] = vbOf(svg), Y0 = 24, PXDB = 8, N = 200, xOf = f => 394.2 + 232 * Math.log10(f / 1000), labels = [...g.querySelectorAll('text')]; g.innerHTML = '';
    const fmt = f => f >= 1000 ? (f / 1000).toFixed(f >= 10000 ? 1 : 2).replace(/\.?0+$/, '') + 'k' : Math.round(f) + ' Hz', pool = [];
    let last = '';
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 25) return; const key = r.slice(0, 24).map(v => Math.round(v * 10)).join(',') + '|' + ctx.value('Width'); if (key === last) return; last = key;
      const width = ctx.value('Width') || 0.1, q = Math.max(0.3, 1 / (2 * Math.sinh(Math.LN2 * width))), slots = [];
      for (let i = 0; i < 12; i++) { const f = r[2 * i], e = r[2 * i + 1], fixed = e >= 100; slots.push({ f, depth: fixed ? e - 100 : e, fixed, on: f > 0 }); }
      let d = '';
      for (let k = 0; k <= N; k++) { const f = 20 * Math.pow(1000, k / N); let db = 0; slots.forEach(s => { if (s.on) db += biquadMag('bell', s.f, -s.depth, q, f); }); d += (k ? ' L' : 'M') + (k / N * W).toFixed(1) + ' ' + clamp(Y0 - db * PXDB, 6, H - 18).toFixed(1); }
      curve.setAttribute('d', d);
      slots.forEach((s, i) => {
        const t = pool[i] || (pool[i] = g.appendChild(mkEl('text', {})));
        if (s.on) { t.setAttribute('x', xOf(s.f).toFixed(1)); t.setAttribute('y', clamp(Y0 + s.depth * PXDB + 16, 20, H - 6).toFixed(1)); t.textContent = 'F' + (i + 1); t.style.display = ''; } else t.style.display = 'none';
        const c = chips[i], tag = c.querySelector('.t span:last-child'), val = c.querySelector('.f');
        c.classList.toggle('fx', s.on && s.fixed); c.classList.toggle('lv', s.on && !s.fixed);
        if (tag) { tag.textContent = s.on ? (s.fixed ? 'FIXED' : 'LIVE') : '—'; tag.style.color = s.on ? '#f2f2f2' : ''; }
        if (val) { val.textContent = s.on ? fmt(s.f) + ' -' + Math.round(s.depth) : '--'; val.style.color = s.on ? '' : '#5a5c60'; }
      });
    } };
  }


  // ---- LV01 voice strip: the four stage tiles (Noise, EQ, Comp, Limit) show what Use x Voice set (the core's stage values) and the compressor's live gain reduction; IN / GR / OUT bars follow the measured
  // peaks and the core's compressor + limiter gain. readouts: [noise depth, low cut Hz, mud, presence, air, comp threshold, ratio, make-up, ceiling, compressor gain (dB), limiter gain (dB)].
  // "Stream loudness -14.6 LUFS target -14" was an example: the strip does not measure loudness (LV06 does), so it shows a dash.
  function voiceStripDisplay(box, ctx) {
    const tiles = [...box.querySelectorAll('.tile')], tile = n => tiles.find(t => (t.querySelector('b') || {}).textContent.trim().toLowerCase() === n);
    const part = t => t && { dot: t.querySelector('b > span'), val: [...t.children].find(c => c.tagName === 'SPAN') };
    const nz = part(tile('noise')), eq = part(tile('eq')), cp = part(tile('comp')), li = part(tile('limit'));
    const rows = [...box.querySelectorAll('.mb')], mv = rows.map(r => r.parentElement.querySelector('.mv')), ml = rows.map(r => (r.parentElement.querySelector('.ml') || {}).textContent);
    const iIn = ml.findIndex(t => /^IN$/i.test(t || '')), iGr = ml.findIndex(t => /^GR$/i.test(t || '')), iOut = ml.findIndex(t => /^OUT$/i.test(t || ''));
    const loud = [...box.querySelectorAll('span')].find(e => /LUFS/.test(e.textContent) && e.children.length === 1);
    if (loud) { loud.firstElementChild.textContent = ''; loud.firstChild.textContent = '—'; }
    const GRAD = 'linear-gradient(to right,#2bd14a 0 72%,#f0c93d 72% 88%,#e0443e 88%)';
    const lv = (r, db) => { const p = clamp((db + 36) / 36, 0, 1) * 100; r.style.background = 'linear-gradient(to right,transparent 0 ' + p.toFixed(1) + '%,rgba(30,31,34,.92) ' + p.toFixed(1) + '%),' + GRAD; };   // the design's own overlay (8 % white) left the whole bar lit
    const lamp = (p, on) => { if (p && p.dot) p.dot.className = on ? 'dot' : 'off'; };
    return { update(info) {
      const m = info && info.meters, r = info && info.readouts; if (!m || !r || r.length < 11) return;
      if (iIn >= 0) { const v = Math.max(m[0], m[1]); lv(rows[iIn], v); if (mv[iIn]) mv[iIn].textContent = v > -99 ? v.toFixed(1) : '-∞'; }
      if (iOut >= 0) { const v = Math.max(m[2], m[3]); lv(rows[iOut], v); if (mv[iOut]) mv[iOut].textContent = v > -99 ? v.toFixed(1) : '-∞'; }
      const gr = Math.max(0, -(r[9] + r[10]));
      if (iGr >= 0) { const p = clamp(gr / 15, 0, 1) * 100; rows[iGr].style.background = 'linear-gradient(to right,#bdbdbd 0 ' + p.toFixed(1) + '%,rgba(255,255,255,.08) ' + p.toFixed(1) + '%)'; if (mv[iGr]) mv[iGr].textContent = gr > 0.05 ? '-' + gr.toFixed(1) : '0.0'; }
      const noiseOn = r[0] < -0.05, eqOn = Math.abs(r[2]) + Math.abs(r[3]) + Math.abs(r[4]) > 0.05 || r[1] > 20.5, compOn = r[6] > 1.001;
      if (nz) { nz.val.textContent = noiseOn ? Math.round(r[0]) + ' dB' : 'Off'; lamp(nz, noiseOn); tile('noise').classList.toggle('on', noiseOn); }
      if (eq) { eq.val.textContent = eqOn ? 'On' : 'Off'; lamp(eq, eqOn); tile('eq').classList.toggle('on', eqOn); }
      if (cp) { cp.val.textContent = compOn ? 'GR ' + (r[9] < -0.05 ? Math.round(-r[9]) : '0') : 'Off'; lamp(cp, compOn); tile('comp').classList.toggle('on', compOn); }
      if (li) { li.val.textContent = Math.round(r[8]) + ' dBFS'; lamp(li, true); tile('limit').classList.add('on'); }
    } };
  }


  // ---- DL01 echo: the LCD shows the delay the core uses (Sync On with a host tempo: the note length; else Time), the host tempo (a dash without one), the note the Time knob picks (Sync On) and Ping-pong.
  // readouts: [the delay in use (s), the host tempo (bpm, 0 = none)]. The design's "L 375  R 250" (two different times) is not what the plug-in does (both lines have the same time): it says Ping-pong or nothing.
  const NOTE_LONG = ['1/64', '1/32 triplet', '1/32', '1/16 triplet', '1/32 dotted', '1/16', '1/8 triplet', '1/16 dotted', '1/8', '1/4 triplet', '1/8 dotted', '1/4', '1/2 triplet', '1/4 dotted', '1/2', '1/2 dotted', '1 bar', '2 bars'];
  function echoLcdDisplay(box, ctx) {
    const v = [...box.querySelectorAll('.vfd')]; const big = v.find(e => /font-size:\s*40px/.test(e.getAttribute('style') || '')), unit = big && big.nextElementSibling, bpm = v.find(e => /BPM$/.test(e.textContent.trim()));
    const note = v.find(e => /^1\/|bar/.test(e.textContent.trim()) && e !== big), side = v.find(e => /^L \d+/.test(e.textContent.trim())); if (!big || !bpm) return null;
    const pt = ctx.params.find(q => q.name === 'Time');
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 2) return; const ms = r[0] * 1000;
      big.textContent = ms >= 1000 ? (ms / 1000).toFixed(2) : String(Math.round(ms)); if (unit) unit.textContent = ms >= 1000 ? 's' : 'ms';
      bpm.textContent = r[1] > 0 ? Math.round(r[1]) + ' BPM' : '— BPM';
      const sync = ctx.value('Sync') > 0.5; if (note) note.textContent = sync && pt ? NOTE_LONG[clamp(Math.round(pt.c.norm(ctx.value('Time')) * 17), 0, 17)] : 'Free';
      if (side) side.textContent = ctx.value('Ping-pong') > 0.5 ? 'Ping-pong' : '';
    } };
  }


  // ---- VO01 / VO03: the Key (and for VO03 Major / Minor) had no control in the designs, so the harmony and the correction could only be set to another key from the host's parameter list.
  // A "Key C" chip is added to the scale chips (a click steps to the next key); VO03 also gets a Major / Minor chip. VO01's first chip prints the key ("D major"). The chips use the design's own chip class.
  function keyChips(box, ctx, kind) {
    const pk = ctx.params.find(q => q.name === 'Key'); if (!pk) return null;
    const first = [...box.querySelectorAll('button.dbtn')].find(b => /^(MIDI|C major)$/i.test(b.textContent.trim()) || /major$/i.test(b.textContent.trim()) || b.dataset.p === '0' && kind === 'VO03'); if (!first) return null;
    const grp = first.parentElement, mk = (t, title) => { const b = document.createElement('button'); b.className = 'dbtn'; b.textContent = t; b.title = title; grp.prepend(b); return b; };
    const ps = kind === 'VO03' ? ctx.params.find(q => q.name === 'Scale') : null;
    const sc = ps ? mk('Major', 'Scale: major or minor (click to switch)') : null, kb = mk('Key C', 'Key (click for the next one)'), majorChip = kind === 'VO01' ? [...grp.querySelectorAll('button')].find(b => /major$/i.test(b.textContent.trim())) : null;
    kb.addEventListener('click', () => { const st = pk.p.steps, k = st.findIndex(v => Math.abs(v - ctx.get(pk.i)) < 1e-9); ctx.begin(pk.i); ctx.set(pk.i, st[(k + 1) % st.length]); ctx.end(pk.i); });
    if (sc) sc.addEventListener('click', () => { const st = ps.p.steps, k = st.findIndex(v => Math.abs(v - ctx.get(ps.i)) < 1e-9); ctx.begin(ps.i); ctx.set(ps.i, st[(k + 1) % st.length]); ctx.end(ps.i); });
    return { update() {
      const k = Math.round(pk.c.norm(ctx.get(pk.i)) * (pk.p.steps.length - 1)), name = (pk.p.labels && pk.p.labels[k]) || '';
      kb.textContent = 'Key ' + name; if (majorChip) majorChip.textContent = name + ' major';
      if (sc) { const j = Math.round(ps.c.norm(ctx.get(ps.i)) * (ps.p.steps.length - 1)); sc.textContent = (ps.p.labels && ps.p.labels[j]) || ''; }
    } };
  }


  // ---- LV14 align: the two arrivals (the main system, and this speaker after the Delay set on the plug-in) and the bracket between them labelled with the Delay. The spacing is on a log scale
  // (0 - 500 ms) so a few ms and a few hundred ms are both visible; with Delay 0 the two peaks sit on top of each other. The design's "12.4 ms" was an example.
  function alignGraphDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const paths = [...svg.querySelectorAll(':scope > path')], texts = [...svg.querySelectorAll(':scope > text')], lines = [...svg.querySelectorAll(':scope > line')];
    if (paths.length < 3 || texts.length < 3 || lines.length < 2) return null;
    const [, glow, line] = paths, tSecond = texts.find(t => /speaker/i.test(t.textContent)), tMain = texts.find(t => /PA/i.test(t.textContent)), tMs = texts.find(t => /ms$/.test(t.textContent)), br = lines[lines.length - 1];
    const X0 = +br.getAttribute('x1'), CX2 = tSecond ? +tSecond.getAttribute('x') : X0, SPAN = 548; let last = null;   // CX2: where the design drew the second arrival
    return { update() {
      const d = ctx.value('Delay'); if (!Number.isFinite(d) || d === last) return; last = d;
      const dx = SPAN * Math.log10(1 + d) / Math.log10(501), tr = 'translate(' + (X0 + dx - CX2).toFixed(1) + ' 0)';
      glow.setAttribute('transform', tr); line.setAttribute('transform', tr); if (tSecond) tSecond.setAttribute('transform', tr);
      br.setAttribute('x2', (X0 + dx).toFixed(1)); if (tMs) { tMs.setAttribute('x', (X0 + dx / 2).toFixed(1)); tMs.textContent = (d < 10 ? d.toFixed(2) : d < 100 ? d.toFixed(1) : String(Math.round(d))) + ' ms'; }
      br.style.display = d > 0.005 ? '' : 'none'; if (tMs) tMs.style.display = d > 0.005 ? '' : 'none';
    } };
  }


  // ---- LV27 / LV28: the connections they show are not in this version (LV27 has no link to OBS, LV28 has no tablet server: README), so the design's example data (an OBS scene table, "OBS connected",
  // "2 devices", iPad / iPhone rows, the address and the QR code) would be false. They are replaced by a plain statement; the toggles and values that are real parameters stay.
  function offlineStub(box, ctx, kind) {
    const stat = box.querySelector('.stat'), dot = stat && stat.querySelector('.dot'), disp = box.querySelector('.disp > div'); if (!stat || !disp) return null;
    const grey = d => { if (d) { d.style.background = '#55575c'; d.style.boxShadow = 'none'; } };
    const note = text => { const n = document.createElement('div'); n.className = 'leng'; n.style.cssText = 'padding:10px 12px;font-size:13px;letter-spacing:.06em;line-height:1.5;text-transform:none;white-space:normal;color:#9a9a9a'; n.textContent = text; return n; };
    grey(dot);
    if (kind === 'LV27') {
      stat.lastChild.textContent = 'OBS: not connected';
      [...disp.children].forEach((c, i) => { if (i > 0) c.style.display = 'none'; });
      disp.append(note('The link to OBS is not part of this version, so there are no scenes to show. Scene changes cannot reach the presets yet.'));
    } else {
      stat.lastChild.textContent = 'No server';
      const [qr, right] = [...disp.children]; if (qr) { qr.style.opacity = '0.07'; qr.title = 'Not available: this version has no tablet server'; }
      if (right) { [...right.children].forEach((c, i) => { if (i > 0) c.style.display = 'none'; }); const box1 = right.firstElementChild; if (box1 && box1.lastElementChild) { box1.lastElementChild.textContent = 'Not available yet'; } right.append(note('The tablet server is not part of this version: no address, no devices. Allow control, Require PIN and the default permission are saved for when it is.')); }
    }
    return null;
  }


  // ---- LV05: the second dashed line and its label ("-12 dB") are the ducking Depth: they move with the parameter (the first one is 0 dB, the background's own level)
  function depthLineDisplay(box, ctx, y) {
    const svg = svgOf(box); if (!svg) return null;
    const lines = [...svg.querySelectorAll(':scope > line[stroke-dasharray]')], lab = [...svg.querySelectorAll(':scope > text')].find(t => /^-?\d+ dB$/.test(t.textContent.trim()));
    if (lines.length < 2 || !lab) return null; let last = null;
    return { update() { const d = ctx.value('Depth'); if (!Number.isFinite(d) || d === last) return; last = d; const yy = y(d); lines[1].setAttribute('y1', yy.toFixed(1)); lines[1].setAttribute('y2', yy.toFixed(1)); lab.setAttribute('y', (yy - 6).toFixed(1)); lab.textContent = Math.round(d) + ' dB'; } };
  }


  // ---- CS04 modular strip: six module cards (click = choose the module whose five knobs are shown below, the dot = the module's On, drag a card = the order of the six, the Order parameter) and the EQ curve.
  // The design shows the EQ module only; the other modules' knobs use the same row (README: a design extension, the cores' parameters were not on screen). The knobs themselves are bound by
  // tools/gen_skins.py (cs04_bind: data-pb per module); this code draws the cards, the labels and the EQ curve (low shelf 100 Hz, bell at Mid freq Q 1, high shelf 10 kHz; as products/cs04).
  const CS04_MODS = [
    { n: 'Gate', lab: ['Thresh', 'Range', 'Release'], col: '#f0ad3d' }, { n: 'EQ', lab: ['Low', 'Mid freq', 'Mid', 'High', 'Output'], col: '#5f9bff' },
    { n: 'Comp', lab: ['Thresh', 'Ratio', 'Attack', 'Release', 'Makeup'], col: '#f0ad3d' }, { n: 'Saturate', lab: ['Drive', 'Mix'], col: '#ff8a5c' },
    { n: 'De-ess', lab: ['Freq', 'Thresh', 'Range'], col: '#f0ad3d' }, { n: 'Limit', lab: ['Ceiling', 'Release'], col: '#f0ad3d' }];
  function modularStripDisplay(box, ctx) {
    const discs = [...box.querySelectorAll('.disp')], strip = discs.find(d => d.querySelectorAll(':scope > div > div').length === 6), graph = discs.find(d => d.querySelector('svg'));
    if (!strip || !graph) return null;
    const holder = strip.firstElementChild, cards = [...holder.children], byName = {}; cards.forEach(c => { const l = c.querySelector('.lbl'); if (l) byName[l.textContent.trim()] = c; });
    if (CS04_MODS.some(m => !byName[m.n])) return null;
    const P = n => ctx.params.find(q => q.name === n), pOrder = P('Order'), on = CS04_MODS.map(m => P(m.n)), ctls = [...box.querySelectorAll('.ctl')].filter(c => c.querySelector('.dk')); if (!pOrder || on.some(x => !x) || ctls.length !== 5) return null;
    const svg = graph.querySelector('svg'), paths = [...svg.querySelectorAll(':scope > path')], dots = [...svg.querySelectorAll(':scope > circle')], nums = [...svg.querySelectorAll(':scope > text')].filter(t => t.getAttribute('text-anchor') === 'middle'), caption = [...svg.querySelectorAll(':scope > text')].find(t => t.getAttribute('text-anchor') === 'start');
    const note = document.createElement('div'); note.style.cssText = 'height:100%;display:none;align-items:center;justify-content:center;font:12px "Space Mono",monospace;color:#8a8c92;text-align:center;padding:12px'; graph.append(note);
    const W = 380, H = 150, Y0 = 75, PXDB = 6.25, N = 120, xOf = f => Math.log10(f / 20) / 3 * W;
    let seq = [0, 1, 2, 3, 4, 5], dragging = null, last = '';
    const orderOf = () => { const lab = (pOrder.p.labels || [])[Math.round(pOrder.c.norm(ctx.get(pOrder.i)) * ((pOrder.p.steps || []).length - 1))] || ''; const idx = lab.split(' > ').map(n => CS04_MODS.findIndex(m => m.n === n)); return idx.length === 6 && idx.every(i => i >= 0) ? idx : [0, 1, 2, 3, 4, 5]; };
    const toggleOn = k => { const p = on[k]; ctx.begin(p.i); ctx.set(p.i, ctx.get(p.i) > 0.5 ? p.p.steps[0] : p.p.steps[1]); ctx.end(p.i); };
    CS04_MODS.forEach((m, k) => {
      const c = byName[m.n], dot = c.querySelector('span:not(.lbl)'); c.style.cursor = 'grab'; c.style.touchAction = 'none'; c.title = 'Click: choose the module · drag: change the order · dot: On / Off';
      if (dot) { dot.style.cursor = 'pointer'; dot.style.padding = '4px'; dot.style.backgroundClip = 'content-box'; dot.addEventListener('pointerdown', e => e.stopPropagation()); dot.addEventListener('click', e => { e.stopPropagation(); toggleOn(k); }); }
      c.addEventListener('pointerdown', e => { if (e.target === dot) return; c.setPointerCapture(e.pointerId); dragging = { k, x0: e.clientX, moved: false }; });
      c.addEventListener('pointermove', e => {
        if (!dragging || dragging.k !== k) return; if (!dragging.moved && Math.abs(e.clientX - dragging.x0) < 6) return; dragging.moved = true; c.style.opacity = '.7';
        const r = holder.getBoundingClientRect(), pos = clamp(Math.floor((e.clientX - r.left) / r.width * 6), 0, 5), cur = seq.indexOf(k);
        if (pos !== cur) { seq.splice(cur, 1); seq.splice(pos, 0, k); seq.forEach((i, at) => { byName[CS04_MODS[i].n].style.order = at; }); }   // CSS order: moving the node in the DOM would drop the pointer capture
      });
      const end = () => {
        if (!dragging || dragging.k !== k) return; const d = dragging; dragging = null; c.style.opacity = '';
        if (!d.moved) { ctx.selectBand(k); return; }
        const label = seq.map(i => CS04_MODS[i].n).join(' > '), idx = (pOrder.p.labels || []).indexOf(label);
        if (idx >= 0) { ctx.begin(pOrder.i); ctx.set(pOrder.i, pOrder.p.steps[idx]); ctx.end(pOrder.i); }
      };
      c.addEventListener('pointerup', end); c.addEventListener('pointercancel', end);
    });
    return { update() {
      if (dragging && dragging.moved) return;
      const sel = Math.max(0, Math.min(5, ctx.band ? ctx.band() : 1)), ord = orderOf(), ons = on.map(p => ctx.get(p.i) > 0.5), g = n => ctx.value(n);
      const key = [ord.join(''), ons.join(''), sel, g('Low'), g('Mid freq'), g('Mid'), g('High'), g('EQ Output')].join('|'); if (key === last) return; last = key; seq = ord.slice();
      ord.forEach((i, at) => { byName[CS04_MODS[i].n].style.order = at; });
      CS04_MODS.forEach((m, k) => {
        const c = byName[m.n], dot = c.querySelector('span:not(.lbl)'), bar = c.lastElementChild;
        c.style.background = k === sel ? '#1f2a3d' : '#1a1b1e'; c.style.borderColor = k === sel ? '#5f9bff' : '#2a2c30';
        if (dot) dot.style.background = ons[k] ? m.col : '#3a3b3f'; if (bar) { bar.style.background = m.col; bar.style.opacity = ons[k] ? 1 : 0.25; }
      });
      const lab = CS04_MODS[sel].lab; ctls.forEach((c, i) => { const l = c.querySelector('.lbl'); if (l) l.textContent = lab[i] || ''; c.style.visibility = lab[i] ? '' : 'hidden'; c.style.pointerEvents = lab[i] ? '' : 'none'; });
      const isEq = sel === 1; svg.style.display = isEq ? '' : 'none'; note.style.display = isEq ? 'none' : 'flex'; note.textContent = CS04_MODS[sel].n.toUpperCase() + (ons[sel] ? '' : '  (off)');
      if (isEq) {
        const lo = g('Low'), mf = g('Mid freq'), md = g('Mid'), hi = g('High'), out = g('EQ Output') || 0; let d = '';
        for (let k = 0; k <= N; k++) { const f = 20 * Math.pow(1000, k / N), db = biquadMag('lowshelf', 100, lo, 0.70710678, f) + biquadMag('bell', mf, md, 1.0, f) + biquadMag('highshelf', 10000, hi, 0.70710678, f) + out; d += (k ? ' L' : 'M') + (k / N * W).toFixed(1) + ' ' + clamp(Y0 - db * PXDB, 2, H - 2).toFixed(1); }
        if (paths.length >= 3) { paths[0].setAttribute('d', d + ' L' + W + ' ' + Y0 + ' L0 ' + Y0 + ' Z'); paths[1].setAttribute('d', d); paths[2].setAttribute('d', d); }
        [[100, lo], [mf, md], [10000, hi]].forEach(([f, gdb], k) => { if (dots[k]) { const x = xOf(f), y = clamp(Y0 - (gdb + out) * PXDB, 6, H - 6); dots[k].setAttribute('cx', x.toFixed(1)); dots[k].setAttribute('cy', y.toFixed(1)); if (nums[k]) { nums[k].setAttribute('x', x.toFixed(1)); nums[k].setAttribute('y', (y + 3.5).toFixed(1)); } } });
        if (caption) caption.textContent = 'EQ module' + (ons[1] ? '' : ' (off)');
      }
    } };
  }


  // ---- LV15 auto mixer: the eight mic columns. The instances of the product in one process share their levels (README: this stands in for SW Link), so each one can show the gain every mic of the
  // group gets now (readouts: this instance's mic number, then for mics 1..8 the gain in dB (-999 = not in the group) and 1 / 0 = open). A mic that is not there shows a dash; the column of this instance is marked.
  function autoMixerDisplay(box, ctx) {
    const cols = [...box.querySelectorAll('.disp > div > div')].filter(c => c.querySelector('.rl') && c.querySelector('.rv')); if (cols.length !== 8) return null;
    const parts = cols.map(c => ({ fill: c.querySelector(':scope > div > div'), lab: c.querySelector('.rl'), val: c.querySelector('.rv') }));
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length < 17) return; const me = Math.round(r[0]);
      parts.forEach((p, i) => {
        const db = r[1 + i], used = db > -900, open = r[9 + i] > 0.5;
        p.fill.style.height = (used ? clamp(Math.pow(10, db / 20), 0, 1) * 100 : 0).toFixed(1) + '%'; p.fill.style.background = open ? '#f2f2f2' : '#5a5c60';
        p.val.textContent = used ? (Math.abs(db) < 0.5 ? '0 dB' : Math.round(db) + ' dB') : '—'; p.lab.textContent = 'Mic ' + (i + 1) + (me === i + 1 ? ' •' : ''); p.lab.style.opacity = used ? '' : '.4'; p.lab.title = me === i + 1 ? 'This instance' : '';
      });
    } };
  }


  // ---- LV03's small EQ curve: the strip's own filters (high-pass at HPF, low shelf 100 Hz, bell at Mid f with Q 1, high shelf 8 kHz; products/lv03) on 20 Hz - 20 kHz, +-12 dB
  function miniEqCurve(box, ctx) {
    const svg = box.querySelector('.disp svg'), path = svg && svg.querySelector(':scope > path'); if (!path) return null;
    const [, , W, H] = vbOf(svg), Y0 = H / 2, PXDB = (H / 2 - 3) / 12, N = 100; let last = '';
    return { update() {
      const hp = ctx.value('HPF'), lo = ctx.value('EQ Low'), mf = ctx.value('EQ Mid f'), md = ctx.value('EQ Mid'), hi = ctx.value('EQ High'); if (![hp, lo, mf, md, hi].every(Number.isFinite)) return;
      const key = [hp, lo, mf, md, hi].join('|'); if (key === last) return; last = key; let d = '';
      for (let k = 0; k <= N; k++) { const f = 20 * Math.pow(1000, k / N), db = biquadMag('lowcut', Math.max(20, hp), 0, 0.70710678, f) + biquadMag('lowshelf', 100, lo, 0.70710678, f) + biquadMag('bell', mf, md, 1.0, f) + biquadMag('highshelf', 8000, hi, 0.70710678, f); d += (k ? ' L' : 'M') + (k / N * W).toFixed(1) + ' ' + clamp(Y0 - db * PXDB, 2, H - 2).toFixed(1); }
      path.setAttribute('d', d);
    } };
  }


  // ---- ST03 phase align: what the three controls do to the track, shown on a 100 Hz sine over 30 ms: white = the track as it comes in, purple = after Delay (later), Phase (rotation, a constant
  // angle over the band) and Polarity. The design's dashed "Kick out before" curve (the other microphone, an example offset) is hidden: the plug-in does not know the reference's offset until Auto align has run.
  function phaseAlignDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const paths = [...svg.querySelectorAll(':scope > path')], texts = [...svg.querySelectorAll(':scope > text')]; if (paths.length < 3 || texts.length < 3) return null;
    const [inP, refP, outP] = paths, [, , W, H] = vbOf(svg), CY = H / 2, AMP = H * 0.36, F = 100, T = 0.03, N = 240; let last = '';
    refP.style.display = 'none'; texts[1].style.display = 'none'; texts[0].textContent = 'Track as it comes in (100 Hz sine, for illustration)'; texts[2].textContent = 'After Delay, Phase and Polarity'; texts[2].setAttribute('x', 330);
    return { update() {
      const dl = ctx.value('Delay') / 1000, ph = ctx.value('Phase') * Math.PI / 180, pol = ctx.value('Polarity') > 0.5 ? -1 : 1; if (![dl, ph, pol].every(Number.isFinite)) return;
      const key = [dl, ph, pol].join('|'); if (key === last) return; last = key; let a = '', b = '';
      for (let i = 0; i <= N; i++) { const t = i / N * T, x = (i / N * W).toFixed(1); a += (i ? ' L' : 'M') + x + ' ' + (CY - AMP * Math.sin(2 * Math.PI * F * t)).toFixed(1); b += (i ? ' L' : 'M') + x + ' ' + (CY - AMP * pol * Math.sin(2 * Math.PI * F * (t - dl) + ph)).toFixed(1); }
      inP.setAttribute('d', a); outP.setAttribute('d', b);
    } };
  }


  // ---- the one-click measurements (LV14 Measure, ST03 Auto align): the button starts collecting in the plug-in; when the plug-in says "ready" the screen asks it to analyse (the FFT runs on this thread, not
  // the audio thread), and the plug-in hands the result to the host as parameter values. readouts[cfg.state]: 0 idle, 1 collecting, 2 ready, 3 done, 4 failed.
  function measureFlow(box, ctx, cfg) {
    const btn = box.querySelector('button[data-call="' + cfg.call + '"]'); if (!btn) return null;
    const orig = btn.textContent; btn.title = cfg.hint; let asked = false, doneAt = 0, lastSt = -1;
    return { update(info) {
      const r = info && info.readouts; if (!r || r.length <= cfg.state) return; const st = Math.round(r[cfg.state]), now = Date.now();
      if (st !== lastSt) { lastSt = st; if (st === 3 || st === 4) doneAt = now; if (st !== 2) asked = false; }
      if (st === 1) btn.textContent = 'Collecting…';
      else if (st === 2) { btn.textContent = 'Analysing…'; if (!asked) { asked = true; ctx.call('analyse'); } }
      else if ((st === 3 || st === 4) && now - doneAt < 6000) btn.textContent = st === 3 ? 'Done' + (cfg.found !== undefined && r[cfg.found] > 0 ? ': ' + r[cfg.found].toFixed(2) + ' ms' : '') : 'No clear match';
      else btn.textContent = orig;
    } };
  }


  // ======== numbers the design printed as examples: shown only when the plug-in measures them, otherwise a dash ========
  // rules: [{ re: regex on the element's text, text: (info, ctx, m) => string | null (null keeps the text) }]; elements are the leaf nodes (html or svg text) of the design
  function textRules(box, ctx, rules) {
    const leaves = [...box.querySelectorAll('span, div, text, b')].filter(e => !e.children.length && e.textContent.trim());
    const hit = rules.map(r => ({ r, els: leaves.filter(e => r.re.test(e.textContent.trim())) })).filter(h => h.els.length);
    if (!hit.length) return null;
    return { update(info) { hit.forEach(h => h.els.forEach(e => { const t = h.r.text(info || {}, ctx, e.textContent.trim().match(h.r.re)); if (t !== null && t !== undefined && e.textContent !== t) e.textContent = t; })); } };
  }
  // ---- transient shaper (DY09): readouts = the Attack part, the Sustain part, the gain applied (dB). Grey = the measured input level (mirrored), orange = what Attack did (held peaks), white = what Sustain did; the last ~7 s
  function transientDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const grey = svg.querySelector(':scope > path'), rects = [...svg.querySelectorAll(':scope > rect')], txt = [...svg.querySelectorAll(':scope > text')];
    if (!grey || rects.length < 2 || txt.length < 2) return null;
    rects.forEach(r => r.remove());
    const [, , W, H] = vbOf(svg), C = H / 2, N = 122, dx = W / (N - 1), AMP = C - 6, FULL = 15;
    const atk = mkEl('path', { fill: '#f0ad3d', 'fill-opacity': 0.42 }), sus = mkEl('path', { fill: '#ffffff', 'fill-opacity': 0.14 });
    grey.after(sus, atk);
    const gain = mkEl('text', { x: W - 10, y: 16, 'text-anchor': 'end', 'font-family': 'Space Mono, monospace', 'font-size': 11, fill: '#e6e6e6' }); svg.append(gain);
    txt[1].setAttribute('x', 190);
    const lvl = Ring(N, -90), a = Ring(N, 0), s = Ring(N, 0); let last = 0;
    const sg = v => (v < -0.05 ? '−' : '+') + Math.abs(v).toFixed(1);
    const area = (arr, f) => { let top = '', bot = ''; for (let k = 0; k < N; k++) { const x = (k * dx).toFixed(1), h = f(arr[k]).toFixed(1); top += (k ? ' L' : 'M') + x + ' ' + (C - h); bot = ' L' + x + ' ' + (C + +h) + bot; } return top + bot + ' Z'; };
    return { update(info) {
      const r = info && info.readouts, m = info && info.meters; if (!r || r.length < 3) return;
      const now = Date.now(); if (now - last < 55) return; last = now;
      lvl.push(m ? peakDb(m) : -90); a.push(r[0]); s.push(r[1]);
      grey.setAttribute('d', area(lvl.a, v => clamp((v + 60) / 60, 0, 1) * AMP * 0.9));
      atk.setAttribute('d', area(a.a, v => clamp(Math.abs(v) / FULL, 0, 1) * AMP));
      sus.setAttribute('d', area(s.a, v => clamp(Math.abs(v) / FULL, 0, 1) * AMP * 0.7));
      const split = ctx.value('Mode') > 0.5, at = split ? ['Low', 'Mid', 'High'].map(b => ctx.value(b + ' Attack')) : [ctx.value('Attack')], su = split ? ['Low', 'Mid', 'High'].map(b => ctx.value(b + ' Sustain')) : [ctx.value('Sustain')];
      txt[0].textContent = 'Attack ' + (split ? at.map((v, i) => 'LMH'[i] + ' ' + sg(v || 0)).join('  ') : sg(at[0] || 0) + ' dB');
      txt[1].textContent = 'Sustain ' + (split ? su.map((v, i) => 'LMH'[i] + ' ' + sg(v || 0)).join('  ') : sg(su[0] || 0) + ' dB');
      gain.textContent = 'Gain ' + sg(r[2]) + ' dB';
    } };
  }
  // ---- declipper (RS05): readouts = runs restored (count), the ceiling in use (dBFS), the length of the window (ms), then 64 + 64 points of channel 0: what came in (grey) and what goes out (green, before Makeup).
  // Each point is the sample with the largest size in its bin; the dashed lines are the ceiling (the core's own value: Threshold, or the one Detect read from the samples)
  function declipDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const lines = [...svg.querySelectorAll(':scope > line')], paths = [...svg.querySelectorAll(':scope > path')];
    if (lines.length < 2 || paths.length < 2) return null;
    const [, , W, H] = vbOf(svg), C = H / 2, S = 70, M = 64, y = v => clamp(C - v * S, 3, H - 3);
    const info = mkEl('text', { x: W - 10, y: 14, 'text-anchor': 'end', 'font-family': 'Barlow Condensed, sans-serif', 'font-size': 11, fill: '#a4a6ac' }); svg.append(info);
    let last = 0;
    return { update(inf) {
      const r = inf && inf.readouts; if (!r || r.length < 3 + 2 * M) return;
      const now = Date.now(); if (now - last < 55) return; last = now;
      const cl = Math.pow(10, r[1] / 20); [[lines[0], cl], [lines[1], -cl]].forEach(([l, v]) => { l.setAttribute('y1', y(v).toFixed(1)); l.setAttribute('y2', y(v).toFixed(1)); });
      const d = o => { let s = ''; for (let k = 0; k < M; k++) s += (k ? ' L' : 'M') + (k / (M - 1) * W).toFixed(1) + ' ' + y(r[o + k]).toFixed(1); return s; };
      paths[0].setAttribute('d', d(3)); paths[1].setAttribute('d', d(3 + M));
      info.textContent = 'Runs restored ' + Math.round(r[0]) + '  ·  Ceiling ' + r[1].toFixed(1) + ' dBFS  ·  Last ' + r[2].toFixed(0) + ' ms';
    } };
  }
  // ---- grain delay (DL05): readouts = Time (s), frozen, then 4 grain slots x [on, how far back the grain reads (s), speed (direction x rate), place in its window, source span (s)].
  // The axis is the recording: right = now, left = 2 x Time back (a Reverse segment is read from up to 2 Time back); frozen: right = the moment of the freeze. Grey = the measured input level on that axis,
  // pills = the grains' read positions (the live ones solid, the last ~3 s as a fading trail), the arrow = its direction
  function grainDelayDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const pills = [...svg.querySelectorAll(':scope > rect')], grey = svg.querySelector(':scope > path'); if (!grey || pills.length < 4) return null;
    const fill = pills[0].getAttribute('fill') || '#9a8df0'; pills.forEach(p => p.remove());
    const [, , W, H] = vbOf(svg), C = H / 2, AMP = 60, LANES = 4, TRAIL = 40, tr = [];
    const g = mkEl('g', { fill }); svg.append(g);
    const label = (x, anchor) => { const t = mkEl('text', { x, y: 14, 'text-anchor': anchor, 'font-family': 'Barlow Condensed, sans-serif', 'font-size': 11, fill: '#8a8c92' }); svg.append(t); return t; };
    const left = label(10, 'start'), right = label(W - 10, 'end');
    const hist = []; let last = 0, tFreeze = 0;
    return { update(info) {
      const r = info && info.readouts, m = info && info.meters; if (!r || r.length < 22) return;
      const now = Date.now(); if (now - last < 60) return; last = now;
      const P = Math.max(0.05, r[0]), span = 2 * P, frozen = r[1] > 0.5, tEnd = frozen ? (tFreeze || (tFreeze = now)) : (tFreeze = 0, now);
      if (!frozen) { hist.push([now, m ? peakDb(m) : -90]); while (hist.length && now - hist[0][0] > 12000) hist.shift(); }
      const X = back => W - clamp(back / span, 0, 1) * W;
      let top = '', bot = ''; const pts = hist.filter(h => (tEnd - h[0]) / 1000 <= span && h[0] <= tEnd);
      pts.forEach((h, i) => { const x = X((tEnd - h[0]) / 1000).toFixed(1), a = clamp((h[1] + 60) / 60, 0, 1) * AMP; top = (i ? top + ' L' : 'M') + x + ' ' + (C - a).toFixed(1) + (i ? '' : ''); bot = ' L' + x + ' ' + (C + a).toFixed(1) + bot; });
      grey.setAttribute('d', pts.length > 1 ? top + bot + ' Z' : '');
      for (let i = 0; i < LANES; i++) {
        const o = 2 + i * 5; if (r[o] < 0.5) continue;
        const lane = 30 + i * 46; tr.push({ x: X(r[o + 1]), w: Math.max(8, r[o + 4] / span * W), y: lane, a: 0.15 + 0.7 * (0.5 - 0.5 * Math.cos(2 * Math.PI * r[o + 3])), dir: r[o + 2], live: now });
      }
      while (tr.length > TRAIL * 2) tr.shift();
      let s = '';
      for (const q of tr) {
        const age = (now - q.live) / 1000, live = age < 0.1, op = live ? q.a : 0.45 * q.a * Math.max(0, 1 - age / 3); if (op < 0.02) continue;
        const x0 = clamp(q.x - q.w / 2, 0, W - q.w), tip = q.dir < 0 ? x0 : x0 + q.w;
        s += '<rect x="' + x0.toFixed(1) + '" y="' + q.y + '" width="' + q.w.toFixed(1) + '" height="8" rx="4" fill-opacity="' + op.toFixed(2) + '"/>';
        if (live) s += '<path d="M' + tip.toFixed(1) + ' ' + (q.y + 4) + ' l' + (q.dir < 0 ? 7 : -7) + ' -7 l0 14 Z" fill-opacity="' + op.toFixed(2) + '"/>';
      }
      g.innerHTML = s; left.textContent = span.toFixed(span < 10 ? 1 : 0) + ' s back'; right.textContent = frozen ? 'frozen' : 'now';
    } };
  }
  // ---- granular (CR03): readouts = grains playing, chord, how many are listed, then 24 grains x [how far back it reads (s), speed (negative = reversed), place in its window, source span (s), pan].
  // x = how far back in the input (right = now, left = 2 s back), y = the grain's pitch (the speed in semitones, -24 .. +24; Harmony puts the pills on the chord tones), pill = a grain (width = the source it spans,
  // brightness = its window), the arrow = reversed; the last ~1.5 s fade out. Grey = the measured input level on the same axis
  function granularDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const pills = [...svg.querySelectorAll(':scope > rect')], grey = svg.querySelector(':scope > path'); if (!grey || pills.length < 4) return null;
    const fill = pills[0].getAttribute('fill') || '#3fd1a0'; pills.forEach(p => p.remove());
    const [, , W, H] = vbOf(svg), C = H / 2, AMP = 50, SPAN = 2, STEP = (H - 40) / 48, tr = [], g = mkEl('g', { fill }); svg.append(g);
    const label = (x, y, a, t) => { const e = mkEl('text', { x, y, 'text-anchor': a, 'font-family': 'Barlow Condensed, sans-serif', 'font-size': 11, fill: '#8a8c92' }); e.textContent = t; svg.append(e); return e; };
    label(10, 14, 'start', SPAN + ' s back'); const nowL = label(W - 10, 14, 'end', 'now'); label(10, C + 4, 'start', '0 st'); label(10, 34, 'start', '+24'); label(10, H - 10, 'start', '−24');
    const hist = []; let last = 0;
    return { update(info) {
      const r = info && info.readouts, m = info && info.meters; if (!r || r.length < 3 + 120) return;
      const now = Date.now(); if (now - last < 60) return; last = now;
      hist.push([now, m ? peakDb(m) : -90]); while (hist.length && now - hist[0][0] > SPAN * 1000) hist.shift();
      let top = '', bot = ''; hist.forEach((h, i) => { const x = (W - (now - h[0]) / 1000 / SPAN * W).toFixed(1), a = clamp((h[1] + 60) / 60, 0, 1) * AMP; top += (i ? ' L' : 'M') + x + ' ' + (C - a).toFixed(1); bot = ' L' + x + ' ' + (C + a).toFixed(1) + bot; });
      grey.setAttribute('d', hist.length > 1 ? top + bot + ' Z' : '');
      const n = Math.min(24, Math.round(r[2]));
      for (let i = 0; i < n; i++) {
        const o = 3 + i * 5, st = 12 * Math.log2(Math.max(1e-3, Math.abs(r[o + 1])));
        tr.push({ x: W - clamp(r[o] / SPAN, 0, 1) * W, w: Math.max(8, r[o + 3] / SPAN * W), y: C - clamp(st, -24, 24) * STEP, a: 0.2 + 0.7 * (0.5 - 0.5 * Math.cos(2 * Math.PI * r[o + 2])), rev: r[o + 1] < 0, t: now });
      }
      while (tr.length > 160) tr.shift();
      let s = '';
      for (const q of tr) {
        const age = (now - q.t) / 1000, live = age < 0.1, op = live ? q.a : 0.5 * q.a * Math.max(0, 1 - age / 1.5); if (op < 0.03) continue;
        const x0 = clamp(q.x - q.w / 2, 0, W - q.w);
        s += '<rect x="' + x0.toFixed(1) + '" y="' + (q.y - 4).toFixed(1) + '" width="' + q.w.toFixed(1) + '" height="8" rx="4" fill-opacity="' + op.toFixed(2) + '"/>';
        if (q.rev && live) s += '<path d="M' + x0.toFixed(1) + ' ' + q.y.toFixed(1) + ' l7 -6 l0 12 Z" fill-opacity="' + op.toFixed(2) + '"/>';
      }
      g.innerHTML = s; nowL.textContent = r[0] + ' grains';
    } };
  }
  // ---- reference loader (UT03): the window cannot hand a file path to the plug-in, so the page reads the file, decodes it (the web view knows WAV, AIFF, MP3, FLAC, AAC ... ; at 48 kHz),
  // writes it as a 16 bit stereo WAV and sends it in base64 pieces (call refbegin / refdata / refend); the core decodes that and plays it. readouts = match, input LUFS, reference 1 / 2 LUFS,
  // then per reference: length (s), loop start, loop end (s); loads that worked / failed. The drawing: A = the measured input level (last ~12 s), B / C = the overview of the loaded file, the loop region over it
  const REF_RATE = 48000, REF_MAX_S = 1200, REF_PIECE = 3 * 65536;
  // stereo 16 bit WAV bytes from two channels of float samples (clipped to -1 .. 1)
  function wav16(l, r, n, rate) {
    const out = new Uint8Array(44 + n * 4), dv = new DataView(out.buffer), w = (o, t) => { for (let i = 0; i < t.length; i++) out[o + i] = t.charCodeAt(i); };
    w(0, 'RIFF'); dv.setUint32(4, 36 + n * 4, true); w(8, 'WAVE'); w(12, 'fmt '); dv.setUint32(16, 16, true); dv.setUint16(20, 1, true); dv.setUint16(22, 2, true);
    dv.setUint32(24, rate, true); dv.setUint32(28, rate * 4, true); dv.setUint16(32, 4, true); dv.setUint16(34, 16, true); w(36, 'data'); dv.setUint32(40, n * 4, true);
    const q = x => Math.max(-32768, Math.min(32767, Math.round(x * 32767)));
    for (let i = 0, o = 44; i < n; i++, o += 4) { dv.setInt16(o, q(l[i]), true); dv.setInt16(o + 2, q(r[i]), true); }
    return out;
  }
  const b64 = u8 => { let t = ''; for (let i = 0; i < u8.length; i += 0x8000) t += String.fromCharCode.apply(null, u8.subarray(i, i + 0x8000)); return btoa(t); };
  const mmss = sec => { const m = Math.floor(sec / 60), s = Math.round(sec - m * 60); return m + ':' + String(s === 60 ? 59 : s).padStart(2, '0'); };
  // the overview of a decoded file: `bins` heights 0 .. 1 (RMS of the bin in dB, -50 .. 0)
  function overviewOf(l, r, n, bins) {
    const o = new Array(bins).fill(0);
    for (let b = 0; b < bins; b++) { const a = Math.floor(b * n / bins), e = Math.max(a + 1, Math.floor((b + 1) * n / bins)); let sum = 0; for (let i = a; i < e; i++) sum += 0.5 * (l[i] * l[i] + r[i] * r[i]); const db = 10 * Math.log10(sum / (e - a) + 1e-12); o[b] = clamp((db + 50) / 50, 0, 1); }
    return o;
  }
  // a file chosen in the window, decoded by the web view (it gives it at REF_RATE): { l, r, n, rate }. Refuses what is longer than maxSeconds
  async function decodeFile(file, maxSeconds) {
    if (file.size > 300 * 1024 * 1024) throw new Error('The file is too large (over 300 MB)');   // decoding holds the whole file and its samples in the window's memory
    const buf = await file.arrayBuffer(), OAC = window.OfflineAudioContext || window.webkitOfflineAudioContext;
    if (!OAC) throw new Error('This window cannot decode audio files');
    const ab = await new Promise((res, rej) => { const p = new OAC(2, 1, REF_RATE).decodeAudioData(buf, res, rej); if (p && p.catch) p.catch(rej); });
    if (ab.duration > maxSeconds) throw new Error('Too long (the limit is ' + Math.round(maxSeconds / 60) + ' minutes)');
    const l = ab.getChannelData(0);
    return { l, r: ab.numberOfChannels > 1 ? ab.getChannelData(1) : l, n: ab.length, rate: ab.sampleRate, channels: ab.numberOfChannels > 1 ? 2 : 1 };
  }
  // bytes as base64 pieces of REF_PIECE: ctx.call(name, piece) for each, with a short pause between them (the window stays responsive); progress(0 .. 1)
  async function sendPieces(ctx, name, bytes, progress) {
    for (let off = 0; off < bytes.length; off += REF_PIECE) {
      ctx.call(name, b64(bytes.subarray(off, Math.min(bytes.length, off + REF_PIECE))));
      progress(Math.min(1, (off + REF_PIECE) / bytes.length));
      await new Promise(res => setTimeout(res, 3));
    }
  }
  const pickFile = (input, accept, onFile) => { input.type = 'file'; input.accept = accept; input.style.display = 'none'; input.addEventListener('change', () => { const f = input.files && input.files[0]; input.value = ''; if (f) onFile(f); }); };
  function referenceDisplay(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const paths = [...svg.querySelectorAll(':scope > path')], loopRect = svg.querySelector(':scope > rect'), texts = [...svg.querySelectorAll(':scope > text')];
    const loopT = texts.find(t => /^Loop/.test(t.textContent)), labB = texts.find(t => /Reference/.test(t.textContent));
    if (paths.length < 2 || !loopRect || !loopT || !labB) return null;
    const [, , W] = vbOf(svg), CA = 64.8, CB = 180, AMP = 40, BINS = 121, LOOPS = ['Intro', 'Verse', 'Chorus', 'Custom'];
    const note = mkEl('text', { x: W / 2, y: CB + 4, 'text-anchor': 'middle', 'font-family': 'Barlow Condensed, sans-serif', 'font-size': 13, fill: '#9a9a9a' }); svg.append(note);
    const wrap = svg.parentElement; wrap.style.position = 'relative';
    const mkBtn = (right, onClick) => { const b = document.createElement('button'); b.style.cssText = 'position:absolute;top:116px;right:' + right + 'px;font:600 11px "Space Mono",monospace;color:#cfcfcf;background:#161617;border:1px solid #3a3a3d;padding:4px 8px;border-radius:3px;cursor:pointer;z-index:2'; b.addEventListener('click', onClick); wrap.append(b); return b; };
    const input = document.createElement('input'); wrap.append(input);
    const st = { ov: [null, null, null], name: ['', '', ''], busy: false, status: '', pending: null }, hist = Ring(120, -90); let last = 0, lastR = null;
    const selSlot = () => (ctx.value('Source') === 2 ? 2 : 1);
    const loadBtn = mkBtn(80, () => { if (!st.busy) input.click(); }), clearBtn = mkBtn(10, () => { const k = selSlot(); ctx.call('refclear', String(k)); st.ov[k] = null; st.name[k] = ''; st.status = ''; });
    async function load(slot, file) {
      st.busy = true; st.status = 'Decoding ' + file.name + ' …';
      try {
        const d = await decodeFile(file, REF_MAX_S), ov = overviewOf(d.l, d.r, d.n, BINS), bytes = wav16(d.l, d.r, d.n, d.rate);
        ctx.call('refbegin', String(slot));
        await sendPieces(ctx, 'refdata', bytes, f => { st.status = 'Sending ' + file.name + ' … ' + Math.round(100 * f) + ' %'; });
        const base = lastR || [];
        st.pending = { slot, ov, name: file.name, done0: base[10] || 0, failed0: base[11] || 0, t: Date.now() };
        st.status = 'Reading ' + file.name + ' …';
        ctx.call('refend', '');
      } catch (e) { st.busy = false; st.status = 'Could not load: ' + (e && e.message ? e.message : 'unknown file'); ctx.call('refabort', ''); }
    }
    pickFile(input, 'audio/*,.wav,.wave,.aif,.aiff,.mp3,.flac,.ogg,.m4a,.aac', f => { if (!st.busy) load(selSlot(), f); });
    const lane = (hs, c, amp) => { let top = '', bot = ''; for (let k = 0; k < hs.length; k++) { const x = (k / (hs.length - 1) * W).toFixed(1), a = hs[k] * amp; top += (k ? ' L' : 'M') + x + ' ' + (c - a).toFixed(1); bot = ' L' + x + ' ' + (c + a).toFixed(1) + bot; } return top + bot + ' Z'; };
    // Loop = Custom: drag across the lower lane to choose the region (seconds of the file; the core keeps it with setLoopRegion)
    let drag = null, curLen = 0;
    const xOf = e => { const b = svg.getBoundingClientRect(); return clamp((e.clientX - b.left) / b.width, 0, 1); };
    svg.style.touchAction = 'none';
    svg.addEventListener('pointerdown', e => { if (!(curLen > 0) || Math.round(ctx.value('Loop') || 0) !== 3) return; const b = svg.getBoundingClientRect(); if ((e.clientY - b.top) / b.height < 0.5) return; svg.setPointerCapture(e.pointerId); drag = { x0: xOf(e), x1: xOf(e) }; });
    svg.addEventListener('pointermove', e => { if (drag) drag.x1 = xOf(e); });
    const endDrag = () => { if (!drag) return; const a = Math.min(drag.x0, drag.x1) * curLen, b = Math.max(drag.x0, drag.x1) * curLen; drag = null; if (b - a > 0.05) ctx.call('looprange', a.toFixed(3) + ' ' + b.toFixed(3)); };
    svg.addEventListener('pointerup', endDrag); svg.addEventListener('pointercancel', () => { drag = null; });
    return { update(info) {
      const r = info && info.readouts, m = info && info.meters; if (!r || r.length < 12) return; lastR = r;
      const now = Date.now(), k = selSlot(), len = r[4 + 3 * (k - 1)], rs = r[5 + 3 * (k - 1)], re = r[6 + 3 * (k - 1)];
      if (st.pending) {
        if (r[10] > st.pending.done0) { const p = st.pending; st.ov[p.slot] = p.ov; st.name[p.slot] = p.name; st.pending = null; st.busy = false; st.status = ''; }
        else if (r[11] > st.pending.failed0) { st.pending = null; st.busy = false; st.status = 'The plug-in could not read this file'; }
        else if (now - st.pending.t > 120000) { st.pending = null; st.busy = false; st.status = 'No answer from the plug-in'; }
      }
      if (now - last >= 100) { last = now; hist.push(m ? peakDb(m) : -90); paths[0].setAttribute('d', lane(hist.a.map(v => clamp((v + 60) / 60, 0, 1)), CA, AMP)); }
      const loaded = len > 0;
      paths[1].setAttribute('d', loaded ? lane(st.ov[k] || new Array(BINS).fill(0.18), CB, AMP) : '');
      note.textContent = st.status || (loaded ? (st.ov[k] ? '' : 'Loaded (the file name is not known to this window)') : 'Load a reference file (WAV, AIFF, MP3, FLAC …)');
      labB.textContent = (k === 2 ? 'C  ' : 'B  ') + (st.name[k] || (k === 2 ? 'Reference 2' : 'Reference')) + (loaded ? '  ' + mmss(len) : '');
      loopRect.style.display = loopT.style.display = loaded ? '' : 'none';
      curLen = len;
      if (loaded && drag) { loopRect.setAttribute('x', (Math.min(drag.x0, drag.x1) * W).toFixed(1)); loopRect.setAttribute('width', Math.max(2, Math.abs(drag.x1 - drag.x0) * W).toFixed(1)); }
      else if (loaded) { loopRect.setAttribute('x', (rs / len * W).toFixed(1)); loopRect.setAttribute('width', Math.max(2, (re - rs) / len * W).toFixed(1)); loopT.setAttribute('x', ((rs + re) / 2 / len * W).toFixed(1)); loopT.textContent = 'Loop: ' + (LOOPS[Math.round(ctx.value('Loop') || 0)] || '').toLowerCase(); }
      loadBtn.textContent = 'Load ' + (k === 2 ? 'C' : 'B') + ' …'; loadBtn.disabled = st.busy; loadBtn.style.opacity = st.busy ? '.5' : '1';
      clearBtn.style.display = loaded ? '' : 'none'; clearBtn.textContent = 'Clear'; loadBtn.style.right = loaded ? '80px' : '10px';
    } };
  }
  // the loudness / true peak / range texts of MS06's top line: press one to start measuring again (call resetmeters)
  function meterReset(box, ctx) {
    const els = [...box.querySelectorAll('*')].filter(e => !e.children.length && /^(-?\d+(\.\d+)? LUFS|— LUFS|TP -?\d.*|TP —|LRA \d.*|LRA —)$/.test(e.textContent.trim()));
    els.forEach(e => { e.style.cursor = 'pointer'; e.title = 'Press to start measuring again'; e.addEventListener('click', () => ctx.call('resetmeters', '')); });
    return null;
  }

  const lufs = v => (v > -150 ? v.toFixed(1) : '—');
  const combine = (...ds) => { const l = ds.filter(Boolean); return l.length ? { update(i) { l.forEach(d => d.update && d.update(i)); }, destroy() { l.forEach(d => d.destroy && d.destroy()); } } : null; };

  const registry = {
    MS06: (box, ctx) => combine(compressorDisplay(box, ctx, { thr: 'Threshold', ratio: 'Ratio' }), textRules(box, ctx, [{ re: /^-?\d+(\.\d+)? LUFS$|^— LUFS$/, text: info => { const r = info && info.readouts; return r && r.length >= 4 ? (r[1] > -150 ? r[1].toFixed(1) + ' LUFS' : '— LUFS') : null; } },
      { re: /^TP -?\d|^TP —$/, text: info => { const r = info && info.readouts; return r && r.length >= 4 ? (r[2] > -150 ? 'TP ' + r[2].toFixed(1) : 'TP —') : null; } },
      { re: /^LRA \d|^LRA —$/, text: info => { const r = info && info.readouts; return r && r.length >= 4 ? (r[3] > 0 ? 'LRA ' + r[3].toFixed(1) : 'LRA —') : null; } }]), meterReset(box, ctx)),
    LV01: voiceStripDisplay,
    DY04: learnButton, CS02: learnButton, CS03: learnButton,
    DY09: transientDisplay,
    RS05: declipDisplay,
    DL05: grainDelayDisplay,
    CR03: granularDisplay,
    EQ08: (box, ctx) => combine(eqDisplay(box, ctx), analyzerBackdrop(box, ctx), textRules(box, ctx, [{ re: /^Latency [\d.]+ ms$/, text: info => info.latencyMs === undefined ? null : 'Latency ' + info.latencyMs.toFixed(1) + ' ms' }])),
    LV03: (box, ctx) => combine(liveStripDisplay(box, ctx, 'LV03'), miniEqCurve(box, ctx)),
    LV04: (box, ctx) => { let tot = -1, at = 0;   // the time of the last event comes from the clock once, when a new event shows up (the core's seconds do not advance while the host is stopped)
      return combine(liveStripDisplay(box, ctx, 'LV04'), textRules(box, ctx, [{ re: /GR -?[\d.]+ dB/, text: info => { const r = info && info.readouts; if (!r || r.length < 5) return null; if (r[2] < 0) return 'No limit events'; if (r[1] !== tot) { tot = r[1]; at = Date.now() - r[2] * 1000; } const d = new Date(at), p2 = v => String(v).padStart(2, '0'); return p2(d.getHours()) + ':' + p2(d.getMinutes()) + ':' + p2(d.getSeconds()) + '  GR ' + r[3].toFixed(1) + ' dB  ' + r[4].toFixed(1) + ' s'; } }])); },
    UT03: (box, ctx) => combine(referenceDisplay(box, ctx), ((box, ctx) => { const rb = [...box.querySelectorAll('.rbox')].map(b => [...b.querySelectorAll('span')]), mix = rb.find(x => /^Mix/.test(x[0].textContent)), ref = rb.find(x => /^Ref/.test(x[0].textContent)), matchV = [...box.querySelectorAll('.val')].find(e => /LU$/.test(e.textContent));
      return { update(info) { const r = info && info.readouts; if (!r || r.length < 4) return; if (mix) mix[1].textContent = lufs(r[1]) + ' LUFS'; if (ref) ref[1].textContent = lufs(Math.max(r[2], r[3])) + ' LUFS'; if (matchV) matchV.textContent = r[1] > -150 ? (r[0] >= 0 ? '+' : '') + r[0].toFixed(1) + ' LU' : '— LU'; } }; })(box, ctx)),
    LV19: (box, ctx) => combine(derivedReadout(box, ctx, v => v.toFixed(1) + ' frames'), textRules(box, ctx, [{ re: /^\d+(\.\d+)? ms late$/, text: info => info.readouts && info.readouts.length >= 5 ? (info.readouts[4] > 0.5 ? info.readouts[3].toFixed(0) + ' ms late' : 'in sync') : null }])),
    GT02: micPositionDisplay,
    ST05: speakerTriangleDisplay,
    SA08: crusherDisplay,
    SA06: saturatorDisplay,
    LV18: catcherDisplay,
    LV21: generatorDisplay,
    MS02: ceilingDisplay,
    LV17: grHistoryDisplay,
    LV10: xyPadDisplay, VO06: xyPadDisplay,
    RV01: reverbDisplay,
    LV14: (box, ctx) => combine(derivedReadout(box, ctx, v => v.toFixed(1) + ' m'), alignGraphDisplay(box, ctx), measureFlow(box, ctx, { call: 'measure', state: 1, found: 2, hint: 'Send the main system (for example pink noise) to the second (sidechain) input and the measurement microphone to the first, then press: 3 s of both are compared and the delay is set' })),
    CR05: tapeStopDisplay,
    MD02: (box, ctx) => lfoDisplay(box, ctx, 'sine'), MD04: (box, ctx) => lfoDisplay(box, ctx, 'shape'),
    LV22: polarityGauge, LV05: (box, ctx) => combine(gainTraceDisplay(box, ctx, { y: db => clamp(22 - db * 40 / 12, 14, 90) }), depthLineDisplay(box, ctx, db => clamp(22 - db * 40 / 12, 14, 90))), LV29: (box, ctx) => gainTraceDisplay(box, ctx, { y: db => clamp(30 - db * 40 / 24, 14, 80) }),
    MS05: riderDisplay, VO05: riderDisplay, GT03: tunerReadout,
    DY05: deesserDisplay,
    MT04: stereoScope, UT02: stereoScope, UT01: trackGain, ST01: (box, ctx) => { const a = stereoBandsDisplay(box, ctx, ['Low width', 'Lo mid width', 'Hi mid width', 'High width']), b = stereoScope(box, ctx); st01Chips(box, ctx); return { update(i) { if (a) a.update(i); if (b) b.update(i); } }; }, LV26: stereoScope,
    RS03: (box, ctx) => textRules(box, ctx, [{ re: /^Hum at \d+ Hz and \d+ harmonics$/, text: (info, ctx) => { const hz = info.readouts && info.readouts.length >= 1 && info.readouts[0] > 0 ? info.readouts[0] : null, b = ctx.value('Base') !== undefined ? ctx.value('Base') : ctx.value('Base Hz'), f = hz ? hz.toFixed(hz % 1 ? 1 : 0) : (b < 0.5 ? '50' : b < 1.5 ? '60' : 'auto'), n = ctx.value('Harmonics'); return 'Hum at ' + f + ' Hz' + (n > 1 ? ' and ' + (n - 1) + (n - 1 === 1 ? ' harmonic' : ' harmonics') : ' only'); } }]),
    RV08: (box, ctx) => textRules(box, ctx, [{ re: /^Threshold -?\d+ dB$/, text: (info, c) => { const t = c.value('Threshold'); return Number.isFinite(t) ? 'Threshold ' + Math.round(t * 6 - 60) + ' dBFS' : null; } }]),
    LV06: streamMasterDisplay, LV07: speechLevelerDisplay,
    CS04: modularStripDisplay,
    LV15: autoMixerDisplay,
    ST03: (box, ctx) => combine(phaseAlignDisplay(box, ctx), measureFlow(box, ctx, { call: 'autoalign', state: 0, hint: 'Put this plug-in on the earlier microphone and send the other microphone to the second (sidechain) input, then press: 4 s of both are compared and Delay, Phase and Polarity are set' })),
    LV27: (box, ctx) => offlineStub(box, ctx, 'LV27'), LV28: (box, ctx) => offlineStub(box, ctx, 'LV28'),
    MT01: loudnessDisplay, LV23: loudnessDisplay,
    MT02: (box, ctx) => combine(spectrumPath(box, ctx), compareReference(box, ctx)), MD06: spectrumPath, LV09: (box, ctx) => combine(spectrumPath(box, ctx), textRules(box, ctx, [{ re: /^Hum at \d+ Hz and \d+ harmonics$/, text: (info, ctx) => { const hz = info.readouts && info.readouts.length >= 1 && info.readouts[0] > 0 ? info.readouts[0] : null, b = ctx.value('Base') !== undefined ? ctx.value('Base') : ctx.value('Base Hz'), f = hz ? hz.toFixed(hz % 1 ? 1 : 0) : (b < 0.5 ? '50' : b < 1.5 ? '60' : 'auto'), n = ctx.value('Harmonics'); return 'Hum at ' + f + ' Hz' + (n > 1 ? ' and ' + (n - 1) + (n - 1 === 1 ? ' harmonic' : ' harmonics') : ' only'); } }])), LV08: spectrumPath, LV02: (box, ctx) => combine(spectrumPath(box, ctx), feedbackFiltersDisplay(box, ctx)), LO01: spectrumPath, SA05: spectrumPath,
    LV13: (box, ctx) => { const a = eqDisplay(box, ctx), b = spectrumBars(box, ctx); if (!a && !b) return null; return { update(i) { if (a) a.update(i); if (b) b.update(i); } }; },
    CR04: spectrumPath, RS01: spectrumPath,
    LV20: spectrumBars, RS07: spectrumCells,
    MT03: (box, ctx) => { const d = spectrumCells(box, ctx), btn = [...box.querySelectorAll('button')].find(b => /^Pause$/i.test(b.textContent.trim())); if (!d || !btn) return d; let paused = false; btn.title = 'Freezes the picture (the screen only)'; btn.addEventListener('click', () => { paused = !paused; btn.classList.toggle('on', paused); }); return { update(i) { if (!paused) d.update(i); } }; },
    RS04: (box, ctx) => combine(spectrumCells(box, ctx), textRules(box, ctx, [{ re: /^Repaired \d+ events$/, text: info => 'Repaired ' + (info.readouts && info.readouts.length >= 1 ? Math.round(info.readouts[0]) : '—') + ' events' }])),
    RV04: (box, ctx) => { const cu = {}; return combine(convolutionDisplay(box, ctx, cu), irLoader(box, ctx, cu)); },
    VO01: (box, ctx) => combine(pitchGraphDisplay(box, ctx), keyChips(box, ctx, 'VO01')),
    VO03: (box, ctx) => combine(harmonyGraphDisplay(box, ctx), keyChips(box, ctx, 'VO03'), chordLine(box, ctx)),
    VO08: breathDisplay,
    CR01: filterResponseDisplay,
    CR02: stutterGridDisplay,
    LV30: recorderDisplay,
    LO03: lowFocusDisplay,
    RV07: earlyRoomDisplay,
    RV06: (box, ctx) => decayDisplay(box, ctx, { decay: 'Decay' }),
    LV24: (box, ctx) => decayDisplay(box, ctx, { decay: 'Decay', pre: 'Pre-delay' }),
    RS06: (box, ctx) => decayDisplay(box, ctx, { decay: 'Tail length' }),
    DL04: (box, ctx) => delayTapsDisplay(box, ctx, 'taps'),
    LV25: (box, ctx) => delayTapsDisplay(box, ctx, 'fb'),
    LV16: gateDisplay,
    MS04: clipperDisplay,
    MS01: maximizerDisplay,
    DY10: (box, ctx) => multibandDisplay(box, ctx, 'xover'),
    DY11: (box, ctx) => multibandDisplay(box, ctx, 'centre'),
    MS03: (box, ctx) => multibandDisplay(box, ctx, 'xover'),
    LV12: faderBank,
    EQ02: (box, ctx) => { const e = eqDisplay(box, ctx); return combine(e, assistMarks(box, ctx, e), unmaskOverlay(box, ctx, e)); }, EQ07: (box, ctx) => combine(eqDisplay(box, ctx), learnButton(box, ctx)),
    MD05: (box, ctx) => rotaryDisplay(box, ctx),
    DL01: echoLcdDisplay,
    DL02: (box, ctx) => reelDisplay(box, ctx, null),
    SA01: (box, ctx) => reelDisplay(box, ctx, c => { const v = c.value('Speed ips'); return v ? v / 15 : 1; }),
    DY01: (box, ctx) => vuDisplay(box, ctx, 'gr'),
    DY02: (box, ctx) => vuDisplay(box, ctx, 'meter'),
    DY03: (box, ctx) => vuDisplay(box, ctx, 'gr3'),
    DY06: (box, ctx) => vuDisplay(box, ctx, 'gr'),
    MT05: (box, ctx) => vuDisplay(box, ctx, 'outLR'),
    DY08: (box, ctx) => compressorDisplay(box, ctx, { thr: 'Threshold', ratio: 'Ratio', knee: 'Knee', makeup: 'Makeup' }),
  };

  global.SWDISP = { sa06Shape, cr01Gain, wav16, b64, overviewOf, attach(code, box, ctx) { const f = registry[code]; try { return f ? f(box, ctx) : null; } catch (e) { return null; } }, compCurve };
  if (typeof module !== 'undefined') module.exports = global.SWDISP;
})(typeof window !== 'undefined' ? window : globalThis);
