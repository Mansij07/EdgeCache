import { Router } from 'express';
import { z } from 'zod';
import { OriginsRepo } from '../db/origins';

const CreateOrigin = z.object({
  host: z.string().min(1),
  baseUrl: z.string().url(),
  healthCheckPath: z.string().default('/'),
});

export function originsRouter(repo: OriginsRepo): Router {
  const r = Router();

  r.post('/', async (req, res) => {
    const parsed = CreateOrigin.safeParse(req.body);
    if (!parsed.success) {
      return res.status(400).json({ error: 'invalid_body', details: parsed.error.flatten() });
    }
    const origin = await repo.create(parsed.data);
    return res.status(201).json(origin);
  });

  r.get('/', async (_req, res) => {
    return res.json(await repo.list());
  });

  return r;
}
