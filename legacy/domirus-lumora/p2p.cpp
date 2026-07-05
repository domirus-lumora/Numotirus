// core/p2p/p2p.cpp
// P2P network layer implementation with integrated transport (DHT + Mesh).
// P2P 网络层实现，集成传输层（DHT + Mesh）。
// SPDX-License-Identifier: Apache-2.0

#include "p2p.h"
#include "../crypto/crypto_c.h"
#include "kcp/ikcp.h"
#include "../protocol/zrtp.h"
#include "nat_traversal.hpp"
#include "dht_c.h"
#include "../transport/transport.hpp"

#include <sodium.h>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include <iostream>
#include <memory>
#include <thread>
#include <chrono>
#include <mutex>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
typedef SOCKET socket_t;
#define CLOSE closesocket
#define INVALID_SOCK INVALID_SOCKET
static IUINT32 iclock(void) { return (IUINT32)GetTickCount64(); }
static void isleep_ms(DWORD ms) { Sleep(ms); }
#else
#include <arpa/inet.h>
#include <pthread.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#include <netdb.h>
typedef int socket_t;
#define CLOSE close
#define INVALID_SOCK (-1)
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

#define PK_SIZE 32
#define KCP_CONV 0x11223344u
#define KCP_UPDATE_MS 10
#define SELECT_TIMEOUT_MS 100

using namespace numotirus::nat;
using namespace numotirus::transport;

// KCP output callback. KCP 输出回调。
typedef struct {
    socket_t sock;
    struct sockaddr_in peer_addr;
    int peer_addr_valid;
} KcpCtx;

static int kcp_output(const char *buf, int len, ikcpcb *kcp, void *user) {
    (void)kcp;
    KcpCtx *ctx = (KcpCtx *)user;
    if (!ctx->peer_addr_valid) return 0;
    sendto(ctx->sock, buf, len, 0, (struct sockaddr *)&ctx->peer_addr,
           sizeof(ctx->peer_addr));
    return 0;
}

// P2P node structure. P2P 节点结构。
struct P2PNode {
    socket_t sock;
    volatile int running;

    uint8_t pub[PK_SIZE];
    uint8_t sec[PK_SIZE];
    uint8_t peer[PK_SIZE];
    int peer_ready;

    void (*on_message)(const char *ip, uint16_t port, const uint8_t *data, size_t len);

    ikcpcb *kcp;
    KcpCtx kcp_ctx;

    char last_peer_ip[INET_ADDRSTRLEN];
    uint16_t last_peer_port;

    zrtp_session_t *zrtp;
    zrtp_sas_callback on_sas_cb;
    zrtp_result_callback on_result_cb;
    void *zrtp_user_data;
    int zrtp_exchanging;

    void *dht;

    void *nat;
    uint16_t nat_port;
    int nat_initialized;

    // Transport layer (C++ object).
    std::unique_ptr<CombinedTransport> transport;
    uint8_t node_id[20];

#ifdef _WIN32
    HANDLE th;
    HANDLE kcp_th;
    CRITICAL_SECTION kcp_mutex;
#else
    pthread_t th;
    pthread_t kcp_th;
    pthread_mutex_t kcp_mutex;
#endif
};

// Mutex wrappers. 互斥锁封装。
static void kcp_lock(P2PNode *n) {
#ifdef _WIN32
    EnterCriticalSection(&n->kcp_mutex);
#else
    pthread_mutex_lock(&n->kcp_mutex);
#endif
}

static void kcp_unlock(P2PNode *n) {
#ifdef _WIN32
    LeaveCriticalSection(&n->kcp_mutex);
#else
    pthread_mutex_unlock(&n->kcp_mutex);
#endif
}

