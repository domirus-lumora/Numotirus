// core/p2p/nat_stun.cpp
// STUN client for NAT traversal with support for symmetric NAT learning.
// STUN 客户端，用于 NAT 穿透，支持对称型 NAT 学习。

#include "nat_stun.hpp"
#include <cstring>
#include <random>
#include <chrono>
#include <iostream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

namespace numotirus {
namespace nat {

// ============================================================
// STUN Protocol Structures. STUN 协议结构。
// ============================================================

// STUN header (RFC 5389). STUN 头部（RFC 5389）。
struct StunHeader {
    uint16_t type;          // Message type. 消息类型。
    uint16_t length;        // Message length (excluding header). 消息长度（不含头部）。
    uint8_t transaction_id[kStunTransactionIdSize];  // Transaction ID. 事务 ID。
} __attribute__((packed));

// XOR-MAPPED-ADDRESS attribute. XOR-MAPPED-ADDRESS 属性。
struct XorMappedAddressAttr {
    uint16_t type;          // Attribute type. 属性类型。
    uint16_t length;        // Attribute length. 属性长度。
    uint8_t reserved;       // MUST be 0. 必须为 0。
    uint8_t family;         // 1 = IPv4. 1 表示 IPv4。
    uint16_t port;          // XOR'd port. XOR 后的端口。
    uint8_t address[4];     // XOR'd IPv4 address. XOR 后的 IPv4 地址。
} __attribute__((packed));

// ============================================================
// StunClient Implementation. StunClient 实现。
// ============================================================

StunClient::StunClient() {
    GenerateTransactionId();
}

void StunClient::GenerateTransactionId() {
    std::random_device rd;
    std::mt19937 gen(rd());
    for (auto& b : transaction_id_) {
        b = static_cast<uint8_t>(gen() & 0xFF);
    }
}

uint32_t StunClient::XorDecryptAddress(uint32_t addr) const {
    return addr ^ kStunMagicCookie;
}

uint16_t StunClient::XorDecryptPort(uint16_t port) const {
    // XOR with the high 16 bits of the magic cookie. 与 magic cookie 的高 16 位异或。
    return port ^ (static_cast<uint16_t>(kStunMagicCookie >> 16));
}

std::vector<uint8_t> StunClient::BuildBindingRequest() {
    GenerateTransactionId();

    StunHeader header = {};
    header.type = htons(static_cast<uint16_t>(StunMethod::kBinding) |
                        static_cast<uint16_t>(StunClass::kRequest));
    header.length = 0;  // No attributes. 无属性。
    std::memcpy(header.transaction_id, transaction_id_.data(), kStunTransactionIdSize);

    std::vector<uint8_t> result(sizeof(header));
    std::memcpy(result.data(), &header, sizeof(header));
    return result;
}

bool StunClient::ParseResponse(const uint8_t* data, size_t len, MappedAddress& out) {
    if (len < kStunHeaderSize) {
        return false;
    }

    const StunHeader* header = reinterpret_cast<const StunHeader*>(data);
    uint16_t msg_type = ntohs(header->type);
    uint16_t msg_class = msg_type & 0x0110;

    // Check for success response. 检查是否为成功响应。
    if (msg_class != static_cast<uint16_t>(StunClass::kSuccessResponse)) {
        return false;
    }

    // Check transaction ID. 检查事务 ID。
    if (std::memcmp(header->transaction_id, transaction_id_.data(), kStunTransactionIdSize) != 0) {
        // Transaction ID mismatch. 事务 ID 不匹配。
        return false;
    }

    // Parse attributes. 解析属性。
    size_t pos = kStunHeaderSize;
    uint16_t length = ntohs(header->length);

    while (pos + 4 <= len && pos - kStunHeaderSize < length) {
        const uint16_t* attr_type_ptr = reinterpret_cast<const uint16_t*>(data + pos);
        uint16_t attr_type = ntohs(*attr_type_ptr);
        const uint16_t* attr_len_ptr = reinterpret_cast<const uint16_t*>(data + pos + 2);
        uint16_t attr_len = ntohs(*attr_len_ptr);
        pos += 4;

        // Check for XOR-MAPPED-ADDRESS or MAPPED-ADDRESS.
        // 检查 XOR-MAPPED-ADDRESS 或 MAPPED-ADDRESS。
        if (attr_type == static_cast<uint16_t>(StunAttribute::kXorMappedAddress) ||
            attr_type == static_cast<uint16_t>(StunAttribute::kMappedAddress)) {

            if (pos + attr_len > len || attr_len < 8) {
                pos += attr_len;
                continue;
            }

            const XorMappedAddressAttr* addr_attr =
                reinterpret_cast<const XorMappedAddressAttr*>(data + pos);

            uint16_t port = ntohs(addr_attr->port);
            if (attr_type == static_cast<uint16_t>(StunAttribute::kXorMappedAddress)) {
                port = XorDecryptPort(port);
            }

            // IPv4 only (family = 1). 仅 IPv4（family = 1）。
            if (addr_attr->family != 1) {
                pos += attr_len;
                continue;
            }

            // For XOR-MAPPED-ADDRESS, address is XOR'd with magic cookie.
            // 对于 XOR-MAPPED-ADDRESS，地址与 magic cookie 异或。
            uint32_t addr;
            std::memcpy(&addr, addr_attr->address, 4);
            if (attr_type == static_cast<uint16_t>(StunAttribute::kXorMappedAddress)) {
                addr = XorDecryptAddress(addr);
            }

            char ip_str[INET_ADDRSTRLEN];
            struct in_addr in_addr;
            in_addr.s_addr = addr;
            inet_ntop(AF_INET, &in_addr, ip_str, INET_ADDRSTRLEN);

            out.ip = ip_str;
            out.port = port;
            out.family = addr_attr->family;

            return true;
        }

        pos += attr_len;
        if (attr_len % 4 != 0) {
            pos += 4 - (attr_len % 4);
        }
    }

    return false;
}

std::optional<MappedAddress> StunClient::QueryPublicAddress(
    const std::string& server_host,
    uint16_t server_port,
    int timeout_ms) {

#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return std::nullopt;
    }
#endif

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
#ifdef _WIN32
        WSACleanup();
#endif
        return std::nullopt;
    }

