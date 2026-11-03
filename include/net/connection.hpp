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
// 请求上下文记录传输消息号，业务响应据此填写 replyTo，与 JSON 的 msgid 区分。
struct RequestContext {
    std::uint32_t messageId = 0;
    Timestamp receivedAt = std::chrono::system_clock::now();
};

// 业务层通过逻辑会话收发消息；用户身份在登录成功后绑定到该会话。
class Connection {
public:
    virtual ~Connection() = default;
    virtual std::uint64_t id() const noexcept = 0;
    virtual bool alive() const noexcept = 0;
    // 返回 true 表示进入有界发送队列；回调报告全部分片获确认或发送失败。
    // 传输确认表示消息已进入对端接收队列，不代表业务处理完成或用户已读。
    virtual bool send(std::string message, DeliveryCallback delivered = {},
                      std::uint32_t replyTo = 0) = 0;
    // -1 表示尚未登录；原子访问允许网络线程和业务线程安全读取身份值。
    int userId() const noexcept { return userId_.load(); }
    void bindUser(int id) noexcept { userId_.store(id); }
private:
    std::atomic<int> userId_{-1};
};
using ConnectionPtr = std::shared_ptr<Connection>;
} // chat::net 命名空间
