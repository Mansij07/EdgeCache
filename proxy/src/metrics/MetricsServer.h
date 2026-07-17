#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>

#include "metrics/MetricsRegistry.h"

namespace edgecache {

class MetricsServer {
public:
    MetricsServer(std::string host, uint16_t port, MetricsRegistry& metrics,
                  std::function<bool()> readyFn, std::atomic<bool>* running)
        : host_(std::move(host)),
          port_(port),
          metrics_(metrics),
          readyFn_(std::move(readyFn)),
          running_(running) {}

    void run();

private:
    std::string host_;
    uint16_t port_;
    MetricsRegistry& metrics_;
    std::function<bool()> readyFn_;
    std::atomic<bool>* running_;
};

}
