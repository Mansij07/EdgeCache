import { buildApp } from './app';
import { loadConfig } from './config';
import { createPool } from './db/pool';
import { runMigrations } from './db/migrate';
import { RulesRepo } from './db/rules';
import { RedisCoordinator } from './redis/coordinator';

async function main() {
  const cfg = loadConfig();
  const pool = createPool(cfg);
  const redis = new RedisCoordinator(cfg);

  // Bring the schema up (retries until Postgres is reachable).
  await runMigrations(pool);

  // Reconcile Redis's fast-path rule hash with the durable source of truth so a
  // fresh/flushed Redis is repopulated on startup.
  try {
    const rules = await new RulesRepo(pool).list();
    await redis.reconcileAll(rules);
  } catch (e) {
    console.error('[startup] rule reconcile failed (continuing):', e);
  }

  const app = buildApp({ cfg, pool, redis });
  const server = app.listen(cfg.port, () => {
    console.log(`[control-plane] listening on :${cfg.port}`);
  });

  const shutdown = async (sig: string) => {
    console.log(`[control-plane] ${sig} received, shutting down`);
    server.close();
    await redis.close().catch(() => undefined);
    await pool.end().catch(() => undefined);
    process.exit(0);
  };
  process.on('SIGINT', () => void shutdown('SIGINT'));
  process.on('SIGTERM', () => void shutdown('SIGTERM'));
}

main().catch((err) => {
  console.error('[control-plane] fatal', err);
  process.exit(1);
});
