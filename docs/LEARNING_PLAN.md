# EdgeCache Learning Plan — Contributor-Level Mastery

A self-paced, hands-on curriculum for learning this codebase from scratch: what to read, in what
order, and what to run/break at each step to prove you understood it. Written for someone new to
distributed systems, C++, and async networking, aiming for contributor-level mastery (able to
confidently extend, debug, and modify any part of the system) — not just a conceptual overview.

`edgecache_blueprint.md` and [`DESIGN_DECISIONS.md`](DESIGN_DECISIONS.md) already explain the
"why" behind almost every non-obvious choice in this project — this plan uses them as a spine
rather than asking you to reverse-engineer intent from code alone. EdgeCache is fully implemented:
a C++ proxy (`proxy/`), a Node.js/TS control plane (`control-plane/`), a Kafka analytics consumer
(`analytics-consumer/`), a dummy origin (`dummy-origin/`), Docker Compose + Kubernetes deploy
configs (`deploy/`), and a real test suite (`proxy/test/`, `control-plane/test/`,
`test/integration|concurrency|chaos|load`).

**Build/test environment note:** if you have no local C++ toolchain, all C++ builds/tests go
through Docker — `docker build --target build -t edgecache-proxy-test ./proxy` runs `ctest`
inside the build stage. Bash test scripts need Git Bash/WSL on Windows. If you bring up the full
Compose stack and Grafana's port 3000 collides with something else running locally, use the core
subset (`postgres redis dummy-origin control-plane proxy1 proxy2 proxy3`) for most exercises, and
add `kafka analytics-consumer` for the analytics stage.

This plan is organized as **21 stages**, each a self-contained topic with its own files, concepts,
and a hands-on checkpoint. It roughly follows the codebase's own dependency order (leaf modules
first, orchestration last) — the same order the CMake build links things in, and a genuinely good
learning order: you can understand and unit-test each piece in isolation before seeing how they
compose. Check off stages as you go; this is meant to be worked through over several weeks, not a
single sitting.

---

## Progress tracker

- [ ] Phase 0 — Foundational concepts
- [ ] Stage 1 — Orientation
- [ ] Stage 2 — HTTP fundamentals & parsing
- [ ] Stage 3 — Cache core
- [ ] Stage 4 — Cache policy
- [ ] Stage 5 — Configuration
- [ ] Stage 6 — Origin client & resilience
- [ ] Stage 7 — Request coalescing
- [ ] Stage 8 — Redis integration
- [ ] Stage 9 — Metrics
- [ ] Stage 10 — Networking core (epoll)
- [ ] Stage 11 — Analytics sink (Kafka)
- [ ] Stage 12 — Orchestration
- [ ] Stage 13 — Proxy test harness & Docker build
- [ ] Stage 14 — Control plane
- [ ] Stage 15 — Analytics consumer
- [ ] Stage 16 — Dummy origin
- [ ] Stage 17 — Deployment & infrastructure
- [ ] Stage 18 — Observability
- [ ] Stage 19 — End-to-end, concurrency & chaos tests
- [ ] Stage 20 — CI/CD
- [ ] Stage 21 — Docs capstone
- [ ] Capstone exercise

---

## Phase 0 — Foundational Concepts (read before touching code)

You don't need to master these first, just get oriented — each will click harder once you see it
in real code in Stages 1–13. Skim this section, then come back to specific bullets as needed.

- **HTTP caching semantics:** `Cache-Control` directives (`max-age`, `no-store`, `no-cache`,
  `private`), `Expires`, and what "stale-while-revalidate" means (serve an expired-but-recent
  cached copy immediately while refreshing it in the background, instead of making the client
  wait on the origin).
- **LRU cache:** a fixed-capacity cache that evicts the *Least Recently Used* item when full.
  O(1) get/put is achieved with a hash map (key → node) plus a doubly-linked list (recency
  order) — the map gives fast lookup, the list gives fast "move to front" / "evict tail".
- **Concurrency basics:** mutexes (mutual exclusion locks), and why *lock contention* (many
  threads fighting over one lock) kills throughput — the motivation for sharding.
