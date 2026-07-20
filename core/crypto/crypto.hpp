// Copyright (c) 2026 Domirus
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array> // DOMIRUS: A lib used to save data by using a list. 一个用来把数据储存进[]容器里的库。
/* DOMIRUS: We use array to save data because the data that we save in array is a constant, which mean it doesn't change. But std::vector can be change. 
 In theory, vector is always better than array because it is dynamic and can be freely change. But it is write on heap.
 Heap is a place of RAM which is control by the programmer, like pointer, you need to manage it yourself. Moreover, a stack is a place that is control by the compiler, and array is usually based on stack, so it will write faster than heap. */
/* DOMIRUS：我们使用数组来存储数据，因为存储在数组中的数据是常量，也就是说它不会改变。但 std::vector 可以改变。
 理论上，vector 总是比数组更好，因为它是动态的，可以自由更改。但它写入的是堆。
 堆是 RAM 中由程序员控制的区域，就像指针一样，需要自己管理。此外，栈是由编译器控制的区域，而数组通常基于栈，因此它的写入速度会比堆更快。 */

#include <vector> // DOMIRUS: A lib used to save data on a dynamic array.
#include <cstdint> // DOMIRUS: A lib than supply more complex and diverse types of integer.
#include <string> // DOMIRUS: A lib that supply a group of character.

namespace numotirus { // DOMIRUS: numotirus::
namespace crypto { // DOMIRUS: numotirus::crypto

// Constants. 常量。
constexpr size_t PUBLIC_KEY_SIZE = 32;      // X25519 public key size. X25519 公钥大小。
constexpr size_t SECRET_KEY_SIZE = 32;      // X25519 secret key size. X25519 私钥大小。
constexpr size_t SHARED_SECRET_SIZE = 32;   // Shared secret size. 共享秘密大小。
constexpr size_t KEY_SIZE = 32;             // Symmetric key size (ChaCha20). 对称密钥大小。
constexpr size_t NONCE_SIZE = 24;           // XChaCha20 nonce size. XChaCha20 nonce 大小。
constexpr size_t TAG_SIZE = 16;             // Poly1305 authentication tag size. Poly1305 认证标签大小。

// You might ask why are these value. I can't answer you. This is the rules.
// 不要问我为什么是这些值，我也没办法，这是硬性规定。

// Key pair. 密钥对。
struct KeyPair { // DOMIRUS: Use struct to save few value. 用struct是为了存储数据。
    std::array<uint8_t, SECRET_KEY_SIZE> secret;      // Secret key. 私钥。
    // DOMIRUS: unit8_t is a datatypes, like int or float. It means a unsign 8 bit integer, _t means type.  DOMIRUS：unit8_t 是一种数据类型，就像 int 或 float 一样。它表示一个无符号的 8 位整数，_t 表示类型。
    // std::array<datatypes, size of array> variable name.
    std::array<uint8_t, PUBLIC_KEY_SIZE> public_key;  // Public key. 公钥。
};

// X25519 key exchange. X25519 密钥交换。

// Generate a random key pair. 生成随机密钥对。
KeyPair generate_keypair(); //DOMIRUS: The struct KeyPair has a new function name generate_keypair.

// Derive public key from secret key. 从私钥派生公钥。
std::array<uint8_t, PUBLIC_KEY_SIZE> derive_public_key( //This is a function, it will return a array.
    const std::array<uint8_t, SECRET_KEY_SIZE>& secret); // DOMIRUS: The function has one parameter named secret. The & is not about pointer, it means don't copy variable. I mean: It will change the value by not using pointer.
    // DOMIRUS: When we use a function to change a variable, it does not change without using pointer, because it will copy the variable, and the copied variable is only can use inside the class / struct, just like private.
    // DOMIRUS: And add a & can prevent the function copy the variable, so that it can change the value easily withou using pointer.
    // DOMIRUS: But on this function, it has a const, means we can't change. So its function is just to prevent copying the variable to avoid the attacker can found the value of it on RAM.
    
    // DOMIRUS：该函数有一个名为 secret 的参数。这里的 & 并非指指针，而是表示不复制变量。我的意思是：它将通过不使用指针的方式来更改该变量的值。
    // DOMIRUS：当我们使用函数修改变量时，如果不使用指针，变量并不会被修改，因为函数会复制该变量，而复制的变量只能在类/结构体内部使用，就像 private 成员一样。
    // DOMIRUS：添加 & 可以防止函数复制变量，从而使其无需使用指针即可轻松修改值。
    // DOMIRUS：但该函数带有 const 修饰符，意味着我们无法修改其值。因此，该函数的作用仅在于防止变量被复制，以避免攻击者在内存中查找到该变量的值。


// Compute shared secret: my_secret * their_public. 计算共享秘密：自己的私钥 × 对方的公钥。
std::array<uint8_t, SHARED_SECRET_SIZE> compute_shared_secret(
    const std::array<uint8_t, SECRET_KEY_SIZE>& my_secret,
    const std::array<uint8_t, PUBLIC_KEY_SIZE>& their_public);


// ChaCha20-Poly1305 authenticated encryption. ChaCha20-Poly1305 认证加密。

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


// Utilities. 工具函数。

// Derive symmetric key from shared secret using BLAKE2b. 使用 BLAKE2b 从共享秘密派生对称密钥。
std::array<uint8_t, KEY_SIZE> derive_key(
    const std::array<uint8_t, SHARED_SECRET_SIZE>& shared_secret,
    const std::string& salt = "");

// Generate random bytes. 生成随机字节。
std::vector<uint8_t> random_bytes(size_t count);

// Public key encryption (ECIES-style). 公钥加密（ECIES 风格）。

// Encrypt with recipient's public key. 用对方公钥加密。
// Returns: [ephemeral_public(32)] + [nonce(24)] + [ciphertext]
std::vector<uint8_t> encrypt_public(
    const std::vector<uint8_t>& plaintext,
    const std::array<uint8_t, PUBLIC_KEY_SIZE>& recipient_public
);

// Decrypt with my secret key. 用自己的私钥解密。
// Input format: [ephemeral_public(32)] + [nonce(24)] + [ciphertext]
std::vector<uint8_t> decrypt_private(
    const std::vector<uint8_t>& ciphertext, // DOMIRUS: This ciphertext has including the three thing above. The first 32 bit was the ephemeral_public, and so on. 该密文包含了上述三项内容。前32位是ephemeral_public，以此类推。
    const std::array<uint8_t, SECRET_KEY_SIZE>& my_secret // DOMIRUS: Your private key. 你的私钥。
);

} // namespace crypto
} // namespace numotirus