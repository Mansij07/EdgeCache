#!/usr/bin/env bash
# Chaos test: kill Redis and assert the proxy KEEPS SERVING cached content from
# L1 (availability over freshness). Purge propagation pauses, but no
# cache-serving request fails because of the Redis outage.
set -euo pipefail

PROXY="http://localhost:8080"
COMPOSE_DIR="$(dirname "$0")/../../deploy"
URL="$PROXY/products/redis-chaos"

fail() { echo "FAIL: $*" >&2; exit 1; }
xcache() { curl -s -D - -o /dev/null "$1" | tr -d '\r' | awk -F': ' 'tolower($1)=="x-cache"{print $2}'; }

echo "Warm the cache..."
curl -s -o /dev/null "$URL"
[ "$(xcache "$URL")" = "HIT" ] || fail "did not warm to HIT"

echo "Killing Redis..."
(cd "$COMPOSE_DIR" && docker compose stop redis >/dev/null)
sleep 2

echo "Cached content must still be served from L1 with Redis down:"
hit=$(xcache "$URL")
echo "  X-Cache=$hit"
[ "$hit" = "HIT" ] || fail "proxy stopped serving cached content when Redis went down"

rc_metric() { curl -s "http://localhost:9101/metrics" | awk '/^edgecache_redis_connected /{print $2}'; }

echo "Readiness must STAY 200 during the outage (readiness is NOT gated on Redis;"
echo "the proxy still serves from L1, so it must remain in rotation):"
code=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:9101/readyz")
echo "  /readyz -> $code (expect 200)"
[ "$code" = "200" ] || fail "readiness dropped when Redis went down (should not be gated on Redis)"

echo "But the edgecache_redis_connected metric must reflect the outage within ~2s"
echo "(fast PING heartbeat):"
rc=""
for _ in $(seq 1 8); do
  rc=$(rc_metric)
  [ "$rc" = "0" ] && break
  sleep 1
done
echo "  edgecache_redis_connected=$rc (expect 0)"
[ "$rc" = "0" ] || fail "redis_connected did not flip to 0 after Redis was killed"

echo "Restarting Redis..."
(cd "$COMPOSE_DIR" && docker compose start redis >/dev/null)

echo "Metric should recover to 1 within a few seconds:"
rc=""
for _ in $(seq 1 15); do
  rc=$(rc_metric)
  [ "$rc" = "1" ] && break
  sleep 1
done
echo "  edgecache_redis_connected=$rc (expect 1)"
[ "$rc" = "1" ] || fail "redis_connected did not recover to 1 after Redis restart"

code=$(curl -s -o /dev/null -w "%{http_code}" "http://localhost:9101/readyz")
echo "  /readyz throughout -> $code (expect 200)"

echo "PASS: served from L1 throughout; readiness stayed up; redis_connected tracked the outage."
