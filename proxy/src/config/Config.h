#pragma once
#include <cstdint>
#include <string>

namespace edgecache {

// Runtime configuration for a proxy replica. Populated from environment
// variables (12-factor style) so a Kubernetes ConfigMap can drive it with
// zero code changes. See Config::fromEnv().
struct Config {
    // Networking
    std::string listenHost = "0.0.0.0";
    uint16_t listenPort = 8080;   // client-facing proxy traffic
    uint16_t metricsPort = 9100;  // Prometheus /metrics, /healthz, /readyz

    // Concurrency: number of thread-per-core event loops. 0 => hardware_concurrency().
    unsigned int workerThreads = 0;

    // Cache sizing. The pod's memory *limit* should drive this (limit minus a
    // safety buffer). Bytes are split evenly across worker shards.
    uint64_t maxCacheBytes = 256ull * 1024 * 1024;  // 256 MiB default
    uint64_t defaultTtlSeconds = 60;                // when origin gives no directive

    // Origin defaults (a rule/origin registration in the control plane overrides).
    std::string defaultOriginHost = "dummy-origin";
    uint16_t defaultOriginPort = 8081;
    int originConnectTimeoutMs = 2000;
    int originReadTimeoutMs = 5000;

    // Circuit breaker
    int cbFailureThreshold = 5;      // consecutive failures to open
    int cbOpenMs = 5000;             // how long to stay open before half-open probe
    int cbHalfOpenMaxProbes = 1;     // probes allowed in half-open

    // Redis coordination
    std::string redisHost = "redis";
    uint16_t redisPort = 6379;
    std::string redisPassword;       // empty = no auth
    std::string rulesHashKey = "edgecache:rules";
    std::string purgeChannel = "edgecache:purge";
    std::string ruleUpdateChannel = "edgecache:rules:updated";
    int rulePollIntervalSeconds = 15;  // safety-net poll in case a pub/sub msg was missed

    // L2 cache tier (advanced): a shared Redis-backed second tier behind the
    // in-process L1. Raises effective fleet-wide hit rate — a key cached by one
    // replica is an L2 hit on the others — without duplicating memory per pod.
    // Best-effort: never fails a request if Redis is down.
    bool l2Enabled = false;
    std::string l2KeyPrefix = "edgecache:l2:";

    // Kafka access-log producer (optional; only used if the proxy is built with
    // -DEDGECACHE_KAFKA and brokers are configured). Fire-and-forget analytics.
    std::string kafkaBrokers;  // empty = disabled
    std::string kafkaTopic = "edgecache.access-log";

    // Identity (used in stats keys and access-log events).
    std::string replicaId = "proxy-local";

    static Config fromEnv();
    void log() const;
};

}  // namespace edgecache
