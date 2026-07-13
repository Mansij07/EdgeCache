import { Pool } from 'pg';

// Idempotent schema bootstrap. Runs on control-plane startup and retries until
// Postgres is reachable, so `docker compose up` works without an ordering hack.
const SCHEMA = `
CREATE EXTENSION IF NOT EXISTS "pgcrypto";

CREATE TABLE IF NOT EXISTS origins (
  id                 UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  host               TEXT NOT NULL,
  base_url           TEXT NOT NULL,
  health_check_path  TEXT NOT NULL DEFAULT '/',
  created_at         TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS cache_rules (
  id                              UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  path_pattern                    TEXT NOT NULL,
  ttl_seconds                     INT  NOT NULL,
  stale_while_revalidate_seconds  INT  NOT NULL DEFAULT 0,
  origin_id                       UUID REFERENCES origins(id) ON DELETE SET NULL,
  created_at                      TIMESTAMPTZ NOT NULL DEFAULT now(),
  updated_at                      TIMESTAMPTZ NOT NULL DEFAULT now()
);
CREATE INDEX IF NOT EXISTS idx_cache_rules_path_pattern ON cache_rules(path_pattern);

CREATE TABLE IF NOT EXISTS purge_log (
  id            UUID PRIMARY KEY DEFAULT gen_random_uuid(),
  pattern       TEXT NOT NULL,
  requested_by  TEXT NOT NULL DEFAULT 'unknown',
  requested_at  TIMESTAMPTZ NOT NULL DEFAULT now()
);

CREATE TABLE IF NOT EXISTS access_stats_rollup (
  path          TEXT NOT NULL,
  date_hour     TIMESTAMPTZ NOT NULL,
  hits          BIGINT NOT NULL DEFAULT 0,
  misses        BIGINT NOT NULL DEFAULT 0,
  bytes_served  BIGINT NOT NULL DEFAULT 0,
  PRIMARY KEY (path, date_hour)
);
`;

export async function runMigrations(pool: Pool): Promise<void> {
  const maxAttempts = 30;
  for (let attempt = 1; attempt <= maxAttempts; attempt++) {
    try {
      await pool.query(SCHEMA);
      console.log('[db] schema ready');
      return;
    } catch (err) {
      const msg = err instanceof Error ? err.message : String(err);
      console.warn(`[db] migration attempt ${attempt}/${maxAttempts} failed: ${msg}`);
      await new Promise((r) => setTimeout(r, 2000));
    }
  }
  throw new Error('[db] could not run migrations after retries');
}
