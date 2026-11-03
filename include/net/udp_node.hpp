#pragma once

#include "net/connection.hpp"
#include "net/reliable_udp.hpp"
#include <memory>

namespace chat::net {
enum class EventType { Opened, Message, Closed, Delivered };
// 网络层向消费线程交接的事件；Delivered 携带待执行的完成回调和结果。
struct Event {
    EventType type = EventType::Opened;
    ConnectionPtr connection;
    ReceivedMessage message;
    std::string reason;
    DeliveryCallback completion;
    bool success = false;
};

// 一个线程调用 poll 维护收发和定时器，另一个线程消费事件并调用 send。
// 状态机访问由互斥锁保护，业务回调交给事件消费线程执行。
class UdpNode {
public:
    UdpNode(bool server, Endpoint local, TransportConfig config = {});
    ~UdpNode();
    UdpNode(const UdpNode&) = delete;
    UdpNode& operator=(const UdpNode&) = delete;
    Endpoint localEndpoint() const;
    void connect(const Endpoint& server);
    void poll(unsigned timeoutMs = 10);
    // 取出最早的事件；等待超时或停止后队列为空时返回 false。
    bool nextEvent(Event& event, unsigned timeoutMs = 0);
    void stop(); // 关闭会话并唤醒等待者，已排队事件仍可继续取出。
    bool stopped() const;
private:
    struct State;
    std::shared_ptr<State> state_;
};
} // chat::net 命名空间
