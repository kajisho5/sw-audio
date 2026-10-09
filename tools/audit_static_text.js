// Finds text in a product's screen that does not follow any parameter: the leaf texts that contain a digit and stay the same when every parameter is at a different (pseudo-random) value.
// What it prints are the numbers that the design drew as examples and nothing drives (or that are meant to be fixed: axis labels, names). Needs Playwright (see CLAUDE.md) and the preview served:
//   (cd ui && python3 -m http.server 8123 &) ; NODE_PATH=$(npm root -g) node tools/audit_static_text.js [CODE ...]      (all products without arguments)
const { chromium } = require('playwright');
const fs = require('fs'), path = require('path');
const specs = JSON.parse(fs.readFileSync(path.join(__dirname, '../ui/specs.json'), 'utf8')).specs;
(async () => {
  const codes = process.argv.length > 2 ? process.argv.slice(2).map(c => c.toLowerCase()) : Object.keys(specs);
  const b = await chromium.launch({ executablePath: process.env.PW_CHROMIUM || '/opt/pw-browsers/chromium' });
  const pg = await b.newPage({ viewport: { width: 1280, height: 900 } });
  const leaves = () => {
    const out = [];
    const walk = root => root.querySelectorAll('*').forEach(e => {
      if (e.shadowRoot) walk(e.shadowRoot);
      if (e.children.length || /^(STYLE|SCRIPT|TITLE|HEAD|META)$/.test(e.tagName)) return;
      const t = (e.textContent || '').trim(); if (!t || !/\d/.test(t) || e.closest('.tb, .evob')) return;
      out.push(t);
    });
    walk(document); return out;
  };
  const sets = [];
  for (const c of codes) {
    const runs = [];
    for (const r of [1, 2, 3]) { await pg.goto(`http://localhost:8123/preview.html?p=${c}&rand=${r}`); await pg.waitForTimeout(350); runs.push(new Set(await pg.evaluate(leaves))); }
    const fixed = [...runs[0]].filter(t => runs[1].has(t) && runs[2].has(t) && t.length < 60 && !/^SW [A-Z]{2}\d\d$/.test(t));
    console.log(c.toUpperCase().padEnd(5), fixed.join(' | '));
  }
  await b.close();
})();
