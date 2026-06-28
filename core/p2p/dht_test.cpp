// tools/dht_test.cpp
// DHT Bootstrap Test Tool. DHT 引导测试工具。

#include "dht.hpp"
#include <iostream>
#include <cstring>
#include <thread>
#include <chrono>
#include <libsodium>

#ifdef _WIN32
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#endif

using namespace numotirus::dht;

int main(int argc, char** argv) {
    std::cout << "=== DHT Bootstrap Test ===\n\n";

    // Initialize libsodium. 初始化 libsodium。
    if (sodium_init() < 0) {
        std::cerr << "libsodium init failed\n";
        return 1;
    }

    // Generate random node ID. 生成随机节点 ID。
    NodeId own_id = DhtClient::GenerateNodeId();

    std::cout << "Own Node ID: ";
    for (auto b : own_id) {
        printf("%02x", b);
    }
    printf("\n\n");

    // Create UDP socket. 创建 UDP 套接字。
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
#else
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
#endif

    if (sock < 0) {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    // Bind to any port. 绑定到任意端口。
    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = 0;  // Let OS choose. 让操作系统选择。

    if (bind(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Failed to bind socket\n";
#ifdef _WIN32
        closesocket(sock);
        WSACleanup();
#else
        close(sock);
#endif
        return 1;
    }

    // Create routing table. 创建路由表。
    RoutingTable routing_table(own_id);

    // Bootstrap. 引导。
    int added = 0;
    auto callback = [&](const std::string& ip, uint16_t port, const std::vector<Node>& nodes) {
        std::cout << "  From " << ip << ":" << port << " -> " << nodes.size() << " nodes\n";
        for (const auto& node : nodes) {
            routing_table.AddNode(node);
            added++;
        }
    };

    std::cout << "Bootstrapping from default BitTorrent nodes...\n\n";
    int success = BootstrapRoutingTable(sock, own_id, routing_table, callback);

    std::cout << "\n=== Results ===\n";
    std::cout << "Successful bootstraps: " << success << "\n";
    std::cout << "Total nodes added: " << added << "\n";
    std::cout << "Routing table size: " << routing_table.GetTotalNodes() << "\n\n";
    
    routing_table.Print();

    // Cleanup. 清理。
#ifdef _WIN32
    closesocket(sock);
    WSACleanup();
#else
    close(sock);
#endif

    return 0;
}