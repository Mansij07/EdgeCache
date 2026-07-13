#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>

#include "origin/OriginClient.h"

namespace edgecache {

// Test double: returns canned responses and counts how many origin fetches were
// actually issued. This counter is what the concurrency test asserts on to prove
// request coalescing collapses N concurrent misses into exactly one fetch.
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

}  // namespace edgecache
