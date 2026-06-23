// core/p2p/nat_traversal.hpp
// NAT traversal coordinator. NAT 穿透协调器。

#pragma once

#include "nat_stun.hpp"
#include <string>
#include <vector>
#include <functional>
#include <cstdint>
#include <memory>

namespace numotirus {
namespace nat {

// Candidate type. 候选地址类型。
enum class CandidateType : uint8_t {
    kHost,      // 本地地址 / Local IP
    kPublic,    // 公网地址（STUN获取）/ Public IP (via STUN)
    kRelay,     // 中继地址 / Relay address
};

// ICE candidate. ICE 候选地址。
struct Candidate {
    CandidateType type;
    std::string ip;
    uint16_t port;
    std::string foundation;  // 用于去重 / For deduplication
    int priority;            // 优先级 / Priority

    bool operator==(const Candidate& other) const;
};

// Callback when traversal completes. 穿透完成时的回调。
// success: true if direct connection established. 是否成功建立直连。
// public_ip: 对方的公网地址（如果成功）/ Peer's public address (if success).
using TraversalCallback = std::function<void(bool success, const Candidate& peer_candidate)>;

// NAT traversal coordinator. NAT 穿透协调器。
class NatTraversal {
public:
    NatTraversal();
    ~NatTraversal();

    // Initialize with STUN server address. 用 STUN 服务器地址初始化。
    bool Initialize(const std::string& stun_server, uint16_t stun_port = kStunPort);

    // Set local listening port. 设置本地监听端口。
    void SetLocalPort(uint16_t port);

    // Get local candidates (host + public). 获取本地候选地址（本地 + 公网）。
    std::vector<Candidate> GetLocalCandidates() const;

    // Start traversal to peer. 开始向对方穿透。
    // peer_candidates: 对方的候选地址列表。
    // callback: 穿透完成回调。
    void StartTraversal(const std::vector<Candidate>& peer_candidates,
                        TraversalCallback callback);

    // Cancel ongoing traversal. 取消正在进行的穿透。
    void CancelTraversal();

    // Add a candidate discovered via DHT / signaling. 添加通过 DHT/信令发现的候选地址。
    void AddPeerCandidate(const Candidate& candidate);

    // Send punch packet to target. 向目标发送打洞包。
    bool SendPunchPacket(const Candidate& target);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace nat
} // namespace numotirus