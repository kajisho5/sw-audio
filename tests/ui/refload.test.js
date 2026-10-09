// UT03 reference loading, the page's half: the 16 bit WAV the page writes (the core's decoder reads it), its base64 pieces joined up, the overview of a file.
const assert = require('assert');
const D = require('../../ui/displays.js');

// wav16: header fields and samples
{
  const n = 1000, l = new Float32Array(n), r = new Float32Array(n);
  for (let i = 0; i < n; i++) { l[i] = Math.sin(i * 0.05) * 0.5; r[i] = i % 2 ? 1.5 : -1.5; }   // r is out of range: clipped
  const w = D.wav16(l, r, n, 48000), dv = new DataView(w.buffer);
  assert.strictEqual(String.fromCharCode(...w.subarray(0, 4)), 'RIFF'); assert.strictEqual(String.fromCharCode(...w.subarray(8, 12)), 'WAVE'); assert.strictEqual(String.fromCharCode(...w.subarray(36, 40)), 'data');
  assert.strictEqual(w.length, 44 + n * 4); assert.strictEqual(dv.getUint32(4, true), 36 + n * 4); assert.strictEqual(dv.getUint16(20, true), 1); assert.strictEqual(dv.getUint16(22, true), 2);
  assert.strictEqual(dv.getUint32(24, true), 48000); assert.strictEqual(dv.getUint32(28, true), 48000 * 4); assert.strictEqual(dv.getUint16(32, true), 4); assert.strictEqual(dv.getUint16(34, true), 16); assert.strictEqual(dv.getUint32(40, true), n * 4);
  for (let i = 0; i < n; i += 37) { assert.ok(Math.abs(dv.getInt16(44 + 4 * i, true) / 32767 - l[i]) < 1e-4, 'left ' + i); assert.strictEqual(dv.getInt16(46 + 4 * i, true), i % 2 ? 32767 : -32768, 'right clipped ' + i); }
}
// b64 pieces of 3 x 65536 bytes (what the page sends) join to the base64 of the whole
{
  const n = 3 * 65536 * 2 + 12345, u = new Uint8Array(n); for (let i = 0; i < n; i++) u[i] = (i * 31 + (i >> 8)) & 255;
  const piece = 3 * 65536; let joined = ''; for (let off = 0; off < n; off += piece) joined += D.b64(u.subarray(off, Math.min(n, off + piece)));
  assert.strictEqual(joined, Buffer.from(u).toString('base64')); assert.strictEqual(D.b64(new Uint8Array(0)), '');
}
// overview: silence 0, full scale 1, half the amplitude (-6 dB) in between; one value per bin
{
  const n = 12100, l = new Float32Array(n), r = new Float32Array(n); for (let i = 0; i < n; i++) { const a = i < 4000 ? 0 : i < 8000 ? 1 : 0.5; l[i] = r[i] = (i % 2 ? 1 : -1) * a; }
  const o = D.overviewOf(l, r, n, 121); assert.strictEqual(o.length, 121);
  assert.strictEqual(o[5], 0); assert.ok(Math.abs(o[60] - 1) < 1e-6); assert.ok(Math.abs(o[110] - (1 - 6.02 / 50)) < 0.01);
}
console.log('reference loading: wav16, base64 pieces, overview ok');
