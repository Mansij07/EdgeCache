#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cache/CacheEntry.h"
#include "cache/LRUCache.h"

namespace edgecache {

class ShardedCache {
public:

    explicit ShardedCache(uint64_t maxBytes, unsigned shards = 1);

    std::optional<CacheEntry> get(const std::string& key);
    void put(const std::string& key, CacheEntry entry);
    bool erase(const std::string& key);

    size_t purge(const std::string& pattern);
    size_t sweepExpired();

    uint64_t sizeBytes() const;
    size_t count() const;
    uint64_t evictions() const;

    unsigned shardCount() const { return static_cast<unsigned>(shards_.size()); }

private:
    LRUCache& shardFor(const std::string& key);

    std::vector<std::unique_ptr<LRUCache>> shards_;
};

}
