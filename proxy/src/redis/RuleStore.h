#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgecache {

// One path-pattern override rule (mirrors the control-plane cache_rules table
// and the Redis edgecache:rules hash field/value).
struct Rule {
    std::string pathPattern;
    uint64_t ttlSeconds = 0;
    uint64_t staleWhileRevalidateSeconds = 0;
    std::string originId;
};

// Repository: the in-process cache of the rule set fetched from Redis. Thread
// safe. The RedisRuleSync thread calls replaceAll() on pub/sub notifications and
// on the periodic safety-net poll; request handlers call match() on the hot
// path. A read here never blocks on Redis — if Redis is down we keep serving the
// last-known-good rule set (availability over freshness).
class RuleStore {
public:
    // Replace the entire rule set atomically.
    void replaceAll(std::vector<Rule> rules);

    // Longest-pattern-wins match for a request path. std::nullopt if none match.
    std::optional<Rule> match(const std::string& path) const;

    size_t size() const;
    bool everLoaded() const { return loaded_.load(); }

private:
    static bool patternMatches(const std::string& pattern, const std::string& path);

    mutable std::mutex mutex_;
    std::vector<Rule> rules_;  // kept sorted by descending pattern length
    std::atomic<bool> loaded_{false};
};

}  // namespace edgecache