- **Event-driven I/O / epoll / the reactor pattern:** instead of one thread per connection,
  one thread registers many sockets with the OS (`epoll` on Linux) and gets woken only when a
  socket is ready to read/write — this is how one thread serves thousands of connections without
  blocking.
- **Thread-per-core with `SO_REUSEPORT`:** normally one thread owns the listening socket and
  hands connections to workers. With `SO_REUSEPORT`, *every* thread opens its own listening
  socket on the same port, and the kernel load-balances new connections across them directly —
  no hand-off, no shared-state bottleneck at accept time.
- **Circuit breaker pattern:** a State-machine guard (closed → open → half-open) in front of a
  flaky dependency. Closed = calls pass through normally. Too many failures → open = fail fast
  without even trying the call. After a cooldown → half-open = let one probe through to test
  recovery.
- **Request coalescing / thundering herd:** when a cached key expires, many concurrent requests
  for it can all miss simultaneously and hammer the origin at once. Coalescing makes the first
  request the "leader" (it fetches), and all others "wait" on that one in-flight fetch instead of
  each starting their own.
- **Redis primitives used here:** hashes (`edgecache:rules`, one field per path pattern), pub/sub
  (channels like `edgecache:purge` that every subscriber receives instantly), and plain
  string keys with TTL (the L2 cache tier).
- **Kafka, briefly:** a durable, ordered log with topics and consumer groups; used here purely as
  a best-effort, fire-and-forget analytics pipe — never on the request-serving path.
- **Docker basics:** images, multi-stage builds (a `build` stage with the full toolchain, a slim
  final runtime stage), `docker compose` wiring multiple services on one network by service name.
