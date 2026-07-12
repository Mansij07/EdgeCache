#pragma once
#include <chrono>
#include <cstdint>
#include <string>

#include "http/Http.h"

namespace edgecache {

using Clock = std::chrono::steady_clock;

// A stored, cacheable response plus the metadata needed to decide freshness
// and stale-while-revalidate behavior.
struct CacheEntry {
    int status = 200;
    std::string reason;
    Headers headers;
    std::string body;

    Clock::time_point storedAt;
    uint64_t ttlSeconds = 0;                 // hard freshness lifetime
    uint64_t staleWhileRevalidateSeconds = 0;  // extra window to serve stale during refresh
    std::string etag;

    // Approximate heap footprint for byte-accurate LRU accounting.
    size_t sizeBytes() const {
        size_t n = body.size() + reason.size() + etag.size() + 64;
        for (const auto& kv : headers) n += kv.first.size() + kv.second.size() + 8;
        return n;
    }

    // Fresh: within ttl. Stale-but-serveable: within ttl + swr window.
    bool isFresh(Clock::time_point now) const {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - storedAt).count();
        return static_cast<uint64_t>(age) < ttlSeconds;
    }
    bool isServeableStale(Clock::time_point now) const {
        auto age = std::chrono::duration_cast<std::chrono::seconds>(now - storedAt).count();
        return static_cast<uint64_t>(age) < ttlSeconds + staleWhileRevalidateSeconds;
    }

    HttpResponse toResponse() const {
        HttpResponse r;
        r.status = status;
        r.reason = reason;
        r.headers = headers;
        r.body = body;
        return r;
    }
};

}  // namespace edgecache
