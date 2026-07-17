#include "redis/RedisL2Cache.h"

#include <chrono>
#include <sstream>

using namespace std;

namespace edgecache {

namespace {
uint64_t nowWallMs() {
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::system_clock::now().time_since_epoch())
                                     .count());
}

void writeStr(std::ostringstream& os, const std::string& s) {
    os << s.size() << '\n';
    os.write(s.data(), static_cast<std::streamsize>(s.size()));
    os << '\n';
}
}

std::string RedisL2Cache::serialize(const CacheEntry& e) {
    std::ostringstream os;
    os << "EC2\n";
    os << e.status << '\n';
    os << e.ttlSeconds << '\n';
    os << e.staleWhileRevalidateSeconds << '\n';
    os << nowWallMs() << '\n';
    writeStr(os, e.reason);
    writeStr(os, e.etag);
    os << e.headers.size() << '\n';
    for (const auto& kv : e.headers) {
        writeStr(os, kv.first);
        writeStr(os, kv.second);
    }
    writeStr(os, e.body);
    return os.str();
}

bool RedisL2Cache::deserialize(const std::string& blob, CacheEntry& out, uint64_t& storedAtWallMs,
                               uint64_t& ttlSeconds) {
    size_t p = 0;
    auto readLine = [&](std::string& line) -> bool {
        size_t nl = blob.find('\n', p);
        if (nl == std::string::npos) return false;
        line = blob.substr(p, nl - p);
        p = nl + 1;
        return true;
    };
    auto readU = [&](uint64_t& v) -> bool {
        std::string l;
        if (!readLine(l)) return false;
        try {
            v = std::stoull(l);
        } catch (...) {
            return false;
        }
        return true;
    };
    auto readStr = [&](std::string& s) -> bool {
        std::string l;
        if (!readLine(l)) return false;
        size_t len;
        try {
            len = static_cast<size_t>(std::stoull(l));
        } catch (...) {
            return false;
        }
        if (p + len > blob.size()) return false;
        s = blob.substr(p, len);
        p += len;
        if (p >= blob.size() || blob[p] != '\n') return false;
        p += 1;
        return true;
    };

    std::string magic;
    if (!readLine(magic) || magic != "EC2") return false;
    uint64_t status = 0, swr = 0;
    if (!readU(status) || !readU(ttlSeconds) || !readU(swr) || !readU(storedAtWallMs)) return false;
    std::string reason, etag;
    if (!readStr(reason) || !readStr(etag)) return false;
    uint64_t nHeaders = 0;
    if (!readU(nHeaders)) return false;
    Headers headers;
    for (uint64_t i = 0; i < nHeaders; ++i) {
        std::string k, v;
        if (!readStr(k) || !readStr(v)) return false;
        headers[k] = v;
    }
    std::string body;
    if (!readStr(body)) return false;

    out.status = static_cast<int>(status);
    out.reason = std::move(reason);
    out.etag = std::move(etag);
    out.staleWhileRevalidateSeconds = swr;
    out.headers = std::move(headers);
    out.body = std::move(body);
    return true;
}

bool RedisL2Cache::ensureConnected_() {
    if (conn_.connected()) return true;
    return conn_.connect();
}

std::optional<CacheEntry> RedisL2Cache::get(const std::string& cacheKey) {
    if (!cfg_.l2Enabled) return std::nullopt;
    std::lock_guard<std::mutex> lk(mu_);
    if (!ensureConnected_()) return std::nullopt;

    RedisReply r = conn_.command({"GET", keyFor(cacheKey)});
    if (r.isError()) {
        conn_.close();
        return std::nullopt;
    }
    if (r.type != RedisReply::Type::Bulk) return std::nullopt;

    CacheEntry e;
    uint64_t storedAtWallMs = 0, ttl = 0;
    if (!deserialize(r.str, e, storedAtWallMs, ttl)) return std::nullopt;

    uint64_t now = nowWallMs();
    uint64_t elapsedMs = now > storedAtWallMs ? now - storedAtWallMs : 0;
    if (elapsedMs >= ttl * 1000) return std::nullopt;
    uint64_t remaining = ttl - elapsedMs / 1000;

    e.storedAt = Clock::now();
    e.ttlSeconds = remaining == 0 ? 1 : remaining;
    return e;
}

void RedisL2Cache::put(const std::string& cacheKey, const CacheEntry& entry, uint64_t ttlSeconds) {
    if (!cfg_.l2Enabled || ttlSeconds == 0) return;
    std::string blob = serialize(entry);
    std::lock_guard<std::mutex> lk(mu_);
    if (!ensureConnected_()) return;
    RedisReply r =
        conn_.command({"SET", keyFor(cacheKey), blob, "EX", std::to_string(ttlSeconds)});
    if (r.isError()) conn_.close();
}

}
