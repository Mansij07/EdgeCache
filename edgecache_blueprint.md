# EdgeCache — Distributed HTTP Caching Proxy
### Full Architecture, Feature Set & Implementation Blueprint
**Stack constraint honored:** C++ (proxy core), Node.js/TypeScript (control plane), Docker, Kubernetes (priority), Redis (priority), Kafka (optional, non-blocking), REST API.

---

## Project Brief (use this as your north-star spec)

> Build a reverse-proxy HTTP caching layer, conceptually a minimal Varnish/CDN edge node. A fleet of stateless C++ proxy replicas sits in front of one or more origin servers, caching responses in-process according to HTTP caching semantics (`Cache-Control`, `Expires`, path-based rule overrides). A Node.js/TypeScript control plane exposes a REST API for managing cache rules, origins, and purge requests. Redis is the coordination backbone: it holds the live rule set every proxy reads, and it's the pub/sub bus that propagates purge events instantly to every replica in the fleet, so purging a URL on one node clears it everywhere within milliseconds — not on the next TTL expiry. Kafka, if included, only carries non-blocking analytics (access logs) off the hot request path — the proxy never waits on it. The whole thing runs on Kubernetes, where the proxy Deployment autoscales on request rate and a pod's memory limit directly bounds its in-process cache size.

---

## 1. Problem Statement

A single caching proxy is trivial — an in-memory hash map with TTLs. The real engineering problem appears the moment you run **more than one replica**: how do N independent proxy processes agree on cache rules, and how does purging a stale/incorrect cached response on one replica make it disappear from *all* replicas immediately, without waiting for TTL expiry and without a shared cache store that becomes a single point of failure? EdgeCache exists to solve exactly that — fast, coordinated cache invalidation across a stateless fleet.

---

## 2. Functional Requirements

- Proxy incoming HTTP requests to a configured origin, caching cacheable responses in memory.
- Honor standard `Cache-Control` directives (`max-age`, `no-store`, `no-cache`, `private`) from the origin.
- Allow path-pattern-based rule overrides (TTL, stale-while-revalidate window) via the control plane, taking precedence over origin headers.
- Serve cached responses on a hit with an `X-Cache: HIT` header; forward to origin on a miss with `X-Cache: MISS`.
- Support instant purge of a specific URL or a wildcard path pattern, propagated to every proxy replica.
- Prevent cache stampede: many concurrent requests for the same missing key should trigger exactly one origin fetch per replica, not N.
- Circuit-break a origin that's failing/timing out so the proxy stops hammering it.
- Expose live cache statistics (hit rate, evictions, memory usage) per replica and in aggregate.
- REST API for origin registration, rule CRUD, purge, and stats querying.

---

## 3. Non-Functional Requirements

