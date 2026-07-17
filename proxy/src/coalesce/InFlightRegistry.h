#pragma once
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

#include "http/Http.h"

namespace edgecache {

class InFlightRegistry {
public:
    struct InFlight {
        std::mutex m;
        std::condition_variable cv;
        bool ready = false;
        bool success = false;
        HttpResponse response;

        void waitReady() {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [this] { return ready; });
        }
    };

    struct Acquisition {
        bool leader = false;
        std::shared_ptr<InFlight> slot;
    };

    Acquisition acquire(const std::string& key);

    void publish(const std::string& key, const HttpResponse& resp, bool success);

    uint64_t coalescedTotal() const { return coalesced_.load(); }

private:
    std::mutex mapMutex_;
    std::unordered_map<std::string, std::shared_ptr<InFlight>> inflight_;
    std::atomic<uint64_t> coalesced_{0};
};

}
