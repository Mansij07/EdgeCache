#include "http/Http.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <sstream>

namespace edgecache {

bool CaseInsensitiveLess::operator()(const std::string& a, const std::string& b) const {
    return std::lexicographical_compare(
        a.begin(), a.end(), b.begin(), b.end(),
        [](unsigned char c1, unsigned char c2) {
            return std::tolower(c1) < std::tolower(c2);
        });
}

namespace {
std::string trim(const std::string& s) {
    size_t b = s.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}
bool iequals(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
    return true;
}
const char* reasonFor(int status) {
    switch (status) {
        case 200: return "OK";
        case 204: return "No Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 404: return "Not Found";
        case 500: return "Internal Server Error";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        case 504: return "Gateway Timeout";
        default: return "OK";
    }
}
}  // namespace

std::string HttpRequest::header(const std::string& name, const std::string& def) const {
    auto it = headers.find(name);
    return it == headers.end() ? def : it->second;
}

bool HttpRequest::keepAlive() const {
    std::string conn = header("Connection");
    if (version == "HTTP/1.0") return iequals(trim(conn), "keep-alive");
    return !iequals(trim(conn), "close");
}

bool HttpRequest::isCacheableMethod() const {
    return method == "GET" || method == "HEAD";
}

std::string HttpResponse::header(const std::string& name, const std::string& def) const {
    auto it = headers.find(name);
    return it == headers.end() ? def : it->second;
}

std::string HttpResponse::serialize(bool keepAlive) const {
    std::ostringstream os;
    os << version << ' ' << status << ' '
       << (reason.empty() ? reasonFor(status) : reason) << "\r\n";

    for (const auto& kv : headers) {
        // We manage these below to keep the framing correct.
        if (iequals(kv.first, "Connection") || iequals(kv.first, "Content-Length") ||
            iequals(kv.first, "Transfer-Encoding"))
            continue;
        os << kv.first << ": " << kv.second << "\r\n";
    }
    os << "Content-Length: " << body.size() << "\r\n";
    os << "Connection: " << (keepAlive ? "keep-alive" : "close") << "\r\n";
    os << "\r\n";
    os << body;
    return os.str();
}

HttpResponse HttpResponse::simple(int status, const std::string& reason,
                                  const std::string& body, const std::string& contentType) {
    HttpResponse r;
    r.status = status;
    r.reason = reason.empty() ? reasonFor(status) : reason;
    r.headers["Content-Type"] = contentType;
    r.body = body;
    return r;
}

void RequestParser::feed(const char* data, size_t len) { buffer_.append(data, len); }
void RequestParser::reset() { buffer_.clear(); }

RequestParser::State RequestParser::parse(HttpRequest& out) {
    // Find end of header block.
    size_t headerEnd = buffer_.find("\r\n\r\n");
    if (headerEnd == std::string::npos) {
        // Guard against unbounded header growth (simple DoS protection).
        if (buffer_.size() > 64 * 1024) return State::Error;
        return State::NeedMore;
    }

    std::string headerBlock = buffer_.substr(0, headerEnd);
    std::istringstream is(headerBlock);
    std::string line;

    if (!std::getline(is, line)) return State::Error;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    // Request line: METHOD SP target SP version
    {
        std::istringstream ls(line);
        HttpRequest req;
        if (!(ls >> req.method >> req.target >> req.version)) return State::Error;
        out = req;
    }

    out.headers.clear();
    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;  // tolerate malformed header line
        std::string name = trim(line.substr(0, colon));
        std::string value = trim(line.substr(colon + 1));
        if (!name.empty()) out.headers[name] = value;
    }

    // Body framing via Content-Length (chunked bodies for requests are not
    // expected for a caching GET/HEAD proxy; reject to stay safe).
    size_t bodyLen = 0;
    if (out.headers.count("Transfer-Encoding")) return State::Error;
    auto clIt = out.headers.find("Content-Length");
    if (clIt != out.headers.end()) {
        try {
            bodyLen = static_cast<size_t>(std::stoull(clIt->second));
        } catch (...) {
            return State::Error;
        }
    }

    size_t total = headerEnd + 4 + bodyLen;
    if (buffer_.size() < total) return State::NeedMore;

    out.body = buffer_.substr(headerEnd + 4, bodyLen);

    // Split target into path + query.
    size_t q = out.target.find('?');
    if (q == std::string::npos) {
        out.path = out.target;
        out.query.clear();
    } else {
        out.path = out.target.substr(0, q);
        out.query = out.target.substr(q + 1);
    }

    // Consume the parsed request from the buffer (leave any pipelined bytes).
    buffer_.erase(0, total);
    return State::Complete;
}

}  // namespace edgecache
