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

class LRUCache {
public:
    explicit LRUCache(uint64_t maxBytes) : maxBytes_(maxBytes) {}

    std::optional<CacheEntry> get(const std::string& key);

    void put(const std::string& key, CacheEntry entry);

    bool erase(const std::string& key);

    size_t purge(const std::string& pattern);

    size_t sweepExpired();

    uint64_t sizeBytes() const;
    size_t count() const;
    uint64_t evictions() const { return evictions_.load(); }

private:
    struct Node {
        std::string key;
        CacheEntry entry;
        size_t bytes;
    };

    void evictToFit_();

    mutable std::mutex mutex_;
    std::list<Node> order_;
    std::unordered_map<std::string, std::list<Node>::iterator> index_;
    uint64_t maxBytes_;
    uint64_t curBytes_ = 0;
    std::atomic<uint64_t> evictions_{0};
};

bool purgeMatches(const std::string& pattern, const std::string& cacheKey);

}
