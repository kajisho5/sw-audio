// The screens in a real browser (Playwright), against ui/preview.html with its simulated plug-in: what only a browser can tell.
//   1. every product's screen loads without a script error;
//   2. Undo / Redo record one step per gesture (a knob drag is one step), and the History button lists them and goes back;
//   3. EQ02 Assist (marks, a tap places a Bell) and Unmask (the SW Link overlay and its messages, the lamp);
//   4. Low lat (one button, the product's own latency setting); UT01's track line (remember / recall); VO03's chord line;
//   6. the EVO bar's Unit A / B / C (a radio of the product's `unit` parameter; dim on the products that have none).
//   7. the Learn button of DY04, CS02 and RV08, the Set input button of CS03 (all put in the EVO bar: the design only has the text), and DY10's Auto (the design's own button; what the core writes back is one undo step); MS07's Truncation check (the design's own button: it shows its progress and what it found); CS04's Suggest order (a menu of kinds: the order of the six modules, one undo step).
//   5. the EVO bar's oversampling button (1x / 2x / 4x: a click steps, the label follows, undo takes it back; hidden on MS04, dim where the product has no such stage).
// Needs the preview data (python3 tools/gen_skins.py writes ui/skins.json) and Playwright with a Chromium or Chrome:
//   NODE_PATH=$(npm root -g) [PW_CHROMIUM=/path/to/chrome] node tests/ui/browser.test.js            (CI: PW_CHANNEL=chrome)
const assert = require('assert');
const fs = require('fs'), http = require('http'), path = require('path');
const { chromium } = require('playwright');

const root = path.join(__dirname, '../../ui');
const types = { '.html': 'text/html', '.js': 'text/javascript', '.json': 'application/json', '.css': 'text/css', '.woff2': 'font/woff2' };
const server = http.createServer((req, res) => {
  const f = path.join(root, decodeURIComponent(req.url.split('?')[0]).replace(/\.\./g, ''));
  fs.readFile(f, (e, d) => { if (e) { res.writeHead(404); res.end(); } else { res.writeHead(200, { 'content-type': types[path.extname(f)] || 'application/octet-stream' }); res.end(d); } });
});

let browser, pg; const errors = []; let base = '', checks = 0;
const ok = (c, m) => { checks++; assert.ok(c, m); };
const eq = (a, b, m) => { checks++; assert.strictEqual(a, b, m); };
async function open(code, query = '') {
  await pg.goto(base + '/preview.html?p=' + code + '&sim=1' + query);
  await pg.waitForFunction(() => window.ready === true, null, { timeout: 15000 });
  await pg.waitForTimeout(250);
}

