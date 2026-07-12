#include "policy/CachePolicy.h"
#include "policy/RuleOverridePolicy.h"
#include "redis/RuleStore.h"
#include "test_framework.h"

using namespace edgecache;

static HttpResponse resp(int status, const std::string& cacheControl) {
    HttpResponse r = HttpResponse::simple(status, "", "body");
    if (!cacheControl.empty()) r.headers["Cache-Control"] = cacheControl;
    return r;
}

TEST(header_policy_max_age) {
    HeaderBasedPolicy p(30);
    auto d = p.decide("/x", resp(200, "max-age=120"));
    CHECK(d.cacheable);
    CHECK_EQ(d.ttlSeconds, static_cast<uint64_t>(120));
}

TEST(header_policy_no_store) {
    HeaderBasedPolicy p(30);
    CHECK(!p.decide("/x", resp(200, "no-store")).cacheable);
    CHECK(!p.decide("/x", resp(200, "private")).cacheable);
    CHECK(!p.decide("/x", resp(200, "no-cache")).cacheable);
}

TEST(header_policy_default_ttl_when_no_directive) {
    HeaderBasedPolicy p(45);
    auto d = p.decide("/x", resp(200, ""));
    CHECK(d.cacheable);
    CHECK_EQ(d.ttlSeconds, static_cast<uint64_t>(45));
}

TEST(header_policy_s_maxage_wins) {
    HeaderBasedPolicy p(30);
    auto d = p.decide("/x", resp(200, "max-age=10, s-maxage=99"));
    CHECK_EQ(d.ttlSeconds, static_cast<uint64_t>(99));
}

TEST(header_policy_non_cacheable_status) {
    HeaderBasedPolicy p(30);
    CHECK(!p.decide("/x", resp(500, "max-age=60")).cacheable);
    CHECK(p.decide("/x", resp(404, "max-age=60")).cacheable);
}

TEST(rule_override_takes_precedence) {
    RuleStore store;
    store.replaceAll({Rule{"/api/*", 300, 30, "o1"}});
    HeaderBasedPolicy header(30);
    RuleOverridePolicy p(store, header);

    // Origin says no-store, but a matching rule forces caching.
    auto d = p.decide("/api/thing", resp(200, "no-store"));
    CHECK(d.cacheable);
    CHECK_EQ(d.ttlSeconds, static_cast<uint64_t>(300));
    CHECK_EQ(d.staleWhileRevalidateSeconds, static_cast<uint64_t>(30));
}

TEST(rule_override_falls_back_when_no_match) {
    RuleStore store;
    store.replaceAll({Rule{"/api/*", 300, 0, "o1"}});
    HeaderBasedPolicy header(30);
    RuleOverridePolicy p(store, header);
    auto d = p.decide("/other", resp(200, "max-age=15"));
    CHECK(d.cacheable);
    CHECK_EQ(d.ttlSeconds, static_cast<uint64_t>(15));  // from origin header
}

TEST(rule_ttl_zero_means_no_cache) {
    RuleStore store;
    store.replaceAll({Rule{"/no/*", 0, 0, "o1"}});
    HeaderBasedPolicy header(30);
    RuleOverridePolicy p(store, header);
    CHECK(!p.decide("/no/thing", resp(200, "max-age=999")).cacheable);
}

TEST(rule_longest_pattern_wins) {
    RuleStore store;
    store.replaceAll({Rule{"/a/*", 10, 0, "o"}, Rule{"/a/b/*", 20, 0, "o"}});
    auto m = store.match("/a/b/c");
    CHECK(m.has_value());
    CHECK_EQ(m->ttlSeconds, static_cast<uint64_t>(20));
}
