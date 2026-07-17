#pragma once
#include <cstdint>
#include <string>

namespace edgecache {

struct Config {

    std::string listenHost = "0.0.0.0";
    uint16_t listenPort = 8080;
    uint16_t metricsPort = 9100;

    unsigned int workerThreads = 0;

    uint64_t maxCacheBytes = 256ull * 1024 * 1024;
    uint64_t defaultTtlSeconds = 60;

    std::string defaultOriginHost = "dummy-origin";
    uint16_t defaultOriginPort = 8081;
    int originConnectTimeoutMs = 2000;
    int originReadTimeoutMs = 5000;

    int cbFailureThreshold = 5;
    int cbOpenMs = 5000;
    int cbHalfOpenMaxProbes = 1;

    std::string redisHost = "redis";
    uint16_t redisPort = 6379;
    std::string redisPassword;
    std::string rulesHashKey = "edgecache:rules";
    std::string purgeChannel = "edgecache:purge";
    std::string ruleUpdateChannel = "edgecache:rules:updated";
    int rulePollIntervalSeconds = 15;

    bool l2Enabled = false;
    std::string l2KeyPrefix = "edgecache:l2:";

    std::string kafkaBrokers;
    std::string kafkaTopic = "edgecache.access-log";

    std::string replicaId = "proxy-local";

    static Config fromEnv();
    void log() const;
};

}