| Property | Target |
|---|---|
| Availability | A proxy replica keeps serving cached content even if Redis or the control plane is down |
| Reliability | No cache-serving request should ever fail *because* of a Redis/Kafka outage — those are best-effort layers |
| Scalability | Horizontal scaling of proxy replicas with no shared-state bottleneck on the hot path |
| Latency | Cache-hit latency should be sub-millisecond in-process; measure and report after benchmarking |
| Throughput | Target requests/sec per replica — measure via load test, insert real number |
| Durability | Cache content itself is *not* durable (it's a cache) — only rule config and audit logs need durability (Postgres) |
| Security | Admin API authenticated; cache-key computation resistant to header-injection cache poisoning |
| Maintainability | Adding a new origin or rule requires zero proxy code changes or redeploys — config-driven only |

---

## 4. Full Feature List

**MVP (must-have):**
- Single-threaded, then multi-threaded C++ proxy with in-process LRU cache
- `Cache-Control`-based cacheability + TTL parsing
- Path-pattern rule overrides fetched from Redis
- Purge propagation via Redis pub/sub across replicas
- Node.js/TS control plane: origin CRUD, rule CRUD, purge endpoint
- Postgres-backed durable rule storage (Redis is the fast-path read cache in front of it)
- Docker Compose local deployment
- Basic Prometheus `/metrics` endpoint on the proxy

**Advanced:**
- Request coalescing (thundering-herd protection) per replica
- Per-origin circuit breaker (closed/open/half-open)
- Stale-while-revalidate (serve stale content while asynchronously refreshing)
- Kafka-backed access-log pipeline → Node.js analytics consumer → rolled-up stats
- Kubernetes deployment: HPA on request rate, ConfigMap-driven live rule reload
- Two-tier cache: C++ in-process LRU (L1) + shared Redis (L2) for less-hot content

**Stretch:**
- Cluster-wide (not just per-replica) request coalescing via a Redis lock
- Cache-key `Vary` header support for content negotiation
- Signed purge requests (prevent unauthorized cache flushing)
- Live admin dashboard (Next.js) visualizing hit rate and purge events in real time

---

## 5. Full Architecture

```
                                   Clients
                                      |
                                      v
                          +-----------------------+
                          |   Kubernetes Ingress    |
                          +-----------+-------------+
                                      |
                     +----------------+----------------+
                     v                                 v
           +-------------------+           +-------------------+
           | EdgeCache Proxy #1 |   ...     | EdgeCache Proxy #N |   <- C++, Deployment, HPA
           |  (per-core threads, |           |                     |
           |   sharded L1 LRU)   |           |                     |
           +---------+-----------+           +---------+-----------+
                     |    \                              |    \
        origin miss  |     \  purge/rule sub              |     \
                     v      v                             v      v
              +-------------+   +----------------------------------+
              |  Origin(s)   |   |              Redis                 |
              +-------------+   |  - rules hash (fast-path config)    |
                                 |  - pub/sub: purge, rule-updated      |
                                 |  - L2 cache tier (optional)          |
                                 |  - live hit/miss counters             |
                                 +------------------+-------------------+
                                                     ^
                                     writes rules /  |  reads for dashboard
                                     publishes purge  |
                                                     |
                          +--------------------------+---------------------------+
                          |         Node.js / TypeScript Control Plane            |
                          |   REST API: origins, rules, purge, stats               |
                          +---------+----------------------------------+----------+
                                    |                                  |
                                    v                                  v
                          +-------------------+          +-------------------------+
                          |    PostgreSQL       |          |         Kafka            |
                          | origins / rules /   |          |  topic: access-log         |
                          | purge_log (durable)  |          |  (fire-and-forget from     |
                          +-------------------+          |   proxy, non-blocking)       |
                                                          +------------+--------------+
                                                                       |
                                                                       v
                                                          +-------------------------+
                                                          | Analytics Consumer (Node) |
                                                          | rolls up hit/miss stats    |
                                                          | writes back to Postgres    |
                                                          +-------------------------+
```

---

## 6. Component Breakdown

- **EdgeCache Proxy (C++):** the data plane. Stateless, horizontally scaled, never blocks on the control plane. Owns the actual cache memory.
- **Control Plane (Node.js/TS):** the management plane. Handles rule/origin CRUD, purge requests, and stats aggregation. Never touches the hot request path.
- **Redis:** the coordination bus — fast-path rule reads, pub/sub for purge and rule-update propagation, optional L2 cache tier, live counters.
- **PostgreSQL:** the durable source of truth for rules/origins/purge history (Redis is a cache of this, not the primary store) and the destination for rolled-up analytics.
- **Kafka (optional):** decouples access-log analytics from request serving entirely — a design choice, not a requirement, and explicitly justified by "analytics should never be able to slow down or fail a cache-serving request."

---

## 7. Request Lifecycles

### 7a. Cache hit
1. Client request arrives at a proxy replica (routed there by K8s Ingress/Service load balancing).
2. Proxy computes the cache key (method + host + path + normalized query + relevant `Vary` headers).
3. Key found in the local L1 LRU shard, not expired → serve directly from memory, add `X-Cache: HIT`.
4. Fire-and-forget an access-log event to Kafka (non-blocking, dropped silently if Kafka is unavailable).

### 7b. Cache miss (with request coalescing)
1. Key not found in L1 (or expired). Proxy checks an in-flight-request marker for this key.
2. If another request for the same key is already fetching from origin, this request waits on that result instead of firing a second origin call.
3. Otherwise, this request becomes the "leader" for the key, marks it in-flight, and calls the origin (async, non-blocking within the event loop).
4. Origin circuit breaker checked first — if open (origin marked unhealthy), fail fast with a 502 rather than waiting on a timeout.
5. On origin response: apply cache policy (rule override from the local rule cache takes precedence over origin `Cache-Control`), store in L1 if cacheable, release the in-flight marker, respond to all waiters.
6. `X-Cache: MISS` on the response.

### 7c. Purge propagation
1. Admin calls `POST /purge {pattern: "/api/products/*"}` on the control plane.
2. Control plane writes an entry to `purge_log` in Postgres (audit trail), then publishes `{pattern}` to the Redis `edgecache:purge` channel.
3. Every proxy replica, subscribed to that channel, receives the message and evicts all matching keys from its local L1 shard(s) — this happens in milliseconds across the whole fleet, not on next TTL expiry.
4. Eviction is idempotent — a replica that's already purged (or never had) a matching key simply no-ops.

### 7d. Rule update
1. Admin calls `PUT /rules/:id` on the control plane with a new TTL for a path pattern.
2. Control plane writes to Postgres, updates the corresponding field in the Redis `edgecache:rules` hash, then publishes on `edgecache:rules:updated`.
3. Proxies subscribed to that channel refresh their local rule cache immediately; as a safety net, they also poll Redis for the full rule set every N seconds in case a pub/sub message was missed during a reconnect window.

---

## 8. Redis Architecture (priority component)

| Purpose | Structure | Key pattern | Notes |
|---|---|---|---|
| Rule fast-path store | Hash | `edgecache:rules` | field = path pattern, value = JSON `{ttl, originId, swr}` |
| Purge propagation | Pub/Sub | `edgecache:purge` | message = pattern to evict; delivered to all subscribed proxies |
| Rule-update notification | Pub/Sub | `edgecache:rules:updated` | triggers immediate rule-cache refresh, no payload needed (proxies re-read the hash) |
| Live hit/miss counters | String (INCR) | `edgecache:stats:{replica}:hits` / `:misses` | scraped periodically by the analytics consumer or exposed via `/metrics` |
| L2 cache tier (advanced) | String w/ TTL | `edgecache:l2:{cachekey-hash}` | larger/less-hot objects shared across replicas to raise effective hit rate without duplicating memory per pod |

**Failure handling:** if Redis is unreachable, proxies keep serving from L1 using locally cached TTLs and the last-known-good rule set — purge propagation and rule updates simply pause until Redis returns. This is a deliberate availability-over-freshness trade-off, and you should say so explicitly in the README rather than let it look like an oversight.

---

## 9. Kafka Architecture (optional, non-blocking only)

- **Topic:** `edgecache.access-log` — one event per request: `{timestamp, replicaId, cacheKey, hit|miss, latencyMs, bytesServed}`.
- **Producer:** the C++ proxy, publishing asynchronously off the request-handling path (a local ring buffer flushed by a background thread, never awaited by the client-facing response).
- **Consumer:** a small Node.js/TS analytics service in its own consumer group, rolling events up into hourly per-path stats and writing them to Postgres.
- **Delivery guarantee:** best-effort — dropped access-log events under Kafka pressure are an acceptable loss; a dropped *cache-serving* request never is. This asymmetry is the whole justification for Kafka's presence here — it's optional infrastructure for a component that's allowed to be lossy, not core infrastructure.

---

## 10. Database Design (PostgreSQL)

```sql
origins(
  id UUID PK, host TEXT, base_url TEXT, health_check_path TEXT,
  created_at TIMESTAMPTZ
)

cache_rules(
  id UUID PK, path_pattern TEXT, ttl_seconds INT,
  stale_while_revalidate_seconds INT DEFAULT 0,
  origin_id UUID FK -> origins, created_at, updated_at
)
  INDEX (path_pattern)

purge_log(
  id UUID PK, pattern TEXT, requested_by TEXT, requested_at TIMESTAMPTZ
)

access_stats_rollup(
  path TEXT, date_hour TIMESTAMPTZ, hits BIGINT, misses BIGINT, bytes_served BIGINT,
  PRIMARY KEY (path, date_hour)
)
```
Postgres is deliberately kept light — it's the durability layer behind Redis's fast-path rule cache and the destination for rolled-up (not raw) analytics, not something on the hot request path.

---

## 11. API Design (REST — Node.js/TS control plane)

```
POST   /origins                 { host, baseUrl, healthCheckPath }
GET    /origins

POST   /rules                   { pathPattern, ttlSeconds, originId, staleWhileRevalidateSeconds }
GET    /rules
PUT    /rules/:id
DELETE /rules/:id

POST   /purge                   { pattern }                -> publishes to Redis, logs to Postgres

GET    /stats?path=&from=&to=   -> aggregated hit/miss/bandwidth stats

GET    /health
```
The proxy itself exposes no business API — it's a transparent proxy — only operational endpoints:
```
GET /metrics    (Prometheus text format)
GET /healthz    (liveness)
GET /readyz     (readiness — reflects Redis connectivity state)
```

---

## 12. OOP / Class Design (C++ proxy core)

- **`EventLoop`** — wraps `epoll`; one instance per worker thread.
- **`Connection`** — owns a client socket + read/write buffers; incrementally parses HTTP requests.
- **`HttpRequest` / `HttpResponse`** — parsed value objects.
- **`CacheKey`** — value type computed from method + host + path + normalized query (+ `Vary` headers when relevant).
- **`CacheEntry`** — headers + body + metadata (insertion time, TTL, ETag).
- **`LRUCache`** — hash map + intrusive doubly-linked list for O(1) get/put/evict; **sharded** (N shards, one per worker thread) to avoid cross-thread lock contention on the hot path.
- **`OriginClient`** *(interface, Adapter pattern)* — abstracts making an upstream request; concrete `HttpOriginClient`, with a `FakeOriginClient` test double for deterministic tests.
- **`CachePolicy`** *(interface, Strategy pattern)* — decides cacheability + TTL; `HeaderBasedPolicy` reads `Cache-Control`, `RuleOverridePolicy` wraps it and takes precedence when a matching rule exists.
- **`CircuitBreaker`** — per-origin closed/open/half-open state machine (State pattern), guarding `OriginClient` calls.
- **`InFlightRegistry`** — tracks in-progress origin fetches per key for request coalescing; waiters subscribe to a completion signal rather than each firing their own origin call.
- **`PurgeListener`** — subscribes to Redis pub/sub; on message, calls into each thread's `LRUCache` shard to evict matches (Observer pattern — `LRUCache` exposes an eviction interface, `PurgeListener` doesn't know cache internals).
- **`RuleStore`** *(Repository pattern)* — fetches/caches the rule hash from Redis, refreshed on pub/sub notification with a periodic poll safety net.
- **`MetricsRegistry`** — counters/histograms exposed via `/metrics`; injected into components rather than accessed as a global singleton, so components stay unit-testable.