// KCP update timer thread. KCP 更新定时线程。
#ifdef _WIN32
static DWORD WINAPI kcp_update_loop(LPVOID arg) {
#else
static void *kcp_update_loop(void *arg) {
#endif
    P2PNode *n = (P2PNode *)arg;
    while (n->running) {
        isleep_ms(KCP_UPDATE_MS);
        kcp_lock(n);
        if (n->kcp) ikcp_update(n->kcp, iclock());
        kcp_unlock(n);
    }
    return 0;
}

// Receive thread.
// 接收线程。
#ifdef _WIN32
static DWORD WINAPI recv_loop(LPVOID arg) {
#else
static void *recv_loop(void *arg) {
#endif
    P2PNode *n = (P2PNode *)arg;
    uint8_t udp_buf[4096];

    while (n->running) {
        fd_set rset;
        FD_ZERO(&rset);
        FD_SET(n->sock, &rset);
        struct timeval tv = {0, SELECT_TIMEOUT_MS * 1000};
        int sel = select((int)n->sock + 1, &rset, NULL, NULL, &tv);
        if (sel <= 0) continue;

        struct sockaddr_in from;
#ifdef _WIN32
        int fromlen = sizeof(from);
#else
        socklen_t fromlen = sizeof(from);
#endif
        int udp_len = recvfrom(n->sock, (char *)udp_buf, sizeof(udp_buf), 0,
                               (struct sockaddr *)&from, &fromlen);
        if (udp_len <= 0) continue;

        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &from.sin_addr, ip_str, sizeof(ip_str));
        uint16_t port = ntohs(from.sin_port);

        strncpy(n->last_peer_ip, ip_str, sizeof(n->last_peer_ip) - 1);
        n->last_peer_port = port;

        // Feed to KCP.
        kcp_lock(n);
        if (n->kcp) ikcp_input(n->kcp, (char *)udp_buf, udp_len);
        kcp_unlock(n);

        // Decrypt KCP packets.
        while (1) {
            uint8_t kcp_buf[4096];
            kcp_lock(n);
            int klen = n->kcp ? ikcp_recv(n->kcp, (char *)kcp_buf, sizeof(kcp_buf)) : -1;
            kcp_unlock(n);
            if (klen <= 0) break;

            uint8_t *plain = NULL;
            size_t plen = 0;
            if (crypto_decrypt_private(kcp_buf, (size_t)klen, n->sec, &plain, &plen) == 0) {
                if (n->dht) {
                    dht_add_node(n->dht, n->peer, ip_str, port, 0);
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

// Helper to convert uint8_t[20] to NodeId.
// 辅助函数：将 uint8_t[20] 转换为 NodeId。
static NodeId MakeNodeId(const uint8_t* data) {
    NodeId id;
    std::memcpy(id.data(), data, 20);
    return id;
}

// Create a P2P node. 创建 P2P 节点。
P2PNode *p2p_create(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa;
    static int ws_init = 0;
    if (!ws_init) {
        WSAStartup(MAKEWORD(2, 2), &wsa);
        ws_init = 1;
    }
#endif

    if (sodium_init() < 0) return NULL;

    P2PNode *n = (P2PNode *)calloc(1, sizeof(P2PNode));
    if (!n) return NULL;

#ifdef _WIN32
    InitializeCriticalSection(&n->kcp_mutex);
#else
    pthread_mutex_init(&n->kcp_mutex, NULL);
#endif

    n->sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (n->sock == INVALID_SOCK) {
        free(n);
        return NULL;
    }

    int reuse = 1;
    setsockopt(n->sock, SOL_SOCKET, SO_REUSEADDR, (char *)&reuse, sizeof(reuse));

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    if (bind(n->sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        CLOSE(n->sock);
        free(n);
        return NULL;
    }

    CryptoKeypair *kp = crypto_keypair_generate();
    if (!kp) {
        CLOSE(n->sock);
        free(n);
        return NULL;
    }
    memcpy(n->pub, crypto_keypair_get_public(kp), PK_SIZE);
    memcpy(n->sec, crypto_keypair_get_secret(kp), PK_SIZE);
    crypto_keypair_free(kp);

    n->kcp_ctx.sock = n->sock;
    n->kcp_ctx.peer_addr_valid = 0;

    n->kcp = ikcp_create(KCP_CONV, &n->kcp_ctx);
    if (!n->kcp) {
        CLOSE(n->sock);
        free(n);
        return NULL;
    }

    ikcp_setoutput(n->kcp, kcp_output);
    ikcp_nodelay(n->kcp, 1, 10, 2, 1);
    ikcp_wndsize(n->kcp, 128, 128);

    n->zrtp = NULL;
    n->on_sas_cb = NULL;
    n->on_result_cb = NULL;
    n->zrtp_user_data = NULL;
    n->zrtp_exchanging = 0;

    // Initialize DHT (C wrapper).
    uint8_t dht_id[DHT_ID_SIZE];
    dht_generate_node_id(dht_id);
    n->dht = dht_create(dht_id);

    // Initialize NAT traversal.
    n->nat_port = port + 1;
    n->nat_initialized = 0;
    n->nat = nullptr;
    try {
        auto nat_ptr = std::make_unique<NatTraversal>();
        nat_ptr->Initialize("stun.l.google.com", 19302);
        nat_ptr->SetLocalPort(n->nat_port);
        n->nat = nat_ptr.release();
        n->nat_initialized = 1;
    } catch (...) {
        n->nat = nullptr;
        n->nat_initialized = 0;
    }

    // Generate 20-byte Node ID from public key (hash).
    crypto_generichash(n->node_id, 20, n->pub, 32, NULL, 0);

    // Create transport layer (C++ object).
    try {
        NodeId node_id_obj = MakeNodeId(n->node_id);
        n->transport = std::make_unique<CombinedTransport>(n->sock, node_id_obj);
    } catch (...) {
        std::cout << "[P2P] Warning: transport creation failed.\n";
        n->transport = nullptr;
    }

    return n;
}

// Start the node. 启动节点。
int p2p_start(P2PNode *n) {
    if (!n) return -1;
    n->running = 1;

    // Start transport layer.
    if (n->transport) {
        n->transport->Start();
    }

    // Bootstrap DHT from public BitTorrent nodes.
    if (n->dht && n->sock != INVALID_SOCK) {
        std::cout << "[P2P] Bootstrapping DHT from public BitTorrent nodes...\n";
        std::cout << "[P2P] This may take a few seconds...\n";

        uint8_t dht_id[DHT_ID_SIZE];
        dht_generate_node_id(dht_id);

        int added = dht_bootstrap_with_spread(n->dht, n->sock, dht_id);

        if (added > 0) {
            std::cout << "[P2P] DHT bootstrap complete: " << added << " nodes added.\n";
            int size = dht_get_size(n->dht);
            std::cout << "[P2P] DHT routing table now has " << size << " nodes.\n";
        } else {
            std::cout << "[P2P] DHT bootstrap failed. Routing table may be empty.\n";
            std::cout << "[P2P] Try again later or check network connectivity.\n";
        }
    }

    // Start threads.
#ifdef _WIN32
    n->th = CreateThread(NULL, 0, recv_loop, n, 0, NULL);
    if (!n->th) return -1;
    n->kcp_th = CreateThread(NULL, 0, kcp_update_loop, n, 0, NULL);
    if (!n->kcp_th) return -1;
#else
    if (pthread_create(&n->th, NULL, recv_loop, n) != 0) return -1;
    if (pthread_create(&n->kcp_th, NULL, kcp_update_loop, n) != 0) return -1;
#endif
    return 0;
}

// Set message callback. 设置消息回调。
void p2p_set_callback(P2PNode *n, void (*cb)(const char *, uint16_t, const uint8_t *, size_t)) {
    if (!n) return;
    n->on_message = cb;

    if (n->transport) {
        // Capture raw pointer safely.
        P2PNode* raw = n;
        n->transport->SetReceiveCallback(
            [raw](const NodeId& from, const std::string& src_ip, uint16_t src_port,
                  const uint8_t* data, size_t len) {
                (void)from;
                if (raw && raw->on_message) {
                    raw->on_message(src_ip.c_str(), src_port, data, len);
                }
            }
        );
    }
}

// Get own public key. 获取自己的公钥。
const uint8_t *p2p_get_public_key(P2PNode *n) {
    return n ? n->pub : NULL;
}

// Set peer's public key. 设置对方公钥。
int p2p_set_peer_key(P2PNode *n, const uint8_t *k) {
    if (!n) return -1;
    memcpy(n->peer, k, PK_SIZE);
    n->peer_ready = 1;
    return 0;
}

// Check if peer key is set. 检查是否已设置对方公钥。
int p2p_is_peer_ready(P2PNode *n) {
    return n ? n->peer_ready : 0;
}

// Send encrypted message.
// 发送加密消息。
int p2p_send(P2PNode *n, const char *ip, uint16_t port, const uint8_t *data, size_t len) {
    if (!n || !n->peer_ready) return -1;

    char resolved_ip[INET_ADDRSTRLEN] = {0};
    uint16_t resolved_port = 0;

    if (ip != NULL && port != 0) {
        strncpy(resolved_ip, ip, INET_ADDRSTRLEN - 1);
        resolved_port = port;
    } else if (n->transport) {
        uint8_t target_node_id[20];
        crypto_generichash(target_node_id, 20, n->peer, 32, NULL, 0);
        NodeId target_id = MakeNodeId(target_node_id);
        auto result = n->transport->Resolve(target_id);
        if (!result) {
            std::cout << "[P2P] Failed to resolve peer Node ID.\n";
            return -1;
        }
        strncpy(resolved_ip, result->first.c_str(), INET_ADDRSTRLEN - 1);
        resolved_port = result->second;
    } else {
        return -1;
    }

    kcp_lock(n);
    n->kcp_ctx.peer_addr.sin_family = AF_INET;
    n->kcp_ctx.peer_addr.sin_port = htons(resolved_port);
    inet_pton(AF_INET, resolved_ip, &n->kcp_ctx.peer_addr.sin_addr);
    n->kcp_ctx.peer_addr_valid = 1;
    kcp_unlock(n);

    uint8_t *cipher = NULL;
    size_t clen = 0;
    if (crypto_encrypt_public(data, len, n->peer, &cipher, &clen) != 0) return -1;

    kcp_lock(n);
    int ret = ikcp_send(n->kcp, (char *)cipher, (int)clen);
    kcp_unlock(n);

    free(cipher);
    return (ret >= 0) ? 0 : -1;
}

// ZRTP key exchange.
int p2p_zrtp_start_exchange(P2PNode *n,
                            zrtp_sas_callback on_sas,
                            zrtp_result_callback on_result,
                            void *user_data) {
    if (!n || !n->peer_ready) {
        std::cout << "Peer key not set. 未设置对方公钥。\n";
        return -1;
    }
    if (n->zrtp_exchanging) {
        std::cout << "ZRTP already in progress. ZRTP 已在交换中。\n";
        return -1;
    }

    if (!n->zrtp) {
        n->zrtp = zrtp_session_new();
        if (!n->zrtp) return -1;
    }

    zrtp_session_set_keypair(n->zrtp, n->pub, n->sec);
    zrtp_session_set_peer_public(n->zrtp, n->peer);

    if (zrtp_session_key_exchange(n->zrtp) != ZRTP_SUCCESS) {
        std::cout << "ZRTP key exchange failed. ZRTP 密钥交换失败。\n";
        return -1;
    }

    n->on_sas_cb = on_sas;
    n->on_result_cb = on_result;
    n->zrtp_user_data = user_data;
    n->zrtp_exchanging = 1;

    const char *sas = zrtp_session_get_sas(n->zrtp);
    if (n->on_sas_cb) {
        n->on_sas_cb(sas, n->zrtp_user_data);
    }

    return 0;
}

void p2p_zrtp_confirm(P2PNode *n, int confirmed) {
    if (!n || !n->zrtp_exchanging) return;

    if (confirmed) {
        zrtp_session_mark_verified(n->zrtp);
        std::cout << "✅ Peer verified. 对方已验证。\n";
    } else {
        std::cout << "❌ Verification rejected. 验证被拒绝。\n";
    }

    if (n->on_result_cb) {
        n->on_result_cb(confirmed, n->zrtp_user_data);
    }

    n->zrtp_exchanging = 0;
}

void *p2p_get_dht(P2PNode *n) {
    return n ? n->dht : NULL;
}

// NAT traversal.
int p2p_nat_start_traversal(P2PNode *n, const char *peer_candidates_str) {
    if (!n || !n->nat || !n->nat_initialized) {
        std::cout << "[NAT] NAT not initialized. NAT 未初始化。\n";
        return -1;
    }

    if (!peer_candidates_str || std::strlen(peer_candidates_str) == 0) {
        std::cout << "[NAT] No candidates provided. 未提供候选地址。\n";
        return -1;
    }

    std::cout << "[NAT] Parsing candidates: " << peer_candidates_str << "\n";

    auto *nat = reinterpret_cast<NatTraversal *>(n->nat);
    std::vector<Candidate> candidates;
    std::string str = peer_candidates_str;

    while (!str.empty() && (str.back() == ' ' || str.back() == '\n' || str.back() == '\r')) {
        str.pop_back();
    }

    size_t pos = 0;
    while (pos < str.length()) {
        size_t comma = str.find(',', pos);
        std::string item = str.substr(pos, comma - pos);
        pos = (comma == std::string::npos) ? str.length() : comma + 1;

        while (!item.empty() && item.front() == ' ') item.erase(0, 1);
        while (!item.empty() && item.back() == ' ') item.pop_back();

        size_t colon = item.find(':');
        if (colon == std::string::npos) {
            std::cout << "[NAT] Invalid candidate format: " << item << " (missing ':')\n";
            continue;
        }

        std::string ip = item.substr(0, colon);
        std::string port_str = item.substr(colon + 1);

        while (!ip.empty() && ip.back() == ' ') ip.pop_back();
        while (!port_str.empty() && port_str.front() == ' ') port_str.erase(0, 1);

        if (ip.empty() || port_str.empty()) {
            std::cout << "[NAT] Invalid candidate: ip or port empty\n";
            continue;
        }

        int port_int = std::atoi(port_str.c_str());
        if (port_int <= 0 || port_int > 65535) {
            std::cout << "[NAT] Invalid port: " << port_str << "\n";
            continue;
        }

        Candidate c;
        c.type = CandidateType::kPublic;
        c.ip = ip;
        c.port = static_cast<uint16_t>(port_int);
        c.priority = 100;
        candidates.push_back(c);
        std::cout << "[NAT] Added candidate: " << ip << ":" << c.port << "\n";
    }

    if (candidates.empty()) {
        std::cout << "[NAT] No valid candidates. 无有效候选地址。\n";
        return -1;
    }

    nat->StartTraversal(candidates, [](bool success, const Candidate &peer) {
        if (success) {
            std::cout << "[NAT] ✅ Traversal succeeded: " << peer.ip << ":" << peer.port << "\n";
        } else {
            std::cout << "[NAT] ❌ Traversal failed, fallback needed. 穿透失败，需要中继。\n";
        }
    });

    return 0;
}

// Destroy node.
void p2p_destroy(P2PNode *n) {
    if (!n) return;

    n->running = 0;

#ifdef _WIN32
    if (n->th) {
        WaitForSingleObject(n->th, 2000);
        CloseHandle(n->th);
    }
    if (n->kcp_th) {
        WaitForSingleObject(n->kcp_th, 2000);
        CloseHandle(n->kcp_th);
    }
    DeleteCriticalSection(&n->kcp_mutex);
#else
    if (n->th) pthread_join(n->th, NULL);
    if (n->kcp_th) pthread_join(n->kcp_th, NULL);
    pthread_mutex_destroy(&n->kcp_mutex);
#endif

    kcp_lock(n);
    if (n->kcp) {
        ikcp_release(n->kcp);
        n->kcp = NULL;
    }
    kcp_unlock(n);

    if (n->zrtp) {
        zrtp_session_free(n->zrtp);
        n->zrtp = NULL;
    }

    if (n->dht) {
        dht_destroy(n->dht);
        n->dht = NULL;
    }

    if (n->nat) {
        delete static_cast<NatTraversal*>(n->nat);
        n->nat = NULL;
        n->nat_initialized = 0;
    }

    if (n->transport) {
        n->transport->Stop();
        n->transport.reset();
    }

    if (n->sock != INVALID_SOCK) CLOSE(n->sock);
    free(n);
}

/*  Co-authored-by: domirus-lumora*/