// core/p2p/nat_traversal.cpp
// NAT traversal coordinator with multi-strategy support.
// NAT 穿透协调器，支持多策略。

#include "nat_traversal.hpp"
#include "udp_hole_punch.hpp"
#include "port_prediction.hpp"

#ifdef HAVE_LIBJUICE
#include "libjuice_wrapper.hpp"
#endif

#include <unistd.h>
#include <cstring>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <map>
#include <iostream>
#include <sstream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#endif

namespace numotirus {
namespace nat {

// ============================================================
// Candidate helper. 候选地址辅助。
// ============================================================

static std::string CandidateKey(const Candidate& c) {
    return c.ip + ":" + std::to_string(c.port);
}

// ============================================================
// NatTraversal::Impl. 实现类。
// ============================================================

struct NatTraversal::Impl {
    int sock_ = -1;
    uint16_t local_port_ = 0;
    std::string stun_server_;
    uint16_t stun_port_ = kStunPort;
    StunClient stun_client_;
    PortPredictor port_predictor_;
    std::unique_ptr<MultiHolePuncher> puncher_;

    std::vector<Candidate> local_candidates_;
    std::vector<Candidate> peer_candidates_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelled_{false};
    TraversalCallback callback_;
    std::mutex mutex_;
    std::map<std::string, bool> attempted_;

    // Learning phase. 学习阶段。
    bool learning_phase_ = true;
    int learning_samples_ = 0;
    std::vector<uint16_t> learning_ports_;

    Impl() : puncher_(std::make_unique<MultiHolePuncher>()) {}

    bool CreateSocket();
    void CloseSocket();
    void GatherLocalCandidates();
    void DiscoverPublicAddress();
    void RunTraversal();
    void TryDirect(const Candidate& target);
    void TryHolePunch(const Candidate& target);
    void TryPortPrediction(const Candidate& target);
    void TryIce(const Candidate& target);
    void LearnNATBehavior(const std::string& target_key);

    // Learning: send probes to STUN to observe NAT behavior.
    // 学习：向 STUN 发送探测包以观察 NAT 行为。
    void LearnPortPattern();

    std::string TargetKey(const Candidate& c) const {
        return c.ip + ":" + std::to_string(c.port);
    }
};

bool NatTraversal::Impl::CreateSocket() {
    if (sock_ >= 0) CloseSocket();

    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) return false;

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(local_port_);

