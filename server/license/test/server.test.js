// SW AUDIO licence server: node --test (Node 20+). Stripe and D1 are replaced by fakes; the crypto is the real WebCrypto.
// With SW_LICENSE_TOOL=<path to build/sw_license>, the licences it issues are also checked by the plug-ins' C++ code.
import { test } from 'node:test';
import assert from 'node:assert/strict';
import { execFileSync } from 'node:child_process';
import { mkdtempSync, writeFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join } from 'node:path';
import { keyHash, licenseKeyFor, normalizeKey, toB64, toHex, verifyStripeSignature } from '../src/core.js';
import { MemoryStore } from '../src/store.js';
import { handle } from '../src/worker.js';

const enc = new TextEncoder();
const NOW = 1_790_000_000;
const MACHINE = (c) => c.repeat(64);

async function setup(skus = ['in07']) {
  const kp = await crypto.subtle.generateKey({ name: 'Ed25519' }, true, ['sign', 'verify']);
  const env = {
    STRIPE_WEBHOOK_SECRET: 'whsec_test_secret',
    STRIPE_API_KEY: 'rk_test_unused',
    LICENSE_KEY_SECRET: 'test-licence-key-secret',
    LICENSE_PRIVATE_KEY: toB64(await crypto.subtle.exportKey('pkcs8', kp.privateKey)),
    LICENSE_KEY_ID: '1', LICENSE_MAJOR: '1', MAX_ACTIVATIONS: '3',
  };
  const store = new MemoryStore();
  const calls = [];
  const deps = {
    store,
    now: () => NOW,
    lineItemSkus: async (sid) => { calls.push(sid); return skus; },
    privateKey: async () => kp.privateKey,
  };
  const publicRaw = new Uint8Array(await crypto.subtle.exportKey('raw', kp.publicKey));
  return { env, store, deps, kp, publicRaw, calls };
}
async function signed(env, body, t = NOW, secret = env.STRIPE_WEBHOOK_SECRET) {
  const k = await crypto.subtle.importKey('raw', enc.encode(secret), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  const sig = toHex(await crypto.subtle.sign('HMAC', k, enc.encode(`${t}.${body}`)));
  return `t=${t},v1=${sig}`;
}
function post(path, body, headers = {}) {
  return new Request(`https://licence.example${path}`, { method: 'POST', body, headers });
}
function checkoutEvent(id, sessionId, status = 'paid', type = 'checkout.session.completed') {
  return JSON.stringify({ id, type, data: { object: { id: sessionId, mode: 'payment', payment_status: status, payment_intent: `pi_${sessionId}` } } });
}
async function webhook(s, body, headerOverride) {
  const h = headerOverride ?? (await signed(s.env, body));
  return handle(post('/stripe/webhook', body, { 'stripe-signature': h }), s.env, s.deps);
}
async function activate(s, key, machine) {
  const r = await handle(post('/api/activate', JSON.stringify({ key, machine })), s.env, s.deps);
  return { status: r.status, body: await r.json() };
}

test('Stripe signatures: a good one passes; wrong secret, changed body, old timestamp, garbage are refused', async () => {
  const s = await setup();
  const body = checkoutEvent('evt_1', 'cs_test_0000000001');
  assert.equal((await verifyStripeSignature(await signed(s.env, body), body, s.env.STRIPE_WEBHOOK_SECRET, NOW)).ok, true);
  assert.equal((await webhook(s, body, await signed(s.env, body, NOW, 'whsec_other'))).status, 400);
  assert.equal((await webhook(s, body.replace('paid', 'unpaid'), await signed(s.env, body))).status, 400);
  assert.equal((await webhook(s, body, await signed(s.env, body, NOW - 3600))).status, 400);   // a replay an hour later
  assert.equal((await webhook(s, body, 't=1,v1=zz')).status, 400);
  assert.equal((await webhook(s, body, '')).status, 400);
  assert.equal(s.store.licenses.length, 0);
});

test('a paid Checkout Session makes one licence (twice delivered = one); unpaid waits for the async success; refunds stop activations', async () => {
  const s = await setup(['in07', 'studio,live', 'BAD SKU']);
  assert.equal((await webhook(s, checkoutEvent('evt_a', 'cs_test_unpaid0001', 'unpaid'))).status, 200);
  assert.equal(s.store.licenses.length, 0);
  assert.equal((await webhook(s, checkoutEvent('evt_b', 'cs_test_unpaid0001', 'paid', 'checkout.session.async_payment_succeeded'))).status, 200);
  assert.equal(s.store.licenses.length, 1);
  assert.equal(s.store.licenses[0].products, 'in07,studio,live');   // the invalid sku is dropped
  await webhook(s, checkoutEvent('evt_c', 'cs_test_unpaid0001'));   // the same session again
  assert.equal(s.store.licenses.length, 1);
  // the key: derived from the session, only its hash stored
  const key = await licenseKeyFor(s.env.LICENSE_KEY_SECRET, 'cs_test_unpaid0001');
  assert.match(key, /^SWL-[0-9A-HJKMNP-TV-Z]{5}(-[0-9A-HJKMNP-TV-Z]{5}){3}$/);
  assert.equal(s.store.licenses[0].key_hash, await keyHash(key));
  assert.ok(!JSON.stringify(s.store).includes(key));
  // refund: no more activations
  assert.equal((await activate(s, key, MACHINE('a'))).status, 200);
  const refund = JSON.stringify({ id: 'evt_r', type: 'charge.refunded', data: { object: { refunded: true, payment_intent: 'pi_cs_test_unpaid0001' } } });
  assert.equal((await webhook(s, refund)).status, 200);
  assert.equal((await activate(s, key, MACHINE('b'))).status, 403);
});

test('activation: a licence file bound to the computer, signed so the plug-in accepts it; at most 3 computers; deactivation frees one', async () => {
  const s = await setup(['in07']);
  await webhook(s, checkoutEvent('evt_1', 'cs_test_buyer00001'));
  const key = await licenseKeyFor(s.env.LICENSE_KEY_SECRET, 'cs_test_buyer00001');
  const r = await activate(s, key.toLowerCase().replace(/-/g, ' '), MACHINE('a'));   // typed loosely
  assert.equal(r.status, 200);
  const text = r.body.license;
  assert.match(text, /^SW-LICENSE 1\nkey=1\nproduct=in07\nlicense=L-\d{8}-[0-9a-f]{8}\nmachine=a{64}\nissued=\d{4}-\d{2}-\d{2}\nmajor=1\nsig=[A-Za-z0-9+/]{86}==\n$/);
  // the signature with the public key, as the plug-in checks it
  const cut = text.lastIndexOf('sig=');
  const sig = Uint8Array.from(atob(text.slice(cut + 4).trim()), (c) => c.charCodeAt(0));
  assert.equal(await crypto.subtle.verify({ name: 'Ed25519' }, s.kp.publicKey, sig, enc.encode(text.slice(0, cut))), true);
  // the C++ check of the plug-ins (when the tool is built)
  if (process.env.SW_LICENSE_TOOL) {
    const dir = mkdtempSync(join(tmpdir(), 'swl-'));
    try {
      writeFileSync(join(dir, 'a.swlicense'), text);
      const out = execFileSync(process.env.SW_LICENSE_TOOL, ['verify', toHex(s.publicRaw), '1', join(dir, 'a.swlicense'), 'in07', '1', MACHINE('a')]).toString();
      assert.equal(out.trim(), 'licensed');
    } finally { rmSync(dir, { recursive: true, force: true }); }
  }
  // the same computer again: free; two more; the fifth is refused until one is deactivated
  assert.equal((await activate(s, key, MACHINE('a'))).status, 200);
  assert.equal((await activate(s, key, MACHINE('b'))).status, 200);
  assert.equal((await activate(s, key, MACHINE('c'))).status, 200);
  assert.equal((await activate(s, key, MACHINE('d'))).status, 409);
  const d = await handle(post('/api/deactivate', JSON.stringify({ key, machine: MACHINE('b') })), s.env, s.deps);
  assert.deepEqual(await d.json(), { removed: true });
  assert.equal((await activate(s, key, MACHINE('d'))).status, 200);
  // wrong inputs
  assert.equal((await activate(s, 'SWL-00000-00000-00000-00000', MACHINE('a'))).status, 404);
  assert.equal((await activate(s, key, 'not-a-machine')).status, 400);
  assert.equal((await activate(s, 'hello', MACHINE('a'))).status, 400);
  const bad = await handle(post('/api/activate', '{not json'), s.env, s.deps);
  assert.equal(bad.status, 400);
});

test('the thanks page: waits for the webhook, then shows the key (escaped); refuses anything that is not a session id', async () => {
  const s = await setup(['in07']);
  const get = (q) => handle(new Request(`https://licence.example/thanks?session_id=${encodeURIComponent(q)}`), s.env, s.deps);
  assert.equal((await get('cs_test_waiting001')).status, 202);
  await webhook(s, checkoutEvent('evt_1', 'cs_test_waiting001'));
  const r = await get('cs_test_waiting001');
  assert.equal(r.status, 200);
  const page = await r.text();
  assert.ok(page.includes(await licenseKeyFor(s.env.LICENSE_KEY_SECRET, 'cs_test_waiting001')));
  assert.equal(r.headers.get('referrer-policy'), 'no-referrer');
  assert.equal(r.headers.get('cache-control'), 'no-store');
  assert.equal((await get('<script>alert(1)</script>')).status, 400);
  // the form activation returns the file as a download
  const key = await licenseKeyFor(s.env.LICENSE_KEY_SECRET, 'cs_test_waiting001');
  const f = await handle(post('/activate', new URLSearchParams({ key, machine: MACHINE('e') }).toString(), { 'content-type': 'application/x-www-form-urlencoded' }), s.env, s.deps);
  assert.equal(f.status, 200);
  assert.match(f.headers.get('content-disposition'), /attachment/);
  assert.match(await f.text(), /^SW-LICENSE 1\n/);
});

test('licence keys: typed loosely they still match; anything else is not a key', () => {
  assert.equal(normalizeKey('swl-abcde-fghjk-mnpqr-stvwx'), 'SWL-ABCDE-FGHJK-MNPQR-STVWX');
  assert.equal(normalizeKey('SWL ABCDE FGHJK MNPQR STVWX'), 'SWL-ABCDE-FGHJK-MNPQR-STVWX');
  assert.equal(normalizeKey('SWL-0OIL1-00000-00000-00000'), 'SWL-00111-00000-00000-00000');   // O = 0, I = L = 1
  assert.equal(normalizeKey('SWL-ABCDE-FGHJK-MNPQR-STVWU'), null);   // U is not in the alphabet
  assert.equal(normalizeKey('SWL-ABCDE'), null);
  assert.equal(normalizeKey(42), null);
  assert.equal(normalizeKey('x'.repeat(100)), null);
});
