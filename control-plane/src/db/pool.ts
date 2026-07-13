import { Pool } from 'pg';
import { Config } from '../config';

// A single shared connection pool for the process. Postgres is the durable
// source of truth for rules/origins/purge history — Redis is a fast-path cache
// in front of it, not the primary store.
export function createPool(cfg: Config): Pool {
  const pool = new Pool({
    connectionString: cfg.databaseUrl,
    max: 10,
    idleTimeoutMillis: 30_000,
    connectionTimeoutMillis: 5_000,
  });
  pool.on('error', (err) => {
    // Background idle-client errors shouldn't crash the process.
    console.error('[db] idle client error', err.message);
  });
  return pool;
}
