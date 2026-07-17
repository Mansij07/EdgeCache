#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "config/Config.h"
#include "redis/RedisClient.h"
#include "redis/RuleStore.h"

namespace edgecache {

class RedisCoordinator {
public:

    using PurgeHandler = std::function<uint64_t(const std::string& pattern)>;
    using PurgeMessageHook = std::function<void()>;

    RedisCoordinator(const Config& cfg, RuleStore& store, PurgeHandler onPurge);
    ~RedisCoordinator();

    void start();
    void stop();

    bool redisConnected() const { return connected_.load(); }
    void setPurgeMessageHook(PurgeMessageHook hook) { purgeHook_ = std::move(hook); }

private:
    void subscriberLoop();
    void pollerLoop();
    bool reloadRules(RedisConnection& conn);

    const Config& cfg_;
    RuleStore& store_;
    PurgeHandler onPurge_;
    PurgeMessageHook purgeHook_;

    std::thread subThread_;
    std::thread pollThread_;
    std::atomic<bool> running_{false};
    std::atomic<bool> connected_{false};
    std::atomic<bool> reloadRequested_{true};
};

Rule parseRuleJson(const std::string& pathPattern, const std::string& json);

}
