#include "p2p.h"
#include "../crypto/crypto_c.h"
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

#define PK 32

struct P2PNode {
    socket_t sock;
    int running;

    uint8_t pub[PK];
    uint8_t sec[PK];
    uint8_t peer[PK];
    int peer_ready;

    p2p_on_message cb;

#ifdef _WIN32
    HANDLE th;
#else
    pthread_t th;
#endif
};
static DWORD WINAPI recv_loop(LPVOID arg) {
    P2PNode* n = (P2PNode*)arg;
    uint8_t buf[4096];
    struct sockaddr_in from;
    int fromlen = sizeof(from);

    while (n->running) {
        int len = recvfrom(n->sock, (char*)buf, sizeof(buf), 0,
                           (struct sockaddr*)&from, &fromlen);
        if (len <= 0) continue;

        printf("[DEBUG] Received %d bytes\n", len);

        uint8_t* plain = NULL;
        size_t plen = 0;

        if (crypto_decrypt_private(buf, len, n->sec, &plain, &plen) == 0) {
            if (n->cb) n->cb("peer", 0, plain, plen);
            free(plain);
        }
    }
    return 0;
}


P2PNode* p2p_create(uint16_t port) {
    P2PNode* n = calloc(1, sizeof(P2PNode));

    n->sock = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in a;
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = INADDR_ANY;
    a.sin_port = htons(port);

    bind(n->sock, (struct sockaddr*)&a, sizeof(a));

    CryptoKeypair* kp = crypto_keypair_generate();
    memcpy(n->pub, crypto_keypair_get_public(kp), PK);
    memcpy(n->sec, crypto_keypair_get_secret(kp), PK);
    crypto_keypair_free(kp);

    return n;
}

int p2p_start(P2PNode* n) {
    n->running = 1;

#ifdef _WIN32
    n->th = CreateThread(NULL, 0, recv_loop, n, 0, NULL);
#else
    pthread_create(&n->th, NULL, recv_loop, n);
#endif

    return 0;
}

const uint8_t* p2p_get_public_key(P2PNode* n) {
    return n->pub;
}

int p2p_set_peer_key(P2PNode* n, const uint8_t* k) {
    memcpy(n->peer, k, PK);
    n->peer_ready = 1;
    return 0;
}

int p2p_send(P2PNode* n,
             const char* ip,
             uint16_t port,
             const uint8_t* data,
             size_t len) {

    uint8_t* cipher = NULL;
    size_t clen = 0;

    if (crypto_encrypt_public(data, len, n->peer, &cipher, &clen) != 0)
        return -1;

    struct sockaddr_in a;
    a.sin_family = AF_INET;
    a.sin_port = htons(port);
    inet_pton(AF_INET, ip, &a.sin_addr);

    sendto(n->sock, (char*)cipher, clen, 0,
           (struct sockaddr*)&a, sizeof(a));

    free(cipher);

    printf("[DEBUG] Sending %zu bytes to %s:%d\n", clen, ip, port);
    return 0;
}

void p2p_set_callback(P2PNode* n, p2p_on_message cb) {
    n->cb = cb;
}

void p2p_destroy(P2PNode* n) {
    n->running = 0;
    CLOSE(n->sock);
    free(n);
}