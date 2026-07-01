// core/p2p/dht.cpp
// Kademlia DHT routing table - MSB fixed, thread-safe.
// Kademlia DHT 路由表 —— 修复 MSB 索引，线程安全。
// SPDX-License-Identifier: Apache-2.0

#include "dht.hpp"
#include <sodium.h>
#include <cstring>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <map>
#include <vector>
#include <thread>

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
namespace dht {

// Constructor. 构造函数。
RoutingTable::RoutingTable(const NodeId& own_id) : own_id_(own_id) {
    if (sodium_init() < 0) {
        throw std::runtime_error("libsodium init failed");
    }
}

// Destructor. 析构函数。
RoutingTable::~RoutingTable() {
    Clear();
}

// Clear all buckets and free memory. 清空所有桶并释放内存。
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

// Compute XOR distance between two node IDs. 计算两个节点 ID 之间的 XOR 距离。
void RoutingTable::XorDistance(const NodeId& a, const NodeId& b, NodeId& out) {
    for (size_t i = 0; i < kIdSize; ++i) {
        out[i] = a[i] ^ b[i];
    }
}

// Compare two node IDs lexicographically. 按字典序比较两个节点 ID。
int RoutingTable::CompareId(const NodeId& a, const NodeId& b) {
    return std::memcmp(a.data(), b.data(), kIdSize);
}

// Get bucket index based on XOR distance from own_id, searching from MSB.
// 根据与自己的 XOR 距离获取桶索引，从 MSB 开始搜索。
int RoutingTable::GetBucketIndex(const NodeId& other) const {
    NodeId diff;
    XorDistance(own_id_, other, diff);

    // Search from most significant byte (index 0) to least.
    // 从最高有效字节（索引 0）向最低字节搜索。
    for (int i = 0; i < kIdSize; ++i) {
        if (diff[i] == 0) continue;
        // Within the byte, search from MSB (bit 7) to LSB (bit 0).
        // 在字节内从最高有效位（bit 7）向最低位搜索。
        for (int bit = 7; bit >= 0; --bit) {
            if (diff[i] & (1 << bit)) {
                return (i * 8) + (7 - bit);
            }
        }
    }
    return -1;  // Same as own ID. 与自己相同。
}

// Collect all nodes into a vector (must be called with lock held).
// 收集所有节点到 vector 中（必须在持有锁时调用）。
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

// Add or update a node. 添加或更新节点。
void RoutingTable::AddNode(const Node& node) {
    // Ignore self. 忽略自己。
    if (std::memcmp(own_id_.data(), node.id.data(), kIdSize) == 0) return;

    int idx = GetBucketIndex(node.id);
    if (idx < 0 || idx >= 160) return;

    std::lock_guard<std::mutex> lock(mutex_);
    KBucket& bucket = buckets_[idx];

    // If exists, move to front. 若存在则移到头部。
    BucketNode* cur = bucket.head;
    BucketNode* prev = nullptr;
    while (cur) {
        if (std::memcmp(cur->node.id.data(), node.id.data(), kIdSize) == 0) {
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

    // New node at head. 新节点插入头部。
    BucketNode* new_node = new BucketNode{node, bucket.head};
    bucket.head = new_node;
    bucket.count++;

    // Evict oldest if bucket is full. 桶满则淘汰最旧。
    if (bucket.count > kK) {
        EvictOldest(bucket);
    }
}

// Remove the oldest (tail) node from a bucket. 从桶中移除最旧的（尾部）节点。
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

// Add nodes from compact wire format. 从紧凑网络格式添加节点。
void RoutingTable::AddNodesFromCompact(const uint8_t* data, size_t len) {
    size_t offset = 0;
    while (offset + sizeof(CompactNodeInfo) <= len) {
        const CompactNodeInfo* info = reinterpret_cast<const CompactNodeInfo*>(data + offset);
        Node node;
        std::memcpy(node.id.data(), info->id, kIdSize);

        struct in_addr addr;
        addr.s_addr = info->ip;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
        node.ip = ip_str;
        node.port = ntohs(info->port);
        node.last_seen = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();

        AddNode(node);
        offset += sizeof(CompactNodeInfo);
    }
}

// Find the K closest nodes to target. 查找离目标最近的 K 个节点。
std::vector<Node> RoutingTable::FindClosest(const NodeId& target, int count) const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<Node> all_nodes;
    CollectAllNodesLocked(all_nodes);
    if (all_nodes.empty()) {
        return {};
    }

    // Sort by XOR distance to target. 按到目标的 XOR 距离排序。
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

// Get all nodes (for debugging). 获取所有节点（用于调试）。
std::vector<Node> RoutingTable::GetAllNodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<Node> result;
    CollectAllNodesLocked(result);
    return result;
}

// Get total number of nodes. 获取总节点数。
int RoutingTable::GetTotalNodes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    int total = 0;
    for (const auto& bucket : buckets_) {
        total += bucket.count;
    }
    return total;
}

// Print routing table for debugging. 打印路由表用于调试。
void RoutingTable::Print() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::cout << "=== DHT Routing Table ===\n";
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

// BencodeParser implementation. Bencode 解析器实现。

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

// Parse string with size limit (1MB) to avoid memory bomb.
// 解析字符串，带大小限制（1MB）防止内存炸弹。
BencodeParser::Value BencodeParser::ParseString(const uint8_t* data, size_t len, size_t& pos) {
    Value val;
    val.type = Value::kString;
    size_t start = pos;
    while (pos < len && data[pos] >= '0' && data[pos] <= '9') pos++;
    if (pos >= len || data[pos] != ':') return val;
    std::string len_str(reinterpret_cast<const char*>(data + start), pos - start);
    size_t str_len = std::stoul(len_str);

    // Reject strings larger than 1 MiB. 拒绝大于 1 MiB 的字符串。
    if (str_len > 1024 * 1024) return val;

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

// DhtClient implementation. DHT 客户端实现。

DhtClient::DhtClient() : sock_(-1), routing_table_(nullptr) {}

DhtClient::~DhtClient() {
    if (routing_table_) {
        routing_table_.reset();
    }
}

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

std::string DhtClient::EncodeFindNode(const NodeId& target, const std::string& tid) {
    // Bencode format: {"t":tid,"y":"q","q":"find_node","a":{"id":<id>,"target":<target>}}
    // Bencode 格式：{"t":tid,"y":"q","q":"find_node","a":{"id":<id>,"target":<target>}}
    std::string msg = "d1:ad2:id20:";
    msg += std::string(reinterpret_cast<const char*>(own_id_.data()), kIdSize);
    msg += "6:target20:";
    msg += std::string(reinterpret_cast<const char*>(target.data()), kIdSize);
    msg += "e1:q9:find_node1:t";
    msg += std::to_string(tid.size());
    msg += ":";
    msg += tid;
    msg += "1:y1:qe";
    return msg;
}

std::string DhtClient::EncodePing(const std::string& tid) {
    std::string msg = "d1:ad2:id20:";
    msg += std::string(reinterpret_cast<const char*>(own_id_.data()), kIdSize);
    msg += "e1:q4:ping1:t";
    msg += std::to_string(tid.size());
    msg += ":";
    msg += tid;
    msg += "1:y1:qe";
    return msg;
}

bool DhtClient::SendFindNode(const std::string& ip, uint16_t port, const NodeId& target) {
    if (sock_ < 0) return false;
    std::string tid = GenerateTransactionId();
    std::string query = EncodeFindNode(target, tid);

    struct sockaddr_in addr = {};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);

    // Try as IP first; if fails, resolve as hostname.
    // 先尝试作为 IP，失败则解析为主机名。
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

    // Track pending query for response matching. 跟踪待处理查询以匹配响应。
    pending_queries_[tid] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    ).count();

