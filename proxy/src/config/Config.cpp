#include "config/Config.h"

#include <cstdlib>
#include <iostream>
#include <thread>

namespace edgecache {

namespace {
std::string envStr(const char* key, const std::string& def) {
    const char* v = std::getenv(key);
    return v ? std::string(v) : def;
}
uint64_t envU64(const char* key, uint64_t def) {
    const char* v = std::getenv(key);
    if (!v) return def;
    try {
        return std::stoull(v);
    } catch (...) {
        return def;
    }
}
int envInt(const char* key, int def) {
    const char* v = std::getenv(key);
    if (!v) return def;
    try {
        return std::stoi(v);
    } catch (...) {
        return def;
    }
}
}  // namespace

Config Config::fromEnv() {
    Config c;
    c.listenHost = envStr("EDGECACHE_LISTEN_HOST", c.listenHost);
    c.listenPort = static_cast<uint16_t>(envInt("EDGECACHE_LISTEN_PORT", c.listenPort));
    c.metricsPort = static_cast<uint16_t>(envInt("EDGECACHE_METRICS_PORT", c.metricsPort));
    c.workerThreads = static_cast<unsigned int>(envInt("EDGECACHE_WORKERS", c.workerThreads));
    if (c.workerThreads == 0) {
        unsigned int hw = std::thread::hardware_concurrency();
        c.workerThreads = hw == 0 ? 2 : hw;
    }

    c.maxCacheBytes = envU64("EDGECACHE_MAX_CACHE_BYTES", c.maxCacheBytes);
    c.defaultTtlSeconds = envU64("EDGECACHE_DEFAULT_TTL_SECONDS", c.defaultTtlSeconds);

    c.defaultOriginHost = envStr("EDGECACHE_ORIGIN_HOST", c.defaultOriginHost);
    c.defaultOriginPort = static_cast<uint16_t>(envInt("EDGECACHE_ORIGIN_PORT", c.defaultOriginPort));
    c.originConnectTimeoutMs = envInt("EDGECACHE_ORIGIN_CONNECT_TIMEOUT_MS", c.originConnectTimeoutMs);
    c.originReadTimeoutMs = envInt("EDGECACHE_ORIGIN_READ_TIMEOUT_MS", c.originReadTimeoutMs);

    c.cbFailureThreshold = envInt("EDGECACHE_CB_FAILURE_THRESHOLD", c.cbFailureThreshold);
    c.cbOpenMs = envInt("EDGECACHE_CB_OPEN_MS", c.cbOpenMs);
    c.cbHalfOpenMaxProbes = envInt("EDGECACHE_CB_HALFOPEN_PROBES", c.cbHalfOpenMaxProbes);

    c.redisHost = envStr("EDGECACHE_REDIS_HOST", c.redisHost);
    c.redisPort = static_cast<uint16_t>(envInt("EDGECACHE_REDIS_PORT", c.redisPort));
    c.redisPassword = envStr("EDGECACHE_REDIS_PASSWORD", c.redisPassword);
    c.rulesHashKey = envStr("EDGECACHE_RULES_HASH", c.rulesHashKey);
    c.purgeChannel = envStr("EDGECACHE_PURGE_CHANNEL", c.purgeChannel);
    c.ruleUpdateChannel = envStr("EDGECACHE_RULE_UPDATE_CHANNEL", c.ruleUpdateChannel);
    c.rulePollIntervalSeconds = envInt("EDGECACHE_RULE_POLL_SECONDS", c.rulePollIntervalSeconds);

    // L2 accepts ON/1/true (case-sensitive on the common forms) to enable.
    {
        std::string v = envStr("EDGECACHE_L2_ENABLED", c.l2Enabled ? "ON" : "OFF");
        c.l2Enabled = (v == "ON" || v == "on" || v == "1" || v == "true" || v == "TRUE");
    }
    c.l2KeyPrefix = envStr("EDGECACHE_L2_KEY_PREFIX", c.l2KeyPrefix);

    c.kafkaBrokers = envStr("EDGECACHE_KAFKA_BROKERS", c.kafkaBrokers);
    c.kafkaTopic = envStr("EDGECACHE_KAFKA_TOPIC", c.kafkaTopic);

    c.replicaId = envStr("EDGECACHE_REPLICA_ID", envStr("HOSTNAME", c.replicaId));
    return c;
}

void Config::log() const {
    std::cerr << "[config] listen=" << listenHost << ":" << listenPort
              << " metrics=" << metricsPort << " workers=" << workerThreads
              << " maxCacheBytes=" << maxCacheBytes << " defaultTtl=" << defaultTtlSeconds
              << " origin=" << defaultOriginHost << ":" << defaultOriginPort
              << " redis=" << redisHost << ":" << redisPort << " replica=" << replicaId
              << " l2=" << (l2Enabled ? "on" : "off") << std::endl;
}

}  // namespace edgecache
