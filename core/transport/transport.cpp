// core/transport/transport.cpp
// Transport abstraction implementation.
// 传输抽象层实现。

#include "transport.hpp"
#include "../p2p/dht.hpp"
#include "../p2p/nat/udp_hole_punch.hpp"
#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <memory>

#ifdef _WIN32
#include <winsock2.h>
#include <windows.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef int socklen_t;
#else
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <fcntl.h>
#include <netdb.h>
#endif

namespace numotirus {
namespace transport {

// DhtTransport implementation.
// DhtTransport 实现。

struct DhtTransport::Impl {
    int sock_;
    NodeId own_id_;
    std::unique_ptr<dht::DhtClient> client_;
    ReceiveCallback callback_;
    bool running_ = false;
    std::thread worker_;

    Impl(int sock, const NodeId& id) : sock_(sock), own_id_(id) {
        client_ = std::make_unique<dht::DhtClient>();
        client_->Initialize(sock_, own_id_);
    }

    ~Impl() {
        Stop();
    }

    void Start() {
        if (running_) return;
        running_ = true;
        client_->BootstrapDefault([this](const std::string& ip, uint16_t port, const std::vector<dht::Node>& nodes) {
            (void)ip; (void)port; (void)nodes;
        });

        worker_ = std::thread([this]() {
            uint8_t buf[4096];
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            while (running_) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(sock_, &fds);
                struct timeval tv = {1, 0};
                if (select(sock_ + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
                int n = recvfrom(sock_, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);
                if (n > 0) {
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &from.sin_addr, ip_str, sizeof(ip_str));
                    uint16_t port = ntohs(from.sin_port);

                    client_->FeedPacket(buf, n);

                    if (callback_) {
                        NodeId dummy;
                        callback_(dummy, std::string(ip_str), port, buf, n);
                    }
                }
            }
        });
    }

    void Stop() {
        running_ = false;
        if (worker_.joinable()) worker_.join();
    }
};

DhtTransport::DhtTransport(int udp_socket, const NodeId& own_id)
    : impl_(std::make_unique<Impl>(udp_socket, own_id)) {}

DhtTransport::~DhtTransport() = default;

bool DhtTransport::Send(const NodeId& target, const uint8_t* data, size_t len) {
    auto addr = Resolve(target);
    if (!addr) return false;

    struct sockaddr_in dest = {};
    dest.sin_family = AF_INET;
    dest.sin_port = htons(addr->second);
    if (inet_pton(AF_INET, addr->first.c_str(), &dest.sin_addr) != 1) return false;

    int sent = sendto(impl_->sock_, reinterpret_cast<const char*>(data), len, 0,
                      (struct sockaddr*)&dest, sizeof(dest));
    return sent == (int)len;
}

void DhtTransport::SetReceiveCallback(ReceiveCallback cb) {
    impl_->callback_ = cb;
}

TransportState DhtTransport::GetState() const {
    TransportState state;
    state.dht_available = impl_->client_->GetRoutingTable().GetTotalNodes() > 0;
    state.dht_node_count = impl_->client_->GetRoutingTable().GetTotalNodes();
    return state;
}

bool DhtTransport::Start() {
    impl_->Start();
    return true;
}

void DhtTransport::Stop() {
    impl_->Stop();
}

std::optional<std::pair<std::string, uint16_t>> DhtTransport::Resolve(const NodeId& target) {
    auto nodes = impl_->client_->IterativeFindNode(target, 8);
    if (nodes.empty()) return std::nullopt;
    const auto& best = nodes[0];
    return std::make_pair(best.ip, best.port);
}

// MeshTransport implementation.
// MeshTransport 实现。

struct MeshTransport::Impl {
    int sock_;
    NodeId own_id_;
    ReceiveCallback callback_;
    std::unordered_map<NodeId, std::string, NodeIdHash> neighbors_;
    mutable std::mutex mutex_;   // mutable for const methods.
    bool running_ = false;
    std::thread discover_thread_;
    std::thread recv_thread_;

