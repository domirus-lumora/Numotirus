#ifndef CRYPTO_C_H
#define CRYPTO_C_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// ===============================
// Keypair / 密钥对
// ===============================

typedef struct CryptoKeypair CryptoKeypair;

// Generate keypair / 生成密钥对
CryptoKeypair* crypto_keypair_generate(void);

// Free keypair / 释放密钥对
void crypto_keypair_free(CryptoKeypair* kp);

// Get public key / 获取公钥
const uint8_t* crypto_keypair_get_public(const CryptoKeypair* kp);

// Get secret key / 获取私钥
const uint8_t* crypto_keypair_get_secret(const CryptoKeypair* kp);

// ===============================
// ECIES encryption
// ===============================

// Encrypt with recipient public key / 用对方公钥加密
int crypto_encrypt_public(
    const uint8_t* plain, size_t plain_len,
    const uint8_t* pubkey,
    uint8_t** out, size_t* out_len
);

// Decrypt with own secret key / 用自己私钥解密
int crypto_decrypt_private(
    const uint8_t* cipher, size_t cipher_len,
    const uint8_t* seckey,
    uint8_t** out, size_t* out_len
);

#ifdef __cplusplus
}
#endif

#endif