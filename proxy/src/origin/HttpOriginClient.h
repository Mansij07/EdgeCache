#pragma once
#include "origin/OriginClient.h"

namespace edgecache {

// Concrete OriginClient using a blocking TCP socket with connect/read timeouts.
//
// The blocking fetch runs on the worker thread that had the cache miss. Because
// there are N worker threads (thread-per-core) and every origin call is bounded
// by connect+read timeouts and guarded by a circuit breaker, one slow origin
// degrades at most that thread's throughput rather than the whole process. A
// fully non-blocking origin state machine within the event loop is the more
// advanced design; this bounded-blocking approach is the documented simplification.
class HttpOriginClient : public OriginClient {
public:
    OriginResult fetch(const HttpRequest& req, const OriginTarget& target) override;
};

}  // namespace edgecache
