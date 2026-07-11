#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "http/Http.h"

namespace edgecache {

// One epoll-based event loop per worker thread. Owns its own SO_REUSEPORT
// listener and the client connections the kernel routes to it, so there is no
// shared connection state or cache mutex across worker threads on the hot path.
//
// Client I/O is fully non-blocking (partial reads/writes buffered). The request
// handler it invokes may briefly block on an origin fetch during a cache miss;
// that is bounded by the origin timeout + circuit breaker and only affects this
// one loop's other connections (a documented simplification vs. a fully async
// origin state machine).
class EventLoop {
public:
    // requestFn maps a parsed request to a response, already bound to this
    // thread's cache shard. onTick runs once per loop iteration (e.g. periodic
    // TTL sweep). running is a shared shutdown flag.
    EventLoop(std::string host, uint16_t port,
              std::function<HttpResponse(const HttpRequest&)> requestFn,
              std::atomic<bool>* running);

    // Blocks running the loop until *running becomes false. Returns false if the
    // loop could not start (e.g. failed to bind).
    bool run();

    void setOnTick(std::function<void()> onTick) { onTick_ = std::move(onTick); }

private:
    struct Conn {
        int fd = -1;
        RequestParser parser;
        std::string outBuf;
        size_t outOff = 0;
        bool closeAfterFlush = false;
    };

    void acceptLoop(int epfd, int listenFd);
    void onReadable(int epfd, Conn& c);
    // Returns false if the connection was closed (and erased) during the flush,
    // in which case the caller must not touch `c` again.
    bool flush(int epfd, Conn& c);
    void updateEpollOut(int epfd, Conn& c, bool wantWrite);
    void closeConn(int epfd, int fd);

    std::string host_;
    uint16_t port_;
    std::function<HttpResponse(const HttpRequest&)> requestFn_;
    std::function<void()> onTick_;
    std::atomic<bool>* running_;
    std::unordered_map<int, Conn> conns_;
};

}  // namespace edgecache
