#ifndef P2P_H
#define P2P_H

#include <stdint.h>
#include <stddef.h>

// Opaque node handle. 不透明的节点句柄。
typedef struct P2PNode P2PNode;

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

// Destroy node and free resources. 销毁节点并释放资源。
void p2p_destroy(P2PNode* n);

#endif