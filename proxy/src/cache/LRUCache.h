#pragma once
#include <atomic>
#include <cstdint>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "cache/CacheEntry.h"
#include "cache/CacheKey.h"

namespace edgecache {

// Byte-bounded LRU cache with O(1) get/put/evict via an intrusive-style
// doubly-linked list (std::list) plus a hash map of iterators.
//
// Sharding model: one LRUCache instance is owned per worker thread, so the hot
// path (get/put by the owning thread) sees an essentially uncontended mutex.
// The mutex exists only so the PurgeListener thread can evict across shards
// safely. A fully lock-free per-thread purge queue is a documented possible
// optimization; the lightly-locked shard is the pragmatic implementation here.
class LRUCache {
public:
    explicit LRUCache(uint64_t maxBytes) : maxBytes_(maxBytes) {}

    // Returns a copy of the entry if present (fresh or stale — caller decides),
    // and marks it most-recently-used. std::nullopt if absent.
    std::optional<CacheEntry> get(const std::string& key);

    // Insert/replace. Evicts LRU entries until within the byte budget.
    void put(const std::string& key, CacheEntry entry);

    // Remove a single key. Returns true if it was present.
    bool erase(const std::string& key);

    // Evict every key whose *path* matches the purge pattern. Returns count.
    // Idempotent: purging a pattern with no matches is a no-op returning 0.
    size_t purge(const std::string& pattern);

    // Drop entries whose hard TTL+SWR window has fully elapsed. Returns count.
    size_t sweepExpired();

    // Stats.
    uint64_t sizeBytes() const;
    size_t count() const;
    uint64_t evictions() const { return evictions_.load(); }

private:
    struct Node {
        std::string key;
        CacheEntry entry;
        size_t bytes;
    };

    void evictToFit_();  // caller holds mutex_

    mutable std::mutex mutex_;
    std::list<Node> order_;  // front = most recently used
    std::unordered_map<std::string, std::list<Node>::iterator> index_;
    uint64_t maxBytes_;
    uint64_t curBytes_ = 0;
    std::atomic<uint64_t> evictions_{0};
};

// Match a cache-key against a purge pattern. Pattern operates on the request
// path. Supports an exact path, or a trailing '*' wildcard (prefix match).
// "*" alone matches everything. Exposed for unit testing.
bool purgeMatches(const std::string& pattern, const std::string& cacheKey);

}  // namespace edgecache
