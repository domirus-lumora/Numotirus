// core/p2p/nat_stun.cpp
// STUN client implementation. STUN 客户端实现。

#include "nat_stun.hpp"
#include <cstring>
#include <random>
#include <chrono>
#include <string>
#include <vector>

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

// STUN attribute types. STUN 属性类型。
enum StunAttr : uint16_t {
    kAttrMappedAddress = 0x0001,
    kAttrXorMappedAddress = 0x0020,
};

// STUN header. STUN 头部。
struct StunHeader {
    uint16_t type;
    uint16_t length;
    uint8_t transaction_id[kStunTransactionIdSize];
} __attribute__((packed));

// XOR mapped address attribute. XOR 映射地址属性。
struct XorMappedAddressAttr {
    uint16_t type;
    uint16_t length;
    uint8_t reserved;
    uint8_t family;
    uint16_t port;
    uint8_t address[4];
} __attribute__((packed));

std::vector<uint8_t> StunClient::BuildBindingRequest() {
    // Generate transaction ID. 生成事务 ID。
    std::random_device rd;
    std::mt19937 gen(rd());
    for (auto& b : transaction_id_) {
        b = static_cast<uint8_t>(gen() & 0xff);
    }

    StunHeader header;
    header.type = htons(static_cast<uint16_t>(StunMethod::kBinding) |
                        static_cast<uint16_t>(StunClass::kRequest));
    header.length = 0;
    std::memcpy(header.transaction_id, transaction_id_.data(), kStunTransactionIdSize);

    std::vector<uint8_t> result(sizeof(header));
    std::memcpy(result.data(), &header, sizeof(header));
    return result;
}

bool StunClient::ParseResponse(const uint8_t* data, size_t len, MappedAddress& out) {
    if (len < kStunHeaderSize) return false;

    const StunHeader* header = reinterpret_cast<const StunHeader*>(data);
    uint16_t msg_type = ntohs(header->type);
    uint16_t msg_class = msg_type & 0x0110;

    // Check if it's a success response. 检查是否为成功响应。
    if (msg_class != static_cast<uint16_t>(StunClass::kSuccessResponse)) {
        return false;
    }

    // Check transaction ID. 检查事务 ID。
    if (std::memcmp(header->transaction_id, transaction_id_.data(), kStunTransactionIdSize) != 0) {
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

        if (attr_type == kAttrXorMappedAddress || attr_type == kAttrMappedAddress) {
            const XorMappedAddressAttr* addr_attr =
                reinterpret_cast<const XorMappedAddressAttr*>(data + pos);
            uint16_t port = ntohs(addr_attr->port);
            if (attr_type == kAttrXorMappedAddress) {
                port ^= 0x2112;  // Magic cookie XOR for port (RFC 5389).
            }

            char ip_str[INET_ADDRSTRLEN];
            struct in_addr addr;
            std::memcpy(&addr, addr_attr->address, 4);
            if (attr_type == kAttrXorMappedAddress) {
                uint32_t magic_cookie = 0x2112A442;
                addr.s_addr ^= htonl(magic_cookie);
            }

            inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
            out.ip = ip_str;
            out.port = port;
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
    // Initialize Winsock. 初始化 Winsock。
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return std::nullopt;
    }
#endif

    // Create UDP socket. 创建 UDP 套接字。
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
    if (sendto(sock, reinterpret_cast<const char*>(request.data()), request.size(), 0,
               reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr)) < 0) {
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return std::nullopt;
    }

    // Receive response. 接收响应。
    uint8_t buffer[1024];
    struct sockaddr_in from;
    socklen_t from_len = sizeof(from);
    int n = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                     reinterpret_cast<struct sockaddr*>(&from), &from_len);

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

    return addr;
}

} // namespace nat
} // namespace numotirus