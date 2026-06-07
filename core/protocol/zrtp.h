// core/protocol/zrtp.h
// ZRTP session management using libsodium crypto_kx.
// 使用 libsodium crypto_kx 的 ZRTP 会话管理。

#pragma once

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Error codes for ZRTP operations. ZRTP 操作的错误码。
typedef enum {
  ZRTP_SUCCESS = 0,
  ZRTP_ERROR_INVALID_ARGUMENT = -1,
  ZRTP_ERROR_KEY_EXCHANGE_FAILED = -2,
  ZRTP_ERROR_TRUST_STORE_IO = -3,
} zrtp_error_t;

// Opaque ZRTP session handle. 不透明的 ZRTP 会话句柄。
typedef struct zrtp_session_t zrtp_session_t;

// Create a new ZRTP session. 创建新的 ZRTP 会话。
// Returns NULL on allocation failure. 分配失败返回 NULL。
zrtp_session_t* zrtp_session_new(void);

// Destroy a ZRTP session and free resources. 销毁 ZRTP 会话并释放资源。
void zrtp_session_free(zrtp_session_t* sess);

// Set own keypair (public + secret). 设置自己的密钥对（公钥 + 私钥）。
void zrtp_session_set_keypair(zrtp_session_t* sess,
                              const uint8_t pubkey[32],
                              const uint8_t seckey[32]);

// Set peer's public key before key exchange. 在密钥交换前设置对方的公钥。
void zrtp_session_set_peer_public(zrtp_session_t* sess,
                                  const uint8_t peer_pubkey[32]);

// Perform key exchange using crypto_kx. 使用 crypto_kx 执行密钥交换。
// Returns ZRTP_SUCCESS on success, error code on failure.
// 成功返回 ZRTP_SUCCESS，失败返回错误码。
zrtp_error_t zrtp_session_key_exchange(zrtp_session_t* sess);

// Get SAS string (4 groups of 4 digits). 获取 SAS 字符串（4 组 4 位数字）。
// Returns pointer to internal buffer, valid until session is destroyed.
// 返回指向内部缓冲区的指针，在会话销毁前有效。
const char* zrtp_session_get_sas(const zrtp_session_t* sess);

// Mark SAS as verified by user. 标记 SAS 已被用户验证。
void zrtp_session_mark_verified(zrtp_session_t* sess);

// Check if peer has been verified. 检查对方是否已验证。
int zrtp_session_is_verified(const zrtp_session_t* sess);

// Get receive key (for decrypting incoming messages). 获取接收密钥（用于解密传入消息）。
const uint8_t* zrtp_session_get_rx_key(const zrtp_session_t* sess);

// Get transmit key (for encrypting outgoing messages). 获取发送密钥（用于加密传出消息）。
const uint8_t* zrtp_session_get_tx_key(const zrtp_session_t* sess);

// Save trust entry to file (peer_id, public key, shared secret).
// 保存信任条目到文件（peer_id, 公钥, 共享秘密）。
zrtp_error_t zrtp_trust_store_save(const char* peer_id,
                                   const uint8_t peer_pubkey[32],
                                   const uint8_t shared_secret[32]);

// Load trust entry from file. 从文件加载信任条目。
zrtp_error_t zrtp_trust_store_load(const char* peer_id,
                                   uint8_t peer_pubkey[32],
                                   uint8_t shared_secret[32]);

#ifdef __cplusplus
}
#endif