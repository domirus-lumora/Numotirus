// core/transport/transport.hpp
// Transport abstraction layer for Numotirus.
// 传输抽象层，整合 DHT 和 Mesh 路由。

#pragma once

#include <cstdint>
#include <functional>
#include <array>
#include <vector>
#include <string>
#include <optional>
#include <memory>
#include <unordered_map>
#include <mutex>

namespace numotirus {
namespace transport {

// Node ID type (20 bytes, matching DHT).
// 节点 ID 类型（20 字节，与 DHT 一致）。
using NodeId = std::array<uint8_t, 20>;

// Hash specialization for NodeId to use in unordered_map.
// NodeId 的哈希特化，用于 unordered_map。
struct NodeIdHash {
    std::size_t operator()(const NodeId& id) const noexcept {
        std::size_t h = 0;
        for (uint8_t b : id) {
            h = h * 31 + b;
        }
        return h;
    }
};

// Transport state.
// 传输状态。
struct TransportState {
    bool dht_available = false;          // DHT is usable. DHT 可用。
    bool mesh_available = false;         // Mesh has neighbors. Mesh 有邻居。
    int mesh_neighbor_count = 0;         // Number of mesh neighbors. Mesh 邻居数。
    int dht_node_count = 0;              // Number of nodes in DHT routing table. DHT 路由表节点数。
};

// Callback for received messages, includes source IP and port.
// 接收消息的回调函数，包含源 IP 和端口。
using ReceiveCallback = std::function<void(
    const NodeId& from,
    const std::string& src_ip,
    uint16_t src_port,
    const uint8_t* data,
    size_t len
)>;

// Abstract transport interface.
// 传输层抽象接口。
class Transport {
public:
    virtual ~Transport() = default;

    // Send data to a NodeId. Returns true if the message was sent or queued.
    // 发送数据到目标 NodeId。返回 true 表示已发送或已入队。
    virtual bool Send(const NodeId& target, const uint8_t* data, size_t len) = 0;

    // Register callback for incoming messages.
    // 注册接收消息的回调。
    virtual void SetReceiveCallback(ReceiveCallback cb) = 0;

    // Get current state.
    // 获取当前状态。
    virtual TransportState GetState() const = 0;

    // Start/stop the transport.
    // 启动/停止传输层。
    virtual bool Start() = 0;
    virtual void Stop() = 0;
};

// DHT-based transport (for internet reachable peers).
// 基于 DHT 的传输（用于公网可达的对等节点）。
class DhtTransport : public Transport {
public:
    DhtTransport(int udp_socket, const NodeId& own_id);
    ~DhtTransport() override;

    bool Send(const NodeId& target, const uint8_t* data, size_t len) override;
    void SetReceiveCallback(ReceiveCallback cb) override;
    TransportState GetState() const override;
    bool Start() override;
    void Stop() override;

    // Resolve a NodeId to an IP:port.
    // 解析 NodeId 为 IP:port。
    std::optional<std::pair<std::string, uint16_t>> Resolve(const NodeId& target);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Mesh-based transport (for local / ad-hoc networks).
// 基于 Mesh 的传输（用于局域网/自组网）。
class MeshTransport : public Transport {
public:
    MeshTransport(int udp_socket, const NodeId& own_id);
    ~MeshTransport() override;

    bool Send(const NodeId& target, const uint8_t* data, size_t len) override;
    void SetReceiveCallback(ReceiveCallback cb) override;
    TransportState GetState() const override;
    bool Start() override;
    void Stop() override;

    // Add a discovered neighbor manually.
    // 手动添加已发现的邻居。
    void AddNeighbor(const NodeId& id, const std::string& physical_address);

    // Get the address of a neighbor by NodeId. Used by CombinedTransport.
    // 通过 NodeId 获取邻居地址。供 CombinedTransport 使用。
    std::optional<std::pair<std::string, uint16_t>> GetNeighborAddress(const NodeId& id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Combined transport that uses DHT or Mesh automatically.
// 组合传输层，自动选择 DHT 或 Mesh。
class CombinedTransport : public Transport {
public:
    CombinedTransport(int udp_socket, const NodeId& own_id);
    ~CombinedTransport() override;

    bool Send(const NodeId& target, const uint8_t* data, size_t len) override;
    void SetReceiveCallback(ReceiveCallback cb) override;
    TransportState GetState() const override;
    bool Start() override;
    void Stop() override;

    // Resolve a NodeId to an IP:port (for upper layer use).
    // 解析 NodeId 为 IP:port（供上层使用）。
    std::optional<std::pair<std::string, uint16_t>> Resolve(const NodeId& target);

    DhtTransport* GetDht() { return dht_.get(); }
    MeshTransport* GetMesh() { return mesh_.get(); }

private:
    std::unique_ptr<DhtTransport> dht_;
    std::unique_ptr<MeshTransport> mesh_;
    ReceiveCallback callback_;
    bool started_ = false;

    void OnDhtReceive(const NodeId& from, const std::string& ip, uint16_t port, const uint8_t* data, size_t len);
    void OnMeshReceive(const NodeId& from, const std::string& ip, uint16_t port, const uint8_t* data, size_t len);
};

} // namespace transport
} // namespace numotirus