// SW AUDIO licence server (Cloudflare Workers + D1). Sales are perpetual (買い切り) through Stripe Checkout; this server only issues
// and activates licences:
//   POST /stripe/webhook     Stripe events (signature checked): a paid Checkout Session makes a licence for the products in it
//                            (the Stripe products' metadata "sku": a product code such as in07, or studio / live / all);
//                            a full refund marks it refunded (no new activations).
//   GET  /thanks?session_id  the page Checkout returns to: the licence key (derived from the session, never stored) and how to activate
//   POST /api/activate       {key, machine} -> {license: "<.swlicense text>"} bound to that computer; at most MAX_ACTIVATIONS
//                            computers per licence (the same computer again is free)
//   POST /api/deactivate     {key, machine} -> frees that computer's place
//                            (/api/*: CORS for any origin, the plug-in window's page calls them; OPTIONS answered)
//   POST /activate           the same as a form (key, machine) -> the .swlicense file as a download
// Secrets (wrangler secret put): STRIPE_WEBHOOK_SECRET, STRIPE_API_KEY (restricted: read Checkout Sessions and Products),
// LICENSE_KEY_SECRET (HMAC for licence keys), LICENSE_PRIVATE_KEY (Ed25519, PKCS#8, base64). Vars: LICENSE_KEY_ID, LICENSE_MAJOR,
// MAX_ACTIVATIONS. Binding: DB (D1, schema.sql).
import { importPrivateKey, keyHash, licenseKeyFor, normalizeKey, signLicense, toHex, validMachine, validSku, verifyStripeSignature } from './core.js';
import { D1Store } from './store.js';

const SECURITY_HEADERS = {
  'Cache-Control': 'no-store',
  'Referrer-Policy': 'no-referrer',            // the thanks page URL carries the session id
  'X-Content-Type-Options': 'nosniff',
  'Content-Security-Policy': "default-src 'none'; style-src 'unsafe-inline'; form-action 'self'; frame-ancestors 'none'; base-uri 'none'",
};
function json(body, status = 200, extra = {}) {
  return new Response(JSON.stringify(body), { status, headers: { 'Content-Type': 'application/json; charset=utf-8', ...SECURITY_HEADERS, ...extra } });
}
// /api/* is called from the plug-in window's page, which has no origin of its own (Origin: null): any origin, never credentials
// (the key and the machine code are in the body; nothing in a cookie)
const CORS = { 'Access-Control-Allow-Origin': '*' };
function api(body, status = 200) { return json(body, status, CORS); }
function preflight() {
  return new Response(null, { status: 204, headers: { ...SECURITY_HEADERS, ...CORS, 'Access-Control-Allow-Methods': 'POST', 'Access-Control-Allow-Headers': 'Content-Type', 'Access-Control-Max-Age': '86400' } });
}
function html(body, status = 200) {
  return new Response(body, { status, headers: { 'Content-Type': 'text/html; charset=utf-8', ...SECURITY_HEADERS } });
}
function esc(s) {
  return String(s).replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);
}
async function readBody(request, max) {
  const text = await request.text();
  if (text.length > max) throw new Error('too large');
  return text;
}
function today(nowSec) {
  return new Date(nowSec * 1000).toISOString().slice(0, 10);
}

