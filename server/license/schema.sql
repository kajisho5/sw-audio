-- SW AUDIO licence server (Cloudflare D1). Apply with: npx wrangler d1 execute sw-licences --file schema.sql
-- No names, e-mail addresses or licence keys are stored (Stripe keeps the customer; the key is kept only as its SHA-256).
CREATE TABLE IF NOT EXISTS licenses (
  id TEXT PRIMARY KEY,               -- the licence id written into licence files (L-YYYYMMDD-xxxxxxxx)
  key_hash TEXT NOT NULL UNIQUE,     -- SHA-256 of the licence key (the key is derived from the session, never stored)
  session_id TEXT NOT NULL UNIQUE,   -- the Stripe Checkout Session it came from
  payment_intent TEXT,               -- for refunds
  products TEXT NOT NULL,            -- comma-separated product codes / bundles (from the Stripe products' metadata "sku")
  major INTEGER NOT NULL,            -- the major version it covers (perpetual: every update of that major version)
  created_at TEXT NOT NULL,
  refunded INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX IF NOT EXISTS licenses_payment_intent ON licenses (payment_intent);
CREATE TABLE IF NOT EXISTS activations (
  license_id TEXT NOT NULL,
  machine TEXT NOT NULL,             -- the plug-in's salted machine hash (64 hex)
  created_at TEXT NOT NULL,
  PRIMARY KEY (license_id, machine)
);
CREATE TABLE IF NOT EXISTS deactivations (   -- the computers freed by the owner (self-service: a few a year, see MAX_DEACTIVATIONS)
  license_id TEXT NOT NULL,
  machine TEXT NOT NULL,
  created_at TEXT NOT NULL
);
CREATE INDEX IF NOT EXISTS deactivations_license ON deactivations (license_id, created_at);
CREATE TABLE IF NOT EXISTS events (  -- Stripe event ids already handled (webhooks can arrive twice)
  id TEXT PRIMARY KEY,
  created_at TEXT NOT NULL
);
