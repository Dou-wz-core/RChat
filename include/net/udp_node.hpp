#pragma once

#include "net/connection.hpp"
#include "net/reliable_udp.hpp"
#include <memory>

namespace chat::net {
enum class EventType { Opened, Message, Closed, Delivered };
struct Event {
    EventType type = EventType::Opened;
    ConnectionPtr connection;
    ReceivedMessage message;
    std::string reason;
    DeliveryCallback completion;
    bool success = false;
};

// One thread calls poll(); another consumes events and calls Connection::send().
// All engine state is protected. No user callback runs in the network thread.
class UdpNode {
public:
    UdpNode(bool server, Endpoint local, TransportConfig config = {});
    ~UdpNode();
    UdpNode(const UdpNode&) = delete;
    UdpNode& operator=(const UdpNode&) = delete;
    Endpoint localEndpoint() const;
    void connect(const Endpoint& server);
    void poll(unsigned timeoutMs = 10);
    bool nextEvent(Event& event, unsigned timeoutMs = 0);
    void stop();
    bool stopped() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
} // namespace chat::net
