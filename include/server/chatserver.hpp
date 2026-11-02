#pragma once
#include "net/udp_node.hpp"
#include <csignal>

// UDP transport owns sockets/timers. This adapter only validates and dispatches JSON.
class ChatServer {
public:
    explicit ChatServer(chat::net::Endpoint local);
    void run(volatile std::sig_atomic_t& running);
    void stop() { node_.stop(); }
    chat::net::Endpoint localEndpoint() const { return node_.localEndpoint(); }
private:
    void onMessage(const chat::net::Event& event);
    void handle(chat::net::Event& event);
    chat::net::UdpNode node_;
};
