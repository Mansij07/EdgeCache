#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "metrics/MetricsRegistry.h"

namespace edgecache {

// A tiny single-threaded blocking HTTP server for operational endpoints:
//   GET /metrics  -> Prometheus exposition
//   GET /healthz  -> liveness (always 200 while the process runs)
//   GET /readyz   -> readiness; 200 while the proxy is serving, 503 otherwise.
//
// Both liveness and readiness are intentionally independent of Redis: the proxy
// serves cache hits perfectly well from L1 while Redis is down, so a Redis blip
// must NOT pull a healthy replica out of rotation. Redis health is observable
// separately via the edgecache_redis_connected metric (updated within ~1s by the
// coordinator's heartbeat). `readyFn` stays as an injection point for a real
// not-ready condition (e.g. startup before workers bind).
class MetricsServer {
public:
    MetricsServer(std::string host, uint16_t port, MetricsRegistry& metrics,
                  std::function<bool()> readyFn, std::atomic<bool>* running)
        : host_(std::move(host)),
          port_(port),
          metrics_(metrics),
          readyFn_(std::move(readyFn)),
          running_(running) {}

    void run();  // blocks until *running becomes false

private:
    std::string host_;
    uint16_t port_;
    MetricsRegistry& metrics_;
    std::function<bool()> readyFn_;
    std::atomic<bool>* running_;
};

}  // namespace edgecache
