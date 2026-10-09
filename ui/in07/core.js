/* SWINGBY plug-in window — the core: the link to the plug-in, the parameter model, settings, the embedded pictures and fonts.
   The native side (plugin/clap/inst_gui.hpp, instrument_adapter.hpp) loads the page with SWBOOT = {params, values, presets, settings, licence, assets, version}
   and answers the page's text messages with scripts it composes:
     page -> native: "s <i> <plain>" value, "b <i>" / "e <i>" gesture, "n <key> <vel>" / "o <key>" a note from the screen, "p" poll,
                     "r" ready, "c <name> <arg...>" a call (factory, users, user, save, lic, licfile, set, asset; free text as base64)
     native -> page: SW.update(values, info), SW.asset(...), SW.reply(name, data)
   Without a native side (a browser preview) window.SWMOCK(message) takes the messages. */
(function () {
  'use strict';
  const BOOT = window.SWBOOT || {};
  const SW = window.SW = window.SW || {};
  const post = m => {
    try {
      if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.sw) window.webkit.messageHandlers.sw.postMessage(m);
      else if (window.chrome && window.chrome.webview) window.chrome.webview.postMessage(m);
      else if (window.SWMOCK) window.SWMOCK(m);
    } catch (e) { /* the host closed the window */ }
  };
  SW.post = post;
  SW.screens = SW.screens || [];   // the screens register here (screens/*.js); the frame (app.js) builds them
  SW.boot = BOOT;
  const b64 = s => btoa(unescape(encodeURIComponent(s)));   // UTF-8 text as base64 (calls carry text that may hold spaces)
  SW.b64 = b64;
  SW.unb64 = s => decodeURIComponent(escape(atob(s)));

  // ---- small events
  const handlers = {};
  SW.on = (name, cb) => { (handlers[name] = handlers[name] || []).push(cb); return () => { handlers[name] = handlers[name].filter(x => x !== cb); }; };
  SW.emit = (name, ...a) => (handlers[name] || []).slice().forEach(cb => { try { cb(...a); } catch (e) { console.error(e); } });

  // ---- the parameter model (plain values, as the plug-in's table)
  const specs = BOOT.params || [];
  const byId = {};
  specs.forEach((s, i) => { byId[s.id] = i; });
  let vals = (BOOT.values || specs.map(s => s.def)).slice();
  const pending = new Map();   // values the page set, not yet seen back from the plug-in (index -> time)
  const clamp = (x, a, b) => Math.max(a, Math.min(b, x));
  const P = SW.P = {
    count: specs.length,
    idx(id) { const i = typeof id === 'number' ? id : byId[id]; return i === undefined ? -1 : i; },
    spec(id) { return specs[P.idx(id)]; },
    get(id) { const i = P.idx(id); return i < 0 ? 0 : vals[i]; },
    has(id) { return P.idx(id) >= 0; },
    // the position 0..1 of a value on the control (the plug-in's curve; a stepped value is its step's place)
    norm(id, v) {
      const s = P.spec(id); if (!s) return 0;
      if (v === undefined) v = P.get(id);
      let n;
      switch (s.curve) {
        case 'log': n = Math.log(v / s.min) / Math.log(s.max / s.min); break;
        case 'skew': n = Math.pow(clamp((v - s.min) / (s.max - s.min), 0, 1), 1 / s.skew); break;
        case 'step': { const st = s.steps || []; let b = 0; st.forEach((x, k) => { if (Math.abs(x - v) < Math.abs(st[b] - v)) b = k; }); n = st.length > 1 ? b / (st.length - 1) : 0; break; }
        default: n = (v - s.min) / (s.max - s.min);
      }
      n = clamp(isFinite(n) ? n : 0, 0, 1);
      return s.rev && s.curve !== 'step' ? 1 - n : n;
    },
    fromNorm(id, n) {
      const s = P.spec(id); if (!s) return 0;
      n = clamp(n, 0, 1);
      if (s.rev && s.curve !== 'step') n = 1 - n;
      switch (s.curve) {
        case 'log': return s.min * Math.pow(s.max / s.min, n);
        case 'skew': return s.min + (s.max - s.min) * Math.pow(n, s.skew);
        case 'step': { const st = s.steps || [0]; return st[Math.round(n * (st.length - 1))]; }
        default: return s.min + n * (s.max - s.min);
      }
    },
    stepIndex(id, v) { const s = P.spec(id); if (!s || !s.steps) return 0; if (v === undefined) v = P.get(id); let b = 0; s.steps.forEach((x, k) => { if (Math.abs(x - v) < Math.abs(s.steps[b] - v)) b = k; }); return b; },
    label(id, v) { const s = P.spec(id); if (!s || !s.labels) return ''; return s.labels[P.stepIndex(id, v)] || ''; },
    // the page sets a value: kept here at once, sent to the plug-in, every view told
    set(id, v, quiet) {
      const i = P.idx(id); if (i < 0) return;
      const s = specs[i];
      if (s.curve === 'step') v = s.steps[P.stepIndex(i, v)]; else v = clamp(v, Math.min(s.min, s.max), Math.max(s.min, s.max));
      if (vals[i] === v) return;
      vals[i] = v; pending.set(i, Date.now());
      post('s ' + i + ' ' + v);
      if (!quiet) SW.emit('param', i, v, true);
    },
    begin(id) { const i = P.idx(id); if (i >= 0) post('b ' + i); },
    end(id) { const i = P.idx(id); if (i >= 0) post('e ' + i); },
    // a whole edit as one gesture (a click on a button)
    tap(id, v) { P.begin(id); P.set(id, v); P.end(id); },
    text(id, v) {
      const s = P.spec(id); if (!s) return '';
      if (v === undefined) v = P.get(id);
      if (s.curve === 'step') return P.label(id, v);
      if (s.minLabel && v <= s.min + 1e-9) return s.minLabel;
      if (s.maxLabel && P.norm(id, v) >= (s.maxLabelNorm || 1) - 1e-9) return s.maxLabel;
      return SW.fmt(v, s.unit);
    },
    values: () => vals
  };
  // numbers as the plug-in shows them: Hz / kHz, ms / s, dB with a sign, % without decimals, cents and semitones signed
  SW.fmt = (v, unit) => {
    const a = Math.abs(v);
    switch (unit) {
      case 'Hz': return a >= 1000 ? (v / 1000).toFixed(a >= 10000 ? 1 : 2) + ' kHz' : (a >= 100 ? v.toFixed(0) : a >= 10 ? v.toFixed(1) : v.toFixed(2)) + ' Hz';
      case 'ms': return a >= 1000 ? (v / 1000).toFixed(a >= 10000 ? 1 : 2) + ' s' : (a >= 100 ? v.toFixed(0) : a >= 10 ? v.toFixed(1) : v.toFixed(1)) + ' ms';
      case 's': return v.toFixed(a >= 10 ? 1 : 2) + ' s';
      case 'dB': return (v > 0.05 ? '+' : '') + v.toFixed(1) + ' dB';
      case '%': return Math.round(v) + ' %';
      case 'ct': return (v > 0 ? '+' : '') + Math.round(v) + ' ct';
      case 'st': return (v > 0 ? '+' : '') + Math.round(v) + ' st';
      case 'v': return Math.round(v) + ' v';
      default: return (Math.abs(v - Math.round(v)) < 1e-9 ? String(Math.round(v)) : v.toFixed(2)) + (unit ? ' ' + unit : '');
    }
  };

  // ---- the plug-in tells the page (every 50 ms while visible): its values, and how it plays
  SW.info = { bpm: 120, playing: false, beat: 0, demo: false, note: 0 };
  SW.update = (v, info) => {
    const now = Date.now();
    for (let i = 0; i < v.length && i < vals.length; i++) {
      const p = pending.get(i);
      if (p !== undefined) { if (vals[i] === v[i] || now - p > 400) pending.delete(i); else continue; }   // the page's own edit wins until it is seen back
      if (vals[i] !== v[i]) { vals[i] = v[i]; SW.emit('param', i, v[i], false); }
    }
    if (info) { Object.assign(SW.info, info); SW.info._at = performance.now(); SW.emit('info', SW.info); }
  };
  SW.reply = (name, data) => SW.emit('reply:' + name, data);
  SW.call = (name, ...args) => post(['c', name].concat(args).join(' '));
  SW.noteOn = (key, vel) => post('n ' + key + ' ' + (vel || 0.8));
  SW.noteOff = key => post('o ' + key);

  // ---- settings (kept by the plug-in in the user folder: the motion, the theme, the size)
  SW.settings = Object.assign({ motion: '60', theme: 'dark', zoom: '100' }, BOOT.settings || {});
  SW.setSetting = (k, v) => { SW.settings[k] = String(v); SW.call('set', k, SW.b64(String(v))); SW.emit('setting', k, String(v)); };
  if (!BOOT.settings || !BOOT.settings.motion) {   // the OS asks for less motion: start still
    try { if (window.matchMedia('(prefers-reduced-motion: reduce)').matches) SW.settings.motion = 'off'; } catch (e) { /* none */ }
  }

  // ---- pictures and fonts: sent one by one after the page is up (the page itself stays small)
  SW.img = {};      // name -> HTMLImageElement (decoded) or null
  SW.url = {};      // name -> object URL
  const want = (BOOT.assets || []).slice();
  let next = 0;
  const askNext = () => { if (next < want.length) SW.call('asset', next++); else SW.emit('assets'); };
  SW.asset = (index, name, mime, data) => {
    try {
      const bin = atob(data), bytes = new Uint8Array(bin.length);
      for (let k = 0; k < bin.length; k++) bytes[k] = bin.charCodeAt(k);
      if (/^font\//.test(mime)) {
        const m = /^(.*?)-(\d+)$/.exec(name), family = m ? m[1].replace(/_/g, ' ') : name, weight = m ? m[2] : '400';
        const f = new FontFace(family, bytes.buffer, { weight: weight });
        f.load().then(ff => { document.fonts.add(ff); SW.emit('font', family); }).catch(() => {});
      } else {
        const u = URL.createObjectURL(new Blob([bytes], { type: mime }));
        SW.url[name] = u;
        const im = new Image();
        im.onload = () => { SW.img[name] = im; SW.emit('image', name); };
        im.src = u;
      }
    } catch (e) { console.error(e); }
    askNext();
  };
  SW.startAssets = () => {
    if (BOOT.assetUrls) {   // a preview: straight from the files
      Object.keys(BOOT.assetUrls).forEach(name => {
        const u = BOOT.assetUrls[name];
        if (/\.woff2$/.test(u)) {
          const m = /^(.*?)-(\d+)$/.exec(name);
          const f = new FontFace(m ? m[1].replace(/_/g, ' ') : name, 'url(' + u + ')', { weight: m ? m[2] : '400' });
          f.load().then(ff => { document.fonts.add(ff); SW.emit('font'); }).catch(() => {});
        } else { SW.url[name] = u; const im = new Image(); im.onload = () => { SW.img[name] = im; SW.emit('image', name); }; im.src = u; }
      });
      return;
    }
    askNext();
  };

  SW.clamp = clamp;
  // fn at most once per display frame (many values can change in one update)
  SW.throttle = fn => { let q = false; return () => { if (q) return; q = true; requestAnimationFrame(() => { q = false; fn(); }); }; };
  SW.el = (tag, attrs, ...kids) => {
    const svg = /^(svg|path|circle|ellipse|g|line|rect|polyline|text|defs|radialGradient|stop)$/.test(tag);
    const e = svg ? document.createElementNS('http://www.w3.org/2000/svg', tag) : document.createElement(tag);
    if (attrs) for (const k in attrs) {
      const v = attrs[k];
      if (v === undefined || v === null || v === false) continue;
      if (k === 'class') e.setAttribute('class', v);
      else if (k === 'style' && typeof v === 'object') Object.assign(e.style, v);
      else if (k.startsWith('on') && typeof v === 'function') e.addEventListener(k.slice(2), v);
      else if (k === 'text') e.textContent = v;
      else if (k === 'html') e.innerHTML = v;
      else e.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false) e.appendChild(typeof c === 'string' ? document.createTextNode(c) : c);
    return e;
  };
  SW.toast = text => {
    const t = document.getElementById('swtoast'); if (!t) return;
    t.textContent = text; t.classList.add('show');
    clearTimeout(SW._toastT); SW._toastT = setTimeout(() => t.classList.remove('show'), 1800);
  };
})();
