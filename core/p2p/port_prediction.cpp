// core/p2p/port_prediction.cpp
// Dynamic port prediction for symmetric NAT traversal.
// 对称型 NAT 穿透的动态端口预测。

#include "port_prediction.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>

namespace numotirus {
namespace nat {

// ============================================================
// PortPredictor Implementation. PortPredictor 实现。
// ============================================================

void PortPredictor::RecordMapping(const std::string& target_key, uint16_t public_port) {
    auto& hist = history_[target_key];

    // Avoid duplicate consecutive entries. 避免连续重复条目。
    if (!hist.ports.empty() && hist.ports.back() == public_port) {
        return;
    }

    hist.ports.push_back(public_port);
    hist.times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count());

    // Limit history size. 限制历史大小。
    EvictOldEntries(hist);

    // Re-analyze pattern. 重新分析模式。
    AnalyzePattern(hist);
}

void PortPredictor::EvictOldEntries(PortHistory& hist) {
    while (hist.ports.size() > kMaxHistorySize) {
        hist.ports.erase(hist.ports.begin());
        hist.times.erase(hist.times.begin());
    }
}

void PortPredictor::AnalyzePattern(PortHistory& hist) {
    if (hist.ports.size() < 2) {
        hist.pattern = NatPattern::kUnknown;
        hist.delta = 0;
        return;
    }

    // Calculate deltas between consecutive ports.
    // 计算连续端口之间的差值。
    std::vector<int16_t> deltas;
    for (size_t i = 1; i < hist.ports.size(); ++i) {
        int16_t d = static_cast<int16_t>(hist.ports[i]) - static_cast<int16_t>(hist.ports[i - 1]);
        deltas.push_back(d);
    }

    // Check if all deltas are the same (linear pattern).
    // 检查所有差值是否相同（线性模式）。
    bool all_same = true;
    for (size_t i = 1; i < deltas.size(); ++i) {
        if (deltas[i] != deltas[0]) {
            all_same = false;
            break;
        }
    }

    if (all_same && deltas.size() >= 2) {
        hist.delta = deltas[0];
        if (hist.delta > 0) {
            hist.pattern = NatPattern::kLinearUp;
        } else if (hist.delta < 0) {
            hist.pattern = NatPattern::kLinearDown;
        } else {
            hist.pattern = NatPattern::kUnknown;  // Same port? 同一端口？
        }
        return;
    }

    // Check if most deltas are similar (tolerate jitter).
    // 检查是否大部分差值相似（容忍抖动）。
    int16_t avg_delta = 0;
    for (int16_t d : deltas) {
        avg_delta += d;
    }
    avg_delta /= static_cast<int16_t>(deltas.size());

    int matches = 0;
    for (int16_t d : deltas) {
        if (std::abs(d - avg_delta) <= 2) {
            matches++;
        }
    }

    if (matches >= static_cast<int>(deltas.size()) * 0.6 && std::abs(avg_delta) > 0) {
        hist.delta = avg_delta;
        if (avg_delta > 0) {
            hist.pattern = NatPattern::kLinearUp;
        } else {
            hist.pattern = NatPattern::kLinearDown;
        }
        return;
    }

    // No clear pattern. 无明显模式。
    hist.pattern = NatPattern::kRandom;
    hist.delta = 0;
}

uint16_t PortPredictor::PredictNext(const PortHistory& hist) const {
    if (hist.ports.empty()) return 0;
    uint16_t last = hist.ports.back();

    if (hist.pattern == NatPattern::kLinearUp) {
        return static_cast<uint16_t>(last + hist.delta);
    } else if (hist.pattern == NatPattern::kLinearDown) {
        return static_cast<uint16_t>(last + hist.delta);  // delta is negative. delta 为负。
    }

    // Random: fallback to last port + 1. 随机：回退到上一个端口 + 1。
    return static_cast<uint16_t>(last + 1);
}

std::vector<uint16_t> PortPredictor::PredictPorts(const std::string& target_key, int count) {
    std::vector<uint16_t> results;

    auto it = history_.find(target_key);
    if (it == history_.end()) {
        return results;  // No history. 无历史。
    }

    const auto& hist = it->second;
    if (hist.ports.empty()) {
        return results;
    }

    uint16_t base = PredictNext(hist);

    // Generate candidates around the prediction.
    // 在预测值周围生成候选端口。
    int half = count / 2;
    for (int i = -half; i <= half && static_cast<int>(results.size()) < count; ++i) {
        uint16_t candidate = static_cast<uint16_t>(base + i);
        // Avoid port 0. 避免端口 0。
        if (candidate == 0) continue;
        // Avoid duplicates. 避免重复。
        bool dup = false;
        for (uint16_t p : results) {
            if (p == candidate) {
                dup = true;
                break;
            }
        }
        if (!dup) {
            results.push_back(candidate);
        }
    }

    // If we have a linear pattern, also extend further in that direction.
    // 如果是线性模式，也在该方向延伸。
    if (hist.pattern == NatPattern::kLinearUp || hist.pattern == NatPattern::kLinearDown) {
        for (int i = 1; i <= 3 && static_cast<int>(results.size()) < count; ++i) {
            uint16_t extended = static_cast<uint16_t>(base + hist.delta * i);
            if (extended != 0) {
                results.push_back(extended);
            }
        }
    }

    // Sort by closeness to base. 按与 base 的接近程度排序。
    std::sort(results.begin(), results.end(),
        [base](uint16_t a, uint16_t b) {
            return std::abs(static_cast<int>(a) - static_cast<int>(base)) <
                   std::abs(static_cast<int>(b) - static_cast<int>(base));
        });

    return results;
}

NatPattern PortPredictor::GetPattern(const std::string& target_key) const {
    auto it = history_.find(target_key);
    if (it == history_.end()) {
        return NatPattern::kUnknown;
    }
    return it->second.pattern;
}

void PortPredictor::ClearHistory(const std::string& target_key) {
    history_.erase(target_key);
}

std::vector<std::string> PortPredictor::GetKnownTargets() const {
    std::vector<std::string> keys;
    for (const auto& pair : history_) {
        keys.push_back(pair.first);
    }
    return keys;
}

// ============================================================
// Free functions. 自由函数。
// ============================================================

std::vector<uint16_t> QuickPredictPorts(uint16_t current_port, int count) {
    return GeneratePortRange(current_port, count / 2);
}

std::vector<uint16_t> GeneratePortRange(uint16_t center, int half_range) {
    std::vector<uint16_t> ports;
    for (int i = -half_range; i <= half_range; ++i) {
        uint16_t port = static_cast<uint16_t>(center + i);
        if (port >= 1024 && port <= 65535) {
            ports.push_back(port);
        }
    }
    return ports;
}

} // namespace nat
} // namespace numotirus