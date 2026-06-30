// core/p2p/udp_hole_punch.hpp
// Multi-port UDP hole punching for NAT traversal.
// 多端口 UDP 打洞，用于 NAT 穿透。

#pragma once

#include <string>
#include <functional>
#include <vector>
#include <cstdint>
#include <atomic>
#include <memory>

namespace numotirus {
namespace nat {

// Result of a hole punching attempt. 打洞尝试的结果。
struct PunchResult {
    bool success = false;
    std::string peer_ip;
    uint16_t peer_port = 0;
    uint16_t local_port = 0;       // The port that worked. 成功的本地端口。
    uint16_t hit_port = 0;         // The port that was hit. 命中的端口。
    uint64_t latency_ms = 0;
};

// Callback for punch results. 打洞结果回调。
using PunchCallback = std::function<void(const PunchResult& result)>;

// Multi-port UDP hole puncher.
// 多端口 UDP 打洞器。
class MultiHolePuncher {
public:
    MultiHolePuncher();
    ~MultiHolePuncher();

    // Initialize with socket and STUN server.
    // 用套接字和 STUN 服务器初始化。
    bool Initialize(int sock, const std::string& stun_server = "");

    // Start multi-port hole punching.
    // 启动多端口打洞。
    // target_ip: target IP address. 目标 IP 地址。
    // target_ports: list of ports to try (sorted by priority). 要尝试的端口列表（按优先级排序）。
    // callback: called when a port succeeds or all fail. 端口成功或全部失败时调用。
    // timeout_ms: total timeout. 总超时时间。
    void Punch(const std::string& target_ip,
               const std::vector<uint16_t>& target_ports,
               PunchCallback callback,
               int timeout_ms = 5000);

    // Cancel current punching attempt. 取消当前打洞尝试。
    void Cancel();

    // Check if currently punching. 检查是否正在打洞。
    bool IsRunning() const { return running_.load(); }

    // Get the port that was used for the last successful punch.
    // 获取上一次成功打洞使用的端口。
    uint16_t GetLastHitPort() const { return last_hit_port_; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
    std::atomic<bool> running_{false};
    uint16_t last_hit_port_ = 0;
};

// ============================================================
// Legacy compatibility functions. 旧版兼容函数。
// ============================================================

// Start single-port UDP hole punching (legacy).
// 启动单端口 UDP 打洞（旧版）。
void StartUdpHolePunch(uint16_t local_port,
                       const std::string& stun_server,
                       const std::string& peer_ip,
                       uint16_t peer_port,
                       PunchCallback callback);

// Get public address via STUN. 通过 STUN 获取公网地址。
// Returns "ip:port" on success, empty on failure.
// 成功返回 "ip:port"，失败返回空字符串。
std::string GetPublicAddressViaStun(const std::string& stun_server = "stun.l.google.com:19302");

} // namespace nat
} // namespace numotirus