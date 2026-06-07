// core/protocol/sas.h
// SAS (Short Authentication String) generation for ZRTP handshake.
// ZRTP 握手中的 SAS（短认证字符串）生成。

#ifndef PROTOCOL_SAS_H
#define PROTOCOL_SAS_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Generate SAS from shared secret. 从共享秘密生成 SAS。
// shared_secret: 32-byte shared secret from X25519. X25519 产生的 32 字节共享秘密。
// secret_len: length of shared_secret, must be 32. 共享秘密长度，必须为 32。
// out: output buffer, at least 20 bytes. 输出缓冲区，至少 20 字节。
// out_size: size of out buffer. 输出缓冲区大小。
// Returns 0 on success, -1 on failure. 成功返回 0，失败返回 -1。
int sas_generate(const uint8_t* shared_secret, size_t secret_len, char* out, size_t out_size);

#ifdef __cplusplus
}
#endif

#endif