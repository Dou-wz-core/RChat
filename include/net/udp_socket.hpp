#pragma once
#include "net/reliable_udp.hpp"
#include <memory>

namespace chat::net {
class UdpSocket {
public:
    explicit UdpSocket(const Endpoint& local);
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    Endpoint localEndpoint() const;
    bool waitReadable(unsigned timeoutMs);
    bool receive(Endpoint& from, Bytes& bytes);
    bool send(const Endpoint& to, const Bytes& bytes);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace chat::net
