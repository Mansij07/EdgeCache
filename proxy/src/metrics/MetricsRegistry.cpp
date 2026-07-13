#include "metrics/MetricsRegistry.h"

#include <sstream>

namespace edgecache {

// Note: kBounds/kBuckets are static constexpr members — implicitly inline in
// C++17, so no out-of-line definition is required here.

void MetricsRegistry::observeOriginLatency(double seconds) {
    std::lock_guard<std::mutex> lk(histMutex_);
    for (int i = 0; i < kBuckets; ++i) {
        if (seconds <= kBounds[i]) bucketCounts_[i]++;
    }
    histSum_ += seconds;
    histCount_++;
}

std::string MetricsRegistry::render() const {
    std::ostringstream os;

    auto counter = [&](const char* name, const char* help, uint64_t v) {
        os << "# HELP " << name << ' ' << help << "\n";
        os << "# TYPE " << name << " counter\n";
        os << name << ' ' << v << "\n";
    };
    auto gauge = [&](const char* name, const char* help, uint64_t v) {
        os << "# HELP " << name << ' ' << help << "\n";
        os << "# TYPE " << name << " gauge\n";
        os << name << ' ' << v << "\n";
    };

    os << "# HELP edgecache_requests_total Total requests by cache result\n";
    os << "# TYPE edgecache_requests_total counter\n";
    os << "edgecache_requests_total{result=\"hit\"} " << hits_.load() << "\n";
    os << "edgecache_requests_total{result=\"miss\"} " << misses_.load() << "\n";

    os << "# HELP edgecache_l2_requests_total Shared Redis L2 tier lookups by result\n";
    os << "# TYPE edgecache_l2_requests_total counter\n";
    os << "edgecache_l2_requests_total{result=\"hit\"} " << l2Hits_.load() << "\n";
    os << "edgecache_l2_requests_total{result=\"miss\"} " << l2Misses_.load() << "\n";

    counter("edgecache_stale_served_total",
            "Responses served stale during revalidation", staleServed_.load());
    counter("edgecache_origin_errors_total", "Origin fetch failures", originErrors_.load());
    counter("edgecache_circuit_breaker_rejections_total",
            "Requests fast-failed by an open circuit breaker", circuitRejects_.load());
    counter("edgecache_bytes_served_total", "Total response bytes served", bytesServed_.load());
    counter("edgecache_purge_messages_total", "Purge pub/sub messages received", purgeMessages_.load());
    counter("edgecache_purged_keys_total", "Cache keys evicted by purge", purgedKeys_.load());
    counter("edgecache_evictions_total", "LRU evictions across shards", evictionsFn());
    counter("edgecache_inflight_coalesced_total",
            "Requests coalesced onto an in-flight origin fetch", coalescedFn());

    gauge("edgecache_cache_size_bytes", "Approximate in-process cache size", cacheSizeBytesFn());
    gauge("edgecache_cache_entries", "Number of cached entries", cacheEntriesFn());
    gauge("edgecache_rules_loaded", "Number of override rules in the local store", ruleCountFn());
    gauge("edgecache_redis_connected", "1 if Redis is currently reachable",
          redisConnectedFn() ? 1 : 0);

    // Circuit breaker state per origin, one-hot encoded.
    os << "# HELP edgecache_circuit_breaker_state Circuit breaker state (1=active)\n";
    os << "# TYPE edgecache_circuit_breaker_state gauge\n";
    for (const auto& [origin, state] : circuitStatesFn()) {
        for (const char* s : {"closed", "open", "half_open"}) {
            os << "edgecache_circuit_breaker_state{origin=\"" << origin << "\",state=\"" << s
               << "\"} " << (state == s ? 1 : 0) << "\n";
        }
    }

    // Origin latency histogram.
    {
        std::lock_guard<std::mutex> lk(histMutex_);
        os << "# HELP edgecache_origin_latency_seconds Origin fetch latency\n";
        os << "# TYPE edgecache_origin_latency_seconds histogram\n";
        for (int i = 0; i < kBuckets; ++i) {
            os << "edgecache_origin_latency_seconds_bucket{le=\"" << kBounds[i] << "\"} "
               << bucketCounts_[i] << "\n";
        }
        os << "edgecache_origin_latency_seconds_bucket{le=\"+Inf\"} " << histCount_ << "\n";
        os << "edgecache_origin_latency_seconds_sum " << histSum_ << "\n";
        os << "edgecache_origin_latency_seconds_count " << histCount_ << "\n";
    }

    return os.str();
}

}  // namespace edgecache
