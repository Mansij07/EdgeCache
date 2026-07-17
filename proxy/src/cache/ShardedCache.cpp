#include "cache/ShardedCache.h"

#include <functional>
#include <utility>

using namespace std;

namespace edgecache {

ShardedCache::ShardedCache(uint64_t maxBytes, unsigned shards) {
    if (shards == 0) shards = 1;
    uint64_t perShard = maxBytes / shards;
    if (perShard == 0) perShard = maxBytes;
    shards_.reserve(shards);
    for (unsigned i = 0; i < shards; ++i) {
        shards_.push_back(std::make_unique<LRUCache>(perShard));
    }
}

LRUCache& ShardedCache::shardFor(const std::string& key) {
    size_t h = std::hash<std::string>{}(key);
    return *shards_[h % shards_.size()];
}

std::optional<CacheEntry> ShardedCache::get(const std::string& key) {
    return shardFor(key).get(key);
}

void ShardedCache::put(const std::string& key, CacheEntry entry) {
    shardFor(key).put(key, std::move(entry));
}

bool ShardedCache::erase(const std::string& key) { return shardFor(key).erase(key); }

size_t ShardedCache::purge(const std::string& pattern) {
    size_t n = 0;
    for (auto& s : shards_) n += s->purge(pattern);
    return n;
}

size_t ShardedCache::sweepExpired() {
    size_t n = 0;
    for (auto& s : shards_) n += s->sweepExpired();
    return n;
}

uint64_t ShardedCache::sizeBytes() const {
    uint64_t n = 0;
    for (auto& s : shards_) n += s->sizeBytes();
    return n;
}

size_t ShardedCache::count() const {
    size_t n = 0;
    for (auto& s : shards_) n += s->count();
    return n;
}

uint64_t ShardedCache::evictions() const {
    uint64_t n = 0;
    for (auto& s : shards_) n += s->evictions();
    return n;
}

}