    if (bind(sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        CloseSocket();
        return false;
    }

    // Initialize puncher with this socket. 用此套接字初始化打洞器。
    puncher_->Initialize(sock_, stun_server_);

    return true;
}

void NatTraversal::Impl::CloseSocket() {
    if (sock_ >= 0) {
#ifdef _WIN32
        closesocket(sock_);
#else
        close(sock_);
#endif
        sock_ = -1;
    }
    puncher_->Cancel();
}

void NatTraversal::Impl::GatherLocalCandidates() {
    local_candidates_.clear();

    Candidate host;
    host.type = CandidateType::kHost;
    host.ip = "127.0.0.1";
    host.port = local_port_;
    host.foundation = "host";
    host.priority = 126;
    local_candidates_.push_back(host);

    // Also add LAN IP if we can find it. 如果可能，也添加局域网 IP。
    // This is simplified; in production you'd enumerate interfaces.
    // 简化版本；在生产环境中应枚举所有接口。
}

void NatTraversal::Impl::DiscoverPublicAddress() {
    auto result = stun_client_.QueryPublicAddress(stun_server_, stun_port_);
    if (result.has_value()) {
        Candidate pub;
        pub.type = CandidateType::kPublic;
        pub.ip = result->ip;
        pub.port = result->port;
        pub.foundation = "stun";
        pub.priority = 200;
        local_candidates_.push_back(pub);

        std::cout << "[NAT] Public address: " << pub.ip << ":" << pub.port << "\n";
    }
}

void NatTraversal::Impl::LearnPortPattern() {
    // Send 5 probes to STUN to observe NAT port allocation behavior.
    // 向 STUN 发送 5 次探测，观察 NAT 端口分配行为。
    std::cout << "[NAT] Learning NAT behavior...\n";

    learning_ports_.clear();
    for (int i = 0; i < 5; ++i) {
        auto result = stun_client_.QueryPublicAddress(stun_server_, stun_port_, 1000);
        if (result.has_value()) {
            learning_ports_.push_back(result->port);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    if (learning_ports_.size() >= 2) {
        // Record to predictor for future use. 记录到预测器中供以后使用。
        for (uint16_t port : learning_ports_) {
            port_predictor_.RecordMapping("stun_learn", port);
        }
        std::cout << "[NAT] Learned " << learning_ports_.size() << " port samples.\n";
        std::cout << "[NAT] Pattern: ";
        switch (port_predictor_.GetPattern("stun_learn")) {
            case NatPattern::kLinearUp:
                std::cout << "Linear Up\n";
                break;
            case NatPattern::kLinearDown:
                std::cout << "Linear Down\n";
                break;
            case NatPattern::kRandom:
                std::cout << "Random (unpredictable)\n";
                break;
            default:
                std::cout << "Unknown (insufficient data)\n";
                break;
        }
    }

    learning_phase_ = false;
}

void NatTraversal::Impl::TryDirect(const Candidate& target) {
    std::cout << "[NAT] Trying direct UDP to " << target.ip << ":" << target.port << "\n";

    if (sock_ < 0) return;

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target.port);
    if (inet_pton(AF_INET, target.ip.c_str(), &addr.sin_addr) != 1) {
        return;
    }

    const char* test_msg = "PING";
    if (sendto(sock_, test_msg, strlen(test_msg), 0,
               reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) > 0) {

        // Wait for response. 等待响应。
#ifdef _WIN32
        DWORD timeout = 500;
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
        struct timeval tv = {0, 500000};
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

        uint8_t buffer[64];
        struct sockaddr_in from_addr;
        socklen_t from_len = sizeof(from_addr);
        int n = recvfrom(sock_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                         reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);

        if (n > 0 && !cancelled_.load()) {
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
            std::cout << "[NAT] ✅ Direct UDP succeeded!\n";
            Candidate peer;
            peer.ip = ip_str;
            peer.port = ntohs(from_addr.sin_port);
            peer.type = CandidateType::kPublic;
            if (callback_) {
                callback_(true, peer);
                running_.store(false);
            }
        }
    }
}

void NatTraversal::Impl::TryHolePunch(const Candidate& target) {
    std::cout << "[NAT] UDP hole punch to " << target.ip << ":" << target.port << "\n";

    // Use multi-port puncher with a focused port list.
    // 使用多端口打洞器，使用集中的端口列表。
    std::vector<uint16_t> ports;
    ports.push_back(target.port);

    // Add ports from prediction. 添加预测端口。
    auto target_key = TargetKey(target);
    auto predicted = port_predictor_.PredictPorts(target_key, 10);
    for (uint16_t p : predicted) {
        if (std::find(ports.begin(), ports.end(), p) == ports.end()) {
            ports.push_back(p);
        }
    }

    // Also add a simple range around the target. 也在目标端口周围添加一个范围。
    auto range_ports = GeneratePortRange(target.port, 5);
    for (uint16_t p : range_ports) {
        if (std::find(ports.begin(), ports.end(), p) == ports.end() && p != target.port) {
            ports.push_back(p);
        }
    }

    // Sort by closeness to target port. 按与目标端口的接近程度排序。
    std::sort(ports.begin(), ports.end(),
        [target](uint16_t a, uint16_t b) {
            return std::abs(static_cast<int>(a) - static_cast<int>(target.port)) <
                   std::abs(static_cast<int>(b) - static_cast<int>(target.port));
        });

    // Limit to reasonable number. 限制为合理数量。
    if (ports.size() > 30) {
        ports.resize(30);
    }

    std::cout << "[NAT] Probing " << ports.size() << " ports...\n";

    puncher_->Punch(target.ip, ports,
        [this](const PunchResult& result) {
            if (result.success && !cancelled_.load()) {
                std::cout << "[NAT] ✅ UDP hole punch succeeded on port " << result.hit_port << "!\n";
                Candidate peer;
                peer.ip = result.peer_ip;
                peer.port = result.peer_port;
                peer.type = CandidateType::kPublic;
                if (callback_) {
                    callback_(true, peer);
                    running_.store(false);
                }
            }
        },
        4000  // 4 second timeout. 4 秒超时。
    );
}

void NatTraversal::Impl::TryPortPrediction(const Candidate& target) {
    auto target_key = TargetKey(target);
    auto predicted = port_predictor_.PredictPorts(target_key, 15);

    if (predicted.empty()) {
        // Fallback: use simple prediction. 回退：使用简单预测。
        predicted = QuickPredictPorts(target.port, 10);
    }

    std::cout << "[NAT] Port prediction to " << target.ip << ":" << target.port
              << " — trying " << predicted.size() << " ports...\n";

    // Use puncher with predicted ports. 使用预测端口进行打洞。
    puncher_->Punch(target.ip, predicted,
        [this](const PunchResult& result) {
            if (result.success && !cancelled_.load()) {
                std::cout << "[NAT] ✅ Port prediction succeeded on port " << result.hit_port << "!\n";
                Candidate peer;
                peer.ip = result.peer_ip;
                peer.port = result.peer_port;
                peer.type = CandidateType::kPublic;
                if (callback_) {
                    callback_(true, peer);
                    running_.store(false);
                }
            }
        },
        3000
    );
}

void NatTraversal::Impl::TryIce(const Candidate& target) {
#ifdef HAVE_LIBJUICE
    std::cout << "[NAT] ICE to " << target.ip << ":" << target.port << "\n";
    // ICE implementation would go here.
    // ICE 实现会放在这里。
    std::cout << "[ICE] ICE requires TURN server, not available.\n";
#else
    std::cout << "[NAT] ICE unavailable (libjuice not compiled)\n";
#endif
}

void NatTraversal::Impl::RunTraversal() {
    if (!callback_) return;

    // Learning phase: observe NAT behavior first.
    // 学习阶段：先观察 NAT 行为。
    if (learning_phase_) {
        LearnPortPattern();
    }

    // Sort peer candidates by priority. 按优先级排序对方候选地址。
    auto targets = peer_candidates_;
    std::sort(targets.begin(), targets.end(),
        [](const Candidate& a, const Candidate& b) {
            return a.priority > b.priority;
        });

    for (const auto& target : targets) {
        if (cancelled_.load()) break;

        std::string key = CandidateKey(target);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (attempted_[key]) continue;
            attempted_[key] = true;
        }

        // Record this target for port prediction. 记录此目标用于端口预测。
        port_predictor_.RecordMapping(TargetKey(target), target.port);

        // Try strategies in parallel. 并行尝试策略。
        // We launch them and let whichever succeeds first win.
        // 我们启动它们，哪个先成功就使用哪个。
        std::atomic<bool> done{false};

        // Launch direct attempt. 启动直接尝试。
        std::thread direct_thread([this, &target, &done]() {
            if (done.load()) return;
            TryDirect(target);
            if (!running_.load() || cancelled_.load()) {
                done.store(true);
            }
        });

        // Launch hole punch. 启动打洞。
        std::thread punch_thread([this, &target, &done]() {
            if (done.load()) return;
            TryHolePunch(target);
            if (!running_.load() || cancelled_.load()) {
                done.store(true);
            }
        });

        // Launch port prediction. 启动端口预测。
        std::thread pred_thread([this, &target, &done]() {
            if (done.load()) return;
            TryPortPrediction(target);
            if (!running_.load() || cancelled_.load()) {
                done.store(true);
            }
        });

        // Wait for any to succeed or all to fail. 等待任何一个成功或全部失败。
        int max_wait = 5000;  // 5 seconds total. 总共 5 秒。
        int elapsed = 0;
        while (running_.load() && !cancelled_.load() && elapsed < max_wait) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            elapsed += 100;
        }

        done.store(true);

        if (direct_thread.joinable()) direct_thread.join();
        if (punch_thread.joinable()) punch_thread.join();
        if (pred_thread.joinable()) pred_thread.join();

        if (!running_.load() || cancelled_.load()) break;
    }

