import { Router } from 'express';
import { StatsRepo } from '../db/stats';

// Read-only aggregated stats produced by the analytics consumer's rollups.
// GET /stats?path=&from=&to=
export function statsRouter(repo: StatsRepo): Router {
  const r = Router();

  r.get('/', async (req, res) => {
    const q = {
      path: (req.query.path as string) || undefined,
      from: (req.query.from as string) || undefined,
      to: (req.query.to as string) || undefined,
    };
    const [rows, summary] = await Promise.all([repo.query(q), repo.summary(q)]);
    return res.json({ summary, rows });
  });

  return r;
}
