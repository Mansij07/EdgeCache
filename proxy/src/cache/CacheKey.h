#pragma once
#include <string>

#include "http/Http.h"

namespace edgecache {

// A cache key is computed from method + host + normalized path + normalized
// query. Query parameters are sorted so "?a=1&b=2" and "?b=2&a=1" collapse to
// one key. Only whitelisted request headers may ever influence the key (Vary
// support is a stretch goal) — arbitrary attacker-controlled headers are never
// mixed in, which is the defense against cache-key poisoning.
struct CacheKey {
    std::string value;  // canonical string form

    bool operator==(const CacheKey& o) const { return value == o.value; }

    // Build from a request. `host` is the effective origin host used for routing.
    static CacheKey fromRequest(const HttpRequest& req, const std::string& host);
};

// Normalize a raw query string: percent-decode-safe splitting on '&', sort
// params lexicographically, drop empties. Exposed for unit testing.
std::string normalizeQuery(const std::string& rawQuery);

struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const {
        return std::hash<std::string>{}(k.value);
    }
};

}  // namespace edgecache
