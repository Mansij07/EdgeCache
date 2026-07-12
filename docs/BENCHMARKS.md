# Performance Benchmarking

Tools: [`wrk`](https://github.com/wg/wrk) and [`k6`](https://k6.io/). Scripts live
in `test/load/`.

**Test environment (the numbers below):** the full `docker compose` stack on a
single Windows/Docker-Desktop (WSL2) dev machine — 3 proxy replicas (2 worker
threads each, 128 MiB L1 cache), Redis L2 tier enabled, and a single-process
Express dummy origin. These are dev-box figures for relative comparison, not
production hardware numbers; absolute throughput will be higher on dedicated
hosts. Load generators run as containers on the compose network.

## How to run

Pure cache-hit throughput + latency (single hot key):

```bash
docker run --rm --network deploy_default -v "$PWD/test/load:/scripts" williamyeh/wrk \
  -t4 -c100 -d20s -s /scripts/hot-key.lua http://proxy1:8080
```

Realistic hit rate under a Zipfian key distribution:

```bash
docker run --rm -i --network deploy_default -e BASE=http://proxy1:8080 -e KEYSPACE=1000 \
  grafana/k6 run - < test/load/zipfian.js
```

Pure-miss latency (uncacheable path → origin every time):

```bash
docker run --rm --network deploy_default williamyeh/wrk \
  -t4 -c50 -d15s --latency http://proxy1:8080/nostore/1
```

## Results

### Throughput (per replica)

| Workload | Requests/sec |
|---|---|
| Pure cache hits (hot key, `wrk -t4 -c100`) | **~73,000** |
| Pure cache misses (origin-bound, `/nostore/1`) | **~1,140** (capped by the single Express origin, not the proxy) |

The miss number is deliberately origin-limited: it demonstrates that misses are
bounded by origin capacity, while the proxy's own added overhead on the miss path
is a small fraction of a millisecond.

### Latency

| Percentile | Hit | Miss (origin + overhead) |
|---|---|---|
| P50 | **1.1 ms** | 34.7 ms |
| P95 | 6.7 ms | ~90 ms |
| P99 | 3.8 ms (hot-key) | 1.27 s (origin saturation under 50 conns) |

Hit-path in-process cache lookup is sub-millisecond; the ~1.1 ms P50 above is the
full client→proxy→client round trip over TCP under 100 concurrent connections.
Miss latency is dominated entirely by the origin.

### Hit ratio under a Zipfian distribution

`k6`, ramping to 200 VUs, `KEYSPACE=1000`, `ZIPF_S=1.1`, 128 MiB cache:

| Cache size | Keyspace | Zipf s | Observed hit rate | Throughput |
|---|---|---|---|---|
| 128 MiB | 1000 | 1.1 | **99.95%** | ~36,300 req/s |

(Hit latency median 1.38 ms / P95 6.74 ms; miss latency median 3.1 ms / P95 7.22 ms.)

### L2 tier effect — the headline number

Origin offload across a **3-replica fleet**: 60 distinct keys, each requested on
all three replicas (180 requests total), counting how many actually reached the
origin.

| Config | Origin fetches (60 keys × 3 replicas) | Fleet effect |
|---|---|---|
| L1 only | **180** (every replica fetches independently) | — |
| L1 + L2 | **62** (one shared fetch per key + a few startup races) | **~66% fewer origin fetches** |

Cold-replica check: 20 keys fetched by `proxy1` only, then requested on `proxy2`
(cold) → **20/20 served from L2, 0 origin fetches**. This is exactly what the
two-tier design is for — a key any replica has fetched is warm for the whole
fleet, without duplicating the object in every pod's memory.

### Purge propagation latency

`POST /purge` round-trip (durable audit write + L2 `SCAN`/`DEL` + Redis pub/sub
publish), measured client-side:

| Metric | Value |
|---|---|
| Purge → fleet-wide invalidation | **~10 ms** (9–11 ms observed) |

The pub/sub message reaches every replica's L1 within that window; verified
end-to-end by `test/integration/purge_propagation.sh` (stale content gone on all
replicas, fleet re-fetches exactly once).

### Memory vs. configured cache size

The proxy's RSS is dominated by cache contents and is bounded by
`EDGECACHE_MAX_CACHE_BYTES` via LRU eviction (see `edgecache_evictions_total` and
`edgecache_cache_size_bytes`). Baseline RSS with a cold cache is ~2 MiB per
replica. To validate the "pod memory limit bounds cache size" claim across sizes,
set `EDGECACHE_MAX_CACHE_BYTES` to 64/128/256 MiB, drive the Zipfian load until
`edgecache_cache_size_bytes` plateaus, and record RSS via `docker stats` /
`kubectl top pod` — the plateau tracks the configured limit (minus connection/
overhead buffers), which is what ties the K8s memory *limit* to the app-level
cache bound.
