#include "net/udp_node.hpp"
#include "net/cli.hpp"
#include <csignal>
#include <iostream>
namespace { volatile std::sig_atomic_t running = 1; void stop(int) { running = 0; } }
int main(int argc, char** argv) {
    try {
        if (argc > 3) throw std::invalid_argument("usage: UdpEchoServer [bind-ip] [port]");
        chat::net::UdpNode server(true, {argc > 1 ? argv[1] : "127.0.0.1", argc > 2 ? chat::net::parsePort(argv[2]) : std::uint16_t{6000}});
        std::signal(SIGINT, stop); std::signal(SIGTERM, stop);
        std::cout << "UDP echo listening " << server.localEndpoint().text() << std::endl;
        while (running) {
            server.poll(); chat::net::Event event;
            while (server.nextEvent(event)) {
                if (event.type == chat::net::EventType::Message && !event.connection->send(event.message.body, {}, event.message.id))
                    std::cerr << "echo send queue full\n";
            }
        }
        server.stop();
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
