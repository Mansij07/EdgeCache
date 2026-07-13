#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "cache/CacheEntry.h"
#include "config/Config.h"
#include "redis/RedisClient.h"

namespace edgecache {

// Shared L2 cache tier backed by Redis, sitting behind each replica's in-process
// L1. A response cached by one replica becomes an L2 hit for every other replica,
// raising effective fleet-wide hit rate without duplicating the object in every
// pod's memory.
//
// Design points:
//  * Best-effort: every operation degrades to a miss / no-op if Redis is down,
//    so L2 can NEVER fail or slow-fail a request beyond one Redis round-trip.
//  * Objects are stored with a native Redis TTL (the fresh window), so any value
//    GET returns is still fresh — expiry is enforced by Redis itself.
//  * Entries are shared across replicas and wall-clock time, so freshness can't
//    use a per-process steady_clock; the stored wall-clock timestamp lets a
//    reader compute the remaining TTL to seed its L1 copy.
//  * Thread-safe via one mutex-guarded connection. L2 is consulted on the miss
//    path (not the hot L1-hit path), so lock contention is inherently limited.
class RedisL2Cache {
public:
    explicit RedisL2Cache(const Config& cfg)
        : cfg_(cfg), conn_(cfg.redisHost, cfg.redisPort, cfg.redisPassword) {}

    // Look up by cache key. Returns a fresh entry (with ttlSeconds set to the
    // remaining freshness) or nullopt on miss/disabled/Redis-error.
    std::optional<CacheEntry> get(const std::string& cacheKey);

    // Write-through with a Redis TTL of ttlSeconds (the fresh window). No-op if
    // ttlSeconds == 0 or Redis is unreachable.
    void put(const std::string& cacheKey, const CacheEntry& entry, uint64_t ttlSeconds);

    // Serialization is static + Redis-free so it can be unit-tested directly.
    static std::string serialize(const CacheEntry& e);
    static bool deserialize(const std::string& blob, CacheEntry& out, uint64_t& storedAtWallMs,
                            uint64_t& ttlSeconds);

private:
    std::string keyFor(const std::string& cacheKey) const { return cfg_.l2KeyPrefix + cacheKey; }
    bool ensureConnected_();  // caller holds mu_

    const Config& cfg_;
    std::mutex mu_;
    RedisConnection conn_;
};

}  // namespace edgecache
