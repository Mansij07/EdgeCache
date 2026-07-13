#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <thread>

#include "config/Config.h"
#include "redis/RedisClient.h"
#include "redis/RuleStore.h"

namespace edgecache {

// Owns the proxy's two Redis connections and the background threads that keep
// the replica coordinated with the fleet:
//
//  * Subscriber thread — SUBSCRIBEs to the purge and rule-update channels. A
//    purge message triggers PurgeHandler (which evicts matching keys from every
//    shard — the Observer: PurgeListener knows nothing about cache internals).
//    A rule-update message triggers an immediate rule reload.
//
//  * Poller thread — HGETALLs the rule hash on an interval as a safety net in
//    case a pub/sub message was missed during a reconnect window, and reloads
//    on demand when the subscriber signals a rule update.
//
// If Redis is unreachable, both threads keep retrying while the request path
// continues serving from the last-known-good cache and rule set.
class RedisCoordinator {
public:
    // Returns the number of keys evicted (for metrics). Idempotent.
    using PurgeHandler = std::function<uint64_t(const std::string& pattern)>;
    using PurgeMessageHook = std::function<void()>;  // called once per purge msg received

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
    std::atomic<bool> reloadRequested_{true};  // force an initial load
};

// Parse a rule value JSON object like {"ttl":60,"originId":"abc","swr":10}.
// Tolerant minimal parser (no external JSON dependency). Exposed for testing.
Rule parseRuleJson(const std::string& pathPattern, const std::string& json);

}  // namespace edgecache
