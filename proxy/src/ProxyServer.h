#pragma once
#include <atomic>
#include <memory>
#include <thread>
#include <vector>

#include "RequestHandler.h"
#include "analytics/AccessLog.h"
#include "cache/ShardedCache.h"
#include "coalesce/InFlightRegistry.h"
#include "config/Config.h"
#include "metrics/MetricsRegistry.h"
#include "origin/CircuitBreakerRegistry.h"
#include "origin/HttpOriginClient.h"
#include "policy/CachePolicy.h"
#include "policy/RuleOverridePolicy.h"
#include "redis/RedisCoordinator.h"
#include "redis/RedisL2Cache.h"
#include "redis/RuleStore.h"

namespace edgecache {

// Assembles and runs a full proxy replica: N thread-per-core event loops that
// all share one key-sharded cache, the shared request-handling pipeline, the
// Redis coordinator (rule sync + purge subscription), and the metrics/admin
// server.
class ProxyServer {
public:
    explicit ProxyServer(Config cfg);

    // Blocks until stop() is called (typically from a signal handler).
    void run();
    void stop();

private:
    uint64_t purgeAllShards(const std::string& pattern);
    void wireMetrics();

    Config cfg_;
    std::atomic<bool> running_{false};

    RuleStore ruleStore_;
    MetricsRegistry metrics_;
    InFlightRegistry inflight_;
    HttpOriginClient origin_;
    CircuitBreakerRegistry breakers_;

    HeaderBasedPolicy headerPolicy_;
    RuleOverridePolicy policy_;

    std::unique_ptr<AccessLogSink> accessLog_;
    std::unique_ptr<RedisL2Cache> l2_;  // shared L2 tier; null when disabled
    std::unique_ptr<RequestHandler> handler_;
    std::unique_ptr<RedisCoordinator> redis_;

    ShardedCache cache_;
    std::vector<std::thread> workers_;
    std::thread metricsThread_;
};

}  // namespace edgecache
