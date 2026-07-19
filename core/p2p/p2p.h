// core/p2p/p2p.h
// P2P network layer C API for Numotirus.
// Numotirus 的 P2P 网络层 C API。
// SPDX-License-Identifier: Apache-2.0

#ifndef P2P_H
#define P2P_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define P2P_PUBLIC_KEY_SIZE 32
#define P2P_SECRET_KEY_SIZE 32
#define P2P_NODE_ID_SIZE 20
#define P2P_MAX_IP_LEN 64

// Message received callback.
// 消息接收回调。
typedef void (*p2p_message_callback)(const char* ip, uint16_t port,
                                     const uint8_t* data, size_t len);

// Noise SAS ready callback.
// Noise SAS 就绪回调。
typedef void (*p2p_noise_sas_callback)(const char* sas, void* user_data);

// Noise verification result callback.
// Noise 验证结果回调。
typedef void (*p2p_noise_result_callback)(int confirmed, void* user_data);

// P2P node structure (opaque for C users, fully defined for internal use).
// P2P 节点结构（对 C 用户不透明，内部完整定义）。
typedef struct P2PNode {
    int sock;
    volatile int running;

    uint8_t pub_key[P2P_PUBLIC_KEY_SIZE];
    uint8_t sec_key[P2P_SECRET_KEY_SIZE];
    uint8_t peer_key[P2P_PUBLIC_KEY_SIZE];
    int peer_ready;

    p2p_message_callback on_message;

    void* kcp;
    void* kcp_ctx;
    void* kcp_mutex;

    char last_peer_ip[P2P_MAX_IP_LEN];
    uint16_t last_peer_port;

    // Noise session fields.
    // Noise 会话字段。
    void* noise_session;                           // NoiseSession* (C++ object)
    p2p_noise_sas_callback on_noise_sas;          // SAS callback
    p2p_noise_result_callback on_noise_result;    // Verification result callback
    void* noise_user_data;                        // User data for callbacks
    int noise_exchanging;                         // 1 if exchange in progress

    void* dht;
    void* transport;
    uint8_t node_id[P2P_NODE_ID_SIZE];

#ifdef _WIN32
    void* recv_thread;
    void* kcp_thread;
    void* dht_bootstrap_thread;
#else
    void* recv_thread;
    void* kcp_thread;
    void* dht_bootstrap_thread;
#endif
} P2PNode;

// Create a P2P node listening on the given UDP port.
// 创建在指定 UDP 端口上监听的 P2P 节点。
P2PNode* p2p_create(uint16_t port);

// Destroy the node and release all resources.
// 销毁节点并释放所有资源。
void p2p_destroy(P2PNode* node);

// Start the node's internal threads (non-blocking).
// 启动节点的内部线程（非阻塞）。
int p2p_start(P2PNode* node);

// Set callback for incoming messages.
// 设置接收消息的回调。
void p2p_set_message_callback(P2PNode* node, p2p_message_callback cb);

// Get own public key.
// 获取自己的公钥。
const uint8_t* p2p_get_public_key(const P2PNode* node);

// Set peer's public key from raw bytes.
// 从原始字节设置对方的公钥。
int p2p_set_peer_key(P2PNode* node, const uint8_t* key);

// Set peer's public key from hex string (64 characters).
// 从十六进制字符串设置对方公钥（64字符）。
int p2p_set_peer_key_hex(P2PNode* node, const char* hex);

// Check if peer key has been set.
// 检查是否已设置对方公钥。
int p2p_is_peer_ready(const P2PNode* node);

// Send encrypted message to peer.
// 发送加密消息给对方。
int p2p_send(P2PNode* node, const char* ip, uint16_t port,
             const uint8_t* data, size_t len);

// Start Noise key exchange.
// 启动 Noise 密钥交换。
int p2p_noise_start_exchange(P2PNode* node,
                             p2p_noise_sas_callback on_sas,
                             p2p_noise_result_callback on_result,
                             void* user_data);

// Confirm or reject Noise session after SAS verification.
// SAS 验证后确认或拒绝 Noise 会话。
void p2p_noise_confirm(P2PNode* node, int confirmed);

// Get Noise session keys for encryption.
// 获取 Noise 会话密钥用于加密。
int p2p_noise_get_keys(P2PNode* node, uint8_t* rx_key, uint8_t* tx_key);

// Clean up Noise session.
// 清理 Noise 会话。
void p2p_noise_cleanup(P2PNode* node);

// Get the DHT handle for debugging.
// 获取 DHT 句柄用于调试。
void* p2p_get_dht(P2PNode* node);
void kcp_lock(P2PNode* n);
void kcp_unlock(P2PNode* n);

#ifdef __cplusplus
}
#endif

#endif // P2P_H