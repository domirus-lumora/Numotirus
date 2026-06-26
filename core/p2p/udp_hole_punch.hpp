// core/p2p/udp_hole_punch.hpp
// UDP hole punching for NAT traversal. UDP 打洞，用于 NAT 穿透。

#pragma once

#include <string>
#include <functional>
#include <cstdint>

namespace numotirus {
namespace nat {

// Callback when hole punching result is ready.
// 打洞结果回调函数。
using PunchCallback = std::function<void(bool success, const std::string& peer_ip, uint16_t peer_port)>;

// Start UDP hole punching. 启动 UDP 打洞。
// local_port: 本地 UDP 端口 / Local UDP port
// stun_server: STUN 服务器地址 (可为空，用于获取公网地址) / STUN server address (can be empty)
// peer_ip: 对方的公网 IP / Peer's public IP
// peer_port: 对方的公网端口 / Peer's public port
// callback: 结果回调 / Result callback
void StartUdpHolePunch(uint16_t local_port,
                       const std::string& stun_server,
                       const std::string& peer_ip,
                       uint16_t peer_port,
                       PunchCallback callback);

// Get public address via STUN server. 通过 STUN 服务器获取公网地址。
// Returns "ip:port" on success, empty string on failure.
// 成功返回 "ip:port"，失败返回空字符串。
std::string GetPublicAddressViaStun(const std::string& stun_server = "stun.l.google.com:19302");

} // namespace nat
} // namespace numotirus