#pragma once
#include <string>

#include "http/Http.h"

namespace edgecache {

struct CacheKey {
    std::string value;

    bool operator==(const CacheKey& o) const { return value == o.value; }

    static CacheKey fromRequest(const HttpRequest& req, const std::string& host);
};

std::string normalizeQuery(const std::string& rawQuery);

struct CacheKeyHash {
    size_t operator()(const CacheKey& k) const {
        return std::hash<std::string>{}(k.value);
    }
};

}
