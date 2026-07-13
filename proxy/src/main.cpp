#include <csignal>
#include <cstdlib>
#include <iostream>

#include "ProxyServer.h"
#include "config/Config.h"

namespace {
edgecache::ProxyServer* g_server = nullptr;

void handleSignal(int) {
    if (g_server) g_server->stop();
}
}  // namespace

int main() {
    // Never die from a write to a closed socket.
    std::signal(SIGPIPE, SIG_IGN);

    edgecache::Config cfg = edgecache::Config::fromEnv();
    edgecache::ProxyServer server(cfg);
    g_server = &server;

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    std::cerr << "EdgeCache proxy starting..." << std::endl;
    server.run();
    std::cerr << "EdgeCache proxy stopped." << std::endl;
    return 0;
}
