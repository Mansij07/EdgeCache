import { NextFunction, Request, Response } from 'express';
import { Config } from '../config';

// Admin API authentication. Every mutating/admin endpoint requires
// `Authorization: Bearer <ADMIN_API_KEY>`. In Kubernetes this key comes from a
// Secret, and the admin API is additionally isolated behind an internal-only
// Ingress/NetworkPolicy — defense in depth, not a single check.
export function requireAdmin(cfg: Config) {
  return (req: Request, res: Response, next: NextFunction): void => {
    const header = req.header('authorization') ?? '';
    const match = header.match(/^Bearer\s+(.+)$/i);
    const token = match?.[1];
    if (!token || token !== cfg.adminApiKey) {
      res.status(401).json({ error: 'unauthorized' });
      return;
    }
    next();
  };
}
