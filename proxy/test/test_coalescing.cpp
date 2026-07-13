#include <atomic>
#include <chrono>
#include <thread>
#include <vector>

#include "RequestHandler.h"
#include "cache/ShardedCache.h"
#include "coalesce/InFlightRegistry.h"
#include "config/Config.h"
#include "metrics/MetricsRegistry.h"
#include "origin/CircuitBreakerRegistry.h"
#include "origin/FakeOriginClient.h"
#include "policy/CachePolicy.h"
#include "test_framework.h"

using namespace edgecache;

TEST(inflight_registry_leader_and_waiter) {
    InFlightRegistry reg;
    auto a = reg.acquire("k");
    CHECK(a.leader);
    auto b = reg.acquire("k");
    CHECK(!b.leader);              // second caller becomes a waiter
    CHECK_EQ(reg.coalescedTotal(), static_cast<uint64_t>(1));

    // Publish from a helper thread; waiter should unblock with the result.
    std::thread pub([&] {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        HttpResponse r = HttpResponse::simple(200, "OK", "done");
        reg.publish("k", r, true);
    });
    b.slot->waitReady();
    CHECK(b.slot->success);
    CHECK_EQ(b.slot->response.body, std::string("done"));
    pub.join();

    // After publish the marker is cleared; a new acquire is a fresh leader.
    auto c = reg.acquire("k");
    CHECK(c.leader);
}

// The headline concurrency proof: 100 concurrent requests for the same missing
// key must produce exactly ONE origin fetch.
TEST(coalescing_collapses_concurrent_misses_to_one_fetch) {
    Config cfg;
    cfg.defaultOriginHost = "test-origin";
    cfg.workerThreads = 1;

    FakeOriginClient origin([](const HttpRequest&, const OriginTarget&) {
        // Sleep so all requests pile up before the leader finishes.
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        OriginResult r;
        r.ok = true;
        r.response = HttpResponse::simple(200, "OK", "payload");
        r.response.headers["Cache-Control"] = "max-age=60";
        return r;
    });

    HeaderBasedPolicy policy(60);
    CircuitBreakerRegistry breakers(cfg);
    InFlightRegistry inflight;
    MetricsRegistry metrics;
    RequestHandler handler(cfg, policy, origin, breakers, inflight, metrics);

    ShardedCache shard(1 << 20);

    HttpRequest req;
    req.method = "GET";
    req.target = "/hot";
    req.path = "/hot";

    constexpr int N = 100;
    std::vector<std::thread> threads;
    std::atomic<int> okBodies{0};
    for (int i = 0; i < N; ++i) {
        threads.emplace_back([&] {
            HttpResponse resp = handler.handle(req, shard);
            if (resp.body == "payload") okBodies.fetch_add(1);
        });
    }
    for (auto& t : threads) t.join();

    CHECK_EQ(origin.fetchCount(), static_cast<uint64_t>(1));  // <-- the whole point
    CHECK_EQ(okBodies.load(), N);                             // everyone got the answer
    CHECK(inflight.coalescedTotal() >= 1);
}

TEST(second_request_after_store_is_a_hit) {
    Config cfg;
    cfg.defaultOriginHost = "test-origin";
    FakeOriginClient origin([](const HttpRequest&, const OriginTarget&) {
        OriginResult r;
        r.ok = true;
        r.response = HttpResponse::simple(200, "OK", "v");
        r.response.headers["Cache-Control"] = "max-age=60";
        return r;
    });
    HeaderBasedPolicy policy(60);
    CircuitBreakerRegistry breakers(cfg);
    InFlightRegistry inflight;
    MetricsRegistry metrics;
    RequestHandler handler(cfg, policy, origin, breakers, inflight, metrics);
    ShardedCache shard(1 << 20);

    HttpRequest req;
    req.method = "GET";
    req.target = "/x";
    req.path = "/x";

    auto r1 = handler.handle(req, shard);
    CHECK_EQ(r1.header("X-Cache"), std::string("MISS"));
    auto r2 = handler.handle(req, shard);
    CHECK_EQ(r2.header("X-Cache"), std::string("HIT"));
    CHECK_EQ(origin.fetchCount(), static_cast<uint64_t>(1));
}

TEST(circuit_open_fast_fails_without_origin_call) {
    Config cfg;
    cfg.defaultOriginHost = "dead-origin";
    cfg.cbFailureThreshold = 1;
    cfg.cbOpenMs = 10000;
    FakeOriginClient origin([](const HttpRequest&, const OriginTarget&) {
        OriginResult r;
        r.ok = false;  // always fails
        r.error = "boom";
        return r;
    });
    HeaderBasedPolicy policy(60);
    CircuitBreakerRegistry breakers(cfg);
    InFlightRegistry inflight;
    MetricsRegistry metrics;
    RequestHandler handler(cfg, policy, origin, breakers, inflight, metrics);
    ShardedCache shard(1 << 20);

    HttpRequest req;
    req.method = "GET";
    req.target = "/dead";
    req.path = "/dead";

    auto r1 = handler.handle(req, shard);  // fails, opens breaker
    CHECK_EQ(r1.status, 502);
    uint64_t after = origin.fetchCount();
    auto r2 = handler.handle(req, shard);  // breaker open -> fast fail, no fetch
    CHECK_EQ(r2.status, 503);
    CHECK_EQ(origin.fetchCount(), after);  // no additional origin call
}