// ---- Stripe
async function webhook(request, env, deps) {
  let payload;
  try { payload = await readBody(request, 1 << 20); } catch { return json({ error: 'too large' }, 413); }
  const v = await verifyStripeSignature(request.headers.get('stripe-signature'), payload, env.STRIPE_WEBHOOK_SECRET, deps.now());
  if (!v.ok) return json({ error: v.reason }, 400);
  let event;
  try { event = JSON.parse(payload); } catch { return json({ error: 'not json' }, 400); }
  const type = event.type, obj = event.data?.object ?? {};
  if (type === 'checkout.session.completed' || type === 'checkout.session.async_payment_succeeded') {
    // async payment methods (e.g. Konbini) complete unpaid and succeed later: only a paid session makes a licence
    if (obj.mode === 'payment' && obj.payment_status === 'paid' && typeof obj.id === 'string') await issue(obj, env, deps);
  } else if (type === 'charge.refunded') {
    if (obj.refunded === true && typeof obj.payment_intent === 'string') await deps.store.markRefunded(obj.payment_intent);   // full refunds only
  }
  if (typeof event.id === 'string') await deps.store.seenEvent(event.id);
  return json({ received: true });
}
async function issue(session, env, deps) {
  if (await deps.store.licenseBySession(session.id)) return;   // webhooks can come twice
  const skus = [...new Set((await deps.lineItemSkus(session.id)).flatMap((s) => String(s).split(',')).map((s) => s.trim().toLowerCase()).filter(validSku))];
  if (skus.length === 0) return;   // nothing licensable in this purchase
  const key = await licenseKeyFor(env.LICENSE_KEY_SECRET, session.id);
  const rnd = toHex(crypto.getRandomValues(new Uint8Array(4)));
  await deps.store.insertLicense({
    id: `L-${today(deps.now()).replace(/-/g, '')}-${rnd}`,
    key_hash: await keyHash(key),
    session_id: session.id,
    payment_intent: typeof session.payment_intent === 'string' ? session.payment_intent : null,
    products: skus.join(','),
    major: Number(env.LICENSE_MAJOR ?? 1),
    created_at: new Date(deps.now() * 1000).toISOString(),
  });
}
async function stripeLineItemSkus(env, sessionId) {
  const r = await fetch(`https://api.stripe.com/v1/checkout/sessions/${encodeURIComponent(sessionId)}/line_items?limit=100&expand[]=data.price.product`, {
    headers: { Authorization: `Bearer ${env.STRIPE_API_KEY}` },
  });
  if (!r.ok) throw new Error(`stripe line items: ${r.status}`);   // the webhook answers 500 and Stripe sends it again later
  const j = await r.json();
  return (j.data ?? []).map((li) => li.price?.product?.metadata?.sku).filter(Boolean);
}

// ---- the page Checkout returns to
async function thanks(url, env, deps) {
  const sid = url.searchParams.get('session_id') ?? '';
  if (!/^cs_[A-Za-z0-9_]{10,200}$/.test(sid)) return html(page('ライセンス', '<p>このページは購入の完了後に開きます。</p>'), 400);
  const l = await deps.store.licenseBySession(sid);
  if (!l) return html(page('ライセンスを準備しています', '<p>お支払いを確認しています。数秒後にこのページを再読み込みしてください。コンビニ払いなどは、お支払いの完了後に表示されます。</p>'), 202);
  const key = await licenseKeyFor(env.LICENSE_KEY_SECRET, sid);
  const body = `<p>ご購入ありがとうございます。ライセンスキー：</p><p class="key">${esc(key)}</p>
<p>対象：${esc(l.products)}（${esc(l.major)}.x の更新はすべて無料）</p>
<p>このキーは控えておいてください（このページは再読み込みで再表示できます）。1 本のライセンスで ${esc(env.MAX_ACTIVATIONS ?? 3)} 台まで使えます。</p>
<h2>有効化</h2><p>プラグインの画面に表示される「この PC のコード」を入力すると、ライセンスファイルをダウンロードできます。</p>
<form method="post" action="/activate"><input type="hidden" name="key" value="${esc(key)}"><input name="machine" size="70" placeholder="この PC のコード（64 文字）" pattern="[0-9a-f]{64}" required> <button>ライセンスファイルを作る</button></form>`;
  return html(page('ライセンス', body));
}
function page(title, body) {
  return `<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${esc(title)} — SW AUDIO</title>
<style>body{font:16px/1.6 system-ui,sans-serif;max-width:720px;margin:40px auto;padding:0 16px;color:#1d1d1f;background:#fff}.key{font:600 22px ui-monospace,monospace;letter-spacing:.04em}input{font:inherit}</style></head>
<body><h1>${esc(title)}</h1>${body}</body></html>`;
}

