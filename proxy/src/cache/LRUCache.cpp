#include "cache/LRUCache.h"

using namespace std;

namespace edgecache {

namespace {

std::string pathOfKey(const std::string& key) {
    size_t p1 = key.find('|');
    if (p1 == std::string::npos) return key;
    size_t p2 = key.find('|', p1 + 1);
    if (p2 == std::string::npos) return key;
    size_t start = p2 + 1;
    size_t q = key.find('?', start);
    if (q == std::string::npos) return key.substr(start);
    return key.substr(start, q - start);
}
}

bool purgeMatches(const std::string& pattern, const std::string& cacheKey) {
    std::string path = pathOfKey(cacheKey);
    if (pattern == "*" || pattern == "/*") return true;
    if (!pattern.empty() && pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return path.compare(0, prefix.size(), prefix) == 0;
    }
    return path == pattern;
}

std::optional<CacheEntry> LRUCache::get(const std::string& key) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = index_.find(key);
    if (it == index_.end()) return std::nullopt;

    order_.splice(order_.begin(), order_, it->second);
    return it->second->entry;
}

void LRUCache::put(const std::string& key, CacheEntry entry) {
    std::lock_guard<std::mutex> lk(mutex_);
    size_t bytes = entry.sizeBytes();

    auto it = index_.find(key);
    if (it != index_.end()) {
        curBytes_ -= it->second->bytes;
        it->second->entry = std::move(entry);
        it->second->bytes = bytes;
        curBytes_ += bytes;
        order_.splice(order_.begin(), order_, it->second);
    } else {
        order_.push_front(Node{key, std::move(entry), bytes});
        index_[key] = order_.begin();
        curBytes_ += bytes;
    }
    evictToFit_();
}

void LRUCache::evictToFit_() {
    while (curBytes_ > maxBytes_ && !order_.empty()) {
        Node& back = order_.back();
        curBytes_ -= back.bytes;
        index_.erase(back.key);
        order_.pop_back();
        evictions_.fetch_add(1, std::memory_order_relaxed);
    }
}

bool LRUCache::erase(const std::string& key) {
    std::lock_guard<std::mutex> lk(mutex_);
    auto it = index_.find(key);
    if (it == index_.end()) return false;
    curBytes_ -= it->second->bytes;
    order_.erase(it->second);
    index_.erase(it);
    return true;
}

size_t LRUCache::purge(const std::string& pattern) {
    std::lock_guard<std::mutex> lk(mutex_);
    size_t removed = 0;
    for (auto it = order_.begin(); it != order_.end();) {
        if (purgeMatches(pattern, it->key)) {
            curBytes_ -= it->bytes;
            index_.erase(it->key);
            it = order_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

size_t LRUCache::sweepExpired() {
    std::lock_guard<std::mutex> lk(mutex_);
    auto now = Clock::now();
    size_t removed = 0;
    for (auto it = order_.begin(); it != order_.end();) {
        if (!it->entry.isServeableStale(now)) {
            curBytes_ -= it->bytes;
            index_.erase(it->key);
            it = order_.erase(it);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

uint64_t LRUCache::sizeBytes() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return curBytes_;
}

size_t LRUCache::count() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return index_.size();
}

}
