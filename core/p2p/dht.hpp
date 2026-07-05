// core/p2p/dht.hpp
// Kademlia DHT routing table - Full BEP 5 implementation with get_peers/announce_peer.
// Kademlia DHT 路由表 —— 完整 BEP 5 实现，包含 get_peers/announce_peer。
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <array>
#include <vector>
#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include <unordered_map>
#include <mutex>
#include <fstream>
#include <chrono>

namespace numotirus {
namespace dht {

constexpr int kK = 8;                           // Bucket size. 桶大小。
constexpr int kAlpha = 3;                       // Parallelism factor. 并行因子。
constexpr int kIdSize = 20;                     // Node ID size in bytes. 节点 ID 字节数。
constexpr int kPendingTimeoutMs = 15000;        // Pending query timeout. 待处理查询超时。

using NodeId = std::array<uint8_t, kIdSize>;
using InfoHash = std::array<uint8_t, kIdSize>;

struct Node {
    NodeId id;
    std::string ip;
    uint16_t port = 0;
    uint64_t last_seen = 0;

    bool operator==(const Node& other) const {
        return id == other.id;
    }
};

struct PeerInfo {
    std::string ip;
    uint16_t port = 0;
    uint64_t last_seen = 0;
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
        std::unordered_map<std::string, Value> dict;
    };

    static Value Parse(const std::string& data);
    static Value Parse(const uint8_t* data, size_t len);
    static std::string Encode(const Value& val);

private:
    static Value Parse(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseDict(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseList(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseString(const uint8_t* data, size_t len, size_t& pos);
    static Value ParseInt(const uint8_t* data, size_t len, size_t& pos);
    static void EncodeValue(const Value& val, std::string& out);
};

using DhtResponseCallback = std::function<void(
    const std::string& peer_ip,
    uint16_t peer_port,
    const std::vector<Node>& nodes
)>;

using GetPeersCallback = std::function<void(
    const std::vector<PeerInfo>& peers,
    const std::vector<Node>& nodes,
    const std::string& token
)>;

using AnnounceCallback = std::function<void(bool success)>;

class DhtClient {
public:
    DhtClient();
    ~DhtClient();

    // Feed a raw UDP packet for processing (DHT responses and application data).
    // 喂入原始 UDP 包进行处理（DHT 响应和应用数据）。
    void FeedPacket(const uint8_t* data, size_t len) {
        HandleResponse(data, len);
    }

    bool Initialize(int sock, const NodeId& own_id);

    // Bootstrap from nodes. Sends all requests in parallel, total timeout ~5 seconds.
    // 从节点引导。并行发送所有请求，总超时约 5 秒。
    int Bootstrap(const std::vector<std::pair<std::string, uint16_t>>& bootstrap_nodes,
                  DhtResponseCallback callback = nullptr);

    int BootstrapDefault(DhtResponseCallback callback = nullptr);

    // BEP 5: get_peers.
    // BEP 5：获取对等节点。
    bool GetPeers(const InfoHash& info_hash, GetPeersCallback callback);

    // BEP 5: announce_peer.
    // BEP 5：宣告对等节点。
    bool AnnouncePeer(const InfoHash& info_hash, uint16_t port, const std::string& token,
                      AnnounceCallback callback = nullptr);

    bool SendFindNode(const std::string& ip, uint16_t port, const NodeId& target);
    bool DecodeResponse(const uint8_t* data, size_t len,
                        std::vector<Node>& out_nodes,
                        std::string& out_tid);

    RoutingTable& GetRoutingTable() { return *routing_table_; }
    const RoutingTable& GetRoutingTable() const { return *routing_table_; }

    static std::string GenerateTransactionId();
    static NodeId GenerateNodeId();

    std::vector<Node> IterativeFindNode(const NodeId& target, int count = kK);
    std::vector<PeerInfo> IterativeGetPeers(const InfoHash& info_hash);
    bool IterativeAnnouncePeer(const InfoHash& info_hash, uint16_t port);

private:
    int sock_ = -1;
    NodeId own_id_;
    std::unique_ptr<RoutingTable> routing_table_;
    std::unordered_map<std::string, uint64_t> pending_queries_;      // tid -> timestamp. 事务ID -> 时间戳。
    std::unordered_map<std::string, GetPeersCallback> pending_get_peers_;
    std::unordered_map<std::string, AnnounceCallback> pending_announce_;

    void CleanPendingQueries();  // Remove timed-out entries. 移除超时条目。

    std::string EncodeFindNode(const NodeId& target, const std::string& tid);
    std::string EncodeGetPeers(const InfoHash& info_hash, const std::string& tid);
    std::string EncodeAnnouncePeer(const InfoHash& info_hash, uint16_t port,
                                   const std::string& token, const std::string& tid);
    std::string EncodePing(const std::string& tid);

    bool SendGetPeers(const std::string& ip, uint16_t port, const InfoHash& info_hash);
    bool SendAnnouncePeer(const std::string& ip, uint16_t port, const InfoHash& info_hash,
                          uint16_t announce_port, const std::string& token);

    bool SendPing(const std::string& ip, uint16_t port);
    std::vector<Node> DecodeCompactNodes(const std::string& compact);
    std::vector<PeerInfo> DecodeCompactPeers(const std::string& compact);

    bool ParseGetPeersResponse(const uint8_t* data, size_t len,
                               std::vector<PeerInfo>& out_peers,
                               std::vector<Node>& out_nodes,
                               std::string& out_token,
                               std::string& out_tid);

    bool ParseAnnounceResponse(const uint8_t* data, size_t len,
                               std::string& out_tid);

    void HandleResponse(const uint8_t* data, size_t len);
};

int BootstrapRoutingTable(
    int sock,
    const NodeId& own_id,
    RoutingTable& routing_table,
    DhtResponseCallback callback = nullptr
);

} // namespace dht
} // namespace numotirus