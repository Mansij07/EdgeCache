#include "origin/CircuitBreaker.h"

namespace edgecache {

bool CircuitBreaker::allowRequest() {
    std::lock_guard<std::mutex> lk(mutex_);
    switch (state_) {
        case State::Closed:
            return true;
        case State::Open: {
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                               Clock::now() - openedAt_)
                               .count();
            if (elapsed >= openMs_) {
                // Move to half-open and let this request be a probe.
                state_ = State::HalfOpen;
                halfOpenProbes_ = 1;
                return true;
            }
            return false;
        }
        case State::HalfOpen:
            if (halfOpenProbes_ < halfOpenMaxProbes_) {
                ++halfOpenProbes_;
                return true;
            }
            return false;
    }
    return true;
}

void CircuitBreaker::recordSuccess() {
    std::lock_guard<std::mutex> lk(mutex_);
    consecutiveFailures_ = 0;
    if (state_ != State::Closed) {
        state_ = State::Closed;
        halfOpenProbes_ = 0;
    }
}

void CircuitBreaker::recordFailure() {
    std::lock_guard<std::mutex> lk(mutex_);
    if (state_ == State::HalfOpen) {
        transitionToOpen_();
        return;
    }
    ++consecutiveFailures_;
    if (state_ == State::Closed && consecutiveFailures_ >= failureThreshold_) {
        transitionToOpen_();
    }
}

void CircuitBreaker::transitionToOpen_() {
    state_ = State::Open;
    openedAt_ = Clock::now();
    halfOpenProbes_ = 0;
}

CircuitBreaker::State CircuitBreaker::state() const {
    std::lock_guard<std::mutex> lk(mutex_);
    return state_;
}

const char* CircuitBreaker::stateName() const {
    switch (state()) {
        case State::Closed: return "closed";
        case State::Open: return "open";
        case State::HalfOpen: return "half_open";
    }
    return "unknown";
}

}  // namespace edgecache
