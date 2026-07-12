#!/usr/bin/env bash
# Concurrency test: fire many concurrent requests for the SAME cold key at one
# replica and assert the origin was hit ~once, proving request coalescing.
#
# Note: the deterministic proof lives in the C++ unit test
# (test_coalescing.cpp::coalescing_collapses_concurrent_misses_to_one_fetch);
# this script is the end-to-end sanity check against the running stack.
set -euo pipefail

PROXY="http://localhost:8080"
ORIGIN="http://localhost:8081"
N="${N:-100}"
# A fresh, never-requested key so the miss path (and coalescing) is exercised.
KEY="/products/coalesce-$RANDOM$RANDOM"

before=$(curl -s "$ORIGIN/__origin_stats" | grep -o '"originHits":[0-9]*' | cut -d: -f2)

echo "Firing $N concurrent requests at $PROXY$KEY ..."
pids=()
for _ in $(seq 1 "$N"); do
  curl -s -o /dev/null "$PROXY$KEY" &
  pids+=($!)
done
wait "${pids[@]}"

after=$(curl -s "$ORIGIN/__origin_stats" | grep -o '"originHits":[0-9]*' | cut -d: -f2)
delta=$((after - before))

echo "Origin hits during burst: $delta (expected ~1, definitely << $N)"
# Allow a small margin for timing, but it must be dramatically less than N.
if [ "$delta" -le 5 ]; then
  echo "PASS: $N concurrent misses coalesced into $delta origin fetch(es)."
else
  echo "WARN: coalescing weaker than expected ($delta origin hits). Check timing/overlap." >&2
  exit 1
fi
