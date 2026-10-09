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
          needles.forEach((n, k) => { const target = vuAngle(Math.min(20, Math.max(0, peakDb(m) + (ctx.value('Makeup') || 0) - outDb(m))), GR_SCALE); cur[k] += (target - cur[k]) * (target < cur[k] ? 0.45 : 0.22); n.style.transform = 'rotate(' + cur[k].toFixed(1) + 'deg)'; });
          return;
        }
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
      const rd = info.readouts, g = rd ? Math.max(0, -(rd[1] + rd[2])) : Math.max(0, peakDb(m) + (ctx.get(gainI) || 0) - outDb(m)), gv = rd ? Math.min(30, g) : (peakDb(m) > -70 ? Math.min(30, g) : 0);   // the core's own gain reduction (slow + limiter) when it is sent
      shown += (gv - shown) * 0.4; if (grFill) grFill.style.height = (clamp(shown / 25, 0, 1) * 100).toFixed(1) + '%'; const rg = ro(grC); if (rg) rg.textContent = shown.toFixed(1);
      if (intE && info.readouts) intE.lastElementChild.textContent = info.readouts[0] > -150 ? info.readouts[0].toFixed(1) + ' LUFS' : '—';
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
      const sp = info && info.spectrum; if (!sp) return; const a = sm.feed(sp); let d = 'M0 ' + H;
      for (let b = 0; b < 64; b++) d += ' L' + ((b + 0.5) / 64 * W).toFixed(1) + ' ' + (H - specDb(a[b], -90) * (H - TOP)).toFixed(1);
      area.setAttribute('d', d + ' L' + W + ' ' + H + ' Z');
    } };
  }

  // 31 third-octave bars with peak holds (LV20)
  function spectrumBars(box, ctx) {
    const svg = svgOf(box); if (!svg) return null;
    const rs = [...svg.querySelectorAll(':scope > rect')], bars = rs.filter(r => +r.getAttribute('height') > 4), ticks = rs.filter(r => +r.getAttribute('height') <= 4);
    if (bars.length !== 31 || ticks.length !== 31) return null;
    bars.sort((a, b) => +a.getAttribute('x') - +b.getAttribute('x')); ticks.sort((a, b) => +a.getAttribute('x') - +b.getAttribute('x'));
    const base = +bars[0].getAttribute('y') + +bars[0].getAttribute('height'), full = Math.max(...bars.map(r => +r.getAttribute('height'))) * 1.05, th = +ticks[0].getAttribute('height');
    const sm = Smooth(), hold = new Array(31).fill(0);
    return { update(info) {
      const sp = info && info.spectrum; if (!sp) return; const a = sm.feed(sp);
      for (let k = 0; k < 31; k++) {                                          // third-octave k covers 20 * 10^((k-0.5)/10) .. 20 * 10^((k+0.5)/10); our band b spans 20 * 1000^(b/64)
        const f0 = 20 * Math.pow(10, (k - 0.5) / 10), f1 = 20 * Math.pow(10, (k + 0.5) / 10), b0 = Math.max(0, Math.floor(Math.log(f0 / 20) / Math.log(1000) * 64)), b1 = Math.min(63, Math.max(b0, Math.ceil(Math.log(f1 / 20) / Math.log(1000) * 64) - 1));
        let v = -120; for (let b = b0; b <= b1; b++) v = Math.max(v, a[b]);
        const x = specDb(v, -80), h = Math.max(1, x * full); bars[k].setAttribute('y', (base - h).toFixed(1)); bars[k].setAttribute('height', h.toFixed(1));
        hold[k] = Math.max(x, hold[k] - 0.012); ticks[k].setAttribute('y', (base - hold[k] * full - th).toFixed(1));
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
      const r = info && info.readouts; if (!r) return;
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

  const registry = {
    MT01: loudnessDisplay, LV23: loudnessDisplay,
    MT02: spectrumPath, MD06: spectrumPath, LV09: spectrumPath, LV08: spectrumPath, LV02: spectrumPath, LO01: spectrumPath, SA05: spectrumPath,
    LV20: spectrumBars, MT03: spectrumCells, RS04: spectrumCells, RS07: spectrumCells,
    RV06: (box, ctx) => decayDisplay(box, ctx, { decay: 'Decay' }),
    LV24: (box, ctx) => decayDisplay(box, ctx, { decay: 'Decay', pre: 'Pre-delay' }),
    RS06: (box, ctx) => decayDisplay(box, ctx, { decay: 'Tail length' }),
    DL04: (box, ctx) => delayTapsDisplay(box, ctx, 'taps'),
    LV25: (box, ctx) => delayTapsDisplay(box, ctx, 'fb'),
    LV16: gateDisplay,
    ST01: (box, ctx) => stereoBandsDisplay(box, ctx, ['Low width', 'Lo mid width', 'Hi mid width', 'High width']),
    MS04: clipperDisplay,
    MS01: maximizerDisplay,
    DY10: (box, ctx) => multibandDisplay(box, ctx, 'xover'),
    DY11: (box, ctx) => multibandDisplay(box, ctx, 'centre'),
    MS03: (box, ctx) => multibandDisplay(box, ctx, 'xover'),
    LV12: faderBank,
    EQ02: eqDisplay, EQ07: eqDisplay, EQ08: eqDisplay,
    MD05: (box, ctx) => rotaryDisplay(box, ctx),
    DL02: (box, ctx) => reelDisplay(box, ctx, null),
    SA01: (box, ctx) => reelDisplay(box, ctx, c => { const v = c.value('Speed ips'); return v ? v / 15 : 1; }),
    DY01: (box, ctx) => vuDisplay(box, ctx, 'gr'),
    DY02: (box, ctx) => vuDisplay(box, ctx, 'meter'),
    DY03: (box, ctx) => vuDisplay(box, ctx, 'gr3'),
    DY06: (box, ctx) => vuDisplay(box, ctx, 'gr'),
    MT05: (box, ctx) => vuDisplay(box, ctx, 'outLR'),
    DY08: (box, ctx) => compressorDisplay(box, ctx, { thr: 'Threshold', ratio: 'Ratio', knee: 'Knee', makeup: 'Makeup' })
  };

  global.SWDISP = { attach(code, box, ctx) { const f = registry[code]; try { return f ? f(box, ctx) : null; } catch (e) { return null; } }, compCurve };
  if (typeof module !== 'undefined') module.exports = global.SWDISP;
})(typeof window !== 'undefined' ? window : globalThis);
