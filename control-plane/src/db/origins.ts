import { Pool } from 'pg';

export interface Origin {
  id: string;
  host: string;
  baseUrl: string;
  healthCheckPath: string;
  createdAt: string;
}

function mapRow(r: any): Origin {
  return {
    id: r.id,
    host: r.host,
    baseUrl: r.base_url,
    healthCheckPath: r.health_check_path,
    createdAt: r.created_at,
  };
}

export class OriginsRepo {
  constructor(private pool: Pool) {}

  async create(input: {
    host: string;
    baseUrl: string;
    healthCheckPath: string;
  }): Promise<Origin> {
    const { rows } = await this.pool.query(
      `INSERT INTO origins (host, base_url, health_check_path)
       VALUES ($1, $2, $3)
       RETURNING *`,
      [input.host, input.baseUrl, input.healthCheckPath]
    );
    return mapRow(rows[0]);
  }

  async list(): Promise<Origin[]> {
    const { rows } = await this.pool.query('SELECT * FROM origins ORDER BY created_at DESC');
    return rows.map(mapRow);
  }

  async get(id: string): Promise<Origin | null> {
    const { rows } = await this.pool.query('SELECT * FROM origins WHERE id = $1', [id]);
    return rows[0] ? mapRow(rows[0]) : null;
  }
}
