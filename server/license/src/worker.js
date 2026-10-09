// SW AUDIO licence server (Cloudflare Workers + D1). Sales are perpetual (買い切り) through Stripe Checkout; this server only issues
// and activates licences:
//   POST /stripe/webhook     Stripe events (signature checked): a paid Checkout Session makes a licence for the products in it
//                            (the Stripe products' metadata "sku": a product code such as in07, or studio / live / all);
//                            a full refund marks it refunded (no new activations).
//   GET  /thanks?session_id  the page Checkout returns to: the licence key (derived from the session, never stored) and how to activate
//   POST /api/activate       {key, machine} -> {license: "<.swlicense text>"} bound to that computer; at most MAX_ACTIVATIONS
//                            computers per licence (the same computer again is free)
//   POST /api/deactivate     {key, machine} -> frees that computer's place (at most MAX_DEACTIVATIONS per licence in 365 days:
//                            a licence file keeps working offline, so freeing places without a limit would let one key serve any
//                            number of computers; more than that goes through support)
//                            (/api/*: CORS for any origin, the plug-in window's page calls them; OPTIONS answered)
//                            errors: {error, code}: input, not_found, refunded, max (all places taken), limit (no more freeing this year)
//   GET  /activate           a form (key, this computer's code) for a computer without the internet: activated on another one
//   POST /activate           the same as a form (key, machine) -> the .swlicense file as a download
//   GET  /manage             a form for the key; POST /manage: the computers it is active on, each with a button to free it
//   POST /deactivate         the form behind those buttons (key, machine), the same limit as /api/deactivate
// Secrets (wrangler secret put): STRIPE_WEBHOOK_SECRET, STRIPE_API_KEY (restricted: read Checkout Sessions and Products),
// LICENSE_KEY_SECRET (HMAC for licence keys), LICENSE_PRIVATE_KEY (Ed25519, PKCS#8, base64). Vars: LICENSE_KEY_ID, LICENSE_MAJOR,
// MAX_ACTIVATIONS, MAX_DEACTIVATIONS. Binding: DB (D1, schema.sql).
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
<h2>有効化</h2><p>プラグインの画面右上の TRIAL を押し、LICENCE KEY にこのキーを入れて ACTIVATE。インターネットにつながらないパソコンは、そのパソコンの画面に出る「THIS COMPUTER」のコードをここに入れると、ライセンスファイルをダウンロードできます（LICENCE FILE で読み込みます）。</p>
<form method="post" action="/activate"><input type="hidden" name="key" value="${esc(key)}"><input name="machine" size="70" placeholder="THIS COMPUTER のコード（64 文字）" pattern="[0-9a-fA-F]{64}" required> <button>ライセンスファイルを作る</button></form>
<p>使わなくなったパソコンの解除は <a href="/manage">/manage</a>（1 年に ${esc(env.MAX_DEACTIVATIONS ?? 3)} 回まで）。</p>`;
  return html(page('ライセンス', body));
}
function page(title, body) {
  return `<!doctype html><html lang="ja"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>${esc(title)} — SW AUDIO</title>
