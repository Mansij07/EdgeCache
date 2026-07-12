// k6 load test against EdgeCache with a Zipfian key distribution — a realistic
// "some URLs are far more popular than others" traffic shape, which produces a
// meaningful hit-rate number rather than an artificially perfect one.
//
//   k6 run -e BASE=http://localhost:8080 -e KEYSPACE=1000 test/load/zipfian.js
//
// Reports the cache hit rate observed from the X-Cache response header.
import http from 'k6/http';
import { check } from 'k6';
import { Rate, Trend } from 'k6/metrics';

const BASE = __ENV.BASE || 'http://localhost:8080';
const KEYSPACE = parseInt(__ENV.KEYSPACE || '1000', 10);
const ZIPF_S = parseFloat(__ENV.ZIPF_S || '1.1'); // skew; higher = more concentrated

export const options = {
  scenarios: {
    ramp: {
      executor: 'ramping-vus',
      startVUs: 10,
      stages: [
        { duration: '20s', target: 50 },
        { duration: '40s', target: 200 },
        { duration: '20s', target: 0 },
      ],
    },
  },
};

const hitRate = new Rate('edgecache_hit_rate');
const hitLatency = new Trend('edgecache_hit_latency_ms', true);
const missLatency = new Trend('edgecache_miss_latency_ms', true);

// Precompute a Zipfian CDF over [1, KEYSPACE].
const weights = [];
let norm = 0;
for (let k = 1; k <= KEYSPACE; k++) {
  norm += 1 / Math.pow(k, ZIPF_S);
}
let acc = 0;
for (let k = 1; k <= KEYSPACE; k++) {
  acc += 1 / Math.pow(k, ZIPF_S) / norm;
  weights.push(acc);
}

function zipfSample() {
  const r = Math.random();
  // Binary search the CDF.
  let lo = 0;
  let hi = weights.length - 1;
  while (lo < hi) {
    const mid = (lo + hi) >> 1;
    if (weights[mid] < r) lo = mid + 1;
    else hi = mid;
  }
  return lo + 1; // key id in [1, KEYSPACE]
}

export default function () {
  const id = zipfSample();
  const res = http.get(`${BASE}/products/${id}`);
  const xcache = res.headers['X-Cache'] || 'NONE';
  const isHit = xcache === 'HIT' || xcache === 'STALE';
  hitRate.add(isHit);
  if (isHit) hitLatency.add(res.timings.duration);
  else missLatency.add(res.timings.duration);
  check(res, { 'status 200': (r) => r.status === 200 });
}
