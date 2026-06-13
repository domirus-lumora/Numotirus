// core/p2p/p2p.h
// P2P network layer for Numotirus. Numotirus 的 P2P 网络层。

#ifndef P2P_H
#define P2P_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque node handle. 不透明的节点句柄。
typedef struct P2PNode P2PNode;

// Callback for ZRTP SAS ready. ZRTP SAS 就绪时的回调函数。
// sas: 短认证字符串 (4组4位数字). SAS string.
// user_data: 用户数据，由 p2p_zrtp_start_exchange 传入。User data.
typedef void (*zrtp_sas_callback)(const char* sas, void* user_data);

// Callback for ZRTP verification result. ZRTP 验证结果回调。
// confirmed: 1 if user confirmed, 0 if rejected. 1 表示用户确认，0 表示拒绝。
// user_data: 用户数据。User data.
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

// Start ZRTP key exchange. Does not block.
// 启动 ZRTP 密钥交换，不阻塞。
// n: P2P node.
// on_sas: callback when SAS is generated (called from network thread).
// on_result: callback when user confirms or rejects (can be called from any thread).
// user_data: passed to both callbacks.
// Returns 0 on success, -1 on error. 成功返回0，错误返回-1。
int p2p_zrtp_start_exchange(P2PNode* n,
                            zrtp_sas_callback on_sas,
                            zrtp_result_callback on_result,
                            void* user_data);

// Confirm or reject the ZRTP session after SAS verification.
// SAS 验证后确认或拒绝会话。
// n: P2P node.
// confirmed: 1 to accept, 0 to reject. 1 接受，0 拒绝。
void p2p_zrtp_confirm(P2PNode* n, int confirmed);

// Destroy node and free resources. 销毁节点并释放资源。
void p2p_destroy(P2PNode* n);

#ifdef __cplusplus
}
#endif

#endif  // P2P_H