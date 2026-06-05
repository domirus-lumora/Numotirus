#include "p2p.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void on_msg(const char* ip, uint16_t port,
                    const uint8_t* data, size_t len) {
    printf("\n[%s] %.*s\n", ip, (int)len, data);
    printf("> ");
}

int main(int argc, char** argv) {
    if (argc < 2) return 1;

    uint16_t port = atoi(argv[1]);
    P2PNode* n = p2p_create(port);
    p2p_set_callback(n, on_msg);
    p2p_start(n);

    printf("My public key:\n");
    const uint8_t* p = p2p_get_public_key(n);
    for (int i = 0; i < 32; i++) printf("%02x", p[i]);
    printf("\n");

    // 如果是发送端（提供了目标端口），则输入对方公钥
    if (argc >= 3) {
        printf("Enter peer's public key (64 hex chars): ");
        char hex[65];
        if (scanf("%64s", hex) == 1) {
            uint8_t peer_pub[32];
            for (int i = 0; i < 32; i++) {
                unsigned int val;
                sscanf(hex + i * 2, "%02x", &val);
                peer_pub[i] = (uint8_t)val;
            }
            p2p_set_peer_key(n, peer_pub);
            printf("Peer key set. You can now send messages.\n");
        }
    }

    char line[1024];
    while (fgets(line, sizeof(line), stdin)) {
        line[strcspn(line, "\n")] = 0;
        if (strlen(line) == 0) continue;
        if (argc >= 3) {
            p2p_send(n, "127.0.0.1", 8888, (uint8_t*)line, strlen(line));
        } else {
            // 接收端不需要发送，只是监听
        }
    }

    p2p_destroy(n);
    return 0;
}