<style>body{font:16px/1.6 system-ui,sans-serif;max-width:720px;margin:40px auto;padding:0 16px;color:#1d1d1f;background:#fff}.key{font:600 22px ui-monospace,monospace;letter-spacing:.04em}input,button{font:inherit}table{border-collapse:collapse}td,th{border:1px solid #d5e2dd;padding:6px 10px;text-align:left}.m{font-family:ui-monospace,monospace}.ok{color:#1d6b3a}.err{color:#a3261b}</style></head>
<body><h1>${esc(title)}</h1>${body}</body></html>`;
}

// ---- activation
async function licenceFor(key, machine, deps) {
  const k = normalizeKey(key);
  if (!k || (machine !== undefined && !validMachine(machine))) return { status: 400, code: 'input', error: 'licence key and machine code are needed' };
  const l = await deps.store.licenseByKeyHash(await keyHash(k));
  if (!l) return { status: 404, code: 'not_found', error: 'licence key not found' };
  return { licence: l, key: k };
}
async function activation(key, machine, env, deps) {
  const f = await licenceFor(key, machine, deps);
  if (!f.licence) return f;
  const l = f.licence;
  if (l.refunded) return { status: 403, code: 'refunded', error: 'this licence was refunded' };
  const machines = await deps.store.activations(l.id);
  const max = Number(env.MAX_ACTIVATIONS ?? 3);
  if (!machines.includes(machine) && machines.length >= max) {
    return { status: 409, code: 'max', error: `this licence is active on ${max} computers: free one you no longer use at /manage, or contact support` };
  }
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
  return r.license ? api({ license: r.license }) : api({ error: r.error, code: r.code }, r.status);
}
function activateFormPage(env) {
  return html(page('有効化（ライセンスファイル）', `<p>インターネットにつながらないパソコンで使うときは、ここでライセンスキーと、そのパソコンのプラグインの画面（LICENCE）に出る「THIS COMPUTER」のコードを入れて、ライセンスファイルを作ります。できたファイルをそのパソコンへ移し、LICENCE FILE で読み込んでください。</p>
<form method="post" action="/activate"><p><input name="key" size="34" placeholder="SWL-XXXXX-XXXXX-XXXXX-XXXXX" required></p>
<p><input name="machine" size="70" placeholder="THIS COMPUTER のコード（64 文字）" pattern="[0-9a-fA-F]{64}" required></p><p><button>ライセンスファイルを作る</button></p></form>
<p>1 本のライセンスで ${esc(env.MAX_ACTIVATIONS ?? 3)} 台まで使えます。使わなくなったパソコンの解除は <a href="/manage">/manage</a>。</p>`));
}
async function activateForm(request, env, deps) {
  let form;
  try { form = new URLSearchParams(await readBody(request, 4096)); } catch { return html(page('有効化', '<p>入力が長すぎます。</p>'), 413); }
  const r = await activation(form.get('key'), (form.get('machine') ?? '').trim().toLowerCase(), env, deps);
  if (!r.license) return html(page('有効化できませんでした', `<p>${esc(r.error)}</p>`), r.status);
  return new Response(r.license, { headers: { 'Content-Type': 'application/octet-stream', 'Content-Disposition': 'attachment; filename="SW_AUDIO.swlicense"', ...SECURITY_HEADERS } });
}
// ---- freeing a computer's place: a few a year by the owner (the licence file on that computer keeps working offline: without a
// limit, activate -> keep the file -> free -> activate elsewhere would make one key serve any number of computers)
function sinceIso(deps) { return new Date((deps.now() - 365 * 86400) * 1000).toISOString(); }
async function deactivation(key, machine, env, deps) {
  const f = await licenceFor(key, machine, deps);
  if (!f.licence) return f;
  const l = f.licence;
  const machines = await deps.store.activations(l.id);
  if (!machines.includes(machine)) return { status: 200, removed: false, remaining: await remainingFrees(l, env, deps) };
  const left = await remainingFrees(l, env, deps);
  if (left <= 0) return { status: 429, code: 'limit', error: 'no more computers can be freed this year: contact support' };
  const removed = await deps.store.removeActivation(l.id, machine);
  if (removed) await deps.store.addDeactivation(l.id, machine, new Date(deps.now() * 1000).toISOString());
  return { status: 200, removed, remaining: left - (removed ? 1 : 0) };
}
async function remainingFrees(l, env, deps) {
  return Math.max(0, Number(env.MAX_DEACTIVATIONS ?? 3) - (await deps.store.deactivationsSince(l.id, sinceIso(deps))));
}
async function deactivateApi(request, env, deps) {
  let body;
  try { body = JSON.parse(await readBody(request, 4096)); } catch { return api({ error: 'not json', code: 'input' }, 400); }
  const r = await deactivation(body?.key, body?.machine, env, deps);
  return r.error ? api({ error: r.error, code: r.code }, r.status) : api({ removed: r.removed, remaining: r.remaining });
}
function manageFormPage() {
  return html(page('有効化したパソコン', `<p>ライセンスキーを入れると、このライセンスを有効化したパソコンの一覧が出ます。使わなくなったパソコン（買い替え、故障など）の分を解除すると、別のパソコンで有効化できます。</p>
<form method="post" action="/manage"><p><input name="key" size="34" placeholder="SWL-XXXXX-XXXXX-XXXXX-XXXXX" required> <button>一覧を見る</button></p></form>`));
}
async function manageList(key, env, deps, note = '') {
  const f = await licenceFor(key, undefined, deps);
  if (!f.licence) return html(page('有効化したパソコン', `<p>${f.status === 404 ? 'このライセンスキーは見つかりません。' : 'ライセンスキーを入れてください。'}</p><p><a href="/manage">戻る</a></p>`), f.status);
  const l = f.licence;
  const list = await deps.store.activationList(l.id);
  const left = await remainingFrees(l, env, deps);
  const max = Number(env.MAX_ACTIVATIONS ?? 3);
  const rows = list.map((a) => `<tr><td class="m">${esc(a.machine.slice(0, 8))}…${esc(a.machine.slice(-4))}</td><td>${esc(String(a.created_at).slice(0, 10))}</td><td>${left > 0
    ? `<form method="post" action="/deactivate"><input type="hidden" name="key" value="${esc(f.key)}"><input type="hidden" name="machine" value="${esc(a.machine)}"><button>解除</button></form>` : ''}</td></tr>`).join('');
  return html(page('有効化したパソコン', `${note}<p>${esc(list.length)} / ${esc(max)} 台。コードはプラグインの画面（LICENCE）の「THIS COMPUTER」の最初と最後の文字です。</p>
${list.length ? `<table><tr><th>パソコンのコード</th><th>有効化した日</th><th></th></tr>${rows}</table>` : '<p>まだどのパソコンでも有効化されていません。</p>'}
<p>解除できるのは 1 年に ${esc(Number(env.MAX_DEACTIVATIONS ?? 3))} 回まで（あと ${esc(left)} 回）。それより多く必要なときはお問い合わせください。解除したパソコンでは、ライセンスファイルを消してください（ライセンスの規約）。</p>`));
}
async function manageForm(request, env, deps) {
  let form;
  try { form = new URLSearchParams(await readBody(request, 4096)); } catch { return html(page('有効化したパソコン', '<p>入力が長すぎます。</p>'), 413); }
  return manageList(form.get('key'), env, deps);
}
async function deactivateForm(request, env, deps) {
  let form;
  try { form = new URLSearchParams(await readBody(request, 4096)); } catch { return html(page('解除', '<p>入力が長すぎます。</p>'), 413); }
  const machine = (form.get('machine') ?? '').trim().toLowerCase();
  const r = await deactivation(form.get('key'), machine, env, deps);
  if (r.error && r.code !== 'limit') return html(page('解除できませんでした', `<p>${r.status === 404 ? 'このライセンスキーは見つかりません。' : '入力を確かめてください。'}</p><p><a href="/manage">戻る</a></p>`), r.status);
  const note = r.code === 'limit' ? '<p class="err">今年はこれ以上解除できません。お問い合わせください。</p>'
    : r.removed ? `<p class="ok">解除しました（${esc(machine.slice(0, 8))}…）。</p>` : '<p>そのパソコンは一覧にありません。</p>';
  const out = await manageList(form.get('key'), env, deps, note);
  return r.code === 'limit' ? new Response(out.body, { status: 429, headers: out.headers }) : out;
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
    if (route === 'GET /activate') return activateFormPage(env);
    if (route === 'POST /activate') return await activateForm(request, env, deps);
    if (route === 'GET /manage') return manageFormPage();
    if (route === 'POST /manage') return await manageForm(request, env, deps);
    if (route === 'POST /deactivate') return await deactivateForm(request, env, deps);
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
