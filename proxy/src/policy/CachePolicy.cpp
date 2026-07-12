#include "policy/CachePolicy.h"

#include <algorithm>
#include <cctype>

namespace edgecache {

namespace {
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Returns true if `directive` is present in the Cache-Control value. If it has
// an "=<num>" form, its numeric value is written to `outValue`.
bool hasDirective(const std::string& cc, const std::string& directive, long* outValue) {
    std::string low = lower(cc);
    size_t pos = 0;
    while ((pos = low.find(directive, pos)) != std::string::npos) {
        // Ensure token boundary before.
        bool boundaryBefore = (pos == 0) || low[pos - 1] == ',' || low[pos - 1] == ' ';
        size_t after = pos + directive.size();
        char nextCh = after < low.size() ? low[after] : '\0';
        bool boundaryAfter = (nextCh == '\0' || nextCh == ',' || nextCh == ' ' || nextCh == '=');
        if (boundaryBefore && boundaryAfter) {
            if (nextCh == '=' && outValue) {
                size_t numStart = after + 1;
                size_t numEnd = numStart;
                while (numEnd < low.size() && (std::isdigit((unsigned char)low[numEnd]))) numEnd++;
                if (numEnd > numStart) {
                    try {
                        *outValue = std::stol(low.substr(numStart, numEnd - numStart));
                    } catch (...) {
                    }
                }
            }
            return true;
        }
        pos = after;
    }
    return false;
}

bool statusCacheable(int status) {
    switch (status) {
        case 200:
        case 203:
        case 204:
        case 301:
        case 404:
            return true;
        default:
            return false;
    }
}
}  // namespace

CacheDecision HeaderBasedPolicy::decide(const std::string& /*path*/,
                                        const HttpResponse& resp) const {
    CacheDecision d;
    if (!statusCacheable(resp.status)) {
        d.reason = "status-not-cacheable";
        return d;
    }

    std::string cc = resp.header("Cache-Control");
    long v = 0;
    if (hasDirective(cc, "no-store", nullptr)) {
        d.reason = "no-store";
        return d;
    }
    if (hasDirective(cc, "private", nullptr)) {
        d.reason = "private";
        return d;
    }
    if (hasDirective(cc, "no-cache", nullptr)) {
        // no-cache technically means "revalidate before use"; we treat as
        // non-cacheable at MVP (no conditional revalidation yet).
        d.reason = "no-cache";
        return d;
    }

    long ttl = -1;
    if (hasDirective(cc, "s-maxage", &v)) {
        ttl = v;  // shared-cache directive wins for us
    } else if (hasDirective(cc, "max-age", &v)) {
        ttl = v;
    }

    if (ttl < 0) {
        d.cacheable = true;
        d.ttlSeconds = defaultTtl_;
        d.reason = "heuristic-default-ttl";
        return d;
    }
    if (ttl == 0) {
        d.reason = "max-age-0";
        return d;
    }

    d.cacheable = true;
    d.ttlSeconds = static_cast<uint64_t>(ttl);
    d.reason = "origin-max-age";
    return d;
}

}  // namespace edgecache
