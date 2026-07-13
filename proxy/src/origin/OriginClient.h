#pragma once
#include <cstdint>
#include <string>

#include "http/Http.h"

namespace edgecache {

struct OriginResult {
    bool ok = false;         // true if we got a well-formed HTTP response
    bool timedOut = false;   // distinguishes timeout from connect/protocol failure
    HttpResponse response;
    std::string error;
};

struct OriginTarget {
    std::string host;
    uint16_t port = 80;
    int connectTimeoutMs = 2000;
    int readTimeoutMs = 5000;
};

// Adapter over "make one upstream HTTP request". The interface is what makes the
// request path unit-testable: FakeOriginClient substitutes for a real network
// call so coalescing / policy / circuit-breaker logic can be tested deterministically.
class OriginClient {
public:
    virtual ~OriginClient() = default;
    virtual OriginResult fetch(const HttpRequest& req, const OriginTarget& target) = 0;
};

}  // namespace edgecache
