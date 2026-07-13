#include "redis/RuleStore.h"

#include <algorithm>

namespace edgecache {

bool RuleStore::patternMatches(const std::string& pattern, const std::string& path) {
    if (pattern == "*" || pattern == "/*") return true;
    if (!pattern.empty() && pattern.back() == '*') {
        std::string prefix = pattern.substr(0, pattern.size() - 1);
        return path.compare(0, prefix.size(), prefix) == 0;
    }
    return pattern == path;
}

void RuleStore::replaceAll(std::vector<Rule> rules) {
    // Sort so the most specific (longest) pattern is checked first.
    std::sort(rules.begin(), rules.end(), [](const Rule& a, const Rule& b) {
        return a.pathPattern.size() > b.pathPattern.size();
    });
    std::lock_guard<std::mutex> lk(mutex_);
    rules_ = std::move(rules);
    loaded_.store(true);
}

std::optional<Rule> RuleStore::match(const std::string& path) const {
    std::lock_guard<std::mutex> lk(mutex_);
    for (const auto& r : rules_) {
        if (patternMatches(r.pathPattern, path)) return r;
    }
    return std::nullopt;
}

size_t RuleStore::size() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return rules_.size();
}

}  // namespace edgecache
