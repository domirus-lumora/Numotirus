// core/p2p/port_prediction.cpp
// Dynamic port prediction implementation using EWMA and variance analysis.
// 使用指数加权移动平均和方差分析的动态端口预测实现。
// SPDX-License-Identifier: Apache-2.0

#include "port_prediction.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iostream>

namespace numotirus {
namespace nat {

void PortPredictor::RecordMapping(const std::string& target_key, uint16_t public_port) {
    auto& hist = history_[target_key];

    // Avoid duplicate consecutive entries.
    // 避免连续重复条目。
    if (!hist.ports.empty() && hist.ports.back() == public_port) {
        return;
    }

    hist.ports.push_back(public_port);
    hist.times.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count());

    EvictOldEntries(hist);
    AnalyzePattern(hist);
}

void PortPredictor::EvictOldEntries(PortHistory& hist) {
    while (hist.ports.size() > kMaxHistorySize) {
        hist.ports.erase(hist.ports.begin());
        hist.times.erase(hist.times.begin());
    }
}

void PortPredictor::AnalyzePattern(PortHistory& hist) {
    if (hist.ports.size() < 3) {
        hist.pattern = NatPattern::kUnknown;
        hist.delta = 0;
        hist.ewma = 0.0;
        hist.variance = 0.0;
        return;
    }

    // Compute deltas between consecutive ports.
    // 计算连续端口之间的差值。
    std::vector<int16_t> deltas;
    deltas.reserve(hist.ports.size() - 1);
    for (size_t i = 1; i < hist.ports.size(); ++i) {
        int16_t d = static_cast<int16_t>(hist.ports[i] - hist.ports[i - 1]);
        deltas.push_back(d);
    }

    // Compute EWMA (alpha = 0.7, strong recency weighting).
    // 计算指数加权移动平均（alpha=0.7，近期权重高）。
    double alpha = 0.7;
    double ewma = static_cast<double>(deltas[0]);
    for (size_t i = 1; i < deltas.size(); ++i) {
        ewma = alpha * deltas[i] + (1.0 - alpha) * ewma;
    }
    hist.ewma = ewma;

    // Compute variance around EWMA.
    // 计算围绕 EWMA 的方差。
    double var = 0.0;
    for (int16_t d : deltas) {
        double diff = static_cast<double>(d) - ewma;
        var += diff * diff;
    }
    var /= static_cast<double>(deltas.size());
    hist.variance = var;

    // Classify pattern: if variance is low and EWMA is non-negligible, treat as linear.
    // 分类模式：若方差低且 EWMA 不为零，则视为线性。
    const double kVarThreshold = 4.0;    // Tolerate small jitter. 容忍小抖动。
    if (var < kVarThreshold && std::abs(ewma) > 0.5) {
        hist.delta = static_cast<int16_t>(std::round(ewma));
        if (hist.delta > 0) {
            hist.pattern = NatPattern::kLinearUp;
        } else if (hist.delta < 0) {
            hist.pattern = NatPattern::kLinearDown;
        } else {
            hist.pattern = NatPattern::kUnknown;
        }
    } else {
        hist.pattern = NatPattern::kRandom;
        hist.delta = 0;
    }
}

uint16_t PortPredictor::PredictNext(const PortHistory& hist) const {
    if (hist.ports.empty()) {
        return 0;
    }
    uint16_t last = hist.ports.back();

    // If linear, use delta.
    // 若为线性，使用差值。
    if (hist.pattern == NatPattern::kLinearUp || hist.pattern == NatPattern::kLinearDown) {
        int32_t next = static_cast<int32_t>(last) + hist.delta;
        if (next >= 1 && next <= 65535) {
            return static_cast<uint16_t>(next);
        }
    }

    // Fallback: use EWMA-based prediction.
    // 回退：基于 EWMA 预测。
    if (hist.ports.size() >= 2) {
        int32_t next = static_cast<int32_t>(last) + static_cast<int32_t>(std::round(hist.ewma));
        if (next >= 1 && next <= 65535) {
            return static_cast<uint16_t>(next);
        }
    }

    // Ultimate fallback: last + 1.
    // 最终回退：上一个端口 + 1。
    return static_cast<uint16_t>(last + 1);
}

std::vector<uint16_t> PortPredictor::PredictPorts(const std::string& target_key, int count) {
    std::vector<uint16_t> results;
    auto it = history_.find(target_key);
    if (it == history_.end() || it->second.ports.empty()) {
        return results;
    }

    const auto& hist = it->second;
    uint16_t base = PredictNext(hist);

    // Generate candidates around the prediction.
    // 在预测值周围生成候选端口。
    int half = count / 2;
    for (int i = -half; i <= half && static_cast<int>(results.size()) < count; ++i) {
        uint16_t candidate = static_cast<uint16_t>(base + i);
        if (candidate == 0) {
            continue;
        }
        // Avoid duplicates.
        // 避免重复。
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

    // If linear, extend further in that direction.
    // 若为线性，在该方向延伸。
    if (hist.pattern == NatPattern::kLinearUp || hist.pattern == NatPattern::kLinearDown) {
        for (int i = 1; i <= 3 && static_cast<int>(results.size()) < count; ++i) {
            uint16_t extended = static_cast<uint16_t>(base + hist.delta * i);
            if (extended != 0) {
                // Check duplicate.
                // 检查重复。
                bool dup = false;
                for (uint16_t p : results) {
                    if (p == extended) {
                        dup = true;
                        break;
                    }
                }
                if (!dup) {
                    results.push_back(extended);
                }
            }
        }
    }

    // Sort by closeness to base.
    // 按与 base 的接近程度排序。
    std::sort(results.begin(), results.end(),
        [base](uint16_t a, uint16_t b) {
            return std::abs(static_cast<int>(a) - static_cast<int>(base)) <
                   std::abs(static_cast<int>(b) - static_cast<int>(base));
        });

    return results;
}

NatPattern PortPredictor::GetPattern(const std::string& target_key) const {
    auto it = history_.find(target_key);
    return (it == history_.end()) ? NatPattern::kUnknown : it->second.pattern;
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

// Free functions.
// 自由函数。

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