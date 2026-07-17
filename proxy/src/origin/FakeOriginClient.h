#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

#include "origin/OriginClient.h"

namespace edgecache {

class FakeOriginClient : public OriginClient {
public:
    using Responder = std::function<OriginResult(const HttpRequest&, const OriginTarget&)>;

    explicit FakeOriginClient(Responder r) : responder_(std::move(r)) {}

    OriginResult fetch(const HttpRequest& req, const OriginTarget& target) override {
        fetches_.fetch_add(1, std::memory_order_relaxed);
        return responder_(req, target);
    }

    uint64_t fetchCount() const { return fetches_.load(); }
    void resetCount() { fetches_.store(0); }

private:
    Responder responder_;
    std::atomic<uint64_t> fetches_{0};
};

}
