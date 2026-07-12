#pragma once
#include <cstdint>
#include <string>

#include "http/Http.h"

namespace edgecache {

struct CacheDecision {
    bool cacheable = false;
    uint64_t ttlSeconds = 0;
    uint64_t staleWhileRevalidateSeconds = 0;
    std::string reason;  // human-readable, surfaced in logs / X-Cache-Policy
};

// Strategy: decide whether/how long a response may be cached for a given path.
class CachePolicy {
public:
    virtual ~CachePolicy() = default;
    virtual CacheDecision decide(const std::string& path, const HttpResponse& resp) const = 0;
};

// Reads standard Cache-Control directives from the origin response.
//  - no-store / private / no-cache  -> not cacheable
//  - max-age=N                      -> ttl = N
//  - s-maxage=N                     -> takes precedence over max-age for shared caches
//  - otherwise                      -> defaultTtlSeconds (heuristic)
// Only success-ish statuses are cached (200, 203, 204, 301, 404).
class HeaderBasedPolicy : public CachePolicy {
public:
    explicit HeaderBasedPolicy(uint64_t defaultTtlSeconds)
        : defaultTtl_(defaultTtlSeconds) {}
    CacheDecision decide(const std::string& path, const HttpResponse& resp) const override;

private:
    uint64_t defaultTtl_;
};

}  // namespace edgecache
