#include "metrics/MetricsServer.h"

#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <iostream>
#include <sstream>

#include "net/Listener.h"

namespace edgecache {

namespace {
void sendResponse(int fd, int status, const std::string& reason,
                  const std::string& contentType, const std::string& body) {
    std::ostringstream os;
    os << "HTTP/1.1 " << status << ' ' << reason << "\r\n";
    os << "Content-Type: " << contentType << "\r\n";
    os << "Content-Length: " << body.size() << "\r\n";
    os << "Connection: close\r\n\r\n";
    os << body;
    std::string out = os.str();
    size_t sent = 0;
    while (sent < out.size()) {
        ssize_t n = ::send(fd, out.data() + sent, out.size() - sent, MSG_NOSIGNAL);
        if (n <= 0) break;
        sent += static_cast<size_t>(n);
    }
}
}  // namespace

void MetricsServer::run() {
    std::string err;
    int listenFd = createReusePortListener(host_, port_, err);
    if (listenFd < 0) {
        std::cerr << "[metrics] listener failed: " << err << std::endl;
        return;
    }
    // Blocking accept with a short timeout so we can observe the shutdown flag.
    struct timeval tv{0, 200000};
    setsockopt(listenFd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setNonBlocking(listenFd, false);

    std::cerr << "[metrics] listening on " << host_ << ":" << port_ << std::endl;

    while (running_->load()) {
        int fd = ::accept(listenFd, nullptr, nullptr);
        if (fd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) continue;
            break;
        }
        struct timeval rt{2, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rt, sizeof(rt));

        std::string reqbuf;
        char buf[2048];
        ssize_t n = ::recv(fd, buf, sizeof(buf), 0);
        if (n > 0) reqbuf.assign(buf, static_cast<size_t>(n));

        // Parse the request target from the first line.
        std::string method, target;
        {
            std::istringstream is(reqbuf);
            is >> method >> target;
        }

        if (target == "/metrics") {
            sendResponse(fd, 200, "OK", "text/plain; version=0.0.4", metrics_.render());
        } else if (target == "/healthz") {
            sendResponse(fd, 200, "OK", "text/plain", "ok\n");
        } else if (target == "/readyz") {
            bool ready = readyFn_ ? readyFn_() : true;
            if (ready)
                sendResponse(fd, 200, "OK", "text/plain", "ready\n");
            else
                sendResponse(fd, 503, "Service Unavailable", "text/plain", "not ready\n");
        } else {
            sendResponse(fd, 404, "Not Found", "text/plain", "not found\n");
        }
        ::close(fd);
    }
    ::close(listenFd);
}

}  // namespace edgecache