// ---- activation
async function activation(key, machine, env, deps) {
  const k = normalizeKey(key);
  if (!k || !validMachine(machine)) return { status: 400, error: 'licence key and machine code are needed' };
  const l = await deps.store.licenseByKeyHash(await keyHash(k));
  if (!l) return { status: 404, error: 'licence key not found' };
  if (l.refunded) return { status: 403, error: 'this licence was refunded' };
  const machines = await deps.store.activations(l.id);
  const max = Number(env.MAX_ACTIVATIONS ?? 3);
  if (!machines.includes(machine) && machines.length >= max) return { status: 409, error: `this licence is active on ${max} computers; deactivate one first` };
  await deps.store.addActivation(l.id, machine);
  const pk = await deps.privateKey();
  const text = await signLicense(pk, {
    keyId: Number(env.LICENSE_KEY_ID ?? 1), products: l.products.split(','), id: l.id, machine, issued: today(deps.now()), major: Number(l.major),
  });
  return { status: 200, license: text };
}
async function activateApi(request, env, deps) {
  let body;
  try { body = JSON.parse(await readBody(request, 4096)); } catch { return api({ error: 'not json' }, 400); }
  const r = await activation(body?.key, body?.machine, env, deps);
  return r.license ? api({ license: r.license }) : api({ error: r.error }, r.status);
}
async function activateForm(request, env, deps) {
  let form;
  try { form = new URLSearchParams(await readBody(request, 4096)); } catch { return html(page('有効化', '<p>入力が長すぎます。</p>'), 413); }
  const r = await activation(form.get('key'), (form.get('machine') ?? '').trim().toLowerCase(), env, deps);
  if (!r.license) return html(page('有効化できませんでした', `<p>${esc(r.error)}</p>`), r.status);
  return new Response(r.license, { headers: { 'Content-Type': 'application/octet-stream', 'Content-Disposition': 'attachment; filename="SW_AUDIO.swlicense"', ...SECURITY_HEADERS } });
}
async function deactivateApi(request, env, deps) {
  let body;
  try { body = JSON.parse(await readBody(request, 4096)); } catch { return api({ error: 'not json' }, 400); }
  const k = normalizeKey(body?.key);
  if (!k || !validMachine(body?.machine)) return api({ error: 'licence key and machine code are needed' }, 400);
  const l = await deps.store.licenseByKeyHash(await keyHash(k));
  if (!l) return api({ error: 'licence key not found' }, 404);
  return api({ removed: await deps.store.removeActivation(l.id, body.machine) });
}

export async function handle(request, env, deps) {
  const url = new URL(request.url);
  const route = `${request.method} ${url.pathname}`;
  try {
    if (route === 'POST /stripe/webhook') return await webhook(request, env, deps);
    if (route === 'GET /thanks') return await thanks(url, env, deps);
    if (route === 'POST /api/activate') return await activateApi(request, env, deps);
    if (route === 'POST /api/deactivate') return await deactivateApi(request, env, deps);
    if (route === 'OPTIONS /api/activate' || route === 'OPTIONS /api/deactivate') return preflight();
    if (route === 'POST /activate') return await activateForm(request, env, deps);
    if (route === 'GET /health') return json({ ok: true });
    return json({ error: 'not found' }, 404);
  } catch (e) {
    console.error(route, e && e.message);
    return json({ error: 'server error' }, 500, url.pathname.startsWith('/api/') ? CORS : {});   // Stripe retries a webhook that got 500
  }
}

export default {
  async fetch(request, env) {
    let pk = null;
    const deps = {
      store: new D1Store(env.DB),
      now: () => Math.floor(Date.now() / 1000),
      lineItemSkus: (sid) => stripeLineItemSkus(env, sid),
      privateKey: async () => (pk ??= await importPrivateKey(env.LICENSE_PRIVATE_KEY)),
    };
    return handle(request, env, deps);
  },
};
