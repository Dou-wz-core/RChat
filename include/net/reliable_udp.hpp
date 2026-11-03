#pragma once

#include "net/protocol.hpp"
#include <deque>
#include <functional>
#include <map>
#include <string>

namespace chat::net {
// 会话绑定到 IPv4 地址和端口；后续报文必须同时匹配地址与会话标识。
struct Endpoint {
    std::string address = "127.0.0.1";
    std::uint16_t port = 0;
    bool operator==(const Endpoint& other) const { return address == other.address && port == other.port; }
    bool operator!=(const Endpoint& other) const { return !(*this == other); }
    std::string text() const { return address + ":" + std::to_string(port); }
};
struct TransportConfig {
    // 重传间隔指数退避到 maxRtoMs；maxAttempts 包含首次发送。
    std::uint64_t initialRtoMs = 250;
    std::uint64_t maxRtoMs = 2000;
    unsigned maxAttempts = 7;
    // 定期发心跳；超过 idleTimeoutMs 没收到该会话的有效报文则关闭。
    std::uint64_t heartbeatMs = 5000;
    std::uint64_t idleTimeoutMs = 20000;
    std::uint64_t pacingMs = 2; // 收到 ACK 后发送下一分片前的最短间隔。
    // 分别约束会话数、每个会话的待发消息数，以及单会话和全局待发字节数。
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

// 可靠传输状态机由同一调用方串行驱动，回调执行期间不能再次调用状态机。
// 发送函数和时间由外部传入，既可接真实套接字，也可模拟丢包、乱序和重复。
class ReliableUdp {
public:
    using Send = std::function<void(const Endpoint&, const Bytes&)>;
    struct Callbacks {
        // 返回 false 时拒绝建立会话，不向对端发送握手成功应答。
        std::function<bool(std::uint64_t, const Endpoint&)> opened;
        // 返回 false 时保留接收状态并暂缓确认末片，等待发送方重试。
        std::function<bool(std::uint64_t, ReceivedMessage)> received;
        std::function<void(std::uint64_t, const std::string&)> closed;
        // 每条已入队消息在全部分片获确认或会话关闭时报告一次结果。
        std::function<void(std::uint64_t, std::uint32_t, bool)> delivered;
    };
    ReliableUdp(bool server, Send send, Callbacks callbacks, TransportConfig config = {});
    void connect(const Endpoint& server, std::uint64_t nonce, std::uint64_t now);
    void input(const Endpoint& from, const Bytes& bytes, std::uint64_t now);
    void tick(std::uint64_t now); // now 使用单调递增的毫秒时间。
    // 返回当前方向的消息编号；会话无效或超过资源限制时返回 0。
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
        std::uint16_t fragment = 0; // 当前等待发送或等待确认的分片下标。
    };
    struct Peer {
        Endpoint endpoint;
        // 记录最近接收、心跳发送、下一片最早发送和本片最近发送的时间。
        std::uint64_t nonce = 0, lastSeen = 0, lastPing = 0, nextSend = 0, sentAt = 0;
        // 两个方向各自编号；接收端只接纳恰好等于期望值的分片和消息。
        std::uint32_t txSequence = 1, nextMessage = 1, rxSequence = 1, rxMessage = 1;
        std::size_t queuedBytes = 0;
        unsigned attempts = 0;
        // 停等窗口为 1，只有队首消息的一个分片可以处于等待 ACK 的状态。
        std::deque<Outbound> outbound;
        // 接收端逐片拼接当前消息，并保存首片声明的总片数和请求关联号。
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
    // 客户端握手状态独立于已建立的会话，nonce 用于匹配 WELCOME。
    Endpoint serverEndpoint_;
    std::uint64_t nonce_ = 0, helloSent_ = 0;
    unsigned helloAttempts_ = 0;
    bool connecting_ = false;
};
std::uint64_t randomToken();
// 超时与退避使用单调时钟，不受系统时间调整影响。
std::uint64_t monotonicMs();
} // chat::net 命名空间
