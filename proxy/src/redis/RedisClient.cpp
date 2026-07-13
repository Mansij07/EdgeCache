#include "redis/RedisClient.h"

#include <fcntl.h>
#include <netdb.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstdlib>
#include <cstring>

namespace edgecache {

std::string RedisConnection::encode(const std::vector<std::string>& args) {
    std::string out = "*" + std::to_string(args.size()) + "\r\n";
    for (const auto& a : args) {
        out += "$" + std::to_string(a.size()) + "\r\n";
        out += a;
        out += "\r\n";
    }
    return out;
}

void RedisConnection::close() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
    rbuf_.clear();
    rpos_ = 0;
}

bool RedisConnection::connect() {
    close();
    struct addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    struct addrinfo* res = nullptr;
    if (getaddrinfo(host_.c_str(), std::to_string(port_).c_str(), &hints, &res) != 0)
        return false;

    for (struct addrinfo* rp = res; rp; rp = rp->ai_next) {
        int fd = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (fd < 0) continue;
        if (::connect(fd, rp->ai_addr, rp->ai_addrlen) == 0) {
            int one = 1;
            setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
            fd_ = fd;
            break;
        }
        ::close(fd);
    }
    freeaddrinfo(res);
    if (fd_ < 0) return false;

    if (!password_.empty()) {
        RedisReply r = command({"AUTH", password_});
        if (r.isError()) {
            close();
            return false;
        }
    }
    return true;
}

bool RedisConnection::writeAll(const std::string& data) {
    size_t sent = 0;
    while (sent < data.size()) {
        ssize_t n = ::send(fd_, data.data() + sent, data.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) {
            if (errno == EINTR) continue;
            return false;
        }
        sent += static_cast<size_t>(n);
    }
    return true;
}

bool RedisConnection::fillBuffer(int timeoutMs) {
    // Compact consumed prefix occasionally.
    if (rpos_ > 4096) {
        rbuf_.erase(0, rpos_);
        rpos_ = 0;
    }
    struct pollfd pfd;
    pfd.fd = fd_;
    pfd.events = POLLIN;
    int rc = poll(&pfd, 1, timeoutMs);
    if (rc == 0) return false;  // timeout
    if (rc < 0) {
        if (errno == EINTR) return false;
        close();
        return false;
    }
    char buf[8192];
    ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
    if (n > 0) {
        rbuf_.append(buf, static_cast<size_t>(n));
        return true;
    }
    if (n == 0) {  // peer closed
        close();
        return false;
    }
    if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK) return false;
    close();
    return false;
}

bool RedisConnection::readLine(std::string& line, int timeoutMs) {
    while (true) {
        size_t nl = rbuf_.find("\r\n", rpos_);
        if (nl != std::string::npos) {
            line = rbuf_.substr(rpos_, nl - rpos_);
            rpos_ = nl + 2;
            return true;
        }
        if (!fillBuffer(timeoutMs)) return false;
    }
}

bool RedisConnection::readN(std::string& out, size_t n, int timeoutMs) {
    while (rbuf_.size() - rpos_ < n + 2) {  // +2 for trailing CRLF
        if (!fillBuffer(timeoutMs)) return false;
    }
    out = rbuf_.substr(rpos_, n);
    rpos_ += n + 2;  // skip CRLF
    return true;
}

bool RedisConnection::parseReply(RedisReply& out, int timeoutMs) {
    std::string line;
    if (!readLine(line, timeoutMs)) return false;
    if (line.empty()) return false;
    char type = line[0];
    std::string rest = line.substr(1);
    switch (type) {
        case '+':
            out.type = RedisReply::Type::Status;
            out.str = rest;
            return true;
        case '-':
            out.type = RedisReply::Type::Error;
            out.str = rest;
            return true;
        case ':':
            out.type = RedisReply::Type::Integer;
            out.integer = std::strtoll(rest.c_str(), nullptr, 10);
            return true;
        case '$': {
            long long len = std::strtoll(rest.c_str(), nullptr, 10);
            if (len < 0) {
                out.type = RedisReply::Type::Nil;
                return true;
            }
            out.type = RedisReply::Type::Bulk;
            return readN(out.str, static_cast<size_t>(len), timeoutMs);
        }
        case '*': {
            long long count = std::strtoll(rest.c_str(), nullptr, 10);
            out.type = RedisReply::Type::Array;
            if (count < 0) {
                out.type = RedisReply::Type::Nil;
                return true;
            }
            out.elements.resize(static_cast<size_t>(count));
            for (long long i = 0; i < count; ++i) {
                if (!parseReply(out.elements[static_cast<size_t>(i)], timeoutMs)) return false;
            }
            return true;
        }
        default:
            return false;
    }
}

RedisReply RedisConnection::command(const std::vector<std::string>& args) {
    RedisReply reply;
    if (fd_ < 0) {
        reply.type = RedisReply::Type::Error;
        reply.str = "not connected";
        return reply;
    }
    if (!writeAll(encode(args))) {
        close();
        reply.type = RedisReply::Type::Error;
        reply.str = "write failed";
        return reply;
    }
    if (!parseReply(reply, -1)) {
        reply.type = RedisReply::Type::Error;
        reply.str = "read failed";
    }
    return reply;
}

bool RedisConnection::subscribe(const std::vector<std::string>& channels) {
    if (fd_ < 0) return false;
    std::vector<std::string> args = {"SUBSCRIBE"};
    args.insert(args.end(), channels.begin(), channels.end());
    if (!writeAll(encode(args))) {
        close();
        return false;
    }
    // Read the subscribe confirmations (one array reply per channel).
    for (size_t i = 0; i < channels.size(); ++i) {
        RedisReply r;
        if (!parseReply(r, 5000)) return false;
    }
    return true;
}

bool RedisConnection::readReply(RedisReply& out, int timeoutMs) {
    if (fd_ < 0) return false;
    return parseReply(out, timeoutMs);
}

}  // namespace edgecache
