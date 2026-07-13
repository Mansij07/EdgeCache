#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace edgecache {

// A parsed RESP reply.
struct RedisReply {
    enum class Type { Nil, Status, Error, Integer, Bulk, Array };
    Type type = Type::Nil;
    std::string str;             // Status / Error / Bulk
    long long integer = 0;       // Integer
    std::vector<RedisReply> elements;  // Array

    bool isError() const { return type == Type::Error; }
};

// Minimal blocking Redis client speaking RESP directly over a TCP socket — no
// external dependency (hiredis) required, which keeps the proxy image tiny and
// the build free of third-party libs. Supports the small command surface
// EdgeCache needs: AUTH, PING, HGETALL, SUBSCRIBE, and reading pub/sub messages.
//
// One RedisConnection is a single socket. A pub/sub subscription monopolizes a
// connection (Redis rule), so the proxy uses separate connections for the
// subscriber and the rule poller.
class RedisConnection {
public:
    RedisConnection(std::string host, uint16_t port, std::string password = "")
        : host_(std::move(host)), port_(port), password_(std::move(password)) {}
    ~RedisConnection() { close(); }

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;

    // (Re)establish the connection, running AUTH if a password is set.
    bool connect();
    bool connected() const { return fd_ >= 0; }
    void close();

    // Send a command (array of bulk strings) and read the single reply.
    RedisReply command(const std::vector<std::string>& args);

    // Subscribe to one or more channels on this connection. After this the
    // connection is a subscriber — use readReply() to pull messages.
    bool subscribe(const std::vector<std::string>& channels);

    // Read one reply, blocking up to timeoutMs (<=0 means block indefinitely).
    // Returns false on timeout, disconnect, or protocol error (check connected()).
    bool readReply(RedisReply& out, int timeoutMs);

private:
    bool writeAll(const std::string& data);
    bool fillBuffer(int timeoutMs);
    bool readLine(std::string& line, int timeoutMs);
    bool readN(std::string& out, size_t n, int timeoutMs);
    bool parseReply(RedisReply& out, int timeoutMs);
    static std::string encode(const std::vector<std::string>& args);

    std::string host_;
    uint16_t port_;
    std::string password_;
    int fd_ = -1;
    std::string rbuf_;  // read buffer
    size_t rpos_ = 0;   // consumed offset into rbuf_
};

}  // namespace edgecache
