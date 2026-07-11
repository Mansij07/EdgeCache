#include "http/Http.h"
#include "test_framework.h"

using namespace edgecache;

TEST(parse_simple_get) {
    RequestParser p;
    std::string raw = "GET /products/1?b=2&a=1 HTTP/1.1\r\nHost: shop\r\n\r\n";
    p.feed(raw.data(), raw.size());
    HttpRequest req;
    CHECK(p.parse(req) == RequestParser::State::Complete);
    CHECK_EQ(req.method, std::string("GET"));
    CHECK_EQ(req.path, std::string("/products/1"));
    CHECK_EQ(req.query, std::string("b=2&a=1"));
    CHECK_EQ(req.header("Host"), std::string("shop"));
    CHECK(req.keepAlive());
    CHECK(req.isCacheableMethod());
}

TEST(parse_needs_more_when_incomplete) {
    RequestParser p;
    std::string part = "GET / HTTP/1.1\r\nHost: x";
    p.feed(part.data(), part.size());
    HttpRequest req;
    CHECK(p.parse(req) == RequestParser::State::NeedMore);
}

TEST(parse_body_via_content_length) {
    RequestParser p;
    std::string raw =
        "POST /submit HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nhello";
    p.feed(raw.data(), raw.size());
    HttpRequest req;
    CHECK(p.parse(req) == RequestParser::State::Complete);
    CHECK_EQ(req.body, std::string("hello"));
    CHECK(!req.isCacheableMethod());
}

TEST(parse_pipelined_requests) {
    RequestParser p;
    std::string raw =
        "GET /a HTTP/1.1\r\nHost: x\r\n\r\nGET /b HTTP/1.1\r\nHost: x\r\n\r\n";
    p.feed(raw.data(), raw.size());
    HttpRequest r1, r2;
    CHECK(p.parse(r1) == RequestParser::State::Complete);
    CHECK_EQ(r1.path, std::string("/a"));
    CHECK(p.parse(r2) == RequestParser::State::Complete);
    CHECK_EQ(r2.path, std::string("/b"));
}

TEST(connection_close_disables_keepalive) {
    RequestParser p;
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n";
    p.feed(raw.data(), raw.size());
    HttpRequest req;
    CHECK(p.parse(req) == RequestParser::State::Complete);
    CHECK(!req.keepAlive());
}

TEST(response_serialize_roundtrip_headers) {
    HttpResponse r = HttpResponse::simple(200, "OK", "hi");
    r.headers["X-Cache"] = "HIT";
    std::string wire = r.serialize(true);
    CHECK(wire.find("HTTP/1.1 200 OK") != std::string::npos);
    CHECK(wire.find("X-Cache: HIT") != std::string::npos);
    CHECK(wire.find("Content-Length: 2") != std::string::npos);
    CHECK(wire.find("Connection: keep-alive") != std::string::npos);
}
