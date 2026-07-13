#include "origin/HttpOriginClient.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <sstream>

namespace edgecache {

namespace {

int setNonBlocking(int fd, bool nb) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return -1;
    if (nb)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags);
}

// Connect with a timeout. Returns a connected fd or -1.
int connectWithTimeout(const OriginTarget& t, std::string& err) {
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    std::string portStr = std::to_string(t.port);

    struct addrinfo* res = nullptr;
    int gai = getaddrinfo(t.host.c_str(), portStr.c_str(), &hints, &res);
    if (gai != 0) {
        err = std::string("dns: ") + gai_strerror(gai);
        return -1;
    }

    int fd = -1;
    for (struct addrinfo* rp = res; rp != nullptr; rp = rp->ai_next) {
        fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        setNonBlocking(fd, true);

        int rc = ::connect(fd, rp->ai_addr, rp->ai_addrlen);
        if (rc == 0) {
            setNonBlocking(fd, false);
            freeaddrinfo(res);
            return fd;
        }
        if (errno == EINPROGRESS) {
            fd_set wset;
            FD_ZERO(&wset);
            FD_SET(fd, &wset);
            struct timeval tv;
            tv.tv_sec = t.connectTimeoutMs / 1000;
            tv.tv_usec = (t.connectTimeoutMs % 1000) * 1000;
            rc = select(fd + 1, nullptr, &wset, nullptr, &tv);
            if (rc > 0) {
                int soErr = 0;
                socklen_t len = sizeof(soErr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, &soErr, &len);
                if (soErr == 0) {
                    setNonBlocking(fd, false);
                    freeaddrinfo(res);
                    return fd;
                }
                err = std::string("connect: ") + std::strerror(soErr);
            } else if (rc == 0) {
                err = "connect: timeout";
            } else {
                err = std::string("connect select: ") + std::strerror(errno);
            }
        } else {
            err = std::string("connect: ") + std::strerror(errno);
        }
        ::close(fd);
        fd = -1;
    }
    freeaddrinfo(res);
    if (err.empty()) err = "connect: no addresses";
    return -1;
}

bool sendAll(int fd, const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

// Parse a raw HTTP response buffer into an HttpResponse. Body is everything
// after the header block (origin used Connection: close so EOF frames it).
bool parseResponse(const std::string& raw, HttpResponse& out) {
    size_t headerEnd = raw.find("\r\n\r\n");
    if (headerEnd == std::string::npos) return false;

    std::istringstream is(raw.substr(0, headerEnd));
    std::string line;
    if (!std::getline(is, line)) return false;
    if (!line.empty() && line.back() == '\r') line.pop_back();

    {
        std::istringstream ls(line);
        std::string version;
        int status = 0;
        ls >> version >> status;
        std::string reason;
        std::getline(ls, reason);
        if (!reason.empty() && reason.front() == ' ') reason.erase(0, 1);
        if (!reason.empty() && reason.back() == '\r') reason.pop_back();
        out.version = "HTTP/1.1";
        out.status = status ? status : 502;
        out.reason = reason;
    }

    while (std::getline(is, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty()) break;
        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;
        std::string name = line.substr(0, colon);
        std::string value = line.substr(colon + 1);
        while (!value.empty() && (value.front() == ' ' || value.front() == '\t')) value.erase(0, 1);
        // Drop hop-by-hop headers we re-manage on the way out.
        if (name == "Connection" || name == "Transfer-Encoding" ||
            name == "Content-Length" || name == "Keep-Alive")
            continue;
        out.headers[name] = value;
    }

    out.body = raw.substr(headerEnd + 4);
    return true;
}

}  // namespace

OriginResult HttpOriginClient::fetch(const HttpRequest& req, const OriginTarget& target) {
    OriginResult result;

    std::string err;
    int fd = connectWithTimeout(target, err);
    if (fd < 0) {
        result.error = err;
        result.timedOut = (err.find("timeout") != std::string::npos);
        return result;
    }

    // Read timeout on the socket.
    struct timeval tv;
    tv.tv_sec = target.readTimeoutMs / 1000;
    tv.tv_usec = (target.readTimeoutMs % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    int one = 1;
    setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));

    // Build the upstream request. HTTP/1.0 + Connection: close lets the origin
    // frame the body by closing the connection — simplest robust framing.
    std::ostringstream os;
    os << req.method << ' ' << req.target << " HTTP/1.0\r\n";
    os << "Host: " << target.host << "\r\n";
    for (const auto& kv : req.headers) {
        const std::string& n = kv.first;
        if (n == "Host" || n == "Connection" || n == "Keep-Alive" ||
            n == "Proxy-Connection" || n == "Transfer-Encoding" || n == "Content-Length")
            continue;
        os << n << ": " << kv.second << "\r\n";
    }
    os << "X-Forwarded-Proto: http\r\n";
    os << "Connection: close\r\n\r\n";
    if (!req.body.empty()) os << req.body;

    if (!sendAll(fd, os.str())) {
        result.error = "send failed";
        ::close(fd);
        return result;
    }

    std::string raw;
    char buf[16384];
    while (true) {
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) {
            raw.append(buf, static_cast<size_t>(n));
            if (raw.size() > 32ull * 1024 * 1024) break;  // 32 MiB safety cap
        } else if (n == 0) {
            break;  // origin closed — full response received
        } else {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                result.timedOut = true;
                result.error = "read timeout";
                ::close(fd);
                return result;
            }
            result.error = std::string("recv: ") + std::strerror(errno);
            ::close(fd);
            return result;
        }
    }
    ::close(fd);

    if (!parseResponse(raw, result.response)) {
        result.error = "malformed origin response";
        return result;
    }
    result.ok = true;
    return result;
}

}  // namespace edgecache
