#pragma once
#include <chrono>
#include <mutex>
#include <string>

namespace edgecache {

// Per-origin circuit breaker (State pattern). Guards OriginClient calls so a
// failing/timing-out origin is stopped from being hammered.
//
//   CLOSED  --N consecutive failures-->  OPEN
//   OPEN    --openMs elapsed-->          HALF_OPEN (allow limited probes)
//   HALF_OPEN --probe success-->         CLOSED
//   HALF_OPEN --probe failure-->         OPEN (reset timer)
class CircuitBreaker {
public:
    enum class State { Closed, Open, HalfOpen };

    CircuitBreaker(int failureThreshold, int openMs, int halfOpenMaxProbes)
        : failureThreshold_(failureThreshold),
          openMs_(openMs),
          halfOpenMaxProbes_(halfOpenMaxProbes) {}

    // Called before issuing an origin request. Returns false if the request
    // should be rejected immediately (fast 502) because the breaker is open.
    bool allowRequest();

    void recordSuccess();
    void recordFailure();

    State state() const;
    const char* stateName() const;

private:
    using Clock = std::chrono::steady_clock;
    void transitionToOpen_();  // caller holds mutex_

    mutable std::mutex mutex_;
    State state_ = State::Closed;
    int consecutiveFailures_ = 0;
    int halfOpenProbes_ = 0;
    Clock::time_point openedAt_;

    const int failureThreshold_;
    const int openMs_;
    const int halfOpenMaxProbes_;
};

}  // namespace edgecache
