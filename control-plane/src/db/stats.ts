import { Pool } from 'pg';

export interface StatRow {
  path: string;
  dateHour: string;
  hits: number;
  misses: number;
  bytesServed: number;
}

export interface StatsQuery {
  path?: string;
  from?: string; // ISO timestamp
  to?: string;
}

export class StatsRepo {
  constructor(private pool: Pool) {}

  async query(q: StatsQuery): Promise<StatRow[]> {
    const clauses: string[] = [];
    const params: any[] = [];
    if (q.path) {
      params.push(q.path);
      clauses.push(`path = $${params.length}`);
    }
    if (q.from) {
      params.push(q.from);
      clauses.push(`date_hour >= $${params.length}`);
    }
    if (q.to) {
      params.push(q.to);
      clauses.push(`date_hour <= $${params.length}`);
    }
    const where = clauses.length ? `WHERE ${clauses.join(' AND ')}` : '';
    const { rows } = await this.pool.query(
      `SELECT path, date_hour, hits, misses, bytes_served
       FROM access_stats_rollup
       ${where}
       ORDER BY date_hour DESC
       LIMIT 1000`,
      params
    );
    return rows.map((r) => ({
      path: r.path,
      dateHour: r.date_hour,
      hits: Number(r.hits),
      misses: Number(r.misses),
      bytesServed: Number(r.bytes_served),
    }));
  }

  // Aggregate totals across the selected window (for a quick summary view).
  async summary(q: StatsQuery): Promise<{
    hits: number;
    misses: number;
    bytesServed: number;
    hitRate: number;
  }> {
    const rows = await this.query(q);
    const hits = rows.reduce((a, r) => a + r.hits, 0);
    const misses = rows.reduce((a, r) => a + r.misses, 0);
    const bytesServed = rows.reduce((a, r) => a + r.bytesServed, 0);
    const total = hits + misses;
    return { hits, misses, bytesServed, hitRate: total ? hits / total : 0 };
  }
}
