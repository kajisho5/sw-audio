// SW AUDIO licence server — storage. D1 (Cloudflare's SQLite) in production; MemoryStore for the tests. Both have the same methods.
// Nothing personal is stored: no names, no e-mail addresses (Stripe keeps those), no licence keys (only their SHA-256), and the
// machines only as the plug-ins' salted hashes.

export class D1Store {
  constructor(db) { this.db = db; }
  async seenEvent(id) {
    const r = await this.db.prepare('INSERT OR IGNORE INTO events (id, created_at) VALUES (?, ?)').bind(id, new Date().toISOString()).run();
    return (r.meta?.changes ?? 0) === 0;   // true when it was already there
  }
  async licenseBySession(sessionId) {
    return this.db.prepare('SELECT * FROM licenses WHERE session_id = ?').bind(sessionId).first();
  }
  async licenseByKeyHash(h) {
    return this.db.prepare('SELECT * FROM licenses WHERE key_hash = ?').bind(h).first();
  }
  async insertLicense(l) {
    await this.db.prepare('INSERT OR IGNORE INTO licenses (id, key_hash, session_id, payment_intent, products, major, created_at, refunded) VALUES (?, ?, ?, ?, ?, ?, ?, 0)')
      .bind(l.id, l.key_hash, l.session_id, l.payment_intent ?? null, l.products, l.major, l.created_at).run();
  }
  async markRefunded(paymentIntent) {
    await this.db.prepare('UPDATE licenses SET refunded = 1 WHERE payment_intent = ?').bind(paymentIntent).run();
  }
  async activations(licenseId) {
    return (await this.activationList(licenseId)).map((x) => x.machine);
  }
  async activationList(licenseId) {   // [{machine, created_at}], oldest first
    const r = await this.db.prepare('SELECT machine, created_at FROM activations WHERE license_id = ? ORDER BY created_at').bind(licenseId).all();
    return r.results ?? [];
  }
  async addActivation(licenseId, machine) {
    await this.db.prepare('INSERT OR IGNORE INTO activations (license_id, machine, created_at) VALUES (?, ?, ?)').bind(licenseId, machine, new Date().toISOString()).run();
  }
  async removeActivation(licenseId, machine) {
    const r = await this.db.prepare('DELETE FROM activations WHERE license_id = ? AND machine = ?').bind(licenseId, machine).run();
    return (r.meta?.changes ?? 0) > 0;
  }
  async deactivationsSince(licenseId, isoSince) {
    const r = await this.db.prepare('SELECT COUNT(*) AS n FROM deactivations WHERE license_id = ? AND created_at >= ?').bind(licenseId, isoSince).first();
    return Number(r?.n ?? 0);
  }
  async addDeactivation(licenseId, machine, iso) {
    await this.db.prepare('INSERT INTO deactivations (license_id, machine, created_at) VALUES (?, ?, ?)').bind(licenseId, machine, iso).run();
  }
}

export class MemoryStore {
  constructor() { this.events = new Set(); this.licenses = []; this.acts = []; this.deacts = []; this.clock = 0; }
  async seenEvent(id) { if (this.events.has(id)) return true; this.events.add(id); return false; }
  async licenseBySession(s) { return this.licenses.find((l) => l.session_id === s) ?? null; }
  async licenseByKeyHash(h) { return this.licenses.find((l) => l.key_hash === h) ?? null; }
  async insertLicense(l) { if (!this.licenses.some((x) => x.session_id === l.session_id || x.id === l.id)) this.licenses.push({ ...l, refunded: 0 }); }
  async markRefunded(pi) { for (const l of this.licenses) if (l.payment_intent === pi) l.refunded = 1; }
  async activations(id) { return (await this.activationList(id)).map((a) => a.machine); }
  async activationList(id) { return this.acts.filter((a) => a.license_id === id).map((a) => ({ machine: a.machine, created_at: a.created_at })); }
  async addActivation(id, m) {
    if (!this.acts.some((a) => a.license_id === id && a.machine === m)) this.acts.push({ license_id: id, machine: m, created_at: new Date(Date.UTC(2026, 9, 1) + this.clock++ * 1000).toISOString() });
  }
  async removeActivation(id, m) { const n = this.acts.length; this.acts = this.acts.filter((a) => !(a.license_id === id && a.machine === m)); return this.acts.length < n; }
  async deactivationsSince(id, iso) { return this.deacts.filter((d) => d.license_id === id && d.created_at >= iso).length; }
  async addDeactivation(id, m, iso) { this.deacts.push({ license_id: id, machine: m, created_at: iso }); }
}
