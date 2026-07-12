-- wrk script hammering a single hot key — measures pure cache-hit throughput
-- and latency (the sub-millisecond in-process path).
--
--   wrk -t4 -c100 -d30s -s test/load/hot-key.lua http://localhost:8080
--
-- After the first request warms the cache, essentially every response is a HIT.

wrk.method = "GET"

local hits = 0
local misses = 0

request = function()
  return wrk.format("GET", "/products/42")
end

response = function(status, headers, body)
  local xc = headers["X-Cache"]
  if xc == "HIT" or xc == "STALE" then
    hits = hits + 1
  else
    misses = misses + 1
  end
end

done = function(summary, latency, requests)
  local total = hits + misses
  io.write("------------------------------\n")
  io.write(string.format("Requests:   %d\n", total))
  io.write(string.format("Cache HITs: %d (%.2f%%)\n", hits, total > 0 and hits / total * 100 or 0))
  io.write(string.format("Latency p50: %.3f ms\n", latency:percentile(50) / 1000))
  io.write(string.format("Latency p99: %.3f ms\n", latency:percentile(99) / 1000))
end
