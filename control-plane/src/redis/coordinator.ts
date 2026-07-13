import Redis from 'ioredis';
import { Config } from '../config';
import { Rule } from '../db/rules';

// Owns the control plane's Redis connection and all writes to the coordination
// backbone: mirroring the durable rule set into the fast-path Redis hash,
// notifying proxies of rule changes, and publishing purge events to the whole
// fleet. Redis is downstream of Postgres — we only touch it AFTER a durable
// write succeeds.
export class RedisCoordinator {
  private redis: Redis;

  constructor(private cfg: Config) {
    this.redis = new Redis(cfg.redisUrl, {
      lazyConnect: false,
      maxRetriesPerRequest: 2,
      retryStrategy: (times) => Math.min(times * 200, 2000),
    });
    this.redis.on('error', (err) => console.error('[redis] error', err.message));
    this.redis.on('connect', () => console.log('[redis] connected'));
  }

  private ruleValue(rule: Rule): string {
    return JSON.stringify({
      ttl: rule.ttlSeconds,
      swr: rule.staleWhileRevalidateSeconds,
      originId: rule.originId ?? '',
    });
  }

  // Upsert one rule into the Redis hash, then signal proxies to refresh.
  async upsertRule(rule: Rule): Promise<void> {
    await this.redis.hset(this.cfg.rulesHashKey, rule.pathPattern, this.ruleValue(rule));
    await this.redis.publish(this.cfg.ruleUpdateChannel, rule.pathPattern);
  }

  async removeRule(pathPattern: string): Promise<void> {
    await this.redis.hdel(this.cfg.rulesHashKey, pathPattern);
    await this.redis.publish(this.cfg.ruleUpdateChannel, pathPattern);
  }

  // Rebuild the entire Redis hash from the durable rule set (called on startup
  // so Redis is consistent with Postgres even after a Redis flush/restart).
  async reconcileAll(rules: Rule[]): Promise<void> {
    const pipeline = this.redis.pipeline();
    pipeline.del(this.cfg.rulesHashKey);
    for (const rule of rules) {
      pipeline.hset(this.cfg.rulesHashKey, rule.pathPattern, this.ruleValue(rule));
    }
    await pipeline.exec();
    await this.redis.publish(this.cfg.ruleUpdateChannel, '*');
    console.log(`[redis] reconciled ${rules.length} rules into ${this.cfg.rulesHashKey}`);
  }

  // Publish a purge pattern to every subscribed proxy. This is the core
  // fleet-wide invalidation primitive: one call clears the URL everywhere.
  async publishPurge(pattern: string): Promise<number> {
    return this.redis.publish(this.cfg.purgeChannel, pattern);
  }

  // Whether an L2 key's path matches a purge pattern — mirrors the proxy's
  // purgeMatches (prefix '*' / exact), operating on the path portion of the key.
  private l2PathMatches(key: string, pattern: string): boolean {
    // key = "<prefix>METHOD|host|path[?query]"
    const rest = key.slice(this.cfg.l2KeyPrefix.length);
    const firstBar = rest.indexOf('|');
    const secondBar = firstBar >= 0 ? rest.indexOf('|', firstBar + 1) : -1;
    let path = secondBar >= 0 ? rest.slice(secondBar + 1) : rest;
    const q = path.indexOf('?');
    if (q >= 0) path = path.slice(0, q);

    if (pattern === '*' || pattern === '/*') return true;
    if (pattern.endsWith('*')) return path.startsWith(pattern.slice(0, -1));
    return path === pattern;
  }

  // Evict matching objects from the shared L2 tier. Best-effort. Deleting L2
  // BEFORE the pub/sub L1 eviction is deliberate: it prevents a replica from
  // re-promoting a just-evicted L1 key from a stale L2 copy. Returns keys deleted.
  async purgeL2(pattern: string): Promise<number> {
    let cursor = '0';
    let deleted = 0;
    do {
      const [next, keys] = await this.redis.scan(
        cursor,
        'MATCH',
        `${this.cfg.l2KeyPrefix}*`,
        'COUNT',
        500
      );
      cursor = next;
      const toDelete = keys.filter((k) => this.l2PathMatches(k, pattern));
      if (toDelete.length > 0) {
        deleted += await this.redis.del(...toDelete);
      }
    } while (cursor !== '0');
    return deleted;
  }

  async ping(): Promise<boolean> {
    try {
      const pong = await this.redis.ping();
      return pong === 'PONG';
    } catch {
      return false;
    }
  }

  async close(): Promise<void> {
    await this.redis.quit();
  }
}
