// Copyright (c) 2026 Domirus
// SPDX-License-Identifier: Apache-2.0

#include "crypto.hpp"
#include <sodium.h>
#include <stdexcept>
#include <cstring>
#include <string>

namespace numotirus {
namespace crypto {

// Internal helpers. 内部辅助函数。

// Ensure libsodium is initialized once. 确保 libsodium 只初始化一次。
static bool is_sodium_initialized() {
    static bool initialized = []() {
        return sodium_init() >= 0;
    }();
    return initialized;
}

// X25519 key exchange. X25519 密钥交换。
KeyPair generate_keypair() {
    if (!is_sodium_initialized()) {
        throw std::runtime_error("libsodium not initialized");
    }

    KeyPair kp;
    if (crypto_box_keypair(kp.public_key.data(), kp.secret.data()) != 0) {
        throw std::runtime_error("crypto_box_keypair failed");
    }
    return kp;
}

std::array<uint8_t, PUBLIC_KEY_SIZE> derive_public_key(
    const std::array<uint8_t, SECRET_KEY_SIZE>& secret) {

    std::array<uint8_t, PUBLIC_KEY_SIZE> pub;
    if (crypto_scalarmult_base(pub.data(), secret.data()) != 0) {
        throw std::runtime_error("crypto_scalarmult_base failed");
    }
    return pub;
}

std::array<uint8_t, SHARED_SECRET_SIZE> compute_shared_secret(
    const std::array<uint8_t, SECRET_KEY_SIZE>& my_secret,
    const std::array<uint8_t, PUBLIC_KEY_SIZE>& their_public) {

    std::array<uint8_t, SHARED_SECRET_SIZE> shared;
    if (crypto_scalarmult(shared.data(), my_secret.data(), their_public.data()) != 0) {
        throw std::runtime_error("crypto_scalarmult failed");
    }
    return shared;
}

// Symmetric key derivation. 对称密钥派生。
std::array<uint8_t, KEY_SIZE> derive_key(
    const std::array<uint8_t, SHARED_SECRET_SIZE>& shared_secret,
    const std::string& salt) {

    std::array<uint8_t, KEY_SIZE> key;
    crypto_generichash_blake2b_state state;
    if (crypto_generichash_blake2b_init(&state, nullptr, 0, KEY_SIZE) != 0) {
        throw std::runtime_error("crypto_generichash_blake2b_init failed");
    }
    crypto_generichash_blake2b_update(&state, shared_secret.data(), shared_secret.size());

    if (!salt.empty()) {
        crypto_generichash_blake2b_update(&state,
            reinterpret_cast<const uint8_t*>(salt.data()), salt.size());
    }

    if (crypto_generichash_blake2b_final(&state, key.data(), KEY_SIZE) != 0) {
        throw std::runtime_error("crypto_generichash_blake2b_final failed");
    }
    return key;
}

// XChaCha20-Poly1305 authenticated encryption.
// XChaCha20-Poly1305 认证加密。
std::vector<uint8_t> encrypt(
    const std::vector<uint8_t>& plaintext,
    const std::array<uint8_t, KEY_SIZE>& key,
    const std::array<uint8_t, NONCE_SIZE>& nonce,
    const std::vector<uint8_t>& associated_data) {

    if (!is_sodium_initialized()) {
        throw std::runtime_error("libsodium not initialized");
    }

    std::vector<uint8_t> ciphertext(plaintext.size() + TAG_SIZE);
    unsigned long long ciphertext_len;

    if (crypto_aead_xchacha20poly1305_ietf_encrypt(
        ciphertext.data(), &ciphertext_len,
        plaintext.data(), plaintext.size(),
        associated_data.data(), associated_data.size(),
        nullptr,
        nonce.data(),
        key.data()) != 0) {
        throw std::runtime_error("encryption failed");
    }

    ciphertext.resize(ciphertext_len);
    return ciphertext;
}

std::vector<uint8_t> decrypt(
    const std::vector<uint8_t>& ciphertext,
    const std::array<uint8_t, KEY_SIZE>& key,
    const std::array<uint8_t, NONCE_SIZE>& nonce,
    const std::vector<uint8_t>& associated_data) {

    if (!is_sodium_initialized()) {
        throw std::runtime_error("libsodium not initialized");
    }

    if (ciphertext.size() < TAG_SIZE) {
        return {};
    }

    std::vector<uint8_t> plaintext(ciphertext.size() - TAG_SIZE);
    unsigned long long plaintext_len;

    int ret = crypto_aead_xchacha20poly1305_ietf_decrypt(
        plaintext.data(), &plaintext_len,
        nullptr,
        ciphertext.data(), ciphertext.size(),
        associated_data.data(), associated_data.size(),
        nonce.data(),
        key.data()
    );

    if (ret != 0) {
        return {};
    }

    plaintext.resize(plaintext_len);
    return plaintext;
}

// Random bytes. 随机字节。
std::vector<uint8_t> random_bytes(size_t count) {
    if (!is_sodium_initialized()) {
        throw std::runtime_error("libsodium not initialized");
    }

    std::vector<uint8_t> bytes(count);
    randombytes_buf(bytes.data(), count);
    return bytes;
}

// Public key encryption (ECIES). 公钥加密（ECIES）。
std::vector<uint8_t> encrypt_public(
    const std::vector<uint8_t>& plaintext,
    const std::array<uint8_t, PUBLIC_KEY_SIZE>& recipient_public) {

    if (!is_sodium_initialized()) {
        throw std::runtime_error("libsodium not initialized");
    }

    // Generate ephemeral key pair. 生成临时密钥对。
    std::array<uint8_t, SECRET_KEY_SIZE> ephemeral_secret;
    std::array<uint8_t, PUBLIC_KEY_SIZE> ephemeral_public;
    crypto_box_keypair(ephemeral_public.data(), ephemeral_secret.data());

    // Compute shared secret. 计算共享秘密。
    std::array<uint8_t, SHARED_SECRET_SIZE> shared;
    if (crypto_scalarmult(shared.data(), ephemeral_secret.data(), recipient_public.data()) != 0) {
        throw std::runtime_error("crypto_scalarmult failed in encrypt_public");
    }

    // Derive symmetric key. 派生对称密钥。
    auto key = derive_key(shared, "Corvus");

    // Generate random nonce. 生成随机 nonce。
    auto nonce_vec = random_bytes(NONCE_SIZE);
    std::array<uint8_t, NONCE_SIZE> nonce;
    std::copy(nonce_vec.begin(), nonce_vec.end(), nonce.begin());

    // Encrypt. 加密。
    auto cipher = encrypt(plaintext, key, nonce);

    // Assemble result: [ephemeral_public] + [nonce] + [ciphertext]
    // 组装结果：[临时公钥] + [随机数] + [密文]
    std::vector<uint8_t> result;
    result.reserve(PUBLIC_KEY_SIZE + NONCE_SIZE + cipher.size());
    result.insert(result.end(), ephemeral_public.begin(), ephemeral_public.end());
    result.insert(result.end(), nonce.begin(), nonce.end());
    result.insert(result.end(), cipher.begin(), cipher.end());

    return result;
}

std::vector<uint8_t> decrypt_private(
    const std::vector<uint8_t>& ciphertext,
    const std::array<uint8_t, SECRET_KEY_SIZE>& my_secret) {

    if (!is_sodium_initialized()) {
        throw std::runtime_error("libsodium not initialized");
    }

    // Check minimum length. 检查最小长度。
    if (ciphertext.size() < PUBLIC_KEY_SIZE + NONCE_SIZE + TAG_SIZE) {
        return {};
    }

    // Extract ephemeral public key. 提取临时公钥。
    std::array<uint8_t, PUBLIC_KEY_SIZE> ephemeral_public;
    std::copy(ciphertext.begin(), ciphertext.begin() + PUBLIC_KEY_SIZE, ephemeral_public.begin());

    // Extract nonce. 提取随机数。
    std::array<uint8_t, NONCE_SIZE> nonce;
    std::copy(ciphertext.begin() + PUBLIC_KEY_SIZE,
              ciphertext.begin() + PUBLIC_KEY_SIZE + NONCE_SIZE,
              nonce.begin());

    // Extract actual ciphertext. 提取实际密文。
    std::vector<uint8_t> enc(
        ciphertext.begin() + PUBLIC_KEY_SIZE + NONCE_SIZE,
        ciphertext.end()
    );

    // Compute shared secret. 计算共享秘密。
    std::array<uint8_t, SHARED_SECRET_SIZE> shared;
    if (crypto_scalarmult(shared.data(), my_secret.data(), ephemeral_public.data()) != 0) {
        throw std::runtime_error("crypto_scalarmult failed in decrypt_private");
    }

    // Derive symmetric key. 派生对称密钥。
    auto key = derive_key(shared, "Corvus");

    // Decrypt and return. 解密并返回。
    return decrypt(enc, key, nonce);
}

} // namespace crypto
} // namespace numotirus