    int sent = sendto(sock_, query.c_str(), query.size(), 0,
                  reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (sent < 0) {
    #ifdef _WIN32
        std::cout << "[DHT] sendto error: " << WSAGetLastError() << "\n";
    #else
        perror("[DHT] sendto");
    #endif
    }
    return sent > 0;
}

bool DhtClient::SendPing(const std::string& ip, uint16_t port) {
    if (sock_ < 0) return false;
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

    int sent = sendto(sock_, query.c_str(), query.size(), 0,
                  reinterpret_cast<struct sockaddr*>(&addr), sizeof(addr));
    if (sent < 0) {
    #ifdef _WIN32
        std::cout << "[DHT] sendto error: " << WSAGetLastError() << "\n";
    #else
        perror("[DHT] sendto");
    #endif
    }
    return sent > 0;
}

std::vector<Node> DhtClient::DecodeCompactNodes(const std::string& compact) {
    std::vector<Node> nodes;
    size_t offset = 0;
    const size_t node_size = 4 + 2 + kIdSize;  // IP + port + ID. IP + 端口 + ID。
    while (offset + node_size <= compact.size()) {
        const CompactNodeInfo* info = reinterpret_cast<const CompactNodeInfo*>(compact.data() + offset);
        Node node;
        std::memcpy(node.id.data(), info->id, kIdSize);
        struct in_addr addr;
        addr.s_addr = info->ip;
        char ip_str[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &addr, ip_str, INET_ADDRSTRLEN);
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

bool DhtClient::DecodeResponse(const uint8_t* data, size_t len,
                               std::vector<Node>& out_nodes,
                               std::string& out_tid) {
    out_nodes.clear();
    try {
        auto parsed = BencodeParser::Parse(data, len);
        if (parsed.type != BencodeParser::Value::kDict) return false;

        // Extract transaction ID. 提取事务 ID。
        auto tid_it = parsed.dict.find("t");
        if (tid_it != parsed.dict.end() && tid_it->second.type == BencodeParser::Value::kString) {
            out_tid = tid_it->second.str;
        }

        // Extract nodes from "r" (response) field. 从 "r"（响应）字段提取节点。
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

int DhtClient::Bootstrap(const std::vector<std::pair<std::string, uint16_t>>& bootstrap_nodes,
                         DhtResponseCallback callback) {
    if (sock_ < 0 || !routing_table_) return 0;
    int success_count = 0, total_added = 0;

    // Set socket receive timeout. 设置套接字接收超时。
#ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv = {5, 0};
    setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::cout << "[DHT] Bootstrapping to " << bootstrap_nodes.size() << " nodes...\n";

    for (const auto& node_pair : bootstrap_nodes) {
        const std::string& ip = node_pair.first;
        uint16_t port = node_pair.second;
        std::cout << "[DHT] Querying " << ip << ":" << port << "...\n";

        // Send find_node with our own ID as target. 用我们自己的 ID 作为目标发送 find_node。
        if (!SendFindNode(ip, port, own_id_)) {
            std::cout << "[DHT] Failed to send to " << ip << ":" << port << "\n";
            continue;
        }

        // Wait for response. 等待响应。
        uint8_t buffer[4096];
        struct sockaddr_in from_addr;
#ifdef _WIN32
        int from_len = sizeof(from_addr);
#else
        socklen_t from_len = sizeof(from_addr);
#endif
        int n = recvfrom(sock_, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                         reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
        if (n <= 0) {
            std::cout << "[DHT] Timeout from " << ip << ":" << port << "\n";
            continue;
        }

        std::vector<Node> nodes;
        std::string tid;
        if (DecodeResponse(buffer, static_cast<size_t>(n), nodes, tid)) {
            std::cout << "[DHT] Got " << nodes.size() << " nodes from " << ip << ":" << port << "\n";
            for (const auto& node : nodes) {
                routing_table_->AddNode(node);
                total_added++;
            }
            if (callback) {
                char ip_str[INET_ADDRSTRLEN];
                inet_ntop(AF_INET, &from_addr.sin_addr, ip_str, INET_ADDRSTRLEN);
                callback(ip_str, ntohs(from_addr.sin_port), nodes);
            }
            success_count++;
        } else {
            std::cout << "[DHT] Failed to parse response from " << ip << ":" << port << "\n";
        }
    }

    std::cout << "[DHT] Bootstrap complete: " << success_count << "/" << bootstrap_nodes.size()
              << " successful, " << total_added << " nodes added\n";
    return success_count;
}

// Bootstrap using 20 default public nodes. 使用 20 个默认公共节点引导。
int DhtClient::BootstrapDefault(DhtResponseCallback callback) {
    std::vector<std::pair<std::string, uint16_t>> bootstrap_nodes = {
        // Transmission official node. Transmission 官方节点。
        {"dht.transmissionbt.com", 6881},
        // libtorrent official node. libtorrent 官方节点。
        {"dht.libtorrent.org", 25401},
        // Community nodes verified by qBittorrent community. qBittorrent 社区验证的节点。
        {"ntp.juliusbeckmann.de", 6881},
        {"mgts.ivth.ru", 57858},
        {"sorcerer.leentje.org", 49786},
        {"libertalia.space", 50005},
        {"milda.intelib.org", 51413},
        // Known public routers (may be dead but kept for completeness). 已知公共路由器（可能已死但保留）。
        {"router.bittorrent.com", 6881},
        {"router.utorrent.com", 6881},
        {"router.bitcomet.com", 6881},
        {"dht.aelitis.com", 6881},
        {"relay.pkarr.org", 6881},
        {"router.silotis.us", 6881},
        // Active DHT nodes from public scanning. 从公开扫描获取的活跃 DHT 节点。
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

// Convenience function: bootstrap using 20 default nodes.
// 便捷函数：使用 20 个默认节点引导。
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