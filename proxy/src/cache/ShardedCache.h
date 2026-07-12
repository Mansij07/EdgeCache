#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "cache/CacheEntry.h"
#include "cache/LRUCache.h"

namespace edgecache {

// A byte-bounded LRU cache striped across N independent LRUCache shards, each
// with its own mutex. A key is routed to a shard by hash(key), so a given key
// always lives in exactly ONE shard regardless of which worker thread serves the
// request. That is what makes a HIT consistent across worker threads and avoids
// storing a hot key once per thread.
//
// Sharding by key (rather than one global mutex) keeps lock contention low under
// concurrency without sacrificing that cross-thread consistency. This replaces
// the earlier "one shard owned per worker thread" model, which sharded by the
// accepting thread and so produced spurious cross-thread misses.
class ShardedCache {
public:
    // `maxBytes` is the TOTAL budget, split evenly across `shards` (clamped >=1).
    explicit ShardedCache(uint64_t maxBytes, unsigned shards = 1);

    // Per-key operations route to the owning shard.
    std::optional<CacheEntry> get(const std::string& key);
    void put(const std::string& key, CacheEntry entry);
    bool erase(const std::string& key);

    // Fleet-wide operations fan out to every shard and sum the result.
    size_t purge(const std::string& pattern);
    size_t sweepExpired();

    // Aggregated stats across all shards.
    uint64_t sizeBytes() const;
    size_t count() const;
    uint64_t evictions() const;

    unsigned shardCount() const { return static_cast<unsigned>(shards_.size()); }

private:
    LRUCache& shardFor(const std::string& key);

    std::vector<std::unique_ptr<LRUCache>> shards_;
};

}  // namespace edgecache
