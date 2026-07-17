#include "net/EventLoop.h"

#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>

#include "net/Listener.h"

using namespace std;

namespace edgecache {

EventLoop::EventLoop(std::string host, uint16_t port,
                     std::function<HttpResponse(const HttpRequest&)> requestFn,
                     std::atomic<bool>* running)
    : host_(std::move(host)),
      port_(port),
      requestFn_(std::move(requestFn)),
      running_(running) {}

void EventLoop::updateEpollOut(int epfd, Conn& c, bool wantWrite) {
    struct epoll_event ev{};
    ev.data.fd = c.fd;
    ev.events = wantWrite ? (EPOLLIN | EPOLLOUT) : EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_MOD, c.fd, &ev);
}

void EventLoop::closeConn(int epfd, int fd) {
    epoll_ctl(epfd, EPOLL_CTL_DEL, fd, nullptr);
    ::close(fd);
    conns_.erase(fd);
}

void EventLoop::acceptLoop(int epfd, int listenFd) {
    while (true) {
        int cfd = ::accept(listenFd, nullptr, nullptr);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            break;
        }
        setNonBlocking(cfd, true);
        struct epoll_event ev{};
        ev.data.fd = cfd;
        ev.events = EPOLLIN;
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, cfd, &ev) < 0) {
            ::close(cfd);
            continue;
        }
        conns_[cfd].fd = cfd;
    }
}

void EventLoop::onReadable(int epfd, Conn& c) {
    char buf[16384];
    bool peerClosed = false;
    while (true) {
        ssize_t n = ::recv(c.fd, buf, sizeof(buf), 0);
        if (n > 0) {
            c.parser.feed(buf, static_cast<size_t>(n));
        } else if (n == 0) {
            peerClosed = true;
            break;
        } else {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;
            if (errno == EINTR) continue;
            peerClosed = true;
            break;
        }
    }

    while (true) {
        HttpRequest req;
        auto st = c.parser.parse(req);
        if (st == RequestParser::State::NeedMore) break;
        if (st == RequestParser::State::Error) {
            HttpResponse bad = HttpResponse::simple(400, "Bad Request", "bad request\n");
            c.outBuf += bad.serialize(false);
            c.closeAfterFlush = true;
            break;
        }
        bool keepAlive = req.keepAlive();
        HttpResponse resp = requestFn_(req);
        c.outBuf += resp.serialize(keepAlive);
        if (!keepAlive) {
            c.closeAfterFlush = true;
            break;
        }
    }

    int fd = c.fd;
    bool wantClose = peerClosed;
    if (!flush(epfd, c)) return;

    if (wantClose && c.outBuf.empty()) {
        closeConn(epfd, fd);
    }
}

bool EventLoop::flush(int epfd, Conn& c) {
    while (c.outOff < c.outBuf.size()) {
        ssize_t n = ::send(c.fd, c.outBuf.data() + c.outOff, c.outBuf.size() - c.outOff,
                           MSG_NOSIGNAL);
        if (n > 0) {
            c.outOff += static_cast<size_t>(n);
        } else {
            if (errno == EINTR) continue;
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                updateEpollOut(epfd, c, true);
                return true;
            }
            closeConn(epfd, c.fd);
            return false;
        }
    }

    c.outBuf.clear();
    c.outOff = 0;
    updateEpollOut(epfd, c, false);
    if (c.closeAfterFlush) {
        closeConn(epfd, c.fd);
        return false;
    }
    return true;
}

bool EventLoop::run() {
    std::string err;
    int listenFd = createReusePortListener(host_, port_, err);
    if (listenFd < 0) {
        std::cerr << "[eventloop] listener failed: " << err << std::endl;
        return false;
    }
    int epfd = epoll_create1(0);
    if (epfd < 0) {
        std::cerr << "[eventloop] epoll_create1 failed" << std::endl;
        ::close(listenFd);
        return false;
    }
    struct epoll_event lev{};
    lev.data.fd = listenFd;
    lev.events = EPOLLIN;
    epoll_ctl(epfd, EPOLL_CTL_ADD, listenFd, &lev);

    constexpr int kMaxEvents = 256;
    struct epoll_event events[kMaxEvents];

    while (running_->load()) {
        int nfds = epoll_wait(epfd, events, kMaxEvents, 500);
        if (nfds < 0) {
            if (errno == EINTR) continue;
            break;
        }
        for (int i = 0; i < nfds; ++i) {
            int fd = events[i].data.fd;
            if (fd == listenFd) {
                acceptLoop(epfd, listenFd);
                continue;
            }
            auto it = conns_.find(fd);
            if (it == conns_.end()) continue;
            Conn& c = it->second;

            if (events[i].events & (EPOLLHUP | EPOLLERR)) {
                closeConn(epfd, fd);
                continue;
            }
            if (events[i].events & EPOLLOUT) {
                flush(epfd, c);
                if (conns_.find(fd) == conns_.end()) continue;
            }
            if (events[i].events & EPOLLIN) {
                onReadable(epfd, c);
            }
        }
        if (onTick_) onTick_();
    }

    for (auto& [fd, c] : conns_) ::close(fd);
    conns_.clear();
    ::close(listenFd);
    ::close(epfd);
    return true;
}

}
