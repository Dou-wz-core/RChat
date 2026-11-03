#include "net/protocol.hpp"
#include <algorithm>
#include <stdexcept>

namespace chat::net {
namespace {
// 按高位字节在前的顺序读写整数，width 决定字段在协议中占用的字节数。
void put(Bytes& b, std::size_t pos, std::uint64_t value, std::size_t width) {
    for (std::size_t i = 0; i < width; ++i)
        b[pos + i] = static_cast<std::uint8_t>(value >> ((width - i - 1) * 8));
}

// 按高位字节在前的顺序读写整数，width 决定字段在协议中占用的字节数。
std::uint64_t get(const std::uint8_t* b, std::size_t pos, std::size_t width) {
    std::uint64_t n = 0;
    for (std::size_t i = 0; i < width; ++i) n = (n << 8) | b[pos + i];
    return n;
}

// 校验 UDP 报文的字段约束；不检查 CRC，允许部分字段为 0。
bool valid(const Packet& p) {
    if (p.payload.size() > MaxPayload) return false;
    // 数据片必须携带有效会话、序号和消息号；除末片外，每片都要填满有效载荷。
    // 末片同时校验整条消息的实际大小，防止合法分片数拼出超过 64KiB 的消息。
    if (p.type == PacketType::Data)
        return p.session && p.sequence && p.message && !p.ack &&
               p.fragments && p.fragments <= MaxFragments &&
               p.fragment < p.fragments && !p.payload.empty() &&
               (p.fragment + 1 == p.fragments || p.payload.size() == MaxPayload) &&
               (p.fragment + 1 != p.fragments ||
                (static_cast<std::size_t>(p.fragments) - 1) * MaxPayload + p.payload.size() <= MaxMessage);
    // 控制报文不能混入数据片字段，握手报文的载荷只能是非零的 8 字节随机数。
    if (p.sequence || p.message || p.replyTo || p.fragment || p.fragments) return false;
    switch (p.type) {
    case PacketType::Hello: return !p.session && !p.ack && p.payload.size() == 8 && decodeNonce(p.payload);
    case PacketType::Welcome: return p.session && !p.ack && p.payload.size() == 8 && decodeNonce(p.payload);
    case PacketType::Ack: return p.session && p.ack && p.payload.empty();
    case PacketType::Ping:
    case PacketType::Pong:
    case PacketType::Close: return p.session && !p.ack && p.payload.empty();
    default: return false;
    }
}
}

// 逐位计算 CRC32：每次处理最低位，使用反射多项式 0xedb88320。
std::uint32_t crc32(const std::uint8_t* data, std::size_t size) {
    // 逐位计算 CRC32：每次处理最低位，使用反射多项式 0xedb88320。
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (int bit = 0; bit < 8; ++bit)
            crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}

// 编解码显式使用网络字节序，避免结构体填充和主机字节序影响报文布局。
Bytes encode(const Packet& p) {
    if (!valid(p)) throw std::invalid_argument("invalid UDP packet");
    Bytes out(HeaderSize + p.payload.size(), 0);
    // 0..3 为协议标识，4 为版本，5 为报文类型，6..7 的保留标志保持为 0。
    out[0] = 'C'; out[1] = 'H'; out[2] = 'U'; out[3] = 'D';
    out[4] = 1; out[5] = static_cast<std::uint8_t>(p.type);
    // 8..35 保存会话、收发序号、请求关联和分片信息，36..39 保存载荷与首部长度。
    put(out, 8, p.session, 8); put(out, 16, p.sequence, 4); put(out, 20, p.ack, 4);
    put(out, 24, p.message, 4); put(out, 28, p.replyTo, 4);
    put(out, 32, p.fragment, 2); put(out, 34, p.fragments, 2);
    put(out, 36, p.payload.size(), 2); put(out, 38, HeaderSize, 2);
    std::copy(p.payload.begin(), p.payload.end(), out.begin() + HeaderSize);
    // 计算时 40..43 的校验字段仍为 0；校验范围包括整个首部和有效载荷。
    put(out, 40, crc32(out.data(), out.size()), 4);
    return out;
}

// 校验长度、首部、CRC 和字段约束；全部通过后才更新 packet。
bool decode(const std::uint8_t* data, std::size_t size, Packet& p, std::string* error) {
    auto fail = [&](const char* why) { if (error) *error = why; return false; };
    // 先限制数据报大小，再访问固定偏移，避免短报文造成越界读取。
    if (!data || size < HeaderSize || size > MaxDatagram) return fail("datagram size");
    if (data[0] != 'C' || data[1] != 'H' || data[2] != 'U' || data[3] != 'D') return fail("magic");
    if (data[4] != 1 || get(data, 6, 2) || get(data, 38, 2) != HeaderSize) return fail("version/flags/header");
    if (HeaderSize + get(data, 36, 2) != size) return fail("payload length");
    // 将校验字段清零后重算 CRC，与编码时使用相同的字节序列。
    Bytes copy(data, data + size);
    std::fill(copy.begin() + 40, copy.begin() + 44, std::uint8_t{0});
    if (crc32(copy.data(), size) != get(data, 40, 4)) return fail("CRC32");
    // 使用临时对象承接解析结果，错误报文不会覆盖调用方已有的 Packet。
    Packet candidate;
    candidate.type = static_cast<PacketType>(data[5]);
    candidate.session = get(data, 8, 8);
    candidate.sequence = static_cast<std::uint32_t>(get(data, 16, 4));
    candidate.ack = static_cast<std::uint32_t>(get(data, 20, 4));
    candidate.message = static_cast<std::uint32_t>(get(data, 24, 4));
    candidate.replyTo = static_cast<std::uint32_t>(get(data, 28, 4));
    candidate.fragment = static_cast<std::uint16_t>(get(data, 32, 2));
    candidate.fragments = static_cast<std::uint16_t>(get(data, 34, 2));
    candidate.payload.assign(data + HeaderSize, data + size);
    if (!valid(candidate)) return fail("packet semantics");
    p = std::move(candidate);
    if (error) error->clear();
    return true;
}
Bytes encodeNonce(std::uint64_t nonce) { Bytes b(8); put(b, 0, nonce, 8); return b; }
std::uint64_t decodeNonce(const Bytes& b) { return b.size() == 8 ? get(b.data(), 0, 8) : 0; }
} // chat::net 命名空间
