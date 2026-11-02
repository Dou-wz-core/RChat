#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace chat::net {
using Timestamp = std::chrono::system_clock::time_point;
using DeliveryCallback = std::function<void(bool)>;
struct RequestContext {
    std::uint32_t messageId = 0;
    Timestamp receivedAt = std::chrono::system_clock::now();
};

// Business code sees an authenticated logical peer, never a TCP/UDP socket.
class Connection {
public:
    virtual ~Connection() = default;
    virtual std::uint64_t id() const noexcept = 0;
    virtual bool alive() const noexcept = 0;
    // true means admitted to a bounded send queue. Callback reports transport ACK/failure.
    virtual bool send(std::string message, DeliveryCallback delivered = {},
                      std::uint32_t replyTo = 0) = 0;
    int userId() const noexcept { return userId_.load(); }
    void bindUser(int id) noexcept { userId_.store(id); }
private:
    std::atomic<int> userId_{-1};
};
using ConnectionPtr = std::shared_ptr<Connection>;
} // namespace chat::net
