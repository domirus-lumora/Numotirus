// core/p2p/p2p.h
// P2P network layer for Numotirus. Numotirus 的 P2P 网络层。

#ifndef P2P_H
#define P2P_H

#include <cstdint>
#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque node handle. 不透明的节点句柄。
typedef struct P2PNode P2PNode;

// ZRTP SAS ready callback. ZRTP SAS 就绪回调。
// sas: SAS string (4 groups of 4 digits). SAS 字符串（4组4位数字）。
// user_data: User data passed to p2p_zrtp_start_exchange. 用户数据。
typedef void (*zrtp_sas_callback)(const char* sas, void* user_data);

// ZRTP verification result callback. ZRTP 验证结果回调。
// confirmed: 1 if confirmed, 0 if rejected. 1 表示确认，0 表示拒绝。
// user_data: User data. 用户数据。
typedef void (*zrtp_result_callback)(int confirmed, void* user_data);

// Create a P2P node. 创建 P2P 节点。
P2PNode* p2p_create(uint16_t port);

// Start the node (non-blocking, internal thread). 启动节点（非阻塞，内部线程）。
int p2p_start(P2PNode* n);

// Set message callback. 设置消息回调。
void p2p_set_callback(P2PNode* n, void (*cb)(const char* ip, uint16_t port,
                                              const uint8_t* data, size_t len));

// Get own public key. 获取自己的公钥。
const uint8_t* p2p_get_public_key(P2PNode* n);

// Set peer's public key. 设置对方的公钥。
int p2p_set_peer_key(P2PNode* n, const uint8_t* k);

// Check if peer key is set. 检查是否已设置对方公钥。
int p2p_is_peer_ready(P2PNode* n);

// Send encrypted message. 发送加密消息。
int p2p_send(P2PNode* n, const char* ip, uint16_t port,
             const uint8_t* data, size_t len);

// -------------------------------------------------------------------------
// Asynchronous ZRTP key exchange. 异步 ZRTP 密钥交换。
// -------------------------------------------------------------------------

// Start ZRTP key exchange (non-blocking). 启动 ZRTP 密钥交换（非阻塞）。
int p2p_zrtp_start_exchange(P2PNode* n,
                            zrtp_sas_callback on_sas,
                            zrtp_result_callback on_result,
                            void* user_data);

// Confirm or reject the ZRTP session after SAS verification.
// SAS 验证后确认或拒绝 ZRTP 会话。
void p2p_zrtp_confirm(P2PNode* n, int confirmed);

// Destroy node and free resources. 销毁节点并释放资源。
void p2p_destroy(P2PNode* n);

// Get DHT instance. 获取 DHT 实例。
void* p2p_get_dht(P2PNode* n);

// -------------------------------------------------------------------------
// DHT routing table. DHT 路由表。
// -------------------------------------------------------------------------

void* dht_create(const uint8_t* own_id);
void dht_destroy(void* dht);
void dht_add_node(void* dht, const uint8_t* id, const char* ip, uint16_t port, uint64_t last_seen);
int dht_find_closest(void* dht, const uint8_t* target,
                     uint8_t* out_ids, char* out_ips, uint16_t* out_ports, int max_count);
void dht_print(void* dht);

// -------------------------------------------------------------------------
// NAT traversal. NAT 穿透。
// -------------------------------------------------------------------------

// Start NAT traversal to peer. 开始向对方进行 NAT 穿透。
// peer_candidates: format "ip1:port1,ip2:port2". 格式 "ip1:port1,ip2:port2"。
int p2p_nat_start_traversal(P2PNode* n, const char* peer_candidates);

#ifdef __cplusplus
}
#endif

#endif  // P2P_H