```
EventLoop (1 per thread) --owns--> Connection[]
Connection --parses--> HttpRequest
Connection --uses--> LRUCache (shard) --stores--> CacheEntry, keyed by CacheKey
Connection --on miss--> InFlightRegistry --coalesces--> OriginClient (interface) <|-- HttpOriginClient, FakeOriginClient
OriginClient --guarded by--> CircuitBreaker (per origin)
Connection --consults--> CachePolicy (interface) <|-- HeaderBasedPolicy, RuleOverridePolicy(wraps HeaderBasedPolicy)
RuleOverridePolicy --reads--> RuleStore --synced from--> Redis
PurgeListener --evicts from--> LRUCache (all shards)
```
Dependency Inversion is what makes this testable at all: `OriginClient` and `CachePolicy` as interfaces mean the entire request-handling path can be unit-tested with fake origins and canned headers, without a real network call or a real Redis instance.

---

## 13. Concurrency Model

- **Thread-per-core with `SO_REUSEPORT`:** the listening socket is opened with `SO_REUSEPORT` and one `EventLoop` runs per CPU core/thread; the kernel load-balances incoming connections across threads. Each thread owns its own `LRUCache` shard — **no cross-thread mutex on the cache hot path**, which is the single biggest concurrency decision in the whole project and a strong interview talking point (nginx uses the same pattern).
- **Purge fan-out:** a purge message must reach every thread's shard, not just one — implemented via a lock-free (or lightly-locked) per-thread message queue that each `EventLoop` drains once per iteration, rather than a shared mutex across all shards.
- **Request coalescing races:** the true race to solve is two requests for the same missing key arriving on the *same* thread microseconds apart before the first one has registered itself as in-flight — solved by registering the in-flight marker synchronously before the async origin call is issued, not after.
- **Origin calls are fully async** within the event loop (non-blocking sockets), so one slow origin never stalls the thread's other connections.

