// Loads the real page (tools/gui_page_dump.cpp) in Chromium with no network and a stub of the native bridge, feeds it SWHOST.update(...) with the real argument list
// (values, latency, cpu, meters, spectrum, readouts, stereo) and reports script errors, the loaded fonts and the messages posted to the host. Needs Playwright (see CLAUDE.md).
// usage: NODE_PATH=$(npm root -g) node tools/gui_page_check.js /tmp/page.html /tmp/page.png [number of readouts the product sends, default 0]
const { chromium } = require('playwright');
(async () => {
  const [file, out, nro] = process.argv.slice(2), NRO = +(nro || 0);
  const b = await chromium.launch(); const ctx = await b.newContext({ viewport: { width: 1000, height: 700 } });
  const pg = await ctx.newPage(); const msgs = []; let errs = [];
  pg.on('pageerror', e => errs.push(e.message)); pg.on('console', m => { if (m.type() === 'error') errs.push(m.text()); });
  await pg.route(u => !u.href.startsWith('file:'), r => r.abort());           // no network: fonts must come from the page itself
  await pg.addInitScript(() => { window.__msgs = []; window.chrome = { webview: { postMessage: m => window.__msgs.push(m), addEventListener() {} } }; });
  await pg.goto('file://' + file); await pg.waitForTimeout(800);
  const nv = await pg.evaluate(() => SWBOOT.params.length + (SWBOOT.traits.autoGain ? 1 : 0) + (SWBOOT.traits.delta ? 1 : 0));
  for (let t = 0; t < 40; t++) {
    const vals = await pg.evaluate(() => SWBOOT.values.slice());
    const sp = Array.from({ length: 64 }, (_, i) => -30 - i * 0.6 + 6 * Math.sin(i / 4 + t / 3));
    const st = [0.5]; for (let k = 0; k < 160; k++) st.push(0.3 * Math.sin(k * 0.2 + t), 0.3 * Math.sin(k * 0.2 + t + 0.5));
    await pg.evaluate(([v, sp, st, t, NRO]) => SWHOST.update(v, 1.5, 12, [-18, -18, -15, -16], sp, Array.from({ length: NRO }, (_, k) => k === 0 ? -21 + Math.sin(t / 4) * 3 : k === 1 ? -22 : k === 5 ? -24 : 1 + Math.sin(t / 4)), st), [vals, sp, st, t, NRO]);
    await pg.waitForTimeout(60);
  }
  const fonts = await pg.evaluate(async () => { await document.fonts.ready; return [...document.fonts].filter(f => f.status === 'loaded').map(f => f.family + f.weight).join(','); });
  console.log('params', nv, 'errors', JSON.stringify(errs), 'fonts', fonts, 'posted', await pg.evaluate(() => window.__msgs.length));
  await pg.screenshot({ path: out }); await b.close();
})();
