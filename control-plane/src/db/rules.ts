import { Pool } from 'pg';

export interface Rule {
  id: string;
  pathPattern: string;
  ttlSeconds: number;
  staleWhileRevalidateSeconds: number;
  originId: string | null;
  createdAt: string;
  updatedAt: string;
}

function mapRow(r: any): Rule {
  return {
    id: r.id,
    pathPattern: r.path_pattern,
    ttlSeconds: r.ttl_seconds,
    staleWhileRevalidateSeconds: r.stale_while_revalidate_seconds,
    originId: r.origin_id,
    createdAt: r.created_at,
    updatedAt: r.updated_at,
  };
}

export class RulesRepo {
  constructor(private pool: Pool) {}

  async create(input: {
    pathPattern: string;
    ttlSeconds: number;
    staleWhileRevalidateSeconds: number;
    originId: string | null;
  }): Promise<Rule> {
    const { rows } = await this.pool.query(
      `INSERT INTO cache_rules (path_pattern, ttl_seconds, stale_while_revalidate_seconds, origin_id)
       VALUES ($1, $2, $3, $4)
       RETURNING *`,
      [input.pathPattern, input.ttlSeconds, input.staleWhileRevalidateSeconds, input.originId]
    );
    return mapRow(rows[0]);
  }

  async list(): Promise<Rule[]> {
    const { rows } = await this.pool.query(
      'SELECT * FROM cache_rules ORDER BY length(path_pattern) DESC, created_at DESC'
    );
    return rows.map(mapRow);
  }

  async get(id: string): Promise<Rule | null> {
    const { rows } = await this.pool.query('SELECT * FROM cache_rules WHERE id = $1', [id]);
    return rows[0] ? mapRow(rows[0]) : null;
  }

  async update(
    id: string,
    patch: Partial<{
      pathPattern: string;
      ttlSeconds: number;
      staleWhileRevalidateSeconds: number;
      originId: string | null;
    }>
  ): Promise<Rule | null> {
    const { rows } = await this.pool.query(
      `UPDATE cache_rules SET
         path_pattern = COALESCE($2, path_pattern),
         ttl_seconds = COALESCE($3, ttl_seconds),
         stale_while_revalidate_seconds = COALESCE($4, stale_while_revalidate_seconds),
         origin_id = COALESCE($5, origin_id),
         updated_at = now()
       WHERE id = $1
       RETURNING *`,
      [
        id,
        patch.pathPattern ?? null,
        patch.ttlSeconds ?? null,
        patch.staleWhileRevalidateSeconds ?? null,
        patch.originId ?? null,
      ]
    );
    return rows[0] ? mapRow(rows[0]) : null;
  }

  async remove(id: string): Promise<Rule | null> {
    const { rows } = await this.pool.query(
      'DELETE FROM cache_rules WHERE id = $1 RETURNING *',
      [id]
    );
    return rows[0] ? mapRow(rows[0]) : null;
  }
}
