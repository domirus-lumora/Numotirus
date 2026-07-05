// core/p2p/udp_hole_punch.cpp
// Multi-port UDP hole punching implementation.
// 多端口 UDP 打洞实现。
// SPDX-License-Identifier: Apache-2.0

#include "udp_hole_punch.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <atomic>
#include <mutex>
#include <vector>
#include <unordered_set>
#include <map>

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

struct MultiHolePuncher::Impl {
    int sock_ = -1;
    std::string stun_server_;
    std::mutex mutex_;
    std::atomic<bool> cancelled_{false};
    std::unordered_set<uint16_t> sent_ports_;   // Fast lookup. 快速查找。
    PunchCallback callback_;
    std::thread worker_thread_;

    ~Impl() {
        if (worker_thread_.joinable()) {
            worker_thread_.join();
        }
    }

    bool SendPunchPacket(const std::string& ip, uint16_t port);
    void ReceiveLoop(const std::string& target_ip,
                     const std::vector<uint16_t>& target_ports,
                     int timeout_ms);
};

bool MultiHolePuncher::Impl::SendPunchPacket(const std::string& ip, uint16_t port) {
    if (sock_ < 0) return false;

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        return false;
    }

    const char* punch_msg = "PUNCH";
    int sent = sendto(sock_, punch_msg, strlen(punch_msg), 0,
                      reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return sent > 0;
}

void MultiHolePuncher::Impl::ReceiveLoop(const std::string& target_ip,
                                         const std::vector<uint16_t>& target_ports,
                                         int timeout_ms) {
    // Set socket timeout.
    // 设置套接字超时。
#ifdef _WIN32
    DWORD timeout = timeout_ms;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    uint64_t start_time = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    std::cout << "[HolePunch] Sending to " << target_ports.size() << " ports...\n";

    // Send punch packets to all target ports.
    // 向所有目标端口发送打洞包。
    int ports_sent = 0;
    sent_ports_.clear();
    for (uint16_t port : target_ports) {
        if (cancelled_.load()) break;
        if (SendPunchPacket(target_ip, port)) {
            sent_ports_.insert(port);
            ports_sent++;
        }
        // Rate limit: don't flood too fast.
        // 速率限制：不要发送太快。
        if (ports_sent % 10 == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    std::cout << "[HolePunch] Sent " << ports_sent << " punch packets. Waiting for response...\n";

    // Wait for response.
    // 等待响应。
    uint8_t buffer[1024];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);

    while (!cancelled_.load()) {
        int n = recvfrom(sock_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                         reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);

        if (n <= 0) {
            // Check timeout.
            // 检查超时。
            uint64_t now = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count();
            if (now - start_time > static_cast<uint64_t>(timeout_ms)) {
                break;
            }
            continue;
        }

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
        uint16_t from_port = ntohs(from_addr.sin_port);

        std::cout << "[HolePunch] Received response from " << ip_str << ":" << from_port << "\n";

        // Check if this port was in our sent list.
        // 检查这个端口是否在我们的发送列表中。
        if (sent_ports_.find(from_port) != sent_ports_.end() && callback_) {
            PunchResult result;
            result.success = true;
            result.peer_ip = ip_str;
            result.peer_port = from_port;
            result.hit_port = from_port;
            result.local_port = from_port; // Actual local port used (same as from_port if symmetric).
            result.latency_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now().time_since_epoch()
            ).count() - start_time;

            callback_(result);
            return;
        }
    }

    // Timeout or cancelled.
    // 超时或取消。
    if (callback_) {
        PunchResult result;
        result.success = false;
        callback_(result);
    }
}

MultiHolePuncher::MultiHolePuncher() : impl_(std::make_unique<Impl>()) {}

MultiHolePuncher::~MultiHolePuncher() {
    Cancel();
}

bool MultiHolePuncher::Initialize(int sock, const std::string& stun_server) {
    if (sock < 0) return false;
    impl_->sock_ = sock;
    impl_->stun_server_ = stun_server;
    return true;
}

