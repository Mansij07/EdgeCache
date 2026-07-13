import { Router } from 'express';
import { z } from 'zod';
import { PurgeRepo } from '../db/purge';
import { RedisCoordinator } from '../redis/coordinator';

const PurgeBody = z.object({
  pattern: z.string().min(1),
});

// The headline endpoint. Order matters:
//   1. Write the audit entry to Postgres (durable trail of who purged what).
//   2. Publish the pattern to Redis, which fans out to every proxy replica.
// A replica evicts matching keys from its local cache in milliseconds — no
// waiting for TTL expiry, no shared cache store.
export function purgeRouter(repo: PurgeRepo, redis: RedisCoordinator): Router {
  const r = Router();

  r.post('/', async (req, res) => {
    const parsed = PurgeBody.safeParse(req.body);
    if (!parsed.success) {
      return res.status(400).json({ error: 'invalid_body', details: parsed.error.flatten() });
    }
    const requestedBy =
      (req.header('x-actor') as string | undefined) ?? req.ip ?? 'unknown';

    // 1. Durable audit first.
    const entry = await repo.log(parsed.data.pattern, requestedBy);

    // 2. Evict the shared L2 tier BEFORE the L1 fan-out, so a replica can't
    //    re-promote a just-evicted L1 key from a stale L2 copy. Best-effort.
    let l2Evicted = 0;
    try {
      l2Evicted = await redis.purgeL2(parsed.data.pattern);
    } catch (e) {
      console.error('[purge] L2 eviction failed (continuing)', e);
    }

    // 3. Fan out to the fleet. subscribers = number of proxies that received it.
    const subscribers = await redis.publishPurge(parsed.data.pattern);

    return res.status(202).json({
      purged: parsed.data.pattern,
      auditId: entry.id,
      subscribersNotified: subscribers,
      l2KeysEvicted: l2Evicted,
    });
  });

  r.get('/log', async (_req, res) => {
    return res.json(await repo.recent());
  });

  return r;
}
