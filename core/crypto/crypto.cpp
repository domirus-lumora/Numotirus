#include "crypto.hpp"

#include <algorithm>
#include <atomic>
#include <charconv>
#include <format>
#include <limits>

#include <sodium.h>

namespace numotirus{
namespace crypto {

namespace{
    std::atomic<bool> sodium_init_status = false;
}

const char* crypto_error_string(CryptoError error) noexcept {
    switch(error) {
        case CryptoError::kSuccess:
            return "Success.";
        case CryptoError::kNotInitialized:
            return "libsodium has not initialized yet";
        case CryptoError::kInvalidInput:
            return "Invalid input parameter";
        case CryptoError::kInvalidPublicKey:
            return "Invalid public key, might be caused by zero or low-order";
        case CryptoError::kInvalidSecretKey:
            return "Invalid secret key, might be all-zero";
        case CryptoError::kAuthenticationFailed:
            return "Authentication failed";
        case CryptoError::kEncryptionFailed:
            return "Encryption failed (internal error)";
        case CryptoError::kDecryptionFailed:
            return "Decryption failed (internal error)";
        case CryptoError::kDerivationFailed:
            return "Key derivation failed";
        case CryptoError::kKeyPairGenerationFailed:
            return "KeyPair generation failed.";
        case CryptoError::kInitializationFailed :
            return "libsodium initialzation failed.";
        case CryptoError::kMemoryError:
            return "Memory allocation failed";
        case CryptoError::kInvalidCiphertext:
            return "Invalid ciphertext format, might be too short";
        case CryptoError::kBufferTooSmall:
            return "Output buffer too small";
        case CryptoError::kRandomGenerationFailed:
            return "Random generation failed";
        case CryptoError::kInvalidLength :
            return "Invalid length parameter";
        case CryptoError::kSaltTooShort:
            return "Salt is too short";
        case CryptoError::kPermissionDenied:
            return "Permission denied. Don't do something dangerous";
        case CryptoError::kSubkeyIdInvalid:
            return "Your subkey_id is 0";
        case CryptoError::kKdfContextTooLong:
            return "context must be less than or equal to 8 bytes";
    }
    return "Unknown Error";
}

//X25519 public Key
//X25519 公钥
PublicKey::PublicKey() noexcept {
    this -> fill(0);
}
PublicKey::PublicKey(std::span<const uint8_t, kPublicKeySize> bytes) noexcept {
    std::copy (bytes.begin(), bytes.end(), this ->begin());
}
tl::expected<PublicKey, CryptoError> PublicKey::from_bytes_validated(std::span<const uint8_t, kPublicKeySize> bytes) noexcept {
    PublicKey key(bytes);
    if (!sodium_is_zero(key.data(),key.size())) { //If there is a digit which is not 0, then means the public key is formed by at least one non-zero digit, then it is a valid public key
        return key;
    }
    return tl::unexpected(CryptoError::kInvalidPublicKey);
}

// X25519 Secret Key
// X25519 私钥
SecretKey::SecretKey() noexcept {
    this -> fill(0);
}

using base = std::array<uint8_t, kSecretKeySize>;
SecretKey::SecretKey(SecretKey&& other) noexcept : base(other){
    secure_zero(other);
};

SecretKey& SecretKey::operator=(SecretKey&& other) noexcept {
    if (this != &other) { //Check is other same as this, to prevent someone use a = std::copy(a) 检查other是不是和this是同一个对象，来避免有人用 a = std::copy(a)
        secure_zero(*this);
        std::copy(other.begin(), other.end(),begin());
        secure_zero(other);
    }
    return *this;
}

SecretKey::~SecretKey() {
    sodium_memzero(this ->data(), this ->size());
}

tl::expected<SecretKey, CryptoError> SecretKey::from_bytes(std::span<const uint8_t, kSecretKeySize> bytes) noexcept {
    SecretKey s_key;
    std::copy(bytes.begin(),bytes.end(),s_key.begin());
    if (sodium_is_zero(s_key.data(),s_key.size())) {
        return tl::unexpected(CryptoError::kInvalidSecretKey);
    }
    return s_key;

}

/// ECDH shared secret (32 bytes) — cleared with sodium_memzero
/// ECDH 共享秘密（32 字节）— 使用 sodium_memzero 清零
SharedSecret::SharedSecret() noexcept {
    this -> fill(0);
}

SharedSecret::SharedSecret(SharedSecret&& other) noexcept{
    std::copy(other.begin(),other.end(),this->begin());
    secure_zero(other);
}

SharedSecret& SharedSecret::operator=(SharedSecret&& other) noexcept {
    if(this != &other) {
        secure_zero(*this);
        std::copy(other.begin(),other.end(),this->begin());
        secure_zero(other);
    }
    return *this;
}

SharedSecret::~SharedSecret() {
    sodium_memzero(this ->data(),this ->size());
};

tl::expected<SharedSecret, CryptoError> SharedSecret::from_bytes(std::span<const uint8_t, kSharedKeySize> bytes) noexcept {
    SharedSecret secret;
    if (sodium_is_zero(bytes.data(),bytes.size())) {
        return tl::unexpected(CryptoError::kInvalidInput);
    }
    std::copy(bytes.begin(),bytes.end(),secret.begin());
    return secret;
}

/// AEAD symmetric key (32 bytes) — cleared with sodium_memzero
/// AEAD 对称密钥（32 字节）— 使用 sodium_memzero 清零
SymmetricKey::SymmetricKey() noexcept {
    this->fill(0);
}

SymmetricKey::SymmetricKey(SymmetricKey&& other) noexcept { //Sym a = Sym(std::move(b))
    std::copy(other.begin(),other.end(),this->begin());
    secure_zero(other);
}

SymmetricKey& SymmetricKey::operator=(SymmetricKey&& other) noexcept { //a = std::move(b)
    if(this != &other) {
        secure_zero(*this);
        std::copy(other.begin(),other.end(),this->begin());
        secure_zero(other);
    }
    return *this;
}

SymmetricKey::~SymmetricKey() {
    sodium_memzero(this ->data(), this ->size());
}

[[nodiscard]] tl::expected<SymmetricKey, CryptoError> SymmetricKey::from_bytes(std::span<const uint8_t, kSymmetricKeySize> bytes) noexcept {
    SymmetricKey symmetric_key;
    std::copy(bytes.begin(),bytes.end(),symmetric_key.begin());
    return symmetric_key;
}

/// XChaCha20 nonce (24 bytes)
/// XChaCha20 随机数（24 字节）
    /// Construct from raw bytes
    /// 从原始字节构造
    Nonce::Nonce(std::span<const uint8_t, kNonceSize> bytes) noexcept {
        std::copy(bytes.begin(),bytes.end(),this ->begin());
    }

