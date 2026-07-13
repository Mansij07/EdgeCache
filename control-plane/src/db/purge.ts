import { Pool } from 'pg';

export interface PurgeLogEntry {
  id: string;
  pattern: string;
  requestedBy: string;
  requestedAt: string;
}

export class PurgeRepo {
  constructor(private pool: Pool) {}

  // Write the audit entry FIRST (durable trail), before publishing to Redis.
  async log(pattern: string, requestedBy: string): Promise<PurgeLogEntry> {
    const { rows } = await this.pool.query(
      `INSERT INTO purge_log (pattern, requested_by)
       VALUES ($1, $2)
       RETURNING *`,
      [pattern, requestedBy]
    );
    const r = rows[0];
    return {
      id: r.id,
      pattern: r.pattern,
      requestedBy: r.requested_by,
      requestedAt: r.requested_at,
    };
  }

  async recent(limit = 50): Promise<PurgeLogEntry[]> {
    const { rows } = await this.pool.query(
      'SELECT * FROM purge_log ORDER BY requested_at DESC LIMIT $1',
      [limit]
    );
    return rows.map((r) => ({
      id: r.id,
      pattern: r.pattern,
      requestedBy: r.requested_by,
      requestedAt: r.requested_at,
    }));
  }
}
