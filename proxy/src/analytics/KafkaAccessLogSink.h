#pragma once
#ifdef EDGECACHE_KAFKA
#include <atomic>
#include <string>
#include <thread>

#include "analytics/AccessLog.h"

struct rd_kafka_s;
struct rd_kafka_topic_s;

namespace edgecache {

// Kafka-backed access-log sink (compiled only when built with -DEDGECACHE_KAFKA
// and librdkafka available). Producing is fire-and-forget: rd_kafka_produce
// enqueues into librdkafka's internal buffer and returns immediately; a
// background thread drains delivery callbacks via rd_kafka_poll. If the broker
// is down or the queue is full, the event is dropped — never awaited. This
// asymmetry (a dropped access-log event is fine; a dropped request is not) is
// the entire justification for Kafka being here.
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

}  // namespace edgecache
#endif  // EDGECACHE_KAFKA
