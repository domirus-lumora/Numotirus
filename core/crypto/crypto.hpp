// Copyright (c) 2026 Domirus
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <string>

namespace numotirus {
namespace crypto {

// ============================================================
// Constants. 常量。
// ============================================================

constexpr size_t PUBLIC_KEY_SIZE = 32;      // X25519 public key size. X25519 公钥大小。
constexpr size_t SECRET_KEY_SIZE = 32;      // X25519 secret key size. X25519 私钥大小。
constexpr size_t SHARED_SECRET_SIZE = 32;   // Shared secret size. 共享秘密大小。
constexpr size_t KEY_SIZE = 32;             // Symmetric key size (ChaCha20). 对称密钥大小。
constexpr size_t NONCE_SIZE = 24;           // XChaCha20 nonce size. XChaCha20 nonce 大小。
constexpr size_t TAG_SIZE = 16;             // Poly1305 authentication tag size. Poly1305 认证标签大小。

// ============================================================
// Key pair. 密钥对。
// ============================================================

struct KeyPair {
    std::array<uint8_t, SECRET_KEY_SIZE> secret;      // Secret key. 私钥。
    std::array<uint8_t, PUBLIC_KEY_SIZE> public_key;  // Public key. 公钥。
};

// ============================================================
// X25519 key exchange. X25519 密钥交换。
// ============================================================

// Generate a random key pair. 生成随机密钥对。
KeyPair generate_keypair();

// Derive public key from secret key. 从私钥派生公钥。
std::array<uint8_t, PUBLIC_KEY_SIZE> derive_public_key(
    const std::array<uint8_t, SECRET_KEY_SIZE>& secret);

// Compute shared secret: my_secret * their_public. 计算共享秘密：自己的私钥 × 对方的公钥。
std::array<uint8_t, SHARED_SECRET_SIZE> compute_shared_secret(
    const std::array<uint8_t, SECRET_KEY_SIZE>& my_secret,
    const std::array<uint8_t, PUBLIC_KEY_SIZE>& their_public);

// ============================================================
// ChaCha20-Poly1305 authenticated encryption. ChaCha20-Poly1305 认证加密。
// ============================================================

// Encrypt plaintext with key and nonce. 使用密钥和 nonce 加密明文。
// Optional associated data provides integrity without encryption. 可选的关联数据提供不加密的完整性保护。
std::vector<uint8_t> encrypt(
    const std::vector<uint8_t>& plaintext,
    const std::array<uint8_t, KEY_SIZE>& key,
    const std::array<uint8_t, NONCE_SIZE>& nonce,
    const std::vector<uint8_t>& associated_data = {});

// Decrypt ciphertext. Returns empty vector on failure (authentication failed). 解密密文。失败（认证失败）时返回空向量。
std::vector<uint8_t> decrypt(
    const std::vector<uint8_t>& ciphertext,
    const std::array<uint8_t, KEY_SIZE>& key,
    const std::array<uint8_t, NONCE_SIZE>& nonce,
    const std::vector<uint8_t>& associated_data = {});

// ============================================================
// Utilities. 工具函数。
// ============================================================

// Derive symmetric key from shared secret using BLAKE2b. 使用 BLAKE2b 从共享秘密派生对称密钥。
std::array<uint8_t, KEY_SIZE> derive_key(
    const std::array<uint8_t, SHARED_SECRET_SIZE>& shared_secret,
    const std::string& salt = "");

// Generate random bytes. 生成随机字节。
std::vector<uint8_t> random_bytes(size_t count);

// ============================================================
// Public key encryption (ECIES-style). 公钥加密（ECIES 风格）。
// ============================================================

// Encrypt with recipient's public key. 用对方公钥加密。
// Returns: [ephemeral_public(32)] + [nonce(24)] + [ciphertext]
std::vector<uint8_t> encrypt_public(
    const std::vector<uint8_t>& plaintext,
    const std::array<uint8_t, PUBLIC_KEY_SIZE>& recipient_public
);

// Decrypt with my secret key. 用自己的私钥解密。
// Input format: [ephemeral_public(32)] + [nonce(24)] + [ciphertext]
std::vector<uint8_t> decrypt_private(
    const std::vector<uint8_t>& ciphertext,
    const std::array<uint8_t, SECRET_KEY_SIZE>& my_secret
);

} // namespace crypto
} // namespace numotirus