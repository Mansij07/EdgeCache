#pragma once
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

#include "cache/CacheEntry.h"
#include "config/Config.h"
#include "redis/RedisClient.h"

namespace edgecache {

class RedisL2Cache {
public:
    explicit RedisL2Cache(const Config& cfg)
        : cfg_(cfg), conn_(cfg.redisHost, cfg.redisPort, cfg.redisPassword) {}

    std::optional<CacheEntry> get(const std::string& cacheKey);

    void put(const std::string& cacheKey, const CacheEntry& entry, uint64_t ttlSeconds);

    static std::string serialize(const CacheEntry& e);
    static bool deserialize(const std::string& blob, CacheEntry& out, uint64_t& storedAtWallMs,
                            uint64_t& ttlSeconds);

private:
    std::string keyFor(const std::string& cacheKey) const { return cfg_.l2KeyPrefix + cacheKey; }
    bool ensureConnected_();

    const Config& cfg_;
    std::mutex mu_;
    RedisConnection conn_;
};

}
