// core/crypto/crypto.hpp
// Numotirus Cryptographic Core
// SPDX-License-Identifier: Apache-2.0
//
// Provides type-safe wrappers around libsodium primitives:
//   - X25519 key exchange
//   - XChaCha20-Poly1305 authenticated encryption
//   - HKDF-BLAKE2b key derivation (full extract + expand)
//   - Secure memory management (sodium_memzero)
//   - Full buffer boundary checking
//
// Design principles:
//   - All sensitive data is zeroed with sodium_memzero
//   - All buffer operations perform boundary checks
//   - Errors are returned via tl::expected with specific error codes
//   - Preallocated buffers are supported to reduce dynamic allocation
//   - Key comparison is constant-time
//   - Nonce must be unique per key
//   - generate_nonce() provides cryptographically random nonces
//   - from_bytes() accepts externally provided nonces; caller is responsible for uniqueness
//   - All encryption functions check libsodium initialization status at entry
//   - noexcept is used only for functions that never throw and do not allocate memory
//
// 设计原则：
//   - 所有敏感数据使用 sodium_memzero 清零
//   - 所有缓冲区操作进行边界检查
//   - 错误通过 tl::expected 携带具体错误码
//   - 支持预分配缓冲区，以减少动态内存分配
//   - 密钥比较使用常量时间操作
//   - Nonce 对于同一密钥必须保持唯一
//   - generate_nonce() 提供密码学安全的随机 Nonce
//   - from_bytes() 接受外部提供的 Nonce；Nonce 的唯一性由调用者负责
//   - 所有加密函数在入口处检查 libsodium 初始化状态
//   - noexcept 仅用于绝对不会抛出异常且不会分配内存的函数

#pragma once

#include <array>
#include <vector>
#include <span>
#include <cstdint>
#include <string_view>
#include <string>
#include <optional>
#include <type_traits>
#include <tl/expected.hpp>

namespace numotirus {
namespace crypto {

//  Constants
//  常量
constexpr size_t kPublicKeySize = 32;   // X25519 public key / X25519 的公钥
constexpr size_t kSecretKeySize = 32;   // X25519 secret key / X25519 的私钥
constexpr size_t kSharedKeySize = 32;   // X25519 shared secret / X25519 的共享密钥
constexpr size_t kSymmetricKeySize = 32;   // XChaCha20 key / XChaCha20 的对称密钥
constexpr size_t kNonceSize = 24;   // XChaCha20 nonce / XChaCha20 的随机数
constexpr size_t kAeadTagSize = 16;   // Poly1305 authentication tag / Poly1305 的验证标签

/// BLAKE2b maximum output length
/// BLAKE2b 最大输出长度
constexpr size_t BLAKE2B_MAX_OUTPUT = 64;

//  Error Codes
//  错误码
enum class CryptoError {
    kSuccess = 0,
    kNotInitialized, // libsodium not initialized / libsodium 未初始化
    kInvalidInput, // Invalid input parameter / 输入参数无效
    kInvalidPublicKey, // Invalid public key (zero or low-order) / 公钥无效（零或低阶点）
    kInvalidSecretKey, // Invalid secret key (all zero) / 私钥无效（全零）
    kAuthenticationFailed, // Authentication failed / 认证失败
    kEncryptionFailed, // Encryption failed (internal error) / 加密失败（内部错误）
    kDecryptionFailed, // Decryption failed (internal error) / 解密失败（内部错误）
    kDerivationFailed, // Key derivation failed / 密钥派生失败
    kKeyPairGenerationFailed, // KeyPair can't generate / 无法生成密钥对
    kInitializationFailed, // libsodium can't initialize / 无法初始化 libsodium
    kMemoryError, // Memory allocation failed / 内存分配失败
    kInvalidCiphertext, // Invalid ciphertext format (too short) / 密文格式无效（长度不足）
    kBufferTooSmall, // Output buffer too small / 输出缓冲区过小
    kRandomGenerationFailed, // Random generation failed / 随机数生成失败
    kInvalidLength, // Invalid length parameter / 长度参数无效
    kSaltTooShort, // Salt too short / 盐值长度不足
    kPermissionDenied, // Permission denied / 权限不足
    kSubkeyIdInvalid,   // subkey_id == 0
    kKdfContextTooLong, // context > 8 bytes
};

/// Convert error code to string
/// 错误码转字符串
const char* crypto_error_string(CryptoError error) noexcept;

//  Type-safe wrappers
//  类型安全包装
/// X25519 public key (always 32 bytes)
/// X25519 公钥（固定 32 字节）
struct PublicKey : std::array<uint8_t, kPublicKeySize> {
    PublicKey() noexcept;
    
