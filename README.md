# EdgeCache — Distributed HTTP Caching Proxy

A reverse-proxy HTTP caching layer — conceptually a minimal Varnish/CDN edge node. A fleet
of stateless **C++ proxy replicas** sits in front of one or more origin servers, caching
responses in-process according to HTTP caching semantics. A **Node.js/TypeScript control
plane** exposes a REST API for managing cache rules, origins, and purges. **Redis** is the
coordination backbone: it holds the live rule set every proxy reads, and it is the pub/sub
bus that propagates purge events to every replica in the fleet within milliseconds.

> **The thesis in one sentence:** purging a URL on one node clears it *everywhere* instantly,
> not on the next TTL expiry, and without a shared cache store that becomes a single point of
> failure.

---

## Architecture

```
                                   Clients
                                      |
                                      v
                          +-----------------------+
                          |   Kubernetes Ingress  |
                          +-----------+-----------+
                                      |
                     +----------------+----------------+
                     v                                 v
           +--------------------+          +--------------------+
           | EdgeCache Proxy #1 |   ...    | EdgeCache Proxy #N |   <- C++, Deployment, HPA
           |  thread-per-core   |          |                    |
           |  sharded L1 LRU    |          |                    |
           +---------+----------+          +---------+----------+
                     |    \                          |    \
        origin miss  |     \  purge/rule sub          |     \
                     v      v                         v      v
              +-----------+   +---------------------------------+
              | Origin(s) |   |              Redis              |
              +-----------+   |  - rules hash (fast-path config)|
                             |  - pub/sub: purge, rule-updated  |
                             |  - live hit/miss counters        |
                             +----------------+-----------------+
                                              ^
                              writes rules /  |  reads for dashboard
                              publishes purge |
                                              |
                   +--------------------------+--------------------------+
                   |         Node.js / TypeScript Control Plane          |
                   |   REST API: origins, rules, purge, stats           |
                   +---------+-------------------------------+----------+
                             |                               |
                             v                               v
                   +-------------------+          +-----------------------+
                   |    PostgreSQL     |          |         Kafka         |
                   | origins / rules / |          |  topic: access-log    |
                   | purge_log         |          |  (fire-and-forget)    |
                   +-------------------+          +-----------+-----------+
                                                              |
                                                              v
                                                  +-----------------------+
                                                  | Analytics Consumer    |
                                                  | rolls up hit/miss     |
                                                  | writes to Postgres    |
                                                  +-----------------------+
```

---

## Why Redis, not just TTLs?

A single caching proxy is trivial: a hash map with TTLs. The real problem appears the moment
you run **more than one replica**. If a cached response is wrong (a bad deploy, a stale price,
leaked private data), TTL-only expiry means it stays served on every replica until each
independent TTL fires — potentially minutes. EdgeCache instead publishes the purge pattern to
a Redis pub/sub channel that **every** replica subscribes to; each evicts matching keys from
its local L1 cache within milliseconds. Redis also holds the live rule set (path-pattern TTL
overrides) so operators change caching behavior with zero proxy redeploys. Crucially, Redis is
a *best-effort coordination layer*: if it goes down, proxies keep serving from their
last-known cache and rule set — availability over freshness, by design.

---

## Quickstart

```bash
cd deploy
docker compose up --build
# in another shell:
bash ../scripts/seed.sh          # register an origin + rules
```

This brings up 3 proxy replicas, the control plane, Postgres, Redis, Kafka (KRaft mode), a
dummy origin, the analytics consumer, and Prometheus + Grafana. See
[`docs/DEMO.md`](docs/DEMO.md) for the full walk-through.

Try it:

```bash
curl -i http://localhost:8080/products/1     # X-Cache: MISS
curl -i http://localhost:8080/products/1     # X-Cache: HIT (sub-millisecond, in-process)

# Purge it across the whole fleet in one call
curl -X POST http://localhost:9000/purge \
     -H 'Authorization: Bearer dev-admin-key' \
     -H 'Content-Type: application/json' \
     -d '{"pattern":"/products/1"}'

curl -i http://localhost:8080/products/1     # X-Cache: MISS — stale copy gone from every replica's L1 and the shared L2
```

The purge clears the stale entry from **every** replica's L1 and the shared L2 in
one call. The next request re-fetches from origin exactly once; with the L2 tier
on, the other replicas then serve that *fresh* copy from L2 (so they may show
`HIT` — of the new content, never the purged one). `test/integration/purge_propagation.sh`
verifies this end-to-end.

Windows: `powershell -File scripts\smoke.ps1`.

---

## Key design decisions (with trade-offs)

