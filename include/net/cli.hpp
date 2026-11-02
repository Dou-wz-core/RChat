#pragma once
#include <cstdint>
#include <stdexcept>
#include <string>
namespace chat::net {
inline std::uint16_t parsePort(const std::string& text) {
    std::size_t used = 0;
    auto port = std::stoul(text, &used);
    if (used != text.size() || port == 0 || port > 65535) throw std::invalid_argument("port must be 1..65535");
    return static_cast<std::uint16_t>(port);
}
}
