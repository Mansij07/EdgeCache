#pragma once
#include "analytics/AccessLog.h"
#include "cache/ShardedCache.h"
#include "coalesce/InFlightRegistry.h"
#include "config/Config.h"
#include "metrics/MetricsRegistry.h"
#include "origin/CircuitBreakerRegistry.h"
#include "origin/OriginClient.h"
#include "policy/CachePolicy.h"
#include "redis/RedisL2Cache.h"

namespace edgecache {

class RequestHandler {
public:
    RequestHandler(const Config& cfg, CachePolicy& policy, OriginClient& origin,
                   CircuitBreakerRegistry& breakers, InFlightRegistry& inflight,
                   MetricsRegistry& metrics, AccessLogSink* accessLog = nullptr,
                   RedisL2Cache* l2 = nullptr)
        : cfg_(cfg),
          policy_(policy),
          origin_(origin),
          breakers_(breakers),
          inflight_(inflight),
          metrics_(metrics),
          accessLog_(accessLog),
          l2_(l2) {}

    HttpResponse handle(const HttpRequest& req, ShardedCache& cache);

private:

    HttpResponse process(const HttpRequest& req, ShardedCache& cache);

    OriginTarget targetFor(const std::string& ) const;
    OriginResult fetchGuarded(const HttpRequest& req, const OriginTarget& target,
                              bool& circuitRejected);
    void maybeStore(const HttpRequest& req, const HttpResponse& resp, ShardedCache& cache);

    void storeTiers(const HttpRequest& req, const HttpResponse& resp, ShardedCache& cache);
    void triggerRevalidation(const HttpRequest& req, const std::string& key, ShardedCache& cache);

    const Config& cfg_;
    CachePolicy& policy_;
    OriginClient& origin_;
    CircuitBreakerRegistry& breakers_;
    InFlightRegistry& inflight_;
    MetricsRegistry& metrics_;
    AccessLogSink* accessLog_;
    RedisL2Cache* l2_;
};

}
