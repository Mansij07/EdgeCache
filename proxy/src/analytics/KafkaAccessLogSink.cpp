#ifdef EDGECACHE_KAFKA
#include "analytics/KafkaAccessLogSink.h"

#include <librdkafka/rdkafka.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <sstream>

using namespace std;

namespace edgecache {

KafkaAccessLogSink::KafkaAccessLogSink(const std::string& brokers, const std::string& topic) {
    char errstr[512];
    rd_kafka_conf_t* conf = rd_kafka_conf_new();
    rd_kafka_conf_set(conf, "bootstrap.servers", brokers.c_str(), errstr, sizeof(errstr));

    rd_kafka_conf_set(conf, "queue.buffering.max.messages", "100000", errstr, sizeof(errstr));
    rd_kafka_conf_set(conf, "queue.buffering.max.ms", "200", errstr, sizeof(errstr));

    rk_ = rd_kafka_new(RD_KAFKA_PRODUCER, conf, errstr, sizeof(errstr));
    if (!rk_) {
        std::cerr << "[kafka] producer init failed: " << errstr << std::endl;
        return;
    }
    topic_ = rd_kafka_topic_new(rk_, topic.c_str(), nullptr);
    if (!topic_) {
        std::cerr << "[kafka] topic init failed" << std::endl;
        rd_kafka_destroy(rk_);
        rk_ = nullptr;
        return;
    }
    running_.store(true);
    pollThread_ = std::thread([this] { pollLoop(); });
    std::cerr << "[kafka] access-log producer -> " << brokers << " topic=" << topic << std::endl;
}

KafkaAccessLogSink::~KafkaAccessLogSink() {
    running_.store(false);
    if (pollThread_.joinable()) pollThread_.join();
    if (rk_) rd_kafka_flush(rk_, 1000);
    if (topic_) rd_kafka_topic_destroy(topic_);
    if (rk_) rd_kafka_destroy(rk_);
}

void KafkaAccessLogSink::pollLoop() {
    while (running_.load()) {
        rd_kafka_poll(rk_, 200);
    }
}

void KafkaAccessLogSink::log(const AccessLogEvent& ev) noexcept {
    if (!rk_ || !topic_) return;

    std::ostringstream os;
    os << "{\"timestamp\":" << ev.timestampMs << ",\"replicaId\":\"" << ev.replicaId
       << "\",\"path\":\"" << ev.path << "\",\"cacheKey\":\"" << ev.cacheKey
       << "\",\"result\":\"" << ev.result << "\",\"status\":" << ev.status
       << ",\"latencyMs\":" << ev.latencyMs << ",\"bytesServed\":" << ev.bytesServed << "}";
    std::string payload = os.str();

    int rc = rd_kafka_produce(topic_, RD_KAFKA_PARTITION_UA, RD_KAFKA_MSG_F_COPY,
                              const_cast<char*>(payload.data()), payload.size(),
                              ev.path.data(), ev.path.size(), nullptr);
    if (rc == -1) {

        dropped_.fetch_add(1, std::memory_order_relaxed);
    }
}

}
#endif
