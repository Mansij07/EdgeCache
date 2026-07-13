#pragma once
#include <cstdint>
#include <string>

namespace edgecache {

// One access-log event per request. Mirrors the Kafka edgecache.access-log
// schema: {timestamp, replicaId, cacheKey, result, latencyMs, bytesServed}.
struct AccessLogEvent {
    int64_t timestampMs = 0;
    std::string replicaId;
    std::string path;
    std::string cacheKey;
    std::string result;   // "hit" | "miss" | "stale" | "pass"
    int status = 0;
    double latencyMs = 0.0;
    uint64_t bytesServed = 0;
};

// Sink for access-log events. Implementations MUST be non-blocking and MUST NOT
// throw — a cache-serving request is never allowed to slow down or fail because
// of analytics. Under back-pressure, dropping events is the correct behavior.
class AccessLogSink {
public:
    virtual ~AccessLogSink() = default;
    virtual void log(const AccessLogEvent& ev) noexcept = 0;
};

// Default sink: does nothing. Used in unit tests and whenever the Kafka feature
// is compiled out or unconfigured.
class NullAccessLogSink : public AccessLogSink {
public:
    void log(const AccessLogEvent&) noexcept override {}
};

}  // namespace edgecache