---

## 14. Docker Architecture

Images: `edgecache-proxy` (C++, statically linked where practical for a small final image), `edgecache-control-plane` (Node.js/TS), `edgecache-analytics-consumer` (Node.js/TS, only if Kafka is included). `docker-compose.yml` wires: 2–3 proxy replicas, control plane, Postgres, Redis, Kafka (+ Zookeeper/KRaft), a dummy origin server (a trivial Express app returning `Cache-Control` headers you can tweak), and an analytics consumer. Everything communicates over the Compose network by service name.

---

## 15. Kubernetes Architecture (priority component)

- **Proxy:** `Deployment`, **HPA on request rate** (via a Prometheus adapter, or CPU as a simpler fallback) — this is your cleanest, most demonstrable HPA story: generate load, watch replica count climb, watch it come back down.
- **Control plane:** `Deployment`, HPA optional (it's low-traffic — admin operations, not client-facing).
- **Analytics consumer:** its own `Deployment` (independent scaling from the control plane's API traffic).
- **Redis / Postgres / Kafka:** run via existing Helm charts (Bitnami/Strimzi) with `PersistentVolumeClaim`s — don't hand-roll StatefulSets for off-the-shelf infra you're not building yourself.
- **ConfigMap:** proxy memory-limit-per-pod, default TTLs, Redis connection info — changing a ConfigMap value and rolling the Deployment is a good "live config" demo point.
- **Secret:** Postgres credentials, Redis auth (if enabled), Kafka SASL credentials, control-plane admin API key/JWT signing secret.
- **Ingress:** public-facing, routes to the proxy `Service`; a **separate internal-only route** (or a second Ingress restricted by network policy) for the control-plane admin API — a real security boundary worth calling out explicitly, not just an implementation detail.
- **Resource requests/limits:** the proxy pod's memory *limit* directly determines the app-level max-cache-size config (limit minus a safety buffer for connection buffers/overhead) — tying a Kubernetes resource limit directly to an application-level cache-sizing decision is a concrete, specific thing to discuss in an interview.
- **Probes:** liveness = process responsiveness only; readiness = Redis reachable (if you want purge-freshness guarantees to gate traffic) — document which choice you made and why, since "should a proxy that can't reach Redis be marked unready" is a genuine, debatable design decision, not an obvious one.

---

## 16. Observability

- **Metrics (Prometheus, exposed by the C++ proxy):** `edgecache_requests_total{result="hit|miss"}`, `edgecache_cache_size_bytes`, `edgecache_evictions_total`, `edgecache_origin_latency_seconds` (histogram), `edgecache_circuit_breaker_state{origin}`, `edgecache_inflight_coalesced_total`.
- **Logs:** structured JSON per request (cache key, hit/miss, latency, origin called or not) — sampled at high volume rather than logging every request at full detail in production-scale testing.
- **Dashboards (Grafana):** fleet-wide hit rate over time, per-origin circuit-breaker state, memory usage per pod vs. its limit, purge-to-eviction latency (time from purge publish to last replica confirming eviction, if you instrument it).
- **Alerts:** hit rate dropping sharply (possible cache-poisoning or rule misconfiguration), a circuit breaker stuck open, memory usage approaching the pod limit.
- **Health checks:** `/healthz` (liveness), `/readyz` (readiness, reflecting Redis connectivity as discussed above).

---

## 17. Failure Scenarios & Recovery (15)

1. **Origin down** → circuit breaker opens after N consecutive failures; fast 502s instead of hung requests; half-open probes periodically to detect recovery.
2. **Origin slow (not down)** → bounded per-request timeout independent of the circuit breaker's failure threshold.
3. **Redis (pub/sub) unavailable** → proxies keep serving from local L1 with TTL-only expiry; purge propagation pauses until Redis returns — documented trade-off, not a bug.
4. **Redis (rule store) unavailable** → proxies fall back to the last-known-good rule set cached in process memory.
5. **Proxy pod exceeds memory limit** → LRU eviction should keep usage bounded before this happens; if it still does, K8s OOM-kills and restarts the pod — acceptable since cache content isn't durable data.
6. **Proxy pod restarts** → cold cache on that replica only; other replicas keep serving, briefly higher origin load for keys not yet re-cached there.
7. **Synchronized TTL expiry across replicas for a hot key (stampede)** → stale-while-revalidate serves the stale copy while one coalesced fetch refreshes it, instead of every replica hammering origin simultaneously.
8. **Duplicate purge delivery** (expected pub/sub behavior, not a bug) → eviction handler is idempotent.
9. **Kafka unavailable** → access-log events dropped; request serving completely unaffected — this is the entire point of making Kafka fire-and-forget.
10. **Control plane down** → data plane (proxies) keeps serving with last-synced config; only new rule changes/purges are blocked until it's back — a clean control-plane/data-plane separation story.
11. **Postgres down** → control plane can't durably persist new rules; reject writes rather than silently pushing an un-persisted change straight to Redis (safer default — document why).
12. **Cache-key poisoning attempt** (attacker-controlled header used in key computation) → strict, whitelist-only header inclusion in cache-key computation via `Vary`, never arbitrary headers.
13. **Origin sends inconsistent `Cache-Control` across requests for the same URL** → policy applies whatever the most recent fetch returned; a known, documented limitation shared by real CDNs, not something to over-engineer around.
14. **Per-replica-only request coalescing under extreme fan-out** (two replicas both miss the same key independently) → acceptable at MVP scope; cluster-wide coalescing via a Redis lock is an explicit, named stretch goal, not an oversight.
15. **Network partition between a replica and Redis** → same behavior as scenario 3; readiness probe reflects the degraded state if you chose that design in Section 15.

---

## 18. Testing Strategy

- **Unit:** LRU eviction correctness (O(1) get/put/evict under a fixed capacity), cache-key normalization, `Cache-Control` parsing edge cases, circuit-breaker state transitions.
- **Integration:** full proxy + real dummy origin + real Redis in Docker Compose; verify a purge on the control plane clears the key on 2+ proxy replicas within a bounded time window.
- **Concurrency:** fire 100 concurrent requests for the same missing key at one replica, assert exactly 1 origin call was made (proves request coalescing works, not just "seems to work").
- **Failure:** kill the dummy origin mid-test, assert the circuit breaker opens and subsequent requests fail fast rather than time out; kill Redis mid-test, assert the proxy keeps serving cached content.
- **Load:** `wrk` or `k6` against a mix of hot and cold keys following a Zipfian distribution (a realistic "some URLs are much more popular" traffic shape) to get a meaningful hit-rate number, not an artificially perfect one.

---

## 19. Performance Benchmarking

Use `wrk` or `k6`. Measure and report (insert real numbers after running):
- Requests/sec per replica for pure cache hits vs. pure cache misses.
- P50/P95/P99 latency for hits (should be sub-millisecond, in-process) vs. misses (bounded by origin latency + your added overhead).
- Cache hit ratio under a Zipfian request distribution at a given cache size.
- Memory usage as a function of configured cache size, to validate the "pod memory limit bounds cache size" design decision.
- Effect of enabling the Redis L2 tier on effective fleet-wide hit rate (this is genuinely the most interesting number to produce, since it directly measures whether the two-tier design was worth building).

---

## 20. 10-Week Development Roadmap

- **Week 1:** Minimal single-threaded HTTP proxy (pass-through, no caching) + a dummy origin, Docker Compose skeleton.
- **Week 2:** In-process LRU cache, `Cache-Control` parsing, cacheability decision, `X-Cache` header.
- **Week 3:** Multi-threaded event loop (`SO_REUSEPORT`, per-thread sharded cache); load test to validate the threading model works as intended.
- **Week 4:** Redis rule store integration; Node.js/TS control plane with origin + rule CRUD writing to Postgres and Redis.
- **Week 5:** Redis pub/sub purge propagation across replicas; purge endpoint end-to-end.
- **Week 6:** Request coalescing + per-origin circuit breaker.
- **Week 7:** Stale-while-revalidate; Kafka access-log pipeline + Node.js analytics consumer.
- **Week 8:** Observability — Prometheus `/metrics`, structured logging, Grafana dashboards.
- **Week 9:** Kubernetes manifests (Deployment + HPA for proxy, Ingress, ConfigMap/Secret, Helm-chart-based Redis/Postgres/Kafka), deploy to kind/minikube, in-cluster load test.
- **Week 10:** L2 Redis cache tier (stretch), chaos-testing pass (kill origin, kill Redis, kill a proxy pod live), README + demo recording, resume bullets with real numbers.

---

## 21. MVP Definition (resume-ready floor)

Multi-threaded C++ proxy with sharded in-process LRU cache, `Cache-Control`-aware caching, Redis-backed rule overrides, Redis pub/sub purge propagation verified across 2+ replicas, Node.js/TS control plane for origin/rule/purge management, Postgres-durable rule storage, Docker Compose deployment, basic Prometheus metrics. This alone is a complete, demonstrable distributed caching system.

## 22. Advanced Version (post-MVP)

Request coalescing, per-origin circuit breaking, stale-while-revalidate, Kafka-based non-blocking analytics pipeline, Kubernetes deployment with HPA on request rate, two-tier (L1 in-process + L2 Redis) caching.

---

## 23. Repository Structure

```
edgecache/
  proxy/                     # C++
    src/
      net/                    # EventLoop, Connection
      http/                    # HttpRequest, HttpResponse parsing
      cache/                    # LRUCache, CacheKey, CacheEntry
      policy/                    # CachePolicy implementations
      origin/                     # OriginClient, CircuitBreaker
      redis/                       # RuleStore, PurgeListener
      metrics/                      # MetricsRegistry
    test/
    CMakeLists.txt
  control-plane/              # Node.js / TypeScript
    src/
      routes/                  # origins, rules, purge, stats
      db/                        # Postgres access layer
      redis/                       # rule sync + pub/sub publish
    test/
  analytics-consumer/          # Node.js / TypeScript (Kafka consumer)
  deploy/
    docker-compose.yml
    k8s/
      base/
      overlays/local/
  test/
    integration/
    concurrency/                 # coalescing proof tests
    chaos/
  README.md
```

---

## 24. README Structure

Pitch → architecture diagram (the one in Section 5) → "why Redis, not just TTLs" (the purge-propagation story, 3–4 sentences — this is the section that sells the project fastest) → quickstart (`docker compose up`) → key design decisions with trade-offs stated explicitly (thread-per-core sharded cache vs. one shared cache, Kafka as best-effort-only) → benchmark results table → failure-handling summary table → what's next.

---

## 25. Demo Strategy (3–5 minutes)

1. `docker compose up` — 3 proxy replicas, control plane, Redis, Postgres, dummy origin (0:30).
2. Hit a URL, show `X-Cache: MISS` then `X-Cache: HIT` on the second request; hit a *different* proxy replica for the same URL, show it's also a `HIT` (proves shared-rule/L2 behavior, or explain L1-only if you didn't build L2) (1:00).
3. Call `POST /purge` for that URL, immediately re-request from all 3 replicas, show every single one now returns `MISS` — this is the entire thesis of the project in 20 seconds (1:00).
4. Kill the dummy origin container, show the circuit breaker open in the Grafana dashboard and requests failing fast instead of hanging (1:00).
5. Fire a load test, show the HPA scale proxy replica count up live in `kubectl get pods -w` (1:00).

---

## 26. Resume Bullets

- Built a distributed HTTP caching proxy in C++ with a thread-per-core, sharded in-process LRU cache, achieving **[insert measured]** requests/sec per replica with sub-millisecond cache-hit latency.
- Designed a Redis pub/sub-based invalidation system propagating cache purges across a fleet of stateless proxy replicas in under **[insert measured]** ms, versus relying on TTL-only expiry.
- Implemented request coalescing and per-origin circuit breaking in the proxy's async I/O core, reducing origin load by **[insert measured %]** under a simulated cache-stampede scenario.
- Deployed the system on Kubernetes with HPA scaling proxy replicas on live request rate, and built a non-blocking Kafka-based analytics pipeline that adds zero latency to the request-serving path.

