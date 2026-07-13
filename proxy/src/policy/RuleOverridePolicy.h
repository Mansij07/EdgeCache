#pragma once
#include "policy/CachePolicy.h"
#include "redis/RuleStore.h"

namespace edgecache {

// Decorator/Strategy: consults the RuleStore first. If a path-pattern rule
// matches, its TTL/SWR take precedence over the origin's Cache-Control headers
// (the control plane is authoritative for those paths). Otherwise it delegates
// to the wrapped header-based policy. A matching rule with ttl==0 is treated as
// "explicitly do not cache".
class RuleOverridePolicy : public CachePolicy {
public:
    RuleOverridePolicy(const RuleStore& rules, HeaderBasedPolicy inner)
        : rules_(rules), inner_(std::move(inner)) {}

    CacheDecision decide(const std::string& path, const HttpResponse& resp) const override {
        auto rule = rules_.match(path);
        if (rule) {
            CacheDecision d;
            if (rule->ttlSeconds == 0) {
                d.cacheable = false;
                d.reason = "rule-no-cache";
                return d;
            }
            // Rule overrides even a permissive origin — but we still refuse to
            // cache genuinely uncacheable statuses (e.g. 500) to avoid pinning
            // an error response.
            if (resp.status >= 500) {
                d.reason = "rule-but-server-error";
                return d;
            }
            d.cacheable = true;
            d.ttlSeconds = rule->ttlSeconds;
            d.staleWhileRevalidateSeconds = rule->staleWhileRevalidateSeconds;
            d.reason = "rule-override";
            return d;
        }
        return inner_.decide(path, resp);
    }

private:
    const RuleStore& rules_;
    HeaderBasedPolicy inner_;
};

}  // namespace edgecache
