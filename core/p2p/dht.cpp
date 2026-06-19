// core/p2p/dht.cpp
// Kademlia DHT routing table implementation. Kademlia DHT 路由表实现。

#include "dht.hpp"
#include <sodium.h>
#include <cstring>
#include <iostream>
#include <algorithm>

namespace numotirus {
namespace dht {

RoutingTable::RoutingTable(const NodeId& own_id) : own_id_(own_id) {
    if (sodium_init() < 0) {
        // 打印错误或抛出异常
        throw std::runtime_error("libsodium initialization failed");
    }
}  // 确保 libsodium 已初始化 / Ensure libsodium is initialized

RoutingTable::~RoutingTable() {
    for (auto& bucket : buckets_) {
        BucketNode* cur = bucket.head;
        while (cur) {
            BucketNode* next = cur->next;
            delete cur;
            cur = next;
        }
    }
}

void RoutingTable::XorDistance(const NodeId& a, const NodeId& b, NodeId& out) {
    for (size_t i = 0; i < out.size(); ++i) {
        out[i] = a[i] ^ b[i];
    }
}

int RoutingTable::CompareId(const NodeId& a, const NodeId& b) {
    return std::memcmp(a.data(), b.data(), kIdSize);
}

int RoutingTable::GetBucketIndex(const NodeId& other) const {
    NodeId diff;
    XorDistance(own_id_, other, diff);

    for (size_t i = 0; i < diff.size(); ++i) {
        if (diff[i] == 0) continue;
        for (int bit = 7; bit >= 0; --bit) {
            if (diff[i] & (1 << bit)) {
                return static_cast<int>(i * 8 + (7 - bit));
            }
        }
    }
    return -1;  // 自己 / Self
}

void RoutingTable::AddNode(const Node& node) {
    // Ignore self. 忽略自己。
    if (std::memcmp(own_id_.data(), node.id.data(), kIdSize) == 0) {
        return;
    }

    int idx = GetBucketIndex(node.id);
    if (idx < 0) return;

    KBucket& bucket = buckets_[idx];

    // Check if node already exists. 检查节点是否已存在。
    BucketNode* cur = bucket.head;
    while (cur) {
        if (cur->node.id == node.id) {
            cur->node = node;  // Update. 更新。
            return;
        }
        cur = cur->next;
    }

    // Insert new node at head. 在头部插入新节点。
    BucketNode* new_node = new BucketNode{node, bucket.head};
    bucket.head = new_node;
    bucket.count++;

    // If bucket is full, evict the oldest (tail). 如果桶满了，移除最老的 (尾部)。
    if (bucket.count > kK) {
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
        }
    }
}

std::vector<Node> RoutingTable::FindClosest(const NodeId& target, int count) const {
    std::vector<Node> result;
    int idx = GetBucketIndex(target);
    if (idx < 0) idx = 0;

    // Search outward from target bucket. 从目标桶向外搜索。
    for (int offset = 0; offset < 256 && static_cast<int>(result.size()) < count; ++offset) {
        int bucket_idx = idx + offset;
        if (bucket_idx >= 256) break;

        const KBucket& bucket = buckets_[bucket_idx];
        BucketNode* cur = bucket.head;
        while (cur && static_cast<int>(result.size()) < count) {
            // Avoid duplicates. 避免重复。
            bool duplicate = false;
            for (const Node& existing : result) {
                if (existing.id == cur->node.id) {
                    duplicate = true;
                    break;
                }
            }
            if (!duplicate) {
                result.push_back(cur->node);
            }
            cur = cur->next;
        }
    }
    return result;
}

void RoutingTable::Print() const {
    std::cout << "=== DHT Routing Table ===\n";
    int total = 0;
    for (int i = 0; i < 256; ++i) {
        if (buckets_[i].count > 0) {
            std::cout << "Bucket " << i << ": " << buckets_[i].count << " nodes\n";
            total += buckets_[i].count;
        }
    }
    std::cout << "Total nodes: " << total << "\n";
}

} // namespace dht
} // namespace numotirus