    Impl(int sock, const NodeId& id) : sock_(sock), own_id_(id) {}

    ~Impl() {
        Stop();
    }

    void Start() {
        if (running_) return;
        running_ = true;

        discover_thread_ = std::thread([this]() {
            struct sockaddr_in broadcast_addr = {};
            broadcast_addr.sin_family = AF_INET;
            broadcast_addr.sin_port = htons(8889);
            broadcast_addr.sin_addr.s_addr = INADDR_BROADCAST;
            int broadcast_enable = 1;
#ifdef _WIN32
            setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, (char*)&broadcast_enable, sizeof(broadcast_enable));
#else
            setsockopt(sock_, SOL_SOCKET, SO_BROADCAST, &broadcast_enable, sizeof(broadcast_enable));
#endif

            while (running_) {
                std::string msg = "DISCOVER ";
                for (uint8_t b : own_id_) {
                    char hex[3];
                    snprintf(hex, sizeof(hex), "%02x", b);
                    msg += hex;
                }
                sendto(sock_, msg.c_str(), msg.size(), 0,
                       (struct sockaddr*)&broadcast_addr, sizeof(broadcast_addr));
                std::this_thread::sleep_for(std::chrono::seconds(5));
            }
        });

        recv_thread_ = std::thread([this]() {
            uint8_t buf[4096];
            struct sockaddr_in from;
            socklen_t from_len = sizeof(from);
            while (running_) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(sock_, &fds);
                struct timeval tv = {1, 0};
                if (select(sock_ + 1, &fds, nullptr, nullptr, &tv) <= 0) continue;
                int n = recvfrom(sock_, (char*)buf, sizeof(buf), 0, (struct sockaddr*)&from, &from_len);
                if (n <= 0) continue;

                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from.sin_addr, ip_str, sizeof(ip_str));
                uint16_t port = ntohs(from.sin_port);

                std::string msg((char*)buf, n);
                if (msg.rfind("DISCOVER", 0) == 0) {
                    std::string hex = msg.substr(9);
                    if (hex.size() == 40) {
                        NodeId id;
                        for (int i = 0; i < 20; i++) {
                            std::string byte_str = hex.substr(i*2, 2);
                            id[i] = (uint8_t)std::stoi(byte_str, nullptr, 16);
                        }
                        std::string addr = std::string(ip_str) + ":" + std::to_string(port);
                        AddNeighbor(id, addr);
                    }
                } else {
                    if (callback_) {
                        NodeId dummy;
                        callback_(dummy, std::string(ip_str), port, buf, n);
                    }
                }
            }
        });
    }

    void Stop() {
        running_ = false;
        if (discover_thread_.joinable()) discover_thread_.join();
        if (recv_thread_.joinable()) recv_thread_.join();
    }

    void AddNeighbor(const NodeId& id, const std::string& addr) {
        std::lock_guard<std::mutex> lock(mutex_);
        neighbors_[id] = addr;
    }

    bool SendToNeighbor(const NodeId& target, const uint8_t* data, size_t len) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = neighbors_.find(target);
        if (it == neighbors_.end()) return false;
        auto pos = it->second.find(':');
        if (pos == std::string::npos) return false;
        std::string ip = it->second.substr(0, pos);
        uint16_t port = std::stoi(it->second.substr(pos+1));
        struct sockaddr_in dest = {};
        dest.sin_family = AF_INET;
        dest.sin_port = htons(port);
        if (inet_pton(AF_INET, ip.c_str(), &dest.sin_addr) != 1) return false;
        int sent = sendto(sock_, reinterpret_cast<const char*>(data), len, 0,
                          (struct sockaddr*)&dest, sizeof(dest));
        return sent == (int)len;
    }

    std::optional<std::pair<std::string, uint16_t>> GetNeighborAddress(const NodeId& id) const {
        std::lock_guard<std::mutex> lock(mutex_);  // mutex_ is mutable
        auto it = neighbors_.find(id);
        if (it == neighbors_.end()) return std::nullopt;
        auto pos = it->second.find(':');
        if (pos == std::string::npos) return std::nullopt;
        std::string ip = it->second.substr(0, pos);
        uint16_t port = std::stoi(it->second.substr(pos+1));
        return std::make_pair(ip, port);
    }
};

