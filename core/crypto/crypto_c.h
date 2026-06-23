// core/crypto/crypto_c.h
// Crypto C API for Numotirus. Numotirus 的加密 C API。

#ifndef CRYPTO_C_H
#define CRYPTO_C_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Opaque keypair handle. 不透明的密钥对句柄。
typedef struct CryptoKeypair CryptoKeypair;

// Generate a new keypair. 生成新的密钥对。
CryptoKeypair* crypto_keypair_generate(void);

// Free a keypair. 释放密钥对。
void crypto_keypair_free(CryptoKeypair* kp);

// Get public key (32 bytes). 获取公钥（32 字节）。
const uint8_t* crypto_keypair_get_public(const CryptoKeypair* kp);

// Get secret key (32 bytes). 获取私钥（32 字节）。
const uint8_t* crypto_keypair_get_secret(const CryptoKeypair* kp);

// Encrypt plaintext with recipient's public key (ECIES). 使用对方公钥加密明文。
int crypto_encrypt_public(const uint8_t* plain, size_t plain_len,
                          const uint8_t* pubkey,
                          uint8_t** out, size_t* out_len);

// Decrypt ciphertext with own secret key. 使用自己的私钥解密密文。
int crypto_decrypt_private(const uint8_t* cipher, size_t cipher_len,
                           const uint8_t* seckey,
                           uint8_t** out, size_t* out_len);

#ifdef __cplusplus
}
#endif

#endif  // CRYPTO_C_H