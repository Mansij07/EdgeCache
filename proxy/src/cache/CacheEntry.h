#pragma once
#include <chrono>
#include <cstdint>
#include <string>

#include "http/Http.h"

namespace edgecache {

using Clock = std::chrono::steady_clock;

struct CacheEntry {
    int status = 200;
    std::string reason;
    Headers headers;
    std::string body;

    Clock::time_point storedAt;
    uint64_t ttlSeconds = 0;
    uint64_t staleWhileRevalidateSeconds = 0;
    std::string etag;

    size_t sizeBytes() const {
        size_t n = body.size() + reason.size() + etag.size() + 64;
        for (const auto& kv : headers) n += kv.first.size() + kv.second.size() + 8;
        return n;
    }

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

}