(async () => {
  await new Promise(r => server.listen(0, '127.0.0.1', r)); base = 'http://127.0.0.1:' + server.address().port;
  const launch = process.env.PW_CHANNEL ? { channel: process.env.PW_CHANNEL } : { executablePath: process.env.PW_CHROMIUM || '/opt/pw-browsers/chromium' };
  browser = await chromium.launch({ ...launch, args: ['--no-sandbox'] });
  pg = await browser.newPage({ viewport: { width: 1100, height: 800 } });
  pg.on('pageerror', e => errors.push('page error: ' + e));
  pg.on('console', m => { if (m.type() === 'error' && !/Failed to load resource/.test(m.text())) errors.push('console error: ' + m.text()); });

  // ---- 1. every screen
  const specs = JSON.parse(fs.readFileSync(path.join(root, 'specs.json'), 'utf8')).specs;   // the products that exist (products.json also lists the ones that are not built)
  const codes = Object.keys(JSON.parse(fs.readFileSync(path.join(root, 'products.json'), 'utf8'))).filter(c => specs[c.toLowerCase()]);
  ok(codes.length >= 100, 'the products that exist: ' + codes.length);
  for (const c of codes) {
    errors.length = 0; await open(c);
    const n = await pg.evaluate(() => document.getElementById('app').shadowRoot ? document.getElementById('app').shadowRoot.querySelectorAll('*').length : document.getElementById('app').querySelectorAll('*').length);
    ok(n > 30, c + ': the screen has content (' + n + ' elements)'); eq(errors.length, 0, c + ': ' + errors.join(' | '));
  }

  // ---- 2. undo / redo / history on DY08 (Threshold is the first knob, 0 dB at the top of its range: dragging down lowers it)
  await open('DY08');
  const dial = pg.locator('.ctl').first().locator('[data-dial]'), val = pg.locator('.ctl').first().locator('.val');
  const undoB = pg.locator('[data-act="undo"]'), redoB = pg.locator('[data-act="redo"]');
  const drag = async dy => { const r = await dial.boundingBox(), x = r.x + r.width / 2, y = r.y + r.height / 2; await pg.mouse.move(x, y); await pg.mouse.down(); for (let k = 1; k <= 10; k++) await pg.mouse.move(x, y + dy * k / 10); await pg.mouse.up(); await pg.waitForTimeout(80); };
  const v0 = await val.textContent(); await drag(40); const v1 = await val.textContent(); await drag(40); const v2 = await val.textContent();
  ok(v0 !== v1 && v1 !== v2, 'two drags changed the value'); ok(await undoB.isEnabled(), 'undo is enabled after a drag');
  await undoB.click(); eq(await val.textContent(), v1, 'one undo = one drag'); await undoB.click(); eq(await val.textContent(), v0, 'two undos'); ok(!(await undoB.isEnabled()), 'nothing left to undo');
  await redoB.click(); eq(await val.textContent(), v1, 'redo'); await redoB.click(); eq(await val.textContent(), v2, 'redo again');
  await pg.keyboard.press('Control+z'); eq(await val.textContent(), v1, 'Ctrl+Z undoes'); await pg.keyboard.press('Control+Shift+z'); eq(await val.textContent(), v2, 'Ctrl+Shift+Z redoes'); await pg.keyboard.press('Control+z'); await pg.keyboard.press('Control+y'); eq(await val.textContent(), v2, 'Ctrl+Y redoes');
  const popRows = () => pg.evaluate(() => { const sh = document.getElementById('app').shadowRoot; const p = [...sh.querySelectorAll('div')].find(d => /Go back to before|Nothing has been changed/.test(d.textContent) && d.style.position === 'absolute'); return p ? [...p.children].map(c => c.textContent) : null; });
  await pg.locator('button[data-history]').click(); await pg.waitForTimeout(150);
  let rows = await popRows(); ok(rows && rows.length === 3, 'History lists a header and the two drags: ' + JSON.stringify(rows)); ok(/Threshold/.test(rows[1]) && /→/.test(rows[1]), 'a row says what changed: ' + rows[1]);
  await pg.evaluate(() => { const sh = document.getElementById('app').shadowRoot; [...sh.querySelectorAll('div')].find(d => /Go back to before/.test(d.textContent) && d.style.position === 'absolute').children[2].click(); });
  await pg.waitForTimeout(150); eq(await val.textContent(), v0, 'a History row goes back to before that change'); rows = await popRows(); eq(rows, null, 'the list closes');
  await drag(40); const w1 = await val.textContent(); await dial.dblclick(); await pg.waitForTimeout(80); await undoB.click(); eq(await val.textContent(), w1, 'a double click (reset) is one step'); await undoB.click();
  const r2 = await dial.boundingBox(); await pg.mouse.move(r2.x + r2.width / 2, r2.y + r2.height / 2); for (let k = 0; k < 4; k++) await pg.mouse.wheel(0, 100); await pg.waitForTimeout(120);
  ok((await val.textContent()) !== v0, 'the wheel moved the value'); await undoB.click(); eq(await val.textContent(), v0, 'four wheel notches are one step');

  // ---- 3. EQ02 Assist and Unmask (the preview's simulated read-outs: three resonances; two other instances)
  await open('EQ02');
  const assist = pg.locator('button[data-call="assist"]'), marks = pg.locator('path[data-k]');
  eq(await marks.count(), 0, 'no marks before Assist'); await assist.click(); await pg.waitForTimeout(1200); eq(await marks.count(), 3, 'three resonance marks'); ok(/\bon\b/.test(await assist.getAttribute('class')), 'Assist is lit');
  const onBefore = await pg.locator('button.dbtn.on').count(); await marks.first().dispatchEvent('pointerdown', { bubbles: true }); await pg.waitForTimeout(400);
  ok(await undoB.isEnabled(), 'placing a Bell is an undo step'); await assist.click(); await pg.waitForTimeout(800); eq(await marks.count(), 0, 'the marks go when Assist is off'); void onBefore;
  const lamp = pg.locator('.evr[data-link]'), unmask = pg.locator('button[data-call="linkwatch"]'), note = pg.locator('svg text', { hasText: 'Unmask' });
  ok(/2 other/.test(await lamp.getAttribute('title')), 'the SW Link lamp says how many others: ' + await lamp.getAttribute('title')); eq(await lamp.locator('.evd').getAttribute('style'), '', 'the lamp is lit');
  await unmask.click(); await pg.waitForFunction(() => document.getElementById('app').shadowRoot.querySelectorAll('svg g rect[fill="#e5484d"]').length > 0, null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'Unmask shades overlapping bands (the simulated spectra move: waited 8 s)')); ok(/overlap/.test((await note.allTextContents()).join()), 'Unmask says how many bands overlap');
  await unmask.click(); await pg.waitForTimeout(500); eq(await pg.locator('svg g rect[fill="#e5484d"]').count(), 0, 'the shading goes when Unmask is off');
  await open('EQ02', '&peers=0'); await pg.locator('button[data-call="linkwatch"]').click(); await pg.waitForTimeout(800);
  ok(/no other SW AUDIO plug-in found/.test((await pg.locator('svg text', { hasText: 'Unmask' }).allTextContents()).join()), 'alone: Unmask says nobody else is there'); ok((await pg.locator('.evr[data-link] .evd').getAttribute('style')).includes('#55575c'), 'alone: the lamp is grey');

  // ---- 4. Low lat, UT01's track line
  await open('EQ08'); const low = pg.locator('button', { hasText: 'Low lat' });
  ok(!/\bon\b/.test((await low.getAttribute('class')) || ''), 'EQ08: Low lat is off while Phase is Linear'); await low.click(); await pg.waitForTimeout(300); ok(/\bon\b/.test(await low.getAttribute('class')), 'Low lat lights when Phase is Minimum');
  ok(/\bon\b/.test(await pg.locator('button', { hasText: 'Minimum' }).first().getAttribute('class')), 'the Minimum button is lit too');
  await open('UT01'); const evt = pg.locator('.evob .evt');
  ok(/Vocal track: no Gain remembered/.test(await evt.textContent()), 'UT01 shows the kind of track: ' + await evt.textContent());
  const d2 = pg.locator('[data-dial]').first(), rb = await d2.boundingBox(); await pg.mouse.move(rb.x + rb.width / 2, rb.y + rb.height / 2); await pg.mouse.down(); for (let k = 1; k <= 8; k++) await pg.mouse.move(rb.x + rb.width / 2, rb.y + rb.height / 2 - 6 * k); await pg.mouse.up();
  await evt.click(); await pg.waitForTimeout(250); ok(/Vocal track: Gain \+[\d.]+ dB remembered/.test(await evt.textContent()), 'a click remembers the Gain: ' + await evt.textContent());

  // VO03: the EVO line names the chord held on the MIDI track (the preview's read-out holds F A C most of the time)
  await open('VO03'); await pg.waitForFunction(() => /MIDI chord: C F A/.test(document.getElementById('app').shadowRoot.querySelector('.evob .evt').textContent), null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'VO03 shows the held chord'));

  // ---- 5. the oversampling button of the EVO bar
  await open('EQ01'); const osb = pg.locator('button[data-fmt="os"]');
  eq(await osb.count(), 1, 'EQ01 has the oversampling button'); eq((await osb.textContent()).trim(), '2× OS', 'default 2x');
  await osb.click(); await pg.waitForTimeout(150); eq((await osb.textContent()).trim(), '4× OS', 'a click steps to 4x');
  await osb.click(); await pg.waitForTimeout(150); eq((await osb.textContent()).trim(), '1× OS', 'then 1x'); await osb.click(); await pg.waitForTimeout(150); eq((await osb.textContent()).trim(), '2× OS', 'and round again to 2x');
  await pg.waitForTimeout(700); await osb.click(); await pg.waitForTimeout(150); eq((await osb.textContent()).trim(), '4× OS', 'a later click is a new undo step');
  await pg.locator('[data-act="undo"]').click(); await pg.waitForTimeout(150); eq((await osb.textContent()).trim(), '2× OS', 'undo takes that step back (quick clicks in a row are one step, like the wheel)');
  await open('SA03'); eq((await pg.locator('button[data-fmt="os"]').textContent()).trim(), '4× OS', 'SA03 starts at 4x (the spec recommends it)');
  await open('GT01'); eq((await pg.locator('button[data-fmt="os"]').textContent()).trim(), '4× OS', 'GT01 starts at 4x');
  await open('DL01'); eq(await pg.locator('button[data-fmt="os"]').count(), 0, 'DL01 has no oversampling button'); ok(await pg.locator('button[data-inert]', { hasText: '2× OS' }).count() === 1, 'DL01: the 2x OS button is dimmed');
  await open('MS04'); ok(await pg.locator('button', { hasText: '2× OS' }).evaluate(e => getComputedStyle(e).visibility === 'hidden' || e.style.visibility === 'hidden'), 'MS04: the EVO 2x OS is hidden (the panel has its own Oversample)');

  // ---- 6. Unit A / B / C
  await open('EQ03'); const unitBtn = t => pg.locator('.evob button', { hasText: new RegExp('^' + t + '$') });
  ok(/\bon\b/.test(await unitBtn('A').getAttribute('class')), 'Unit A is lit by default'); ok(!/\bon\b/.test((await unitBtn('B').getAttribute('class')) || ''), 'Unit B is not');
  await unitBtn('B').click(); await pg.waitForTimeout(150); ok(/\bon\b/.test(await unitBtn('B').getAttribute('class')), 'a click lights Unit B'); ok(!/\bon\b/.test((await unitBtn('A').getAttribute('class')) || ''), 'and A goes out');
  await pg.waitForTimeout(700); await unitBtn('C').click(); await pg.waitForTimeout(150); ok(/\bon\b/.test(await unitBtn('C').getAttribute('class')), 'Unit C');
  await pg.locator('[data-act="undo"]').click(); await pg.waitForTimeout(150); ok(/\bon\b/.test(await unitBtn('B').getAttribute('class')), 'undo goes back to B');
  await open('UT01'); ok(await pg.locator('.evob button[data-inert]', { hasText: /^A$/ }).count() === 1, 'UT01 has no Unit: A is dimmed'); ok(/no Unit A \/ B \/ C/.test(await pg.locator('.evob button[data-inert]', { hasText: /^A$/ }).getAttribute('title')), 'and says why');

  // ---- 7. DY04 Learn (the button is not in the design: gen_skins puts it in the EVO bar; the core's learner reports through the read-outs)
  await open('DY04'); const lrn = pg.locator('.evob button[data-call="learn"]');
  eq(await lrn.count(), 1, 'DY04 has the Learn button in the EVO bar'); eq((await lrn.textContent()).trim(), 'Learn', 'idle: it says Learn');
  await lrn.click(); await pg.waitForFunction(() => /Listening: \d+ hits/.test(document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="learn"]').textContent), null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'while listening the button shows the hits heard'));
  ok(/\bon\b/.test(await lrn.getAttribute('class')), 'and is lit');
  await lrn.click(); await pg.waitForFunction(() => document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="learn"]').textContent.trim() === 'Learn', null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'a second click ends the listening: back to Learn'));
  ok(!/\bon\b/.test((await lrn.getAttribute('class')) || ''), 'and goes out');
  await open('CS02'); const lrn2 = pg.locator('.evob button[data-call="learn"]');
  eq(await lrn2.count(), 1, 'CS02 has the Learn button in the EVO bar'); eq((await lrn2.textContent()).trim(), 'Learn', 'CS02 idle: it says Learn');
  await lrn2.click(); await pg.waitForFunction(() => /Listening: \d+ hits/.test(document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="learn"]').textContent), null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'CS02: while listening the button shows the hits heard'));
  await lrn2.click(); await pg.waitForFunction(() => document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="learn"]').textContent.trim() === 'Learn', null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'CS02: a second click ends the listening'));
  await open('CS03'); const lrn3 = pg.locator('.evob button[data-call="learn"]');
  eq(await lrn3.count(), 1, 'CS03 has the Set input button in the EVO bar'); eq((await lrn3.textContent()).trim(), 'Set input', 'CS03 idle: it says Set input');
  await lrn3.click(); await pg.waitForFunction(() => /Listening \d+ %/.test(document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="learn"]').textContent), null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'CS03: while listening the button shows how far it is'));
  await pg.waitForFunction(() => document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="learn"]').textContent.trim() === 'Set input', null, { timeout: 15000 }).then(() => ok(true), () => ok(false, 'CS03: after the 5 s it is back to Set input by itself'));
  await open('RV08'); const lrn4 = pg.locator('.evob button[data-call="learn"]');
  eq(await lrn4.count(), 1, 'RV08 has the Learn button in the EVO bar'); eq((await lrn4.textContent()).trim(), 'Learn', 'RV08 idle: it says Learn');
  await lrn4.click(); await pg.waitForFunction(() => /Listening: \d+ hits/.test(document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="learn"]').textContent), null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'RV08: while listening the button shows the hits heard'));
  await lrn4.click(); await pg.waitForFunction(() => document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="learn"]').textContent.trim() === 'Learn', null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'RV08: a second click ends the listening'));
  // DY10 Auto: the design's own button; what the core writes in answer (three crossovers, as changes from the host) is one undo step
  await open('DY10'); const aut = pg.locator('button[data-call="learn"]');
  eq(await aut.count(), 1, 'DY10 has the Auto button (the toolbar\'s Auto gain is not it)'); eq((await aut.textContent()).trim(), 'Auto', 'DY10 idle: it says Auto');
  const x0 = (await pg.evaluate(() => window.simValues())).slice(32, 35);
  await aut.click(); await pg.waitForFunction(() => /Listening \d+ %/.test(document.getElementById('app').shadowRoot.querySelector('button[data-call="learn"]').textContent), null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'DY10: while listening the button shows how far it is'));
  await pg.waitForFunction(() => document.getElementById('app').shadowRoot.querySelector('button[data-call="learn"]').textContent.trim() === 'Auto', null, { timeout: 15000 }).then(() => ok(true), () => ok(false, 'DY10: after the listening it is back to Auto'));
  await pg.waitForTimeout(700);   // the writes have stopped
  eq(JSON.stringify((await pg.evaluate(() => window.simValues())).slice(32, 35)), '[600,1800,5200]', 'the crossovers the core wrote');
  await pg.locator('[data-act="undo"]').click(); await pg.waitForTimeout(250);
  eq(JSON.stringify((await pg.evaluate(() => window.simValues())).slice(32, 35)), JSON.stringify(x0), 'one Undo takes all three back (the Auto step)');
  await pg.locator('[data-act="redo"]').click(); await pg.waitForTimeout(250);
  eq(JSON.stringify((await pg.evaluate(() => window.simValues())).slice(32, 35)), '[600,1800,5200]', 'and Redo brings them again');
  await aut.click(); await pg.waitForTimeout(300); await aut.click(); await pg.waitForTimeout(300);   // started, then cancelled: back to Auto, nothing written
  eq((await aut.textContent()).trim(), 'Auto', 'a second press while it listens cancels');
  // MS07 Truncation check: a button of the panel; it shows how far it is and then what it found
  await open('MS07'); const trc = pg.locator('button[data-call="check"]');
  eq(await trc.count(), 1, 'MS07 has the Truncation check button'); eq((await trc.textContent()).trim(), 'Truncation check', 'MS07 idle: it says Truncation check');
  await trc.click(); await pg.waitForFunction(() => /Checking \d+ %/.test(document.getElementById('app').shadowRoot.querySelector('button[data-call="check"]').textContent), null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'MS07: while listening the button shows how far it is'));
  await pg.waitForFunction(() => /Input: 16 bit grid/.test(document.getElementById('app').shadowRoot.querySelector('button[data-call="check"]').textContent), null, { timeout: 15000 }).then(() => ok(true), () => ok(false, 'MS07: the button says what it found'));
  ok(/16 bit/.test(await trc.getAttribute('title')) && /Bits is 16/.test(await trc.getAttribute('title')), 'and the tooltip puts it beside Bits: ' + await trc.getAttribute('title'));
  // CS04 Suggest order: a menu of kinds in the EVO bar; a choice sets the order of the six modules (one undo step)
  await open('CS04'); const sug = pg.locator('.evob button[data-suggest]');
  eq(await sug.count(), 1, 'CS04 has the Suggest order button'); eq((await sug.textContent()).trim(), 'Suggest order', 'its label');
  const cardOrder = () => pg.evaluate(() => { const sh = document.getElementById('app').shadowRoot; const cards = [...sh.querySelectorAll('.disp')].map(d => d.firstElementChild).find(h => h && h.children.length === 6); return cards ? [...cards.children].map(c => [+c.style.order || 0, c.querySelector('.lbl') ? c.querySelector('.lbl').textContent.trim() : '']).sort((a, b) => a[0] - b[0]).map(x => x[1]).join(' > ') : null; });
  eq(await cardOrder(), 'Gate > EQ > Comp > Saturate > De-ess > Limit', 'the default order');
  await sug.click(); await pg.waitForTimeout(150);
  const kinds = await pg.evaluate(() => [...document.getElementById('app').shadowRoot.querySelectorAll('[data-kind]')].map(r => r.dataset.kind));
  eq(kinds.join(','), 'Voice (speech),Vocal (singing),Drums,Bass,Mix bus', 'the menu lists the kinds');
  await pg.evaluate(() => [...document.getElementById('app').shadowRoot.querySelectorAll('[data-kind]')].find(r => r.dataset.kind === 'Mix bus').click()); await pg.waitForTimeout(400);
  eq(await cardOrder(), 'EQ > Comp > Saturate > Limit > Gate > De-ess', 'Mix bus puts the modules in the bus order');
  eq(await pg.evaluate(() => !!document.getElementById('app').shadowRoot.querySelector('[data-kind]')), false, 'the menu closed');

  await pg.locator('[data-act="undo"]').click(); await pg.waitForTimeout(400);
  eq(await cardOrder(), 'Gate > EQ > Comp > Saturate > De-ess > Limit', 'one Undo takes the order back');
  for (const [kind, ord] of [['Voice (speech)', 'Gate > EQ > De-ess > Comp > Saturate > Limit'], ['Vocal (singing)', 'Gate > EQ > Comp > De-ess > Saturate > Limit'], ['Drums', 'Gate > EQ > Comp > Saturate > Limit > De-ess'], ['Bass', 'Gate > Comp > EQ > Saturate > Limit > De-ess']]) {
    await sug.click(); await pg.waitForTimeout(100);
    await pg.evaluate(k => [...document.getElementById('app').shadowRoot.querySelectorAll('[data-kind]')].find(r => r.dataset.kind === k).click(), kind); await pg.waitForTimeout(350);
    eq(await cardOrder(), ord, kind + ': the table\'s order');
  }

  // EQ05 Match: "Reference" takes a file (chosen or dropped), "Match" listens and then the core writes the four bands (one undo step); without a reference Match asks for one
  await open('EQ05'); const mt = pg.locator('.evob button[data-call="match"]'), mref = pg.locator('.evob button[data-ref]');
  eq(await mt.count(), 1, 'EQ05 has the Match button in the EVO bar'); eq(await mref.count(), 1, 'and the Reference button'); eq((await mt.textContent()).trim(), 'Match', 'Match idle'); eq((await mref.textContent()).trim(), 'Reference', 'Reference idle');
  ok(/Choose a reference first/.test(await mt.getAttribute('title')), 'without a reference the tooltip says so');
  const wavFile = (sec, rate) => { const n = sec * rate, b = Buffer.alloc(44 + 2 * n); b.write('RIFF', 0); b.writeUInt32LE(36 + 2 * n, 4); b.write('WAVEfmt ', 8); b.writeUInt32LE(16, 16); b.writeUInt16LE(1, 20); b.writeUInt16LE(1, 22); b.writeUInt32LE(rate, 24); b.writeUInt32LE(rate * 2, 28); b.writeUInt16LE(2, 32); b.writeUInt16LE(16, 34); b.write('data', 36); b.writeUInt32LE(2 * n, 40); for (let i = 0; i < n; i++) b.writeInt16LE(Math.round(6000 * Math.sin(i * 0.05) + (Math.random() - 0.5) * 6000), 44 + 2 * i); return b; };
  const eqDef = (await pg.evaluate(() => window.simValues())).slice(0, 12);
  const chooser = pg.waitForEvent('filechooser'); await mt.click(); const fc = await chooser;   // (no reference: the press opens the file chooser)
  await fc.setFiles({ name: 'reference-song.wav', mimeType: 'audio/wav', buffer: wavFile(3, 48000) });
  await pg.waitForFunction(() => document.getElementById('app').shadowRoot.querySelector('.evob button[data-ref]').textContent.trim() === 'reference-son…', null, { timeout: 15000 }).then(() => ok(true), () => ok(false, 'EQ05: the Reference button shows the file\'s name'));
  ok(/Reference: reference-song\.wav \(0:03\)/.test(await mref.getAttribute('title')), 'its tooltip: ' + await mref.getAttribute('title'));
  await mt.click(); await pg.waitForFunction(() => /Listening \d+ %/.test(document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="match"]').textContent), null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'EQ05: while listening the button shows how far it is'));
  await pg.waitForFunction(() => document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="match"]').textContent.trim() === 'Matched', null, { timeout: 15000 }).then(() => ok(true), () => ok(false, 'EQ05: after the listening and the fit it says Matched'));
  ok(/3\.1 dB/.test(await mt.getAttribute('title')) && /0\.3 dB/.test(await mt.getAttribute('title')), 'the tooltip has the difference before and after: ' + await mt.getAttribute('title'));
  await pg.waitForTimeout(700);
  eq(JSON.stringify((await pg.evaluate(() => window.simValues())).slice(0, 12)), '[3.5,9000,0,-2,2800,1.1,1.5,450,0.9,2,90,0]', 'the values the core wrote');
  await pg.locator('[data-act="undo"]').click(); await pg.waitForTimeout(250);
  eq(JSON.stringify((await pg.evaluate(() => window.simValues())).slice(0, 12)), JSON.stringify(eqDef), 'one Undo takes all twelve back (the Match step)');
  await pg.locator('[data-act="redo"]').click(); await pg.waitForTimeout(250);
  eq(JSON.stringify((await pg.evaluate(() => window.simValues())).slice(0, 12)), '[3.5,9000,0,-2,2800,1.1,1.5,450,0.9,2,90,0]', 'and Redo brings them again');
  await mt.click(); await pg.waitForTimeout(400); await mt.click(); await pg.waitForTimeout(400);   // started, then cancelled
  eq((await mt.textContent()).trim(), 'Match', 'a second press while it listens cancels');
  await mref.click({ modifiers: ['Shift'] }); await pg.waitForTimeout(300);
  eq((await mref.textContent()).trim(), 'Reference', 'shift-click on Reference removes it');
  await pg.evaluate(() => { const sh = document.getElementById('app').shadowRoot, dt = new DataTransfer(); dt.items.add(new File([new Uint8Array(1)], 'x.wav')); sh.querySelector('.evob button[data-ref]').dispatchEvent(new DragEvent('dragover', { dataTransfer: dt, bubbles: true, cancelable: true })); });
  // EQ05 From UT03 (SW Link): enabled while a UT03 in the host shares a reference; pressing it takes that as the reference
  await open('EQ05', '&utref=0'); const mlk = pg.locator('.evob button[data-linkref]');
  eq(await mlk.count(), 1, 'EQ05 has the From UT03 button'); ok(/No UT03 Reference/.test(await mlk.getAttribute('title')), 'dim without a UT03 reference: ' + await mlk.getAttribute('title'));
  await mlk.click(); await pg.waitForTimeout(400); eq((await pg.locator('.evob button[data-ref]').textContent()).trim(), 'Reference', 'a press without a UT03 reference does nothing');
  await open('EQ05', '&utref=1'); const mlk2 = pg.locator('.evob button[data-linkref]'); ok(!/No UT03 Reference/.test(await mlk2.getAttribute('title')), 'lit with one: ' + await mlk2.getAttribute('title'));
  await mlk2.click(); await pg.waitForFunction(() => document.getElementById('app').shadowRoot.querySelector('.evob button[data-ref]').textContent.trim() === 'UT03 reference', null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'EQ05: the UT03 reference is the reference'));
  ok(/Reference: UT03 reference\./.test(await pg.locator('.evob button[data-ref]').getAttribute('title')), 'its tooltip: ' + await pg.locator('.evob button[data-ref]').getAttribute('title'));
  await pg.locator('.evob button[data-call="match"]').click(); await pg.waitForFunction(() => /Listening \d+ %/.test(document.getElementById('app').shadowRoot.querySelector('.evob button[data-call="match"]').textContent), null, { timeout: 8000 }).then(() => ok(true), () => ok(false, 'EQ05: Match listens with the UT03 reference'));
  // LV05 Key: the design's dropdown lists the host's sidechain and the other SW AUDIO instances (SW Link); the choice is the Key parameter; the dot says whether that instance is there
  await open('LV05'); const ksel = pg.locator('button.sel');
  eq(await ksel.count(), 1, 'LV05 has the Key dropdown'); eq((await ksel.textContent()).trim(), 'Sidechain', 'it starts on the host\'s sidechain'); ok(/sidechain input/.test(await ksel.getAttribute('title')), 'its tooltip: ' + await ksel.getAttribute('title'));
  await ksel.click(); await pg.waitForTimeout(150);
  const keyRows = await pg.evaluate(() => [...document.getElementById('app').shadowRoot.querySelectorAll('[data-key]')].map(r => r.textContent.replace('\u2713 ', '')));
  eq(keyRows.length, 30, 'the menu lists Sidechain and 29 LIVE products'); eq(keyRows[0] + ' / ' + keyRows[1] + ' / ' + keyRows[29], 'Sidechain / LV01 Voice / LV30 Recorder', 'in the order of the products');
  await pg.evaluate(() => [...document.getElementById('app').shadowRoot.querySelectorAll('[data-key]')].find(r => /LV01 Voice/.test(r.textContent)).click()); await pg.waitForTimeout(400);
  eq((await ksel.textContent()).trim(), 'LV01 Voice', 'the button shows the choice'); eq((await pg.evaluate(() => window.simValues()))[6], 1, 'the Key parameter is 1');
  ok(/LV01 Voice is in this host and playing/.test(await ksel.getAttribute('title')), 'the instance is found (read-out): ' + await ksel.getAttribute('title'));
  eq(await pg.evaluate(() => !!document.getElementById('app').shadowRoot.querySelector('[data-key]')), false, 'the menu closed');
  await open('DY04'); eq(await pg.locator('.evob button[data-ref]').count(), 0, 'DY04 has no Reference button');

  await open('DY01'); eq(await pg.locator('.evob button[data-call="learn"]').count(), 0, 'DY01 has no Learn button');

  await browser.close(); server.close();
  console.log('browser checks: ' + codes.length + ' screens load, undo / history / Assist / Unmask / Low lat / UT01 line / oversampling button / Unit / DY04 / CS02 / RV08 Learn, CS03 Set input, DY10 Auto, MS07 Truncation check, CS04 Suggest order and EQ05 Match and LV05 Key: ' + checks + ' checks passed');
})().catch(async e => { console.error(e); try { await browser.close(); } catch (_) { } server.close(); process.exit(1); });
