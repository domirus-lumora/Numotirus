// tct.cpp
#include "tct.hpp"
#include <cstring>
#include <chrono>
#include <limits>
#include <random>
#include <sodium.h>

namespace tct {

// ------------------------------------------------------------
// 权重派生（SplitMix64 风格）
// ------------------------------------------------------------
static double hash_to_double(uint64_t x, int salt) {
    uint64_t h = x ^ (salt * 0x9e3779b97f4a7c15ULL);
    h = (h ^ (h >> 30)) * 0xbf58476d1ce4e5b9ULL;
    h = (h ^ (h >> 27)) * 0x94d049bb133111ebULL;
    h = h ^ (h >> 31);
    return 1.5 + (h % 1'000'000) / 1'000'000.0;
}

Weights Weights::from_secret(uint64_t secret) {
    Weights w;
    w.w1 = hash_to_double(secret, 1);
    w.w2 = hash_to_double(secret, 2);
    w.w3 = hash_to_double(secret, 3);
    return w;
}

// ------------------------------------------------------------
// 载荷序列化（大端）
// ------------------------------------------------------------
std::array<uint8_t, PAYLOAD_SIZE> Payload::to_bytes() const {
    std::array<uint8_t, PAYLOAD_SIZE> bytes{};

    // now (4 bytes)
    bytes[0] = (now >> 24) & 0xFF;
    bytes[1] = (now >> 16) & 0xFF;
    bytes[2] = (now >>  8) & 0xFF;
    bytes[3] = now & 0xFF;

    // quantized (8 bytes, 有符号转无符号)
    uint64_t uq = static_cast<uint64_t>(quantized);
    for (int i = 0; i < 8; ++i) {
        bytes[4 + i] = (uq >> (56 - i * 8)) & 0xFF;
    }
    return bytes;
}

Payload Payload::from_bytes(const std::array<uint8_t, PAYLOAD_SIZE>& bytes) {
    Payload p{};
    p.now = (static_cast<uint32_t>(bytes[0]) << 24) |
            (static_cast<uint32_t>(bytes[1]) << 16) |
            (static_cast<uint32_t>(bytes[2]) <<  8) |
            static_cast<uint32_t>(bytes[3]);

    uint64_t uq = 0;
    for (int i = 0; i < 8; ++i) {
        uq = (uq << 8) | bytes[4 + i];
    }
    p.quantized = static_cast<int64_t>(uq);
    return p;
}

// ------------------------------------------------------------
// TCT 实现
// ------------------------------------------------------------
TeslaCoilTransform::TeslaCoilTransform(uint64_t secret, const std::array<uint8_t, 32>& key)
    : secret_(secret), key_(key), weights_(Weights::from_secret(secret)) {}

uint32_t TeslaCoilTransform::current_timestamp() const {
    using namespace std::chrono;
    auto sec = duration_cast<seconds>(system_clock::now().time_since_epoch());
    return static_cast<uint32_t>(sec.count());
}

double TeslaCoilTransform::compute_r(uint32_t now, uint64_t secret) const {
    // r = ln(now + secret)
    return std::log(static_cast<double>(now) + static_cast<double>(secret));
}

static inline double eml_forward(double x, double w) {
    return std::exp(x) - std::log(w);
}

static inline double eml_inverse(double y, double w) {
    double arg = y + std::log(w);
    if (arg <= 0.0) return std::numeric_limits<double>::quiet_NaN();
    return std::log(arg);
}

double TeslaCoilTransform::forward_chain(double x, double r, const Weights& w) const {
    double y1 = eml_forward(x, w.w1);
    double y2 = eml_forward(y1, w.w2);
    double y3 = eml_forward(y2, w.w3);
    return y3 + r;
}

double TeslaCoilTransform::inverse_chain(double y, double r, const Weights& w) const {
    double y3 = y - r;
    double y2 = eml_inverse(y3, w.w3);
    if (std::isnan(y2)) return y2;
    double y1 = eml_inverse(y2, w.w2);
    if (std::isnan(y1)) return y1;
    double x = eml_inverse(y1, w.w1);
    return x;
}

int64_t TeslaCoilTransform::double_to_fixed(double x) const {
    return std::llround(x * QUANT_PRECISION);
}

double TeslaCoilTransform::fixed_to_double(int64_t x) const {
    return static_cast<double>(x) / QUANT_PRECISION;
}

// ------------------------------------------------------------
// 加密与解密
// ------------------------------------------------------------
std::vector<uint8_t> TeslaCoilTransform::encrypt(double plaintext, const Nonce& nonce) const {
    uint32_t now = current_timestamp();
    double r = compute_r(now, secret_);
    double c1 = forward_chain(plaintext, r, weights_);
    int64_t quantized = double_to_fixed(c1);

    Payload payload{now, quantized};
    auto plain_bytes = payload.to_bytes();

    auto cipher = aead_encrypt(plain_bytes, nonce);

    std::vector<uint8_t> result;
    result.reserve(NONCE_SIZE + cipher.size());
    result.insert(result.end(), nonce.begin(), nonce.end());
    result.insert(result.end(), cipher.begin(), cipher.end());
    return result;
}

std::optional<double> TeslaCoilTransform::decrypt(const std::vector<uint8_t>& ciphertext) const {
    if (ciphertext.size() < NONCE_SIZE + TAG_SIZE) return std::nullopt;

    Nonce nonce;
    for (size_t i = 0; i < NONCE_SIZE; ++i) nonce[i] = ciphertext[i];

    std::vector<uint8_t> enc(ciphertext.begin() + NONCE_SIZE, ciphertext.end());
    auto opt_payload = aead_decrypt(enc, nonce);
    if (!opt_payload) return std::nullopt;

    Payload payload = Payload::from_bytes(*opt_payload);
    double r = compute_r(payload.now, secret_);
    double c1 = fixed_to_double(payload.quantized);
    double plaintext = inverse_chain(c1, r, weights_);

    if (std::isnan(plaintext)) return std::nullopt;
    if (plaintext < PLAINTEXT_MIN - 0.1 || plaintext > PLAINTEXT_MAX + 0.1) return std::nullopt;
    return plaintext;
}

// ------------------------------------------------------------
// 缩放辅助
// ------------------------------------------------------------
double TeslaCoilTransform::scale_to_plaintext(int64_t value, int64_t max_value) {
    if (max_value == 0) return 0.0;
    double r = static_cast<double>(value) / static_cast<double>(max_value);
    if (r < -1.0) r = -1.0;
    if (r >  1.0) r =  1.0;
    return r;
}

int64_t TeslaCoilTransform::scale_from_plaintext(double plaintext, int64_t max_value) {
    double scaled = plaintext * static_cast<double>(max_value);
    if (scaled > static_cast<double>(max_value)) scaled = static_cast<double>(max_value);
    if (scaled < -static_cast<double>(max_value)) scaled = -static_cast<double>(max_value);
    return static_cast<int64_t>(std::round(scaled));
}

// ------------------------------------------------------------
// AEAD（XChaCha20-Poly1305）
// ------------------------------------------------------------
std::vector<uint8_t> TeslaCoilTransform::aead_encrypt(
    const std::array<uint8_t, PAYLOAD_SIZE>& plain,
    const Nonce& nonce) const
{
    std::vector<uint8_t> cipher(plain.size() + TAG_SIZE);
    unsigned long long clen;
    crypto_aead_xchacha20poly1305_ietf_encrypt(
        cipher.data(), &clen,
        plain.data(), plain.size(),
        nullptr, 0,
        nullptr,
        nonce.data(),
        key_.data()
    );
    cipher.resize(clen);
    return cipher;
}

std::optional<std::array<uint8_t, PAYLOAD_SIZE>>
TeslaCoilTransform::aead_decrypt(const std::vector<uint8_t>& ciphertext,
                                 const Nonce& nonce) const
{
    std::array<uint8_t, PAYLOAD_SIZE> plain;
    unsigned long long plen;
    int ret = crypto_aead_xchacha20poly1305_ietf_decrypt(
        plain.data(), &plen,
        nullptr,
        ciphertext.data(), ciphertext.size(),
        nullptr, 0,
        nonce.data(),
        key_.data()
    );
    if (ret != 0 || plen != PAYLOAD_SIZE) return std::nullopt;
    return plain;
}

} // namespace tct