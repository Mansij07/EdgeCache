#pragma once
#include <cstdint>
#include <string>

#include "http/Http.h"

namespace edgecache {

struct CacheDecision {
    bool cacheable = false;
    uint64_t ttlSeconds = 0;
    uint64_t staleWhileRevalidateSeconds = 0;
    std::string reason;
};

class CachePolicy {
public:
    virtual ~CachePolicy() = default;
    virtual CacheDecision decide(const std::string& path, const HttpResponse& resp) const = 0;
};

class HeaderBasedPolicy : public CachePolicy {
public:
    explicit HeaderBasedPolicy(uint64_t defaultTtlSeconds)
        : defaultTtl_(defaultTtlSeconds) {}
    CacheDecision decide(const std::string& path, const HttpResponse& resp) const override;

private:
    uint64_t defaultTtl_;
};

}
