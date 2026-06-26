// core/p2p/nat_traversal.cpp
// NAT traversal coordinator implementation. NAT 穿透协调器实现。

#include "nat_traversal.hpp"
#include "udp_hole_punch.hpp"
#include "port_prediction.hpp"
#include "libjuice_wrapper.hpp"
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

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
typedef int socklen_t;
#else
#include <sys/socket.h>
#endif

namespace numotirus {
namespace nat {

bool Candidate::operator==(const Candidate& other) const {
    return ip == other.ip && port == other.port && type == other.type;
}

struct NatTraversal::Impl {
    int sock_ = -1;
    uint16_t local_port_ = 0;
    std::string stun_server_;
    uint16_t stun_port_ = kStunPort;
    StunClient stun_client_;
    std::vector<Candidate> local_candidates_;
    std::vector<Candidate> peer_candidates_;
    std::atomic<bool> running_{false};
    std::atomic<bool> cancelled_{false};
    TraversalCallback callback_;
    std::mutex mutex_;
    std::map<std::string, bool> attempted_;

    bool CreateSocket();
    void CloseSocket();
    void GatherLocalCandidates();
    void DiscoverPublicAddress();
    void PunchLoop();
    std::string CandidateKey(const Candidate& c) const;
    void TryUdpHolePunch(const Candidate& target);
    void TryPortPrediction(const Candidate& target);
    void TryIce(const Candidate& target);
};

bool NatTraversal::Impl::CreateSocket() {
    if (sock_ >= 0) CloseSocket();

    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) return false;

    struct sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(local_port_);

    if (bind(sock_, reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr)) != 0) {
        CloseSocket();
        return false;
    }

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
    }
}

std::string NatTraversal::Impl::CandidateKey(const Candidate& c) const {
    return c.ip + ":" + std::to_string(c.port);
}

void NatTraversal::Impl::TryUdpHolePunch(const Candidate& target) {
    std::cout << "[NAT] UDP 打洞到 " << target.ip << ":" << target.port << std::endl;

    StartUdpHolePunch(local_port_, stun_server_, target.ip, target.port,
        [this](bool success, const std::string& peer_ip, uint16_t peer_port) {
            if (success && !cancelled_.load()) {
                std::cout << "[NAT] ✅ UDP 打洞成功：" << peer_ip << ":" << peer_port << std::endl;
                Candidate peer;
                peer.ip = peer_ip;
                peer.port = peer_port;
                peer.type = CandidateType::kPublic;
                callback_(true, peer);
                running_.store(false);
            }
        }
    );

    std::this_thread::sleep_for(std::chrono::seconds(2));
}

void NatTraversal::Impl::TryPortPrediction(const Candidate& target) {
    std::cout << "[NAT] 端口预测到 " << target.ip << ":" << target.port << std::endl;

    auto predicted_ports = PredictSymmetricNatPorts(local_port_, stun_server_);
    if (predicted_ports.empty()) {
        std::cout << "[NAT] 没有可用的预测端口" << std::endl;
        return;
    }

    bool success = BirthdayAttackOnSymmetricNat(local_port_, target.ip, target.port, predicted_ports);
    if (success && !cancelled_.load()) {
        std::cout << "[NAT] ✅ 端口预测成功" << std::endl;
        Candidate peer;
        peer.ip = target.ip;
        peer.port = target.port;
        peer.type = CandidateType::kPublic;
        callback_(true, peer);
        running_.store(false);
    } else {
        std::cout << "[NAT] ❌ 端口预测失败" << std::endl;
    }
}

void NatTraversal::Impl::TryIce(const Candidate& target) {
    std::cout << "[NAT] ICE 到 " << target.ip << ":" << target.port << std::endl;

    IceAgent agent;
    bool initialized = agent.Initialize(local_port_, stun_server_,
        [](IceState state) {
            std::cout << "[ICE] 状态：" << static_cast<int>(state) << std::endl;
        },
        [](const std::string& sdp) {
            std::cout << "[ICE] 候选地址：" << sdp << std::endl;
        },
        [](const uint8_t* data, size_t len) {
            std::cout << "[ICE] 收到 " << len << " 字节" << std::endl;
        }
    );

    if (!initialized) {
        std::cout << "[NAT] ICE 初始化失败" << std::endl;
        return;
    }

    agent.GatherCandidates();
    std::string local_sdp = agent.GetLocalDescription();
    if (!local_sdp.empty()) {
        std::cout << "[ICE] 本地 SDP：" << local_sdp << std::endl;
    }

    std::cout << "[ICE] ICE 需要 SDP 交换，CLI 模式未实现" << std::endl;
}

void NatTraversal::Impl::PunchLoop() {
    if (!callback_) return;

    std::vector<Candidate> targets = peer_candidates_;
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

        TryUdpHolePunch(target);
        if (cancelled_.load() || !running_.load()) break;

        TryPortPrediction(target);
        if (cancelled_.load() || !running_.load()) break;

        TryIce(target);
        if (cancelled_.load() || !running_.load()) break;
    }

    if (!cancelled_.load() && callback_) {
        Candidate fallback;
        fallback.type = CandidateType::kRelay;
        callback_(false, fallback);
    }
    running_.store(false);
}

// ============================================================================
// Public interface. 公开接口。
// ============================================================================

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
    std::thread(&Impl::PunchLoop, impl_.get()).detach();
}

void NatTraversal::CancelTraversal() {
    if (impl_->running_.load()) {
        impl_->cancelled_.store(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        impl_->running_.store(false);
    }
    impl_->CloseSocket();
}

void NatTraversal::AddPeerCandidate(const Candidate& candidate) {
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    std::string key = impl_->CandidateKey(candidate);
    for (const auto& c : impl_->peer_candidates_) {
        if (impl_->CandidateKey(c) == key) return;
    }
    impl_->peer_candidates_.push_back(candidate);
}

bool NatTraversal::SendPunchPacket(const Candidate& target) {
    return true;
}

} // namespace nat
} // namespace numotirus