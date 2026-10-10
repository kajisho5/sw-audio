/* SWINGBY — PLAY: the presets (CONSTELLATIONS: categories, the factory list and the user's), the orbit view, the selected layer
   (ORBITS), the eight macros, the ribbon (play a note from the screen) and ARP ON. The layout is the design's (docs/design/in07 SW_Main). */
(function () {
  'use strict';
  const SW = window.SW, P = SW.P, el = SW.el, ui = SW.ui;
  const KIND = ['pearl', 'ring', 'crystal', 'crater'];          // Analog, Wavetable, FM, Sample
  const TYPE = ['VA', 'WT', 'FM', 'SMP'];
  const lid = (l, k) => 'in07.l' + (l + 1) + '.' + k;
  SW.layerKind = l => KIND[P.stepIndex(lid(l, 'osc.type'))] || 'pearl';
  SW.layerType = l => TYPE[P.stepIndex(lid(l, 'osc.type'))] || 'VA';
  // a layer's name from what it plays: the wave, the table, the FM ratio or the sample
  SW.layerName = l => {
    const t = P.stepIndex(lid(l, 'osc.type'));
    const n = t === 1 ? P.label(lid(l, 'wt.table')) + ' TABLE' : t === 2 ? 'FM ' + P.label(lid(l, 'fm.ratio')) : t === 3 ? P.label(lid(l, 'smp.id')) : P.label(lid(l, 'osc.wave'));
    return 'L' + (l + 1) + ' ' + String(n).toUpperCase();
  };
  SW.layerOn = l => P.get(lid(l, 'on')) > 0.5;
  SW.layerLvl = l => SW.clamp((P.get(lid(l, 'level')) + 60) / 66, 0, 1);
  SW.focus = 0;
  // the orbit view's values: the selected layer's sound (the macros move it as they move the sound), the LFO, the host
  SW.orbitProps = () => {
    const f = SW.focus, m = i => (P.get('in07.macro' + (i + 1)) - 50) / 50;
    const sync = P.stepIndex('in07.lfo1.sync');
    const layers = [0, 1, 2, 3].map(l => ({ name: SW.layerName(l), kind: SW.layerKind(l), lvl: SW.layerOn(l) ? SW.layerLvl(l) : 0.3, on: SW.layerOn(l) }));
    return {
      values: {
        cutoff: SW.clamp(P.get(lid(f, 'flt.cutoff')) * Math.pow(4, m(0)), 20, 20000), res: SW.clamp(P.get(lid(f, 'flt.res')) + 50 * m(1), 0, 100),
        unison: P.get(lid(f, 'osc.unison')), detune: SW.clamp(P.get(lid(f, 'osc.detune')) + 50 * m(5), 0, 100),
        lfoRate: P.get('in07.lfo1.rate'), lfoDepth: 20 + 0.8 * P.get('in07.lfo1.orbit'), drive: SW.clamp(P.get(lid(f, 'flt.drive')) + 50 * m(4), 0, 100),
        release: SW.clamp(P.get(lid(f, 'amp.r')) * Math.pow(4, m(3)), 10, 20000), bpm: SW.info.bpm || 120,
        spb: [2, 4, 6, 8][P.stepIndex('in07.arp.rate')], gateSpb: [2, 4, 8][P.stepIndex('in07.gate.rate')],
        satRate: P.get('in07.sat.rate'), satDepth: P.get('in07.sat.depth')
      },
      lfoText: sync > 0 ? 'LFO ' + P.label('in07.lfo1.sync') : 'LFO ' + Number(P.get('in07.lfo1.rate')).toFixed(2) + ' Hz',
      layers, focus: f, arpOn: P.get('in07.arp.on') > 0.5, gateOn: P.get('in07.gate.on') > 0.5,
      arpSteps: Array.from({ length: 16 }, (_, i) => i < P.get('in07.arp.steps') ? P.get('in07.arp.vel' + (i + 1)) / 100 : 0),
      gateSteps: Array.from({ length: 16 }, (_, i) => P.get('in07.gate.step' + (i + 1)) > 0.5 ? 1 : 0),
      noteKey: SW.info.note || 0, running: !!SW.info.playing || (SW.info.held > 0)
    };
  };
  // a sprite sheet's first frame as a thumbnail
  SW.thumb = (kind, size) => {
    const s = el('span', { 'aria-hidden': 'true', class: 'thumb', style: { display: 'block', width: size + 'px', height: size + 'px', flexShrink: '0', backgroundRepeat: 'no-repeat' } });
    const set = k => { kind = k; const u = SW.url[k]; s.style.backgroundImage = u ? 'url(' + u + ')' : ''; s.style.backgroundSize = (k === 'ring' ? size * 12 : size * 12) + 'px auto'; s.style.backgroundPosition = '0 0'; };
    set(kind);
    SW.on('image', n => { if (n === kind) set(kind); });
    s.setKind = set;
    return s;
  };

  let orbit = null;
  const build = root => {
    // ---- the preset title
    const title = el('span', { class: 'disp', style: { fontSize: '17px', letterSpacing: '.32em', color: 'var(--strong)', whiteSpace: 'nowrap', maxWidth: '520px', overflow: 'hidden', textOverflow: 'ellipsis' } });
    const sub = el('span', { style: { fontSize: '11px', letterSpacing: '.34em', color: 'var(--sub)' } });
    const arrow = (d, label, dir) => {
      const b = el('button', { type: 'button', 'aria-label': label, style: { width: '44px', height: '44px', padding: '0', border: 'none', background: 'none', color: 'var(--sub)' }, html: '<svg width="14" height="14" viewBox="0 0 14 14" aria-hidden="true"><path d="' + d + '" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>' });
      b.addEventListener('click', () => step(dir));
      return b;
    };
    root.appendChild(el('div', { style: { position: 'absolute', left: '320px', width: '640px', top: '58px', display: 'flex', flexDirection: 'column', alignItems: 'center', gap: '2px' } },
      el('div', { style: { display: 'flex', alignItems: 'center', gap: '14px' } }, arrow('M9 2.5 4.5 7 9 11.5', 'Previous preset', -1), title, arrow('M5 2.5 9.5 7 5 11.5', 'Next preset', 1)), sub));

    // ---- the orbit view
    const ov = el('div', { class: 'orbit', style: { left: '320px', top: '112px', width: '640px', height: '577px' } });
    root.appendChild(ov);
    orbit = new SW.OrbitView(ov, Object.assign({ view: 'system' }, SW.orbitProps()));

    // ---- presets (left)
    let cat = 'ALL';
    const chips = el('div', { role: 'radiogroup', 'aria-label': 'Category', style: { display: 'flex', flexWrap: 'wrap', gap: '2px' } });
    ['ALL'].concat(SW.CATS, ['USER']).forEach(c => {
      const b = el('button', { type: 'button', class: 'chipbtn', role: 'radio', text: c });
      b.addEventListener('click', () => { cat = c; drawList(); });
      chips.appendChild(b);
    });
    const list = el('div', { role: 'listbox', 'aria-label': 'Presets', style: { display: 'flex', flexDirection: 'column', gap: '2px', flex: '1', minHeight: '0', overflow: 'auto' } });
    const visible = () => {
      if (cat === 'USER') return SW.presets.user;
      const f = SW.presets.factory.filter(p => cat === 'ALL' || p.category === cat);
      return cat === 'ALL' ? f : f.concat(SW.presets.user.filter(p => p.category === cat));
    };
    const same = (a, b) => a.kind === b.kind && (a.kind === 'factory' ? a.index === b.index : a.kind === 'user' ? a.path === b.path : true);
    const drawList = () => {
      chips.querySelectorAll('button').forEach(b => b.setAttribute('aria-checked', b.textContent === cat ? 'true' : 'false'));
      list.innerHTML = '';
      const v = visible();
      if (!v.length) list.appendChild(el('p', { style: { margin: '8px 10px', fontSize: '13px', lineHeight: '1.6', color: 'var(--sub)' }, text: cat === 'USER' ? 'No saved presets yet. SAVE keeps the sound playing now.' : 'Nothing here.' }));
      v.forEach(p => {
        const on = same(p, SW.current);
        const row = el('button', { type: 'button', role: 'option', 'aria-selected': on ? 'true' : 'false', style: { minHeight: '40px', display: 'flex', alignItems: 'center', gap: '10px', padding: '0 10px', border: on ? '1px solid var(--line2)' : '1px solid transparent', borderRadius: '10px', textAlign: 'left', background: on ? 'var(--sel)' : 'none', color: on ? 'var(--strong)' : 'var(--text)' } },
          el('span', { style: { width: on ? '6px' : '3px', height: on ? '6px' : '3px', borderRadius: '50%', flexShrink: '0', background: on ? 'var(--cat)' : 'var(--sub)' } }),
          el('span', { style: { flex: '1', fontSize: '16px', fontWeight: on ? '600' : '400', letterSpacing: '.04em', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }, text: p.name }),
          el('span', { class: 'mono', style: { fontSize: '9px', color: 'var(--sub)' }, text: p.kind === 'user' ? (cat === 'USER' ? (p.category || '') : (p.category ? p.category + ' · ' : '') + 'USER') : p.category }));
        row.addEventListener('click', () => SW.loadPreset(p));
        list.appendChild(row);
        if (on) setTimeout(() => row.scrollIntoView({ block: 'nearest' }), 0);
      });
    };
    const step = dir => {
      const v = visible(); if (!v.length) return;
      const k = v.findIndex(p => same(p, SW.current));
      SW.loadPreset(v[((k < 0 ? (dir > 0 ? -1 : 0) : k) + dir + v.length) % v.length]);
    };
    const save = el('button', { type: 'button', class: 'btn', style: { width: '100%' } }, 'SAVE PRESET');
    save.addEventListener('click', () => SW.openSave());
    root.appendChild(el('aside', { class: 'panel', 'aria-label': 'Presets', style: { left: '24px', top: '112px', width: '248px', height: '560px', padding: '16px', display: 'flex', flexDirection: 'column', gap: '10px' } },
      el('span', { class: 'cap', text: 'CONSTELLATIONS' }), chips, list, save));
    const showTitle = () => {
      const c = SW.current;
      title.textContent = String(c.name || 'Init').toUpperCase();
      const no = c.kind === 'factory' ? String(c.index + 1).padStart(3, '0') : '';
      sub.textContent = c.kind === 'factory' ? (c.category + ' · FACTORY · ' + no) : c.kind === 'user' ? ((c.category ? c.category + ' · ' : '') + 'USER') : 'INIT';
    };
    SW.on('preset', () => { showTitle(); drawList(); });
    SW.on('presets', drawList);
    showTitle(); drawList();

    // ---- the selected layer (right)
    const lchips = el('div', { role: 'radiogroup', 'aria-label': 'Layer', style: { display: 'flex', flexDirection: 'column', gap: '4px' } });
    const rows = [0, 1, 2, 3].map(l => {
      const th = SW.thumb(SW.layerKind(l), 28), name = el('span', { style: { fontSize: '14px', letterSpacing: '.06em', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' } });
      const bar = el('span', { style: { position: 'absolute', left: '0', top: '0', height: '2px', background: 'var(--acc)' } }), type = el('span', { class: 'mono', style: { fontSize: '10px', color: 'var(--sub)' } });
      const b = el('button', { type: 'button', role: 'radio', style: { height: '44px', display: 'flex', alignItems: 'center', gap: '10px', padding: '0 8px', borderRadius: '10px', textAlign: 'left', border: '1px solid transparent', background: 'none' } },
        el('span', { style: { margin: '0 4px', display: 'block' } }, th),
        el('span', { style: { flex: '1', minWidth: '0', display: 'flex', flexDirection: 'column', gap: '1px' } }, name, el('span', { style: { position: 'relative', height: '2px', background: 'var(--track)' } }, bar)), type);
      b.addEventListener('click', () => { SW.focus = l; SW.emit('focus-set'); });   // every screen follows (LAYER's cards and its panel)
      b.addEventListener('dblclick', () => { SW.focus = l; SW.emit('focus-set'); SW.go('layer'); });
      lchips.appendChild(b);
      return { b, th, name, bar, type };
    });
    const cut = el('span', { class: 'disp', style: { fontSize: '30px', lineHeight: '36px', color: 'var(--strong)' } }), cutU = el('span', { style: { fontSize: '14px', color: 'var(--sub)', marginLeft: '6px', fontFamily: "'Barlow Condensed', sans-serif" } });
    const cutBar = el('span', { style: { position: 'absolute', left: '0', top: '0', height: '2px', background: 'var(--acc)' } });
    const grid = el('div', { style: { display: 'grid', gridTemplateColumns: 'repeat(2, minmax(0, 1fr))', gap: '10px' } });
    const cells = ['RES', 'DRIVE', 'UNISON', 'DETUNE'].map(k => { const v = el('span', { class: 'disp', style: { fontSize: '16px', color: 'var(--strong)' } }); grid.appendChild(el('div', { style: { display: 'flex', flexDirection: 'column', gap: '1px' } }, el('span', { class: 'cap2', text: k }), v)); return v; });
    const envPath = el('path', { fill: 'none', stroke: 'var(--acc)', 'stroke-width': '1.4', 'stroke-linejoin': 'round' });
    const envSvg = el('svg', { viewBox: '0 0 216 52', width: 216, height: 52, 'aria-hidden': 'true', style: { display: 'block' } }, el('path', { d: 'M2 50H214', stroke: 'var(--track)', 'stroke-width': '1' }), envPath);
    const envText = el('span', { class: 'mono', style: { fontSize: '10px', color: 'var(--sub)' } });
    const edit = el('button', { type: 'button', class: 'btn', style: { marginTop: 'auto', flexShrink: '0', width: '100%' }, html: 'EDIT LAYER<svg width="12" height="12" viewBox="0 0 12 12" aria-hidden="true"><path d="M4.5 2.5 8 6l-3.5 3.5" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linecap="round" stroke-linejoin="round"/></svg>' });
    edit.addEventListener('click', () => SW.go('layer'));
    root.appendChild(el('aside', { class: 'panel', 'aria-label': 'Selected orbit', style: { right: '24px', top: '112px', width: '248px', height: '560px', padding: '16px', display: 'flex', flexDirection: 'column', gap: '9px' } },
      el('span', { class: 'cap', text: 'ORBITS' }), lchips, el('div', { class: 'hline' }),
      el('div', { style: { display: 'flex', flexDirection: 'column', gap: '2px' } }, el('span', { class: 'cap2', text: 'CUTOFF', style: { letterSpacing: '.3em' } }), el('span', {}, cut, cutU), el('span', { style: { position: 'relative', height: '2px', marginTop: '6px', background: 'var(--track)' } }, cutBar)),
      grid, el('div', { style: { display: 'flex', flexDirection: 'column', gap: '4px' } }, el('span', { class: 'cap2', text: 'AMP ENVELOPE', style: { letterSpacing: '.3em' } }), envSvg, envText), edit));
    const unit = (txt) => { const m = /^(.*?)\s*([A-Za-z%]+)$/.exec(txt); return m ? [m[1], m[2]] : [txt, '']; };
    const showLayer = () => {
      const f = SW.focus;
      rows.forEach((r, l) => {
        const on = l === f;
        r.b.setAttribute('aria-checked', on ? 'true' : 'false');
        r.b.style.background = on ? 'var(--sel)' : 'none'; r.b.style.borderColor = on ? 'var(--line2)' : 'transparent';
        r.th.setKind(SW.layerKind(l)); r.th.style.opacity = SW.layerOn(l) ? '1' : '.35';
        r.name.textContent = SW.layerName(l).replace(/^L\d /, 'L' + (l + 1) + ' ');
        r.name.style.color = SW.layerOn(l) ? 'var(--text)' : 'var(--sub)';
        r.bar.style.width = (SW.layerOn(l) ? SW.layerLvl(l) * 100 : 0) + '%';
        r.type.textContent = SW.layerType(l);
      });
      const [cv, cu] = unit(P.text(lid(f, 'flt.cutoff'))); cut.textContent = cv; cutU.textContent = cu;
      cutBar.style.width = (P.norm(lid(f, 'flt.cutoff')) * 100) + '%';
      const vals = [P.text(lid(f, 'flt.res')), P.text(lid(f, 'flt.drive')), P.text(lid(f, 'osc.unison')) + ' v', P.text(lid(f, 'osc.detune'))];
      vals.forEach((t, k) => { const [a, b] = unit(t); cells[k].innerHTML = ''; cells[k].append(a, el('span', { style: { fontSize: '10px', color: 'var(--sub)', marginLeft: '3px', fontFamily: "'Barlow Condensed', sans-serif" }, text: b })); });
      const A = P.get(lid(f, 'amp.a')), D = P.get(lid(f, 'amp.d')), S = P.get(lid(f, 'amp.s')), R = P.get(lid(f, 'amp.r'));
      envPath.setAttribute('d', ui.envPath(A, D, S, R, 216, 52));
      envText.textContent = SW.fmt(A, 'ms') + ' · ' + SW.fmt(D, 'ms') + ' · ' + Math.round(S) + ' % · ' + SW.fmt(R, 'ms');
      orbit.set(SW.orbitProps());
      SW.emit('focus');
    };
    const showSoon = SW.throttle(showLayer);
    SW.on('param', showSoon);
    SW.on('focus-set', showLayer);
    SW.on('info', () => orbit.set({ values: Object.assign(orbit.props.values || {}, { bpm: SW.info.bpm || 120 }), noteKey: SW.info.note || 0, running: !!SW.info.playing || SW.info.held > 0 }));
    showLayer();

    // ---- macros
    const NAMES = ['BRIGHT', 'RESO', 'ATTACK', 'RELEASE', 'DRIVE', 'WIDTH', 'DELAY', 'REVERB'];
    // right click: MIDI learn (the next controller moves the macro; the plug-in keeps it with the session), or forget it
    const knobs = NAMES.map((n, i) => {
      const k = ui.knob('in07.macro' + (i + 1), { label: n });
      const cc = el('span', { class: 'cc mono' });
      k.appendChild(cc);
      k.addEventListener('contextmenu', e => {
        e.preventDefault();
        const learning = SW.info.learn === i, have = (SW.info.cc || [])[i] >= 0;
        ui.popup(e.clientX, e.clientY, [
          learning ? { label: 'CANCEL LEARN', act: () => SW.call('learn', -1) } : { label: 'MIDI LEARN', act: () => SW.call('learn', i) },
          ...(have ? [{ label: 'FORGET CC ' + SW.info.cc[i], act: () => SW.call('forget', i) }] : [])
        ]);
      });
      return { k, cc };
    });
    const showCc = () => knobs.forEach(({ k, cc }, i) => {
      const learning = SW.info.learn === i, n = (SW.info.cc || [])[i];
      cc.textContent = learning ? 'LEARN…' : (n >= 0 ? 'CC ' + n : '');
      k.classList.toggle('learning', learning);
    });
    SW.on('info', showCc); showCc();
    root.appendChild(el('section', { 'aria-label': 'Macros (right click: MIDI learn)', style: { position: 'absolute', left: '200px', width: '880px', top: '694px', display: 'grid', gridTemplateColumns: 'repeat(8, minmax(0, 1fr))' } },
      knobs.map(x => x.k)));

    // ---- the ribbon (C1 .. C6: press and slide to play) and ARP ON
    const ribbon = el('div', { role: 'slider', 'aria-label': 'Pitch ribbon: press to play a note', tabindex: '0', style: { position: 'relative', flex: '1', height: '44px', cursor: 'pointer', touchAction: 'none' } },
      el('span', { 'aria-hidden': 'true', style: { position: 'absolute', left: '0', right: '0', top: '21px', height: '1px', background: 'linear-gradient(90deg, rgba(0,0,0,0), var(--acc) 10%, var(--acc) 90%, rgba(0,0,0,0))', opacity: '.55' } }));
    for (let o = 0; o <= 5; o++) {
      ribbon.appendChild(el('span', { 'aria-hidden': 'true', style: { position: 'absolute', left: (o * 20) + '%', top: '14px', width: '1px', height: '15px', background: 'var(--track)' } }));
      ribbon.appendChild(el('span', { 'aria-hidden': 'true', class: 'mono', style: { position: 'absolute', left: (o * 20) + '%', top: '30px', marginLeft: '4px', fontSize: '9px', color: 'var(--sub)' }, text: 'C' + (o + 1) }));
    }
    const mark = el('span', { 'aria-hidden': 'true', style: { position: 'absolute', top: '15px', width: '12px', height: '12px', marginLeft: '-6px', borderRadius: '50%', display: 'none', boxShadow: '0 0 10px var(--cat)', background: 'var(--cat)' } });
    ribbon.appendChild(mark);
    let note = -1;
    const keyAt = e => { const r = ribbon.getBoundingClientRect(); return 24 + Math.round(SW.clamp((e.clientX - r.left) / r.width, 0, 1) * 60); };
    const play = k => { if (k === note) return; if (note >= 0) SW.noteOff(note); note = k; SW.noteOn(k, 0.8); mark.style.display = 'block'; mark.style.left = ((k - 24) / 60 * 100) + '%'; };
    const stop = () => { if (note >= 0) SW.noteOff(note); note = -1; mark.style.display = 'none'; };
    ribbon.addEventListener('pointerdown', e => { e.preventDefault(); ribbon.setPointerCapture(e.pointerId); play(keyAt(e)); });
    ribbon.addEventListener('pointermove', e => { if (note >= 0) play(keyAt(e)); });
    ribbon.addEventListener('pointerup', stop); ribbon.addEventListener('pointercancel', stop); ribbon.addEventListener('lostpointercapture', stop);
    window.addEventListener('blur', stop);
    ribbon.addEventListener('keydown', e => { if (e.key === ' ' || e.key === 'Enter') { e.preventDefault(); play(60); setTimeout(stop, 400); } });
    const arp = ui.onButton('in07.arp.on', { on: 'ARP ON', off: 'ARP OFF', style: { width: '112px' } });
    const morphBtn = ui.onButton('in07.morph.on', { on: 'MORPH ON', off: 'MORPH', style: { width: '112px' } });
    morphBtn.title = 'Swing by between presets: drag the probe among four planets (A: this sound; B, C, D: factory presets)';
    root.appendChild(el('div', { style: { position: 'absolute', left: '24px', right: '24px', bottom: '14px', height: '44px', display: 'flex', alignItems: 'center', gap: '14px' } },
      el('span', { style: { width: '70px', fontSize: '10px', letterSpacing: '.3em', color: 'var(--sub)' }, text: 'RIBBON' }), ribbon, morphBtn, arp));

    // ---- the morph: four planets at the corners of a square over the orbit view (A: the sound as it is; B, C, D: a factory preset each),
    // a probe that each planet pulls with the inverse square of its distance (the plug-in's own weights, products/in07 morphWeights)
    const F = { left: 440, top: 186, size: 400 };
    const field = el('div', { role: 'group', 'aria-label': 'Morph: drag the probe among the planets', style: { position: 'absolute', left: F.left + 'px', top: F.top + 'px', width: F.size + 'px', height: F.size + 'px', border: '1px dashed var(--line2)', borderRadius: '6px', cursor: 'crosshair', touchAction: 'none', display: 'none' } });
    const NS = 'http://www.w3.org/2000/svg';
    const lines = document.createElementNS(NS, 'svg');
    lines.setAttribute('width', F.size); lines.setAttribute('height', F.size); lines.setAttribute('aria-hidden', 'true');
    lines.style.cssText = 'position:absolute;left:0;top:0;overflow:visible;pointer-events:none';
    field.appendChild(lines);
    const CORNER = [[0, 0], [1, 0], [0, 1], [1, 1]], IDS = [null, 'in07.morph.b', 'in07.morph.c', 'in07.morph.d'];
    const planets = CORNER.map(([cx, cy], k) => {
      const dot = el('span', { 'aria-hidden': 'true', style: { position: 'absolute', left: (cx * F.size - 9) + 'px', top: (cy * F.size - 9) + 'px', width: '18px', height: '18px', borderRadius: '50%', border: '1px solid var(--line2)', background: 'radial-gradient(circle at 35% 30%, var(--strong), var(--acc) 45%, rgba(0,0,0,0) 72%)' } });
      const pct = el('span', { class: 'mono', 'aria-hidden': 'true', style: { position: 'absolute', left: (cx ? F.size + 16 : -66) + 'px', top: (cy * F.size - 7) + 'px', width: '50px', textAlign: cx ? 'left' : 'right', fontSize: '10px', color: 'var(--sub)' } });   // beside its planet, outside the square (the lines never cross it)
      field.appendChild(dot); field.appendChild(pct);
      return { dot, pct };
    });
    const nameA = el('span', { style: { fontSize: '13px', letterSpacing: '.08em', color: 'var(--strong)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis', maxWidth: '160px' } });
    const tagOf = t => el('span', { class: 'mono', style: { fontSize: '10px', color: 'var(--acc)', marginRight: '6px' }, text: t });
    const slotBox = (k, style) => el('div', { style: Object.assign({ position: 'absolute', display: 'flex', alignItems: 'center', width: '200px' }, style) }, tagOf(['A', 'B', 'C', 'D'][k]),
      k === 0 ? nameA : el('div', { style: { flex: '1' } }, ui.dropdown(IDS[k], { label: 'Morph planet ' + ['A', 'B', 'C', 'D'][k] })));
    const labels = el('div', { style: { position: 'absolute', left: (F.left - 30) + 'px', top: (F.top - 46) + 'px', width: (F.size + 60) + 'px', height: (F.size + 92) + 'px', pointerEvents: 'none', display: 'none' } },
      slotBox(0, { left: '0', top: '6px' }), slotBox(1, { right: '0', top: '0', pointerEvents: 'auto' }),
      slotBox(2, { left: '0', bottom: '0', pointerEvents: 'auto' }), slotBox(3, { right: '0', bottom: '0', pointerEvents: 'auto' }));
    const probe = el('span', { 'aria-hidden': 'true', style: { position: 'absolute', width: '16px', height: '16px', marginLeft: '-8px', marginTop: '-8px', borderRadius: '50%', background: 'var(--cat)', boxShadow: '0 0 14px var(--cat)', pointerEvents: 'none' } });
    field.appendChild(probe);
    root.appendChild(labels); root.appendChild(field);
    const weights = (x, y) => {   // as the plug-in: Shepard over the planets set; on a planet, that one alone
      const set = [true, P.stepIndex('in07.morph.b') > 0, P.stepIndex('in07.morph.c') > 0, P.stepIndex('in07.morph.d') > 0];
      const w = [0, 0, 0, 0]; let sum = 0;
      for (let k = 0; k < 4; k++) {
        if (!set[k]) continue;
        const d2 = (x - CORNER[k][0]) ** 2 + (y - CORNER[k][1]) ** 2;
        if (d2 < 1e-12) { return w.map((_, j) => (j === k ? 1 : 0)); }
        w[k] = 1 / d2; sum += w[k];
      }
      return w.map(v => v / sum);
    };
    const showMorph = () => {
      const on = P.get('in07.morph.on') > 0.5;
      field.style.display = on ? 'block' : 'none'; labels.style.display = on ? 'block' : 'none';
      if (!on) return;
      nameA.textContent = String(SW.current.name || 'Init').toUpperCase();
      const x = P.get('in07.morph.x') / 100, y = P.get('in07.morph.y') / 100, w = weights(x, y);
      probe.style.left = (x * F.size) + 'px'; probe.style.top = (y * F.size) + 'px';
      lines.innerHTML = '';
      CORNER.forEach(([cx, cy], k) => {
        const setK = k === 0 || P.stepIndex(IDS[k]) > 0;
        planets[k].dot.style.opacity = setK ? '1' : '.25';
        planets[k].pct.textContent = setK ? Math.round(w[k] * 100) + ' %' : '';
        if (!setK || w[k] <= 0) return;
        const ln = document.createElementNS(NS, 'line');
        ln.setAttribute('x1', x * F.size); ln.setAttribute('y1', y * F.size); ln.setAttribute('x2', cx * F.size); ln.setAttribute('y2', cy * F.size);
        ln.setAttribute('stroke', 'var(--acc)'); ln.setAttribute('stroke-width', (0.8 + 5 * w[k]).toFixed(2)); ln.setAttribute('stroke-opacity', (0.2 + 0.7 * w[k]).toFixed(2));
        ln.setAttribute('stroke-dasharray', '4 5'); ln.setAttribute('stroke-linecap', 'round');
        lines.appendChild(ln);
      });
    };
    const setFrom = e => {
      const r = field.getBoundingClientRect();
      P.set('in07.morph.x', Math.round(SW.clamp((e.clientX - r.left) / r.width, 0, 1) * 1000) / 10);
      P.set('in07.morph.y', Math.round(SW.clamp((e.clientY - r.top) / r.height, 0, 1) * 1000) / 10);
    };
    let dragging = false;
    field.addEventListener('pointerdown', e => {
      if (e.button !== 0) return;
      e.preventDefault(); field.setPointerCapture(e.pointerId); dragging = true;
      P.begin('in07.morph.x'); P.begin('in07.morph.y'); setFrom(e);
    });
    field.addEventListener('pointermove', e => { if (dragging) setFrom(e); });
    const release = () => { if (!dragging) return; dragging = false; P.end('in07.morph.x'); P.end('in07.morph.y'); };
    field.addEventListener('pointerup', release); field.addEventListener('pointercancel', release); field.addEventListener('lostpointercapture', release);
    field.addEventListener('dblclick', () => { P.tap('in07.morph.x', 0); P.tap('in07.morph.y', 0); });   // back to A
    ui.watch(['in07.morph.on', 'in07.morph.x', 'in07.morph.y', 'in07.morph.b', 'in07.morph.c', 'in07.morph.d'], showMorph);
    SW.on('preset', showMorph);
  };
  SW.screens.push({ id: 'play', label: 'PLAY', build, show: on => { if (orbit) { orbit.show(on); if (on) orbit.set(SW.orbitProps()); } } });
})();
