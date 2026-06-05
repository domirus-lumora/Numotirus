// core/p2p/p2p.c
// P2P network layer implementation. P2P 网络层实现。

#include "p2p.h"
#include "../crypto/crypto_c.h"
#include <sodium.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
typedef SOCKET socket_t;
#define CLOSE closesocket
#else
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <pthread.h>
typedef int socket_t;
#define CLOSE close
#endif

#define PK_SIZE 32  // 公钥/私钥大小 / Public/secret key size

// P2P node structure. P2P 节点结构。
struct P2PNode {
    socket_t sock;                          // UDP socket. UDP 套接字。
    int running;                            // Running flag. 运行标志。
    uint8_t pub[PK_SIZE];                   // Own public key. 自己的公钥。
    uint8_t sec[PK_SIZE];                   // Own secret key. 自己的私钥。
    uint8_t peer[PK_SIZE];                  // Peer's public key. 对方的公钥。
    int peer_ready;                         // Whether peer key is set. 是否已设置对方公钥。
    void (*on_message)(const char* ip, uint16_t port,
                       const uint8_t* data, size_t len);  // Message callback. 消息回调。
#ifdef _WIN32
    HANDLE th;                              // Receive thread handle. 接收线程句柄。
#else
    pthread_t th;
#endif
};

// Receive thread function. 接收线程函数。
#ifdef _WIN32
static DWORD WINAPI recv_loop(LPVOID arg) {
#else
static void* recv_loop(void* arg) {
#endif
    P2PNode* n = (P2PNode*)arg;
    uint8_t buf[4096];
    struct sockaddr_in from;

    while (n->running) {
        int fromlen = sizeof(from);
        int len = recvfrom(n->sock, (char*)buf, sizeof(buf), 0,
                           (struct sockaddr*)&from, &fromlen);
        if (len <= 0) continue;

        // Decrypt received message. 解密收到的消息。
        uint8_t* plain = NULL;
        size_t plen = 0;
        if (crypto_decrypt_private(buf, len, n->sec, &plain, &plen) == 0) {
            if (n->on_message) {
                char ip[16];
                inet_ntop(AF_INET, &from.sin_addr, ip, sizeof(ip));
                n->on_message(ip, ntohs(from.sin_port), plain, plen);
            }
            free(plain);
        }
    }
    return 0;
}

// Create a P2P node. 创建 P2P 节点。
P2PNode* p2p_create(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    static int ws_init = 0;
    if (!ws_init) {
        WSAStartup(MAKEWORD(2,2), &wsa);
        ws_init = 1;
    }
#endif

    if (sodium_init() < 0) return NULL;

    P2PNode* n = calloc(1, sizeof(P2PNode));
    if (!n) return NULL;

    // Create UDP socket. 创建 UDP 套接字。
    n->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (n->sock == INVALID_SOCKET) {
        free(n);
        return NULL;
    }

    // Allow port reuse. 允许端口重用。
    int reuse = 1;
    setsockopt(n->sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    // Bind to local port. 绑定到本地端口。
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);

    if (bind(n->sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        CLOSE(n->sock);
        free(n);
        return NULL;
    }

    // Generate crypto key pair. 生成加密密钥对。
    CryptoKeypair* kp = crypto_keypair_generate();
    if (!kp) {
        CLOSE(n->sock);
        free(n);
        return NULL;
    }
    memcpy(n->pub, crypto_keypair_get_public(kp), PK_SIZE);
    memcpy(n->sec, crypto_keypair_get_secret(kp), PK_SIZE);
    crypto_keypair_free(kp);

    return n;
}

// Start the node. 启动节点。
int p2p_start(P2PNode* n) {
    if (!n) return -1;
    n->running = 1;
#ifdef _WIN32
    n->th = CreateThread(NULL, 0, recv_loop, n, 0, NULL);
    if (!n->th) return -1;
#else
    if (pthread_create(&n->th, NULL, recv_loop, n) != 0) return -1;
#endif
    return 0;
}

// Set message callback. 设置消息回调。
void p2p_set_callback(P2PNode* n, void (*cb)(const char*, uint16_t, const uint8_t*, size_t)) {
    if (n) n->on_message = cb;
}

// Get own public key. 获取自己的公钥。
const uint8_t* p2p_get_public_key(P2PNode* n) {
    return n->pub;
}

// Set peer's public key. 设置对方的公钥。
int p2p_set_peer_key(P2PNode* n, const uint8_t* k) {
    if (!n) return -1;
    memcpy(n->peer, k, PK_SIZE);
    n->peer_ready = 1;
    return 0;
}

// Check if peer key is set. 检查是否已设置对方公钥。
int p2p_is_peer_ready(P2PNode* n) {
    return n ? n->peer_ready : 0;
}

// Send encrypted message. 发送加密消息。
int p2p_send(P2PNode* n, const char* ip, uint16_t port,
             const uint8_t* data, size_t len) {
    if (!n || !n->peer_ready) return -1;

    // Encrypt message. 加密消息。
    uint8_t* cipher = NULL;
    size_t clen = 0;
    if (crypto_encrypt_public(data, len, n->peer, &cipher, &clen) != 0)
        return -1;

    // Send via UDP. 通过 UDP 发送。
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &addr.sin_addr);

    int sent = sendto(n->sock, (char*)cipher, clen, 0,
                      (struct sockaddr*)&addr, sizeof(addr));
    free(cipher);
    return (sent == (int)clen) ? 0 : -1;
}

// Destroy node and free resources. 销毁节点并释放资源。
void p2p_destroy(P2PNode* n) {
    if (!n) return;
    n->running = 0;
#ifdef _WIN32
    if (n->th) {
        WaitForSingleObject(n->th, 1000);
        CloseHandle(n->th);
    }
#else
    if (n->th) pthread_join(n->th, NULL);
#endif
    if (n->sock != INVALID_SOCKET) CLOSE(n->sock);
    free(n);
}