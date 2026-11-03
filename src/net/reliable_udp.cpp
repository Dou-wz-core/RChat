#include "net/reliable_udp.hpp"
#include <algorithm>
#include <chrono>
#include <limits>
#include <random>
#include <stdexcept>

namespace chat::net {

// 随机数生成器，返回非零的 64 位随机数；用于会话号和握手随机数。
std::uint64_t randomToken() {
    // 0 是协议中的无效标识，生成会话号和握手随机数时都要排除。
    std::random_device device;
    std::uint64_t result = 0;
    do { result = (static_cast<std::uint64_t>(device()) << 32) ^ device(); } while (!result);
    return result;
}

// 返回自单调时钟的毫秒数，避免系统时间调整导致的时间回退。
std::uint64_t monotonicMs() {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

// ReliableUdp 的实现类，持有状态机和回调；析构时关闭所有会话。
ReliableUdp::ReliableUdp(bool server, Send send, Callbacks callbacks, TransportConfig config)
    : server_(server), send_(std::move(send)), callbacks_(std::move(callbacks)), config_(config) {
    // 超时和容量配置必须可用，空闲超时还要给正常心跳留出响应时间。
    if (!send_ || !config_.initialRtoMs || config_.maxRtoMs < config_.initialRtoMs ||
        !config_.maxAttempts || !config_.heartbeatMs || config_.idleTimeoutMs <= config_.heartbeatMs ||
        !config_.maxSessions || !config_.maxQueuedMessages || !config_.maxPeerBytes || !config_.maxTotalBytes)
        throw std::invalid_argument("invalid UDP transport configuration");
}

// 发送数据报；发送失败时不抛异常，调用方可通过 tick() 检查超时和重传。
void ReliableUdp::transmit(const Endpoint& to, Packet p) { send_(to, encode(p)); }

// 建立新的会话
void ReliableUdp::connect(const Endpoint& server, std::uint64_t nonce, std::uint64_t now) {
    // 客户端一次只建立一个会话，立即执行 tick 发出第一份 HELLO。
    if (server_ || !nonce || connecting_ || !peers_.empty()) throw std::logic_error("invalid connect state");
    serverEndpoint_ = server; nonce_ = nonce; helloAttempts_ = 0; helloSent_ = now; connecting_ = true;
    tick(now);
}

// 关闭会话；如果是客户端主动关闭，则不再尝试重连。
std::uint64_t ReliableUdp::rto(unsigned attempts) const {
    // 从首次发送的初始间隔开始翻倍；先判断上限，避免乘法溢出。
    auto delay = config_.initialRtoMs;
    for (unsigned i = 1; i < attempts && delay < config_.maxRtoMs; ++i)
        delay = delay > config_.maxRtoMs / 2 ? config_.maxRtoMs : delay * 2;
    return delay;
}

// 发送 WELCOME 消息，携带客户端的随机数；客户端收到后确认握手。
void ReliableUdp::welcome(std::uint64_t id, const Peer& peer) {
    // 回传客户端的随机数，使客户端能确认这是本次握手的应答。
    Packet p; p.type = PacketType::Welcome; p.session = id; p.payload = encodeNonce(peer.nonce);
    transmit(peer.endpoint, std::move(p));
}

// 处理接收到的数据包
void ReliableUdp::input(const Endpoint& from, const Bytes& bytes, std::uint64_t now) {
    Packet p;
    // 先解码数据包；解码失败时不抛异常，直接丢弃该数据报。
    if (!decode(bytes, p)) return;

    // 处理握手消息；HELLO 可能因 WELCOME 丢失而重发，WELCOME 可能因 ACK 丢失而重发。
    if (p.type == PacketType::Hello) {
        if (!server_) return;
        auto nonce = decodeNonce(p.payload);
        // HELLO 可能因 WELCOME 丢失而重发，同一地址和随机数复用已有会话。
        for (const auto& item : peers_) {
            if (item.second.endpoint == from && item.second.nonce == nonce) {
                welcome(item.first, item.second); return;
            }
        }
        // 先检查会话上限和事件接纳结果，再创建会话，避免无法通知上层的连接。
        if (peers_.size() >= config_.maxSessions) return;
        auto id = randomToken();
        while (peers_.count(id)) id = randomToken();
        if (callbacks_.opened && !callbacks_.opened(id, from)) return;
        Peer peer; peer.endpoint = from; peer.nonce = nonce; peer.lastSeen = now; peer.lastPing = now;
        auto it = peers_.emplace(id, std::move(peer)).first;
        welcome(id, it->second);
        return;
    }
    // 处理握手响应；客户端只接受当前握手的 WELCOME，忽略旧握手或其他地址的应答。
    if (p.type == PacketType::Welcome) {
        // 仅接受目标服务端对当前握手的响应，忽略旧握手或其他地址的应答。
        if (server_ || !connecting_ || from != serverEndpoint_ || decodeNonce(p.payload) != nonce_) return;
        if (callbacks_.opened && !callbacks_.opened(p.session, from)) return;
        Peer peer; peer.endpoint = from; peer.nonce = nonce_; peer.lastSeen = now; peer.lastPing = now;
        peers_.emplace(p.session, std::move(peer)); connecting_ = false;
        return;
    }

    // 已建会话同时校验来源地址和会话号，通过后才刷新存活时间。
    auto it = peers_.find(p.session);
    if (it == peers_.end() || it->second.endpoint != from) return;
    auto& peer = it->second;
    peer.lastSeen = now;

    // 处理关闭、心跳和数据消息；ACK 只接受当前在途分片的确认，迟到或重复的确认不能推进发送窗口。
    if (p.type == PacketType::Close) { close(p.session, "peer closed", false); return; }
    if (p.type == PacketType::Ping) {
        Packet pong; pong.type = PacketType::Pong; pong.session = p.session; transmit(from, pong); return;
    }
    if (p.type == PacketType::Data) { receiveData(p.session, peer, p); return; }
    // 只接受当前在途分片的 ACK，迟到或重复的确认不能推进发送窗口。
    if (p.type != PacketType::Ack || peer.outbound.empty() || !peer.attempts || p.ack != peer.txSequence) return;
    if (peer.txSequence == std::numeric_limits<std::uint32_t>::max()) { close(p.session, "sequence exhausted"); return; }
    
    // 序号不允许回绕；确认后推进到下一片，并重新开始计算重传次数。
    ++peer.txSequence; peer.attempts = 0; peer.nextSend = now + config_.pacingMs;
    auto& message = peer.outbound.front();
    ++message.fragment;
    if (static_cast<std::size_t>(message.fragment) * MaxPayload >= message.body.size()) {
        // 末片获确认后才释放整条消息的容量，并通知上层传输完成。
        auto id = message.id;
        totalBytes_ -= message.body.size(); peer.queuedBytes -= message.body.size();
        peer.outbound.pop_front();
        if (callbacks_.delivered) callbacks_.delivered(p.session, id, true);
    }
}

// 处理接收到的数据分片；按序号和分片号组装完整消息，末片确认后投递给上层。
void ReliableUdp::receiveData(std::uint64_t id, Peer& peer, const Packet& p) {
    auto acknowledge = [&] {
        Packet ack; ack.type = PacketType::Ack; ack.session = id; ack.ack = p.sequence;
        transmit(peer.endpoint, ack);
    };
    // 已接收的分片只补发 ACK，不再拼接或投递；未来序号则等待发送方重传。
    if (p.sequence < peer.rxSequence) { acknowledge(); return; }
    if (p.sequence != peer.rxSequence || p.message != peer.rxMessage || p.fragment != peer.nextFragment) return;
    if (peer.rxSequence == std::numeric_limits<std::uint32_t>::max() ||
        peer.rxMessage == std::numeric_limits<std::uint32_t>::max()) {
        close(id, "receive sequence exhausted"); return;
    }
    // 后续分片的总片数和 replyTo 必须与首片一致，防止不同消息被混合重组。
    if (p.fragment == 0) { peer.fragments = p.fragments; peer.replyTo = p.replyTo; }
    if (p.fragments != peer.fragments || p.replyTo != peer.replyTo ||
        peer.assembly.size() + p.payload.size() > MaxMessage) return;
    const bool last = p.fragment + 1 == p.fragments;
    if (last) {
        // 先用副本拼出完整消息；上层队列拒收时保留原组装状态和期望序号。
        // 不确认末片会促使发送方重试，队列有空间后仍能正确完成本条消息。
        ReceivedMessage message{p.message, p.replyTo, peer.assembly};
        message.body.append(p.payload.begin(), p.payload.end());
        if (callbacks_.received && !callbacks_.received(id, std::move(message))) return;
        peer.assembly.clear(); peer.nextFragment = 0; peer.fragments = 0; ++peer.rxMessage;
    } else {
        // 中间片只进入组装缓冲，收到末片之前不会生成业务消息。
        peer.assembly.append(p.payload.begin(), p.payload.end()); ++peer.nextFragment;
    }
    ++peer.rxSequence;
    acknowledge();
}

// 将消息入队并登记完成回调；返回值为逻辑消息编号，0 表示入队失败。
std::uint32_t ReliableUdp::enqueue(std::uint64_t id, std::string body, std::uint32_t replyTo) {
    auto it = peers_.find(id);
    if (it == peers_.end() || body.empty() || body.size() > MaxMessage) return 0;
    auto& peer = it->second;
    // 同时限制消息数和字节数；减去已用量前取最小值，避免无符号减法下溢。
    if (peer.outbound.size() >= config_.maxQueuedMessages ||
        body.size() > config_.maxPeerBytes - std::min(peer.queuedBytes, config_.maxPeerBytes) ||
        body.size() > config_.maxTotalBytes - std::min(totalBytes_, config_.maxTotalBytes) ||
        peer.nextMessage == std::numeric_limits<std::uint32_t>::max()) return 0;
    auto message = peer.nextMessage++;
    peer.queuedBytes += body.size(); totalBytes_ += body.size();
    peer.outbound.push_back({message, replyTo, std::move(body)});
    return message;
}

// 处理定时器事件；握手、心跳和重传都在 tick 中处理。
void ReliableUdp::tick(std::uint64_t now) {
    // 握手和数据使用相同的退避策略，达到最大尝试次数后报告失败。
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
        // close 会删除当前会话，先推进迭代器，保证后续遍历仍然有效。
        auto id = it->first; auto& peer = it->second; ++it;
        if (now - peer.lastSeen >= config_.idleTimeoutMs) { close(id, "heartbeat timeout"); continue; }
        if (now - peer.lastPing >= config_.heartbeatMs) {
            Packet p; p.type = PacketType::Ping; p.session = id;
            transmit(peer.endpoint, p); peer.lastPing = now;
        }
        // 等待发送间隔或本片的重传期限，不在每次 tick 中重复发包。
        if (peer.outbound.empty() || now < peer.nextSend ||
            (peer.attempts && now - peer.sentAt < rto(peer.attempts))) continue;
        if (peer.attempts >= config_.maxAttempts) { close(id, "retransmission limit"); continue; }
        // 每次只构造队首消息的当前分片；重传继续使用相同的分片序号。
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

// 关闭会话；如果是客户端主动关闭，则不再尝试重连。
void ReliableUdp::close(std::uint64_t id, const std::string& reason, bool notifyPeer) {
    auto it = peers_.find(id);
    if (it == peers_.end()) return;
    if (notifyPeer) { Packet p; p.type = PacketType::Close; p.session = id; transmit(it->second.endpoint, p); }
    // 先报告所有待发消息失败，再释放会话；上层可据此保存未交付的聊天消息。
    for (const auto& message : it->second.outbound)
        if (callbacks_.delivered) callbacks_.delivered(id, message.id, false);
    totalBytes_ -= it->second.queuedBytes; peers_.erase(it);
    if (callbacks_.closed) callbacks_.closed(id, reason);
}

// 关闭所有会话；客户端主动关闭时不再尝试重连。
void ReliableUdp::closeAll() {
    connecting_ = false;
    while (!peers_.empty()) close(peers_.begin()->first, "local shutdown");
}

} // chat::net 命名空间
