#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace edgecache {

class MetricsRegistry {
public:

    void recordHit() { hits_.fetch_add(1, std::memory_order_relaxed); }
    void recordMiss() { misses_.fetch_add(1, std::memory_order_relaxed); }
    void recordStaleServed() { staleServed_.fetch_add(1, std::memory_order_relaxed); }
    void recordOriginError() { originErrors_.fetch_add(1, std::memory_order_relaxed); }
    void recordCircuitReject() { circuitRejects_.fetch_add(1, std::memory_order_relaxed); }
    void recordBytesServed(uint64_t n) { bytesServed_.fetch_add(n, std::memory_order_relaxed); }
    void recordPurgeMessage() { purgeMessages_.fetch_add(1, std::memory_order_relaxed); }
    void addPurgedKeys(uint64_t n) { purgedKeys_.fetch_add(n, std::memory_order_relaxed); }
    void recordL2Hit() { l2Hits_.fetch_add(1, std::memory_order_relaxed); }
    void recordL2Miss() { l2Misses_.fetch_add(1, std::memory_order_relaxed); }

    void observeOriginLatency(double seconds);

    uint64_t hits() const { return hits_.load(); }
    uint64_t misses() const { return misses_.load(); }

    std::function<uint64_t()> cacheSizeBytesFn = [] { return 0ull; };
    std::function<uint64_t()> cacheEntriesFn = [] { return 0ull; };
    std::function<uint64_t()> evictionsFn = [] { return 0ull; };
    std::function<uint64_t()> coalescedFn = [] { return 0ull; };
    std::function<uint64_t()> ruleCountFn = [] { return 0ull; };
    std::function<bool()> redisConnectedFn = [] { return false; };

    std::function<std::vector<std::pair<std::string, std::string>>()> circuitStatesFn =
        [] { return std::vector<std::pair<std::string, std::string>>{}; };

    std::string render() const;

private:
    std::atomic<uint64_t> hits_{0};
    std::atomic<uint64_t> misses_{0};
    std::atomic<uint64_t> staleServed_{0};
    std::atomic<uint64_t> originErrors_{0};
    std::atomic<uint64_t> circuitRejects_{0};
    std::atomic<uint64_t> bytesServed_{0};
    std::atomic<uint64_t> purgeMessages_{0};
    std::atomic<uint64_t> purgedKeys_{0};
    std::atomic<uint64_t> l2Hits_{0};
    std::atomic<uint64_t> l2Misses_{0};

    static constexpr int kBuckets = 8;

    static constexpr double kBounds[kBuckets] = {0.001, 0.005, 0.01, 0.05,
                                                 0.1,   0.5,   1.0,  5.0};
    mutable std::mutex histMutex_;
    uint64_t bucketCounts_[kBuckets] = {0};
    uint64_t histCount_ = 0;
    double histSum_ = 0.0;
};

}
