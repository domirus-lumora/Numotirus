// tct.hpp
// Tesla Coil Transform (TCT) v1.0
// A reversible EML-based transformation
// Security provided by XChaCha20-Poly1305 (libsodium)
// TCT provides obfuscation, not cryptographic security
// Tested: 100k random roundtrips, max error < 5e-6
// — Domirus, 13

#pragma once

#include <cstdint>
#include <vector>
#include <optional>
#include <array>
#include <cmath>

namespace tct {

// ------------------------------------------------------------
// 常量
// ------------------------------------------------------------
constexpr int QUANT_PRECISION = 1'000'000;   // 量化精度 10^6
constexpr int PAYLOAD_SIZE = 12;             // now(4) + quantized(8)
constexpr int TAG_SIZE = 16;                 // XChaCha20-Poly1305 认证标签
constexpr int NONCE_SIZE = 24;               // XChaCha20 需要 24 字节 nonce

constexpr double PLAINTEXT_MIN = -1.0;
constexpr double PLAINTEXT_MAX =  1.0;

// ------------------------------------------------------------
// nonce 类型（24 字节）
// ------------------------------------------------------------
using Nonce = std::array<uint8_t, NONCE_SIZE>;

// ------------------------------------------------------------
// 权重（从 secret 派生）
// ------------------------------------------------------------
struct Weights {
    double w1, w2, w3;
    static Weights from_secret(uint64_t secret);
};

// ------------------------------------------------------------
// 载荷（不再包含 epoch/reserved）
// ------------------------------------------------------------
struct Payload {
    uint32_t now;           // 发送时的时间戳（秒）
    int64_t  quantized;     // 混淆值 × 1e6 取整

    std::array<uint8_t, PAYLOAD_SIZE> to_bytes() const;
    static Payload from_bytes(const std::array<uint8_t, PAYLOAD_SIZE>& bytes);
};

// ------------------------------------------------------------
// Tesla Coil Transform
// ------------------------------------------------------------
class TeslaCoilTransform {
public:
    // secret: 64 位整数（双方共享）
    // key: 32 字节 XChaCha20-Poly1305 密钥
    TeslaCoilTransform(uint64_t secret, const std::array<uint8_t, 32>& key);

    // 正向变换 + AEAD 加密
    // 返回 [nonce(24)] + [ciphertext]
    std::vector<uint8_t> encrypt(double plaintext, const Nonce& nonce) const;

    // 解密 + 逆变换
    // 失败返回 nullopt
    std::optional<double> decrypt(const std::vector<uint8_t>& ciphertext) const;

    // 辅助：原始整数（0..max）↔ 归一化浮点 [-1,1]
    static double scale_to_plaintext(int64_t value, int64_t max_value);
    static int64_t scale_from_plaintext(double plaintext, int64_t max_value);

private:
    uint64_t secret_;
    std::array<uint8_t, 32> key_;
    Weights weights_;

    uint32_t current_timestamp() const;
    double compute_r(uint32_t now, uint64_t secret) const;

    double forward_chain(double x, double r, const Weights& w) const;
    double inverse_chain(double y, double r, const Weights& w) const;

    int64_t double_to_fixed(double x) const;
    double fixed_to_double(int64_t x) const;

    // AEAD 封装（libsodium 的 XChaCha20-Poly1305）
    std::vector<uint8_t> aead_encrypt(const std::array<uint8_t, PAYLOAD_SIZE>& plain,
                                       const Nonce& nonce) const;
    std::optional<std::array<uint8_t, PAYLOAD_SIZE>> aead_decrypt(
        const std::vector<uint8_t>& ciphertext,
        const Nonce& nonce) const;
};

} // namespace tct