// core/protocol/noise.cpp
// Noise protocol implementation for Numotirus. Numotirus 的 Noise 协议实现。
// SPDX-License-Identifier: Apache-2.0

#include "noise.hpp"

#include <sodium.h>
#include <cstring>
#include <fstream>
#include <chrono>
#include <thread>
#include <memory>
#include <functional>

namespace numotirus {
namespace protocol {
namespace noise {

namespace {

// Initialize libsodium. 初始化 libsodium。
bool EnsureSodium() {
    static bool init = [] { return sodium_init() >= 0; }();
    return init;
}

// Generate SAS string (4 groups of 4 digits) from a 32-byte hash.
// 从 32 字节哈希生成 SAS 字符串（4 组 4 位数字）。
std::string GenerateSasFromHash(const uint8_t hash[32]) {
    uint16_t parts[4];
    for (int i = 0; i < 4; ++i) {
        parts[i] = (hash[i * 2] << 8) | hash[i * 2 + 1];
    }
    char buf[20];
    snprintf(buf, sizeof(buf), "%04u %04u %04u %04u",
             parts[0] % 10000, parts[1] % 10000,
             parts[2] % 10000, parts[3] % 10000);
    return std::string(buf);
}

} // unnamed namespace

static const char* kTrustFile = "trusted_peers.bin";

// Save trust entry to disk. 保存信任条目到磁盘。
ErrorCode TrustSave(const char* peer_id,
                    const std::array<uint8_t, kPublicKeySize>& public_key,
                    const std::array<uint8_t, kSharedKeySize>& shared_secret) {
    if (!peer_id) return ErrorCode::kInvalidArgument;
    std::ofstream file(kTrustFile, std::ios::binary | std::ios::app);
    if (!file) return ErrorCode::kTrustIoError;
    char id[kMaxIdLength] = {0};
    strncpy(id, peer_id, kMaxIdLength - 1);
    file.write(id, kMaxIdLength);
    file.write(reinterpret_cast<const char*>(public_key.data()), kPublicKeySize);
    file.write(reinterpret_cast<const char*>(shared_secret.data()), kSharedKeySize);
    return ErrorCode::kSuccess;
}

// Load trust entry from disk. 从磁盘加载信任条目。
ErrorCode TrustLoad(const char* peer_id,
                    std::array<uint8_t, kPublicKeySize>& public_key,
                    std::array<uint8_t, kSharedKeySize>& shared_secret) {
    if (!peer_id) return ErrorCode::kInvalidArgument;
    std::ifstream file(kTrustFile, std::ios::binary);
    if (!file) return ErrorCode::kTrustIoError;
    char id[kMaxIdLength];
    while (file.read(id, kMaxIdLength)) {
        if (strncmp(id, peer_id, kMaxIdLength) == 0) {
            file.read(reinterpret_cast<char*>(public_key.data()), kPublicKeySize);
            file.read(reinterpret_cast<char*>(shared_secret.data()), kSharedKeySize);
            if (file) return ErrorCode::kSuccess;
            break;
        }
        file.seekg(kPublicKeySize + kSharedKeySize, std::ios::cur);
    }
    return ErrorCode::kTrustIoError;
}

// Delete a trust entry. 删除信任条目。
ErrorCode TrustDelete(const char* peer_id) {
    if (!peer_id) return ErrorCode::kInvalidArgument;
    std::ifstream in(kTrustFile, std::ios::binary);
    if (!in) return ErrorCode::kTrustIoError;
    std::ofstream out("trusted_peers.tmp", std::ios::binary);
    if (!out) return ErrorCode::kTrustIoError;
    char id[kMaxIdLength];
    while (in.read(id, kMaxIdLength)) {
        if (strncmp(id, peer_id, kMaxIdLength) == 0) {
            in.seekg(kPublicKeySize + kSharedKeySize, std::ios::cur);
            continue;
        }
        out.write(id, kMaxIdLength);
        std::array<uint8_t, kPublicKeySize> pub;
        std::array<uint8_t, kSharedKeySize> sec;
        in.read(reinterpret_cast<char*>(pub.data()), kPublicKeySize);
        in.read(reinterpret_cast<char*>(sec.data()), kSharedKeySize);
        out.write(reinterpret_cast<const char*>(pub.data()), kPublicKeySize);
        out.write(reinterpret_cast<const char*>(sec.data()), kSharedKeySize);
    }
    in.close();
    out.close();
    std::remove(kTrustFile);
    std::rename("trusted_peers.tmp", kTrustFile);
    return ErrorCode::kSuccess;
}

// Clear all trust entries. 清除所有信任条目。
ErrorCode TrustClearAll() {
    if (std::remove(kTrustFile) != 0) return ErrorCode::kTrustIoError;
    return ErrorCode::kSuccess;
}

// Generate a random X25519 key pair. 生成随机 X25519 密钥对。
std::optional<KeyPair> GenerateKeyPair() {
    if (!EnsureSodium()) return std::nullopt;
    KeyPair kp;
    if (crypto_box_keypair(kp.public_key.data(), kp.secret_key.data()) != 0)
        return std::nullopt;
    return kp;
}

// Format SAS from a 16-byte hash. 从 16 字节哈希格式化 SAS。
std::string FormatSas(const std::array<uint8_t, 16>& hash) {
    uint16_t parts[4];
    for (int i = 0; i < 4; ++i) {
        parts[i] = (hash[i * 2] << 8) | hash[i * 2 + 1];
    }
    char buf[20];
    snprintf(buf, sizeof(buf), "%04u %04u %04u %04u",
             parts[0] % 10000, parts[1] % 10000,
             parts[2] % 10000, parts[3] % 10000);
    return std::string(buf);
}

// PIMPL implementation. PIMPL 实现。
struct NoiseSession::Impl {
    bool handshake_complete_ = false;
    bool verified_ = false;
    ErrorCode last_error_ = ErrorCode::kSuccess;
    std::array<uint8_t, kSessionKeySize> rx_key_{};
    std::array<uint8_t, kSessionKeySize> tx_key_{};
    std::string sas_;
    bool has_peer_public_ = false;
    std::array<uint8_t, kPublicKeySize> peer_public_{};
    std::array<uint8_t, kSecretKeySize> local_secret_{};
    std::array<uint8_t, kPublicKeySize> local_public_{};
    std::unique_ptr<::noise::HandshakeState> handshake_state_;
};

// Constructor. 构造函数。
NoiseSession::NoiseSession() : impl_(std::make_unique<Impl>()) {
    EnsureSodium();
}

// Destructor. 析构函数。
NoiseSession::~NoiseSession() = default;

// Set local static key pair. 设置本地静态密钥对。
void NoiseSession::SetKeyPair(const KeyPair& keypair) {
    impl_->local_public_ = keypair.public_key;
    impl_->local_secret_ = keypair.secret_key;
}

// Set peer's static public key. 设置对方静态公钥。
void NoiseSession::SetPeerPublic(const std::array<uint8_t, kPublicKeySize>& public_key) {
    impl_->peer_public_ = public_key;
    impl_->has_peer_public_ = true;
}

// Perform Noise XX handshake with 15-second timeout.
// 执行 Noise XX 握手，超时 15 秒。
ErrorCode NoiseSession::Handshake(
    bool initiator,
    std::function<ErrorCode(const uint8_t* data, size_t len)> write,
    std::function<ErrorCode(uint8_t* buffer, size_t len)> read) {
    if (!write || !read) {
        impl_->last_error_ = ErrorCode::kInvalidArgument;
        return ErrorCode::kInvalidArgument;
    }

    // Create noise-cpp handshake state (global namespace ::noise).
    // 创建 noise-cpp 握手状态（全局命名空间 ::noise）。
    auto hs = std::make_unique<::noise::HandshakeState>();
    if (!hs) {
        impl_->last_error_ = ErrorCode::kInitFailed;
        return ErrorCode::kInitFailed;
    }

    // Configure handshake parameters. 配置握手参数。
    ::noise::HandshakeStateConfiguration config;
    config.pattern = ::noise::HandshakePattern::XX;
    config.initiator = initiator;
    config.prologue = {};

    // Convert KeyPair to noise-cpp KeyPair. 转换 KeyPair 到 noise-cpp KeyPair。
    ::noise::KeyPair local_kp;
    std::memcpy(local_kp.sk.data(), impl_->local_secret_.data(), 32);
    std::memcpy(local_kp.pk.data(), impl_->local_public_.data(), 32);
    config.s = local_kp;
    config.e = std::nullopt;

    // Set remote public key. 设置对方公钥。
    std::array<uint8_t, 32> remote_pk;
    std::memcpy(remote_pk.data(), impl_->peer_public_.data(), 32);
    config.rs = remote_pk;
    config.re = std::nullopt;
    config.psks = {};

    hs->initialize(config);
    impl_->handshake_state_ = std::move(hs);

    // Handshake loop with 15-second timeout. 15 秒超时的握手循环。
    const int kTimeoutSeconds = 15;
    auto start_time = std::chrono::steady_clock::now();
    std::vector<uint8_t> msg_buf;
    std::vector<uint8_t> payload_buf;
    uint8_t recv_buffer[2048];

    while (!impl_->handshake_state_->is_handshake_finished()) {
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - start_time).count();
        if (elapsed > kTimeoutSeconds) {
            impl_->last_error_ = ErrorCode::kHandshakeFailed;
            return ErrorCode::kHandshakeFailed;
        }

        if (impl_->handshake_state_->is_my_turn()) {
            // Write message. 发送消息。
            msg_buf.clear();
            impl_->handshake_state_->write_message(msg_buf);
            ErrorCode err = write(msg_buf.data(), msg_buf.size());
            if (err != ErrorCode::kSuccess) {
                impl_->last_error_ = err;
                return err;
            }
        } else {
            // Read message. 接收消息。
            ErrorCode err = read(recv_buffer, sizeof(recv_buffer));
            if (err != ErrorCode::kSuccess) {
                impl_->last_error_ = err;
                return err;
            }
            msg_buf.assign(recv_buffer, recv_buffer + sizeof(recv_buffer));
            impl_->handshake_state_->read_message(msg_buf, payload_buf);
        }
    }

