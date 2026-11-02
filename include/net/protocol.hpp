#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace chat::net {
constexpr std::size_t HeaderSize = 44;
constexpr std::size_t MaxDatagram = 1200;
constexpr std::size_t MaxPayload = MaxDatagram - HeaderSize;
constexpr std::size_t MaxMessage = 64 * 1024;
constexpr std::size_t MaxFragments = (MaxMessage + MaxPayload - 1) / MaxPayload;
using Bytes = std::vector<std::uint8_t>;
enum class PacketType : std::uint8_t {
    Hello = 1, Welcome = 2, Data = 3, Ack = 4, Ping = 5, Pong = 6, Close = 7
};
struct Packet {
    PacketType type = PacketType::Data;
    std::uint64_t session = 0;
    std::uint32_t sequence = 0;
    std::uint32_t ack = 0;
    std::uint32_t message = 0;
    std::uint32_t replyTo = 0;
    std::uint16_t fragment = 0;
    std::uint16_t fragments = 0;
    Bytes payload;
};
std::uint32_t crc32(const std::uint8_t* data, std::size_t size);
// Codec explicitly writes network byte order; never sends a native C++ struct.
Bytes encode(const Packet& packet);
bool decode(const std::uint8_t* data, std::size_t size, Packet& packet,
            std::string* error = nullptr);
inline bool decode(const Bytes& bytes, Packet& packet, std::string* error = nullptr) {
    return decode(bytes.data(), bytes.size(), packet, error);
}
Bytes encodeNonce(std::uint64_t nonce);
std::uint64_t decodeNonce(const Bytes& bytes);
} // namespace chat::net
