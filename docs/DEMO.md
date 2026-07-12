# Demo (3–5 minutes)

Prereqs: `docker compose up --build` from `deploy/`, then `bash scripts/seed.sh`
(or `scripts\smoke.ps1` on Windows for a quick version).

## 0:30 — Bring it up

```bash
cd deploy && docker compose up --build -d
docker compose ps          # 3 proxies, control plane, redis, postgres, kafka, origin, analytics
```

## 1:00 — MISS then HIT, across replicas

```bash
# proxy1: first request is a MISS (fetched from origin, then cached)
curl -i http://localhost:8080/products/1 | grep -i x-cache      # X-Cache: MISS
curl -i http://localhost:8080/products/1 | grep -i x-cache      # X-Cache: HIT

# Same URL on a DIFFERENT replica (proxy2) — its own L1, so first hit is a MISS,
# then HIT. Each replica caches independently (explain L1-per-replica design).
curl -i http://localhost:8082/products/1 | grep -i x-cache
```

Point out the sub-millisecond hit: `curl -w '%{time_total}\n'` on the second request.

## 1:00 — The thesis: fleet-wide purge in one call

```bash
# Warm all three replicas first
for p in 8080 8082 8083; do curl -s -o /dev/null http://localhost:$p/products/1; done

# Purge once on the control plane...
curl -X POST http://localhost:9000/purge \
  -H 'Authorization: Bearer dev-admin-key' -H 'Content-Type: application/json' \
  -d '{"pattern":"/products/1"}'

# ...and EVERY replica immediately MISSes again — not on next TTL, instantly.
for p in 8080 8082 8083; do
  echo -n "proxy:$p "; curl -si http://localhost:$p/products/1 | grep -i x-cache
done
```

Or run the automated version: `bash test/integration/purge_propagation.sh`.

## 1:00 — Circuit breaker on origin failure

```bash
docker compose stop dummy-origin
# Requests now fast-fail (503, low latency) instead of hanging:
curl -s -o /dev/null -w 'status=%{http_code} time=%{time_total}s\n' http://localhost:8080/products/9
# Watch the breaker state in Grafana (http://localhost:3000) or:
curl -s http://localhost:9101/metrics | grep circuit_breaker_state
docker compose start dummy-origin
```

## 1:00 — Autoscaling (Kubernetes)

See `deploy/k8s/README.md`. On kind/minikube:

```bash
kubectl apply -k deploy/k8s/overlays/local
kubectl -n edgecache get pods -w          # watch replicas
# generate load, then watch the HPA scale up:
kubectl -n edgecache get hpa edgecache-proxy -w
```

## Bonus — analytics pipeline

After some traffic, the Kafka → analytics-consumer → Postgres rollup is queryable:

```bash
curl -s -H 'Authorization: Bearer dev-admin-key' \
  'http://localhost:9000/stats?path=/products/1' | jq
```