- **Kubernetes basics:** `Deployment` (a pod's desired replica count + template), `Service`
  (stable network identity for a set of pods), `HPA` (Horizontal Pod Autoscaler — scales replica
  count on a metric), `ConfigMap`/`Secret` (external config/credentials), `Ingress` (external
  HTTP routing in).
- **Prometheus/Grafana:** Prometheus scrapes a `/metrics` text endpoint on an interval and stores
  time series; Grafana queries Prometheus and renders dashboards.

---

## Stage 1 — Orientation (docs only, no code yet)

**Read, in this order:**
1. [`README.md`](../README.md) — the pitch, architecture diagram, quickstart, benchmark headlines.
2. [`edgecache_blueprint.md`](../edgecache_blueprint.md) — the full original spec:
   functional/non-functional requirements, feature list, all 15 named failure scenarios, OOP
   class design, concurrency model. This is the single richest document in the repo — read it fully.
3. [`DESIGN_DECISIONS.md`](DESIGN_DECISIONS.md) — the 8 "why", not "what", decisions plus the
   OOP/pattern table. Keep this open as a reference through Stages 2–13.
4. [`FAILURE_MODES.md`](FAILURE_MODES.md) — skim now, you'll re-read closely in Stage 21.

**Hands-on checkpoint:**
- `cd deploy && docker compose up -d --build postgres redis dummy-origin control-plane proxy1 proxy2 proxy3`
- `bash scripts/seed.sh` (registers an origin + rules; admin key is `dev-admin-key`)
- Reproduce the README demo: `curl -i http://localhost:8080/products/1` twice (MISS then HIT),
  then `POST /purge` on the control plane, then re-`curl` and see MISS again.
- Goal isn't to understand *how* yet — just confirm the system works end-to-end and you can see
  `X-Cache` flip, so every later stage has a concrete behavior to map back to.

---

## Stage 2 — HTTP fundamentals & parsing

**Files:** `proxy/src/http/Http.h`, `proxy/src/http/Http.cpp`
**Test:** `proxy/test/test_http_parse.cpp`

**Concepts:** `HttpRequest`/`HttpResponse` as parsed value objects; incremental parsing (why you
can't assume a whole request arrives in one `read()`); what fields matter for caching (method,
path, query, headers).

**How to study:** read the test file first — it's the spec in executable form — then read the
header, then the implementation. This "test-first" reading order works for every C++ module below.

---

## Stage 3 — Cache core

**Files, in order:**
- `proxy/src/cache/CacheEntry.h` — what's stored per key (headers, body, insertion time, TTL, ETag).
- `proxy/src/cache/CacheKey.h` / `.cpp` + `proxy/test/test_cache_key.cpp` — canonical key = method
  + host + path + *sorted* query params. Note **why** headers are never mixed in arbitrarily
  (poisoning resistance) — cross-reference Design Decision #8.
- `proxy/src/cache/LRUCache.h` / `.cpp` + `proxy/test/test_lru.cpp` — the O(1) hashmap +
  doubly-linked-list eviction core.
- `proxy/src/cache/ShardedCache.h` / `.cpp` + `proxy/test/test_sharded_cache.cpp` — N `LRUCache`
  shards, selected by `hash(cacheKey)` (**not** by which thread/connection touches it — that
  distinction matters, it's what makes any worker thread serve a given key consistently).
  Cross-reference Design Decision #1.

**Hands-on checkpoint:** trace by hand what shard a specific key would land in given a hash and
shard count; confirm your understanding matches what `test_sharded_cache.cpp` asserts.

---

## Stage 4 — Cache policy (cacheability + TTL decisions)

**Files:** `proxy/src/policy/CachePolicy.h` / `.cpp`, `proxy/src/policy/RuleOverridePolicy.h`
**Test:** `proxy/test/test_policy.cpp`

**Concepts:** Strategy pattern (`CachePolicy` interface) + Decorator pattern
(`RuleOverridePolicy` wraps `HeaderBasedPolicy`, taking precedence when a matching path-pattern
rule exists — otherwise falling back to origin `Cache-Control` parsing).

---

## Stage 5 — Configuration

**Files:** `proxy/src/config/Config.h` / `.cpp`

**Concepts:** env-var-driven config (`Config::fromEnv()`), so re-deploys never require code
changes. Cross-reference every `EDGECACHE_*` variable back to `deploy/docker-compose.yml`
(already the source of the running stack's config) to see each one's real runtime value.

---

## Stage 6 — Origin client & resilience

**Files, in order:**
- `proxy/src/origin/OriginClient.h` — the interface (Adapter + Dependency Inversion: this is what
  makes the request path unit-testable without a real network).
- `proxy/src/origin/HttpOriginClient.h` / `.cpp` — the real implementation. Note it's a
  **bounded-blocking** call (Design Decision #2), not a fully async state machine — understand
  *why* that's a deliberate simplification, not an oversight.
- `proxy/src/origin/FakeOriginClient.h` — the test double; see how it's used across the unit tests.
- `proxy/src/origin/CircuitBreaker.h` / `.cpp` + `proxy/test/test_circuit_breaker.cpp` — the
  closed/open/half-open state machine (State pattern).
- `proxy/src/origin/CircuitBreakerRegistry.h` — one breaker *per origin*, looked up by origin id.

**Hands-on checkpoint:** `test/chaos/kill_origin.sh` — kill the dummy origin mid-test and watch
the breaker open (fast 502s instead of hangs), matching Failure Scenario #1 in the blueprint.

---

## Stage 7 — Request coalescing

**Files:** `proxy/src/coalesce/InFlightRegistry.h` / `.cpp`
**Test:** `proxy/test/test_coalescing.cpp`

**Concepts:** the leader/waiter pattern; the specific race this solves — two requests for the
same missing key arriving microseconds apart on the same thread, before the first has registered
itself as in-flight (solved by registering the marker *synchronously before* the async origin
call, not after — Concurrency Model section of the blueprint).

**Hands-on checkpoint:** `test/concurrency/coalescing.sh` — fire 100 concurrent requests for one
missing key, assert exactly 1 origin fetch happened.

---

## Stage 8 — Redis integration (rules, purge, L2 tier)

**Files, in order:**
- `proxy/src/redis/RedisClient.h` / `.cpp` — low-level Redis wire protocol client.
- `proxy/src/redis/RuleStore.h` / `.cpp` + `proxy/test/test_rule_parse.cpp` — Repository pattern;
  last-known-good rule cache in process memory, refreshed on pub/sub notification with a periodic
  poll safety net.
- `proxy/src/redis/RedisL2Cache.h` / `.cpp` + `proxy/test/test_l2_serialize.cpp` — the shared L2
  tier: length-prefixed serialization, write-through on L1 miss, best-effort (a Redis outage
  degrades to an origin fetch, never an error).
- `proxy/src/redis/RedisCoordinator.h` / `.cpp` — ties it together: purge + rule-update pub/sub
  subscription, plus a heartbeat that backs the `edgecache_redis_connected` metric (Design
  Decision #7 — readiness reflects Redis, liveness doesn't).

**Read alongside:** Design Decisions #4, #5, #7 and Section 8 (Redis Architecture table) of the
blueprint.

**Hands-on checkpoint:** `test/chaos/kill_redis.sh` — kill Redis mid-test, confirm the proxy keeps
serving cached content (availability over freshness). Then `test/integration/purge_propagation.sh` —
purge one key, confirm it's evicted from every replica's L1 *and* the shared L2 within milliseconds.

---

## Stage 9 — Metrics

**Files:** `proxy/src/metrics/MetricsRegistry.h` / `.cpp`, `proxy/src/metrics/MetricsServer.h` / `.cpp`

**Concepts:** counters/histograms *injected* into components rather than a global singleton (so
components stay unit-testable in isolation); Prometheus text exposition format; `/healthz` vs
`/readyz` semantics.

**Hands-on checkpoint:** `curl http://localhost:9101/metrics` (proxy1's metrics port per the
compose file) and find `edgecache_requests_total`, `edgecache_circuit_breaker_state`,
`edgecache_inflight_coalesced_total` in the raw output.

---

## Stage 10 — Networking core (epoll event loop)

**Files:** `proxy/src/net/Listener.h` / `.cpp`, `proxy/src/net/EventLoop.h` / `.cpp`

**Concepts:** this is where the `SO_REUSEPORT` + `epoll` thread-per-core model from Phase 0
becomes real code — one `Listener`/`EventLoop` pair per worker thread, each with its own
listening socket on the same port.

---

## Stage 11 — Analytics sink (optional Kafka)

**Files:** `proxy/src/analytics/AccessLog.h`, `proxy/src/analytics/KafkaAccessLogSink.h` / `.cpp`

**Concepts:** Strategy pattern (`NullAccessLogSink` vs `KafkaAccessLogSink`); fire-and-forget
delivery; the `EDGECACHE_KAFKA` **compile-time** flag in `proxy/CMakeLists.txt` that keeps the
default build and unit tests free of any Kafka dependency.

---

## Stage 12 — Orchestration: tying the whole proxy together

**Files, in order:**
- `proxy/src/RequestHandler.h` / `.cpp` — the actual pipeline: cache lookup → policy decision →
  coalescing → circuit-guarded origin fetch → stale-while-revalidate → tiered store (L1 + L2).
  This is the single most important file to understand deeply — every prior stage is a dependency
  injected into this one.
- `proxy/src/ProxyServer.h` / `.cpp` — assembles one full replica: N worker threads sharing one
  `ShardedCache`, wires every component from Stages 3–11 together, owns startup/shutdown.
- `proxy/src/main.cpp` — the entrypoint: signal handling (`SIGINT`/`SIGTERM` → graceful `stop()`),
  `Config::fromEnv()`, `server.run()`.

**Study technique:** read `RequestHandler::process()` line by line, and for every call it makes,
point to which earlier stage owns that behavior. Then do the same for the miss-with-coalescing
and purge-propagation request lifecycles described in Section 7 of `edgecache_blueprint.md`,
matching each numbered step to actual code.

---

## Stage 13 — Proxy test harness & Docker build

**Files:** `proxy/test/test_main.cpp`, `proxy/test/test_framework.h`, `proxy/Dockerfile`,
`proxy/CMakeLists.txt`

**Hands-on checkpoint:** `docker build --target build -t edgecache-proxy-test ./proxy` — this runs
`ctest` inside the build stage (build fails if any test fails); this is also literally what CI
does. Read `.github/workflows/ci.yml` briefly now to confirm.

*(Proxy is now fully covered — everything referenced in `proxy/src/` and `proxy/test/` above
matches the actual repository contents.)*

---

## Stage 14 — Control plane (Node.js/TS management API)

**Files, in order:**
- `control-plane/src/config.ts` — env-driven config, mirrors Stage 5.
- `control-plane/src/db/pool.ts` — Postgres connection pool.
- `control-plane/src/db/migrate.ts` — schema setup, retries until Postgres is reachable (see it
  invoked in `index.ts`).
- `control-plane/src/db/origins.ts`, `rules.ts`, `purge.ts`, `stats.ts` — repository-style data
  access per table from Section 10 (Database Design) of the blueprint (`origins`, `cache_rules`,
  `purge_log`, `access_stats_rollup`).
- `control-plane/src/middleware/auth.ts` — the `requireAdmin` bearer-token check gating every
  route except `/health`.
- `control-plane/src/redis/coordinator.ts` — the TypeScript side of Redis coordination: mirrors
  rules to the `edgecache:rules` hash, publishes purge/rule-update, and does the L2 `SCAN`+`DEL`
  purge (`purgeL2`) *before* the pub/sub fan-out.
- `control-plane/src/routes/origins.ts`, `rules.ts`, `purge.ts`, `stats.ts` — the REST surface
  from Section 11 of the blueprint. **Read `purge.ts` most carefully** — it's the
  Postgres-write → L2-purge → pub/sub-publish sequence that is the entire thesis of the project.
- `control-plane/src/app.ts` — wires everything into an Express app.
- `control-plane/src/index.ts` — entrypoint: migrate → reconcile Redis from Postgres on startup →
  listen → graceful shutdown on `SIGINT`/`SIGTERM`.

**Test:** `control-plane/test/auth.test.ts`

**Hands-on checkpoint:** `cd control-plane && npm install && npm run typecheck && npm test`. Then
re-run the Stage 1 purge demo, but this time with `docs/api-examples.http` open, firing each
request manually and matching it to the route file that handles it.

---

## Stage 15 — Analytics consumer (Kafka → Postgres rollups)

**Files:** `analytics-consumer/src/config.ts`, `rollup.ts`, `index.ts`

**Concepts:** its own Kafka consumer group, rolling raw access-log events up into hourly per-path
stats written to `access_stats_rollup` — the consumer side of Section 9 (Kafka Architecture) in
the blueprint.

**Hands-on checkpoint:** bring up `kafka analytics-consumer` alongside the core stack, generate
some hit/miss traffic, and query `access_stats_rollup` in Postgres directly (`docker compose exec
postgres psql -U edgecache -d edgecache`) to see rolled-up rows appear.

---

## Stage 16 — Dummy origin

**File:** `dummy-origin/server.js`

**Concepts:** a trivial Express app with tweakable `Cache-Control` headers — this is what lets
you deliberately test every cacheability/TTL edge case from Stage 4 by hand.

---

## Stage 17 — Deployment & infrastructure

**Files, in order:**
- `proxy/Dockerfile`, `control-plane/Dockerfile`, `analytics-consumer/Dockerfile`,
  `dummy-origin/Dockerfile` — note the proxy's multi-stage build (a `build` target with the full
  toolchain + tests, a slim final runtime stage, statically-linked libstdc++/libgcc per
  `CMakeLists.txt`).
- `deploy/docker-compose.yml` — you've already read this; revisit it now and map every service
  back to the component that owns it.
- `deploy/k8s/base/` — `namespace.yaml`, `proxy-deployment.yaml`, `proxy-service.yaml`,
  `proxy-hpa.yaml`, `proxy-configmap.yaml`, `control-plane.yaml`, `analytics-consumer.yaml`,
  `ingress.yaml`, `network-policy.yaml`, `secrets.example.yaml`, `kustomization.yaml` — map each
  manifest to Section 15 (Kubernetes Architecture) of the blueprint, especially the
  HPA-on-request-rate story and the ConfigMap → memory-limit → cache-size relationship.
- `deploy/k8s/overlays/local/` — `infra.yaml`, `kustomization.yaml`, the local dev overlay
  (in-cluster Postgres/Redis/Kafka instead of managed services).
- `deploy/k8s/README.md`.

**Hands-on checkpoint:** `make k8s-local` (or `kubectl apply -k deploy/k8s/overlays/local`) on
Docker Desktop's local Kubernetes; watch `kubectl get pods -w`; run a load test and watch the HPA
scale replicas up and back down — this is the single most demonstrable K8s story in the project.

---

## Stage 18 — Observability

**Files:** `deploy/observability/prometheus.yml`,
`deploy/observability/grafana/provisioning/datasources/datasource.yml`,
`deploy/observability/grafana/provisioning/dashboards/dashboards.yml`,
`deploy/observability/grafana/dashboards/edgecache.json`

**Concepts:** scrape targets/intervals in the Prometheus config; how the Grafana dashboard panels
map to the metric names you found in Stage 9.

**Hands-on checkpoint:** bring up `prometheus grafana` too (watch for a possible port-3000
collision — compose maps Grafana to host port 3001), open the EdgeCache dashboard, generate load,
and watch hit rate / circuit-breaker-state panels move live.

---

## Stage 19 — End-to-end, concurrency & chaos tests

**Files:** `test/integration/purge_propagation.sh`, `test/concurrency/coalescing.sh`,
`test/chaos/kill_origin.sh`, `test/chaos/kill_redis.sh`, `test/load/hot-key.lua` (wrk),
`test/load/zipfian.js` (k6), `scripts/seed.sh`, `scripts/smoke.ps1`, `scripts/wsl_build.sh`

You've already run several of these in earlier stages — this pass is about reading the scripts
themselves, not just their output, so you understand *what* each one asserts and why (e.g. why
`purge_propagation.sh` checks for fleet convergence on one re-fetch rather than "every replica
MISS" now that the L2 tier exists).

**Hands-on checkpoint:** `make load-hot` and `make load-zipf`; compare the numbers you get to
`docs/BENCHMARKS.md`'s published figures on this same machine's Docker stack.

---

## Stage 20 — CI/CD

**Files:** `.github/workflows/ci.yml`, `.github/workflows/cd.yml`

**Concepts:** what runs automatically on push/PR (proxy Docker build + ctest, control-plane
typecheck + test) versus what the deploy workflow does.

---

## Stage 21 — Docs capstone (re-read with full context)

**Files:** `docs/BENCHMARKS.md`, `docs/DEMO.md`, `docs/FAILURE_MODES.md` (all 15 scenarios — you
should now be able to point at the exact code for each one), `docs/RUNBOOK.md`,
`docs/api-examples.http`

By now every number and every failure scenario should map to a specific file you've read and a
behavior you've triggered yourself.

---

## Capstone exercise (the real mastery test)

Pick one of the explicitly named, not-yet-built stretch goals from the README's "What's next" /
blueprint Section 22 and design (and optionally implement) it:
- Cluster-wide request coalescing via a Redis lock (extends Stage 7 + Stage 8).
- `Vary`-header cache-key support for content negotiation (extends Stage 3's `CacheKey`).
- Signed purge requests (extends Stage 14's `purge.ts` + auth middleware).

Being able to propose a concrete design for one of these — which files change, what breaks, what
trade-off you're making — is the clearest evidence you've reached contributor-level understanding,
not just read-through familiarity.

---

## Verification

There's no code change to verify here — this plan's own "hands-on checkpoints" are the
verification: by the end you should have (1) run the full stack via Docker Compose, (2) triggered
every one of the 15 documented failure scenarios yourself via the chaos/integration scripts, (3)
built and run the proxy's C++ unit tests and the control plane's TS tests, and (4) deployed to
local Kubernetes and watched the HPA scale under load.
