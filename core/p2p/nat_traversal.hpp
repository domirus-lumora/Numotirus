// core/p2p/nat_traversal.hpp
// NAT traversal coordinator with multi-strategy support.
// NAT 穿透协调器，支持多策略。

#pragma once

#include "nat_stun.hpp"
#include "port_prediction.hpp"
#include <string>
#include <vector>
#include <functional>
#include <memory>
#include <cstdint>
#include <atomic>

// ============================================================
// 跨平台网络头文件 / Cross-platform network headers
// ============================================================

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

#ifndef INET_ADDRSTRLEN
#define INET_ADDRSTRLEN 16
#endif

namespace numotirus {
namespace nat {

// Candidate types. 候选地址类型。
enum class CandidateType : uint8_t {
    kHost,      // Local address. 本地地址。
    kPublic,    // Public address (via STUN). 公网地址（通过 STUN）。
    kRelay,     // Relay address. 中继地址。
};

// ICE candidate. ICE 候选地址。
struct Candidate {
    CandidateType type = CandidateType::kHost;
    std::string ip;
    uint16_t port = 0;
    std::string foundation;  // For deduplication. 用于去重。
    int priority = 0;

    bool operator==(const Candidate& other) const {
        return ip == other.ip && port == other.port;
    }
};

// Traversal result callback. 穿透结果回调。
using TraversalCallback = std::function<void(bool success, const Candidate& peer_candidate)>;

// Strategy used for traversal. 穿透使用的策略。
enum class TraversalStrategy {
    kNone,
    kDirect,        // Direct UDP. 直接 UDP。
    kHolePunch,     // UDP hole punching. UDP 打洞。
    kPortPrediction,// Port prediction. 端口预测。
    kIce,           // ICE (requires TURN). ICE（需要 TURN）。
};

// NAT traversal coordinator.
// NAT 穿透协调器。
class NatTraversal {
public:
    NatTraversal();
    ~NatTraversal();

    // Initialize with STUN server. 用 STUN 服务器初始化。
    bool Initialize(const std::string& stun_server, uint16_t stun_port = kStunPort);

    // Set local listening port. 设置本地监听端口。
    void SetLocalPort(uint16_t port);

    // Get local candidates. 获取本地候选地址。
    std::vector<Candidate> GetLocalCandidates() const;

    // Start traversal to peer. 开始向对方穿透。
    void StartTraversal(const std::vector<Candidate>& peer_candidates,
                        TraversalCallback callback);

    // Cancel current traversal. 取消当前穿透。
    void CancelTraversal();

    // Add a peer candidate discovered via signaling.
    // 添加通过信令发现的对方候选地址。
    void AddPeerCandidate(const Candidate& candidate);

    // Get the last successful strategy. 获取最后一次成功的策略。
    TraversalStrategy GetLastStrategy() const { return last_strategy_; }

    // Check if currently traversing. 检查是否正在穿透。
    bool IsRunning() const { return running_.load(); }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    TraversalStrategy last_strategy_ = TraversalStrategy::kNone;
};

} // namespace nat
} // namespace numotirus