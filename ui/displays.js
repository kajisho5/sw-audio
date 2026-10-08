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

  const registry = {
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
