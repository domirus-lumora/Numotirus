// core/p2p/dht.hpp
// Kademlia DHT routing table with BitTorrent network bootstrap.
// Kademlia DHT 路由表，支持 BitTorrent 网络引导。

#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <map>

namespace numotirus {
namespace dht {

// ============================================================
// Constants. 常量。
// ============================================================

constexpr int kK = 8;            // K-bucket capacity (BitTorrent uses 8).
constexpr int kAlpha = 3;        // Parallelism factor.
constexpr int kIdSize = 20;      // 160-bit node ID (20 bytes).

// ============================================================
// Types. 类型。
// ============================================================

using NodeId = std::array<uint8_t, kIdSize>;

// DHT node information. DHT 节点信息。
struct Node {
    NodeId id;          // Node ID. 节点 ID。
    std::string ip;     // IP address. IP 地址。
    uint16_t port = 0;  // Port. 端口。
    uint64_t last_seen = 0;  // Last seen timestamp (ms).

    bool operator==(const Node& other) const {
        return id == other.id;
    }
};

// Compact node format for wire: IP(4) + port(2) + ID(20).
// 网络传输的紧凑节点格式：IP(4) + 端口(2) + ID(20)。
struct CompactNodeInfo {
    uint32_t ip;
    uint16_t port;
    uint8_t id[kIdSize];
} __attribute__((packed));

// ============================================================
// Routing Table. 路由表。
// ============================================================

class RoutingTable {
public:
    // Constructor: initialize with own node ID.
    // 构造函数：用自己的节点 ID 初始化。
    explicit RoutingTable(const NodeId& own_id);
    ~RoutingTable();

    // Add or update a node. 添加或更新节点。
    void AddNode(const Node& node);

    // Add nodes from compact wire format. 从紧凑网络格式添加节点。
    void AddNodesFromCompact(const uint8_t* data, size_t len);

    // Find the K closest nodes to target ID. 查找离目标 ID 最近的 K 个节点。
    std::vector<Node> FindClosest(const NodeId& target, int count = kK) const;

    // Get own node ID. 获取自己的节点 ID。
    const NodeId& GetOwnId() const { return own_id_; }

    // Get all nodes (for debugging). 获取所有节点（用于调试）。
    std::vector<Node> GetAllNodes() const;

    // Get total number of nodes in routing table. 获取路由表总节点数。
    int GetTotalNodes() const;

    // Print routing table for debugging. 打印路由表用于调试。
    void Print() const;

    // Clear routing table. 清空路由表。
    void Clear();

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
    KBucket buckets_[160];  // 160 bits => 160 buckets.

    static void XorDistance(const NodeId& a, const NodeId& b, NodeId& out);
    static int CompareId(const NodeId& a, const NodeId& b);
    int GetBucketIndex(const NodeId& other) const;
    void EvictOldest(KBucket& bucket);
};

// ============================================================
// Bencode Parser (all static methods). Bencode 解析器（全部静态方法）。
// ============================================================

class BencodeParser {
public:
    // Value union type. 值联合类型。
    struct Value {
        enum Type { kDict, kList, kString, kInt, kNone };
        Type type = kNone;
        std::string str;
        int64_t integer = 0;
        std::vector<Value> list;
        std::map<std::string, Value> dict;
    };

    // Parse from string. 从字符串解析。
    static Value Parse(const std::string& data);
    // Parse from raw data (auto-detect length). 从原始数据解析（自动检测长度）。
    static Value Parse(const uint8_t* data, size_t len);
    // Parse from raw data with position tracking. 从原始数据解析，带位置跟踪。
    static Value Parse(const uint8_t* data, size_t len, size_t& pos);

private:
    static Value ParseDict(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseList(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseString(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseInt(const uint8_t* data, size_t len, size_t& pos);
};

// ============================================================
// DHT Client. DHT 客户端。
// ============================================================

// Callback for DHT responses. DHT 响应回调。
using DhtResponseCallback = std::function<void(
    const std::string& peer_ip,
    uint16_t peer_port,
    const std::vector<Node>& nodes
)>;

class DhtClient {
public:
    DhtClient();
    ~DhtClient();

    // Initialize with UDP socket and own node ID.
    // 用 UDP 套接字和自己的节点 ID 初始化。
    bool Initialize(int sock, const NodeId& own_id);

    // Bootstrap from a list of known nodes. 从已知节点列表引导。
    int Bootstrap(const std::vector<std::pair<std::string, uint16_t>>& bootstrap_nodes,
                  DhtResponseCallback callback = nullptr);

    // Bootstrap using default BitTorrent public nodes.
    // 使用默认 BitTorrent 公共节点引导。
    int BootstrapDefault(DhtResponseCallback callback = nullptr);

    // Get routing table. 获取路由表。
    RoutingTable& GetRoutingTable() { return *routing_table_; }
    const RoutingTable& GetRoutingTable() const { return *routing_table_; }

    // Static utilities. 静态工具方法。
    static std::string GenerateTransactionId();
    static NodeId GenerateNodeId();

private:
    int sock_;
    NodeId own_id_;
    std::unique_ptr<RoutingTable> routing_table_;
    std::map<std::string, uint64_t> pending_queries_;

    std::string EncodeFindNode(const NodeId& target, const std::string& tid);
    std::string EncodePing(const std::string& tid);
    bool SendFindNode(const std::string& ip, uint16_t port, const NodeId& target);
    bool SendPing(const std::string& ip, uint16_t port);
    std::vector<Node> DecodeCompactNodes(const std::string& compact);
    bool DecodeResponse(const uint8_t* data, size_t len,
                        std::vector<Node>& out_nodes,
                        std::string& out_tid);
    bool ParsePacket(const uint8_t* data, size_t len,
                     std::string& from_ip, uint16_t& from_port,
                     std::vector<Node>& nodes);
};

// ============================================================
// Convenience function. 便捷函数。
// ============================================================

// Bootstrap a routing table using default BitTorrent nodes.
// 使用默认 BitTorrent 节点引导路由表。
int BootstrapRoutingTable(
    int sock,
    const NodeId& own_id,
    RoutingTable& routing_table,
    DhtResponseCallback callback = nullptr
);

} // namespace dht
} // namespace numotirus