    /// Construct from raw bytes (never fails)
    /// 从原始字节构造（永不失败）
    Nonce Nonce::from_bytes(std::span<const uint8_t, kNonceSize> bytes) noexcept {
        return Nonce(bytes);
    };

//  Initialization
//  初始化
[[nodiscard]] tl::expected<bool, CryptoError> crypto_init() noexcept {
    if (!crypto_is_initialized()) {
        int result = sodium_init();
        if (result < 0) {
            sodium_init_status.store(false);
            return tl::unexpected(CryptoError::kInitializationFailed);
        }
        sodium_init_status.store(true);
    }
    return true;
}

/// Check if libsodium is initialized
/// 检查 libsodium 是否已成功初始化
bool crypto_is_initialized() noexcept {
    return sodium_init_status.load();
}

//  Utility Functions
//  工具函数
bool secure_compare(std::span<const uint8_t> a, std::span<const uint8_t> b) noexcept {
    if (a.size() == b.size()) {
        int result = sodium_memcmp(a.data(),b.data(),a.size());
        if (result == 0) return true;
        else return false;
    }
    else return false;
}

void secure_zero(std::span<uint8_t> data) noexcept {
    sodium_memzero(data.data(),data.size());
}

std::string to_hex(std::span<const uint8_t> data) {
    std::string result;
    for (auto bytes : data) {
        result += std::format("{:02x}",bytes);
    }
    return result;
}

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> from_hex(std::string_view hex) {
    if (hex.size() % 2 != 0) return tl::unexpected(CryptoError::kInvalidInput);
    std::vector<uint8_t> result;
    result.reserve(hex.size() / 2);

    size_t i = 0;
    while(i<hex.size()) {
        unsigned int value;

        auto[ptr,ec] = std::from_chars(hex.data() +i, hex.data() + i + 2, value, 16);

        if (ec != std::errc{} || ptr != hex.data() + i +2) {
            return tl::unexpected(CryptoError::kInvalidInput);
        }
        result.push_back(static_cast<uint8_t> (value));
        i += 2;
    }
    return result;
}

//  Public Key Validation
//  公钥验证
bool validate_public_key(const PublicKey& key) noexcept {
    if (!sodium_is_zero(key.data(),key.size())) return true;

    return false;
}

//  Key Generation & Exchange
//  密钥生成与交换
tl::expected<KeyPair, CryptoError> [[nodiscard]] generate_keypair() noexcept {
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    PublicKey public_key;
    SecretKey secret_key;
    int result = crypto_kx_keypair(public_key.data(), secret_key.data());

    if (result != 0) return tl::unexpected(CryptoError::kKeyPairGenerationFailed);
    if (!validate_public_key(public_key)) return tl::unexpected(CryptoError::kInvalidPublicKey);
    if (sodium_is_zero(secret_key.data(),secret_key.size())) return tl::unexpected(CryptoError::kInvalidSecretKey);

    KeyPair keypair(public_key,std::move(secret_key));
    return keypair;
}

tl::expected<PublicKey, CryptoError> [[nodiscard]] derive_public_key(const SecretKey& secret) noexcept {
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    PublicKey public_key;
    int result = crypto_scalarmult_curve25519_base (public_key.data(),secret.data());
    if (result != 0) return tl::unexpected(CryptoError::kDerivationFailed);
    if(!validate_public_key(public_key)) {
        return tl::unexpected(CryptoError::kInvalidPublicKey);
    }
    return public_key;
}

tl::expected<SharedSecret, CryptoError> [[nodiscard]] compute_shared_secret(
    const SecretKey& local_secret,
    const PublicKey& remote_public
) noexcept {
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    if (!validate_public_key(remote_public)) return tl::unexpected(CryptoError::kInvalidPublicKey);
    SharedSecret shared_secret;
    int result = crypto_scalarmult_curve25519(shared_secret.data(),local_secret.data(),remote_public.data());
    if (result == 0) {
        if(!sodium_is_zero(shared_secret.data(),shared_secret.size())) {
            return shared_secret;
        }
        else return tl::unexpected(CryptoError::kDerivationFailed);
    }
    else return tl::unexpected(CryptoError::kDerivationFailed);
}

tl::expected<SharedSecret, CryptoError>
compute_shared_secret_unchecked(
    const SecretKey& local_secret,
    const PublicKey& remote_public
) noexcept {
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    SharedSecret shared_secret;

    int result = crypto_scalarmult_curve25519(
        shared_secret.data(),
        local_secret.data(),
        remote_public.data()
    );

    if (result != 0) return tl::unexpected(CryptoError::kDerivationFailed);

    if (sodium_is_zero(shared_secret.data(), shared_secret.size()))
        return tl::unexpected(CryptoError::kDerivationFailed);

    return shared_secret;
}

//  Key Derivation (Full HKDF-BLAKE2b)
//  密钥派生 (Full HKDF-BLAKE2b)
[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError>
hmac_blake2b(
    std::span<const uint8_t> key,
    std::span<const uint8_t> message
) { // HMAC(K, M) = H((K' XOR opad) || H((K' XOR ipad) || M))
    // HMAC(K, M) = H((K' XOR opad) || H(k_ipad))
    // HMAC(K, M) = H((K' XOR opad) || inner)
    // HMAC(K, M) = H(k_opad)
    // HMAC(K, M) = outer
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    if(key.size() > 128) return tl::unexpected(CryptoError::kInvalidInput);

    constexpr uint8_t opad = 0x5c;
    constexpr uint8_t ipad = 0x36;

    std::vector<uint8_t> outer(64); // outer = H(k_opad)
    std::vector<uint8_t> inner(64); // inner = H(k_ipad)

    std::vector<uint8_t> k_opad(128,opad); // k_opad = (k' XOR opad) || inner
    std::vector<uint8_t> k_ipad(128,ipad); // k_ipad = (k' XOR ipad) || message

    size_t i = 0;
    while (i<key.size())
    {
       k_ipad[i] = key[i] ^ ipad;
        i++;
    };
    i = 0;
    k_ipad.insert(k_ipad.end(),message.begin(),message.end());
    int result = crypto_generichash(inner.data(),inner.size(),k_ipad.data(),k_ipad.size(),nullptr,0);
    if(result != 0) {
        secure_zero(k_ipad);
        secure_zero(k_opad);
        secure_zero(inner);
        return tl::unexpected(CryptoError::kDerivationFailed);
    }
    result = 0;
    while (i<key.size())
    {
       k_opad[i] = key[i] ^ opad;
        i++;
    };
    k_opad.insert(k_opad.end(),inner.begin(),inner.end());
    result = crypto_generichash(outer.data(),outer.size(),k_opad.data(),k_opad.size(),nullptr,0);
    if(result != 0) {
        secure_zero(k_ipad);
        secure_zero(k_opad);
        secure_zero(inner);
        return tl::unexpected(CryptoError::kDerivationFailed);
    }
    secure_zero(k_ipad);
    secure_zero(k_opad);
    secure_zero(inner);
    return outer;
}

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> derive_key_hkdf(
    const SharedSecret& shared_secret,
    std::span<const uint8_t> salt,
    std::span<const uint8_t> info,
    size_t output_len = kSymmetricKeySize
) {
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    if (output_len == 0 || output_len > 255 * BLAKE2B_MAX_OUTPUT) return tl::unexpected(CryptoError::kInvalidLength);

    auto hmac_result = hmac_blake2b(salt,shared_secret);
    if(!hmac_result) return tl::unexpected(hmac_result.error());
    std::vector<uint8_t> prk = std::move(*hmac_result);

    uint8_t counter = 0x01;
    // T(n) = HMAC(PRK, T(n-1) || info || counter)
    std::vector<uint8_t> previous;
    std::vector<uint8_t> OKM;
    std::vector<uint8_t> message;
    do {
        message.clear();
        message.insert(message.end(),previous.begin(),previous.end());
        message.insert(message.end(),info.begin(),info.end());
        message.push_back(counter);
        auto kdf_result = hmac_blake2b(prk,message);
        if(!kdf_result) {
            secure_zero(prk);
            secure_zero(previous);
            secure_zero(message);
            return tl::unexpected(kdf_result.error());
        }

        previous = std::move(*kdf_result);
        OKM.insert(OKM.end(),previous.begin(),previous.end());
        counter = counter + int(1);
    }
    while (OKM.size() < output_len);
    OKM.resize(output_len);

    secure_zero(prk);
    secure_zero(previous);
    secure_zero(message);

    return OKM;
}

[[nodiscard]] tl::expected<SymmetricKey, CryptoError> derive_symmetric_key(
    const SharedSecret& shared_secret,
    std::span<const uint8_t> salt,
    std::span<const uint8_t> info
) {
    auto result = derive_key_hkdf(shared_secret,salt,info,kSymmetricKeySize);
    if (!result) return tl::unexpected(result.error());
    std::vector<uint8_t> OKM = std::move(*result);

    if(OKM.size() != kSymmetricKeySize) {
        secure_zero(OKM);
        return tl::unexpected(CryptoError::kDerivationFailed);
    }
    SymmetricKey symmetric_key;
    std::copy(OKM.begin(),OKM.end(),symmetric_key.begin());
    secure_zero(OKM);

    return symmetric_key;
}

[[nodiscard]] tl::expected<SymmetricKey, CryptoError> derive_symmetric_key(
    const SharedSecret& shared_secret,
    std::string_view salt,
    std::string_view info
) {
    std::span<const uint8_t> salt_bytes(
        reinterpret_cast<const uint8_t*>(salt.data()),
        salt.size()
    );

    std::span<const uint8_t> info_bytes(
        reinterpret_cast<const uint8_t*>(info.data()),
        info.size()
    );

    return derive_symmetric_key(shared_secret, salt_bytes, info_bytes);
}

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> derive_key_kdf(
    std::span<const uint8_t, kSymmetricKeySize> master_key,
    uint64_t subkey_id,              // 必须非 0
    std::string_view context,        // 最多 8 字节
    size_t output_len = kSymmetricKeySize
) {
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    if(subkey_id == 0) return tl::unexpected(CryptoError::kSubkeyIdInvalid);
    if(context.size() > crypto_kdf_CONTEXTBYTES) return tl::unexpected(CryptoError::kKdfContextTooLong);
    if(output_len < crypto_kdf_BYTES_MIN || output_len > crypto_kdf_BYTES_MAX) return tl::unexpected(CryptoError::kInvalidLength);

    char ctx[crypto_kdf_CONTEXTBYTES]{};
    std::copy(context.begin(),context.end(),ctx);
    std::vector<uint8_t> output(output_len);
    int result = crypto_kdf_derive_from_key(output.data(),output_len,subkey_id,ctx,master_key.data());
    if (result != 0) {
        secure_zero(output);
        return tl::unexpected(CryptoError::kDerivationFailed);
    }

    return output;
}

[[nodiscard]] tl::expected<SymmetricKey, CryptoError> derive_symmetric_key_kdf(
    std::span<const uint8_t, kSymmetricKeySize> master_key,
    uint64_t subkey_id,
    std::string_view context
) {
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    auto result = derive_key_kdf(master_key,subkey_id,context);
    if(!result) return tl::unexpected(result.error());
    SymmetricKey symmetric_key;
    std::copy(result ->begin(),result ->end(),symmetric_key.begin());

    secure_zero(*result);

    if(sodium_is_zero(symmetric_key.data(),symmetric_key.size())) return tl::unexpected(CryptoError::kDerivationFailed);
    return symmetric_key;
}

//  AEAD: Preallocated buffer (recommended)
//  认证加密：预分配缓冲区版本（推荐）
[[nodiscard]] tl::expected<size_t, CryptoError> encrypt_aead(
    const SymmetricKey& key,
    const Nonce& nonce,
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t> aad,
    std::span<uint8_t> out
) noexcept {
    if (!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    if (plaintext.size() > std::numeric_limits<size_t>::max() - kAeadTagSize)
        return tl::unexpected(CryptoError::kInvalidLength);
    if(out.size() < plaintext.size() + kAeadTagSize) return tl::unexpected(CryptoError::kBufferTooSmall);
    unsigned long long ciphertext_len = 0;

    int result = crypto_aead_xchacha20poly1305_ietf_encrypt(out.data(),&ciphertext_len, plaintext.data(),plaintext.size(),aad.data(),aad.size(),nullptr,nonce.data(),key.data());
    if(result != 0) return tl::unexpected(CryptoError::kEncryptionFailed);

    return ciphertext_len;
}

[[nodiscard]] tl::expected<size_t, CryptoError> decrypt_aead(
    const SymmetricKey& key,
    const Nonce& nonce,
    std::span<const uint8_t> ciphertext,
    std::span<const uint8_t> aad,
    std::span<uint8_t> out
) noexcept {
    if (!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    if(ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) return tl::unexpected(CryptoError::kInvalidCiphertext);
    if(out.size() < ciphertext.size() - kAeadTagSize) return tl::unexpected(CryptoError::kBufferTooSmall);
    unsigned long long plaintext_len = 0;

    int result = crypto_aead_xchacha20poly1305_ietf_decrypt(out.data(),&plaintext_len,nullptr,ciphertext.data(),ciphertext.size(),aad.data(),aad.size(),nonce.data(),key.data());
    if(result != 0) {
    secure_zero(out.first(ciphertext.size() - kAeadTagSize));
    return tl::unexpected(CryptoError::kAuthenticationFailed);
    }

    return plaintext_len;
}

//  AEAD: Dynamic allocation (convenience)
//  认证加密：动态分配版本（便捷）
#ifndef NUMOTIRUS_CRYPTO_NO_HEAP

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> encrypt_aead(
    const SymmetricKey& key,
    const Nonce& nonce,
    std::span<const uint8_t> plaintext,
    std::span<const uint8_t> aad = {}
) {
    if (!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    if (plaintext.size() > std::numeric_limits<size_t>::max() - kAeadTagSize)
        return tl::unexpected(CryptoError::kInvalidLength);
    std::vector<uint8_t> out(plaintext.size()+kAeadTagSize);
    unsigned long long ciphertext_len = 0;

    int result = crypto_aead_xchacha20poly1305_ietf_encrypt(out.data(),&ciphertext_len, plaintext.data(),plaintext.size(),aad.data(),aad.size(),nullptr,nonce.data(),key.data());
    if(result != 0) return tl::unexpected(CryptoError::kEncryptionFailed);

    out.resize(ciphertext_len);
    return out;
}

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> decrypt_aead(
    const SymmetricKey& key,
    const Nonce& nonce,
    std::span<const uint8_t> ciphertext,
    std::span<const uint8_t> aad = {}
) {
    if (!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    if(ciphertext.size() < crypto_aead_xchacha20poly1305_ietf_ABYTES) return tl::unexpected(CryptoError::kInvalidCiphertext);
    std::vector<uint8_t> out(ciphertext.size()-kAeadTagSize);
    unsigned long long plaintext_len = 0;

    int result = crypto_aead_xchacha20poly1305_ietf_decrypt(out.data(),&plaintext_len,nullptr,ciphertext.data(),ciphertext.size(),aad.data(),aad.size(),nonce.data(),key.data());
    if(result != 0) {
        secure_zero(out);
        return tl::unexpected(CryptoError::kAuthenticationFailed);
    }

    out.resize(plaintext_len);
    return out;
}

#endif

//  Randomness Generation
//  随机数生成
[[nodiscard]] tl::expected<Nonce, CryptoError> generate_nonce() noexcept {
    std::array<uint8_t,kNonceSize> bytes;
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    randombytes_buf(bytes.data(),bytes.size());

    if(sodium_is_zero(bytes.data(),bytes.size())) return tl::unexpected(CryptoError::kRandomGenerationFailed);

    Nonce nonce = Nonce::from_bytes(bytes);
    return nonce;
}

#ifndef NUMOTIRUS_CRYPTO_NO_HEAP

[[nodiscard]] tl::expected<std::vector<uint8_t>, CryptoError> random_bytes(size_t count) {
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);

    std::vector<uint8_t> bytes(count);
    randombytes_buf(bytes.data(),bytes.size());

    return bytes;
}

#endif

[[nodiscard]] tl::expected<void, CryptoError> random_bytes_fill(std::span<uint8_t> out) noexcept {
    if(!crypto_is_initialized()) return tl::unexpected(CryptoError::kNotInitialized);
    randombytes_buf(out.data(),out.size());

    return {};
}

}// namespace crypto

}// namespace numotirus
