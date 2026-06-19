// core/p2p/p2p_chat.c
// Command line chat demo using asynchronous ZRTP.
// 使用异步 ZRTP 的命令行聊天演示。

#include "p2p.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif

// Global node pointer. 全局节点指针。
static P2PNode* g_node = NULL;
static int g_zrtp_waiting = 0;  // 1 if waiting for user confirmation.

// Callback when SAS is ready. SAS 就绪时的回调。
void on_sas_ready(const char* sas, void* user_data) {
    (void)user_data;
    printf("\n========================================\n");
    printf("ZRTP Short Authentication String (SAS):\n");
    printf("  %s\n", sas);
    printf("Please verify this code with your peer.\n");
    printf("Type 'y' then Enter to confirm, 'n' to reject.\n");
    printf("> ");
    fflush(stdout);
    g_zrtp_waiting = 1;
}

// Callback when user confirms. 用户确认时的回调。
void on_zrtp_result(int confirmed, void* user_data) {
    (void)user_data;
    if (confirmed) {
        printf("\n✅ ZRTP verification successful. You can now send encrypted messages.\n");
    } else {
        printf("\n❌ ZRTP verification rejected. Encryption not established.\n");
    }
    printf("> ");
    fflush(stdout);
}

// Message callback. 消息回调。
void on_message(const char* ip, uint16_t port, const uint8_t* data, size_t len) {
    printf("\n[%s:%d] %.*s\n> ", ip, port, (int)len, data);
    fflush(stdout);
}

int main(int argc, char** argv) {
#ifdef _WIN32
    system("chcp 65001 > nul");
#endif
    setlocale(LC_ALL, "zh_CN.UTF-8");

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <listen_port> [peer_ip peer_port]\n", argv[0]);
        return 1;
    }

    uint16_t listen_port = (uint16_t)atoi(argv[1]);
    P2PNode* node = p2p_create(listen_port);
    if (!node) {
        printf("Failed to create P2P node\n");
        return 1;
    }
    g_node = node;

    p2p_set_callback(node, on_message);
    p2p_start(node);

    printf("=== P2P Encrypted Chat ===\n");
    printf("Listening on UDP port %d\n", listen_port);
    printf("My public key: ");
    const uint8_t* pub = p2p_get_public_key(node);
    for (int i = 0; i < 32; i++) printf("%02x", pub[i]);
    printf("\n");

    char peer_ip[64] = "127.0.0.1";
    uint16_t peer_port = 0;
    int send_mode = 0;

    if (argc >= 4) {
        strncpy(peer_ip, argv[2], sizeof(peer_ip)-1);
        peer_port = (uint16_t)atoi(argv[3]);
        send_mode = 1;
        printf("Will send to %s:%d\n", peer_ip, peer_port);
    } else {
        printf("Send mode disabled. Use /peer <ip> <port> to enable.\n");
    }

    printf("\nEnter peer's public key (64 hex chars) to enable encryption:\n");
    printf("> ");

    char hex[65];
    if (fgets(hex, sizeof(hex), stdin)) {
        hex[strcspn(hex, "\n")] = 0;
        if (strlen(hex) == 64) {
            uint8_t peer_pub[32];
            for (int i = 0; i < 32; i++) {
                unsigned int val;
                sscanf(hex + i*2, "%02x", &val);
                peer_pub[i] = (uint8_t)val;
            }
            p2p_set_peer_key(node, peer_pub);
            printf("Peer key set. Starting asynchronous ZRTP exchange...\n");

            if (p2p_zrtp_start_exchange(node, on_sas_ready, on_zrtp_result, NULL) != 0) {
                printf("Failed to start ZRTP exchange.\n");
                p2p_destroy(node);
                return 1;
            }
        } else if (strlen(hex) > 0) {
            printf("Invalid key length (must be 64 hex chars).\n");
        }
    }

    printf("\nCommands:\n");
    printf("  /peer <ip> <port>  - set peer address\n");
    printf("  /key <64hex>       - set peer public key\n");
    printf("  /dht               - show DHT routing table\n");
    printf("  /exit              - quit\n");
    printf("  other text         - send message\n");
    printf("> ");
    fflush(stdout);

    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;

        // If waiting for ZRTP confirmation. 如果正在等待 ZRTP 确认。
        if (g_zrtp_waiting) {
            if (line[0] == 'y' || line[0] == 'Y') {
                p2p_zrtp_confirm(g_node, 1);
                g_zrtp_waiting = 0;
            } else if (line[0] == 'n' || line[0] == 'N') {
                p2p_zrtp_confirm(g_node, 0);
                g_zrtp_waiting = 0;
            } else {
                printf("Please type 'y' or 'n' to confirm SAS.\n> ");
                fflush(stdout);
            }
            continue;
        }

        // Normal command processing. 正常命令处理。
        if (strncmp(line, "/peer", 5) == 0) {
            char ip[64];
            int port;
            if (sscanf(line + 5, "%63s %d", ip, &port) == 2) {
                strncpy(peer_ip, ip, sizeof(peer_ip)-1);
                peer_port = (uint16_t)port;
                send_mode = 1;
                printf("Peer address set to %s:%d\n", peer_ip, peer_port);
            } else {
                printf("Usage: /peer <ip> <port>\n");
            }
        }
        else if (strncmp(line, "/key", 4) == 0) {
            char key[65];
            if (sscanf(line + 4, "%64s", key) == 1 && strlen(key) == 64) {
                uint8_t peer_pub[32];
                for (int i = 0; i < 32; i++) {
                    unsigned int val;
                    sscanf(key + i*2, "%02x", &val);
                    peer_pub[i] = (uint8_t)val;
                }
                p2p_set_peer_key(node, peer_pub);
                printf("Peer key set. Starting ZRTP exchange...\n");
                p2p_zrtp_start_exchange(node, on_sas_ready, on_zrtp_result, NULL);
            } else {
                printf("Invalid key (must be 64 hex chars)\n");
            }
        }
        else if (strcmp(line, "/dht") == 0) {
            // Print DHT routing table. 打印 DHT 路由表。
            dht_print(p2p_get_dht(node));
        }
        else if (strcmp(line, "/exit") == 0) {
            break;
        }
        else if (send_mode && p2p_is_peer_ready(node)) {
            p2p_send(node, peer_ip, peer_port, (uint8_t*)line, strlen(line));
        }
        else if (!send_mode) {
            printf("Send mode not active. Use /peer <ip> <port> first.\n");
        }
        else if (!p2p_is_peer_ready(node)) {
            printf("Encryption not ready. Use /key <64hex> first.\n");
        }

        printf("> ");
        fflush(stdout);
    }

    p2p_destroy(node);
    return 0;
}