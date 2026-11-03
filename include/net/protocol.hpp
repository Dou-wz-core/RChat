#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chat::net {
// 固定首部占 44 字节；每个数据报最多 1200 字节，较大的逻辑消息拆成多个分片。
constexpr std::size_t HeaderSize = 44;
constexpr std::size_t MaxDatagram = 1200;
constexpr std::size_t MaxPayload = MaxDatagram - HeaderSize;
constexpr std::size_t MaxMessage = 64 * 1024;
constexpr std::size_t MaxFragments = (MaxMessage + MaxPayload - 1) / MaxPayload;
using Bytes = std::vector<std::uint8_t>;
enum class PacketType : std::uint8_t {
    Hello = 1, Welcome = 2, Data = 3, Ack = 4, Ping = 5, Pong = 6, Close = 7
};
// Packet 仅用于内存中的字段表示，发送时按协议偏移逐项编码。
struct Packet {
    PacketType type = PacketType::Data;
    std::uint64_t session = 0;  // 握手后由服务端分配的会话标识，HELLO 中为 0。
    std::uint32_t sequence = 0; // 当前方向的数据分片序号，从 1 开始递增。
    std::uint32_t ack = 0;      // ACK 报文确认的分片序号，其他报文中为 0。
    std::uint32_t message = 0;  // 当前方向的逻辑消息编号，同一消息的所有分片共用。
    std::uint32_t replyTo = 0;  // 对应请求的传输消息编号，主动推送时通常为 0。
    std::uint16_t fragment = 0; // 分片下标，从 0 开始。
    std::uint16_t fragments = 0; // 当前逻辑消息的分片总数。
    Bytes payload;
};
std::uint32_t crc32(const std::uint8_t* data, std::size_t size);
// 编解码显式使用网络字节序，避免结构体填充和主机字节序影响报文布局。
Bytes encode(const Packet& packet);
// 校验长度、首部、CRC 和字段约束；全部通过后才更新 packet。
bool decode(const std::uint8_t* data, std::size_t size, Packet& packet,
            std::string* error = nullptr);
inline bool decode(const Bytes& bytes, Packet& packet, std::string* error = nullptr) {
    return decode(bytes.data(), bytes.size(), packet, error);
}
Bytes encodeNonce(std::uint64_t nonce);
// 握手随机数固定为 8 字节；长度不符时返回 0，作为无效值处理。
std::uint64_t decodeNonce(const Bytes& bytes);
} // chat::net 命名空间
