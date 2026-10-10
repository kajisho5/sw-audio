/* SWINGBY — ARP: the arpeggiator (mode, rate, octaves, length, swing, the 16 steps: velocity bars and the pitch row, the pattern's
   length) and the trance gate (rate, 16 steps, depth); the orbit view with the rhythm rings (outer: the arp, a tick a step, its length
   the velocity; inner: the gate; the bright mark is the step playing now) and the host's tempo. The design's SW_Arp. */
(function () {
  'use strict';
  const SW = window.SW, P = SW.P, el = SW.el, ui = SW.ui;
  let orbit = null;
  const PITCH_CYCLE = [0, 12, 7, -12];   // a press on a pitch step moves it along this row

  const build = root => {
    const ov = el('div', { class: 'orbit', style: { left: '24px', top: '72px', width: '582px', height: '525px' } });
    root.appendChild(ov);
    orbit = new SW.OrbitView(ov, Object.assign(SW.orbitProps(), { view: 'arp' }));
    const bpm = el('span', { class: 'disp', style: { fontSize: '26px', color: 'var(--strong)' } });
    const host = el('span', { style: { fontSize: '13px', letterSpacing: '.2em', color: 'var(--sub)', marginLeft: '8px' } });
    root.appendChild(el('div', { style: { position: 'absolute', left: '40px', top: '640px', width: '560px', display: 'flex', flexDirection: 'column', gap: '8px' } },
      el('div', {}, bpm, host),
      el('p', { class: 'note', style: { margin: '0' }, text: 'Outer ring: the arpeggiator, one tick a step, length = velocity. Inner ring: the trance gate. The bright mark is the step playing now. Both follow the host\'s beat while it plays; stopped, they start with the first note.' })));
    const showBpm = () => { bpm.textContent = String(Math.round((SW.info.bpm || 120) * 10) / 10); host.textContent = 'BPM · ' + (SW.info.playing ? 'HOST PLAYING' : 'HOST'); };
    SW.on('info', showBpm); showBpm();

    // ---- the arpeggiator
    const row = (label, ctl, h) => el('div', { class: 'row2', style: { gridTemplateColumns: '90px minmax(0, 1fr)', minHeight: (h || 40) + 'px' } }, el('span', { class: 'k', text: label }), ctl);
    const velBars = el('div', { style: { display: 'grid', gridTemplateColumns: 'repeat(16, minmax(0, 1fr))', gap: '5px' } });
    const pitchRow = el('div', { style: { display: 'grid', gridTemplateColumns: 'repeat(16, minmax(0, 1fr))', gap: '5px' } });
    const numRow = el('div', { style: { display: 'grid', gridTemplateColumns: 'repeat(16, minmax(0, 1fr))', gap: '5px' } });
    const bars = [], pitches = [], nums = [];
    for (let i = 0; i < 16; i++) {
      const vid = 'in07.arp.vel' + (i + 1), pid = 'in07.arp.pitch' + (i + 1);
      const fill = el('span', { style: { position: 'absolute', left: '3px', right: '3px', bottom: '3px', borderRadius: '4px', background: 'var(--on)' } });
      const bar = el('div', { role: 'slider', tabindex: '0', 'aria-label': 'Step ' + (i + 1) + ' velocity', style: { position: 'relative', height: '70px', borderRadius: '6px', background: 'var(--groove)', boxShadow: 'var(--groove-shadow)', cursor: 'ns-resize', touchAction: 'none' } }, fill);
      // press and drag up / down: the velocity (a press at the bottom = a rest)
      const setFromY = e => { const r = bar.getBoundingClientRect(); const v = SW.clamp(Math.round((1 - (e.clientY - r.top) / r.height) * 100 / 5) * 5, 0, 100); P.set(vid, v); };
      let down = false, before = 0, beforeAt = 0;
      bar.addEventListener('pointerdown', e => {
        if (e.button !== 0) return;
        e.preventDefault(); bar.setPointerCapture(e.pointerId);
        if (performance.now() - beforeAt > 500) before = P.get(vid);   // the value before a double click's first press
        beforeAt = performance.now();
        down = true; P.begin(vid); setFromY(e);
      });
      bar.addEventListener('pointermove', e => { if (down && bar.hasPointerCapture(e.pointerId)) setFromY(e); });
      const up = () => { if (down) { down = false; P.end(vid); } };
      bar.addEventListener('pointerup', up); bar.addEventListener('pointercancel', up); bar.addEventListener('lostpointercapture', up);
      bar.addEventListener('dblclick', e => { if (e.button === 0) P.tap(vid, before > 0 ? 0 : 100); });
      bar.addEventListener('keydown', e => { const d = { ArrowUp: 5, ArrowRight: 5, ArrowDown: -5, ArrowLeft: -5 }[e.key]; if (d) { e.preventDefault(); P.tap(vid, SW.clamp(P.get(vid) + d, 0, 100)); } });
      velBars.appendChild(bar);
      const pb = el('button', { type: 'button', class: 'mono', 'aria-label': 'Step ' + (i + 1) + ' pitch', style: { height: '30px', padding: '0', borderRadius: '8px', border: '1px solid var(--chip-line)', background: 'var(--chip)', boxShadow: 'var(--chip-shadow)', fontSize: '10px', color: 'var(--label)' } });
      pb.addEventListener('click', () => { const k = PITCH_CYCLE.indexOf(Math.round(P.get(pid))); P.tap(pid, PITCH_CYCLE[(k + 1) % PITCH_CYCLE.length]); });
      pb.addEventListener('contextmenu', e => { e.preventDefault(); P.tap(pid, 0); });
      pitchRow.appendChild(pb);
      const n = el('span', { class: 'mono', style: { textAlign: 'center', fontSize: '10px', color: 'var(--sub)' }, text: String(i + 1) });
      numRow.appendChild(n);
      bars.push({ bar, fill, vid }); pitches.push({ pb, pid }); nums.push(n);
    }
    const showSteps = () => {
      const steps = Math.round(P.get('in07.arp.steps'));
      bars.forEach((b, i) => {
        const v = P.get(b.vid), act = i < steps;
        b.fill.style.height = 'calc(' + v + '% - ' + (v > 0 ? 6 * v / 100 : 0) + 'px)';
        b.fill.style.opacity = act ? (v > 0 ? '1' : '0') : '.25';
        b.bar.style.opacity = act ? '1' : '.4';
        b.bar.setAttribute('aria-valuetext', v > 0 ? v + ' %' : 'rest');
      });
      pitches.forEach((p, i) => {
        const v = Math.round(P.get(p.pid)), act = i < steps;
        p.pb.textContent = v > 0 ? '+' + v : String(v);
        p.pb.style.borderColor = v !== 0 ? 'var(--sel-line)' : 'var(--chip-line)';
        p.pb.style.color = v !== 0 ? 'var(--strong)' : 'var(--label)';
        p.pb.style.opacity = act ? '1' : '.4';
      });
      nums.forEach((n, i) => { n.style.color = i < steps ? 'var(--sub)' : 'var(--track)'; n.style.fontWeight = i % 4 === 0 ? '700' : '400'; });
    };
    ui.watch(['in07.arp.steps'].concat(bars.map(b => b.vid), pitches.map(p => p.pid)), showSteps);
    const arpPanel = el('div', { class: 'panel', style: { left: '630px', top: '72px', width: '626px', height: '476px', padding: '16px 20px', display: 'flex', flexDirection: 'column', gap: '4px' } },
      el('div', { class: 'sec', style: { height: '40px' } }, el('span', { class: 't', text: 'ARPEGGIATOR' }), ui.onButton('in07.arp.on', { style: { width: '96px', height: '32px' } })),
      row('MODE', ui.seg('in07.arp.mode', { labels: ['UP', 'DOWN', 'UP-DN', 'ORDER', 'RANDOM'] })),
      row('RATE', ui.seg('in07.arp.rate', { labels: ['1/8', '1/16', '1/16T', '1/32'] })),
      row('OCTAVES', ui.seg('in07.arp.octaves')),
      row('ALIGN', ui.seg('in07.arp.align', { labels: ['OFF', '2·3·4', '3·4·5', '3·5·7'], title: 'Planetary alignment: each key held on its own orbit (the highest every 2 or 3 steps, the next slower ...); all of them together every so many steps' })),
      ui.slider('in07.arp.length', { label: 'LENGTH', compact: true, labelWidth: 90, valWidth: 70 }),
      ui.slider('in07.arp.swing', { label: 'SWING', compact: true, labelWidth: 90, valWidth: 70 }),
      el('div', { class: 'sec', style: { marginTop: '6px' } }, el('span', { class: 't', text: 'VELOCITY · DRAG A STEP', style: { letterSpacing: '.2em' } }),
        el('div', { style: { display: 'flex', alignItems: 'center', gap: '8px', width: '170px' } }, el('span', { class: 'cap2', text: 'STEPS' }), el('div', { style: { flex: '1' } }, ui.dropdown('in07.arp.steps')))),
      velBars, pitchRow, numRow,
      el('p', { class: 'note', style: { margin: '2px 0 0', fontSize: '12px' }, text: 'Pitch row: 0, +12, +7, −12 semitones (press to step, right-click for 0). A step with no velocity is a rest.' }));
    root.appendChild(arpPanel);

    // ---- the trance gate
    const gateRow = el('div', { style: { display: 'grid', gridTemplateColumns: 'repeat(16, minmax(0, 1fr))', gap: '5px' } });
    const gates = [];
    for (let i = 0; i < 16; i++) {
      const id = 'in07.gate.step' + (i + 1);
      const g = el('button', { type: 'button', role: 'switch', 'aria-label': 'Gate step ' + (i + 1), style: { height: '44px', padding: '0', borderRadius: '6px', border: '1px solid var(--line2)' } });
      g.addEventListener('click', () => P.tap(id, P.get(id) > 0.5 ? 0 : 1));
      gateRow.appendChild(g); gates.push({ g, id });
    }
    ui.watch(gates.map(x => x.id), () => gates.forEach(({ g, id }) => { const on = P.get(id) > 0.5; g.setAttribute('aria-checked', on ? 'true' : 'false'); g.style.background = on ? 'linear-gradient(180deg, rgba(141,224,195,.55), rgba(34,168,124,.75))' : 'none'; }));
    root.appendChild(el('div', { class: 'panel', style: { left: '630px', top: '596px', width: '626px', height: '170px', padding: '16px 20px', display: 'flex', flexDirection: 'column', gap: '10px' } },
      el('div', { class: 'sec', style: { height: '36px' } }, el('span', { class: 't', text: 'TRANCE GATE' }),
        el('div', { style: { display: 'flex', alignItems: 'center', gap: '12px' } }, el('div', { style: { width: '140px' }, title: 'Eclipse: a run of closed steps is one passage of the moon, the sound dims and comes back smoothly' }, ui.seg('in07.gate.shape', { labels: ['HARD', 'ECLIPSE'] })),
          el('div', { style: { width: '150px' } }, ui.seg('in07.gate.rate')), ui.onButton('in07.gate.on', { style: { width: '84px', height: '32px' } }))),
      gateRow, ui.slider('in07.gate.depth', { label: 'DEPTH', compact: true, labelWidth: 90, valWidth: 70 })));

    const showOrbit = SW.throttle(() => orbit.set(Object.assign(SW.orbitProps(), { view: 'arp', arpOn: true, gateOn: P.get('in07.gate.on') > 0.5 })));
    SW.on('param', showOrbit);
    SW.on('info', showOrbit);
  };
  SW.screens.push({ id: 'arp', label: 'ARP', build, show: on => { if (orbit) { orbit.show(on); if (on) orbit.set(Object.assign(SW.orbitProps(), { view: 'arp', arpOn: true })); } } });
})();
