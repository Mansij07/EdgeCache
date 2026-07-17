#pragma once
#include <cstdint>
#include <string>

#include "http/Http.h"

namespace edgecache {

struct OriginResult {
    bool ok = false;
    bool timedOut = false;
    HttpResponse response;
    std::string error;
};

struct OriginTarget {
    std::string host;
    uint16_t port = 80;
    int connectTimeoutMs = 2000;
    int readTimeoutMs = 5000;
};

class OriginClient {
public:
    virtual ~OriginClient() = default;
    virtual OriginResult fetch(const HttpRequest& req, const OriginTarget& target) = 0;
};

}
