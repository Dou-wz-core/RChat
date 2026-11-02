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
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket Invalid = INVALID_SOCKET;
int lastError() { return WSAGetLastError(); }
void closeSocket(Socket fd) { closesocket(fd); }
struct SocketRuntime {
    SocketRuntime() { WSADATA data{}; int e = WSAStartup(MAKEWORD(2, 2), &data); if (e) throw std::system_error(e, std::system_category(), "WSAStartup"); }
    ~SocketRuntime() { WSACleanup(); }
};
#else
using Socket = int;
constexpr Socket Invalid = -1;
int lastError() { return errno; }
void closeSocket(Socket fd) { ::close(fd); }
#endif
sockaddr_in address(const Endpoint& endpoint) {
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
struct UdpSocket::Impl {
#ifdef _WIN32
    SocketRuntime runtime;
#endif
    Socket fd = Invalid;
    std::array<std::uint8_t, 65536> buffer{};
    ~Impl() { if (fd != Invalid) closeSocket(fd); }
};
UdpSocket::UdpSocket(const Endpoint& local) : impl_(std::make_unique<Impl>()) {
    auto addr = address(local);
    impl_->fd = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (impl_->fd == Invalid) throw std::system_error(lastError(), std::system_category(), "socket");
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
    if (impl_->fd >= FD_SETSIZE) throw std::runtime_error("socket exceeds select FD_SETSIZE");
#endif
}
UdpSocket::~UdpSocket() = default;
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
bool UdpSocket::waitReadable(unsigned timeoutMs) {
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
bool UdpSocket::receive(Endpoint& from, Bytes& bytes) {
    sockaddr_in addr{};
#ifdef _WIN32
    int len = sizeof(addr);
    int size = recvfrom(impl_->fd, reinterpret_cast<char*>(impl_->buffer.data()), static_cast<int>(impl_->buffer.size()), 0,
                        reinterpret_cast<sockaddr*>(&addr), &len);
    if (size < 0) {
        auto e = lastError();
        if (e == WSAEWOULDBLOCK || e == WSAEINTR || e == WSAECONNRESET) return false;
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
    return true; // a zero-byte UDP datagram is data, never a disconnect
}
bool UdpSocket::send(const Endpoint& to, const Bytes& bytes) {
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
} // namespace chat::net
