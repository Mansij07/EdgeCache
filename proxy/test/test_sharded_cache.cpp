#include <string>

#include "cache/ShardedCache.h"
#include "test_framework.h"

using namespace std;

using namespace edgecache;

namespace {
CacheEntry mkEntry(const std::string& body, uint64_t ttl = 60) {
    CacheEntry e;
    e.status = 200;
    e.reason = "OK";
    e.body = body;
    e.storedAt = Clock::now();
    e.ttlSeconds = ttl;
    return e;
}
}

TEST(sharded_cache_key_is_retrievable_regardless_of_shard_count) {
    ShardedCache cache(1 << 20, 8);
    for (int i = 0; i < 200; ++i) {
        std::string key = "GET|origin|/products/" + std::to_string(i);
        CHECK(!cache.get(key).has_value());
        cache.put(key, mkEntry("v" + std::to_string(i)));
        auto got = cache.get(key);
        CHECK(got.has_value());
        CHECK_EQ(got->body, std::string("v" + std::to_string(i)));
    }
    CHECK_EQ(cache.count(), static_cast<size_t>(200));
}

TEST(sharded_cache_repeated_reads_of_one_key_all_hit) {
    ShardedCache cache(1 << 20, 4);
    const std::string key = "GET|origin|/hot";
    CHECK(!cache.get(key).has_value());
    cache.put(key, mkEntry("payload"));
    for (int i = 0; i < 50; ++i) {
        auto got = cache.get(key);
        CHECK(got.has_value());
        CHECK_EQ(got->body, std::string("payload"));
    }
}

TEST(sharded_cache_purge_spans_all_shards) {
    ShardedCache cache(1 << 20, 4);
    for (int i = 0; i < 60; ++i) cache.put("GET|origin|/a/" + std::to_string(i), mkEntry("x"));
    for (int i = 0; i < 60; ++i) cache.put("GET|origin|/b/" + std::to_string(i), mkEntry("y"));
    CHECK_EQ(cache.count(), static_cast<size_t>(120));

    size_t purged = cache.purge("/a/*");
    CHECK_EQ(purged, static_cast<size_t>(60));
    CHECK_EQ(cache.count(), static_cast<size_t>(60));
    for (int i = 0; i < 60; ++i) {
        CHECK(!cache.get("GET|origin|/a/" + std::to_string(i)).has_value());
        CHECK(cache.get("GET|origin|/b/" + std::to_string(i)).has_value());
    }
}