    /// Construct from raw bytes (without validation)
    /// Caller should ensure the input is valid, or call validate_public_key() later
    /// 从原始字节构造（不验证）
    /// 调用者应确保输入有效，或之后调用 validate_public_key()

    explicit PublicKey(std::span<const uint8_t, kPublicKeySize> bytes) noexcept;

    /// Construct from raw bytes (with check for public key which is formed by all zero, and the low-order point check will be held in compute_shared_secret)
    /// 从原始字节构造（带全0公钥验证，低阶点在计算共享秘密进行验证）
    [[nodiscard]] static tl::expected<PublicKey, CryptoError> from_bytes_validated(
        std::span<const uint8_t, kPublicKeySize> bytes
    ) noexcept;
};

/// X25519 secret key (always 32 bytes) — cleared with sodium_memzero
/// X25519 私钥（固定 32 字节）— 使用 sodium_memzero 清零
struct SecretKey : std::array<uint8_t, kSecretKeySize> {
    SecretKey() noexcept;

    // Copy disabled
    // 禁止拷贝
    SecretKey(const SecretKey&) = delete;
    SecretKey& operator=(const SecretKey&) = delete;

    SecretKey(SecretKey&& other) noexcept;
    SecretKey& operator=(SecretKey&& other) noexcept;

    ~SecretKey();

    /// Construct from raw bytes (secure copy, checks for all-zero)
    /// 从原始字节构造（安全拷贝，检查全零）
    [[nodiscard]] static tl::expected<SecretKey, CryptoError> from_bytes(
        std::span<const uint8_t, kSecretKeySize> bytes
    ) noexcept;
};

/// X25519 key pair (public + secret)
/// X25519 密钥对（公钥 + 私钥）
struct KeyPair {
    PublicKey public_key;
    SecretKey secret_key;
};

/// ECDH shared secret (32 bytes) — cleared with sodium_memzero
/// ECDH 共享秘密（32 字节）— 使用 sodium_memzero 清零
struct SharedSecret : std::array<uint8_t, kSharedKeySize> {
    SharedSecret() noexcept;

    SharedSecret(const SharedSecret&) = delete;
    SharedSecret& operator=(const SharedSecret&) = delete;

    SharedSecret(SharedSecret&& other) noexcept;
    SharedSecret& operator=(SharedSecret&& other) noexcept;

    ~SharedSecret();

    [[nodiscard]] static tl::expected<SharedSecret, CryptoError> from_bytes(
        std::span<const uint8_t, kSharedKeySize> bytes
    ) noexcept;
};

/// AEAD symmetric key (32 bytes) — cleared with sodium_memzero
/// AEAD 对称密钥（32 字节）— 使用 sodium_memzero 清零
struct SymmetricKey : std::array<uint8_t, kSymmetricKeySize> {
    SymmetricKey() noexcept;

    SymmetricKey(const SymmetricKey&) = delete;
    SymmetricKey& operator=(const SymmetricKey&) = delete;

    SymmetricKey(SymmetricKey&& other) noexcept;
    SymmetricKey& operator=(SymmetricKey&& other) noexcept;

    ~SymmetricKey();

