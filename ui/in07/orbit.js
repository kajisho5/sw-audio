/* SWINGBY plug-in window — the orbit view (docs/design/in07/project/Orbit.dc.html), drawn on a canvas.
   The core in the middle is the sound; the four orbits are the layers (their worlds turn: the Blender sprite sheets); Cutoff opens the
   halo, Resonance rings it, Drive throws sparks, Unison puts moonlets round L1, Detune spreads them, the LFO moon runs its own orbit,
   Release lengthens the trails. Views: 'system', 'close' (one layer's world), 'arp' (the step rings), 'mod' (gravity lines).
   Motion: '60' every display frame, '30' half of them, 'off' a still picture (redrawn when a value changes). The layout is the
   design's 820 x 740, scaled to the element. */
(function () {
  'use strict';
  const SW = window.SW;
  const PI = Math.PI, CX = 410, CY = 360;
  // Blender sprite sheets: 96 frames of one turn, 12 columns (tools/blender/in07_orbital.py); k = drawn size / body radius
  const SHEETS = { ring: { n: 96, c: 12, k: 4.3 }, pearl: { n: 96, c: 12, k: 2.4 }, crater: { n: 96, c: 12, k: 2.4 }, crystal: { n: 96, c: 12, k: 2.4 },
    lfo: { n: 1, c: 1, k: 2.4 }, bead: { n: 1, c: 1, k: 2.4 } };
  const hash = n => { const x = Math.sin(n * 12.9898) * 43758.5453; return x - Math.floor(x); };
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));

  function frame(p, t, self) {
    const v = Object.assign({ cutoff: 2400, res: 30, unison: 1, detune: 22, lfoRate: 0.8, lfoDepth: 35, drive: 18, release: 420, bpm: 120 }, p.values || {});
    const layers = p.layers || [];
    const view = p.view || 'system', focus = p.focus === undefined || p.focus === null ? 0 : Number(p.focus);
    const light = p.theme === 'light';
    const pal = light
      ? { text: '#1b2622', orbit: '#178a64', trail: '#22a87c', label: '#0b5a41', dim: '#6b8a80', halo: '#22a87c', reso: '#0b5a41', spark: '#178a64', wave: '#178a64', tick: '#178a64', cur: '#0b5a41', glowOp: 0.10 }
      : { text: '#e8e8e8', orbit: '#3fd1a0', trail: '#8de0c3', label: '#8de0c3', dim: '#5c7a70', halo: '#3fd1a0', reso: '#8de0c3', spark: '#e8fff6', wave: '#bff3df', tick: '#3fd1a0', cur: '#e8fff6', glowOp: 0.14 };
    const c01 = clamp(Math.log(v.cutoff / 20) / Math.log(1000), 0, 1), res01 = clamp(v.res / 100, 0, 1), det01 = clamp(v.detune / 100, 0, 1);
    const dep01 = clamp(v.lfoDepth / 100, 0, 1), drv01 = clamp(v.drive / 100, 0, 1), rel01 = clamp(Math.log(v.release / 10) / Math.log(400), 0, 1);
    if (p.noteKey !== undefined && p.noteKey !== self._nk) { if (self._nk !== undefined) self._noteT = t; self._nk = p.noteKey; }
    const nT = self._noteT === undefined ? -10 : self._noteT, dn = t - nT;
    let env = 0;
    if (dn >= 0) env = dn < 0.02 ? dn / 0.02 : Math.exp(-(dn - 0.02) / Math.max(0.01, v.release / 1000 / 3));
    const back = [], front = [], backB = [], frontB = [], midB = [], labels = [];
    const put = (behind, d) => (behind ? back : front).push(d);
    const sprite = (list, x, y, r, kind, turns, op) => {
      const sh = SHEETS[kind] || SHEETS.pearl, sz = sh.k * r;
      const fr = sh.n > 1 ? Math.floor((((turns || 0) % 1) + 1) % 1 * sh.n) % sh.n : 0;
      list.push({ x: x - sz / 2, y: y - sz / 2, s: sz, kind, fr, op: op === undefined ? 1 : op, k: 1 });
    };
    const spins = [0.12, 0.09, 0.07, 0.16], phs = [0.35, 2.55, 1.35, 3.0];
    const dimAll = view === 'arp' ? 0.45 : (view === 'mod' ? 0.7 : 1);
    const close = view === 'close', fl = layers[clamp(focus, 0, Math.max(0, layers.length - 1))] || { name: '', kind: 'pearl', lvl: 0.5, on: true };
    const cR = close ? (fl.kind === 'ring' ? 52 : 64) : 64;
    let haloR = close ? cR * 1.5 + 120 * c01 : 80 + 130 * c01;
    const lfoA = close ? 250 : 104, lfoB = lfoA * (1 - 0.75 * dep01), lph = 2 * PI * Math.min(5, v.lfoRate * 0.5) * t, lfoVal = Math.sin(lph);
    haloR += 30 * dep01 * lfoVal;
    const lr = 24 * PI / 180, lx = lfoA * Math.cos(lph), ly = lfoB * Math.sin(lph);
    const lfoPt = [CX + lx * Math.cos(lr) - ly * Math.sin(lr), CY + lx * Math.sin(lr) + ly * Math.cos(lr)];
    const pulse = 0.5 + 0.5 * Math.sin(t * (2 + res01 * 12));
    const jit = drv01 * 4;
    const bodyPos = [];
    const L = [{ a: 300, b: 86, tilt: -9, w: 0.10 }, { a: 240, b: 68, tilt: -6, w: 0.14 }, { a: 185, b: 52, tilt: -3, w: 0.19 }, { a: 130, b: 37, tilt: 0, w: 0.26 }];
    const pt = (o, th) => { const x = o.a * Math.cos(th), y = o.b * Math.sin(th), r = o.tilt * PI / 180; return [CX + x * Math.cos(r) - y * Math.sin(r), CY + x * Math.sin(r) + y * Math.cos(r)]; };
    const orbits = [];
    const trailN = Math.round(3 + rel01 * 22);
    let l1 = [CX, CY], l1behind = false;
    if (!close) {
      layers.forEach((ly, i) => {
        if (i > 3) return;
        const o = L[i], th = phs[i] + o.w * 2 * PI * t, isF = i === focus, on = ly.on !== false;
        const dimL = (on ? 1 : 0.35) * dimAll * (focus >= 0 && !isF && view === 'system' ? 0.75 : 1);
        orbits.push({ a: o.a, b: o.b, tilt: o.tilt, op: (0.18 + 0.3 * ly.lvl + (isF ? 0.25 : 0)) * dimAll, w: isF ? 1.8 : 1.1 });
        if (on) for (let k = trailN; k >= 1; k--) {
          const tt = th - k * 0.035, q = pt(o, tt);
          put(Math.sin(tt) < 0, { x: q[0], y: q[1], r: 2.6 - 1.6 * k / trailN, fill: pal.trail, op: 0.6 * (1 - k / (trailN + 1)) * (0.4 + ly.lvl) * dimL });
        }
        const p0 = pt(o, th), behind = Math.sin(th) < 0;
        const x = p0[0] + jit * Math.sin(t * 37 + i * 1.7), y = p0[1] + jit * Math.cos(t * 29 + i * 2.3);
        const size = 10 + 7 * ly.lvl, reach = ly.kind === 'ring' ? size * 2.05 : size;
        put(behind, { x, y, r: size * 2.4, glow: true, op: pal.glowOp * 5 * dimL });
        sprite(behind ? backB : frontB, x, y, size, ly.kind, spins[i] * t + phs[i] / (2 * PI), (behind ? 0.85 : 1) * dimL);
        labels.push({ x: x + reach + 6, y: y - size - 4, text: ly.name, size: isF ? 11 : 10, ink: isF ? pal.cur : (behind ? pal.dim : pal.label), behind });
        bodyPos.push([x, y]);
        if (i === 0) { l1 = [x, y]; l1behind = behind; }
      });
    }
    const N = Math.round(clamp(v.unison, 1, 8)), mc = close ? [CX, CY] : l1;
    const ru = close ? cR * 1.9 + det01 * 40 : 14 + det01 * 26 + 14, flat = close ? 0.42 : 0.55;
    if (N > 1 || close) for (let u = 0; u < N; u++) {
      const spread = N > 1 ? (u - (N - 1) / 2) / ((N - 1) / 2) : 0;
      const ph = 2 * PI * u / N + t * (1.6 + det01 * 1.4 * spread) * (close ? 0.6 : 1);
      const mb = close ? Math.sin(ph) < 0 : l1behind;
      sprite(mb ? backB : frontB, mc[0] + ru * Math.cos(ph), mc[1] + ru * flat * Math.sin(ph), close ? 6 : 3.4, 'bead', 0, dimAll);
    }
    put(Math.sin(lph) < 0, { x: lfoPt[0], y: lfoPt[1], r: 16, glow: true, op: (light ? 0.6 : 0.9) * dimAll });
    sprite(Math.sin(lph) < 0 ? backB : frontB, lfoPt[0], lfoPt[1], close ? 8 : 6, 'lfo', 0, dimAll);
    labels.push({ x: lfoPt[0] + 12, y: lfoPt[1] + 17, text: p.lfoText || ('LFO ' + Number(v.lfoRate).toFixed(2) + ' Hz'), size: 10, ink: pal.label });
    if (close) {
      const fi = clamp(focus, 0, 3);
      sprite(midB, CX + jit * Math.sin(t * 37), CY + jit * Math.cos(t * 29), cR, fl.kind, spins[fi] * t, fl.on === false ? 0.4 : 1);
      midB[0].k = 1 + 0.08 * env;
      labels.push({ x: CX - 150, y: CY - haloR - 18, text: fl.name, size: 12, ink: pal.cur });
    }
    const sparks = [], sn = Math.round(drv01 * 26), life = 0.6, sr0 = close ? cR + 8 : 70;
    for (let s = 0; s < sn; s++) {
      const uu = t / life + hash(s * 13 + 1), gen = Math.floor(uu), age = uu - gen;
      const a = hash(gen * 31 + s * 7) * 2 * PI, rr = sr0 + (8 + hash(gen * 17 + s * 3) * 30) + age * 26;
      sparks.push({ x: CX + rr * Math.cos(a), y: CY + rr * Math.sin(a) * 0.8, r: (0.8 + hash(s * 11 + gen) * 1.4) * (1 - 0.5 * age), op: (0.3 + 0.7 * hash(s + gen * 5)) * Math.sin(PI * age) });
    }
    // the arp ring (16 steps: length = velocity) and the gate ring; the step playing now from the beat
    const steps = p.arpSteps || [];
    const beat = p.beat !== undefined ? p.beat : t * (v.bpm || 120) / 60;
    const stepNow = Math.floor(beat * (v.spb || 4)) % 16, gstep = Math.floor(beat * (v.gateSpb || 4)) % 16;
    const big = view === 'arp';
    const ticks = steps.map((st, i) => {
      const vv = st, a = -PI / 2 + i * 2 * PI / 16, r0 = 338 - 4, r1 = 338 + 4 + vv * (big ? 22 : 12);
      return { x0: CX + r0 * Math.cos(a), y0: CY + r0 * Math.sin(a), x1: CX + r1 * Math.cos(a), y1: CY + r1 * Math.sin(a),
        ink: i === stepNow && p.running ? pal.cur : pal.tick, op: vv === 0 ? 0.15 : (i === stepNow && p.running ? 1 : 0.55), w: big ? 3 : 2 };
    });
    const aa = -PI / 2 + stepNow * 2 * PI / 16;
    const gs = p.gateSteps || [];
    const arcs = gs.map((g, i) => ({ a0: -PI / 2 + (i + 0.08) * 2 * PI / 16, a1: -PI / 2 + (i + 0.92) * 2 * PI / 16, ink: i === gstep && p.running ? pal.cur : pal.tick, op: g ? (i === gstep && p.running ? 0.95 : 0.5) : 0.08 }));
    // modulation: gravity lines source -> target, bending toward the centre, flowing (negative: back, as dots)
    const lines = [], sources = [];
    if (view === 'mod') {
      const srcPts = { lfo1: lfoPt, lfo2: lfoPt, env: [CX, CY], key: [CX, CY + 300] }, row = ['vel', 'mw', 'at', 'm1', 'm2', 'm3', 'm4', 'm5', 'm6', 'm7', 'm8'];
      const names = { vel: 'VEL', mw: 'MW', at: 'AT', m1: 'M1', m2: 'M2', m3: 'M3', m4: 'M4', m5: 'M5', m6: 'M6', m7: 'M7', m8: 'M8' };
      const used = {};
      (p.mods || []).forEach(m => { if (m && m.on !== false) used[m.from] = true; });
      row.forEach((k, i) => {
        srcPts[k] = [110 + i * 60, 708];
        sources.push({ x: 110 + i * 60, y: 708, r: 6, label: names[k], ink: used[k] ? pal.cur : pal.dim, op: used[k] ? 0.95 : 0.5 });
      });
      const dstPts = { cutoff: [CX, CY - haloR], res: [CX + haloR * 0.9, CY], pitch: [CX, CY], drive: [CX, CY + 52], pan: [CX - 74, CY], level: [CX, CY + 30],
        lforate: lfoPt, L1: bodyPos[0], L2: bodyPos[1], L3: bodyPos[2], L4: bodyPos[3], osc: l1 };
      (p.mods || []).forEach((m, i) => {
        if (!m || m.on === false) return;
        const a = srcPts[m.from], b = dstPts[m.to];
        if (!a || !b) return;
        const amt = clamp(Number(m.amt) || 0, -1, 1), mx = (a[0] + b[0]) / 2, my = (a[1] + b[1]) / 2;
        lines.push({ ax: a[0], ay: a[1], qx: mx + (CX - mx) * 0.35, qy: my + (CY - my) * 0.35, bx: b[0], by: b[1], ink: pal.cur, op: 0.3 + 0.6 * Math.abs(amt),
          w: 1 + 2.4 * Math.abs(amt), dash: amt < 0 ? [2, 6] : [7, 7], off: -((t * 36 * (amt < 0 ? -1 : 1)) % 56) - i * 5 });
      });
    }
    const sz = 2.3 * 64, coreR = close ? 0 : 66 * (1 + 0.16 * env);
    labels.forEach(l => {
      const w = l.text.length * l.size * 0.62, nx = clamp(CX, l.x, l.x + w), ny = clamp(CY, l.y - l.size, l.y);
      l.op = l.behind && Math.hypot(nx - CX, ny - CY) < coreR ? 0 : 1;
    });
    return {
      pal, light, close, orbits, back, front, backB, frontB, midB, labels, sparks, lines, sources,
      arp: { on: !!p.arpOn && !close, ticks, x: CX + 338 * Math.cos(aa), y: CY + 338 * Math.sin(aa), dot: !!p.running },
      gate: { on: !!p.gateOn && !close, arcs },
      lfo: { a: lfoA, b: lfoB },
      halo: { r: Math.max(10, haloR), op: (0.35 + 0.65 * c01) * (light ? 0.8 : 1) },
      reso: { r: Math.max(8, haloR * 0.9), op: res01 * (0.25 + 0.75 * pulse), w: 0.8 + res01 * 1.8 },
      core: { on: !close, s: sz, k: 1 + 0.16 * env },
      wave: { on: dn >= 0 && dn < 1.5, r: (close ? cR + 4 : 66) + Math.max(0, dn) * 260, op: Math.max(0, 0.6 * (1 - dn / 1.5)) }
    };
  }

  const rgba = (hex, a) => { const n = parseInt(hex.slice(1), 16); return 'rgba(' + ((n >> 16) & 255) + ',' + ((n >> 8) & 255) + ',' + (n & 255) + ',' + a + ')'; };

  function paint(ctx, f) {
    const pal = f.pal;
    const circle = (x, y, r, fill, op) => { if (op <= 0.001) return; ctx.globalAlpha = Math.min(1, op); ctx.fillStyle = fill; ctx.beginPath(); ctx.arc(x, y, Math.max(0.1, r), 0, 2 * PI); ctx.fill(); };
    const glow = (x, y, r, op) => {
      if (op <= 0.001) return;
      const g = ctx.createRadialGradient(x, y, 0, x, y, r); g.addColorStop(0, rgba(pal.trail, 0.55)); g.addColorStop(1, rgba(pal.trail, 0));
      ctx.globalAlpha = Math.min(1, op); ctx.fillStyle = g; ctx.beginPath(); ctx.arc(x, y, r, 0, 2 * PI); ctx.fill();
    };
    const dot = d => d.glow ? glow(d.x, d.y, d.r, d.op) : circle(d.x, d.y, d.r, d.fill, d.op);
    const sprites = list => list.forEach(b => {
      const im = SW.img[b.kind]; if (!im || b.op <= 0.001) return;
      const sh = SHEETS[b.kind], fw = im.naturalWidth / sh.c, fh = sh.n > 1 ? im.naturalHeight / Math.ceil(sh.n / sh.c) : im.naturalHeight;
      const sx = (b.fr % sh.c) * fw, sy = Math.floor(b.fr / sh.c) * fh, s = b.s * b.k, x = b.x - (s - b.s) / 2, y = b.y - (s - b.s) / 2;
      ctx.globalAlpha = Math.min(1, b.op); ctx.drawImage(im, sx, sy, fw, fh, x, y, s, s);
    });
    ctx.lineCap = 'round';
    // the arp ring and the gate ring
    if (f.arp.on) {
      ctx.globalAlpha = 0.14; ctx.strokeStyle = pal.orbit; ctx.lineWidth = 1; ctx.beginPath(); ctx.arc(CX, CY, 338, 0, 2 * PI); ctx.stroke();
      f.arp.ticks.forEach(k => { ctx.globalAlpha = k.op; ctx.strokeStyle = k.ink; ctx.lineWidth = k.w; ctx.beginPath(); ctx.moveTo(k.x0, k.y0); ctx.lineTo(k.x1, k.y1); ctx.stroke(); });
      if (f.arp.dot) { glow(f.arp.x, f.arp.y, 16, 1); circle(f.arp.x, f.arp.y, 5, pal.spark, 1); }
    }
    if (f.gate.on) {
      ctx.lineCap = 'butt'; ctx.lineWidth = 6;
      f.gate.arcs.forEach(g => { ctx.globalAlpha = g.op; ctx.strokeStyle = g.ink; ctx.beginPath(); ctx.arc(CX, CY, 316, g.a0, g.a1); ctx.stroke(); });
      ctx.lineCap = 'round';
    }
    f.orbits.forEach(o => { ctx.globalAlpha = o.op; ctx.strokeStyle = pal.orbit; ctx.lineWidth = o.w; ctx.beginPath(); ctx.ellipse(CX, CY, o.a, o.b, o.tilt * PI / 180, 0, 2 * PI); ctx.stroke(); });
    ctx.globalAlpha = 0.32; ctx.strokeStyle = pal.trail; ctx.lineWidth = 1; ctx.setLineDash([3, 5]); ctx.beginPath(); ctx.ellipse(CX, CY, f.lfo.a, f.lfo.b, 24 * PI / 180, 0, 2 * PI); ctx.stroke(); ctx.setLineDash([]);
    f.back.forEach(dot);
    { // the halo (cutoff) and the resonance ring
      const g = ctx.createRadialGradient(CX, CY, 0, CX, CY, f.halo.r);
      g.addColorStop(0, rgba(pal.halo, 0.34)); g.addColorStop(0.5, rgba(pal.halo, 0.09)); g.addColorStop(1, rgba(pal.halo, 0));
      ctx.globalAlpha = f.halo.op; ctx.fillStyle = g; ctx.beginPath(); ctx.arc(CX, CY, f.halo.r, 0, 2 * PI); ctx.fill();
      if (f.reso.op > 0.001) { ctx.globalAlpha = f.reso.op; ctx.strokeStyle = pal.reso; ctx.lineWidth = f.reso.w; ctx.beginPath(); ctx.arc(CX, CY, f.reso.r, 0, 2 * PI); ctx.stroke(); }
    }
    f.sparks.forEach(s => circle(s.x, s.y, s.r, pal.spark, s.op));
    sprites(f.backB);
    if (f.core.on) {
      const im = SW.img[f.light ? 'daycore' : 'core'];
      if (im) { const s = f.core.s * f.core.k; ctx.globalAlpha = 1; ctx.drawImage(im, CX - s / 2, CY - s / 2, s, s); }
    }
    sprites(f.midB);
    if (f.wave.on && f.wave.op > 0.001) { ctx.globalAlpha = f.wave.op; ctx.strokeStyle = pal.wave; ctx.lineWidth = 1.5; ctx.beginPath(); ctx.arc(CX, CY, f.wave.r, 0, 2 * PI); ctx.stroke(); }
    f.front.forEach(dot);
    sprites(f.frontB);
    f.lines.forEach(g => {
      ctx.globalAlpha = g.op; ctx.strokeStyle = g.ink; ctx.lineWidth = g.w; ctx.setLineDash(g.dash); ctx.lineDashOffset = g.off;
      ctx.beginPath(); ctx.moveTo(g.ax, g.ay); ctx.quadraticCurveTo(g.qx, g.qy, g.bx, g.by); ctx.stroke();
      ctx.setLineDash([]); circle(g.bx, g.by, 3.5, g.ink, g.op);
    });
    f.sources.forEach(s => { ctx.globalAlpha = s.op; ctx.strokeStyle = s.ink; ctx.lineWidth = 1.2; ctx.beginPath(); ctx.arc(s.x, s.y, s.r, 0, 2 * PI); ctx.stroke(); });
    ctx.textBaseline = 'top';
    f.sources.forEach(s => { ctx.globalAlpha = s.op; ctx.fillStyle = s.ink; ctx.font = '9px "Space Mono", monospace'; ctx.textAlign = 'center'; ctx.fillText(s.label, s.x, s.y + 10); });
    ctx.textAlign = 'left';
    f.labels.forEach(l => { if (l.op <= 0) return; ctx.globalAlpha = 1; ctx.fillStyle = l.ink; ctx.font = l.size + 'px "Space Mono", monospace'; ctx.fillText(l.text, l.x, l.y - l.size); });
    ctx.globalAlpha = 1;
  }

  class OrbitView {
    constructor(host, props) {
      this.host = host;
      this.canvas = document.createElement('canvas');
      this.canvas.className = 'orbit-canvas';
      this.canvas.setAttribute('role', 'img');
      this.canvas.style.cssText = 'display:block;width:100%;height:100%';
      host.appendChild(this.canvas);
      this.ctx = this.canvas.getContext('2d');
      this.props = props || {};
      this.alive = true; this.visible = true; this.raf = 0; this.last = 0; this.t = 0;
      this._tick = ts => {
        this.raf = 0;
        if (!this.alive) return;
        const motion = SW.settings.motion;
        if (motion !== 'off' && this.visible && !document.hidden) {
          if (motion === '60' || ts - this.last >= 1000 / 30 - 4) { this.last = ts; this.t = ts / 1000; this.draw(); }
          this.raf = requestAnimationFrame(this._tick);
        }
      };
      SW.on('image', () => this.drawSoon());
      SW.on('setting', k => { if (k === 'motion') { if (SW.settings.motion === 'off') this.draw(); else this.run(); } });
      this.run();
    }
    set(props) { Object.assign(this.props, props); if (SW.settings.motion === 'off' || !this.raf) this.drawSoon(); }
    show(on) { this.visible = on; if (on) this.run(); }
    run() { if (!this.raf && this.alive) { if (SW.settings.motion === 'off') this.draw(); else this.raf = requestAnimationFrame(this._tick); } }
    drawSoon() { if (this._soon) return; this._soon = true; requestAnimationFrame(() => { this._soon = false; this.draw(); }); }
    draw() {
      if (!this.visible) return;
      const r = this.host.getBoundingClientRect(), dpr = Math.min(2, window.devicePixelRatio || 1);
      const w = Math.max(1, Math.round(this.host.clientWidth * dpr)), h = Math.max(1, Math.round(this.host.clientHeight * dpr));
      if (this.canvas.width !== w || this.canvas.height !== h) { this.canvas.width = w; this.canvas.height = h; }
      const sx = w / 820, sy = h / 740, s = Math.min(sx, sy);
      this.ctx.setTransform(1, 0, 0, 1, 0, 0); this.ctx.clearRect(0, 0, w, h);
      this.ctx.setTransform(s, 0, 0, s, (w - 820 * s) / 2, (h - 740 * s) / 2);
      const t = SW.settings.motion === 'off' ? 2.6 : this.t;
      const p = Object.assign({ theme: SW.settings.theme }, this.props);
      if (SW.info && SW.info.playing) p.beat = SW.info.beat + (performance.now() - (SW.info._at || performance.now())) / 1000 * (SW.info.bpm || 120) / 60;
      paint(this.ctx, frame(p, t, this));
      void r;
    }
  }
  SW.OrbitView = OrbitView;
})();
