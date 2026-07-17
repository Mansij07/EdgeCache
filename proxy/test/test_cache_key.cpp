#include "cache/CacheKey.h"
#include "test_framework.h"

using namespace std;

using namespace edgecache;

TEST(query_normalization_sorts_params) {
    CHECK_EQ(normalizeQuery("b=2&a=1"), std::string("a=1&b=2"));
    CHECK_EQ(normalizeQuery("a=1&b=2"), std::string("a=1&b=2"));
    CHECK_EQ(normalizeQuery(""), std::string(""));
    CHECK_EQ(normalizeQuery("z=9"), std::string("z=9"));
}

TEST(cache_key_stable_across_param_order) {
    HttpRequest r1;
    r1.method = "GET";
    r1.path = "/products";
    r1.query = "color=red&size=xl";
    HttpRequest r2;
    r2.method = "GET";
    r2.path = "/products";
    r2.query = "size=xl&color=red";
    auto k1 = CacheKey::fromRequest(r1, "shop");
    auto k2 = CacheKey::fromRequest(r2, "shop");
    CHECK_EQ(k1.value, k2.value);
}

TEST(cache_key_differs_by_method_and_host) {
    HttpRequest g;
    g.method = "GET";
    g.path = "/x";
    HttpRequest h = g;
    h.method = "HEAD";
    CHECK(!(CacheKey::fromRequest(g, "a") == CacheKey::fromRequest(h, "a")));
    CHECK(!(CacheKey::fromRequest(g, "a") == CacheKey::fromRequest(g, "b")));
}

TEST(cache_key_excludes_arbitrary_headers) {

    HttpRequest a;
    a.method = "GET";
    a.path = "/p";
    a.headers["X-Evil"] = "1";
    HttpRequest b;
    b.method = "GET";
    b.path = "/p";
    b.headers["X-Evil"] = "2";
    CHECK_EQ(CacheKey::fromRequest(a, "h").value, CacheKey::fromRequest(b, "h").value);
}
