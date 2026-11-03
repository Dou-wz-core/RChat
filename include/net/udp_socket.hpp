#pragma once
#include "net/reliable_udp.hpp"
#include <memory>

namespace chat::net {
// 管理一个非阻塞 IPv4 UDP 套接字，析构时关闭；底层平台类型封装在 Impl 中。
class UdpSocket {
public:
    explicit UdpSocket(const Endpoint& local);
    ~UdpSocket();
    UdpSocket(const UdpSocket&) = delete;
    UdpSocket& operator=(const UdpSocket&) = delete;
    // 绑定端口为 0 时，返回操作系统实际分配的端口。
    Endpoint localEndpoint() const;
    // 最多等待 timeoutMs 毫秒，超时或等待被中断时返回 false。
    bool waitReadable(unsigned timeoutMs);
    // 无可读数据时返回 false；成功读取空数据报时仍返回 true。
    bool receive(Endpoint& from, Bytes& bytes);
    bool send(const Endpoint& to, const Bytes& bytes);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // chat::net 命名空间
