# Failure Scenarios & Recovery

The 15 scenarios from the blueprint, with EdgeCache's actual behavior.

| # | Scenario | Behavior / Recovery |
|---|---|---|
| 1 | **Origin down** | Circuit breaker opens after `EDGECACHE_CB_FAILURE_THRESHOLD` consecutive failures; subsequent requests fast-fail with `503` (breaker open) instead of hanging. Half-open probes every `EDGECACHE_CB_OPEN_MS` detect recovery and close the breaker. |
| 2 | **Origin slow (not down)** | Each origin request is bounded by `EDGECACHE_ORIGIN_CONNECT_TIMEOUT_MS` + `EDGECACHE_ORIGIN_READ_TIMEOUT_MS`, independent of the breaker's failure threshold. A slow origin yields a `502`/`504`, not an unbounded hang. |
| 3 | **Redis (pub/sub) unavailable** | Proxies keep serving from L1 with TTL-only expiry. The subscriber thread reconnects in a loop; purge propagation resumes when Redis returns. Documented trade-off, not a bug. |
| 4 | **Redis (rule store) unavailable** | The in-process `RuleStore` retains the last-known-good rule set; the poller thread retries. Rule reads on the hot path never touch Redis. |
| 5 | **Proxy exceeds memory limit** | The byte-bounded LRU evicts before this happens (`EDGECACHE_MAX_CACHE_BYTES` is set below the pod memory limit). If it still OOMs, K8s restarts the pod — acceptable, cache isn't durable. |
| 6 | **Proxy pod restarts** | Cold cache on that replica only; other replicas keep serving. Briefly higher origin load for keys not yet re-cached there (coalescing limits the stampede). |
| 7 | **Synchronized TTL expiry for a hot key (stampede)** | Stale-while-revalidate serves the stale copy while exactly one coalesced background fetch refreshes it — no fleet-wide origin hammering. |
| 8 | **Duplicate purge delivery** | Eviction is idempotent: `LRUCache::purge` on an already-purged/absent key is a no-op returning 0. |
| 9 | **Kafka unavailable** | Access-log events are dropped by the producer (queue full / broker down); request serving is completely unaffected. This is the point of fire-and-forget. |
| 10 | **Control plane down** | Data plane keeps serving with last-synced config. Only *new* rule changes / purges are blocked until it returns. Clean control-plane/data-plane separation. |
| 11 | **Postgres down** | Control plane rejects rule/origin writes (Postgres is the durable source of truth) rather than pushing an unpersisted change to Redis. Reads of already-synced rules on proxies are unaffected. |
| 12 | **Cache-key poisoning attempt** | The key is computed only from method/host/path/normalized-query. Arbitrary request headers are never included, so an attacker header can neither poison nor fragment the cache. |
| 13 | **Origin sends inconsistent `Cache-Control` for the same URL** | Policy applies whatever the most recent fetch returned — a known, documented limitation shared by real CDNs. |
| 14 | **Per-replica-only coalescing under extreme fan-out** | Two replicas can each fetch the same key once. Acceptable at MVP scope; cluster-wide coalescing via a Redis lock is a named stretch goal. |
| 15 | **Network partition between a replica and Redis** | Same as #3/#4: serve from L1, `/readyz` reports not-ready so K8s stops routing new traffic to the degraded replica; recovers automatically. |

## How to exercise these

- `test/chaos/kill_origin.sh` — scenarios 1, 2
- `test/chaos/kill_redis.sh` — scenarios 3, 4, 15
- `test/integration/purge_propagation.sh` — scenario 8 (idempotent purge across fleet)
- `test/concurrency/coalescing.sh` + `proxy/test/test_coalescing.cpp` — scenarios 7, 14
- `docker compose stop control-plane` / `postgres` — scenarios 10, 11
