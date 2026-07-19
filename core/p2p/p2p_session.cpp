// core/p2p/p2p_session.cpp
// P2P session management: peer key, Noise exchange, message sending.
// P2P 会话管理：对端公钥、Noise 交换、消息发送。
// SPDX-License-Identifier: Apache-2.0

#include "p2p.h"
#include "../crypto/crypto_c.h"
#include "../protocol/noise.hpp"
#include "kcp/ikcp.h"
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
using namespace numotirus::protocol::noise;

static NodeId MakeNodeId(const uint8_t* data) {
    NodeId id;
    memcpy(id.data(), data, 20);
    return id;
}

typedef struct {
    int sock;
    struct sockaddr_in peer_addr;
    int peer_addr_valid;
} KcpCtx;

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

int p2p_set_peer_key(P2PNode* n, const uint8_t* key) {
    if (!n || !key) return -1;
    memcpy(n->peer_key, key, P2P_PUBLIC_KEY_SIZE);
    n->peer_ready = 1;
    return 0;
}

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

int p2p_is_peer_ready(const P2PNode* n) {
    return n ? n->peer_ready : 0;
}

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

// Start Noise key exchange.
// 启动 Noise 密钥交换。
int p2p_noise_start_exchange(P2PNode* n,
                             p2p_noise_sas_callback on_sas,
                             p2p_noise_result_callback on_result,
                             void* user_data) {
    if (!n || !n->peer_ready) {
        std::cout << "Peer key not set. 未设置对方公钥。\n";
        return -1;
    }
    if (n->noise_exchanging) {
        std::cout << "Noise already in progress. Noise 已在交换中。\n";
        return -1;
    }

    auto session = std::make_unique<NoiseSession>();
    if (!session) return -1;

    KeyPair kp;
    memcpy(kp.public_key.data(), n->pub_key, 32);
    memcpy(kp.secret_key.data(), n->sec_key, 32);
    session->SetKeyPair(kp);

    std::array<uint8_t, 32> peer_key;
    memcpy(peer_key.data(), n->peer_key, 32);
    session->SetPeerPublic(peer_key);

    n->noise_session = session.release();
    n->on_noise_sas = on_sas;
    n->on_noise_result = on_result;
    n->noise_user_data = user_data;
    n->noise_exchanging = 1;

    auto* sess = static_cast<NoiseSession*>(n->noise_session);

    auto write_cb = [n](const uint8_t* data, size_t len) -> ErrorCode {
        KcpCtx* ctx = (KcpCtx*)n->kcp_ctx;
        if (!ctx->peer_addr_valid) return ErrorCode::kInvalidArgument;
        int sent = sendto(n->sock, (const char*)data, len, 0,
                          (struct sockaddr*)&ctx->peer_addr, sizeof(ctx->peer_addr));
        return (sent > 0) ? ErrorCode::kSuccess : ErrorCode::kHandshakeFailed;
    };

    auto read_cb = [n](uint8_t* buffer, size_t len) -> ErrorCode {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(n->sock, &fds);
        struct timeval tv = {2, 0};
        int ret = select(n->sock + 1, &fds, NULL, NULL, &tv);
        if (ret <= 0) return ErrorCode::kHandshakeFailed;
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);
        int nread = recvfrom(n->sock, (char*)buffer, len, 0,
                             (struct sockaddr*)&from, &from_len);
        if (nread <= 0) return ErrorCode::kHandshakeFailed;
        return ErrorCode::kSuccess;
    };

    ErrorCode err = sess->Handshake(true, write_cb, read_cb);
    if (err != ErrorCode::kSuccess) {
        delete static_cast<NoiseSession*>(n->noise_session);
        n->noise_session = nullptr;
        n->noise_exchanging = 0;
        std::cout << "Noise handshake failed. Noise 握手失败。\n";
        return -1;
    }

    std::string sas = sess->GetSas();
    if (n->on_noise_sas) {
        n->on_noise_sas(sas.c_str(), n->noise_user_data);
    }

    return 0;
}

// Confirm or reject Noise session after SAS verification.
// SAS 验证后确认或拒绝 Noise 会话。
void p2p_noise_confirm(P2PNode* n, int confirmed) {
    if (!n || !n->noise_exchanging) return;
    auto* sess = static_cast<NoiseSession*>(n->noise_session);
    if (!sess) return;

    if (confirmed) {
        sess->MarkVerified();
        std::cout << "Peer verified. 对方已验证。\n";
    } else {
        std::cout << "Verification rejected. 验证被拒绝。\n";
    }

    if (n->on_noise_result) {
        n->on_noise_result(confirmed, n->noise_user_data);
    }
    n->noise_exchanging = 0;
}