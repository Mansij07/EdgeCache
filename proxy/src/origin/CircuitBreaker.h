#pragma once
#include <chrono>
#include <mutex>
#include <string>

namespace edgecache {

class CircuitBreaker {
public:
    enum class State { Closed, Open, HalfOpen };

    CircuitBreaker(int failureThreshold, int openMs, int halfOpenMaxProbes)
        : failureThreshold_(failureThreshold),
          openMs_(openMs),
          halfOpenMaxProbes_(halfOpenMaxProbes) {}

    bool allowRequest();

    void recordSuccess();
    void recordFailure();

    State state() const;
    const char* stateName() const;

private:
    using Clock = std::chrono::steady_clock;
    void transitionToOpen_();

    mutable std::mutex mutex_;
    State state_ = State::Closed;
    int consecutiveFailures_ = 0;
    int halfOpenProbes_ = 0;
    Clock::time_point openedAt_;

    const int failureThreshold_;
    const int openMs_;
    const int halfOpenMaxProbes_;
};

}
