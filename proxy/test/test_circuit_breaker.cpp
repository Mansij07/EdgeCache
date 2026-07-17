#include <thread>

#include "origin/CircuitBreaker.h"
#include "test_framework.h"

using namespace std;

using namespace edgecache;

TEST(cb_opens_after_threshold) {
    CircuitBreaker cb(3, 100, 1);
    CHECK(cb.allowRequest());
    cb.recordFailure();
    cb.recordFailure();
    CHECK(cb.state() == CircuitBreaker::State::Closed);
    cb.recordFailure();
    CHECK(cb.state() == CircuitBreaker::State::Open);
    CHECK(!cb.allowRequest());
}

TEST(cb_success_resets_failures) {
    CircuitBreaker cb(3, 100, 1);
    cb.recordFailure();
    cb.recordFailure();
    cb.recordSuccess();
    cb.recordFailure();
    cb.recordFailure();
    CHECK(cb.state() == CircuitBreaker::State::Closed);
}

TEST(cb_half_open_then_close_on_success) {
    CircuitBreaker cb(1, 50, 1);
    cb.recordFailure();
    CHECK(cb.state() == CircuitBreaker::State::Open);
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    CHECK(cb.allowRequest());
    CHECK(cb.state() == CircuitBreaker::State::HalfOpen);
    CHECK(!cb.allowRequest());
    cb.recordSuccess();
    CHECK(cb.state() == CircuitBreaker::State::Closed);
}

TEST(cb_half_open_failure_reopens) {
    CircuitBreaker cb(1, 50, 1);
    cb.recordFailure();
    std::this_thread::sleep_for(std::chrono::milliseconds(70));
    CHECK(cb.allowRequest());
    cb.recordFailure();
    CHECK(cb.state() == CircuitBreaker::State::Open);
}
