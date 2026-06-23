// core/p2p/nat_traversal.cpp
// NAT traversal coordinator implementation. NAT 穿透协调器实现。

#include "nat_traversal.hpp"

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <ifaddrs.h>
#include <unistd.h>
#endif

#include <cstring>
#include <thread>
#include <chrono>
#include <random>
#include <algorithm>
#include <atomic>
#include <mutex>
#include <map>

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

    // Track which candidates have been attempted.
    std::map<std::string, bool> attempted_;

    bool CreateSocket();
    void CloseSocket();
    void GatherLocalCandidates();
    void DiscoverPublicAddress();
    void PunchLoop();
    std::string CandidateKey(const Candidate& c) const;

    // Make SendPunchPacket public so that NatTraversal can call it.
    bool SendPunchPacket(const Candidate& target);
};

bool NatTraversal::Impl::CreateSocket() {
    if (sock_ >= 0) {
        CloseSocket();
    }

    sock_ = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock_ < 0) {
        return false;
    }

    // Bind to local port.
    struct sockaddr_in addr = {};
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

    // Host candidate: 127.0.0.1
    Candidate host;
    host.type = CandidateType::kHost;
    host.ip = "127.0.0.1";
    host.port = local_port_;
    host.foundation = "host";
    host.priority = 126;
    local_candidates_.push_back(host);

#ifdef _WIN32
    // Windows: use GetAdaptersInfo.
    ULONG buffer_size = 0;
    GetAdaptersInfo(nullptr, &buffer_size);
    if (buffer_size > 0) {
        std::vector<uint8_t> buffer(buffer_size);
        PIP_ADAPTER_INFO adapter_info = reinterpret_cast<PIP_ADAPTER_INFO>(buffer.data());
        if (GetAdaptersInfo(adapter_info, &buffer_size) == NO_ERROR) {
            for (PIP_ADAPTER_INFO adapter = adapter_info; adapter != nullptr; adapter = adapter->Next) {
                if (adapter->IpAddressList.IpAddress.String[0] == '0') continue;
                if (strcmp(adapter->IpAddressList.IpAddress.String, "127.0.0.1") == 0) continue;

                Candidate c;
                c.type = CandidateType::kHost;
                c.ip = adapter->IpAddressList.IpAddress.String;
                c.port = local_port_;
                c.foundation = "host_" + std::string(c.ip);
                c.priority = 110;
                local_candidates_.push_back(c);
            }
        }
    }
#else
    // Linux: use getifaddrs.
    struct ifaddrs* ifaddr = nullptr;
    if (getifaddrs(&ifaddr) == 0) {
        for (struct ifaddrs* ifa = ifaddr; ifa != nullptr; ifa = ifa->ifa_next) {
            if (ifa->ifa_addr == nullptr) continue;
            if (ifa->ifa_addr->sa_family != AF_INET) continue;

            struct sockaddr_in* sa = reinterpret_cast<struct sockaddr_in*>(ifa->ifa_addr);
            char ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &sa->sin_addr, ip, INET_ADDRSTRLEN);

            if (strcmp(ip, "127.0.0.1") == 0) continue;
            if (strncmp(ip, "0.", 2) == 0) continue;

            Candidate c;
            c.type = CandidateType::kHost;
            c.ip = ip;
            c.port = local_port_;
            c.foundation = "host_" + std::string(ip);
            c.priority = 110;
            local_candidates_.push_back(c);
        }
        freeifaddrs(ifaddr);
    }
#endif
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

bool NatTraversal::Impl::SendPunchPacket(const Candidate& target) {
    if (sock_ < 0) return false;

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(target.port);
    inet_pton(AF_INET, target.ip.c_str(), &addr.sin_addr);

    // Simple punch packet: "PUNCH" + random data.
    std::string msg = "PUNCH";
    std::random_device rd;
    std::mt19937 gen(rd());
    for (int i = 0; i < 8; ++i) {
        msg.push_back(static_cast<char>(gen() & 0xff));
    }

    int sent = sendto(sock_, msg.c_str(), msg.size(), 0,
                      reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return sent == static_cast<int>(msg.size());
}

void NatTraversal::Impl::PunchLoop() {
    if (!callback_) return;

    // Sort peer candidates by priority.
    std::vector<Candidate> targets = peer_candidates_;
    std::sort(targets.begin(), targets.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.priority > b.priority;
              });

    // Try each candidate.
    for (const auto& target : targets) {
        if (cancelled_.load()) break;

        std::string key = CandidateKey(target);
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (attempted_[key]) continue;
            attempted_[key] = true;
        }

        // Send punch packets (multiple packets to increase success rate).
        for (int i = 0; i < 3 && !cancelled_.load(); ++i) {
            SendPunchPacket(target);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }

        // Wait for response (or timeout).
        uint8_t buffer[1024];
        struct sockaddr_in from;
        socklen_t from_len = sizeof(from);

#ifdef _WIN32
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock_, &fds);
        struct timeval tv = {0, 100000}; // 100ms
        int ret = select(sock_ + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(sock_, &fds)) {
            int n = recvfrom(sock_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                             reinterpret_cast<struct sockaddr*>(&from), &from_len);
            if (n > 0) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from.sin_addr, ip, INET_ADDRSTRLEN);
                uint16_t port = ntohs(from.sin_port);

                std::string from_key = std::string(ip) + ":" + std::to_string(port);
                if (attempted_[from_key]) {
                    // Success!
                    Candidate peer;
                    peer.ip = ip;
                    peer.port = port;
                    peer.type = CandidateType::kPublic;
                    callback_(true, peer);
                    running_.store(false);
                    return;
                }
            }
        }
#else
        struct timeval tv = {0, 100000};
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock_, &fds);
        int ret = select(sock_ + 1, &fds, nullptr, nullptr, &tv);
        if (ret > 0 && FD_ISSET(sock_, &fds)) {
            int n = recvfrom(sock_, buffer, sizeof(buffer), 0,
                             reinterpret_cast<struct sockaddr*>(&from), &from_len);
            if (n > 0) {
                char ip[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from.sin_addr, ip, INET_ADDRSTRLEN);
                uint16_t port = ntohs(from.sin_port);

                std::string from_key = std::string(ip) + ":" + std::to_string(port);
                if (attempted_[from_key]) {
                    Candidate peer;
                    peer.ip = ip;
                    peer.port = port;
                    peer.type = CandidateType::kPublic;
                    callback_(true, peer);
                    running_.store(false);
                    return;
                }
            }
        }
#endif
    }

    // All attempts failed.
    if (!cancelled_.load() && callback_) {
        Candidate fallback;
        fallback.type = CandidateType::kRelay;
        callback_(false, fallback);
    }
    running_.store(false);
}

// ============================================================================
// Public interface.
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

    // Create socket and gather candidates.
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

    // Start punching in a separate thread.
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
    return impl_->SendPunchPacket(target);
}

} // namespace nat
} // namespace numotirus