// Central configuration, 12-factor style. All values come from the environment
// so a Kubernetes ConfigMap/Secret can drive the control plane with no code change.

export interface Config {
  port: number;
  databaseUrl: string;
  redisUrl: string;
  adminApiKey: string;
  // Redis coordination keys — must match the proxy's configuration exactly.
  rulesHashKey: string;
  purgeChannel: string;
  ruleUpdateChannel: string;
  // Key prefix of the shared L2 cache tier — must match the proxy's
  // EDGECACHE_L2_KEY_PREFIX so purge can evict L2 objects too.
  l2KeyPrefix: string;
}

function required(name: string, fallback?: string): string {
  const v = process.env[name] ?? fallback;
  if (v === undefined) {
    throw new Error(`Missing required environment variable: ${name}`);
  }
  return v;
}

export function loadConfig(): Config {
  return {
    port: parseInt(process.env.PORT ?? '9000', 10),
    databaseUrl: required(
      'DATABASE_URL',
      'postgres://edgecache:edgecache@postgres:5432/edgecache'
    ),
    redisUrl: required('REDIS_URL', 'redis://redis:6379'),
    // In production this MUST be supplied via a Secret. The dev default keeps
    // `docker compose up` frictionless.
    adminApiKey: required('ADMIN_API_KEY', 'dev-admin-key'),
    rulesHashKey: process.env.RULES_HASH_KEY ?? 'edgecache:rules',
    purgeChannel: process.env.PURGE_CHANNEL ?? 'edgecache:purge',
    ruleUpdateChannel: process.env.RULE_UPDATE_CHANNEL ?? 'edgecache:rules:updated',
    l2KeyPrefix: process.env.L2_KEY_PREFIX ?? 'edgecache:l2:',
  };
}
