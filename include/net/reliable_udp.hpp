#pragma once

#include "net/protocol.hpp"
#include <deque>
#include <functional>
#include <map>
#include <string>

namespace chat::net {
struct Endpoint {
    std::string address = "127.0.0.1";
    std::uint16_t port = 0;
    bool operator==(const Endpoint& other) const { return address == other.address && port == other.port; }
    bool operator!=(const Endpoint& other) const { return !(*this == other); }
    std::string text() const { return address + ":" + std::to_string(port); }
};
struct TransportConfig {
    std::uint64_t initialRtoMs = 250;
    std::uint64_t maxRtoMs = 2000;
    unsigned maxAttempts = 7;
    std::uint64_t heartbeatMs = 5000;
    std::uint64_t idleTimeoutMs = 20000;
    std::uint64_t pacingMs = 2;
    std::size_t maxSessions = 256;
    std::size_t maxQueuedMessages = 32;
    std::size_t maxPeerBytes = 256 * 1024;
    std::size_t maxTotalBytes = 4 * 1024 * 1024;
};
struct ReceivedMessage {
    std::uint32_t id = 0;
    std::uint32_t replyTo = 0;
    std::string body;
};

// Single-owner, deterministic state machine. Callbacks must not reenter it.
// Network I/O is injected; tests can drop, reorder or duplicate datagrams.
class ReliableUdp {
public:
    using Send = std::function<void(const Endpoint&, const Bytes&)>;
    struct Callbacks {
        std::function<bool(std::uint64_t, const Endpoint&)> opened;
        std::function<bool(std::uint64_t, ReceivedMessage)> received;
        std::function<void(std::uint64_t, const std::string&)> closed;
        std::function<void(std::uint64_t, std::uint32_t, bool)> delivered;
    };
    ReliableUdp(bool server, Send send, Callbacks callbacks, TransportConfig config = {});
    void connect(const Endpoint& server, std::uint64_t nonce, std::uint64_t now);
    void input(const Endpoint& from, const Bytes& bytes, std::uint64_t now);
    void tick(std::uint64_t now);
    std::uint32_t enqueue(std::uint64_t session, std::string body, std::uint32_t replyTo = 0);
    void close(std::uint64_t session, const std::string& reason, bool notifyPeer = true);
    void closeAll();
    std::size_t sessionCount() const { return peers_.size(); }
    std::size_t queuedBytes() const { return totalBytes_; }
private:
    struct Outbound {
        std::uint32_t id;
        std::uint32_t replyTo;
        std::string body;
        std::uint16_t fragment = 0;
    };
    struct Peer {
        Endpoint endpoint;
        std::uint64_t nonce = 0, lastSeen = 0, lastPing = 0, nextSend = 0, sentAt = 0;
        std::uint32_t txSequence = 1, nextMessage = 1, rxSequence = 1, rxMessage = 1;
        std::size_t queuedBytes = 0;
        unsigned attempts = 0;
        std::deque<Outbound> outbound;
        std::string assembly;
        std::uint16_t nextFragment = 0, fragments = 0;
        std::uint32_t replyTo = 0;
    };
    void transmit(const Endpoint& to, Packet packet);
    void welcome(std::uint64_t id, const Peer& peer);
    void receiveData(std::uint64_t id, Peer& peer, const Packet& packet);
    std::uint64_t rto(unsigned attempts) const;
    bool server_;
    Send send_;
    Callbacks callbacks_;
    TransportConfig config_;
    std::map<std::uint64_t, Peer> peers_;
    std::size_t totalBytes_ = 0;
    Endpoint serverEndpoint_;
    std::uint64_t nonce_ = 0, helloSent_ = 0;
    unsigned helloAttempts_ = 0;
    bool connecting_ = false;
};
std::uint64_t randomToken();
std::uint64_t monotonicMs();
} // namespace chat::net
