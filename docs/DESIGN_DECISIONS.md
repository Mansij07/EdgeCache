# Design Decisions & Trade-offs

This document records the decisions that are *not* obvious from the code, and the
trade-offs each one makes. These are the "interview talking points".

## 1. Thread-per-core with `SO_REUSEPORT` and a sharded cache

Each worker thread opens its **own** listening socket with `SO_REUSEPORT`; the
kernel load-balances incoming connections across threads. Each thread owns its
**own** `LRUCache` shard, so the cache hot path (get/put by the owning thread)
has no cross-thread mutex contention. This is the single biggest concurrency
decision in the project — nginx uses the same pattern.

- **Trade-off:** the cache is *not shared between threads of one pod*, so a key
  cached by thread A is a miss on thread B. Effective per-pod hit rate is slightly
  lower than one big shared cache would give. We buy it back with the optional L2
  Redis tier and with request coalescing being replica-wide (below).
- **Implementation note:** the shard uses a light internal mutex (rather than a
  fully lock-free per-thread purge queue) so the purge thread can evict across
  shards. On the hot path that mutex is essentially uncontended because only the
  owning worker touches the shard. The lock-free queue is a documented possible
  optimization.

## 2. Bounded-blocking origin fetch (not a fully async origin state machine)

Client I/O is fully non-blocking (epoll, buffered partial reads/writes). On a
cache **miss**, the origin fetch is a blocking call bounded by connect/read
timeouts and guarded by a circuit breaker.

- **Why:** a correct fully-async origin state machine inside the event loop is a
  large amount of code; bounded blocking is dramatically simpler and, with N
  worker threads plus timeouts plus the breaker, one slow origin only degrades
  that one thread's throughput, not the process.
- **Trade-off:** a burst of misses on one thread can briefly reduce that thread's
  hit-serving throughput. Named as a simplification, not hidden.

## 3. Request coalescing is replica-wide, cache shards are per-thread

The `InFlightRegistry` is shared across all worker threads of a replica, so N
concurrent misses for the same key (spread across threads by `SO_REUSEPORT`)
collapse into exactly **one** origin fetch. The single leader stores into its own
shard; waiters also populate their own shard from the shared result so future
requests hit locally.

- **Trade-off:** two *different replicas* can still each fetch the same key once.
  Cluster-wide coalescing via a Redis lock is a named stretch goal.

## 4. Redis is a best-effort coordination layer, never on the serving path

Rule reads on the hot path hit an in-process `RuleStore` (last-known-good). Redis
is used by background threads only: a subscriber (purge + rule-update pub/sub) and
a poller (periodic `HGETALL` safety net). If Redis is unreachable, the proxy keeps
serving from L1 with the last-known rules.

- **Trade-off:** during a Redis outage, purge propagation and rule updates pause.
  This is **availability over freshness**, chosen deliberately — a Redis outage
  must never fail a cache-serving request.

## 5. Postgres is the source of truth; Redis is a cache of it

The control plane writes rules/origins/purges to Postgres **first**, then mirrors
to Redis. If Postgres is down, writes are rejected rather than pushed only to
Redis.

- **Why:** never let the fast-path store hold a rule that isn't durably persisted.
- **Trade-off:** control-plane writes are unavailable during a Postgres outage —
  but the data plane keeps serving with its last-synced config.

## 6. Kafka carries only analytics, fire-and-forget

The proxy produces one access-log event per request to Kafka via librdkafka's
internal async queue; a background thread serves delivery reports. If the broker
is down or the queue is full, events are **dropped**. Kafka is an optional
compile-time feature (`-DEDGECACHE_KAFKA`) so the default build and unit tests
have zero third-party dependencies.

- **Why Kafka at all:** to decouple analytics from serving entirely. A dropped
  access-log event is acceptable; a dropped cache-serving request is not. That
  asymmetry is the whole justification.

## 7. Readiness reflects Redis; liveness does not

`/readyz` returns 503 when Redis is unreachable, so Kubernetes stops routing new
traffic to a replica that can't receive purge/rule updates (purge-freshness
gating). `/healthz` only reflects process responsiveness, so a Redis blip never
gets a still-serving pod killed.

- **This is a genuinely debatable call.** The alternative (keep serving stale-ish
  from an unready-to-purge replica) is also defensible; we chose to gate traffic.

## 8. Cache-key computation is poisoning-resistant

The cache key is `method | host | path | normalized-query`. Query params are
sorted so order doesn't fragment the cache. **Arbitrary request headers are never
mixed into the key** — only a whitelist could ever be (via `Vary`, a stretch
goal). This prevents an attacker-controlled header from poisoning or fragmenting
the cache.

---

## OOP / class design (C++ core)

| Class | Role | Pattern |
|---|---|---|
| `EventLoop` | epoll wrapper, one per worker thread | — |
| `RequestParser` / `HttpRequest` / `HttpResponse` | incremental HTTP parsing + value objects | — |
| `CacheKey` | canonical key from method/host/path/query | Value object |
| `CacheEntry` / `LRUCache` | byte-bounded O(1) LRU, sharded | — |
| `OriginClient` → `HttpOriginClient`, `FakeOriginClient` | upstream fetch abstraction | Adapter + Dependency Inversion |
| `CachePolicy` → `HeaderBasedPolicy`, `RuleOverridePolicy` | cacheability/TTL decision | Strategy + Decorator |
| `CircuitBreaker` | closed/open/half-open guard per origin | State |
| `InFlightRegistry` | coalesce concurrent misses | — |
| `RedisCoordinator` / `RuleStore` | rule sync + purge subscription | Repository + Observer |
| `MetricsRegistry` | counters/histograms, injected not global | Dependency Injection |
| `AccessLogSink` → `NullAccessLogSink`, `KafkaAccessLogSink` | fire-and-forget analytics | Strategy |

Dependency Inversion (`OriginClient` and `CachePolicy` as interfaces) is what
makes the whole request path unit-testable with a `FakeOriginClient` and canned
headers — no real network or Redis needed. See `proxy/test/`.
