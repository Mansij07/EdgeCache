#!/usr/bin/env bash
# Integration test: prove a purge on the control plane invalidates a cached key
# across the WHOLE fleet — including the shared Redis L2 tier.
#
# Requires the docker compose stack to be up:  (cd deploy && docker compose up -d)
#
# NOTE ON L2: with the shared L2 tier enabled, "every replica independently
# returns X-Cache: MISS after a purge" is NOT the right invariant. After a purge,
# the first replica to miss re-fetches from origin ONCE and re-populates the
# shared L2, so the other replicas serve that FRESH copy from L2 (fleet-wide
# thundering-herd protection — the whole point of L2). The correct guarantees are:
#   * the stale copy is gone everywhere (the served content nonce changes), and
#   * the fleet converges on ONE freshly-fetched copy (a single origin re-fetch).
# This also still catches an L1-eviction failure: a replica that kept its stale
# copy would serve the OLD nonce and fail step 3.
set -euo pipefail

PROXIES=("http://localhost:8080" "http://localhost:8082" "http://localhost:8083")
CONTROL="http://localhost:9000"
ORIGIN="http://localhost:8081"
ADMIN_KEY="${ADMIN_API_KEY:-dev-admin-key}"
URL_PATH="/products/purge-$RANDOM$RANDOM"

fail() { echo "FAIL: $*" >&2; exit 1; }
nonce() { curl -s "$1$URL_PATH" | grep -o '"nonce":"[^"]*"'; }
origin_hits() {
  curl -s "$ORIGIN/__origin_stats" | grep -o "\"${URL_PATH}\":[0-9]*" | cut -d: -f2
}

echo "1) Warm every replica for $URL_PATH (shared via L2)..."
for p in "${PROXIES[@]}"; do curl -s -o /dev/null "$p$URL_PATH"; done
before_nonce=$(nonce "${PROXIES[0]}")
[ -n "$before_nonce" ] || fail "no content served while warming"
echo "   cached nonce (fleet-wide): $before_nonce"

echo "2) Purge $URL_PATH across the fleet (L1 on every replica + shared L2)..."
resp=$(curl -s -X POST "$CONTROL/purge" \
  -H "Authorization: Bearer $ADMIN_KEY" \
  -H 'Content-Type: application/json' \
  -d "{\"pattern\":\"$URL_PATH\"}")
echo "   $resp"
before_hits=$(origin_hits)
# Give pub/sub a beat to fan out (should be milliseconds; allow generous margin).
sleep 1

echo "3) Every replica must now serve FRESH content (stale copy evicted everywhere)..."
declare -A seen
for p in "${PROXIES[@]}"; do
  n=$(nonce "$p")
  echo "   $p -> $n"
  [ "$n" != "$before_nonce" ] || fail "$p still served the pre-purge (stale) copy"
  seen["$n"]=1
done

echo "4) The fleet must converge on ONE freshly-fetched copy (shared via L2)..."
[ "${#seen[@]}" -eq 1 ] || fail "replicas served ${#seen[@]} distinct copies; L2 not shared"
after_hits=$(origin_hits)
delta=$((after_hits - before_hits))
echo "   origin re-fetches after purge: $delta (expect 1 — a single shared re-fetch)"
[ "$delta" -le 1 ] || fail "expected a single shared re-fetch, got $delta"

echo "PASS: purge evicted stale content across all ${#PROXIES[@]} replicas (L1 + shared L2);"
echo "      the fleet re-fetched exactly once and converged on the fresh copy."
