// core/p2p/dht.hpp
// Kademlia DHT routing table. Kademlia DHT 路由表。

#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <string>

namespace numotirus {
namespace dht {

constexpr int kK = 20;           // K-bucket 容量 / Bucket capacity
constexpr int kAlpha = 3;        // 并行查询数 / Parallelism factor
constexpr int kIdSize = 32;      // 256-bit node ID (公钥大小 / Public key size)

using NodeId = std::array<uint8_t, kIdSize>;

// DHT node information. DHT 节点信息。
struct Node {
    NodeId id;          // 节点 ID / Node ID
    std::string ip;     // IP 地址 / IP address
    uint16_t port;      // 端口 / Port
    uint64_t last_seen; // 最后活跃时间戳 (毫秒) / Last seen timestamp (ms)
};

// Kademlia routing table. Kademlia 路由表。
class RoutingTable {
public:
    // Construct with own node ID. 用自身节点 ID 构造。
    explicit RoutingTable(const NodeId& own_id);

    // Destructor. 析构。
    ~RoutingTable();

    // Add or update a node. 添加或更新节点。
    void AddNode(const Node& node);

    // Find K closest nodes to target. 查找离目标最近的 K 个节点。
    std::vector<Node> FindClosest(const NodeId& target, int count = kK) const;

    // Print routing table for debugging. 打印路由表用于调试。
    void Print() const;

private:
    struct BucketNode {
        Node node;
        BucketNode* next = nullptr;
    };

    struct KBucket {
        BucketNode* head = nullptr;
        int count = 0;
    };

    NodeId own_id_;
    KBucket buckets_[256];

    static void XorDistance(const NodeId& a, const NodeId& b, NodeId& out);
    static int CompareId(const NodeId& a, const NodeId& b);
    int GetBucketIndex(const NodeId& other) const;
};

} // namespace dht
} // namespace numotirus