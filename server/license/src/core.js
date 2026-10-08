// SW AUDIO licence server — the parts that touch neither the network nor the database (tested with `node --test`).
// Runs on Cloudflare Workers and on Node 20+ (both have WebCrypto with Ed25519 and HMAC).

const enc = new TextEncoder();

export function toHex(buf) {
  return [...new Uint8Array(buf)].map((b) => b.toString(16).padStart(2, '0')).join('');
}
export function toB64(buf) {
  let s = '';
  for (const b of new Uint8Array(buf)) s += String.fromCharCode(b);
  return btoa(s);
}
export function fromB64(str) {
  const s = atob(str);
  const out = new Uint8Array(s.length);
  for (let i = 0; i < s.length; i++) out[i] = s.charCodeAt(i);
  return out;
}
// a comparison whose time does not depend on where the inputs differ
export function timingSafeEqual(a, b) {
  if (a.length !== b.length) return false;
  let d = 0;
  for (let i = 0; i < a.length; i++) d |= a[i] ^ b[i];
  return d === 0;
}

async function hmacSha256(key, message) {
  const k = await crypto.subtle.importKey('raw', enc.encode(key), { name: 'HMAC', hash: 'SHA-256' }, false, ['sign']);
  return new Uint8Array(await crypto.subtle.sign('HMAC', k, enc.encode(message)));
}
export async function sha256Hex(text) {
  return toHex(await crypto.subtle.digest('SHA-256', enc.encode(text)));
}

// ---- Stripe webhooks: header "t=<unix>,v1=<hex>[,v1=<hex>...]"; the signed text is "<t>.<raw body>", HMAC-SHA256 with the
// endpoint's signing secret (the whole "whsec_..." string). Events older or newer than the tolerance are refused (replays).
export async function verifyStripeSignature(header, payload, secret, nowSec, toleranceSec = 300) {
  if (!header || !secret) return { ok: false, reason: 'missing signature' };
  let t = null;
  const v1 = [];
  for (const part of header.split(',')) {
    const i = part.indexOf('=');
    if (i < 0) continue;
    const k = part.slice(0, i).trim(), v = part.slice(i + 1).trim();
    if (k === 't' && /^\d{1,12}$/.test(v)) t = Number(v);
    if (k === 'v1' && /^[0-9a-f]{64}$/.test(v)) v1.push(v);
  }
  if (t === null || v1.length === 0) return { ok: false, reason: 'malformed signature' };
  if (Math.abs(nowSec - t) > toleranceSec) return { ok: false, reason: 'timestamp outside the tolerance' };
  const expected = enc.encode(toHex(await hmacSha256(secret, `${t}.${payload}`)));
  for (const s of v1) if (timingSafeEqual(enc.encode(s), expected)) return { ok: true };
  return { ok: false, reason: 'signature mismatch' };
}

// ---- licence keys. A key is derived from the Checkout Session with a server secret, so the thanks page can show it again and the
// database never holds it (only its SHA-256). Format: SWL-XXXXX-XXXXX-XXXXX-XXXXX (Crockford base32, 100 bits).
const B32 = '0123456789ABCDEFGHJKMNPQRSTVWXYZ';
export async function licenseKeyFor(secret, sessionId) {
  const h = await hmacSha256(secret, `licence-key:${sessionId}`);
  let bits = 0n;
  for (let i = 0; i < 13; i++) bits = (bits << 8n) | BigInt(h[i]);   // 104 bits, 100 used
  bits >>= 4n;
  let s = '';
  for (let i = 0; i < 20; i++) { s = B32[Number(bits & 31n)] + s; bits >>= 5n; }
  return `SWL-${s.slice(0, 5)}-${s.slice(5, 10)}-${s.slice(10, 15)}-${s.slice(15, 20)}`;
}
// what a person types: case, spaces and the usual look-alikes (O/0, I/L/1) do not matter; null when it is not a key
export function normalizeKey(input) {
  if (typeof input !== 'string' || input.length > 64) return null;
  const s = input.toUpperCase().replace(/[\s]/g, '').replace(/O/g, '0').replace(/[IL]/g, '1');
  const m = /^SW1-?([0-9A-Z]{5})-?([0-9A-Z]{5})-?([0-9A-Z]{5})-?([0-9A-Z]{5})$/.exec(s.replace(/^SWL/, 'SW1'));
  if (!m) return null;
  const body = m.slice(1).join('');
  for (const c of body) if (!B32.includes(c)) return null;
  return `SWL-${m[1]}-${m[2]}-${m[3]}-${m[4]}`;
}
export function keyHash(normalizedKey) {
  return sha256Hex(`swl:${normalizedKey}`);
}

export function validMachine(m) {
  return typeof m === 'string' && /^[0-9a-f]{64}$/.test(m);
}
export function validSku(s) {
  return typeof s === 'string' && /^[a-z0-9-]{2,32}$/.test(s);
}
export function validLicenseId(s) {
  return typeof s === 'string' && /^[A-Za-z0-9._-]{1,64}$/.test(s);
}

// ---- the licence file: the same text core/src/license.cpp checks (Ed25519 over every byte before the sig line)
export function licenseMessage({ keyId, products, id, machine, issued, major }) {
  if (!Number.isInteger(keyId) || keyId < 1 || keyId > 9999) throw new Error('key id');
  if (!Array.isArray(products) || products.length === 0 || !products.every(validSku)) throw new Error('products');
  if (!validLicenseId(id)) throw new Error('licence id');
  if (machine && !validMachine(machine)) throw new Error('machine');
  if (!/^\d{4}-\d{2}-\d{2}$/.test(issued)) throw new Error('issued');
  if (!Number.isInteger(major) || major < 0 || major > 999) throw new Error('major');
  return `SW-LICENSE 1\nkey=${keyId}\nproduct=${products.join(',')}\nlicense=${id}\n` + (machine ? `machine=${machine}\n` : '') +
    `issued=${issued}\nmajor=${major}\n`;
}
export async function importPrivateKey(pkcs8B64) {
  return crypto.subtle.importKey('pkcs8', fromB64(pkcs8B64), { name: 'Ed25519' }, false, ['sign']);
}
export async function signLicense(privateKey, fields) {
  const m = licenseMessage(fields);
  const sig = await crypto.subtle.sign({ name: 'Ed25519' }, privateKey, enc.encode(m));
  return `${m}sig=${toB64(sig)}\n`;
}
