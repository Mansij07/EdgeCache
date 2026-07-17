#pragma once
#include <map>
#include <string>
#include <vector>

namespace edgecache {

struct CaseInsensitiveLess {
    bool operator()(const std::string& a, const std::string& b) const;
};

using Headers = std::map<std::string, std::string, CaseInsensitiveLess>;

struct HttpRequest {
    std::string method;
    std::string target;
    std::string version = "HTTP/1.1";
    Headers headers;
    std::string body;
    std::string path;
    std::string query;
    std::string header(const std::string& name, const std::string& def = "") const;
    bool keepAlive() const;
    bool isCacheableMethod() const;
};

struct HttpResponse {
    int status = 200;
    std::string reason = "OK";
    std::string version = "HTTP/1.1";
    Headers headers;
    std::string body;

    std::string header(const std::string& name, const std::string& def = "") const;

    std::string serialize(bool keepAlive) const;

    static HttpResponse simple(int status, const std::string& reason,
                               const std::string& body,
                               const std::string& contentType = "text/plain");
};

class RequestParser {
public:
    enum class State { NeedMore, Complete, Error };

    void feed(const char* data, size_t len);

    State parse(HttpRequest& out);

    void reset();
    size_t buffered() const { return buffer_.size(); }

private:
    std::string buffer_;
};

}
