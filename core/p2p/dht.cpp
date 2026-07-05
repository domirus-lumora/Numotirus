// core/p2p/dht.cpp
// Kademlia DHT routing table implementation.
// Kademlia DHT 路由表实现。
// SPDX-License-Identifier: Apache-2.0

#include "dht.hpp"
#include <sodium.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <thread>
#include <sstream>
#include <iomanip>

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
#include <fcntl.h>
#endif

namespace numotirus {
namespace dht {

// RoutingTable implementation.
// RoutingTable 实现。

RoutingTable::RoutingTable(const NodeId& own_id) : own_id_(own_id) {
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium init failed");
    }
}

RoutingTable::~RoutingTable() {
    Clear();
}

void RoutingTable::Clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& bucket : buckets_) {
        BucketNode* cur = bucket.head;
        while (cur) {
            BucketNode* next = cur->next;
            delete cur;
            cur = next;
        }
        bucket.head = nullptr;
        bucket.count = 0;
    }
}

void RoutingTable::XorDistance(const NodeId& a, const NodeId& b, NodeId& out) {
    for (size_t i = 0; i < kIdSize; ++i) {
        out[i] = a[i] ^ b[i];
    }
}

int RoutingTable::CompareId(const NodeId& a, const NodeId& b) {
    return std::memcmp(a.data(), b.data(), kIdSize);
}

int RoutingTable::GetBucketIndex(const NodeId& other) const {
    NodeId diff;
    XorDistance(own_id_, other, diff);
    for (int i = 0; i < kIdSize; ++i) {
        if (diff[i] == 0) continue;
        for (int bit = 7; bit >= 0; --bit) {
            if (diff[i] & (1 << bit)) {
                return (i * 8) + (7 - bit);
            }
        }
    }
    return -1;
}

void RoutingTable::CollectAllNodesLocked(std::vector<Node>& out) const {
    out.clear();
    for (const auto& bucket : buckets_) {
        BucketNode* cur = bucket.head;
        while (cur) {
            out.push_back(cur->node);
            cur = cur->next;
        }
    }
}

void RoutingTable::AddNode(const Node& node) {
    if (std::memcmp(own_id_.data(), node.id.data(), kIdSize) == 0) return;
    int idx = GetBucketIndex(node.id);
    if (idx < 0 || idx >= 160) return;

    std::lock_guard<std::mutex> lock(mutex_);
    KBucket& bucket = buckets_[idx];

    BucketNode* cur = bucket.head;
    BucketNode* prev = nullptr;
    while (cur) {
        if (std::memcmp(cur->node.id.data(), node.id.data(), kIdSize) == 0) {
            // Move to front if already exists.
            // 如果已存在，移到队首。
            if (prev) {
                prev->next = cur->next;
                cur->next = bucket.head;
                bucket.head = cur;
            }
            cur->node.last_seen = node.last_seen;
            return;
        }
        prev = cur;
        cur = cur->next;
    }

    // Insert new node at the front.
    // 在队首插入新节点。
    BucketNode* new_node = new BucketNode{node, bucket.head};
    bucket.head = new_node;
    bucket.count++;
    if (bucket.count > kK) {
        EvictOldest(bucket);
    }
}

void RoutingTable::EvictOldest(KBucket& bucket) {
    if (!bucket.head || bucket.count <= kK) return;
    BucketNode* prev = nullptr;
    BucketNode* last = bucket.head;
    while (last && last->next) {
        prev = last;
        last = last->next;
    }
    if (prev) {
        prev->next = nullptr;
        delete last;
        bucket.count--;
    } else if (last) {
        delete last;
        bucket.head = nullptr;
        bucket.count = 0;
    }
}

void RoutingTable::AddNodesFromCompact(const uint8_t* data, size_t len) {
    size_t offset = 0;
    while (offset + sizeof(CompactNodeInfo) <= len) {
        const CompactNodeInfo* info = reinterpret_cast<const CompactNodeInfo*>(data + offset);
        Node node;
        std::memcpy(node.id.data(), info->id, kIdSize);
        struct in_addr addr;
        addr.s_addr = info->ip;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
        node.ip = ip_str;
        node.port = ntohs(info->port);
        node.last_seen = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        AddNode(node);
        offset += sizeof(CompactNodeInfo);
    }
}

std::vector<Node> RoutingTable::FindClosest(const NodeId& target, int count) const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Node> all_nodes;
    CollectAllNodesLocked(all_nodes);
    if (all_nodes.empty()) {
        return {};
    }
    // Sort by XOR distance.
    // 按 XOR 距离排序。
    std::sort(all_nodes.begin(), all_nodes.end(),
        [&target](const Node& a, const Node& b) {
            NodeId da, db;
            XorDistance(a.id, target, da);
            XorDistance(b.id, target, db);
            return CompareId(da, db) < 0;
        });
    if (static_cast<int>(all_nodes.size()) > count) {
        all_nodes.resize(count);
    }
    return all_nodes;
}

std::vector<Node> RoutingTable::GetAllNodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Node> result;
    CollectAllNodesLocked(result);
    return result;
}

int RoutingTable::GetTotalNodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int total = 0;
    for (const auto& bucket : buckets_) {
        total += bucket.count;
    }
    return total;
}

