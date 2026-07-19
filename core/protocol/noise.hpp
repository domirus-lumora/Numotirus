// core/protocol/noise.hpp
// Noise protocol header file for Numotirus. Numotirus 的 Noise 协议头文件。
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>
#include <memory>
#include <tl/expected.hpp>

#include "noise-cpp/noise.h"

namespace numotirus {
namespace protocol {
namespace noise {

// Constants. 常量。
constexpr size_t kPublicKeySize = 32;
constexpr size_t kSecretKeySize = 32;
constexpr size_t kSharedKeySize = 32;
constexpr size_t kSessionKeySize = 32;
constexpr size_t kNonceSize = 12;
constexpr size_t kTagSize = 16;
constexpr size_t kSasLength = 20;
constexpr size_t kMaxIdLength = 64;

// Error codes. 错误码。
enum class ErrorCode {
    kSuccess = 0,
    kInvalidArgument = -1,
    kInitFailed = -2,
    kHandshakeFailed = -3,
    kVerifyFailed = -4,
    kTrustIoError = -5,
    kNotInitialized = -6,
};

// Key pair. 密钥对。
struct KeyPair {
    std::array<uint8_t, kPublicKeySize> public_key;
    std::array<uint8_t, kSecretKeySize> secret_key;
};

// Trust entry. 信任条目。
struct TrustEntry {
    char id[kMaxIdLength];
    std::array<uint8_t, kPublicKeySize> public_key;
    std::array<uint8_t, kSharedKeySize> shared_secret;
};

template <typename T>
using Result = tl::expected<T, ErrorCode>;

// Utility functions. 工具函数。
std::optional<KeyPair> GenerateKeyPair();
std::string FormatSas(const std::array<uint8_t, 16>& hash);

// Trust store. 信任存储。
ErrorCode TrustSave(const char* peer_id,
                    const std::array<uint8_t, kPublicKeySize>& public_key,
                    const std::array<uint8_t, kSharedKeySize>& shared_secret);

ErrorCode TrustLoad(const char* peer_id,
                    std::array<uint8_t, kPublicKeySize>& public_key,
                    std::array<uint8_t, kSharedKeySize>& shared_secret);

ErrorCode TrustDelete(const char* peer_id);
ErrorCode TrustClearAll();

// Noise session. Noise 会话。
class NoiseSession {
public:
    NoiseSession();
    ~NoiseSession();

    NoiseSession(const NoiseSession&) = delete;
    NoiseSession& operator=(const NoiseSession&) = delete;
    NoiseSession(NoiseSession&&) = default;
    NoiseSession& operator=(NoiseSession&&) = default;

    void SetKeyPair(const KeyPair& keypair);
    void SetPeerPublic(const std::array<uint8_t, kPublicKeySize>& public_key);

    ErrorCode Handshake(
        bool initiator,
        std::function<ErrorCode(const uint8_t* data, size_t len)> write,
        std::function<ErrorCode(uint8_t* buffer, size_t len)> read);

    std::string GetSas() const;
    void MarkVerified();
    bool IsVerified() const;

    const std::array<uint8_t, kSessionKeySize>& GetRxKey() const;
    const std::array<uint8_t, kSessionKeySize>& GetTxKey() const;

    bool IsHandshakeComplete() const;
    ErrorCode GetLastError() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace noise
}  // namespace protocol
}  // namespace numotirus