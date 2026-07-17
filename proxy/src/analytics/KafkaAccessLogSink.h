#pragma once
#ifdef EDGECACHE_KAFKA
#include <atomic>
#include <string>
#include <thread>

#include "analytics/AccessLog.h"

struct rd_kafka_s;
struct rd_kafka_topic_s;

namespace edgecache {

class KafkaAccessLogSink : public AccessLogSink {
public:
    KafkaAccessLogSink(const std::string& brokers, const std::string& topic);
    ~KafkaAccessLogSink() override;

    bool ok() const { return rk_ != nullptr; }
    void log(const AccessLogEvent& ev) noexcept override;

private:
    void pollLoop();

    rd_kafka_s* rk_ = nullptr;
    rd_kafka_topic_s* topic_ = nullptr;
    std::thread pollThread_;
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> dropped_{0};
};

}
#endif
