// p2p_chat.c
// Command line chat demo using P2P layer. 使用 P2P 层的命令行聊天演示。

#include "p2p.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <locale.h>

static void on_message(const char* ip, uint16_t port, const uint8_t* data, size_t len) {
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
            printf("Peer key set. Encryption enabled.\n");

            // ZRTP 密钥交换和 SAS 验证
            if (p2p_zrtp_exchange(node) != 0) {
                printf("ZRTP verification failed. Exiting.\n");
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
    printf("  /exit              - quit\n");
    printf("  other text         - send message\n");
    printf("> ");
    fflush(stdout);

    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;

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
                printf("Peer key set. Encryption enabled.\n");
            } else {
                printf("Invalid key (must be 64 hex chars)\n");
            }
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