void RoutingTable::Print() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "DHT Routing Table:\n";
    int total = 0, non_empty = 0;
    for (int i = 0; i < 160; ++i) {
        if (buckets_[i].count > 0) {
            std::cout << "Bucket " << i << ": " << buckets_[i].count << " nodes\n";
            total += buckets_[i].count;
            non_empty++;
        }
    }
    std::cout << "Non-empty buckets: " << non_empty << "/160\n";
    std::cout << "Total nodes: " << total << "\n";
}

// BencodeParser implementation.
// BencodeParser 实现。

BencodeParser::Value BencodeParser::Parse(const std::string& data) {
    size_t pos = 0;
    return Parse(reinterpret_cast<const uint8_t*>(data.data()), data.size(), pos);
}

BencodeParser::Value BencodeParser::Parse(const uint8_t* data, size_t len) {
    size_t pos = 0;
    return Parse(data, len, pos);
}

BencodeParser::Value BencodeParser::Parse(const uint8_t* data, size_t len, size_t& pos) {
    if (pos >= len) return Value{};
    char c = static_cast<char>(data[pos]);
    if (c == 'd') return ParseDict(data, len, ++pos);
    if (c == 'l') return ParseList(data, len, ++pos);
    if (c >= '0' && c <= '9') return ParseString(data, len, pos);
    if (c == 'i') return ParseInt(data, len, ++pos);
    return Value{};
}

BencodeParser::Value BencodeParser::ParseDict(const uint8_t* data, size_t len, size_t& pos) {
    Value val;
    val.type = Value::kDict;
    while (pos < len && static_cast<char>(data[pos]) != 'e') {
        Value key = ParseString(data, len, pos);
        if (key.type != Value::kString) break;
        Value value = Parse(data, len, pos);
        val.dict[key.str] = value;
    }
    if (pos < len && static_cast<char>(data[pos]) == 'e') pos++;
    return val;
}

BencodeParser::Value BencodeParser::ParseList(const uint8_t* data, size_t len, size_t& pos) {
    Value val;
    val.type = Value::kList;
    while (pos < len && static_cast<char>(data[pos]) != 'e') {
        val.list.push_back(Parse(data, len, pos));
    }
    if (pos < len && static_cast<char>(data[pos]) == 'e') pos++;
    return val;
}

BencodeParser::Value BencodeParser::ParseString(const uint8_t* data, size_t len, size_t& pos) {
    Value val;
    val.type = Value::kString;
    size_t start = pos;
    while (pos < len && data[pos] >= '0' && data[pos] <= '9') pos++;
    if (pos >= len || data[pos] != ':') return val;
    std::string len_str(reinterpret_cast<const char*>(data + start), pos - start);
    size_t str_len = std::stoul(len_str);
    if (str_len > 1024 * 1024) return val;  // Protect against oversized strings. 防止超大字符串。
    pos++;
    if (pos + str_len <= len) {
        val.str = std::string(reinterpret_cast<const char*>(data + pos), str_len);
        pos += str_len;
    }
    return val;
}

BencodeParser::Value BencodeParser::ParseInt(const uint8_t* data, size_t len, size_t& pos) {
    Value val;
    val.type = Value::kInt;
    bool negative = false;
    if (pos < len && static_cast<char>(data[pos]) == '-') {
        negative = true;
        pos++;
    }
    size_t start = pos;
    while (pos < len && data[pos] >= '0' && data[pos] <= '9') pos++;
    if (pos < len && static_cast<char>(data[pos]) == 'e') {
        std::string num_str(reinterpret_cast<const char*>(data + start), pos - start);
        val.integer = std::stoll(num_str);
        if (negative) val.integer = -val.integer;
        pos++;
    }
    return val;
}

std::string BencodeParser::Encode(const Value& val) {
    std::string out;
    EncodeValue(val, out);
    return out;
}

void BencodeParser::EncodeValue(const Value& val, std::string& out) {
    switch (val.type) {
        case Value::kString:
            out += std::to_string(val.str.size()) + ":";
            out += val.str;
            break;
        case Value::kInt:
            out += "i" + std::to_string(val.integer) + "e";
            break;
        case Value::kList:
            out += "l";
            for (const auto& item : val.list) {
                EncodeValue(item, out);
            }
            out += "e";
            break;
        case Value::kDict:
            out += "d";
            for (const auto& pair : val.dict) {
                Value key_val;
                key_val.type = Value::kString;
                key_val.str = pair.first;
                EncodeValue(key_val, out);
                EncodeValue(pair.second, out);
            }
            out += "e";
            break;
        default:
            break;
    }
}

// DhtClient implementation.
// DhtClient 实现。

DhtClient::DhtClient() = default;

DhtClient::~DhtClient() = default;

bool DhtClient::Initialize(int sock, const NodeId& own_id) {
    if (sock < 0) return false;
    sock_ = sock;
    own_id_ = own_id;
    routing_table_ = std::make_unique<RoutingTable>(own_id);
    return true;
}

std::string DhtClient::GenerateTransactionId() {
    uint8_t tid[2];
    randombytes_buf(tid, 2);
    return std::string(reinterpret_cast<char*>(tid), 2);
}

