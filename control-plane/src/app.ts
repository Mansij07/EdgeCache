import express, { Express, NextFunction, Request, Response } from 'express';
import { Pool } from 'pg';
import { Config } from './config';
import { OriginsRepo } from './db/origins';
import { PurgeRepo } from './db/purge';
import { RulesRepo } from './db/rules';
import { StatsRepo } from './db/stats';
import { requireAdmin } from './middleware/auth';
import { RedisCoordinator } from './redis/coordinator';
import { originsRouter } from './routes/origins';
import { purgeRouter } from './routes/purge';
import { rulesRouter } from './routes/rules';
import { statsRouter } from './routes/stats';

export interface Deps {
  cfg: Config;
  pool: Pool;
  redis: RedisCoordinator;
}

export function buildApp(deps: Deps): Express {
  const { cfg, pool, redis } = deps;
  const app = express();
  app.use(express.json({ limit: '256kb' }));

  // Lightweight request logging.
  app.use((req: Request, _res: Response, next: NextFunction) => {
    if (req.path !== '/health') console.log(`[api] ${req.method} ${req.path}`);
    next();
  });

  const origins = new OriginsRepo(pool);
  const rules = new RulesRepo(pool);
  const purge = new PurgeRepo(pool);
  const stats = new StatsRepo(pool);

  // Health is public (used by orchestrators). It reports dependency state but
  // stays 200 as long as the process itself is up.
  app.get('/health', async (_req, res) => {
    let db = false;
    try {
      await pool.query('SELECT 1');
      db = true;
    } catch {
      db = false;
    }
    const redisOk = await redis.ping();
    res.json({ status: 'ok', dependencies: { postgres: db, redis: redisOk } });
  });

  // Everything else is admin-authenticated.
  const admin = requireAdmin(cfg);
  app.use('/origins', admin, originsRouter(origins));
  app.use('/rules', admin, rulesRouter(rules, redis));
  app.use('/purge', admin, purgeRouter(purge, redis));
  app.use('/stats', admin, statsRouter(stats));

  // Central error handler so a thrown DB error becomes a clean 500, not a hang.
  app.use((err: Error, _req: Request, res: Response, _next: NextFunction) => {
    console.error('[api] unhandled error', err);
    res.status(500).json({ error: 'internal_error', message: err.message });
  });

  return app;
}
