#include "net/udp_socket.hpp"
#include <array>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <cerrno>
#include <fcntl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace chat::net {
namespace {

// 解析数字形式的 IPv4 地址，端口转成网络字节序；失败抛异常。
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket Invalid = INVALID_SOCKET;
int lastError() { return WSAGetLastError(); }
void closeSocket(Socket fd) { closesocket(fd); }

struct SocketRuntime {
    // WinSock 使用前初始化，套接字释放后再清理运行环境。
    SocketRuntime() { WSADATA data{}; int e = WSAStartup(MAKEWORD(2, 2), &data); if (e) throw std::system_error(e, std::system_category(), "WSAStartup"); }
    ~SocketRuntime() { WSACleanup(); }
};
#else
using Socket = int;
constexpr Socket Invalid = -1;
int lastError() { return errno; }
void closeSocket(Socket fd) { ::close(fd); }
#endif

// address() 和 endpoint() 互为逆运算，保证端点信息在 UDP 层和协议层之间正确传递。
sockaddr_in address(const Endpoint& endpoint) {
    // 只接受数字形式的 IPv4 地址，端口转成网络字节序。
    sockaddr_in out{}; out.sin_family = AF_INET; out.sin_port = htons(endpoint.port);
    if (inet_pton(AF_INET, endpoint.address.c_str(), &out.sin_addr) != 1)
        throw std::invalid_argument("expected numeric IPv4 address: " + endpoint.address);
    return out;
}
Endpoint endpoint(const sockaddr_in& addr) {
    char text[INET_ADDRSTRLEN]{};
    if (!inet_ntop(AF_INET, &addr.sin_addr, text, sizeof(text)))
        throw std::system_error(lastError(), std::system_category(), "inet_ntop");
    return {text, ntohs(addr.sin_port)};
}
}

// UdpSocket 的实现类，持有套接字和接收缓冲；析构时关闭套接字。
struct UdpSocket::Impl {
#ifdef _WIN32
    SocketRuntime runtime;
#endif
    Socket fd = Invalid;
    // 使用能容纳 UDP 数据报的缓冲；协议的 1200 字节上限在解码阶段检查。
    std::array<std::uint8_t, 65536> buffer{};
    ~Impl() { if (fd != Invalid) closeSocket(fd); }
};

// 构造函数创建套接字、绑定本地地址并设置非阻塞模式；失败抛异常。
UdpSocket::UdpSocket(const Endpoint& local) : impl_(std::make_unique<Impl>()) {
    auto addr = address(local);
    impl_->fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->fd == Invalid) throw std::system_error(lastError(), std::system_category(), "socket");
    // 非阻塞模式使一次可读通知后的连续接收能在队列读空时立即返回。
#ifdef _WIN32
    u_long nonblocking = 1;
    if (ioctlsocket(impl_->fd, FIONBIO, &nonblocking))
#else
    int flags = fcntl(impl_->fd, F_GETFL, 0);
    if (flags < 0 || fcntl(impl_->fd, F_SETFL, flags | O_NONBLOCK) < 0)
#endif
        throw std::system_error(lastError(), std::system_category(), "nonblocking socket");
    if (::bind(impl_->fd, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr)) != 0)
        throw std::system_error(lastError(), std::system_category(), "bind " + local.text());
#ifndef _WIN32
    // POSIX select 使用固定大小的描述符集合，超出范围会导致 FD_SET 越界。
    if (impl_->fd >= FD_SETSIZE) throw std::runtime_error("socket exceeds select FD_SETSIZE");
#endif
}

// 析构函数关闭套接字，释放缓冲区；异常安全。
UdpSocket::~UdpSocket() = default;

// 返回本地端点。
Endpoint UdpSocket::localEndpoint() const {
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
#else
    socklen_t len = sizeof(addr);
#endif
    if (getsockname(impl_->fd, reinterpret_cast<sockaddr*>(&addr), &len) != 0)
        throw std::system_error(lastError(), std::system_category(), "getsockname");
    return endpoint(addr);
}

// waitReadable() 等待套接字可读；超时返回 false，异常抛出。
bool UdpSocket::waitReadable(unsigned timeoutMs) {
    // Windows 忽略 select 的首个参数，POSIX 要求传入最大描述符加一。
    fd_set set; FD_ZERO(&set); FD_SET(impl_->fd, &set);
    timeval timeout{}; timeout.tv_sec = timeoutMs / 1000; timeout.tv_usec = (timeoutMs % 1000) * 1000;
#ifdef _WIN32
    int count = select(0, &set, nullptr, nullptr, &timeout);
    if (count < 0 && lastError() == WSAEINTR) return false;
#else
    int count = select(impl_->fd + 1, &set, nullptr, nullptr, &timeout);
    if (count < 0 && errno == EINTR) return false;
#endif
    if (count < 0) throw std::system_error(lastError(), std::system_category(), "select");
    return count > 0;
}

// receive() 接收一个完整的 UDP 数据报；零字节数据报仍是一次接收，不表示连接断开。
bool UdpSocket::receive(Endpoint& from, Bytes& bytes) {
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
    int size = recvfrom(impl_->fd, reinterpret_cast<char*>(impl_->buffer.data()), static_cast<int>(impl_->buffer.size()), 0,
                        reinterpret_cast<sockaddr*>(&addr), &len);
    if (size < 0) {
        auto e = lastError();
        // 暂时无数据、中断或 UDP 的 ICMP 错误都不表示逻辑会话已经关闭。
        if (e == WSAEWOULDBLOCK || e == WSAEINTR || e == WSAECONNRESET) return false;
        // 截断报文已被读走，交给协议层作为空报文丢弃，继续处理后续数据。
        if (e == WSAEMSGSIZE) { bytes.clear(); from = endpoint(addr); return true; }
        throw std::system_error(e, std::system_category(), "recvfrom");
    }
#else
    socklen_t len = sizeof(addr);
    auto size = recvfrom(impl_->fd, impl_->buffer.data(), impl_->buffer.size(), 0, reinterpret_cast<sockaddr*>(&addr), &len);
    if (size < 0) {
        if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR || errno == ECONNREFUSED) return false;
        throw std::system_error(errno, std::system_category(), "recvfrom");
    }
#endif
    from = endpoint(addr);
    bytes.assign(impl_->buffer.begin(), impl_->buffer.begin() + size);
    return true; // 零字节 UDP 数据报仍是一次接收，不表示连接断开。
}

// send() 发送一个完整的 UDP 数据报；失败返回 false，异常抛出。
bool UdpSocket::send(const Endpoint& to, const Bytes& bytes) {
    // 一次 sendto 发送一个完整数据报；未成功提交时由上层确认超时触发重传。
    if (bytes.size() > MaxDatagram) return false;
    auto addr = address(to);
#ifdef _WIN32
    int size = sendto(impl_->fd, reinterpret_cast<const char*>(bytes.data()), static_cast<int>(bytes.size()), 0,
                      reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
#else
    auto size = sendto(impl_->fd, bytes.data(), bytes.size(), 0, reinterpret_cast<const sockaddr*>(&addr), sizeof(addr));
#endif
    return size >= 0 && static_cast<std::size_t>(size) == bytes.size();
}
} // chat::net 命名空间
