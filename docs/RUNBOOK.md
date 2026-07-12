# Operations Runbook

## Endpoints

**Proxy (data plane)** — transparent HTTP proxy on `:8080`; operational endpoints
on `:9100` (host ports 9101/9102/9103 for proxy1/2/3 in compose):

- `GET /metrics` — Prometheus exposition
- `GET /healthz` — liveness (200 while the process runs)
- `GET /readyz` — readiness (200 iff Redis reachable, else 503)

**Control plane (management plane)** on `:9000` — all business routes require
`Authorization: Bearer <ADMIN_API_KEY>`:

```
POST   /origins   {host, baseUrl, healthCheckPath}
GET    /origins
POST   /rules     {pathPattern, ttlSeconds, originId, staleWhileRevalidateSeconds}
GET    /rules
PUT    /rules/:id
DELETE /rules/:id
POST   /purge     {pattern}
GET    /purge/log
GET    /stats?path=&from=&to=
GET    /health    (public)
```

## Key metrics to watch

| Metric | Meaning | Alert when |
|---|---|---|
| `edgecache_requests_total{result}` | hit vs miss counters | hit rate drops sharply (poisoning / rule misconfig) |
| `edgecache_cache_size_bytes` | in-process cache size | approaching pod memory limit |
| `edgecache_evictions_total` | LRU evictions | sustained high → cache undersized |
| `edgecache_origin_latency_seconds` | origin fetch histogram | p99 climbing → origin degradation |
| `edgecache_circuit_breaker_state{origin,state}` | breaker state | stuck `open` |
| `edgecache_inflight_coalesced_total` | coalesced requests | — (informational) |
| `edgecache_redis_connected` | 1 if Redis reachable | 0 for a sustained period |

## Common tasks

**Change a TTL live (no redeploy):** `PUT /rules/:id` with a new `ttlSeconds`.
The control plane writes Postgres → Redis → publishes `edgecache:rules:updated`;
proxies refresh within a pub/sub round-trip (safety-net poll every 15s otherwise).

**Purge a bad response fleet-wide:** `POST /purge {"pattern":"/path"}` or a
wildcard `"/path/*"`. Audited in `purge_log`.

**Change cache size / worker count:** edit the ConfigMap
(`EDGECACHE_MAX_CACHE_BYTES`, `EDGECACHE_WORKERS`) and roll the Deployment. Keep
`EDGECACHE_MAX_CACHE_BYTES` below the pod memory limit (leave headroom for
connection buffers).

## Degraded states

- **`/readyz` 503 on some pods** → those pods can't reach Redis; K8s stops routing
  new traffic to them. They still serve their existing cache. Check Redis health.
- **Breaker stuck open for an origin** → origin is failing health/probes. Check
  origin, then breaker will half-open automatically.
- **Control-plane writes 5xx** → check Postgres; writes are rejected (not silently
  applied to Redis) when Postgres is unavailable, by design.
