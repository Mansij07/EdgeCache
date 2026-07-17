#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace edgecache {

struct RedisReply {
    enum class Type { Nil, Status, Error, Integer, Bulk, Array };
    Type type = Type::Nil;
    std::string str;
    long long integer = 0;
    std::vector<RedisReply> elements;

    bool isError() const { return type == Type::Error; }
};

class RedisConnection {
public:
    RedisConnection(std::string host, uint16_t port, std::string password = "")
        : host_(std::move(host)), port_(port), password_(std::move(password)) {}
    ~RedisConnection() { close(); }

    RedisConnection(const RedisConnection&) = delete;
    RedisConnection& operator=(const RedisConnection&) = delete;

    bool connect();
    bool connected() const { return fd_ >= 0; }
    void close();

    RedisReply command(const std::vector<std::string>& args);

    bool subscribe(const std::vector<std::string>& channels);

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
    std::string rbuf_;
    size_t rpos_ = 0;
};

}
