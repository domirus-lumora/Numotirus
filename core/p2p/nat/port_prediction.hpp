// core/p2p/port_prediction.hpp
// Dynamic port prediction for symmetric NAT traversal.
// 对称型 NAT 穿透的动态端口预测。
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <vector>
#include <cstdint>
#include <string>
#include <map>

namespace numotirus {
namespace nat {

constexpr int kMaxHistorySize = 12;      // Maximum port history entries. 最大历史记录数。
constexpr int kMaxPredictions = 30;      // Maximum predictions per call. 单次预测最大返回数。

// Port assignment pattern.
// 端口分配模式。
enum class NatPattern {
    kUnknown,       // Not enough data. 数据不足。
    kLinearUp,      // Increasing by fixed delta. 固定差值递增。
    kLinearDown,    // Decreasing by fixed delta. 固定差值递减。
    kRandom,        // No predictable pattern. 无规律可预测。
};

// Port history for a single target.
// 单个目标的端口历史。
struct PortHistory {
    std::vector<uint16_t> ports;          // Observed ports. 观察到的端口。
    std::vector<uint64_t> times;          // Timestamps. 时间戳。
    NatPattern pattern = NatPattern::kUnknown;
    int16_t delta = 0;                    // Detected increment. 检测到的差值。
    double ewma = 0.0;                    // Exponential weighted moving average of deltas.
    double variance = 0.0;                // Variance of deltas.
    uint64_t last_update = 0;
};

// Dynamic port predictor using EWMA and variance analysis.
// 使用指数加权移动平均和方差分析的动态端口预测器。
class PortPredictor {
public:
    PortPredictor() = default;

    // Record a port mapping for a target.
    // 记录目标端口映射。
    void RecordMapping(const std::string& target_key, uint16_t public_port);

    // Predict the next port(s) for a target.
    // 预测目标的下一个端口。
    // Returns: sorted list of predicted ports (most likely first).
    // 返回：按概率排序的预测端口列表（最可能的在前）。
    std::vector<uint16_t> PredictPorts(const std::string& target_key, int count = kMaxPredictions);

    // Get the current pattern for a target.
    // 获取目标的当前模式。
    NatPattern GetPattern(const std::string& target_key) const;

    // Clear history for a target.
    // 清空目标历史。
    void ClearHistory(const std::string& target_key);

    // Get all known target keys.
    // 获取所有已知目标键。
    std::vector<std::string> GetKnownTargets() const;

private:
    std::map<std::string, PortHistory> history_;

    void AnalyzePattern(PortHistory& hist);
    uint16_t PredictNext(const PortHistory& hist) const;
    void EvictOldEntries(PortHistory& hist);
};

// Utility: generate a range of ports around a center.
// 工具：在中心端口周围生成端口范围。
std::vector<uint16_t> GeneratePortRange(uint16_t center, int half_range);

} // namespace nat
} // namespace numotirus