// Copyright (c) 2026 Domirus
// SPDX-License-Identifier: Apache-2.0

#include "crypto.hpp"
#include <iostream>
#include <cassert>
#include <vector>
#include <array>

// Helper to print hex. 辅助函数：打印十六进制。
static void print_hex(const std::vector<uint8_t>& data) {
    for (auto b : data) {
        printf("%02x", b);
    }
    printf("\n");
}

int main() {
    std::cout << "=== Numotirus Crypto Test ===\n" << std::endl;

    // ============================================================
    // Test 1: Key pair generation. 测试1：密钥对生成。
    // ============================================================
    std::cout << "[Test 1] Key pair generation" << std::endl;
    auto alice = numotirus::crypto::generate_keypair();
    auto bob = numotirus::crypto::generate_keypair();

    std::cout << "  Alice public key: ";
    print_hex({alice.public_key.begin(), alice.public_key.end()});
    std::cout << "  Bob public key:   ";
    print_hex({bob.public_key.begin(), bob.public_key.end()});
    std::cout << "  ✅ OK\n" << std::endl;

    // ============================================================
    // Test 2: Shared secret computation. 测试2：共享秘密计算。
    // ============================================================
    std::cout << "[Test 2] Shared secret computation" << std::endl;
    auto alice_shared = numotirus::crypto::compute_shared_secret(
        alice.secret, bob.public_key);
    auto bob_shared = numotirus::crypto::compute_shared_secret(
        bob.secret, alice.public_key);

    assert(alice_shared == bob_shared);
    std::cout << "  Shared secret: ";
    print_hex({alice_shared.begin(), alice_shared.end()});
    std::cout << "  ✅ Shared secrets match\n" << std::endl;

    // ============================================================
    // Test 3: Key derivation. 测试3：密钥派生。
    // ============================================================
    std::cout << "[Test 3] Key derivation" << std::endl;
    auto key = numotirus::crypto::derive_key(alice_shared, "numotirus-test");
    std::cout << "  Derived key: ";
    print_hex({key.begin(), key.end()});
    std::cout << "  ✅ OK (size=" << key.size() << " bytes)\n" << std::endl;

    // ============================================================
    // Test 4: Encrypt/Decrypt roundtrip. 测试4：加解密往返。
    // ============================================================
    std::cout << "[Test 4] Encrypt/Decrypt roundtrip" << std::endl;
    std::vector<uint8_t> plaintext = {'H','e','l','l','o',' ','N','u','m','o','t','i','r','u','s','!'};
    auto nonce_bytes = numotirus::crypto::random_bytes(numotirus::crypto::NONCE_SIZE);
    std::array<uint8_t, numotirus::crypto::NONCE_SIZE> nonce;
    std::copy(nonce_bytes.begin(), nonce_bytes.end(), nonce.begin());

    std::cout << "  Plaintext:  ";
    print_hex(plaintext);

    auto ciphertext = numotirus::crypto::encrypt(plaintext, key, nonce);
    std::cout << "  Ciphertext: ";
    print_hex(ciphertext);

    auto decrypted = numotirus::crypto::decrypt(ciphertext, key, nonce);
    assert(plaintext == decrypted);
    std::cout << "  Decrypted:  ";
    print_hex(decrypted);
    std::cout << "  ✅ Roundtrip successful\n" << std::endl;

    // ============================================================
    // Test 5: Authentication (tamper detection). 测试5：认证（篡改检测）。
    // ============================================================
    std::cout << "[Test 5] Tamper detection" << std::endl;
    if (!ciphertext.empty()) {
        auto tampered = ciphertext;
        tampered[0] ^= 0x01;  // Flip first byte. 翻转第一个字节。
        auto failed = numotirus::crypto::decrypt(tampered, key, nonce);
        if (failed.empty()) {
            std::cout << "  ✅ Tampered ciphertext rejected (authentication failed)\n" << std::endl;
        } else {
            std::cout << "  ❌ Tampered ciphertext was accepted! This is a bug.\n" << std::endl;
            return 1;
        }
    }

    // ============================================================
    // Test 6: Random bytes. 测试6：随机字节。
    // ============================================================
    std::cout << "[Test 6] Random bytes generation" << std::endl;
    auto rand1 = numotirus::crypto::random_bytes(32);
    auto rand2 = numotirus::crypto::random_bytes(32);
    assert(rand1 != rand2);
    std::cout << "  ✅ Two random sequences are different\n" << std::endl;

    // ============================================================
    // Summary. 总结。
    // ============================================================
    std::cout << "========================================" << std::endl;
    std::cout << "🎉 All tests passed!" << std::endl;
    std::cout << "========================================" << std::endl;

    return 0;
}