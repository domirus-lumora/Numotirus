// core/p2p/port_prediction.cpp
// Port prediction implementation. 端口预测实现。

#include "port_prediction.hpp"
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
#define CLOSE_SOCKET close
#endif

namespace numotirus {
namespace nat {

std::vector<uint16_t> PredictSymmetricNatPorts(uint16_t local_port,
                                               const std::string& stun_server) {
    std::vector<uint16_t> predictions;

    std::string current = GetPublicAddressViaStun(stun_server);
    if (current.empty()) return predictions;

    size_t colon = current.find(':');
    if (colon == std::string::npos) return predictions;
    uint16_t current_port = static_cast<uint16_t>(std::stoi(current.substr(colon + 1)));

    for (int i = 1; i <= 10; ++i) {
        predictions.push_back(current_port + i);
        predictions.push_back(current_port - i);
    }

    return predictions;
}

bool BirthdayAttackOnSymmetricNat(uint16_t local_port,
                                  const std::string& peer_ip,
                                  uint16_t peer_port,
                                  const std::vector<uint16_t>& predicted_ports) {
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return false;

    struct sockaddr_in local_addr{};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port);
    if (bind(sock, (struct sockaddr*)&local_addr, sizeof(local_addr)) < 0) {
        CLOSE_SOCKET(sock);
        return false;
    }

    struct sockaddr_in peer_addr{};
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(peer_port);
    inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr);

    const char* probe_msg = "PROBE";
    for (uint16_t port : predicted_ports) {
        peer_addr.sin_port = htons(port);
        sendto(sock, probe_msg, strlen(probe_msg), 0, (struct sockaddr*)&peer_addr, sizeof(peer_addr));
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }

    char buffer[1024];
    struct sockaddr_in from{};
    socklen_t from_len = sizeof(from);
    int n = recvfrom(sock, buffer, sizeof(buffer), 0, (struct sockaddr*)&from, &from_len);

    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif

    return n > 0;
}

} // namespace nat
} // namespace numotirus