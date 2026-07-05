// core/p2p/p2p_chat.cpp
// Command line chat demo using asynchronous ZRTP.
// 使用异步 ZRTP 的命令行聊天演示。
// SPDX-License-Identifier: Apache-2.0

#include "p2p.h"
#include "dht_c.h"          // for dht_print
#include <iostream>
#include <cstdlib>
#include <cstring>
#include <clocale>
#include <iomanip>

#ifdef _WIN32
#include <windows.h>
#endif

static P2PNode *g_node = nullptr;
static int g_zrtp_waiting = 0;

// ZRTP SAS ready callback.
// ZRTP SAS 就绪回调。
void on_sas_ready(const char *sas, void *user_data) {
    (void)user_data;
    std::cout << "\n========================================\n";
    std::cout << "ZRTP SAS:\n";
    std::cout << "  " << sas << "\n";
    std::cout << "Type 'y' to confirm, 'n' to reject. 输入 'y' 确认，'n' 拒绝。\n> ";
    std::cout.flush();
    g_zrtp_waiting = 1;
}

// ZRTP verification result callback.
// ZRTP 验证结果回调。
void on_zrtp_result(int confirmed, void *user_data) {
    (void)user_data;
    if (confirmed) {
        std::cout << "\n✅ ZRTP verified. ZRTP 验证成功。\n";
    } else {
        std::cout << "\n❌ ZRTP rejected. ZRTP 被拒绝。\n";
    }
    std::cout << "> ";
    std::cout.flush();
}

// Message callback: displays received messages.
// 消息回调：显示收到的消息。
void on_message(const char *ip, uint16_t port, const uint8_t *data, size_t len) {
    std::cout << "\n[" << ip << ":" << port << "] " 
              << std::string(reinterpret_cast<const char*>(data), len) << "\n> ";
    std::cout.flush();
}