| Decision | Why | Trade-off |
|---|---|---|
| **Thread-per-core with a key-sharded L1 cache** (`SO_REUSEPORT`) | The kernel load-balances connections across per-core epoll loops (nginx pattern); the cache is striped into shards by `hash(cache-key)`, so lock contention stays low **and** a given key always maps to one shard — any worker serves it consistently | Cross-shard operations (purge, stats) fan out to every shard; a single global lock would be simpler but contended |
| **Shared Redis L2 tier behind L1** | A key any replica has fetched becomes a hit for the whole fleet, cutting origin load (~66% fewer origin fetches in a 3-replica test) without duplicating the object in every pod's memory | Adds one Redis round-trip on an L1 miss; best-effort (degrades to an origin fetch if Redis is down), and purge must evict L2 too (done centrally in the control plane before the L1 fan-out) |
| **Redis is best-effort, off the serving path** | A Redis/Kafka outage must never fail a cache-serving request | Purge propagation and rule freshness pause during a Redis outage (availability over freshness — documented, not a bug) |
| **Postgres is source of truth, Redis a cache of it** | Rules/origins/purges need durability; Redis gives fast-path reads | A write must hit Postgres first, so control-plane writes are rejected if Postgres is down rather than silently pushing an unpersisted rule |
| **Kafka carries only analytics, fire-and-forget** | Analytics should never slow down or fail a request | Access-log events can be dropped under Kafka pressure — acceptable for a lossy-allowed component |
| **Per-replica request coalescing (MVP)** | Solves the common stampede case with zero external dependency | Two *different* replicas can still each fetch a key once; cluster-wide coalescing via a Redis lock is a named stretch goal |
| **Readiness is independent of Redis** | The proxy serves fine from L1 during a Redis outage, so a Redis blip must not pull a healthy replica out of rotation | Redis health isn't a traffic gate — it's exposed via the `edgecache_redis_connected` metric (updated within ~1s by a heartbeat) for alerting instead |

Full rationale and the OOP/class design: [`docs/DESIGN_DECISIONS.md`](docs/DESIGN_DECISIONS.md).

Want to learn this whole system from scratch, file by file, with the "why"
behind every decision? See the 33-module
[EdgeCache Mastery Course](docs/course/00-INDEX.md).

---

## Repository layout

```
proxy/               C++ data plane (epoll thread-per-core, sharded LRU, coalescing,
                     circuit breaker, SWR, Redis rule sync + purge sub, Prometheus /metrics)
control-plane/       Node.js/TS REST API (origins, rules, purge, stats) → Postgres + Redis
analytics-consumer/  Node.js/TS Kafka consumer → hourly per-path rollups in Postgres
dummy-origin/        Trivial Express origin with tweakable Cache-Control headers
deploy/              docker-compose.yml, k8s/ (base + local overlay), observability configs
test/                integration/ (purge propagation), concurrency/ (coalescing), chaos/, load/
docs/                design decisions, failure modes, demo, runbook, benchmarks, course/ (learning course)
```

---

## Building & testing

- **Proxy (C++):** `cmake -S proxy -B proxy/build && cmake --build proxy/build && (cd proxy/build && ctest)`
  on any POSIX system, or `make test-proxy` to build + test inside Docker. The unit tests cover
  LRU eviction, cache-key normalization, `Cache-Control` parsing, circuit-breaker transitions,
  HTTP parsing, rule-JSON parsing, and a **deterministic request-coalescing proof** (100
  concurrent misses → exactly 1 origin fetch).
- **Control plane / analytics (TS):** `npm install && npm run typecheck && npm test`.
- **End-to-end:** `bash test/integration/purge_propagation.sh`, `test/concurrency/coalescing.sh`,
  `test/chaos/kill_origin.sh`, `test/chaos/kill_redis.sh`.

The proxy Docker build **runs the unit tests as part of the image build**, so a broken build
fails fast.

---

## Kubernetes

`kubectl apply -k deploy/k8s/overlays/local` on kind/minikube brings up the app + dev infra
with an HPA on the proxy. See [`deploy/k8s/README.md`](deploy/k8s/README.md), including how to
switch the HPA to scale on live request rate via the Prometheus Adapter.

---

## Benchmarks & failure handling

Measured on the local `docker compose` stack (3 replicas, 2 workers each, 128 MiB
L1, L2 on) — full table + method in [`docs/BENCHMARKS.md`](docs/BENCHMARKS.md):

- **~73,000 req/s** per replica on the cache-hit path; hit-path P50 **~1.1 ms** (the in-process lookup itself is sub-millisecond).
- **99.95%** hit rate under a Zipfian load (keyspace 1000, s=1.1) at ~36k req/s.
- **~66% fewer origin fetches** across a 3-replica fleet with the L2 tier on (180 → 62 for 60 keys; a cold replica served 20/20 L2-cached keys with 0 origin hits).
- Fleet-wide purge invalidation in **~10 ms**.

All 15 failure scenarios and EdgeCache's behavior: [`docs/FAILURE_MODES.md`](docs/FAILURE_MODES.md).

---

## What's next

- Cluster-wide request coalescing via a Redis lock
- `Vary`-header cache-key support for content negotiation
- Signed purge requests (prevent unauthorized cache flushing)

## License

MIT