    // Generate SAS from handshake hash. 从握手哈希生成 SAS。
    auto hash = impl_->handshake_state_->get_handshake_hash();
    impl_->sas_ = GenerateSasFromHash(hash.data());

    // Derive session keys using libsodium. 使用 libsodium 派生会话密钥。
    uint8_t shared_secret[32];
    if (crypto_scalarmult(shared_secret, impl_->local_secret_.data(),
                          impl_->peer_public_.data()) != 0) {
        impl_->last_error_ = ErrorCode::kHandshakeFailed;
        return ErrorCode::kHandshakeFailed;
    }

    const char* salt_rx = "Numotirus Noise RX";
    const char* salt_tx = "Numotirus Noise TX";
    crypto_generichash(impl_->rx_key_.data(), kSessionKeySize,
                       shared_secret, 32,
                       reinterpret_cast<const uint8_t*>(salt_rx), strlen(salt_rx));
    crypto_generichash(impl_->tx_key_.data(), kSessionKeySize,
                       shared_secret, 32,
                       reinterpret_cast<const uint8_t*>(salt_tx), strlen(salt_tx));

    sodium_memzero(shared_secret, 32);
    sodium_memzero(recv_buffer, sizeof(recv_buffer));

    impl_->handshake_complete_ = true;
    impl_->last_error_ = ErrorCode::kSuccess;
    return ErrorCode::kSuccess;
}

// Get SAS string (empty if handshake not complete). 获取 SAS 字符串（未完成时返回空）。
std::string NoiseSession::GetSas() const {
    return impl_->handshake_complete_ ? impl_->sas_ : "";
}

// Mark peer as verified. 标记对方已验证。
void NoiseSession::MarkVerified() {
    impl_->verified_ = true;
}

// Check if peer is verified. 检查对方是否已验证。
bool NoiseSession::IsVerified() const {
    return impl_->verified_;
}

// Get receive key. 获取接收密钥。
const std::array<uint8_t, kSessionKeySize>& NoiseSession::GetRxKey() const {
    return impl_->rx_key_;
}

// Get transmit key. 获取发送密钥。
const std::array<uint8_t, kSessionKeySize>& NoiseSession::GetTxKey() const {
    return impl_->tx_key_;
}

// Check if handshake is complete. 检查握手是否完成。
bool NoiseSession::IsHandshakeComplete() const {
    return impl_->handshake_complete_;
}

// Get last error. 获取最后一次错误。
ErrorCode NoiseSession::GetLastError() const {
    return impl_->last_error_;
}

} // namespace noise
} // namespace protocol
} // namespace numotirus