#pragma once
#include <cstdint>
#include <string>

namespace edgecache {

struct AccessLogEvent {
    int64_t timestampMs = 0;
    std::string replicaId;
    std::string path;
    std::string cacheKey;
    std::string result;
    int status = 0;
    double latencyMs = 0.0;
    uint64_t bytesServed = 0;
};

class AccessLogSink {
public:
    virtual ~AccessLogSink() = default;
    virtual void log(const AccessLogEvent& ev) noexcept = 0;
};

class NullAccessLogSink : public AccessLogSink {
public:
    void log(const AccessLogEvent&) noexcept override {}
};

}
