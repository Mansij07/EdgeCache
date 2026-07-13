#include "redis/RedisCoordinator.h"

#include <chrono>
#include <cstdlib>
#include <iostream>

namespace edgecache {

namespace {
// Extract the numeric or string value for `key` from a flat JSON object.
// Returns empty string if not found. Handles both "key":123 and "key":"abc".
std::string jsonField(const std::string& json, const std::string& key) {
    std::string needle = "\"" + key + "\"";
    size_t p = json.find(needle);
    if (p == std::string::npos) return "";
    p = json.find(':', p + needle.size());
    if (p == std::string::npos) return "";
    ++p;
    while (p < json.size() && (json[p] == ' ' || json[p] == '\t')) ++p;
    if (p >= json.size()) return "";
    if (json[p] == '"') {
        size_t end = json.find('"', p + 1);
        if (end == std::string::npos) return "";
        return json.substr(p + 1, end - p - 1);
    }
    size_t end = p;
    while (end < json.size() && json[end] != ',' && json[end] != '}' && json[end] != ' ') ++end;
    return json.substr(p, end - p);
}

uint64_t toU64(const std::string& s, uint64_t def = 0) {
    if (s.empty()) return def;
    try {
        return std::stoull(s);
    } catch (...) {
        return def;
    }
}
}  // namespace

Rule parseRuleJson(const std::string& pathPattern, const std::string& json) {
    Rule r;
    r.pathPattern = pathPattern;
    r.ttlSeconds = toU64(jsonField(json, "ttl"));
    r.staleWhileRevalidateSeconds = toU64(jsonField(json, "swr"));
    r.originId = jsonField(json, "originId");
    return r;
}

RedisCoordinator::RedisCoordinator(const Config& cfg, RuleStore& store, PurgeHandler onPurge)
    : cfg_(cfg), store_(store), onPurge_(std::move(onPurge)) {}

RedisCoordinator::~RedisCoordinator() { stop(); }

void RedisCoordinator::start() {
    running_.store(true);
    subThread_ = std::thread([this] { subscriberLoop(); });
    pollThread_ = std::thread([this] { pollerLoop(); });
}

void RedisCoordinator::stop() {
    if (!running_.exchange(false)) return;
    if (subThread_.joinable()) subThread_.join();
    if (pollThread_.joinable()) pollThread_.join();
}

bool RedisCoordinator::reloadRules(RedisConnection& conn) {
    RedisReply r = conn.command({"HGETALL", cfg_.rulesHashKey});
    if (r.isError() || r.type != RedisReply::Type::Array) return false;
    std::vector<Rule> rules;
    for (size_t i = 0; i + 1 < r.elements.size(); i += 2) {
        const std::string& field = r.elements[i].str;
        const std::string& value = r.elements[i + 1].str;
        rules.push_back(parseRuleJson(field, value));
    }
    store_.replaceAll(std::move(rules));
    return true;
}

void RedisCoordinator::subscriberLoop() {
    RedisConnection conn(cfg_.redisHost, cfg_.redisPort, cfg_.redisPassword);
    while (running_.load()) {
        if (!conn.connected()) {
            if (!conn.connect() ||
                !conn.subscribe({cfg_.purgeChannel, cfg_.ruleUpdateChannel})) {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
            std::cerr << "[redis] subscriber connected to " << cfg_.redisHost << ":"
                      << cfg_.redisPort << std::endl;
        }

        RedisReply msg;
        if (!conn.readReply(msg, 1000)) {
            if (!conn.connected()) continue;  // disconnected -> reconnect next loop
            continue;                          // timeout -> just loop
        }
        // Pub/sub message shape: ["message", <channel>, <payload>]
        if (msg.type == RedisReply::Type::Array && msg.elements.size() == 3 &&
            msg.elements[0].str == "message") {
            const std::string& channel = msg.elements[1].str;
            const std::string& payload = msg.elements[2].str;
            if (channel == cfg_.purgeChannel) {
                if (purgeHook_) purgeHook_();
                uint64_t evicted = onPurge_ ? onPurge_(payload) : 0;
                std::cerr << "[redis] purge '" << payload << "' evicted " << evicted
                          << " keys" << std::endl;
            } else if (channel == cfg_.ruleUpdateChannel) {
                reloadRequested_.store(true);
            }
        }
    }
    conn.close();
}

void RedisCoordinator::pollerLoop() {
    // Fast liveness heartbeat: how often to PING Redis so `connected_` (exposed
    // as the edgecache_redis_connected metric) reflects reality quickly — rather
    // than only as fast as the much slower rule-reload cadence below.
    constexpr int kPingIntervalMs = 1000;

    RedisConnection conn(cfg_.redisHost, cfg_.redisPort, cfg_.redisPassword);
    auto lastPoll = std::chrono::steady_clock::now() - std::chrono::hours(1);
    auto lastPing = std::chrono::steady_clock::now() - std::chrono::hours(1);

    while (running_.load()) {
        if (!conn.connected()) {
            if (!conn.connect()) {
                connected_.store(false);
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }

        auto now = std::chrono::steady_clock::now();

        // Heartbeat: a cheap PING probes the connection actively. A dead peer
        // (e.g. Redis stopped) fails the PING, so connected_ drops within ~1s
        // instead of waiting up to rulePollIntervalSeconds for the next reload.
        if (std::chrono::duration_cast<std::chrono::milliseconds>(now - lastPing).count() >=
            kPingIntervalMs) {
            RedisReply pong = conn.command({"PING"});
            lastPing = now;
            if (pong.type == RedisReply::Type::Status && !pong.isError()) {
                connected_.store(true);
            } else {
                connected_.store(false);
                conn.close();  // force a clean reconnect on the next iteration
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
                continue;
            }
        }

        bool due = std::chrono::duration_cast<std::chrono::seconds>(now - lastPoll).count() >=
                   cfg_.rulePollIntervalSeconds;
        if (reloadRequested_.exchange(false) || due) {
            if (reloadRules(conn)) {
                connected_.store(true);
                lastPoll = now;
            } else {
                connected_.store(false);
                conn.close();  // drop the dead socket so we reconnect cleanly
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
    conn.close();
}

}  // namespace edgecache
