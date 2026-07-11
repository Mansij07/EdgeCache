#pragma once
#include <map>
#include <string>
#include <vector>

namespace edgecache {

// Case-insensitive header comparison so lookups behave per RFC 7230.
struct CaseInsensitiveLess {
    bool operator()(const std::string& a, const std::string& b) const;
};

using Headers = std::map<std::string, std::string, CaseInsensitiveLess>;

// Parsed HTTP/1.1 request (value object). Bodies are supported for
// completeness though the caching proxy primarily deals with GET/HEAD.
struct HttpRequest {
    std::string method;
    std::string target;  // raw request-target, e.g. "/products/1?x=2"
    std::string version = "HTTP/1.1";
    Headers headers;
    std::string body;

    // Derived (filled by the parser / handler).
    std::string path;   // decoded path component of target
    std::string query;  // raw query string (without '?')

    std::string header(const std::string& name, const std::string& def = "") const;
    bool keepAlive() const;
    bool isCacheableMethod() const;  // GET or HEAD
};

// Parsed / constructed HTTP response (value object).
struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string version = "HTTP/1.1";
    Headers headers;
    std::string body;

    std::string header(const std::string& name, const std::string& def = "") const;

    // Serialize to wire format. If keepAlive, sets Connection: keep-alive and a
    // Content-Length; otherwise Connection: close.
    std::string serialize(bool keepAlive) const;

    static HttpResponse simple(int status, const std::string& reason,
                               const std::string& body,
                               const std::string& contentType = "text/plain");
};

// Incremental request parser: feed bytes, ask if a full request is ready.
// Returns Complete once headers + declared body are all present.
class RequestParser {
public:
    enum class State { NeedMore, Complete, Error };

    // Append raw bytes read from the socket.
    void feed(const char* data, size_t len);

    // Try to parse a complete request out of the accumulated buffer.
    // On Complete, `out` is filled and the consumed bytes are removed from the
    // internal buffer (pipelined requests remain for the next call).
    State parse(HttpRequest& out);

    void reset();
    size_t buffered() const { return buffer_.size(); }

private:
    std::string buffer_;
};

}  // namespace edgecache
