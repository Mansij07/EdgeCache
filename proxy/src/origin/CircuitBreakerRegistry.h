#pragma once
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "config/Config.h"
#include "origin/CircuitBreaker.h"

namespace edgecache {

class CircuitBreakerRegistry {
public:
    explicit CircuitBreakerRegistry(const Config& cfg) : cfg_(cfg) {}

    CircuitBreaker& forOrigin(const std::string& host) {
        std::lock_guard<std::mutex> lk(mutex_);
        auto it = breakers_.find(host);
        if (it == breakers_.end()) {
            auto cb = std::make_unique<CircuitBreaker>(
                cfg_.cbFailureThreshold, cfg_.cbOpenMs, cfg_.cbHalfOpenMaxProbes);
            CircuitBreaker& ref = *cb;
            breakers_.emplace(host, std::move(cb));
            return ref;
        }
        return *it->second;
    }

    std::vector<std::pair<std::string, std::string>> snapshot() {
        std::lock_guard<std::mutex> lk(mutex_);
        std::vector<std::pair<std::string, std::string>> out;
        for (auto& [host, cb] : breakers_) out.emplace_back(host, cb->stateName());
        return out;
    }

private:
    const Config& cfg_;
    std::mutex mutex_;
    std::map<std::string, std::unique_ptr<CircuitBreaker>> breakers_;
};

}
