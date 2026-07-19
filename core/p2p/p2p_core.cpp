// core/p2p/p2p_core.cpp
// P2P core implementation: lifecycle, socket, KCP, receive loop, DHT bootstrap.
// P2P 核心实现：生命周期、套接字、KCP、接收循环、DHT 引导。
// SPDX-License-Identifier: Apache-2.0

#include "p2p.h"
#include "../crypto/crypto_c.h"
#include "kcp/ikcp.h"
#include "dht_c.h"
#include "../protocol/noise.hpp"
#include "../transport/transport.hpp"

#include <sodium.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <memory>

// Constants used throughout the P2P layer.
// P2P 层使用的常量。
#define PK_SIZE 32
#define KCP_CONV 0x11223344u
#define KCP_UPDATE_MS 10
#define SELECT_TIMEOUT_MS 100

// Ensure INVALID_SOCKET is defined on all platforms.
// 确保在所有平台上都定义了 INVALID_SOCKET。
#ifndef INVALID_SOCKET
#define INVALID_SOCKET (-1)
#endif

// Platform-specific includes and type definitions.
// 平台相关的包含和类型定义。
#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#include <fcntl.h>
typedef SOCKET socket_t;
#define CLOSE_SOCKET closesocket
static IUINT32 iclock(void) { return (IUINT32)GetTickCount64(); }
static void isleep_ms(unsigned int ms) { Sleep(ms); }
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <sys/time.h>
#include <unistd.h>
#include <netdb.h>
#include <fcntl.h>
typedef int socket_t;
#define CLOSE_SOCKET close
static IUINT32 iclock(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (IUINT32)(tv.tv_sec * 1000 + tv.tv_usec / 1000);
}
static void isleep_ms(unsigned int ms) {
    struct timespec ts = {ms / 1000, (ms % 1000) * 1000000L};
    nanosleep(&ts, NULL);
}
#endif

using namespace numotirus::transport;

// KCP output callback context.
// KCP 输出回调上下文。
typedef struct {
    socket_t sock;
    struct sockaddr_in peer_addr;
    int peer_addr_valid;
} KcpCtx;

// KCP output callback: sends data over UDP.
// KCP 输出回调：通过 UDP 发送数据。
static int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    (void)kcp;
    KcpCtx *ctx = (KcpCtx *)user;
    if (!ctx->peer_addr_valid) return 0;
    sendto(ctx->sock, buf, len, 0, (struct sockaddr *)&ctx->peer_addr,
           sizeof(ctx->peer_addr));
    return 0;
}

// Mutex operations for KCP thread safety.
// KCP 线程安全的互斥锁操作。
#ifdef _WIN32
static void kcp_lock_init(P2PNode* n) { InitializeCriticalSection((CRITICAL_SECTION*)n->kcp_mutex); }
static void kcp_lock_destroy(P2PNode* n) { DeleteCriticalSection((CRITICAL_SECTION*)n->kcp_mutex); }
void kcp_lock(P2PNode* n) { EnterCriticalSection((CRITICAL_SECTION*)n->kcp_mutex); }
void kcp_unlock(P2PNode* n) { LeaveCriticalSection((CRITICAL_SECTION*)n->kcp_mutex); }
#else
static void kcp_lock_init(P2PNode* n) { pthread_mutex_init((pthread_mutex_t*)n->kcp_mutex, NULL); }
static void kcp_lock_destroy(P2PNode* n) { pthread_mutex_destroy((pthread_mutex_t*)n->kcp_mutex); }
static void kcp_lock(P2PNode* n) { pthread_mutex_lock((pthread_mutex_t*)n->kcp_mutex); }
static void kcp_unlock(P2PNode* n) { pthread_mutex_unlock((pthread_mutex_t*)n->kcp_mutex); }
#endif

// Helper: convert uint8_t[20] to NodeId (for transport layer).
// 辅助函数：将 uint8_t[20] 转换为 NodeId（用于传输层）。
static NodeId MakeNodeId(const uint8_t* data) {
    NodeId id;
    memcpy(id.data(), data, 20);
    return id;
}

