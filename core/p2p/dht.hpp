// core/p2p/dht.hpp
// Kademlia DHT routing table - MSB fixed, thread-safe.
// Kademlia DHT 路由表 —— 修复 MSB 索引，线程安全。
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <map>
#include <mutex>

namespace numotirus {
namespace dht {

constexpr int kK = 8;
constexpr int kAlpha = 3;
constexpr int kIdSize = 20;

using NodeId = std::array<uint8_t, kIdSize>;

struct Node {
    NodeId id;
    std::string ip;
    uint16_t port = 0;
    uint64_t last_seen = 0;

    bool operator==(const Node& other) const {
        return id == other.id;
    }
};

struct CompactNodeInfo {
    uint32_t ip;
    uint16_t port;
    uint8_t id[kIdSize];
} __attribute__((packed));

class RoutingTable {
public:
    explicit RoutingTable(const NodeId& own_id);
    ~RoutingTable();

    void AddNode(const Node& node);
    void AddNodesFromCompact(const uint8_t* data, size_t len);
    std::vector<Node> FindClosest(const NodeId& target, int count = kK) const;

    const NodeId& GetOwnId() const { return own_id_; }
    std::vector<Node> GetAllNodes() const;
    int GetTotalNodes() const;
    void Print() const;
    void Clear();

    // Public static helpers (used by dht_c.cpp for sorting). 公开静态辅助函数。
    static void XorDistance(const NodeId& a, const NodeId& b, NodeId& out);
    static int CompareId(const NodeId& a, const NodeId& b);

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
    mutable std::mutex mutex_;
    KBucket buckets_[160];

    int GetBucketIndex(const NodeId& other) const;
    void EvictOldest(KBucket& bucket);
    void CollectAllNodesLocked(std::vector<Node>& out) const;
};

class BencodeParser {
public:
    struct Value {
        enum Type { kDict, kList, kString, kInt, kNone };
        Type type = kNone;
        std::string str;
        int64_t integer = 0;
        std::vector<Value> list;
        std::map<std::string, Value> dict;
    };

    static Value Parse(const std::string& data);
    static Value Parse(const uint8_t* data, size_t len);
    static Value Parse(const uint8_t* data, size_t len, size_t& pos);

private:
    static Value ParseDict(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseList(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseString(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseInt(const uint8_t* data, size_t len, size_t& pos);
};

using DhtResponseCallback = std::function<void(
    const std::string& peer_ip,
    uint16_t peer_port,
    const std::vector<Node>& nodes
)>;

class DhtClient {
public:
    DhtClient();
    ~DhtClient();

    bool Initialize(int sock, const NodeId& own_id);
    int Bootstrap(const std::vector<std::pair<std::string, uint16_t>>& bootstrap_nodes,
                  DhtResponseCallback callback = nullptr);
    int BootstrapDefault(DhtResponseCallback callback = nullptr);

    RoutingTable& GetRoutingTable() { return *routing_table_; }
    const RoutingTable& GetRoutingTable() const { return *routing_table_; }

    // Public for dht_c.cpp. 公开供 dht_c.cpp 使用。
    bool SendFindNode(const std::string& ip, uint16_t port, const NodeId& target);
    bool DecodeResponse(const uint8_t* data, size_t len,
                        std::vector<Node>& out_nodes,
                        std::string& out_tid);

    static std::string GenerateTransactionId();
    static NodeId GenerateNodeId();

private:
    int sock_;
    NodeId own_id_;
    std::unique_ptr<RoutingTable> routing_table_;
    std::map<std::string, uint64_t> pending_queries_;

    std::string EncodeFindNode(const NodeId& target, const std::string& tid);
    std::string EncodePing(const std::string& tid);
    bool SendPing(const std::string& ip, uint16_t port);
    std::vector<Node> DecodeCompactNodes(const std::string& compact);
};

int BootstrapRoutingTable(
    int sock,
    const NodeId& own_id,
    RoutingTable& routing_table,
    DhtResponseCallback callback = nullptr
);

} // namespace dht
} // namespace numotirus