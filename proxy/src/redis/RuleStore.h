#pragma once
#include <atomic>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace edgecache {

struct Rule {
    std::string pathPattern;
    uint64_t ttlSeconds = 0;
    uint64_t staleWhileRevalidateSeconds = 0;
    std::string originId;
};

class RuleStore {
public:

    void replaceAll(std::vector<Rule> rules);

    std::optional<Rule> match(const std::string& path) const;

    size_t size() const;
    bool everLoaded() const { return loaded_.load(); }

private:
    static bool patternMatches(const std::string& pattern, const std::string& path);

    mutable std::mutex mutex_;
    std::vector<Rule> rules_;
    std::atomic<bool> loaded_{false};
};

}
