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

// Request coalescing (thundering-herd / cache-stampede protection).
//
// When many requests miss the same key at once, exactly one becomes the
// "leader" and fetches from origin; the rest wait on a shared completion signal
// and reuse the leader's result instead of each firing their own origin call.
//
// The critical race — two requests for the same missing key arriving
// microseconds apart before either registers as in-flight — is closed by
// inserting the in-flight marker under the registry lock in acquire(), i.e.
// synchronously before the origin call is issued.
class InFlightRegistry {
public:
    struct InFlight {
        std::mutex m;
        std::condition_variable cv;
        bool ready = false;
        bool success = false;
        HttpResponse response;

        // Block until the leader publishes a result.
        void waitReady() {
            std::unique_lock<std::mutex> lk(m);
            cv.wait(lk, [this] { return ready; });
        }
    };

    struct Acquisition {
        bool leader = false;                 // true => you must fetch and publish()
        std::shared_ptr<InFlight> slot;      // shared completion signal
    };

    // Register interest in `key`. First caller becomes the leader.
    Acquisition acquire(const std::string& key);

    // Leader publishes the result to all waiters and clears the marker.
    void publish(const std::string& key, const HttpResponse& resp, bool success);

    // Number of requests that waited on someone else's fetch (coalesced).
    uint64_t coalescedTotal() const { return coalesced_.load(); }

private:
    std::mutex mapMutex_;
    std::unordered_map<std::string, std::shared_ptr<InFlight>> inflight_;
    std::atomic<uint64_t> coalesced_{0};
};

}  // namespace edgecache
