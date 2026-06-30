// core/p2p/nat_stun.hpp
// STUN client for NAT traversal with support for symmetric NAT learning.
// STUN 客户端，用于 NAT 穿透，支持对称型 NAT 学习。

#pragma once

#include <array>
#include <string>
#include <cstdint>
#include <optional>
#include <vector>
#include <chrono>

namespace numotirus {
namespace nat {

constexpr uint16_t kStunPort = 3478;
constexpr size_t kStunHeaderSize = 20;
constexpr size_t kStunTransactionIdSize = 12;
constexpr size_t kStunMaxResponseSize = 1024;
constexpr int kStunDefaultTimeoutMs = 3000;

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

// STUN attribute types. STUN 属性类型。
enum class StunAttribute : uint16_t {
    kMappedAddress = 0x0001,
    kXorMappedAddress = 0x0020,
    kErrorCode = 0x0009,
};

// Mapped address from STUN response. STUN 响应中的映射地址。
struct MappedAddress {
    std::string ip;      // Public IP. 公网 IP。
    uint16_t port = 0;   // Public port. 公网端口。
    uint16_t family = 0; // Address family (IPv4 = 1). 地址族（IPv4 = 1）。

    bool IsValid() const { return !ip.empty() && port > 0; }
};

// STUN client with support for multiple queries.
// STUN 客户端，支持多次查询。
class StunClient {
public:
    StunClient();

    // Query public address from STUN server.
    // 向 STUN 服务器查询公网地址。
    // Returns std::nullopt on timeout or error.
    // 超时或错误时返回 std::nullopt。
    std::optional<MappedAddress> QueryPublicAddress(
        const std::string& server_host,
        uint16_t server_port = kStunPort,
        int timeout_ms = kStunDefaultTimeoutMs
    );

    // Query multiple times to detect symmetric NAT behavior.
    // 多次查询以检测对称型 NAT 行为。
    // Returns vector of addresses, empty on failure.
    // 返回地址向量，失败时为空。
    std::vector<MappedAddress> QueryMultiple(
        const std::string& server_host,
        uint16_t server_port = kStunPort,
        int count = 5,
        int timeout_ms = kStunDefaultTimeoutMs
    );

    // Generate a new transaction ID. 生成新的事务 ID。
    void GenerateTransactionId();

    // Get current transaction ID. 获取当前事务 ID。
    const std::array<uint8_t, kStunTransactionIdSize>& GetTransactionId() const {
        return transaction_id_;
    }

private:
    std::array<uint8_t, kStunTransactionIdSize> transaction_id_;

    // Build a STUN binding request. 构建 STUN 绑定请求。
    std::vector<uint8_t> BuildBindingRequest();

    // Parse a STUN response. 解析 STUN 响应。
    bool ParseResponse(const uint8_t* data, size_t len, MappedAddress& out);

    // Helper: XOR-decrypt a port. 辅助：XOR 解密端口。
    uint16_t XorDecryptPort(uint16_t port) const;

    // Helper: XOR-decrypt an IPv4 address. 辅助：XOR 解密 IPv4 地址。
    uint32_t XorDecryptAddress(uint32_t addr) const;

    static const uint32_t kStunMagicCookie = 0x2112A442;
};

// ============================================================
// Convenience functions. 便捷函数。
// ============================================================

// Quick single STUN query. 快速单次 STUN 查询。
std::optional<MappedAddress> QueryStun(
    const std::string& server_host = "stun.l.google.com",
    uint16_t server_port = 19302,
    int timeout_ms = kStunDefaultTimeoutMs
);

// Detect if NAT is symmetric by comparing multiple queries.
// 通过多次查询检测 NAT 是否对称。
bool IsSymmetricNat(
    const std::string& server_host = "stun.l.google.com",
    uint16_t server_port = 19302,
    int samples = 5
);

} // namespace nat
} // namespace numotirus