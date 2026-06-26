// core/p2p/udp_hole_punch.cpp
// UDP hole punching implementation. UDP 打洞实现。

#include "udp_hole_punch.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#define CLOSE_SOCKET closesocket
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#define CLOSE_SOCKET close
#endif

namespace numotirus {
namespace nat {

std::string GetPublicAddressViaStun(const std::string& stun_server) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    size_t colon = stun_server.find(':');
    if (colon == std::string::npos) return "";
    std::string host = stun_server.substr(0, colon);
    uint16_t port = static_cast<uint16_t>(std::stoi(stun_server.substr(colon + 1)));

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "";

    struct sockaddr_in server_addr{};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr);

    // STUN binding request (simplified).
    unsigned char request[] = {0x00, 0x01, 0x00, 0x00, 0x21, 0x12, 0xA4, 0x42};
    sendto(sock, (const char*)request, sizeof(request), 0, (struct sockaddr*)&server_addr, sizeof(server_addr));

    char buffer[1024];
    struct sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &from_len);
    CLOSE_SOCKET(sock);

#ifdef _WIN32
    WSACleanup();
#endif

    if (n < 0) return "";

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &from.sin_addr, ip_str, INET_ADDRSTRLEN);
    return std::string(ip_str) + ":" + std::to_string(ntohs(from.sin_port));
}

void StartUdpHolePunch(uint16_t local_port,
                       const std::string& stun_server,
                       const std::string& peer_ip,
                       uint16_t peer_port,
                       PunchCallback callback) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        callback(false, "", 0);
        return;
    }

    struct sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port);
    if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        CLOSE_SOCKET(sock);
        callback(false, "", 0);
        return;
    }

    if (!stun_server.empty()) {
        std::string public_addr = GetPublicAddressViaStun(stun_server);
        if (!public_addr.empty()) {
            std::cout << "[HolePunch] Public address: " << public_addr << std::endl;
        }
    }

    struct sockaddr_in peer_addr{};
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(peer_port);
    inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr);

    const char* punch_msg = "PUNCH";
    for (int i = 0; i < 5; ++i) {
        sendto(sock, punch_msg, strlen(punch_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    char buffer[1024];
    struct sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &from_len);

    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif

    if (n > 0) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip_str, INET_ADDRSTRLEN);
        callback(true, ip_str, ntohs(from.sin_port));
    } else {
        callback(false, "", 0);
    }
}

} // namespace nat
} // namespace numotirus