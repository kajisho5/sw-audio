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

  const registry = {
    DY08: (box, ctx) => compressorDisplay(box, ctx, { thr: 'Threshold', ratio: 'Ratio', knee: 'Knee', makeup: 'Makeup' })
  };

  global.SWDISP = { attach(code, box, ctx) { const f = registry[code]; try { return f ? f(box, ctx) : null; } catch (e) { return null; } }, compCurve };
  if (typeof module !== 'undefined') module.exports = global.SWDISP;
})(typeof window !== 'undefined' ? window : globalThis);