NodeId DhtClient::GenerateNodeId() {
    NodeId id;
    randombytes_buf(id.data(), kIdSize);
    return id;
}

void DhtClient::CleanPendingQueries() {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();
    auto it = pending_queries_.begin();
    while (it != pending_queries_.end()) {
        if (now - it->second > kPendingTimeoutMs) {
            it = pending_queries_.erase(it);
        } else {
            ++it;
        }
    }
}

// Encode methods.
// 编码方法。

std::string DhtClient::EncodeFindNode(const NodeId& target, const std::string& tid) {
    BencodeParser::Value msg;
    msg.type = BencodeParser::Value::kDict;
    msg.dict["t"] = {BencodeParser::Value::kString, tid, 0, {}, {}};
    msg.dict["y"] = {BencodeParser::Value::kString, "q", 0, {}, {}};
    msg.dict["q"] = {BencodeParser::Value::kString, "find_node", 0, {}, {}};

    BencodeParser::Value args;
    args.type = BencodeParser::Value::kDict;
    std::string id_str(reinterpret_cast<const char*>(own_id_.data()), kIdSize);
    args.dict["id"] = {BencodeParser::Value::kString, id_str, 0, {}, {}};
    std::string target_str(reinterpret_cast<const char*>(target.data()), kIdSize);
    args.dict["target"] = {BencodeParser::Value::kString, target_str, 0, {}, {}};
    msg.dict["a"] = args;

    return BencodeParser::Encode(msg);
}

std::string DhtClient::EncodeGetPeers(const InfoHash& info_hash, const std::string& tid) {
    BencodeParser::Value msg;
    msg.type = BencodeParser::Value::kDict;
    msg.dict["t"] = {BencodeParser::Value::kString, tid, 0, {}, {}};
    msg.dict["y"] = {BencodeParser::Value::kString, "q", 0, {}, {}};
    msg.dict["q"] = {BencodeParser::Value::kString, "get_peers", 0, {}, {}};

    BencodeParser::Value args;
    args.type = BencodeParser::Value::kDict;
    std::string id_str(reinterpret_cast<const char*>(own_id_.data()), kIdSize);
    args.dict["id"] = {BencodeParser::Value::kString, id_str, 0, {}, {}};
    std::string hash_str(reinterpret_cast<const char*>(info_hash.data()), kIdSize);
    args.dict["info_hash"] = {BencodeParser::Value::kString, hash_str, 0, {}, {}};
    msg.dict["a"] = args;

    return BencodeParser::Encode(msg);
}

std::string DhtClient::EncodeAnnouncePeer(const InfoHash& info_hash, uint16_t port,
                                          const std::string& token, const std::string& tid) {
    BencodeParser::Value msg;
    msg.type = BencodeParser::Value::kDict;
    msg.dict["t"] = {BencodeParser::Value::kString, tid, 0, {}, {}};
    msg.dict["y"] = {BencodeParser::Value::kString, "q", 0, {}, {}};
    msg.dict["q"] = {BencodeParser::Value::kString, "announce_peer", 0, {}, {}};

    BencodeParser::Value args;
    args.type = BencodeParser::Value::kDict;
    std::string id_str(reinterpret_cast<const char*>(own_id_.data()), kIdSize);
    args.dict["id"] = {BencodeParser::Value::kString, id_str, 0, {}, {}};
    std::string hash_str(reinterpret_cast<const char*>(info_hash.data()), kIdSize);
    args.dict["info_hash"] = {BencodeParser::Value::kString, hash_str, 0, {}, {}};
    args.dict["port"] = {BencodeParser::Value::kInt, "", static_cast<int64_t>(port), {}, {}};
    args.dict["token"] = {BencodeParser::Value::kString, token, 0, {}, {}};
    msg.dict["a"] = args;

    return BencodeParser::Encode(msg);
}

std::string DhtClient::EncodePing(const std::string& tid) {
    BencodeParser::Value msg;
    msg.type = BencodeParser::Value::kDict;
    msg.dict["t"] = {BencodeParser::Value::kString, tid, 0, {}, {}};
    msg.dict["y"] = {BencodeParser::Value::kString, "q", 0, {}, {}};
    msg.dict["q"] = {BencodeParser::Value::kString, "ping", 0, {}, {}};

    BencodeParser::Value args;
    args.type = BencodeParser::Value::kDict;
    std::string id_str(reinterpret_cast<const char*>(own_id_.data()), kIdSize);
    args.dict["id"] = {BencodeParser::Value::kString, id_str, 0, {}, {}};
    msg.dict["a"] = args;

    return BencodeParser::Encode(msg);
}

// Send methods.
// 发送方法。

