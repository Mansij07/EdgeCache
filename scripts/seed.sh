#!/usr/bin/env bash
# Seed the control plane with an origin and a couple of path-pattern rules.
# Run after `docker compose up`.
set -euo pipefail

CONTROL="${CONTROL:-http://localhost:9000}"
ADMIN_KEY="${ADMIN_API_KEY:-dev-admin-key}"
auth=(-H "Authorization: Bearer $ADMIN_KEY" -H "Content-Type: application/json")

echo "Registering origin..."
origin=$(curl -s "${auth[@]}" -X POST "$CONTROL/origins" \
  -d '{"host":"dummy-origin","baseUrl":"http://dummy-origin:8081","healthCheckPath":"/health"}')
echo "  $origin"
origin_id=$(echo "$origin" | grep -o '"id":"[^"]*"' | head -1 | cut -d'"' -f4)

echo "Creating rule: /products/* cached 120s with 30s stale-while-revalidate..."
curl -s "${auth[@]}" -X POST "$CONTROL/rules" \
  -d "{\"pathPattern\":\"/products/*\",\"ttlSeconds\":120,\"staleWhileRevalidateSeconds\":30,\"originId\":\"$origin_id\"}"
echo

echo "Creating rule: /nostore/* explicitly not cached (ttl 0)..."
curl -s "${auth[@]}" -X POST "$CONTROL/rules" \
  -d "{\"pathPattern\":\"/nostore/*\",\"ttlSeconds\":0,\"staleWhileRevalidateSeconds\":0,\"originId\":\"$origin_id\"}"
echo

echo "Done. Current rules:"
curl -s "${auth[@]}" "$CONTROL/rules"
echo
