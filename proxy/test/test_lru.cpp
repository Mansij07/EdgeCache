#include "cache/LRUCache.h"
#include "test_framework.h"

using namespace edgecache;

static CacheEntry makeEntry(const std::string& body, uint64_t ttl = 60) {
    CacheEntry e;
    e.status = 200;
    e.body = body;
    e.storedAt = Clock::now();
    e.ttlSeconds = ttl;
    return e;
}

TEST(lru_get_put_basic) {
    LRUCache c(1024 * 1024);
    CHECK(!c.get("a").has_value());
    c.put("a", makeEntry("hello"));
    auto got = c.get("a");
    CHECK(got.has_value());
    CHECK_EQ(got->body, std::string("hello"));
    CHECK_EQ(c.count(), static_cast<size_t>(1));
}

TEST(lru_evicts_least_recently_used) {
    // Small budget so only ~2 small entries fit.
    LRUCache c(300);
    c.put("a", makeEntry(std::string(80, 'a')));
    c.put("b", makeEntry(std::string(80, 'b')));
    // Touch 'a' so 'b' becomes LRU.
    CHECK(c.get("a").has_value());
    c.put("c", makeEntry(std::string(80, 'c')));  // should evict 'b'
    CHECK(c.get("a").has_value());
    CHECK(!c.get("b").has_value());
    CHECK(c.get("c").has_value());
    CHECK(c.evictions() >= 1);
}

TEST(lru_erase) {
    LRUCache c(1024);
    c.put("k", makeEntry("v"));
    CHECK(c.erase("k"));
    CHECK(!c.erase("k"));
    CHECK(!c.get("k").has_value());
}

TEST(lru_purge_pattern) {
    LRUCache c(1 << 20);
    c.put("GET|h|/api/products/1", makeEntry("1"));
    c.put("GET|h|/api/products/2", makeEntry("2"));
    c.put("GET|h|/api/users/9", makeEntry("9"));
    size_t n = c.purge("/api/products/*");
    CHECK_EQ(n, static_cast<size_t>(2));
    CHECK(!c.get("GET|h|/api/products/1").has_value());
    CHECK(c.get("GET|h|/api/users/9").has_value());
    // Idempotent: purging again removes nothing.
    CHECK_EQ(c.purge("/api/products/*"), static_cast<size_t>(0));
}

TEST(lru_purge_exact_and_wildcard_all) {
    LRUCache c(1 << 20);
    c.put("GET|h|/x", makeEntry("x"));
    c.put("GET|h|/y", makeEntry("y"));
    CHECK_EQ(c.purge("/x"), static_cast<size_t>(1));
    CHECK(c.get("GET|h|/y").has_value());
    CHECK_EQ(c.purge("*"), static_cast<size_t>(1));
    CHECK_EQ(c.count(), static_cast<size_t>(0));
}

TEST(purge_matches_helper) {
    CHECK(purgeMatches("/a/*", "GET|host|/a/b"));
    CHECK(purgeMatches("/a/b", "GET|host|/a/b"));
    CHECK(!purgeMatches("/a/b", "GET|host|/a/c"));
    CHECK(purgeMatches("*", "ANY|thing|/z"));
    // Query string must not defeat a path match.
    CHECK(purgeMatches("/a/b", "GET|host|/a/b?x=1"));
}

TEST(lru_sweep_expired) {
    LRUCache c(1 << 20);
    CacheEntry fresh = makeEntry("fresh", 60);
    CacheEntry expired = makeEntry("old", 0);
    expired.storedAt = Clock::now() - std::chrono::seconds(10);  // ttl 0, swr 0 -> gone
    c.put("fresh", fresh);
    c.put("expired", expired);
    size_t removed = c.sweepExpired();
    CHECK_EQ(removed, static_cast<size_t>(1));
    CHECK(c.get("fresh").has_value());
    CHECK(!c.get("expired").has_value());
}
