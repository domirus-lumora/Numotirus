// core/p2p/nat_traversal.cpp
// NAT traversal coordinator implementation.
// NAT 穿透协调器实现。
// SPDX-License-Identifier: Apache-2.0

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
#include <future>
#include <condition_variable>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

namespace numotirus {
namespace nat {

static std::string CandidateKey(const Candidate& c) {
    return c.ip + ":" + std::to_string(c.port);
}

struct NatTraversal::Impl {
    int sock_ = -1;
    uint16_t local_port_ = 0;
    std::string stun_server_;
    uint16_t stun_port_ = kStunDefaultPort;
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

    TraversalStrategy last_strategy_ = TraversalStrategy::kNone;

    Impl() : puncher_(std::make_unique<MultiHolePuncher>()) {}

    bool CreateSocket();
    void CloseSocket();
    void GatherLocalCandidates();
    void DiscoverPublicAddress();
    void RunTraversal();
    bool TryDirect(const Candidate& target);
    bool TryHolePunch(const Candidate& target);
    bool TryPortPrediction(const Candidate& target);
    bool TryIce(const Candidate& target);
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
    // In production, enumerate all interfaces.
    // 生产环境应枚举所有接口。
}

void NatTraversal::Impl::DiscoverPublicAddress() {
    auto result = stun_client_.QueryPublicAddress(stun_server_, stun_port_);
    if (result) {
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
    // Send a few probes to STUN to warm up the predictor.
    // 发送几个探测包给 STUN 以预热预测器。
    for (int i = 0; i < 3; ++i) {
        auto result = stun_client_.QueryPublicAddress(stun_server_, stun_port_, 1000);
        if (result) {
            port_predictor_.RecordMapping("stun_learn", result->port);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

bool NatTraversal::Impl::TryDirect(const Candidate& target) {
    std::cout << "[NAT] Trying direct UDP to " << target.ip << ":" << target.port << "\n";
    if (sock_ < 0) return false;

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target.port);
    if (inet_pton(AF_INET, target.ip.c_str(), &addr.sin_addr) != 1) {
        return false;
    }

    const char* msg = "PING";
    if (sendto(sock_, msg, strlen(msg), 0,
               reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) <= 0) {
        return false;
    }

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
        inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
        std::cout << "[NAT] Direct UDP succeeded!\n";
        Candidate peer;
        peer.ip = ip_str;
        peer.port = ntohs(from_addr.sin_port);
        peer.type = CandidateType::kPublic;
        if (callback_) {
            callback_(true, peer);
            running_.store(false);
        }
        last_strategy_ = TraversalStrategy::kDirect;
        return true;
    }
    return false;
}

bool NatTraversal::Impl::TryHolePunch(const Candidate& target) {
    std::cout << "[NAT] UDP hole punch to " << target.ip << ":" << target.port << "\n";

    std::vector<uint16_t> ports;
    ports.push_back(target.port);

    auto predicted = port_predictor_.PredictPorts(TargetKey(target), 10);
    for (uint16_t p : predicted) {
        if (std::find(ports.begin(), ports.end(), p) == ports.end()) {
            ports.push_back(p);
        }
    }

    auto range_ports = GeneratePortRange(target.port, 5);
    for (uint16_t p : range_ports) {
        if (std::find(ports.begin(), ports.end(), p) == ports.end() && p != target.port) {
            ports.push_back(p);
        }
    }

    std::sort(ports.begin(), ports.end(),
        [target](uint16_t a, uint16_t b) {
            return std::abs(static_cast<int>(a) - static_cast<int>(target.port)) <
                   std::abs(static_cast<int>(b) - static_cast<int>(target.port));
        });
    if (ports.size() > 30) ports.resize(30);

    std::promise<bool> done_promise;
    auto done_future = done_promise.get_future();

    const int timeout_ms = 4000;
    puncher_->Punch(target.ip, ports,
        [this, &done_promise](const PunchResult& result) {
            if (result.success && !cancelled_.load()) {
                std::cout << "[NAT] UDP hole punch succeeded on port " << result.hit_port << "\n";
                Candidate peer;
                peer.ip = result.peer_ip;
                peer.port = result.peer_port;
                peer.type = CandidateType::kPublic;
                if (callback_) {
                    callback_(true, peer);
                    running_.store(false);
                }
                last_strategy_ = TraversalStrategy::kHolePunch;
                done_promise.set_value(true);
            } else {
                done_promise.set_value(false);
            }
        },
        timeout_ms);

    auto status = done_future.wait_for(std::chrono::milliseconds(timeout_ms + 200));
    if (status == std::future_status::timeout) {
        puncher_->Cancel();
        std::cout << "[NAT] Hole punch timed out.\n";
        return false;
    }
    return done_future.get();
}

bool NatTraversal::Impl::TryPortPrediction(const Candidate& target) {
    auto predicted = port_predictor_.PredictPorts(TargetKey(target), 15);
    if (predicted.empty()) {
        predicted = GeneratePortRange(target.port, 10);
    }

    std::cout << "[NAT] Port prediction to " << target.ip << ":" << target.port
              << " — trying " << predicted.size() << " ports...\n";

    std::promise<bool> done_promise;
    auto done_future = done_promise.get_future();

    const int timeout_ms = 3000;
    puncher_->Punch(target.ip, predicted,
        [this, &done_promise](const PunchResult& result) {
            if (result.success && !cancelled_.load()) {
                std::cout << "[NAT] Port prediction succeeded on port " << result.hit_port << "\n";
                Candidate peer;
                peer.ip = result.peer_ip;
                peer.port = result.peer_port;
                peer.type = CandidateType::kPublic;
                if (callback_) {
                    callback_(true, peer);
                    running_.store(false);
                }
                last_strategy_ = TraversalStrategy::kPortPrediction;
                done_promise.set_value(true);
            } else {
                done_promise.set_value(false);
            }
        },
        timeout_ms);

    auto status = done_future.wait_for(std::chrono::milliseconds(timeout_ms + 200));
    if (status == std::future_status::timeout) {
        puncher_->Cancel();
        std::cout << "[NAT] Port prediction timed out.\n";
        return false;
    }
    return done_future.get();
}

bool NatTraversal::Impl::TryIce(const Candidate& target) {
#ifdef HAVE_LIBJUICE
    std::cout << "[NAT] ICE to " << target.ip << ":" << target.port << "\n";
    // Placeholder: actual ICE implementation would go here.
    // 占位：实际 ICE 实现应在此处。
    (void)target;
    std::cout << "[ICE] ICE requires TURN server, not available.\n";
    return false;
#else
    (void)target;
    std::cout << "[NAT] ICE unavailable (libjuice not compiled)\n";
    return false;
#endif
}

void NatTraversal::Impl::RunTraversal() {
    if (!callback_) return;

    // Warm up predictor with a few STUN queries.
    // 用几个 STUN 查询预热预测器。
    LearnPortPattern();

    // Sort peer candidates by priority.
    // 按优先级排序对方候选地址。
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

        port_predictor_.RecordMapping(TargetKey(target), target.port);

        // Try strategies sequentially. 顺序尝试策略。
        if (TryDirect(target)) break;
        if (cancelled_.load()) break;

        if (TryHolePunch(target)) break;
        if (cancelled_.load()) break;

        if (TryPortPrediction(target)) break;
        if (cancelled_.load()) break;

        if (TryIce(target)) break;
        if (cancelled_.load()) break;
    }

    // If all failed.
    // 如果全部失败。
    if (!cancelled_.load() && callback_) {
        std::cout << "[NAT] All strategies failed, fallback needed.\n";
        Candidate fallback;
        fallback.type = CandidateType::kRelay;
        callback_(false, fallback);
    }
    running_.store(false);
}

// Public interface.
// 公开接口。

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
    impl_->last_strategy_ = TraversalStrategy::kNone;

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
    impl_->port_predictor_.RecordMapping(impl_->TargetKey(candidate), candidate.port);
}

} // namespace nat
} // namespace numotirus