MeshTransport::MeshTransport(int udp_socket, const NodeId& own_id)
    : impl_(std::make_unique<Impl>(udp_socket, own_id)) {}

MeshTransport::~MeshTransport() = default;

bool MeshTransport::Send(const NodeId& target, const uint8_t* data, size_t len) {
    return impl_->SendToNeighbor(target, data, len);
}

void MeshTransport::SetReceiveCallback(ReceiveCallback cb) {
    impl_->callback_ = cb;
}

TransportState MeshTransport::GetState() const {
    TransportState state;
    std::lock_guard<std::mutex> lock(impl_->mutex_);
    state.mesh_available = !impl_->neighbors_.empty();
    state.mesh_neighbor_count = impl_->neighbors_.size();
    return state;
}

bool MeshTransport::Start() {
    impl_->Start();
    return true;
}

void MeshTransport::Stop() {
    impl_->Stop();
}

void MeshTransport::AddNeighbor(const NodeId& id, const std::string& physical_address) {
    impl_->AddNeighbor(id, physical_address);
}

std::optional<std::pair<std::string, uint16_t>> MeshTransport::GetNeighborAddress(const NodeId& id) const {
    return impl_->GetNeighborAddress(id);
}

// CombinedTransport implementation.
// CombinedTransport 实现。

CombinedTransport::CombinedTransport(int udp_socket, const NodeId& own_id)
    : dht_(std::make_unique<DhtTransport>(udp_socket, own_id)),
      mesh_(std::make_unique<MeshTransport>(udp_socket, own_id)) {
    dht_->SetReceiveCallback([this](const NodeId& from, const std::string& ip, uint16_t port, const uint8_t* data, size_t len) {
        OnDhtReceive(from, ip, port, data, len);
    });
    mesh_->SetReceiveCallback([this](const NodeId& from, const std::string& ip, uint16_t port, const uint8_t* data, size_t len) {
        OnMeshReceive(from, ip, port, data, len);
    });
}

CombinedTransport::~CombinedTransport() = default;

bool CombinedTransport::Send(const NodeId& target, const uint8_t* data, size_t len) {
    auto state = GetState();
    if (state.dht_available) {
        if (dht_->Send(target, data, len)) return true;
    }
    if (state.mesh_available) {
        if (mesh_->Send(target, data, len)) return true;
    }
    return false;
}

void CombinedTransport::SetReceiveCallback(ReceiveCallback cb) {
    callback_ = cb;
}

TransportState CombinedTransport::GetState() const {
    TransportState state = dht_->GetState();
    auto mesh_state = mesh_->GetState();
    state.mesh_available = mesh_state.mesh_available;
    state.mesh_neighbor_count = mesh_state.mesh_neighbor_count;
    return state;
}

bool CombinedTransport::Start() {
    if (started_) return true;
    dht_->Start();
    mesh_->Start();
    started_ = true;
    return true;
}

void CombinedTransport::Stop() {
    dht_->Stop();
    mesh_->Stop();
    started_ = false;
}

std::optional<std::pair<std::string, uint16_t>> CombinedTransport::Resolve(const NodeId& target) {
    auto result = dht_->Resolve(target);
    if (result) return result;
    return mesh_->GetNeighborAddress(target);
}

void CombinedTransport::OnDhtReceive(const NodeId& from, const std::string& ip, uint16_t port, const uint8_t* data, size_t len) {
    if (callback_) callback_(from, ip, port, data, len);
}

void CombinedTransport::OnMeshReceive(const NodeId& from, const std::string& ip, uint16_t port, const uint8_t* data, size_t len) {
    if (callback_) callback_(from, ip, port, data, len);
}

} // namespace transport
} // namespace numotirus