    // Resolve hostname. 解析主机名。
    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* result = nullptr;
    char port_str[8];
    snprintf(port_str, sizeof(port_str), "%d", server_port);

    if (getaddrinfo(server_host.c_str(), port_str, &hints, &result) != 0 || result == nullptr) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return std::nullopt;
    }

    struct sockaddr_in server_addr;
    std::memcpy(&server_addr, result->ai_addr, sizeof(server_addr));
    freeaddrinfo(result);

    // Set timeout. 设置超时。
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    // Send request. 发送请求。
    auto request = BuildBindingRequest();
    int sent = sendto(sock, reinterpret_cast<const char*>(request.data()), request.size(), 0,
                      reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));

    if (sent < 0) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return std::nullopt;
    }

    // Receive response. 接收响应。
    uint8_t buffer[kStunMaxResponseSize];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    int n = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                     reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);

#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    if (n < 0) {
        return std::nullopt;
    }

    MappedAddress addr;
    if (!ParseResponse(buffer, static_cast<size_t>(n), addr)) {
        return std::nullopt;
    }

    // If we got a response but the address is empty, fallback to from address.
    // 如果收到响应但地址为空，回退到 from 地址。
    if (!addr.IsValid()) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
        addr.ip = ip_str;
        addr.port = ntohs(from_addr.sin_port);
        addr.family = 1;
    }

    return addr;
}

std::vector<MappedAddress> StunClient::QueryMultiple(
    const std::string& server_host,
    uint16_t server_port,
    int count,
    int timeout_ms) {

    std::vector<MappedAddress> results;
    results.reserve(count);

    for (int i = 0; i < count; ++i) {
        auto addr = QueryPublicAddress(server_host, server_port, timeout_ms);
        if (addr.has_value()) {
            results.push_back(*addr);
        }
        // Small delay between queries to let NAT state settle.
        // 查询之间加小延迟，让 NAT 状态稳定。
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    return results;
}

// ============================================================
// Convenience functions. 便捷函数。
// ============================================================

std::optional<MappedAddress> QueryStun(
    const std::string& server_host,
    uint16_t server_port,
    int timeout_ms) {

    StunClient client;
    return client.QueryPublicAddress(server_host, server_port, timeout_ms);
}

bool IsSymmetricNat(
    const std::string& server_host,
    uint16_t server_port,
    int samples) {

    if (samples < 2) {
        samples = 5;
    }

    StunClient client;
    auto results = client.QueryMultiple(server_host, server_port, samples, 2000);

    if (results.size() < 2) {
        std::cout << "[STUN] Insufficient data to detect NAT type.\n";
        return false;
    }

    // Check if ports change across queries (symmetric NAT indicator).
    // 检查是否跨查询时端口发生变化（对称型 NAT 指标）。
    uint16_t first_port = results[0].port;
    bool symmetric = false;

    for (size_t i = 1; i < results.size(); ++i) {
        if (results[i].port != first_port) {
            symmetric = true;
            std::cout << "[STUN] Ports vary: " << first_port << " -> " << results[i].port
                      << " (likely symmetric NAT)\n";
            break;
        }
    }

    if (!symmetric) {
        std::cout << "[STUN] Ports consistent: " << first_port
                  << " (likely cone NAT)\n";
    }

    return symmetric;
}

} // namespace nat
} // namespace numotirus