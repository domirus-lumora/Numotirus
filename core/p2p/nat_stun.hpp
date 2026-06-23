// core/p2p/nat_stun.hpp
// STUN client for NAT traversal. STUN 客户端，用于 NAT 穿透。

#pragma once

#include <array>
#include <string>
#include <cstdint>
#include <optional>
#include <vector>

namespace numotirus {
namespace nat {

constexpr uint16_t kStunPort = 3478;
constexpr size_t kStunHeaderSize = 20;
constexpr size_t kStunTransactionIdSize = 12;

// STUN message types. STUN 消息类型。
enum class StunMethod : uint16_t {
    kBinding = 0x0001,
};

enum class StunClass : uint16_t {
    kRequest = 0x0000,
    kIndication = 0x0010,
    kSuccessResponse = 0x0100,
    kErrorResponse = 0x0110,
};

// Mapped address from STUN response. STUN 响应中的映射地址。
struct MappedAddress {
    std::string ip;      // 公网 IP / Public IP
    uint16_t port;       // 公网端口 / Public port
};

// STUN client. STUN 客户端。
class StunClient {
public:
    // Query public address from STUN server. 向 STUN 服务器查询公网地址。
    // Returns std::nullopt on timeout or error. 超时或错误时返回 std::nullopt。
    std::optional<MappedAddress> QueryPublicAddress(
        const std::string& server_host,
        uint16_t server_port = kStunPort,
        int timeout_ms = 3000
    );

private:
    std::vector<uint8_t> BuildBindingRequest();
    bool ParseResponse(const uint8_t* data, size_t len, MappedAddress& out);

    std::array<uint8_t, kStunTransactionIdSize> transaction_id_;
};

} // namespace nat
} // namespace numotirus