#!/usr/bin/env bash
# Chaos test: kill the origin and assert the circuit breaker opens so requests
# fast-fail (fast 5xx) instead of hanging on a timeout. Then bring it back and
# assert recovery (half-open -> closed).
set -euo pipefail

PROXY="http://localhost:8080"
COMPOSE_DIR="$(dirname "$0")/../../deploy"

status_and_time() {
  # prints "<http_status> <total_time_s>"
  curl -s -o /dev/null -w "%{http_code} %{time_total}" "$PROXY/products/chaos-$RANDOM"
}

echo "Baseline (origin up):"; status_and_time; echo

echo "Killing dummy-origin..."
(cd "$COMPOSE_DIR" && docker compose stop dummy-origin >/dev/null)

echo "Sending requests to trip the breaker..."
for _ in $(seq 1 8); do curl -s -o /dev/null "$PROXY/products/chaos-$RANDOM" || true; done

echo "Now requests should fast-fail (low time_total, 5xx):"
# `curl -w` emits no trailing newline, so `read` returns non-zero at EOF even
# though it populated the vars; guard it so `set -e` doesn't abort here (which
# would skip the origin restart below and leave the stack broken).
read -r code t < <(status_and_time) || true
echo "  status=$code time=${t}s"
# With the breaker open, response should be quick and a 5xx.
awk -v t="$t" 'BEGIN{ exit !(t < 1.0) }' || { echo "FAIL: response too slow; breaker may not be open" >&2; }

echo "Restarting dummy-origin..."
(cd "$COMPOSE_DIR" && docker compose start dummy-origin >/dev/null)
sleep 8  # allow health + breaker half-open probe

echo "After recovery:"; status_and_time; echo
echo "Done. Inspect proxy /metrics -> edgecache_circuit_breaker_state to see the transitions."
