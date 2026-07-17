#pragma once
#include "policy/CachePolicy.h"
#include "redis/RuleStore.h"

namespace edgecache {

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

}
