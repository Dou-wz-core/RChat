#include "chatserver.hpp"
#include "net/cli.hpp"
#include <csignal>
#include <iostream>
namespace { volatile std::sig_atomic_t running = 1; void stop(int) { running = 0; } }
int main(int argc, char** argv) {
    try {
        if (argc > 3) throw std::invalid_argument("usage: ChatServer [bind-ip] [port]");
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        ChatServer server({argc > 1 ? argv[1] : "127.0.0.1", argc > 2 ? chat::net::parsePort(argv[2]) : std::uint16_t{6000}});
        server.run(running);
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
