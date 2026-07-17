#pragma once
#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>

#include "http/Http.h"

namespace edgecache {

class EventLoop {
public:

    EventLoop(std::string host, uint16_t port,
              std::function<HttpResponse(const HttpRequest&)> requestFn,
              std::atomic<bool>* running);

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

}
