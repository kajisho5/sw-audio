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
    if (paths.length < 2 || !dot || !hist) return null;
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
        paths[0].setAttribute('d', d); paths[1].setAttribute('d', d);
        const i = peakDb(m), o = outDb(m);
        dot.setAttribute('cx', px(i).toFixed(1)); dot.setAttribute('cy', py(o > -99 ? o : compCurve(i, thr, ratio, knee) + mk).toFixed(1));
        if (++tick % 2) return;                                          // the history moves at ~30 Hz
        lvl.push(i); gr.push(Math.max(0, i + mk - o));
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
  function vuAngle(vu) {
    if (vu <= VU_SCALE[0][0]) return VU_SCALE[0][1];
    for (let k = 1; k < VU_SCALE.length; k++) if (vu <= VU_SCALE[k][0]) { const a = VU_SCALE[k - 1], b = VU_SCALE[k]; return a[1] + (b[1] - a[1]) * (vu - a[0]) / (b[0] - a[0]); }
    return VU_SCALE[VU_SCALE.length - 1][1];
  }
  // modes: 'gr' (needle falls from 0 with gain reduction), 'out' (output level, 0 VU = -15 dBFS peak), 'outLR' (one needle per channel)
  function vuDisplay(box, ctx, mode) {
    const needles = [...box.querySelectorAll('svg line[transform^="rotate("]')].filter(l => l.getAttribute('x1') === '107' && l.getAttribute('y1') === '140');
    if (!needles.length) return null;
    needles.forEach(n => { n.removeAttribute('transform'); n.style.transformOrigin = '107px 140px'; n.style.transition = 'transform 70ms linear'; });
    const cur = needles.map(() => -50);
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
        needles.forEach((n, k) => {
          let vu;
          if (label === 'OFF') vu = -20;
          else if (label && label !== 'GR') vu = outDb(m) + REF[label];
          else if (label === 'GR') vu = -Math.max(0, peakDb(m) - outDb(m));
          else if (mode === 'outLR') vu = (m[2 + (k % 2)] > -99 ? m[2 + (k % 2)] : -100) + 15;
          else if (sel === 0 && mode !== 'out') vu = -Math.max(0, peakDb(m) - outDb(m));           // gain reduction
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
    if (bands.length < 2) return null;
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
      d.c.addEventListener('dblclick', e => { e.stopPropagation(); ctx.begin(b.on); ctx.set(b.on, 0); ctx.end(b.on); });
      d.c.addEventListener('wheel', e => { e.preventDefault(); const q = ctx.params.find(x => x.i === b.Q); ctx.begin(b.Q); ctx.set(b.Q, q.c.value(Math.min(1, Math.max(0, q.c.norm(ctx.get(b.Q)) - Math.sign(e.deltaY) * 0.03)))); ctx.end(b.Q); }, { passive: false });
    });
    svg.addEventListener('dblclick', e => {                               // a double click on an empty place turns on the next free band there
      const free = bands.find(b => ctx.get(b.on) < 0.5); if (!free) return; const [x, y] = pt(e);
      [free.on, free.Freq, free.Gain].forEach(i => ctx.begin(i)); ctx.set(free.on, 1); ctx.set(free.Freq, norm(free.Freq, xf(x))); ctx.set(free.Gain, norm(free.Gain, yg(y))); [free.on, free.Freq, free.Gain].forEach(i => ctx.end(i));
    });
    let last = null;
    return {
      update() {
        const act = bands.filter(b => ctx.get(b.on) > 0.5).map(b => ({ type: typeOf(b), f: ctx.get(b.Freq), g: ctx.get(b.Gain), q: ctx.get(b.Q) || 1, slope: b.Slope !== undefined ? ctx.get(b.Slope) : 12 }));
        const key = act.map(a => a.type + a.f + a.g + a.q + a.slope).join('|'); if (key === last) { return; } last = key;
        let d = '';
        for (let i = 0; i <= 200; i++) {
          const f = xf(X0 + (X1 - X0) * i / 200); let db = 0;
          act.forEach(a => { const stages = (a.type === 'lowcut' || a.type === 'highcut') ? Math.max(1, Math.round((a.slope || 12) / 12)) : 1; for (let s = 0; s < stages; s++) db += biquadMag(a.type, a.f, a.g, a.type === 'lowcut' || a.type === 'highcut' ? 0.707 : a.q, f); });
          d += (i ? ' L' : 'M') + (X0 + (X1 - X0) * i / 200).toFixed(1) + ' ' + gy(Math.max(-DB * 1.3, Math.min(DB * 1.3, db))).toFixed(1);
        }
        line.setAttribute('d', d); glow.setAttribute('d', d); area.setAttribute('d', d + ' L' + X1 + ' ' + YC + ' L' + X0 + ' ' + YC + ' Z');
        bands.forEach((b, k) => { const on = ctx.get(b.on) > 0.5, x = fx(Math.min(FMAX, Math.max(FMIN, ctx.get(b.Freq)))), y = gy(Math.max(-DB, Math.min(DB, ctx.get(b.Gain)))); const ds = dots[k];
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

  const registry = {
    LV12: faderBank,
    EQ02: eqDisplay, EQ07: eqDisplay, EQ08: eqDisplay,
    MD05: (box, ctx) => rotaryDisplay(box, ctx),
    DL02: (box, ctx) => reelDisplay(box, ctx, null),
    SA01: (box, ctx) => reelDisplay(box, ctx, c => { const v = c.value('Speed ips'); return v ? v / 15 : 1; }),
    DY01: (box, ctx) => vuDisplay(box, ctx, 'gr'),
    DY02: (box, ctx) => vuDisplay(box, ctx, 'meter'),
    DY06: (box, ctx) => vuDisplay(box, ctx, 'gr'),
    MT05: (box, ctx) => vuDisplay(box, ctx, 'outLR'),
    DY08: (box, ctx) => compressorDisplay(box, ctx, { thr: 'Threshold', ratio: 'Ratio', knee: 'Knee', makeup: 'Makeup' })
  };

  global.SWDISP = { attach(code, box, ctx) { const f = registry[code]; try { return f ? f(box, ctx) : null; } catch (e) { return null; } }, compCurve };
  if (typeof module !== 'undefined') module.exports = global.SWDISP;
})(typeof window !== 'undefined' ? window : globalThis);
