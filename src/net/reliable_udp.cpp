#include "net/reliable_udp.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <stdexcept>

namespace chat::net {
std::uint64_t randomToken() {
    std::random_device device;
    std::uint64_t result = 0;
    do { result = (static_cast<std::uint64_t>(device()) << 32) ^ device(); } while (!result);
    return result;
}
std::uint64_t monotonicMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
ReliableUdp::ReliableUdp(bool server, Send send, Callbacks callbacks, TransportConfig config)
    : server_(server), send_(std::move(send)), callbacks_(std::move(callbacks)), config_(config) {
    if (!send_ || !config_.initialRtoMs || config_.maxRtoMs < config_.initialRtoMs ||
        !config_.maxAttempts || !config_.heartbeatMs || config_.idleTimeoutMs <= config_.heartbeatMs ||
        !config_.maxSessions || !config_.maxQueuedMessages || !config_.maxPeerBytes || !config_.maxTotalBytes)
        throw std::invalid_argument("invalid UDP transport configuration");
}
void ReliableUdp::transmit(const Endpoint& to, Packet p) { send_(to, encode(p)); }
void ReliableUdp::connect(const Endpoint& server, std::uint64_t nonce, std::uint64_t now) {
    if (server_ || !nonce || connecting_ || !peers_.empty()) throw std::logic_error("invalid connect state");
    serverEndpoint_ = server; nonce_ = nonce; helloAttempts_ = 0; helloSent_ = now; connecting_ = true;
    tick(now);
}
std::uint64_t ReliableUdp::rto(unsigned attempts) const {
    auto delay = config_.initialRtoMs;
    for (unsigned i = 1; i < attempts && delay < config_.maxRtoMs; ++i)
        delay = delay > config_.maxRtoMs / 2 ? config_.maxRtoMs : delay * 2;
    return delay;
}
void ReliableUdp::welcome(std::uint64_t id, const Peer& peer) {
    Packet p; p.type = PacketType::Welcome; p.session = id; p.payload = encodeNonce(peer.nonce);
    transmit(peer.endpoint, std::move(p));
}
void ReliableUdp::input(const Endpoint& from, const Bytes& bytes, std::uint64_t now) {
    Packet p;
    if (!decode(bytes, p)) return;
    if (p.type == PacketType::Hello) {
        if (!server_) return;
        auto nonce = decodeNonce(p.payload);
        for (const auto& item : peers_) {
            if (item.second.endpoint == from && item.second.nonce == nonce) {
                welcome(item.first, item.second); return;
            }
        }
        if (peers_.size() >= config_.maxSessions) return;
        auto id = randomToken();
        while (peers_.count(id)) id = randomToken();
        if (callbacks_.opened && !callbacks_.opened(id, from)) return;
        Peer peer; peer.endpoint = from; peer.nonce = nonce; peer.lastSeen = now; peer.lastPing = now;
        auto it = peers_.emplace(id, std::move(peer)).first;
        welcome(id, it->second);
        return;
    }
    if (p.type == PacketType::Welcome) {
        if (server_ || !connecting_ || from != serverEndpoint_ || decodeNonce(p.payload) != nonce_) return;
        if (callbacks_.opened && !callbacks_.opened(p.session, from)) return;
        Peer peer; peer.endpoint = from; peer.nonce = nonce_; peer.lastSeen = now; peer.lastPing = now;
        peers_.emplace(p.session, std::move(peer)); connecting_ = false;
        return;
    }
    auto it = peers_.find(p.session);
    if (it == peers_.end() || it->second.endpoint != from) return;
    auto& peer = it->second;
    peer.lastSeen = now;
    if (p.type == PacketType::Close) { close(p.session, "peer closed", false); return; }
    if (p.type == PacketType::Ping) {
        Packet pong; pong.type = PacketType::Pong; pong.session = p.session; transmit(from, pong); return;
    }
    if (p.type == PacketType::Data) { receiveData(p.session, peer, p); return; }
    if (p.type != PacketType::Ack || peer.outbound.empty() || !peer.attempts || p.ack != peer.txSequence) return;
    if (peer.txSequence == std::numeric_limits<std::uint32_t>::max()) { close(p.session, "sequence exhausted"); return; }
    ++peer.txSequence; peer.attempts = 0; peer.nextSend = now + config_.pacingMs;
    auto& message = peer.outbound.front();
    ++message.fragment;
    if (static_cast<std::size_t>(message.fragment) * MaxPayload >= message.body.size()) {
        auto id = message.id;
        totalBytes_ -= message.body.size(); peer.queuedBytes -= message.body.size();
        peer.outbound.pop_front();
        if (callbacks_.delivered) callbacks_.delivered(p.session, id, true);
    }
}
void ReliableUdp::receiveData(std::uint64_t id, Peer& peer, const Packet& p) {
    auto acknowledge = [&] {
        Packet ack; ack.type = PacketType::Ack; ack.session = id; ack.ack = p.sequence;
        transmit(peer.endpoint, ack);
    };
    if (p.sequence < peer.rxSequence) { acknowledge(); return; }
    if (p.sequence != peer.rxSequence || p.message != peer.rxMessage || p.fragment != peer.nextFragment) return;
    if (peer.rxSequence == std::numeric_limits<std::uint32_t>::max() ||
        peer.rxMessage == std::numeric_limits<std::uint32_t>::max()) {
        close(id, "receive sequence exhausted"); return;
    }
    if (p.fragment == 0) { peer.fragments = p.fragments; peer.replyTo = p.replyTo; }
    if (p.fragments != peer.fragments || p.replyTo != peer.replyTo ||
        peer.assembly.size() + p.payload.size() > MaxMessage) return;
    const bool last = p.fragment + 1 == p.fragments;
    if (last) {
        ReceivedMessage message{p.message, p.replyTo, peer.assembly};
        message.body.append(p.payload.begin(), p.payload.end());
        if (callbacks_.received && !callbacks_.received(id, std::move(message))) return;
        peer.assembly.clear(); peer.nextFragment = 0; peer.fragments = 0; ++peer.rxMessage;
    } else {
        peer.assembly.append(p.payload.begin(), p.payload.end()); ++peer.nextFragment;
    }
    ++peer.rxSequence;
    acknowledge();
}
std::uint32_t ReliableUdp::enqueue(std::uint64_t id, std::string body, std::uint32_t replyTo) {
    auto it = peers_.find(id);
    if (it == peers_.end() || body.empty() || body.size() > MaxMessage) return 0;
    auto& peer = it->second;
    if (peer.outbound.size() >= config_.maxQueuedMessages ||
        body.size() > config_.maxPeerBytes - std::min(peer.queuedBytes, config_.maxPeerBytes) ||
        body.size() > config_.maxTotalBytes - std::min(totalBytes_, config_.maxTotalBytes) ||
        peer.nextMessage == std::numeric_limits<std::uint32_t>::max()) return 0;
    auto message = peer.nextMessage++;
    peer.queuedBytes += body.size(); totalBytes_ += body.size();
    peer.outbound.push_back({message, replyTo, std::move(body)});
    return message;
}
void ReliableUdp::tick(std::uint64_t now) {
    if (connecting_ && (!helloAttempts_ || now - helloSent_ >= rto(helloAttempts_))) {
        if (helloAttempts_ >= config_.maxAttempts) {
            connecting_ = false;
            if (callbacks_.closed) callbacks_.closed(0, "handshake timeout");
        } else {
            Packet p; p.type = PacketType::Hello; p.payload = encodeNonce(nonce_);
            transmit(serverEndpoint_, std::move(p)); helloSent_ = now; ++helloAttempts_;
        }
    }
    for (auto it = peers_.begin(); it != peers_.end();) {
        auto id = it->first; auto& peer = it->second; ++it;
        if (now - peer.lastSeen >= config_.idleTimeoutMs) { close(id, "heartbeat timeout"); continue; }
        if (now - peer.lastPing >= config_.heartbeatMs) {
            Packet p; p.type = PacketType::Ping; p.session = id;
            transmit(peer.endpoint, p); peer.lastPing = now;
        }
        if (peer.outbound.empty() || now < peer.nextSend ||
            (peer.attempts && now - peer.sentAt < rto(peer.attempts))) continue;
        if (peer.attempts >= config_.maxAttempts) { close(id, "retransmission limit"); continue; }
        const auto& message = peer.outbound.front();
        const auto offset = static_cast<std::size_t>(message.fragment) * MaxPayload;
        const auto count = std::min(MaxPayload, message.body.size() - offset);
        Packet p; p.session = id; p.sequence = peer.txSequence;
        p.message = message.id; p.replyTo = message.replyTo; p.fragment = message.fragment;
        p.fragments = static_cast<std::uint16_t>((message.body.size() + MaxPayload - 1) / MaxPayload);
        p.payload.assign(message.body.begin() + offset, message.body.begin() + offset + count);
        transmit(peer.endpoint, std::move(p)); peer.sentAt = now; ++peer.attempts;
    }
}
void ReliableUdp::close(std::uint64_t id, const std::string& reason, bool notifyPeer) {
    auto it = peers_.find(id);
    if (it == peers_.end()) return;
    if (notifyPeer) { Packet p; p.type = PacketType::Close; p.session = id; transmit(it->second.endpoint, p); }
    for (const auto& message : it->second.outbound)
        if (callbacks_.delivered) callbacks_.delivered(id, message.id, false);
    totalBytes_ -= it->second.queuedBytes; peers_.erase(it);
    if (callbacks_.closed) callbacks_.closed(id, reason);
}
void ReliableUdp::closeAll() {
    connecting_ = false;
    while (!peers_.empty()) close(peers_.begin()->first, "local shutdown");
}
} // namespace chat::net
