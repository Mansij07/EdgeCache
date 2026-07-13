#include "coalesce/InFlightRegistry.h"

namespace edgecache {

InFlightRegistry::Acquisition InFlightRegistry::acquire(const std::string& key) {
    std::lock_guard<std::mutex> lk(mapMutex_);
    auto it = inflight_.find(key);
    if (it == inflight_.end()) {
        auto slot = std::make_shared<InFlight>();
        inflight_.emplace(key, slot);
        return {true, slot};
    }
    // A fetch is already in progress for this key — join it as a waiter.
    coalesced_.fetch_add(1, std::memory_order_relaxed);
    return {false, it->second};
}

void InFlightRegistry::publish(const std::string& key, const HttpResponse& resp, bool success) {
    std::shared_ptr<InFlight> slot;
    {
        std::lock_guard<std::mutex> lk(mapMutex_);
        auto it = inflight_.find(key);
        if (it == inflight_.end()) return;
        slot = it->second;
        inflight_.erase(it);  // clear the marker so future misses start fresh
    }
    {
        std::lock_guard<std::mutex> lk(slot->m);
        slot->response = resp;
        slot->success = success;
        slot->ready = true;
    }
    slot->cv.notify_all();
}

}  // namespace edgecache
