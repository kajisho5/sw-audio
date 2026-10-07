/* The page of a plug-in window: the bridge between the screen (sw-ui.js) and the native side.
   The native side exposes one function to post a text message — webkit.messageHandlers.sw (macOS) or chrome.webview (Windows) — and calls SWHOST.update(...) with text it composed.
   Messages to native (space separated): "s <i> <plain>" a value, "b <i>" / "e <i>" gesture begin / end, "c <name> <arg...>" a button, "p" poll, "r" ready.
   Poll answer (native -> page): SWHOST.update([plain values], latencyMs, cpu). A poll is sent every 50 ms while the page is visible. */
(function () {
  const post = m => { try { if (window.webkit && window.webkit.messageHandlers && window.webkit.messageHandlers.sw) window.webkit.messageHandlers.sw.postMessage(m); else if (window.chrome && window.chrome.webview) window.chrome.webview.postMessage(m); } catch (e) {} };
  let vals = SWBOOT.values.slice(), info = { latencyMs: SWBOOT.latencyMs || 0 }; const listeners = []; const pending = new Set();
  const bridge = {
    values: () => vals,
    set: (i, v) => { vals[i] = v; pending.add(i); post('s ' + i + ' ' + v); },
    begin: i => post('b ' + i), end: i => { post('e ' + i); },
    call: (name, ...a) => post(['c', name, ...a].join(' ')),
    onChange: cb => listeners.push(cb), info: () => info
  };
  window.SWHOST = {
    update(v, lat, cpu) {
      info = { latencyMs: lat, cpu: cpu < 0 ? undefined : cpu };
      v.forEach((x, i) => { if (!pending.has(i) && vals[i] !== x) { vals[i] = x; listeners.forEach(cb => cb(i, x)); } });
      pending.clear();
    }
  };
  window.SWUI_instance = SWUI.mount(document.getElementById('app'), { product: SWBOOT.product, params: SWBOOT.params, traits: SWBOOT.traits, bridge });
  document.addEventListener('contextmenu', e => e.preventDefault());
  setInterval(() => post('p'), 50); post('r');
})();
