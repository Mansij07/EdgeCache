#include <string>

#include "redis/RedisL2Cache.h"
#include "test_framework.h"

using namespace edgecache;

// The L2 serialize/deserialize round-trip must faithfully preserve status,
// headers (including case), body (even with embedded newlines), etag, and the
// SWR window — this is what a replica reconstructs on an L2 hit.
TEST(l2_serialize_roundtrips_all_fields) {
    CacheEntry e;
    e.status = 200;
    e.reason = "OK";
    e.headers["Content-Type"] = "application/json";
    e.headers["X-Custom"] = "a; b=c";
    e.body = "{\"a\":1}\nsecond line\n";  // embedded newlines must survive
    e.ttlSeconds = 120;
    e.staleWhileRevalidateSeconds = 30;
    e.etag = "\"abc123\"";

    std::string blob = RedisL2Cache::serialize(e);

    CacheEntry out;
    uint64_t storedAtWallMs = 0, ttl = 0;
    CHECK(RedisL2Cache::deserialize(blob, out, storedAtWallMs, ttl));

    CHECK_EQ(out.status, 200);
    CHECK_EQ(out.reason, std::string("OK"));
    CHECK_EQ(ttl, static_cast<uint64_t>(120));
    CHECK_EQ(out.staleWhileRevalidateSeconds, static_cast<uint64_t>(30));
    CHECK_EQ(out.etag, std::string("\"abc123\""));
    CHECK_EQ(out.body, std::string("{\"a\":1}\nsecond line\n"));
    CHECK_EQ(out.headers.size(), static_cast<size_t>(2));
    CHECK_EQ(out.headers.at("content-type"), std::string("application/json"));  // case-insensitive
    CHECK_EQ(out.headers.at("X-Custom"), std::string("a; b=c"));
    CHECK(storedAtWallMs > 0);
}

TEST(l2_serialize_handles_empty_body_and_headers) {
    CacheEntry e;
    e.status = 204;
    e.reason = "No Content";
    e.ttlSeconds = 5;
    // no headers, empty body, empty etag

    std::string blob = RedisL2Cache::serialize(e);
    CacheEntry out;
    uint64_t wall = 0, ttl = 0;
    CHECK(RedisL2Cache::deserialize(blob, out, wall, ttl));
    CHECK_EQ(out.status, 204);
    CHECK_EQ(ttl, static_cast<uint64_t>(5));
    CHECK(out.body.empty());
    CHECK_EQ(out.headers.size(), static_cast<size_t>(0));
}

TEST(l2_deserialize_rejects_garbage) {
    CacheEntry out;
    uint64_t wall = 0, ttl = 0;
    CHECK(!RedisL2Cache::deserialize("not a valid blob", out, wall, ttl));
    CHECK(!RedisL2Cache::deserialize("", out, wall, ttl));
    CHECK(!RedisL2Cache::deserialize("EC2\n", out, wall, ttl));  // truncated
}