void MultiHolePuncher::Punch(const std::string& target_ip,
                             const std::vector<uint16_t>& target_ports,
                             PunchCallback callback,
                             int timeout_ms) {
    if (impl_->sock_ < 0 || target_ports.empty()) {
        if (callback) {
            PunchResult result;
            result.success = false;
            callback(result);
        }
        return;
    }

    // Cancel previous run.
    // 取消上一次运行。
    Cancel();

    running_.store(true);
    impl_->cancelled_.store(false);
    impl_->callback_ = callback;

    // Start worker thread.
    // 启动工作线程。
    impl_->worker_thread_ = std::thread([this, target_ip, target_ports, timeout_ms]() {
        impl_->ReceiveLoop(target_ip, target_ports, timeout_ms);
        running_.store(false);
    });
}

void MultiHolePuncher::Cancel() {
    if (running_.load()) {
        impl_->cancelled_.store(true);
        if (impl_->worker_thread_.joinable()) {
            impl_->worker_thread_.join();
        }
        running_.store(false);
    }
}

// Legacy functions (unchanged, but kept for compatibility).
// 旧版函数（未改动，保留兼容性）。

void StartUdpHolePunch(uint16_t local_port,
                       const std::string& stun_server,
                       const std::string& peer_ip,
                       uint16_t peer_port,
                       PunchCallback callback) {
    (void)stun_server;
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        if (callback) {
            PunchResult result;
            result.success = false;
            callback(result);
        }
        return;
    }

    struct sockaddr_in local_addr = {};
    local_addr.sin_family = AF_INET;
    local_addr.sin_addr.s_addr = INADDR_ANY;
    local_addr.sin_port = htons(local_port);
    if (bind(sock, reinterpret_cast<struct sockaddr*>(&local_addr), sizeof(local_addr)) < 0) {
        CLOSE_SOCKET(sock);
        if (callback) {
            PunchResult result;
            result.success = false;
            callback(result);
        }
        return;
    }

    struct sockaddr_in peer_addr = {};
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_port = htons(peer_port);
    inet_pton(AF_INET, peer_ip.c_str(), &peer_addr.sin_addr);

    const char* punch_msg = "PUNCH";
    for (int i = 0; i < 5; ++i) {
        sendto(sock, punch_msg, strlen(punch_msg), 0,
               reinterpret_cast<struct sockaddr*>(&peer_addr), sizeof(peer_addr));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    uint8_t buffer[1024];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
#ifdef _WIN32
    DWORD timeout = 2000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv = {2, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    int n = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                     reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif

    if (n > 0 && callback) {
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
        PunchResult result;
        result.success = true;
        result.peer_ip = ip_str;
        result.peer_port = ntohs(from_addr.sin_port);
        result.hit_port = result.peer_port;
        callback(result);
    } else if (callback) {
        PunchResult result;
        result.success = false;
        callback(result);
    }
}

std::string GetPublicAddressViaStun(const std::string& stun_server) {
    // This function remains as-is for compatibility.
    // 该函数保持不变以兼容。
    size_t colon = stun_server.find(':');
    if (colon == std::string::npos) return "";

    std::string host = stun_server.substr(0, colon);
    uint16_t port = static_cast<uint16_t>(std::stoi(stun_server.substr(colon + 1)));

#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) return "";

    struct addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    struct addrinfo* result = nullptr;
    if (getaddrinfo(host.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
        CLOSE_SOCKET(sock);
        return "";
    }

    struct sockaddr_in server_addr = {};
    memcpy(&server_addr, result->ai_addr, sizeof(server_addr));
    server_addr.sin_port = htons(port);
    freeaddrinfo(result);

    uint8_t request[] = {
        0x00, 0x01, 0x00, 0x00,
        0x21, 0x12, 0xA4, 0x42,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
        0x00, 0x00, 0x00, 0x00
    };
    sendto(sock, reinterpret_cast<const char*>(request), sizeof(request), 0,
           reinterpret_cast<struct sockaddr*>(&server_addr), sizeof(server_addr));

    uint8_t buffer[1024];
    struct sockaddr_in from_addr;
    socklen_t from_len = sizeof(from_addr);
#ifdef _WIN32
    DWORD timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    int n = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                     reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
    CLOSE_SOCKET(sock);
#ifdef _WIN32
    WSACleanup();
#endif

    if (n < 0) return "";

    char ip_str[INET_ADDRSTRLEN];
    inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
    return std::string(ip_str) + ":" + std::to_string(ntohs(from_addr.sin_port));
}

} // namespace nat
} // namespace numotirus