    [[nodiscard]] static tl::expected<SymmetricKey, CryptoError> from_bytes(
        std::span<const uint8_t, kSymmetricKeySize> bytes
    ) noexcept;
};

/// XChaCha20 nonce (24 bytes)
/// XChaCha20 随机数（24 字节）
///
/// ⚠️ IMPORTANT:
///   - Default constructor is deleted; must create via generate_nonce() or from_bytes()
///   - Nonce MUST be unique per key! Reusing a nonce destroys security completely
///   - Recommended to always use generate_nonce() for random nonces
///
/// ⚠️ 重要:
///   - 默认构造函数被删除，必须通过 generate_nonce() 或 from_bytes() 创建
///   - 每个密钥下的 nonce 必须唯一！重复使用 nonce 会彻底破坏安全性
///   - 建议始终使用 generate_nonce() 生成随机 nonce
struct Nonce : std::array<uint8_t, kNonceSize> {
    Nonce() = delete;

    /// Construct from raw bytes
    /// 从原始字节构造
    explicit Nonce(std::span<const uint8_t, kNonceSize> bytes) noexcept;

    /// Construct from raw bytes (never fails)
    /// 从原始字节构造（永不失败）
    static Nonce from_bytes(std::span<const uint8_t, kNonceSize> bytes) noexcept;
};


//  Initialization
//  初始化

/// Initialize libsodium. Must be called before any other crypto function.
/// Thread safety: Can be called multiple times; only the first call has effect.
///
/// Returns:
///   - True: Initialize success
///   - kInitializationFailed: libsodium can't initialize
///
/// 初始化 libsodium。在调用任何其他加密函数前，必须调用此函数。
/// 线程安全：可被多次调用，只有第一次生效。
///
/// 返回:
///   - True: 初始化成功
///   - kInitializationFailed: 无法初始化

[[nodiscard]] tl::expected<bool, CryptoError> crypto_init() noexcept;

/// Check if libsodium is initialized
/// 检查 libsodium 是否已成功初始化
bool crypto_is_initialized() noexcept;


//  Utility Functions
//  工具函数

/// Constant-time comparison of two byte arrays
/// Uses sodium_memcmp to prevent timing attacks
/// If lengths differ, returns false (length information is not considered secret)
///
/// 常量时间比较两个字节数组
/// 使用 sodium_memcmp，防止时序侧信道
/// 若长度不等，直接返回 false（长度信息本身不被视为秘密）
bool secure_compare(std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept;

/// Securely zero memory (using sodium_memzero, cannot be optimized away)
/// 安全清零内存区域（使用 sodium_memzero，不可被优化掉）
void secure_zero(std::span<uint8_t> data) noexcept;

/// Specializations
/// 特化
inline void secure_zero(SecretKey& key) noexcept {
    secure_zero(std::span<uint8_t>(key.data(), key.size()));
}
inline void secure_zero(SharedSecret& secret) noexcept {
    secure_zero(std::span<uint8_t>(secret.data(), secret.size()));
}
inline void secure_zero(SymmetricKey& key) noexcept {
    secure_zero(std::span<uint8_t>(key.data(), key.size()));
}

/// Hex encoding (may throw std::bad_alloc)
/// 十六进制编码（可能抛出 std::bad_alloc）
std::string to_hex(std::span<const uint8_t> data);

/// Hex decoding (may throw std::bad_alloc)
/// 十六进制解码（可能抛出 std::bad_alloc）

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> from_hex(std::string_view hex);


//  Public Key Validation
//  公钥验证

/// Validate that an X25519 public key is not all-zero.
///
/// This function only performs the all-zero check.
/// Low-order point validation is performed by compute_shared_secret().

/// 验证 X25519 公钥不是全零。
///
/// 本函数只检查全零公钥。
/// 低阶点检查由 compute_shared_secret() 进行。

/// 返回 true 表示公钥不是由全零组成的。
bool validate_public_key(const PublicKey& key) noexcept;


//  Key Generation & Exchange
//  密钥生成与交换

/// Generate a new X25519 key pair (may throw std::bad_alloc)
/// 生成新的 X25519 密钥对（可能抛出 std::bad_alloc）
[[nodiscard]] tl::expected<KeyPair, CryptoError> generate_keypair() noexcept;

/// Derive public key from secret key (scalarmult_base)
/// 从私钥派生公钥（scalarmult_base）
[[nodiscard]] tl::expected<PublicKey, CryptoError> derive_public_key(const SecretKey& secret) noexcept;

/// Compute X25519 shared secret (with public key validation)
/// 计算 X25519 共享秘密（带公钥验证）
[[nodiscard]] tl::expected<SharedSecret, CryptoError> compute_shared_secret(
    const SecretKey& local_secret,
    const PublicKey& remote_public
) noexcept;

/// Compute X25519 shared secret (skip public key validation, for performance-critical scenarios)
///
/// ⚠️ WARNING:
///   Caller MUST ensure remote_public has been validated (non-zero, non-low-order).
///   Invalid public key input may cause scalar multiplication to fail or produce an invalid shared secret, which this function rejects.
///
/// ⚠️ 警告:
///   调用者必须确保 remote_public 已经过验证（非零、非低阶点）。
///   错误的公钥输入可能导致标量乘法失败或产生无效共享秘密，本函数会拒绝该结果。
[[nodiscard]] tl::expected<SharedSecret, CryptoError> compute_shared_secret_unchecked(
    const SecretKey& local_secret,
    const PublicKey& remote_public
) noexcept;


//  Key Derivation (Full HKDF-BLAKE2b)
//  密钥派生 (Full HKDF-BLAKE2b)

/// Derive a symmetric key from a shared secret using full HKDF (Extract + Expand).
///
/// Implementation details:
///
///   Extract (keyed BLAKE2b):
///     PRK = HMAC-BLAKE2b(key = salt, message = shared_secret, hash_len = 64)
///
///   Expand:
///     T(0) = empty
///     T(i) = HMAC-BLAKE2b(key = PRK, message = T(i-1) || info || i, hash_len = output_len)
///     OKM = T(1)  (when output_len <= 64, only one iteration is needed)
///
///   This follows the RFC 5869 HKDF construction using HMAC-BLAKE2b.
///
/// Parameters:
///   - shared_secret: Raw ECDH output
///   - salt:          Salt (recommended >= 16 bytes, can be empty but reduces security)
///   - info:          Context string for domain separation
///   - output_len:    Output length, must be > 0 and <= 255 * 64
///
/// Returns:
///   - On success: std::vector<uint8_t> (length = output_len)
///   - On failure: CryptoError
///
/// Exceptions: std::bad_alloc (memory allocation failure)
///
/// ⚠️ This function is NOT marked noexcept because std::vector may throw std::bad_alloc.
///
/// 从共享秘密派生对称密钥，使用完整的 HKDF 构造（Extract + Expand）。
///
/// 实现细节:
///
///   Extract (密钥化 BLAKE2b):
///     PRK = HMAC-BLAKE2b(key = salt, message = shared_secret, hash_len = 64)
///
///   Expand:
///     T(0) = empty
///     T(i) = HMAC-BLAKE2b(PRK, T(i-1) || info || i)
///     OKM = T(1)  (当 output_len <= 64 时只需一次迭代)
///
///   这遵循 RFC 5869 的 HKDF 构造，并使用 HMAC-BLAKE2b。
///
/// 参数:
///   - shared_secret: 原始 ECDH 输出
///   - salt:          盐值（建议至少 16 字节，可为空但会降低安全性）
///   - info:          上下文字符串，用于域隔离
///   - output_len:    输出长度，必须 > 0 且 <= 255 * 64
///
/// 返回:
///   - 成功返回 std::vector<uint8_t>（长度 = output_len）
///   - 失败返回 CryptoError
///
/// 可能抛出的异常：std::bad_alloc（内存分配失败）
///
/// ⚠️ 注意：本函数未标记 noexcept，因为 std::vector 可能抛出 std::bad_alloc。

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError>
hmac_blake2b(
    std::span<const uint8_t> key,
    std::span<const uint8_t> message
);

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> derive_key_hkdf( 
    const SharedSecret& shared_secret,
    std::span<const uint8_t> salt,
    std::span<const uint8_t> info,
    size_t output_len = kSymmetricKeySize
);

/// Convenience: derive 32-byte SymmetricKey
///
/// ⚠️ NOT marked noexcept because it calls derive_key_hkdf which may throw std::bad_alloc.
///
/// 便捷重载：派生 32 字节 SymmetricKey
///
/// ⚠️ 注意：本函数未标记 noexcept，因为内部调用 derive_key_hkdf 可能抛出 std::bad_alloc。
[[nodiscard]] tl::expected<SymmetricKey, CryptoError> derive_symmetric_key(
    const SharedSecret& shared_secret,
    std::span<const uint8_t> salt,
    std::span<const uint8_t> info
);

/// Convenience: accepts string_view
///
/// ⚠️ NOT marked noexcept because it calls derive_key_hkdf which may throw std::bad_alloc.
///
/// 便捷重载：接受 string_view
///
/// ⚠️ 注意：本函数未标记 noexcept，因为内部调用 derive_key_hkdf 可能抛出 std::bad_alloc。
[[nodiscard]] tl::expected<SymmetricKey, CryptoError> derive_symmetric_key(
    const SharedSecret& shared_secret,
    std::string_view salt,
    std::string_view info
);


[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> derive_key_kdf(
    std::span<const uint8_t, kSymmetricKeySize> master_key,
    uint64_t subkey_id,              // 必须非 0
    std::string_view context,        // 最多 8 字节
    size_t output_len = kSymmetricKeySize
);

[[nodiscard]] tl::expected<SymmetricKey, CryptoError> derive_symmetric_key_kdf(
    std::span<const uint8_t, kSymmetricKeySize> master_key,
    uint64_t subkey_id,
    std::string_view context
);


//  AEAD: Preallocated buffer (recommended)
//  认证加密：预分配缓冲区版本（推荐）

/// Encrypt plaintext with associated data, writing to a preallocated buffer.
///
/// Buffer size check:
///   - out.size() must be >= plaintext.size() + kAeadTagSize
///   - If not satisfied, returns kBufferTooSmall
///
/// Parameters:
///   - key:        Symmetric key
///   - nonce:      Nonce (must be unique per key)
///   - plaintext:  Data to encrypt
///   - aad:        Additional authenticated data
///   - out:        Output buffer
///
/// Returns:
///   - On success: bytes written (= plaintext.size() + kAeadTagSize)
///   - On failure: CryptoError
///
/// 使用关联数据加密明文，写入预分配缓冲区。
///
/// 缓冲区大小检查:
///   - out.size() 必须 >= plaintext.size() + kAeadTagSize
///   - 若不满足，返回 kBufferTooSmall
///
/// 参数:
///   - key:        对称密钥
///   - nonce:      随机数（每个密钥下必须唯一）
///   - plaintext:  待加密数据
///   - aad:        附加认证数据
///   - out:        输出缓冲区
///
/// 返回:
///   - 成功返回写入的字节数（= plaintext.size() + kAeadTagSize）
///   - 失败返回 CryptoError

[[nodiscard]] tl::expected<size_t, CryptoError> encrypt_aead(
    const SymmetricKey& key,
    const Nonce& nonce,
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t> aad,
    std::span<uint8_t> out
) noexcept;

/// Decrypt ciphertext and verify associated data, writing to a preallocated buffer.
///
/// Buffer size check:
///   - out.size() must be >= ciphertext.size() - kAeadTagSize
///   - If not satisfied, returns kBufferTooSmall
///
/// Parameters:
///   - key:        Symmetric key
///   - nonce:      Nonce used during encryption
///   - ciphertext: Ciphertext (includes MAC at the end)
///   - aad:        Same AAD used during encryption
///   - out:        Output buffer
///
/// Returns:
///   - On success: bytes written (= ciphertext.size() - kAeadTagSize)
///   - kAuthenticationFailed: Authentication failed
///   - kInvalidCiphertext: Ciphertext too short
///
/// 解密密文并验证关联数据，写入预分配缓冲区。
///
/// 缓冲区大小检查:
///   - out.size() 必须 >= ciphertext.size() - kAeadTagSize
///   - 若不满足，返回 kBufferTooSmall
///
/// 参数:
///   - key:        对称密钥
///   - nonce:      加密时使用的随机数
///   - ciphertext: 密文（末尾包含 MAC）
///   - aad:        加密时使用的相同 AAD
///   - out:        输出缓冲区
///
/// 返回:
///   - 成功返回写入的字节数（= ciphertext.size() - kAeadTagSize）
///   - kAuthenticationFailed: 认证失败
///   - kInvalidCiphertext: 密文长度不足
[[nodiscard]] tl::expected<size_t, CryptoError> decrypt_aead(
    const SymmetricKey& key,
    const Nonce& nonce,
    std::span<const uint8_t> ciphertext,
    std::span<const uint8_t> aad,
    std::span<uint8_t> out
) noexcept;


//  AEAD: Dynamic allocation (convenience)
//  认证加密：动态分配版本（便捷）

/// Encrypt (returns vector)
/// May throw std::bad_alloc (memory allocation failure)
///
/// ⚠️ NOT marked noexcept because std::vector may throw std::bad_alloc.
///
/// 加密（返回 vector）
/// 可能抛出 std::bad_alloc（内存分配失败）
///
/// ⚠️ 注意：本函数未标记 noexcept，因为 std::vector 可能抛出 std::bad_alloc。
#ifndef NUMOTIRUS_CRYPTO_NO_HEAP

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> encrypt_aead(
    const SymmetricKey& key,
    const Nonce& nonce,
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t> aad = {}
);

/// Decrypt (returns vector)
/// May throw std::bad_alloc (memory allocation failure)
///
/// ⚠️ NOT marked noexcept because std::vector may throw std::bad_alloc.
///
/// 解密（返回 vector）
/// 可能抛出 std::bad_alloc（内存分配失败）
///
/// ⚠️ 注意：本函数未标记 noexcept，因为 std::vector 可能抛出 std::bad_alloc。

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> decrypt_aead(
    const SymmetricKey& key,
    const Nonce& nonce,
    std::span<const uint8_t> ciphertext,
    std::span<const uint8_t> aad = {}
);

#endif


//  Randomness Generation
//  随机数生成

/// Generate a cryptographically secure random Nonce
/// 生成安全随机 Nonce

[[nodiscard]] tl::expected<Nonce, CryptoError> generate_nonce() noexcept;

/// Generate arbitrary length secure random bytes (may throw std::bad_alloc)
///
/// ⚠️ NOT marked noexcept because std::vector may throw std::bad_alloc.
///
/// 生成任意长度的安全随机字节（可能抛出 std::bad_alloc）
///
/// ⚠️ 注意：本函数未标记 noexcept，因为 std::vector 可能抛出 std::bad_alloc。

#ifndef NUMOTIRUS_CRYPTO_NO_HEAP

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> random_bytes(size_t count);

#endif

/// Write random bytes to a preallocated buffer
/// 将随机字节写入预分配缓冲区
[[nodiscard]] tl::expected<void, CryptoError> random_bytes_fill(std::span<uint8_t> out) noexcept;

} // namespace crypto
} // namespace numotirus