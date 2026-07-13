import { Router } from 'express';
import { z } from 'zod';
import { RulesRepo } from '../db/rules';
import { RedisCoordinator } from '../redis/coordinator';

const CreateRule = z.object({
  pathPattern: z.string().min(1),
  ttlSeconds: z.number().int().min(0),
  originId: z.string().uuid().nullable().optional(),
  staleWhileRevalidateSeconds: z.number().int().min(0).default(0),
});

const UpdateRule = z.object({
  pathPattern: z.string().min(1).optional(),
  ttlSeconds: z.number().int().min(0).optional(),
  originId: z.string().uuid().nullable().optional(),
  staleWhileRevalidateSeconds: z.number().int().min(0).optional(),
});

// Write path for rules: Postgres FIRST (durable), then mirror into Redis. If the
// Postgres write fails we never touch Redis, so proxies never see an
// unpersisted rule.
export function rulesRouter(repo: RulesRepo, redis: RedisCoordinator): Router {
  const r = Router();

  r.post('/', async (req, res) => {
    const parsed = CreateRule.safeParse(req.body);
    if (!parsed.success) {
      return res.status(400).json({ error: 'invalid_body', details: parsed.error.flatten() });
    }
    const rule = await repo.create({
      pathPattern: parsed.data.pathPattern,
      ttlSeconds: parsed.data.ttlSeconds,
      staleWhileRevalidateSeconds: parsed.data.staleWhileRevalidateSeconds,
      originId: parsed.data.originId ?? null,
    });
    // Best-effort mirror to the fast-path store; the durable write already succeeded.
    await redis.upsertRule(rule).catch((e) => console.error('[rules] redis mirror failed', e));
    return res.status(201).json(rule);
  });

  r.get('/', async (_req, res) => {
    return res.json(await repo.list());
  });

  r.put('/:id', async (req, res) => {
    const parsed = UpdateRule.safeParse(req.body);
    if (!parsed.success) {
      return res.status(400).json({ error: 'invalid_body', details: parsed.error.flatten() });
    }
    const updated = await repo.update(req.params.id, parsed.data);
    if (!updated) return res.status(404).json({ error: 'not_found' });
    await redis.upsertRule(updated).catch((e) => console.error('[rules] redis mirror failed', e));
    return res.json(updated);
  });

  r.delete('/:id', async (req, res) => {
    const removed = await repo.remove(req.params.id);
    if (!removed) return res.status(404).json({ error: 'not_found' });
    await redis
      .removeRule(removed.pathPattern)
      .catch((e) => console.error('[rules] redis remove failed', e));
    return res.json(removed);
  });

  return r;
}
