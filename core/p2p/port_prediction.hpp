// core/p2p/port_prediction.hpp
// Port prediction for symmetric NAT traversal. 端口预测，用于对称型 NAT 穿透。

#pragma once

#include <vector>
#include <cstdint>
#include <string>

namespace numotirus {
namespace nat {

// Predict the next external port of a symmetric NAT.
// 预测对称型 NAT 的下一个外网端口。
// local_port: 本地端口 / Local port
// stun_server: STUN 服务器地址 / STUN server address
// Returns a list of predicted ports (sorted by probability).
// 返回预测的端口列表（按概率排序）。
std::vector<uint16_t> PredictSymmetricNatPorts(uint16_t local_port,
                                               const std::string& stun_server = "stun.l.google.com:19302");

// Birthday attack for symmetric NAT.
// 针对对称型 NAT 的生日攻击。
// local_port: 本地端口 / Local port
// peer_ip: 对方的公网 IP / Peer's public IP
// peer_port: 对方的公网端口 / Peer's public port
// predicted_ports: 预测的端口列表 / Predicted ports list
// Returns true if a connection is established.
// 成功建立连接返回 true。
bool BirthdayAttackOnSymmetricNat(uint16_t local_port,
                                  const std::string& peer_ip,
                                  uint16_t peer_port,
                                  const std::vector<uint16_t>& predicted_ports);

} // namespace nat
} // namespace numotirus