bool DhtClient::SendFindNode(const std::string& ip, uint16_t port, const NodeId& target) {
    if (sock_ < 0) return false;
    CleanPendingQueries();  // Remove old pending entries. 移除旧的待处理条目。

    std::string tid = GenerateTransactionId();
    std::string query = EncodeFindNode(target, tid);

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        // Resolve hostname if IP fails.
        // 如果 IP 解析失败，则解析主机名。
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo* result = nullptr;
        if (getaddrinfo(ip.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
            return false;
        }
        struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        addr.sin_addr = sin->sin_addr;
        freeaddrinfo(result);
    }

    pending_queries_[tid] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    int sent = sendto(sock_, query.c_str(), query.size(), 0,
                      reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return sent > 0;
}

bool DhtClient::SendGetPeers(const std::string& ip, uint16_t port, const InfoHash& info_hash) {
    if (sock_ < 0) return false;
    CleanPendingQueries();

    std::string tid = GenerateTransactionId();
    std::string query = EncodeGetPeers(info_hash, tid);

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo* result = nullptr;
        if (getaddrinfo(ip.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
            return false;
        }
        struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        addr.sin_addr = sin->sin_addr;
        freeaddrinfo(result);
    }

    pending_queries_[tid] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    int sent = sendto(sock_, query.c_str(), query.size(), 0,
                      reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return sent > 0;
}

bool DhtClient::SendAnnouncePeer(const std::string& ip, uint16_t port,
                                 const InfoHash& info_hash,
                                 uint16_t announce_port, const std::string& token) {
    if (sock_ < 0) return false;
    CleanPendingQueries();

    std::string tid = GenerateTransactionId();
    std::string query = EncodeAnnouncePeer(info_hash, announce_port, token, tid);

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo* result = nullptr;
        if (getaddrinfo(ip.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
            return false;
        }
        struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        addr.sin_addr = sin->sin_addr;
        freeaddrinfo(result);
    }

    pending_queries_[tid] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    int sent = sendto(sock_, query.c_str(), query.size(), 0,
                      reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return sent > 0;
}

bool DhtClient::SendPing(const std::string& ip, uint16_t port) {
    if (sock_ < 0) return false;
    CleanPendingQueries();

    std::string tid = GenerateTransactionId();
    std::string query = EncodePing(tid);

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip.c_str(), &addr.sin_addr) != 1) {
        struct addrinfo hints = {};
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        struct addrinfo* result = nullptr;
        if (getaddrinfo(ip.c_str(), nullptr, &hints, &result) != 0 || result == nullptr) {
            return false;
        }
        struct sockaddr_in* sin = reinterpret_cast<struct sockaddr_in*>(result->ai_addr);
        addr.sin_addr = sin->sin_addr;
        freeaddrinfo(result);
    }

    pending_queries_[tid] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    int sent = sendto(sock_, query.c_str(), query.size(), 0,
                      reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    return sent > 0;
}

// Decode methods.
// 解码方法。

std::vector<Node> DhtClient::DecodeCompactNodes(const std::string& compact) {
    std::vector<Node> nodes;
    size_t offset = 0;
    const size_t node_size = 4 + 2 + kIdSize;  // IPv4 + port + 20-byte ID. IPv4 + 端口 + 20字节ID。
    while (offset + node_size <= compact.size()) {
        const CompactNodeInfo* info = reinterpret_cast<const CompactNodeInfo*>(compact.data() + offset);
        Node node;
        std::memcpy(node.id.data(), info->id, kIdSize);
        struct in_addr addr;
        addr.s_addr = info->ip;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
        node.ip = ip_str;
        node.port = ntohs(info->port);
        node.last_seen = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        nodes.push_back(node);
        offset += node_size;
    }
    return nodes;
}

std::vector<PeerInfo> DhtClient::DecodeCompactPeers(const std::string& compact) {
    std::vector<PeerInfo> peers;
    size_t offset = 0;
    const size_t peer_size = 4 + 2;  // IPv4 + port. IPv4 + 端口。
    while (offset + peer_size <= compact.size()) {
        const uint8_t* data = reinterpret_cast<const uint8_t*>(compact.data() + offset);
        uint32_t ip;
        std::memcpy(&ip, data, 4);
        uint16_t port;
        std::memcpy(&port, data + 4, 2);
        PeerInfo peer;
        struct in_addr addr;
        addr.s_addr = ip;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
        peer.ip = ip_str;
        peer.port = ntohs(port);
        peer.last_seen = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();
        peers.push_back(peer);
        offset += peer_size;
    }
    return peers;
}

bool DhtClient::DecodeResponse(const uint8_t* data, size_t len,
                               std::vector<Node>& out_nodes,
                               std::string& out_tid) {
    out_nodes.clear();
    try {
        auto parsed = BencodeParser::Parse(data, len);
        if (parsed.type != BencodeParser::Value::kDict) return false;
        auto tid_it = parsed.dict.find("t");
        if (tid_it != parsed.dict.end() && tid_it->second.type == BencodeParser::Value::kString) {
            out_tid = tid_it->second.str;
        }
        auto r_it = parsed.dict.find("r");
        if (r_it != parsed.dict.end() && r_it->second.type == BencodeParser::Value::kDict) {
            const auto& rdict = r_it->second.dict;
            auto nodes_it = rdict.find("nodes");
            if (nodes_it != rdict.end() && nodes_it->second.type == BencodeParser::Value::kString) {
                out_nodes = DecodeCompactNodes(nodes_it->second.str);
                return true;
            }
        }
        return false;
    } catch (...) {
        return false;
    }
}

bool DhtClient::ParseGetPeersResponse(const uint8_t* data, size_t len,
                                      std::vector<PeerInfo>& out_peers,
                                      std::vector<Node>& out_nodes,
                                      std::string& out_token,
                                      std::string& out_tid) {
    out_peers.clear();
    out_nodes.clear();
    out_token.clear();
    try {
        auto parsed = BencodeParser::Parse(data, len);
        if (parsed.type != BencodeParser::Value::kDict) return false;

        auto tid_it = parsed.dict.find("t");
        if (tid_it != parsed.dict.end() && tid_it->second.type == BencodeParser::Value::kString) {
            out_tid = tid_it->second.str;
        }

        auto r_it = parsed.dict.find("r");
        if (r_it == parsed.dict.end() || r_it->second.type != BencodeParser::Value::kDict) {
            return false;
        }
        const auto& rdict = r_it->second.dict;

        auto token_it = rdict.find("token");
        if (token_it != rdict.end() && token_it->second.type == BencodeParser::Value::kString) {
            out_token = token_it->second.str;
        }

        auto nodes_it = rdict.find("nodes");
        if (nodes_it != rdict.end() && nodes_it->second.type == BencodeParser::Value::kString) {
            out_nodes = DecodeCompactNodes(nodes_it->second.str);
        }

        auto values_it = rdict.find("values");
        if (values_it != rdict.end() && values_it->second.type == BencodeParser::Value::kList) {
            for (const auto& val : values_it->second.list) {
                if (val.type == BencodeParser::Value::kString && val.str.size() == 6) {
                    PeerInfo peer;
                    uint32_t ip;
                    std::memcpy(&ip, val.str.data(), 4);
                    uint16_t port;
                    std::memcpy(&port, val.str.data() + 4, 2);
                    struct in_addr addr;
                    addr.s_addr = ip;
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
                    peer.ip = ip_str;
                    peer.port = ntohs(port);
                    peer.last_seen = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch()
                    ).count();
                    out_peers.push_back(peer);
                }
            }
        }

        return true;
    } catch (...) {
        return false;
    }
}

bool DhtClient::ParseAnnounceResponse(const uint8_t* data, size_t len,
                                      std::string& out_tid) {
    try {
        auto parsed = BencodeParser::Parse(data, len);
        if (parsed.type != BencodeParser::Value::kDict) return false;

        auto tid_it = parsed.dict.find("t");
        if (tid_it != parsed.dict.end() && tid_it->second.type == BencodeParser::Value::kString) {
            out_tid = tid_it->second.str;
        }

        auto r_it = parsed.dict.find("r");
        return r_it != parsed.dict.end() && r_it->second.type == BencodeParser::Value::kDict;
    } catch (...) {
        return false;
    }
}

void DhtClient::HandleResponse(const uint8_t* data, size_t len) {
    try {
        auto parsed = BencodeParser::Parse(data, len);
        if (parsed.type != BencodeParser::Value::kDict) return;

        auto tid_it = parsed.dict.find("t");
        if (tid_it == parsed.dict.end() || tid_it->second.type != BencodeParser::Value::kString) {
            return;
        }
        std::string tid = tid_it->second.str;

        auto y_it = parsed.dict.find("y");
        if (y_it == parsed.dict.end() || y_it->second.type != BencodeParser::Value::kString) {
            return;
        }
        std::string y = y_it->second.str;

        if (y != "r") return;  // Only handle responses. 只处理响应。

        auto r_it = parsed.dict.find("r");
        if (r_it == parsed.dict.end() || r_it->second.type != BencodeParser::Value::kDict) {
            return;
        }
        const auto& rdict = r_it->second.dict;

        // Check for get_peers callback.
        // 检查 get_peers 回调。
        auto get_it = pending_get_peers_.find(tid);
        if (get_it != pending_get_peers_.end()) {
            std::vector<PeerInfo> peers;
            std::vector<Node> nodes;
            std::string token;

            auto values_it = rdict.find("values");
            if (values_it != rdict.end() && values_it->second.type == BencodeParser::Value::kList) {
                for (const auto& val : values_it->second.list) {
                    if (val.type == BencodeParser::Value::kString && val.str.size() == 6) {
                        PeerInfo peer;
                        uint32_t ip;
                        std::memcpy(&ip, val.str.data(), 4);
                        uint16_t port;
                        std::memcpy(&port, val.str.data() + 4, 2);
                        struct in_addr addr;
                        addr.s_addr = ip;
                        char ip_str[INET_ADDRSTRLEN];
                        inet_ntop(AF_INET, &addr, ip_str, sizeof(ip_str));
                        peer.ip = ip_str;
                        peer.port = ntohs(port);
                        peer.last_seen = std::chrono::duration_cast<std::chrono::milliseconds>(
                            std::chrono::system_clock::now().time_since_epoch()
                        ).count();
                        peers.push_back(peer);
                    }
                }
            }

            auto nodes_it = rdict.find("nodes");
            if (nodes_it != rdict.end() && nodes_it->second.type == BencodeParser::Value::kString) {
                nodes = DecodeCompactNodes(nodes_it->second.str);
            }

            auto token_it = rdict.find("token");
            if (token_it != rdict.end() && token_it->second.type == BencodeParser::Value::kString) {
                token = token_it->second.str;
            }

            if (get_it->second) {
                get_it->second(peers, nodes, token);
            }
            pending_get_peers_.erase(get_it);
            pending_queries_.erase(tid);
            return;
        }

        // Check for announce callback.
        // 检查 announce 回调。
        auto ann_it = pending_announce_.find(tid);
        if (ann_it != pending_announce_.end()) {
            if (ann_it->second) {
                ann_it->second(true);
            }
            pending_announce_.erase(ann_it);
            pending_queries_.erase(tid);
            return;
        }

        // Check for find_node response (add nodes to routing table).
        // 检查 find_node 响应（将节点加入路由表）。
        auto nodes_it = rdict.find("nodes");
        if (nodes_it != rdict.end() && nodes_it->second.type == BencodeParser::Value::kString) {
            auto nodes = DecodeCompactNodes(nodes_it->second.str);
            for (const auto& node : nodes) {
                routing_table_->AddNode(node);
            }
            pending_queries_.erase(tid);
        }
    } catch (...) {
        // Ignore parse errors.
        // 忽略解析错误。
    }
}

// Public API methods.
// 公开 API 方法。

bool DhtClient::GetPeers(const InfoHash& info_hash, GetPeersCallback callback) {
    if (sock_ < 0 || !routing_table_) return false;

    auto closest = routing_table_->FindClosest(info_hash, kK);
    if (closest.empty()) {
        std::cout << "[DHT] No nodes in routing table for get_peers\n";
        return false;
    }

    std::string tid = GenerateTransactionId();
    pending_get_peers_[tid] = callback;
    pending_queries_[tid] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    int sent = 0;
    for (const auto& node : closest) {
        if (SendGetPeers(node.ip, node.port, info_hash)) {
            sent++;
        }
    }

    return sent > 0;
}

bool DhtClient::AnnouncePeer(const InfoHash& info_hash, uint16_t port,
                             const std::string& token, AnnounceCallback callback) {
    if (sock_ < 0 || !routing_table_) return false;

    auto closest = routing_table_->FindClosest(info_hash, kK);
    if (closest.empty()) {
        std::cout << "[DHT] No nodes in routing table for announce_peer\n";
        return false;
    }

    std::string tid = GenerateTransactionId();
    pending_announce_[tid] = callback;
    pending_queries_[tid] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    int sent = 0;
    for (const auto& node : closest) {
        if (SendAnnouncePeer(node.ip, node.port, info_hash, port, token)) {
            sent++;
        }
    }

    return sent > 0;
}

// Iterative methods.
// 迭代方法。

std::vector<Node> DhtClient::IterativeFindNode(const NodeId& target, int count) {
    std::vector<Node> result;
    std::unordered_map<std::string, bool> queried;

    auto closest = routing_table_->FindClosest(target, count);
    if (closest.empty()) {
        return result;
    }

    int rounds = 0;
    while (rounds < 5 && static_cast<int>(result.size()) < count) {
        std::vector<Node> to_query;
        for (const auto& node : closest) {
            std::string key = node.ip + ":" + std::to_string(node.port);
            if (queried.find(key) == queried.end()) {
                to_query.push_back(node);
                queried[key] = true;
                if (static_cast<int>(to_query.size()) >= kAlpha) break;
            }
        }

        if (to_query.empty()) break;

        for (const auto& node : to_query) {
            std::vector<Node> nodes;
            std::string tid;
            if (SendFindNode(node.ip, node.port, target)) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(sock_, &fds);
                struct timeval tv = {2, 0};  // 2 seconds. 2秒。
                int ret = select(sock_ + 1, &fds, nullptr, nullptr, &tv);
                if (ret > 0 && FD_ISSET(sock_, &fds)) {
                    uint8_t buffer[4096];
                    struct sockaddr_in from_addr;
                    socklen_t from_len = sizeof(from_addr);
                    int n = recvfrom(sock_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                     reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
                    if (n > 0) {
                        if (DecodeResponse(buffer, static_cast<size_t>(n), nodes, tid)) {
                            for (const auto& nnode : nodes) {
                                routing_table_->AddNode(nnode);
                                // Check if this node is closer.
                                // 检查这个节点是否更近。
                                NodeId dist;
                                RoutingTable::XorDistance(nnode.id, target, dist);
                                bool inserted = false;
                                for (auto it = result.begin(); it != result.end(); ++it) {
                                    NodeId edist;
                                    RoutingTable::XorDistance(it->id, target, edist);
                                    if (RoutingTable::CompareId(dist, edist) < 0) {
                                        result.insert(it, nnode);
                                        inserted = true;
                                        break;
                                    }
                                }
                                if (!inserted) {
                                    result.push_back(nnode);
                                }
                            }
                        }
                    }
                }
            }
        }

        // Sort and trim.
        // 排序并截断。
        std::sort(result.begin(), result.end(),
            [&target](const Node& a, const Node& b) {
                NodeId da, db;
                RoutingTable::XorDistance(a.id, target, da);
                RoutingTable::XorDistance(b.id, target, db);
                return RoutingTable::CompareId(da, db) < 0;
            });
        if (static_cast<int>(result.size()) > count) {
            result.resize(count);
        }

        closest = routing_table_->FindClosest(target, count);
        rounds++;
    }

    return result;
}

std::vector<PeerInfo> DhtClient::IterativeGetPeers(const InfoHash& info_hash) {
    std::vector<PeerInfo> all_peers;
    std::unordered_map<std::string, bool> queried;

    auto closest = routing_table_->FindClosest(info_hash, kK);
    if (closest.empty()) {
        return all_peers;
    }

    int rounds = 0;
    while (rounds < 5) {
        std::vector<Node> to_query;
        for (const auto& node : closest) {
            std::string key = node.ip + ":" + std::to_string(node.port);
            if (queried.find(key) == queried.end()) {
                to_query.push_back(node);
                queried[key] = true;
                if (static_cast<int>(to_query.size()) >= kAlpha) break;
            }
        }

        if (to_query.empty()) break;

        for (const auto& node : to_query) {
            std::vector<PeerInfo> peers;
            std::vector<Node> nodes;
            std::string token;
            std::string tid;

            if (SendGetPeers(node.ip, node.port, info_hash)) {
                fd_set fds;
                FD_ZERO(&fds);
                FD_SET(sock_, &fds);
                struct timeval tv = {2, 0};
                int ret = select(sock_ + 1, &fds, nullptr, nullptr, &tv);
                if (ret > 0 && FD_ISSET(sock_, &fds)) {
                    uint8_t buffer[4096];
                    struct sockaddr_in from_addr;
                    socklen_t from_len = sizeof(from_addr);
                    int n = recvfrom(sock_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                     reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
                    if (n > 0) {
                        if (ParseGetPeersResponse(buffer, static_cast<size_t>(n),
                                                  peers, nodes, token, tid)) {
                            for (const auto& peer : peers) {
                                all_peers.push_back(peer);
                            }
                            for (const auto& nnode : nodes) {
                                routing_table_->AddNode(nnode);
                            }
                            if (!peers.empty()) {
                                return all_peers;
                            }
                        }
                    }
                }
            }
        }

        closest = routing_table_->FindClosest(info_hash, kK);
        rounds++;
    }

    return all_peers;
}

bool DhtClient::IterativeAnnouncePeer(const InfoHash& info_hash, uint16_t port) {
    // Get a token from the closest node.
    // 从最近节点获取 token。
    std::string token;
    auto closest = routing_table_->FindClosest(info_hash, kK);
    if (closest.empty()) {
        return false;
    }

    for (const auto& node : closest) {
        std::vector<PeerInfo> peers;
        std::vector<Node> out_nodes;
        std::string out_token;
        std::string tid;

        if (SendGetPeers(node.ip, node.port, info_hash)) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sock_, &fds);
            struct timeval tv = {2, 0};
            int ret = select(sock_ + 1, &fds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(sock_, &fds)) {
                uint8_t buffer[4096];
                struct sockaddr_in from_addr;
                socklen_t from_len = sizeof(from_addr);
                int n = recvfrom(sock_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                 reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
                if (n > 0) {
                    if (ParseGetPeersResponse(buffer, static_cast<size_t>(n),
                                              peers, out_nodes, out_token, tid)) {
                        token = out_token;
                        for (const auto& nnode : out_nodes) {
                            routing_table_->AddNode(nnode);
                        }
                        if (!token.empty()) break;
                    }
                }
            }
        }
    }

    if (token.empty()) {
        std::cout << "[DHT] Failed to get token for announce_peer\n";
        return false;
    }

    // Announce to all closest nodes.
    // 向所有最近节点宣告。
    auto announce_targets = routing_table_->FindClosest(info_hash, kK);
    if (announce_targets.empty()) {
        return false;
    }

    bool success = false;
    for (const auto& node : announce_targets) {
        if (SendAnnouncePeer(node.ip, node.port, info_hash, port, token)) {
            fd_set fds;
            FD_ZERO(&fds);
            FD_SET(sock_, &fds);
            struct timeval tv = {2, 0};
            int ret = select(sock_ + 1, &fds, nullptr, nullptr, &tv);
            if (ret > 0 && FD_ISSET(sock_, &fds)) {
                uint8_t buffer[4096];
                struct sockaddr_in from_addr;
                socklen_t from_len = sizeof(from_addr);
                int n = recvfrom(sock_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                                 reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
                if (n > 0) {
                    std::string out_tid;
                    if (ParseAnnounceResponse(buffer, static_cast<size_t>(n), out_tid)) {
                        success = true;
                    }
                }
            }
        }
    }

    return success;
}

// Bootstrap.
// 引导。

int DhtClient::Bootstrap(const std::vector<std::pair<std::string, uint16_t>>& bootstrap_nodes,
                         DhtResponseCallback callback) {
    if (sock_ < 0 || !routing_table_) return 0;

    // Set socket to non-blocking for select.
    // 将套接字设为非阻塞以便使用 select。
#ifdef _WIN32
    u_long mode = 1;
    ioctlsocket(sock_, FIONBIO, &mode);
#else
    int flags = fcntl(sock_, F_GETFL, 0);
    fcntl(sock_, F_SETFL, flags | O_NONBLOCK);
#endif

    // Send requests to all nodes in parallel.
    // 并行向所有节点发送请求。
    std::cout << "[DHT] Bootstrapping to " << bootstrap_nodes.size() << " nodes...\n";
    int total_sent = 0;
    for (const auto& node_pair : bootstrap_nodes) {
        const std::string& ip = node_pair.first;
        uint16_t port = node_pair.second;
        if (SendFindNode(ip, port, own_id_)) {
            total_sent++;
        } else {
            std::cout << "[DHT] Failed to send to " << ip << ":" << port << "\n";
        }
    }

    if (total_sent == 0) {
        std::cout << "[DHT] No requests sent.\n";
        return 0;
    }

    // Wait for responses for up to 5 seconds.
    // 等待响应最多 5 秒。
    int success_count = 0, total_added = 0;
    auto start_time = std::chrono::steady_clock::now();
    int timeout_ms = 5000;
    int remaining_timeout = timeout_ms;

    while (remaining_timeout > 0) {
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sock_, &fds);
        struct timeval tv = { remaining_timeout / 1000, (remaining_timeout % 1000) * 1000 };
        int ret = select(sock_ + 1, &fds, nullptr, nullptr, &tv);
        if (ret <= 0) break;

        if (FD_ISSET(sock_, &fds)) {
            uint8_t buffer[4096];
            struct sockaddr_in from_addr;
            socklen_t from_len = sizeof(from_addr);
            int n = recvfrom(sock_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                             reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
            if (n > 0) {
                std::vector<Node> nodes;
                std::string tid;
                if (DecodeResponse(buffer, static_cast<size_t>(n), nodes, tid)) {
                    char ip_str[INET_ADDRSTRLEN];
                    inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, sizeof(ip_str));
                    uint16_t port = ntohs(from_addr.sin_port);
                    std::cout << "[DHT] Got " << nodes.size() << " nodes from "
                              << ip_str << ":" << port << "\n";
                    for (const auto& node : nodes) {
                        routing_table_->AddNode(node);
                        total_added++;
                    }
                    if (callback) {
                        callback(ip_str, port, nodes);
                    }
                    success_count++;
                }
            }
        }

        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - start_time
        ).count();
        remaining_timeout = timeout_ms - static_cast<int>(elapsed);
    }

    // Restore blocking mode.
    // 恢复阻塞模式。
#ifdef _WIN32
    u_long mode_block = 0;
    ioctlsocket(sock_, FIONBIO, &mode_block);
#else
    fcntl(sock_, F_SETFL, flags);
#endif

    std::cout << "[DHT] Bootstrap complete: " << success_count << "/" << total_sent
              << " successful, " << total_added << " nodes added\n";
    return success_count;
}

