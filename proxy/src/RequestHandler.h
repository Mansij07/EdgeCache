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

// The heart of the data plane: turns a parsed request + the shared key-sharded
// cache into a response, applying the full pipeline — cache lookup, policy
// decision, request coalescing, circuit breaking, origin fetch, and
// stale-while-revalidate. Shared dependencies are injected (Dependency
// Inversion) so the whole path is unit-testable with a FakeOriginClient.
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

    // `cache` is the shared key-sharded cache. Times the request and emits an
    // access-log event (fire-and-forget) once complete.
    HttpResponse handle(const HttpRequest& req, ShardedCache& cache);

private:
    // The actual pipeline; handle() wraps this with timing + access logging.
    HttpResponse process(const HttpRequest& req, ShardedCache& cache);

    OriginTarget targetFor(const std::string& /*path*/) const;
    OriginResult fetchGuarded(const HttpRequest& req, const OriginTarget& target,
                              bool& circuitRejected);
    void maybeStore(const HttpRequest& req, const HttpResponse& resp, ShardedCache& cache);
    // Like maybeStore but also write-through to the shared L2 tier (when enabled).
    void storeTiers(const HttpRequest& req, const HttpResponse& resp, ShardedCache& cache);
    void triggerRevalidation(const HttpRequest& req, const std::string& key, ShardedCache& cache);

    const Config& cfg_;
    CachePolicy& policy_;
    OriginClient& origin_;
    CircuitBreakerRegistry& breakers_;
    InFlightRegistry& inflight_;
    MetricsRegistry& metrics_;
    AccessLogSink* accessLog_;
    RedisL2Cache* l2_;  // shared L2 tier; nullptr when disabled
};

}  // namespace edgecache
