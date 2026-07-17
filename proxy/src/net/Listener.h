#pragma once
#include <cstdint>
#include <string>

namespace edgecache {

int createReusePortListener(const std::string& host, uint16_t port, std::string& err);

bool setNonBlocking(int fd, bool nb);

}