// DHT bootstrap thread function (runs in background).
// DHT 引导线程函数（后台运行）。
#ifdef _WIN32
static DWORD WINAPI dht_bootstrap_loop(LPVOID arg) {
#else
static void* dht_bootstrap_loop(void* arg) {
#endif
    P2PNode* n = (P2PNode*)arg;
    if (!n || !n->dht || n->sock == INVALID_SOCKET) {
        return 0;
    }

    std::cout << "[P2P] DHT bootstrapping started in background. DHT 引导已在后台启动。\n";

    uint8_t dht_id[DHT_ID_SIZE];
    dht_generate_node_id(dht_id);
    int added = dht_bootstrap_with_spread(n->dht, n->sock, dht_id);

    if (added > 0) {
        std::cout << "[P2P] DHT bootstrap complete: " << added
                  << " nodes added. DHT 引导完成：已添加 " << added << " 个节点。\n";
        std::cout << "[P2P] DHT routing table now has "
                  << dht_get_size(n->dht)
                  << " nodes. DHT 路由表现在有 " << dht_get_size(n->dht) << " 个节点。\n";
    } else {
        std::cout << "[P2P] DHT bootstrap failed. Routing table may be empty. "
                     "DHT 引导失败，路由表可能为空。\n";
        std::cout << "[P2P] Try again later or check network connectivity. "
                     "请再试一次或检查你的网络连接。\n";
    }

    return 0;
}

// KCP update thread: periodically updates KCP state.
// KCP 更新线程：定期更新 KCP 状态。
#ifdef _WIN32
static DWORD WINAPI kcp_update_loop(LPVOID arg) {
#else
static void* kcp_update_loop(void* arg) {
#endif
    P2PNode* n = (P2PNode*)arg;
    while (n->running) {
        isleep_ms(KCP_UPDATE_MS);
        kcp_lock(n);
        if (n->kcp) ikcp_update((ikcpcb*)n->kcp, iclock());
        kcp_unlock(n);
    }
    return 0;
}

// Receive thread: reads UDP packets, feeds KCP, decrypts and dispatches messages.
// 接收线程：读取 UDP 包，喂给 KCP，解密并分发消息。
#ifdef _WIN32
static DWORD WINAPI recv_loop(LPVOID arg) {
#else
static void* recv_loop(void* arg) {
#endif
    P2PNode* n = (P2PNode*)arg;
    uint8_t udp_buf[4096];

    while (n->running) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(n->sock, &rset);
        struct timeval tv = {0, SELECT_TIMEOUT_MS * 1000};
        int sel = select(n->sock + 1, &rset, NULL, NULL, &tv);
        if (sel <= 0) continue;

        struct sockaddr_in from;
#ifdef _WIN32
        int fromlen = sizeof(from);
#else
        socklen_t fromlen = sizeof(from);
#endif
        int udp_len = recvfrom(n->sock, (char*)udp_buf, sizeof(udp_buf), 0,
                               (struct sockaddr*)&from, &fromlen);
        if (udp_len <= 0) continue;

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip_str, sizeof(ip_str));
        uint16_t port = ntohs(from.sin_port);

        strncpy(n->last_peer_ip, ip_str, sizeof(n->last_peer_ip) - 1);
        n->last_peer_port = port;

        kcp_lock(n);
        if (n->kcp) ikcp_input((ikcpcb*)n->kcp, (char*)udp_buf, udp_len);
        kcp_unlock(n);

        // Extract decrypted messages from KCP receive queue.
        // 从 KCP 接收队列提取解密后的消息。
        while (1) {
            uint8_t kcp_buf[4096];
            kcp_lock(n);
            int klen = n->kcp ? ikcp_recv((ikcpcb*)n->kcp, (char*)kcp_buf, sizeof(kcp_buf)) : -1;
            kcp_unlock(n);
            if (klen <= 0) break;

            uint8_t* plain = NULL;
            size_t plen = 0;
            if (crypto_decrypt_private(kcp_buf, (size_t)klen, n->sec_key, &plain, &plen) == 0) {
                // Add peer to DHT using the hash of its public key.
                // 使用对方公钥的哈希将其加入 DHT。
                if (n->dht) {
                    uint8_t dht_id[20];
                    crypto_generichash(dht_id, 20, n->peer_key, 32, NULL, 0);
                    dht_add_node(n->dht, dht_id, ip_str, port, 0);
                }
                if (n->on_message) {
                    n->on_message(ip_str, port, plain, plen);
                }
                free(plain);
            }
        }
    }
    return 0;
}

// Create a P2P node listening on the given UDP port.
// 创建在指定 UDP 端口上监听的 P2P 节点。
P2PNode* p2p_create(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    static int ws_init = 0;
    if (!ws_init) {
        WSAStartup(MAKEWORD(2, 2), &wsa);
        ws_init = 1;
    }
#endif

    if (sodium_init() < 0) return NULL;

    P2PNode* n = (P2PNode*)calloc(1, sizeof(P2PNode));
    if (!n) return NULL;

#ifdef _WIN32
    n->kcp_mutex = malloc(sizeof(CRITICAL_SECTION));
    if (!n->kcp_mutex) { free(n); return NULL; }
#else
    n->kcp_mutex = malloc(sizeof(pthread_mutex_t));
    if (!n->kcp_mutex) { free(n); return NULL; }
#endif
    kcp_lock_init(n);

    n->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (n->sock == INVALID_SOCKET) {
        p2p_destroy(n);
        return NULL;
    }

    int reuse = 1;
    setsockopt(n->sock, SOL_SOCKET, SO_REUSEADDR, (char*)&reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(n->sock, (struct sockaddr*)&addr, sizeof(addr)) != 0) {
        p2p_destroy(n);
        return NULL;
    }

    CryptoKeypair* kp = crypto_keypair_generate();
    if (!kp) {
        p2p_destroy(n);
        return NULL;
    }
    memcpy(n->pub_key, crypto_keypair_get_public(kp), PK_SIZE);
    memcpy(n->sec_key, crypto_keypair_get_secret(kp), PK_SIZE);
    crypto_keypair_free(kp);

    KcpCtx* ctx = (KcpCtx*)malloc(sizeof(KcpCtx));
    if (!ctx) {
        p2p_destroy(n);
        return NULL;
    }
    ctx->sock = n->sock;
    ctx->peer_addr_valid = 0;
    n->kcp_ctx = ctx;

    ikcpcb* kcp = ikcp_create(KCP_CONV, ctx);
    if (!kcp) {
        p2p_destroy(n);
        return NULL;
    }
    ikcp_setoutput(kcp, kcp_output);
    ikcp_nodelay(kcp, 1, 10, 2, 1);
    ikcp_wndsize(kcp, 128, 128);
    n->kcp = kcp;

    // Noise session (initially null).
    // Noise 会话（初始为空）。
    n->noise_session = NULL;
    n->on_noise_sas = NULL;
    n->on_noise_result = NULL;
    n->noise_user_data = NULL;
    n->noise_exchanging = 0;

    // Initialize DHT with a random node ID.
    // 用随机节点 ID 初始化 DHT。
    uint8_t dht_id[DHT_ID_SIZE];
    dht_generate_node_id(dht_id);
    n->dht = dht_create(dht_id);

    // Generate a 20-byte Node ID from the public key (for transport layer).
    // 从公钥生成 20 字节的节点 ID（用于传输层）。
    crypto_generichash(n->node_id, 20, n->pub_key, 32, NULL, 0);

    // Create the combined transport layer (DHT + Mesh).
    // 创建组合传输层（DHT + Mesh）。
    try {
        NodeId node_id_obj = MakeNodeId(n->node_id);
        n->transport = new CombinedTransport(n->sock, node_id_obj);
    } catch (...) {
        std::cout << "[P2P] Warning: transport creation failed.\n";
        n->transport = NULL;
    }

    return n;
}

// Start the node: starts DHT bootstrap in background and internal threads.
// 启动节点：在后台启动 DHT 引导并启动内部线程。
int p2p_start(P2PNode* n) {
    if (!n) return -1;
    n->running = 1;

    // Start the transport layer.
    // 启动传输层。
    if (n->transport) {
        ((CombinedTransport*)n->transport)->Start();
    }

    // Start DHT bootstrap in a separate thread (non-blocking).
    // 在独立线程中启动 DHT 引导（非阻塞）。
    if (n->dht && n->sock != INVALID_SOCKET) {
#ifdef _WIN32
        n->dht_bootstrap_thread = CreateThread(NULL, 0, dht_bootstrap_loop, n, 0, NULL);
        if (!n->dht_bootstrap_thread) {
            std::cout << "[P2P] Warning: failed to create DHT bootstrap thread. "
                         "警告：创建 DHT 引导线程失败。\n";
        }
#else
        pthread_t* bootstrap = (pthread_t*)malloc(sizeof(pthread_t));
        if (!bootstrap) {
            std::cout << "[P2P] Warning: failed to allocate DHT bootstrap thread. "
                         "警告：分配 DHT 引导线程失败。\n";
        } else if (pthread_create(bootstrap, NULL, dht_bootstrap_loop, n) != 0) {
            free(bootstrap);
            std::cout << "[P2P] Warning: failed to start DHT bootstrap thread. "
                         "警告：启动 DHT 引导线程失败。\n";
        } else {
            n->dht_bootstrap_thread = bootstrap;
        }
#endif
    }

    // Start the receive and KCP update threads.
    // 启动接收线程和 KCP 更新线程。
#ifdef _WIN32
    n->recv_thread = CreateThread(NULL, 0, recv_loop, n, 0, NULL);
    if (!n->recv_thread) return -1;
    n->kcp_thread = CreateThread(NULL, 0, kcp_update_loop, n, 0, NULL);
    if (!n->kcp_thread) return -1;
#else
    pthread_t* recv = (pthread_t*)malloc(sizeof(pthread_t));
    pthread_t* kcp = (pthread_t*)malloc(sizeof(pthread_t));
    if (!recv || !kcp) return -1;
    if (pthread_create(recv, NULL, recv_loop, n) != 0) return -1;
    if (pthread_create(kcp, NULL, kcp_update_loop, n) != 0) return -1;
    n->recv_thread = recv;
    n->kcp_thread = kcp;
#endif
    return 0;
}

// Destroy the node: stop threads, release all resources.
// 销毁节点：停止线程，释放所有资源。
void p2p_destroy(P2PNode* n) {
    if (!n) return;

    n->running = 0;

    // Shutdown socket to wake up recvfrom.
    // 关闭套接字以唤醒 recvfrom。
    if (n->sock != INVALID_SOCKET) {
#ifdef _WIN32
        shutdown(n->sock, SD_BOTH);
#else
        shutdown(n->sock, SHUT_RDWR);
#endif
    }

#ifdef _WIN32
    if (n->recv_thread) {
        WaitForSingleObject((HANDLE)n->recv_thread, 2000);
        CloseHandle((HANDLE)n->recv_thread);
    }
    if (n->kcp_thread) {
        WaitForSingleObject((HANDLE)n->kcp_thread, 2000);
        CloseHandle((HANDLE)n->kcp_thread);
    }
    if (n->dht_bootstrap_thread) {
        WaitForSingleObject((HANDLE)n->dht_bootstrap_thread, 2000);
        CloseHandle((HANDLE)n->dht_bootstrap_thread);
    }
#else
    if (n->recv_thread) {
        pthread_join(*(pthread_t*)n->recv_thread, NULL);
        free(n->recv_thread);
    }
    if (n->kcp_thread) {
        pthread_join(*(pthread_t*)n->kcp_thread, NULL);
        free(n->kcp_thread);
    }
    if (n->dht_bootstrap_thread) {
        pthread_join(*(pthread_t*)n->dht_bootstrap_thread, NULL);
        free(n->dht_bootstrap_thread);
    }
#endif

    kcp_lock(n);
    if (n->kcp) {
        ikcp_release((ikcpcb*)n->kcp);
        n->kcp = NULL;
    }
    kcp_unlock(n);

    if (n->kcp_ctx) {
        free(n->kcp_ctx);
        n->kcp_ctx = NULL;
    }
    kcp_lock_destroy(n);
    if (n->kcp_mutex) {
        free(n->kcp_mutex);
        n->kcp_mutex = NULL;
    }

    // Clean up Noise session.
    // 清理 Noise 会话。
    if (n->noise_session) {
        delete static_cast<numotirus::protocol::noise::NoiseSession*>(n->noise_session);
        n->noise_session = NULL;
    }

    if (n->dht) {
        dht_destroy(n->dht);
        n->dht = NULL;
    }

    if (n->transport) {
        ((CombinedTransport*)n->transport)->Stop();
        delete (CombinedTransport*)n->transport;
        n->transport = NULL;
    }

    if (n->sock != INVALID_SOCKET) CLOSE_SOCKET(n->sock);
    free(n);
}

// Get own public key.
// 获取自己的公钥。
const uint8_t* p2p_get_public_key(const P2PNode* n) {
    return n ? n->pub_key : NULL;
}

// Get DHT handle for debugging.
// 获取 DHT 句柄用于调试。
void* p2p_get_dht(P2PNode* n) {
    return n ? n->dht : NULL;
}