    // If we get here, all strategies failed. 如果执行到这里，所有策略都失败了。
    if (!cancelled_.load() && callback_) {
        std::cout << "[NAT] ❌ All strategies failed, fallback needed.\n";
        Candidate fallback;
        fallback.type = CandidateType::kRelay;
        callback_(false, fallback);
    }
    running_.store(false);
}

// ============================================================
// Public interface. 公开接口。
// ============================================================

NatTraversal::NatTraversal() : impl_(std::make_unique<Impl>()) {}

NatTraversal::~NatTraversal() {
    CancelTraversal();
}

bool NatTraversal::Initialize(const std::string& stun_server, uint16_t stun_port) {
    impl_->stun_server_ = stun_server;
    impl_->stun_port_ = stun_port;
    return true;
}

void NatTraversal::SetLocalPort(uint16_t port) {
    impl_->local_port_ = port;
}

std::vector<Candidate> NatTraversal::GetLocalCandidates() const {
    return impl_->local_candidates_;
}

void NatTraversal::StartTraversal(const std::vector<Candidate>& peer_candidates,
                                   TraversalCallback callback) {
    if (impl_->running_.load()) {
        CancelTraversal();
    }

    impl_->peer_candidates_ = peer_candidates;
    impl_->callback_ = callback;
    impl_->cancelled_.store(false);
    impl_->attempted_.clear();

    if (!impl_->CreateSocket()) {
        if (impl_->callback_) {
            Candidate fallback;
            fallback.type = CandidateType::kRelay;
            impl_->callback_(false, fallback);
        }
        return;
    }

    impl_->GatherLocalCandidates();
    impl_->DiscoverPublicAddress();

    impl_->running_.store(true);
    std::thread(&Impl::RunTraversal, impl_.get()).detach();
}

void NatTraversal::CancelTraversal() {
    if (impl_->running_.load()) {
        impl_->cancelled_.store(true);
        impl_->puncher_->Cancel();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        impl_->running_.store(false);
    }
    impl_->CloseSocket();
}

void NatTraversal::AddPeerCandidate(const Candidate& candidate) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::string key = CandidateKey(candidate);
    for (const auto& c : impl_->peer_candidates_) {
        if (CandidateKey(c) == key) return;
    }
    impl_->peer_candidates_.push_back(candidate);

    // Record for port prediction. 记录用于端口预测。
    impl_->port_predictor_.RecordMapping(impl_->TargetKey(candidate), candidate.port);
}

} // namespace nat
} // namespace numotirus