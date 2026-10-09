/* SWINGBY plug-in window — the frame: header (logo, the five screens, MOTION, theme, size, licence), the presets (factory and the
   user's), the save and licence dialogs, and the toast. The screens register themselves (SW.screens.push({id, label, build, show})). */
(function () {
  'use strict';
  const SW = window.SW, P = SW.P, el = SW.el, ui = SW.ui;
  SW.screens = SW.screens || [];
  const BOOT = SW.boot;
  const CATS = ['LEAD', 'PAD', 'BASS', 'PLUCK', 'KEYS', 'SEQ', 'FX'];
  SW.CATS = CATS;

  // ---- presets: the factory list from the plug-in, the user's from its folder (asked for when the window opens and after a save)
  const factory = (BOOT.presets || []).map((p, i) => ({ kind: 'factory', index: i, name: p.name, category: p.category }));
  SW.presets = { factory, user: [], folder: '' };
  SW.current = Object.assign({ kind: 'init', name: 'Init', category: '' }, BOOT.current || {});
  const sel = P.idx('in07.preset');
  const fromSelector = () => {
    const k = P.stepIndex(sel);
    if (k > 0 && factory[k - 1]) SW.current = { kind: 'factory', index: k - 1, name: factory[k - 1].name, category: factory[k - 1].category };
  };
  if (SW.current.kind !== 'user') fromSelector();
  SW.on('param', i => { if (i === sel && SW.current.kind !== 'user') { fromSelector(); SW.emit('preset'); } });
  SW.on('reply:users', d => { SW.presets.user = (d.list || []).map(u => ({ kind: 'user', path: u.path, name: u.name, category: u.category || '', author: u.author || '' })); SW.presets.folder = d.folder || ''; SW.emit('presets'); });
  SW.on('reply:loaded', d => {
    if (d.error) { SW.toast(d.error); return; }
    SW.current = d.kind === 'user' ? { kind: 'user', path: d.path, name: d.name, category: d.category || '' } : (d.kind === 'factory' && factory[d.index] ? { kind: 'factory', index: d.index, name: factory[d.index].name, category: factory[d.index].category } : { kind: 'init', name: 'Init', category: '' });
    SW.emit('preset');
  });
  SW.loadPreset = p => {
    if (p.kind === 'factory') SW.call('factory', p.index);
    else if (p.kind === 'user') SW.call('user', SW.b64(p.path));
    else SW.call('factory', -1);
    SW.current = Object.assign({}, p);   // shown at once; the reply confirms (or reports an error)
    SW.emit('preset');
  };

  // ---- the frame
  const app = document.getElementById('sw');
  const applyTheme = () => {
    const light = SW.settings.theme === 'light';
    app.classList.toggle('sw-light', light); app.classList.toggle('sw-dark', !light);
    document.body.classList.toggle('sw-light', light);
    logo.src = SW.url[light ? 'logo-light' : 'logo-dark'] || '';
  };
  const applyZoom = () => {
    const z = (parseFloat(SW.settings.zoom) || 100) / 100;
    app.style.transform = z === 1 ? '' : 'scale(' + z + ')';
  };
  app.appendChild(el('div', { class: 'sw-stars', 'aria-hidden': 'true' }));
  const logo = el('img', { class: 'logo', alt: 'SWINGBY' });
  SW.on('image', n => { if (/^logo-/.test(n)) applyTheme(); });
  const brand = el('div', { class: 'sw-brand' },
    el('svg', { viewBox: '58 8 140 240', width: 11, height: 19, 'aria-hidden': 'true', style: { display: 'block', color: 'var(--cat)' } },
      el('path', { fill: 'currentColor', 'fill-rule': 'evenodd', d: 'M82 8H174A24 24 0 0 1 198 32V224A24 24 0 0 1 174 248H82A24 24 0 0 1 58 224V32A24 24 0 0 1 82 8ZM110 34H146A18 18 0 0 1 146 70H110A18 18 0 0 1 110 34ZM110 186H146A18 18 0 0 1 146 222H110A18 18 0 0 1 110 186Z' })),
    el('span', { class: 'badge', text: 'IN07' }), logo);
  const tabs = el('nav', { class: 'sw-tabs', role: 'tablist', 'aria-label': 'Screens' });
  const motion = el('div', { class: 'sw-seg', role: 'radiogroup', 'aria-label': 'Motion' });
  [['60', '60', 'Full motion: every display frame'], ['30', '30', 'Lighter: 30 frames a second'], ['off', 'OFF', 'No motion: a still picture (values still update it)']].forEach(([v, l, t]) => {
    const b = el('button', { type: 'button', role: 'radio', title: t, text: l });
    b.addEventListener('click', () => SW.setSetting('motion', v));
    motion.appendChild(b);
  });
  const showMotion = () => motion.querySelectorAll('button').forEach((b, k) => b.setAttribute('aria-checked', ['60', '30', 'off'][k] === SW.settings.motion ? 'true' : 'false'));
  const themeBtn = el('button', { type: 'button', class: 'sw-icon', title: 'Dark / day', 'aria-label': 'Dark or day screen' });
  const sun = '<svg width="14" height="14" viewBox="0 0 14 14" aria-hidden="true"><circle cx="7" cy="7" r="3" fill="none" stroke="currentColor" stroke-width="1.3"/><path d="M7 1v1.6M7 11.4V13M1 7h1.6M11.4 7H13M2.8 2.8l1.1 1.1M10.1 10.1l1.1 1.1M2.8 11.2l1.1-1.1M10.1 3.9l1.1-1.1" stroke="currentColor" stroke-width="1.3" stroke-linecap="round"/></svg>';
  const moonIcon = '<svg width="14" height="14" viewBox="0 0 14 14" aria-hidden="true"><path d="M11.5 8.6A4.8 4.8 0 1 1 5.4 2.5a4 4 0 0 0 6.1 6.1z" fill="none" stroke="currentColor" stroke-width="1.3" stroke-linejoin="round"/></svg>';
  themeBtn.addEventListener('click', () => SW.setSetting('theme', SW.settings.theme === 'light' ? 'dark' : 'light'));
  const sizeBtn = el('button', { type: 'button', class: 'sw-icon', title: 'Window size', 'aria-label': 'Window size', style: { width: '44px', fontFamily: "'Space Mono', monospace", fontSize: '10px', fontWeight: '700' } });
  const ZOOMS = ['75', '90', '100', '115', '130'];
  sizeBtn.addEventListener('click', () => { const k = ZOOMS.indexOf(SW.settings.zoom); SW.setSetting('zoom', ZOOMS[(k + 1) % ZOOMS.length]); });
  const lic = el('button', { type: 'button', class: 'sw-lic', 'aria-label': 'Licence' }, el('span', { class: 'dot' }), el('span', { class: 't' }));
  lic.addEventListener('click', () => SW.openLicence());
  const tools = el('div', { class: 'sw-tools' }, el('span', { class: 'cap2', text: 'MOTION', style: { fontSize: '11px' } }), motion, themeBtn, sizeBtn, lic);
  app.appendChild(el('header', { class: 'sw-head' }, brand, tabs, tools));
  const showTools = () => {
    showMotion();
    themeBtn.innerHTML = SW.settings.theme === 'light' ? moonIcon : sun;
    sizeBtn.textContent = SW.settings.zoom + '%';
  };
  SW.on('setting', k => { showTools(); if (k === 'theme') applyTheme(); if (k === 'zoom') applyZoom(); });

  // ---- licence chip (state colours: green = licensed, amber = trial)
  SW.licence = Object.assign({ state: 'none' }, BOOT.licence || {});
  const showLic = () => {
    const ok = SW.licence.state === 'licensed';
    lic.className = 'sw-lic ' + (ok ? 'ok' : 'demo');
    lic.querySelector('.t').textContent = ok ? 'LICENSED' : 'TRIAL';
    lic.title = ok ? 'Licensed' : 'Trial: 3 s of silence every 60 s until it is activated';
  };
  SW.on('reply:licence', d => { Object.assign(SW.licence, d); showLic(); SW.emit('licence'); });

  // ---- screens
  const screens = [];
  SW.go = id => {
    screens.forEach(s => {
      const on = s.id === id;
      s.root.hidden = !on;
      s.tab.setAttribute('aria-selected', on ? 'true' : 'false');
      if (s.show) s.show(on);
    });
    SW.screen = id;
    ui.closeMenu();
  };
  SW.screens.forEach(def => {
    const root = el('section', { class: 'screen', 'aria-label': def.label, hidden: true });
    app.appendChild(root);
    const tab = el('button', { type: 'button', class: 'sw-tab', role: 'tab', text: def.label });
    tab.addEventListener('click', () => SW.go(def.id));
    tabs.appendChild(tab);
    screens.push({ id: def.id, root, tab, show: def.show });
    def.build(root);
  });
  app.appendChild(el('div', { id: 'swtoast', class: 'toast', role: 'status' }));

  // ---- the save dialog: name, category, author, comment; an existing name asks before it is replaced
  const dialog = (title, body, buttons) => {
    const box = el('div', { class: 'dialog', role: 'dialog', 'aria-modal': 'true', 'aria-label': title }, el('h2', { text: title }), body, el('div', { class: 'row end' }, buttons));
    const scrim = el('div', { class: 'scrim' }, box);
    scrim.addEventListener('pointerdown', e => { if (e.target === scrim) scrim.remove(); });
    scrim.addEventListener('keydown', e => { if (e.key === 'Escape') scrim.remove(); });
    app.appendChild(scrim);
    const f = box.querySelector('input,textarea,button'); if (f) setTimeout(() => f.focus(), 0);
    return scrim;
  };
  SW.dialog = dialog;
  SW.openSave = () => {
    const name = el('input', { class: 'field', maxlength: '64', 'aria-label': 'Name', value: SW.current.kind === 'user' ? SW.current.name : '' });
    name.placeholder = SW.current.name ? SW.current.name + ' 2' : 'My sound';
    const cat = el('select', { class: 'field', 'aria-label': 'Category' }, el('option', { value: '', text: '—' }), CATS.map(c => el('option', { value: c, text: c })));
    cat.value = SW.current.category || '';
    const author = el('input', { class: 'field', maxlength: '64', 'aria-label': 'Author', value: SW.settings.author || '' });
    const comment = el('input', { class: 'field', maxlength: '256', 'aria-label': 'Comment' });
    const msg = el('div', { class: 'msg' });
    const save = el('button', { type: 'button', class: 'btn on', text: 'SAVE' });
    const cancel = el('button', { type: 'button', class: 'btn', text: 'CANCEL' });
    const body = el('div', { style: { display: 'flex', flexDirection: 'column', gap: '10px' } },
      el('label', { class: 'cap2', text: 'NAME' }), name, el('label', { class: 'cap2', text: 'CATEGORY' }), cat,
      el('label', { class: 'cap2', text: 'AUTHOR' }), author, el('label', { class: 'cap2', text: 'COMMENT' }), comment, msg);
    const scrim = dialog('SAVE PRESET', body, [cancel, save]);
    let overwrite = false;
    const send = () => {
      const n = (name.value || name.placeholder).trim();
      if (!n) { msg.className = 'msg err'; msg.textContent = '名前を入れてください'; return; }
      if (author.value !== (SW.settings.author || '')) SW.setSetting('author', author.value);
      SW.call('save', SW.b64(JSON.stringify({ name: n, category: cat.value, author: author.value, comment: comment.value, overwrite })));
    };
    const off = SW.on('reply:saved', d => {
      if (d.exists && !overwrite) { overwrite = true; msg.className = 'msg err'; msg.textContent = '同じ名前のプリセットがあります。もう一度 SAVE で上書きします。'; save.textContent = 'REPLACE'; return; }
      if (!d.ok) { msg.className = 'msg err'; msg.textContent = d.error || '保存できませんでした'; return; }
      off(); scrim.remove();
      SW.current = { kind: 'user', path: d.path, name: d.name, category: d.category || '' };
      SW.emit('preset'); SW.call('users'); SW.toast('SAVED · ' + d.name);
    });
    name.addEventListener('input', () => { overwrite = false; save.textContent = 'SAVE'; msg.textContent = ''; });
    name.addEventListener('keydown', e => { if (e.key === 'Enter') send(); });
    save.addEventListener('click', send);
    cancel.addEventListener('click', () => { off(); scrim.remove(); });
  };

  // ---- the licence dialog: the state, this computer's code, activation with the key (online, when the plug-in knows the server)
  // or with a licence file (offline: a file activated on another computer for this one)
  SW.openLicence = () => {
    SW.call('lic');
    const state = el('p'), code = el('div', { class: 'code' }), msg = el('div', { class: 'msg' });
    const key = el('input', { class: 'field', 'aria-label': 'Licence key', placeholder: 'SWL-XXXXX-XXXXX-XXXXX-XXXXX', maxlength: '40' });
    const file = el('input', { type: 'file', accept: '.swlicense,text/plain', style: { display: 'none' } });
    const fromFile = el('button', { type: 'button', class: 'btn', text: 'LICENCE FILE…' });
    const activate = el('button', { type: 'button', class: 'btn on', text: 'ACTIVATE' });
    const copy = el('button', { type: 'button', class: 'btn', text: 'COPY CODE', style: { height: '32px', fontSize: '11px' } });
    const close = el('button', { type: 'button', class: 'btn', text: 'CLOSE' });
    const server = BOOT.server || '';
    const show = () => {
      const L = SW.licence;
      state.textContent = L.state === 'licensed' ? 'このパソコンで有効化されています。' + (L.id ? '（' + L.id + '）' : '')
        : '体験版です。ライセンスがない間は、起動から 30 秒後、そのあと 60 秒ごとに 3 秒の無音が入ります。';
      code.textContent = L.machine || '—';
      if (L.message) { msg.className = 'msg ' + (L.ok === false ? 'err' : 'ok'); msg.textContent = L.message; }
    };
    show();
    const off = SW.on('licence', show);
    const body = el('div', { style: { display: 'flex', flexDirection: 'column', gap: '10px' } }, state,
      el('label', { class: 'cap2', text: 'THIS COMPUTER' }), el('div', { class: 'row' }, code, copy),
      server ? el('label', { class: 'cap2', text: 'LICENCE KEY' }) : null, server ? key : null,
      el('p', { style: { fontSize: '13px' }, text: server ? 'ライセンスキーを入れて ACTIVATE。ネットのないパソコンは、別のパソコンでこのコードとキーから受け取ったライセンスファイルを LICENCE FILE で読み込みます。' : 'ライセンスファイル（.swlicense）を LICENCE FILE で読み込みます。' }),
      msg, file);
    const scrim = dialog('LICENCE', body, server ? [close, fromFile, activate] : [close, fromFile]);
    copy.addEventListener('click', () => { try { navigator.clipboard.writeText(SW.licence.machine || ''); SW.toast('COPIED'); } catch (e) { /* none */ } });
    fromFile.addEventListener('click', () => file.click());
    file.addEventListener('change', () => {
      const f = file.files && file.files[0]; if (!f) return;
      if (f.size > 16384) { msg.className = 'msg err'; msg.textContent = 'ライセンスファイルではありません'; return; }
      f.text().then(t => SW.call('licfile', SW.b64(t)));
    });
    activate.addEventListener('click', () => {
      const k = key.value.trim();
      if (!k) { msg.className = 'msg err'; msg.textContent = 'ライセンスキーを入れてください'; return; }
      msg.className = 'msg'; msg.textContent = '有効化しています…';
      fetch(server.replace(/\/$/, '') + '/api/activate', { method: 'POST', headers: { 'Content-Type': 'text/plain' }, body: JSON.stringify({ key: k, machine: SW.licence.machine }) })
        .then(r => r.json().then(j => ({ ok: r.ok, j })))
        .then(({ ok, j }) => { if (ok && j.license) SW.call('licfile', SW.b64(j.license)); else { msg.className = 'msg err'; msg.textContent = j.error || '有効化できませんでした'; } })
        .catch(() => { msg.className = 'msg err'; msg.textContent = 'サーバーにつながりません。ネットの接続を確かめるか、ライセンスファイルを使ってください。'; });
    });
    close.addEventListener('click', () => { off(); scrim.remove(); });
  };

  // ---- start
  applyTheme(); applyZoom(); showTools(); showLic();
  SW.go(SW.screens[0] ? SW.screens[0].id : '');
  SW.startAssets();
  SW.call('users');
  SW.call('lic');
  setInterval(() => { if (!document.hidden) SW.post('p'); }, 50);
  SW.post('r');
  document.addEventListener('contextmenu', e => e.preventDefault());
})();
