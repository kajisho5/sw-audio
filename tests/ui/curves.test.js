// The screen's curves (ui/sw-ui.js) against the values sw::ParamSpec produced for every parameter of every product (tests/ui/curve_samples.json, from tools/dump_specs.py).
// run: node tests/ui/curves.test.js
const SWUI = require('../../ui/sw-ui.js');
const S = require('../../ui/specs.json').specs, C = require('./curve_samples.json');
const xs = [0, 0.13, 0.5, 0.77, 1];
let n = 0, bad = 0;
for (const code of Object.keys(S)) {
  S[code].forEach((p, i) => {
    const c = SWUI.makeCurve(p);
    xs.forEach((x, k) => {
      const [val, nrm] = C[code][i][k]; ++n;
      const v = c.value(x), nn = c.norm(val);
      const tol = 1e-6 * Math.max(1, Math.abs(val));
      if (Math.abs(v - val) > tol || Math.abs(nn - nrm) > 1e-6) { if (bad++ < 10) console.log('MISMATCH', code, p.id, 'x=' + x, 'js', v, nn, 'c++', val, nrm); }
    });
  });
}
// formatting spot checks
const f = (p, v) => SWUI.format(p, v, SWUI.makeCurve(p));
const assert = (a, b, m) => { if (a !== b) { bad++; console.log('FORMAT', m, JSON.stringify(a), '!=', JSON.stringify(b)); } };
assert(f({ curve: 'log', min: 20, max: 20000, unit: 'Hz' }, 1200), '1.2 kHz', 'kHz');
assert(f({ curve: 'lin', min: -24, max: 24, unit: 'dB' }, -3), '-3 dB', 'dB');
assert(f({ curve: 'lin', min: 0, max: 100, unit: '%' }, 62), '62%', 'percent');
assert(f({ curve: 'lin', min: 1, max: 20, unit: ':1', maxLabel: 'inf', maxLabelNorm: 0.95 }, 20), 'inf', 'maxLabel');
assert(f({ curve: 'lin', min: 20, max: 300, unit: 'Hz', minLabel: 'Off' }, 20), 'Off', 'minLabel');
assert(f({ curve: 'step', min: 0, max: 2, steps: [0, 1, 2], labels: ['A', 'B', 'C'] }, 1), 'B', 'step label');
assert(SWUI.parseValue({ unit: 'Hz' }, '1.5 kHz'), 1500, 'parse kHz');
assert(SWUI.parseValue({ unit: 'dB' }, '-6,5'), -6.5, 'parse comma');
console.log(n + ' curve samples,', bad ? bad + ' problems' : 'all match');
process.exit(bad ? 1 : 0);
