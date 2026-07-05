// core/p2p/p2p_session.cpp
// P2P session management: peer key, ZRTP exchange, message sending.
// P2P 会话管理：对端公钥、ZRTP 交换、消息发送。
// SPDX-License-Identifier: Apache-2.0

#include "p2p.h"
#include "../crypto/crypto_c.h"
#include "kcp/ikcp.h"
#include "../protocol/zrtp.h"
#include "../transport/transport.hpp"

#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <memory>
#include <sodium.h>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#endif

using namespace numotirus::transport;

// Mutex operations (forward declarations from p2p_core.cpp).
// 互斥锁操作（来自 p2p_core.cpp 的声明）。
static void kcp_lock(P2PNode* n);
static void kcp_unlock(P2PNode* n);

// Helper: convert uint8_t[20] to NodeId.
// 辅助函数：将 uint8_t[20] 转换为 NodeId。
static NodeId MakeNodeId(const uint8_t* data) {
    NodeId id;
    memcpy(id.data(), data, 20);
    return id;
}

// KCP output callback context.
// KCP 输出回调上下文。
typedef struct {
    int sock;
    struct sockaddr_in peer_addr;
    int peer_addr_valid;
} KcpCtx;

// Set message callback.
// 设置消息回调。
void p2p_set_message_callback(P2PNode* n, p2p_message_callback cb) {
    if (!n) return;
    n->on_message = cb;

    if (n->transport) {
        P2PNode* raw = n;
        ((CombinedTransport*)n->transport)->SetReceiveCallback(
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

// Set peer's public key from raw bytes.
// 从原始字节设置对方的公钥。
int p2p_set_peer_key(P2PNode* n, const uint8_t* key) {
    if (!n || !key) return -1;
    memcpy(n->peer_key, key, P2P_PUBLIC_KEY_SIZE);
    n->peer_ready = 1;
    return 0;
}

// Set peer's public key from hex string.
// 从十六进制字符串设置对方公钥。
int p2p_set_peer_key_hex(P2PNode* n, const char* hex) {
    if (!n || !hex || strlen(hex) != 64) return -1;
    uint8_t key[P2P_PUBLIC_KEY_SIZE];
    for (int i = 0; i < P2P_PUBLIC_KEY_SIZE; ++i) {
        unsigned int val;
        if (sscanf(hex + i * 2, "%02x", &val) != 1) return -1;
        key[i] = (uint8_t)val;
    }
    return p2p_set_peer_key(n, key);
}

// Check if peer key is set.
// 检查是否已设置对方公钥。
int p2p_is_peer_ready(const P2PNode* n) {
    return n ? n->peer_ready : 0;
}

// Send encrypted message.
// 发送加密消息。
int p2p_send(P2PNode* n, const char* ip, uint16_t port, const uint8_t* data, size_t len) {
    if (!n || !n->peer_ready) return -1;

    char resolved_ip[INET_ADDRSTRLEN] = {0};
    uint16_t resolved_port = 0;

    if (ip != NULL && port != 0) {
        strncpy(resolved_ip, ip, INET_ADDRSTRLEN - 1);
        resolved_port = port;
    } else if (n->transport) {
        uint8_t target_node_id[20];
        crypto_generichash(target_node_id, 20, n->peer_key, 32, NULL, 0);
        NodeId target_id = MakeNodeId(target_node_id);
        auto result = ((CombinedTransport*)n->transport)->Resolve(target_id);
        if (!result) {
            std::cout << "[P2P] Failed to resolve peer Node ID.\n";
            return -1;
        }
        strncpy(resolved_ip, result->first.c_str(), INET_ADDRSTRLEN - 1);
        resolved_port = result->second;
    } else {
        return -1;
    }

    KcpCtx* ctx = (KcpCtx*)n->kcp_ctx;
    kcp_lock(n);
    ctx->peer_addr.sin_family = AF_INET;
    ctx->peer_addr.sin_port = htons(resolved_port);
    inet_pton(AF_INET, resolved_ip, &ctx->peer_addr.sin_addr);
    ctx->peer_addr_valid = 1;
    kcp_unlock(n);

    uint8_t* cipher = NULL;
    size_t clen = 0;
    if (crypto_encrypt_public(data, len, n->peer_key, &cipher, &clen) != 0) return -1;

    kcp_lock(n);
    int ret = ikcp_send((ikcpcb*)n->kcp, (char*)cipher, (int)clen);
    kcp_unlock(n);

    free(cipher);
    return (ret >= 0) ? 0 : -1;
}

// Start ZRTP key exchange.
// 启动 ZRTP 密钥交换。
int p2p_zrtp_start_exchange(P2PNode* n,
                            p2p_zrtp_sas_callback on_sas,
                            p2p_zrtp_result_callback on_result,
                            void* user_data) {
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

    zrtp_session_t* zrtp = (zrtp_session_t*)n->zrtp;
    zrtp_session_set_keypair(zrtp, n->pub_key, n->sec_key);
    zrtp_session_set_peer_public(zrtp, n->peer_key);

    if (zrtp_session_key_exchange(zrtp) != ZRTP_SUCCESS) {
        std::cout << "ZRTP key exchange failed. ZRTP 密钥交换失败。\n";
        return -1;
    }

    n->on_zrtp_sas = on_sas;
    n->on_zrtp_result = on_result;
    n->zrtp_user_data = user_data;
    n->zrtp_exchanging = 1;

    const char* sas = zrtp_session_get_sas(zrtp);
    if (n->on_zrtp_sas) {
        n->on_zrtp_sas(sas, n->zrtp_user_data);
    }

    return 0;
}

// Confirm or reject ZRTP session after SAS verification.
// SAS 验证后确认或拒绝 ZRTP 会话。
void p2p_zrtp_confirm(P2PNode* n, int confirmed) {
    if (!n || !n->zrtp_exchanging) return;

    zrtp_session_t* zrtp = (zrtp_session_t*)n->zrtp;
    if (confirmed) {
        zrtp_session_mark_verified(zrtp);
        std::cout << "Peer verified. 对方已验证。\n";
    } else {
        std::cout << "Verification rejected. 验证被拒绝。\n";
    }

    if (n->on_zrtp_result) {
        n->on_zrtp_result(confirmed, n->zrtp_user_data);
    }

    n->zrtp_exchanging = 0;
}