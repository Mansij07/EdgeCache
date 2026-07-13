#include "ProxyServer.h"

#include <chrono>
#include <iostream>

#include "metrics/MetricsServer.h"
#include "net/EventLoop.h"
#ifdef EDGECACHE_KAFKA
#include "analytics/KafkaAccessLogSink.h"
#endif

namespace edgecache {

ProxyServer::ProxyServer(Config cfg)
    : cfg_(std::move(cfg)),
      breakers_(cfg_),
      headerPolicy_(cfg_.defaultTtlSeconds),
      policy_(ruleStore_, headerPolicy_),
      // One shared cache striped across `workerThreads` shards (keyed by cache
      // key), so any worker serves a given key from the same shard.
      cache_(cfg_.maxCacheBytes, cfg_.workerThreads ? cfg_.workerThreads : 1) {
    // Access-log sink: Kafka producer when compiled in and configured, else no-op.
#ifdef EDGECACHE_KAFKA
    if (!cfg_.kafkaBrokers.empty()) {
        auto sink = std::make_unique<KafkaAccessLogSink>(cfg_.kafkaBrokers, cfg_.kafkaTopic);
        if (sink->ok()) {
            accessLog_ = std::move(sink);
        }
    }
#endif
    if (!accessLog_) accessLog_ = std::make_unique<NullAccessLogSink>();

    // Shared Redis L2 tier (advanced): constructed only when enabled.
    if (cfg_.l2Enabled) {
        l2_ = std::make_unique<RedisL2Cache>(cfg_);
        std::cerr << "[proxy] L2 cache tier enabled (prefix=" << cfg_.l2KeyPrefix << ")" << std::endl;
    }

    handler_ = std::make_unique<RequestHandler>(cfg_, policy_, origin_, breakers_, inflight_,
                                                metrics_, accessLog_.get(), l2_.get());
    redis_ = std::make_unique<RedisCoordinator>(
        cfg_, ruleStore_,
        [this](const std::string& pattern) { return purgeAllShards(pattern); });
}

uint64_t ProxyServer::purgeAllShards(const std::string& pattern) {
    metrics_.recordPurgeMessage();
    uint64_t total = cache_.purge(pattern);
    metrics_.addPurgedKeys(total);
    return total;
}

void ProxyServer::wireMetrics() {
    metrics_.cacheSizeBytesFn = [this] { return cache_.sizeBytes(); };
    metrics_.cacheEntriesFn = [this] { return static_cast<uint64_t>(cache_.count()); };
    metrics_.evictionsFn = [this] { return cache_.evictions(); };
    metrics_.coalescedFn = [this] { return inflight_.coalescedTotal(); };
    metrics_.ruleCountFn = [this] { return static_cast<uint64_t>(ruleStore_.size()); };
    metrics_.redisConnectedFn = [this] { return redis_->redisConnected(); };
    metrics_.circuitStatesFn = [this] { return breakers_.snapshot(); };
}

void ProxyServer::run() {
    running_.store(true);
    cfg_.log();
    wireMetrics();

    redis_->start();

    // Metrics/admin server on its own thread. Readiness is intentionally NOT
    // gated on Redis — the proxy keeps serving from L1 during a Redis outage, so
    // it must stay in rotation. Redis health is exposed via the
    // edgecache_redis_connected metric instead. `readyFn` reports ready whenever
    // the proxy is running.
    metricsThread_ = std::thread([this] {
        MetricsServer server(cfg_.listenHost, cfg_.metricsPort, metrics_,
                             [this] { return running_.load(); }, &running_);
        server.run();
    });

    // Worker threads: all share the one key-sharded cache and run an epoll loop.
    for (unsigned int i = 0; i < cfg_.workerThreads; ++i) {
        workers_.emplace_back([this, i] {
            EventLoop loop(
                cfg_.listenHost, cfg_.listenPort,
                [this](const HttpRequest& req) { return handler_->handle(req, cache_); },
                &running_);
            // Periodically sweep hard-expired entries so idle keys don't linger.
            // Only worker 0 runs it — sweepExpired() already covers every shard.
            if (i == 0) {
                auto last = std::chrono::steady_clock::now();
                loop.setOnTick([this, &last] {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - last).count() >= 10) {
                        cache_.sweepExpired();
                        last = now;
                    }
                });
            }
            if (!loop.run()) {
                std::cerr << "[proxy] worker " << i << " failed to start" << std::endl;
            }
        });
    }

    std::cerr << "[proxy] started with " << cfg_.workerThreads << " workers on port "
              << cfg_.listenPort << std::endl;

    for (auto& t : workers_) {
        if (t.joinable()) t.join();
    }
    if (metricsThread_.joinable()) metricsThread_.join();
    redis_->stop();
}

void ProxyServer::stop() { running_.store(false); }

}  // namespace edgecache
