#include "redis/RedisCoordinator.h"
#include "test_framework.h"

using namespace std;

using namespace edgecache;

TEST(parse_rule_json_full) {
    Rule r = parseRuleJson("/api/*", "{\"ttl\":120,\"originId\":\"abc-123\",\"swr\":30}");
    CHECK_EQ(r.pathPattern, std::string("/api/*"));
    CHECK_EQ(r.ttlSeconds, static_cast<uint64_t>(120));
    CHECK_EQ(r.staleWhileRevalidateSeconds, static_cast<uint64_t>(30));
    CHECK_EQ(r.originId, std::string("abc-123"));
}

TEST(parse_rule_json_missing_fields_default_zero) {
    Rule r = parseRuleJson("/x", "{\"ttl\":60}");
    CHECK_EQ(r.ttlSeconds, static_cast<uint64_t>(60));
    CHECK_EQ(r.staleWhileRevalidateSeconds, static_cast<uint64_t>(0));
    CHECK_EQ(r.originId, std::string(""));
}

TEST(parse_rule_json_whitespace_tolerant) {
    Rule r = parseRuleJson("/y", "{ \"ttl\" : 15 , \"swr\" : 5 }");
    CHECK_EQ(r.ttlSeconds, static_cast<uint64_t>(15));
    CHECK_EQ(r.staleWhileRevalidateSeconds, static_cast<uint64_t>(5));
}