int main(int argc, char **argv) {
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif
    std::setlocale(LC_ALL, "zh_CN.UTF-8");

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <listen_port> [peer_ip peer_port]\n";
        std::cerr << "用法: " << argv[0] << " <监听端口> [对方IP 对方端口]\n";
        return 1;
    }

    uint16_t listen_port = static_cast<uint16_t>(std::atoi(argv[1]));
    P2PNode *node = p2p_create(listen_port);
    if (!node) {
        std::cerr << "Failed to create P2P node. 创建 P2P 节点失败。\n";
        return 1;
    }
    g_node = node;

    // Set the message callback (corrected function name).
    // 设置消息回调（修正函数名）。
    p2p_set_message_callback(node, on_message);
    p2p_start(node);

    std::cout << "=== Numotirus P2P Chat ===\n";
    std::cout << "Listening on UDP port " << listen_port << ". 监听 UDP 端口 " << listen_port << "。\n";
    std::cout << "My public key: ";
    const uint8_t *pub = p2p_get_public_key(node);
    for (int i = 0; i < 32; ++i) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)pub[i];
    }
    std::cout << std::dec << "\n";

    char peer_ip[64] = "127.0.0.1";
    uint16_t peer_port = 0;
    int send_mode = 0;

    if (argc >= 4) {
        std::strncpy(peer_ip, argv[2], sizeof(peer_ip) - 1);
        peer_port = static_cast<uint16_t>(std::atoi(argv[3]));
        send_mode = 1;
        std::cout << "Will send to " << peer_ip << ":" << peer_port << ". 将发送到 " << peer_ip << ":" << peer_port << "。\n";
    }

    std::cout << "\nEnter peer public key (64 hex):\n> ";
    char hex[65];
    if (std::fgets(hex, sizeof(hex), stdin)) {
        hex[std::strcspn(hex, "\n")] = 0;
        if (std::strlen(hex) == 64) {
            uint8_t peer_pub[32];
            for (int i = 0; i < 32; ++i) {
                unsigned int val;
                std::sscanf(hex + i * 2, "%02x", &val);
                peer_pub[i] = static_cast<uint8_t>(val);
            }
            p2p_set_peer_key(node, peer_pub);
            std::cout << "Peer key set. Starting ZRTP... 对方公钥已设置，启动 ZRTP...\n";
            p2p_zrtp_start_exchange(node, on_sas_ready, on_zrtp_result, nullptr);
        }
    }

    std::cout << "\nCommands:\n";
    std::cout << "  /peer <ip> <port>  - set peer address. 设置对方地址。\n";
    std::cout << "  /key <64hex>       - set peer public key. 设置对方公钥。\n";
    std::cout << "  /dht               - show DHT routing table. 显示 DHT 路由表。\n";
    std::cout << "  /exit              - quit. 退出。\n";
    std::cout << "> ";
    std::cout.flush();

    char line[1024];
    while (std::fgets(line, sizeof(line), stdin)) {
        line[std::strcspn(line, "\n")] = 0;
        if (std::strlen(line) == 0) continue;

        if (g_zrtp_waiting) {
            if (line[0] == 'y' || line[0] == 'Y') {
                p2p_zrtp_confirm(g_node, 1);
                g_zrtp_waiting = 0;
            } else if (line[0] == 'n' || line[0] == 'N') {
                p2p_zrtp_confirm(g_node, 0);
                g_zrtp_waiting = 0;
            } else {
                std::cout << "Type 'y' or 'n'. 输入 'y' 或 'n'。\n> ";
                std::cout.flush();
            }
            continue;
        }

        if (std::strncmp(line, "/peer", 5) == 0) {
            char ip[64];
            int port;
            if (std::sscanf(line + 5, "%63s %d", ip, &port) == 2) {
                std::strncpy(peer_ip, ip, sizeof(peer_ip) - 1);
                peer_port = static_cast<uint16_t>(port);
                send_mode = 1;
                std::cout << "Peer set to " << peer_ip << ":" << peer_port << ". 对方地址已设为 " << peer_ip << ":" << peer_port << "。\n";

                // NAT traversal is not yet integrated into the core API.
                // NAT 穿透尚未集成到核心 API 中。
                // TODO: add p2p_nat_start_traversal when available.
                // char candidates[128];
                // std::snprintf(candidates, sizeof(candidates), "%s:%d", peer_ip, peer_port);
                // p2p_nat_start_traversal(g_node, candidates);
            } else {
                std::cout << "Usage: /peer <ip> <port>\n";
            }
        }
        else if (std::strncmp(line, "/key", 4) == 0) {
            char key[65];
            if (std::sscanf(line + 4, "%64s", key) == 1 && std::strlen(key) == 64) {
                uint8_t peer_pub[32];
                for (int i = 0; i < 32; ++i) {
                    unsigned int val;
                    std::sscanf(key + i * 2, "%02x", &val);
                    peer_pub[i] = static_cast<uint8_t>(val);
                }
                p2p_set_peer_key(node, peer_pub);
                std::cout << "Peer key set. Starting ZRTP... 对方公钥已设置，启动 ZRTP...\n";
                p2p_zrtp_start_exchange(node, on_sas_ready, on_zrtp_result, nullptr);
            } else {
                std::cout << "Invalid key. 无效公钥。\n";
            }
        }
        else if (std::strcmp(line, "/dht") == 0) {
            dht_print(p2p_get_dht(node));
        }
        else if (std::strcmp(line, "/exit") == 0) {
            break;
        }
        else if (send_mode && p2p_is_peer_ready(node)) {
            p2p_send(node, peer_ip, peer_port, reinterpret_cast<uint8_t*>(line), std::strlen(line));
        } else if (!send_mode) {
            std::cout << "Use /peer first. 请先使用 /peer 设置地址。\n";
        } else if (!p2p_is_peer_ready(node)) {
            std::cout << "Use /key first. 请先使用 /key 设置公钥。\n";
        }

        std::cout << "> ";
        std::cout.flush();
    }

    p2p_destroy(node);
    return 0;
}