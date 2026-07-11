#pragma once
#include <cstdint>
#include <string>

namespace edgecache {

// Create a non-blocking listening socket bound with SO_REUSEPORT so every worker
// thread can open its own listener on the same port and let the kernel
// load-balance incoming connections across them (the thread-per-core pattern).
// Returns the fd, or -1 on failure (message written to `err`).
int createReusePortListener(const std::string& host, uint16_t port, std::string& err);

// Set/clear O_NONBLOCK on a fd.
bool setNonBlocking(int fd, bool nb);

}  // namespace edgecache
