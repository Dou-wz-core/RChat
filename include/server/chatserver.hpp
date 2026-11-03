#pragma once
#include "net/udp_node.hpp"
#include <csignal>

// 接收完整的传输消息，校验 JSON 字段和会话身份，再按 msgid 分发业务。
// 网络线程维护套接字和定时器，事件消费线程串行执行聊天业务与数据库操作。
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
