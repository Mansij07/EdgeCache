#include "net/Listener.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>

namespace edgecache {

bool setNonBlocking(int fd, bool nb) {
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    if (nb)
        flags |= O_NONBLOCK;
    else
        flags &= ~O_NONBLOCK;
    return fcntl(fd, F_SETFL, flags) == 0;
}

int createReusePortListener(const std::string& host, uint16_t port, std::string& err) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        err = std::string("socket: ") + std::strerror(errno);
        return -1;
    }

    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#ifdef SO_REUSEPORT
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &one, sizeof(one)) < 0) {
        err = std::string("SO_REUSEPORT: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
#endif

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    } else if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) != 1) {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }

    if (bind(fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        err = std::string("bind: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (listen(fd, 1024) < 0) {
        err = std::string("listen: ") + std::strerror(errno);
        ::close(fd);
        return -1;
    }
    if (!setNonBlocking(fd, true)) {
        err = "set nonblocking failed";
        ::close(fd);
        return -1;
    }
    return fd;
}

}  // namespace edgecache