int DhtClient::BootstrapDefault(DhtResponseCallback callback) {
    std::vector<std::pair<std::string, uint16_t>> bootstrap_nodes = {
        {"dht.transmissionbt.com", 6881},
        {"dht.libtorrent.org", 25401},
        {"ntp.juliusbeckmann.de", 6881},
        {"mgts.ivth.ru", 57858},
        {"sorcerer.leentje.org", 49786},
        {"libertalia.space", 50005},
        {"milda.intelib.org", 51413},
        {"router.bittorrent.com", 6881},
        {"router.utorrent.com", 6881},
        {"router.bitcomet.com", 6881},
        {"dht.aelitis.com", 6881},
        {"relay.pkarr.org", 6881},
        {"router.silotis.us", 6881},
        {"114.230.238.18", 6881},
        {"68.107.235.88", 13888},
        {"149.202.42.154", 44221},
        {"103.161.70.16", 29741},
        {"196.110.14.128", 42107},
        {"124.16.199.99", 26586},
        {"195.113.203.78", 14883},
    };
    return Bootstrap(bootstrap_nodes, callback);
}

int BootstrapRoutingTable(int sock, const NodeId& own_id,
                          RoutingTable& routing_table,
                          DhtResponseCallback callback) {
    DhtClient client;
    if (!client.Initialize(sock, own_id)) return 0;

    auto wrapped = [&](const std::string& ip, uint16_t port, const std::vector<Node>& nodes) {
        for (const auto& node : nodes) routing_table.AddNode(node);
        if (callback) callback(ip, port, nodes);
    };

    std::vector<std::pair<std::string, uint16_t>> bootstrap_nodes = {
        {"dht.transmissionbt.com", 6881},
        {"dht.libtorrent.org", 25401},
        {"ntp.juliusbeckmann.de", 6881},
        {"mgts.ivth.ru", 57858},
        {"sorcerer.leentje.org", 49786},
        {"libertalia.space", 50005},
        {"milda.intelib.org", 51413},
        {"router.bittorrent.com", 6881},
        {"router.utorrent.com", 6881},
        {"router.bitcomet.com", 6881},
        {"dht.aelitis.com", 6881},
        {"relay.pkarr.org", 6881},
        {"router.silotis.us", 6881},
        {"114.230.238.18", 6881},
        {"68.107.235.88", 13888},
        {"149.202.42.154", 44221},
        {"103.161.70.16", 29741},
        {"196.110.14.128", 42107},
        {"124.16.199.99", 26586},
        {"195.113.203.78", 14883},
    };
    return client.Bootstrap(bootstrap_nodes, wrapped);
}

} // namespace dht
} // namespace numotirus