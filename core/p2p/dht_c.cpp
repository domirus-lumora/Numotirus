// core/p2p/dht_c.cpp
// C wrapper implementation for DHT routing table.
// DHT 路由表的 C 封装实现。

#include "dht_c.h"
#include "dht.hpp"
#include <cstring>
#include <cstdlib>

using namespace numotirus::dht;

extern "C" {

// ============================================================
// Routing Table C API. 路由表 C API。
// ============================================================

DhtHandle* dht_create(const uint8_t* own_id) {
    NodeId id;
    std::memcpy(id.data(), own_id, kIdSize);
    return new RoutingTable(id);
}

void dht_destroy(DhtHandle* dht) {
    delete static_cast<RoutingTable*>(dht);
}

void dht_add_node(DhtHandle* dht, const uint8_t* id, const char* ip,
                  uint16_t port, uint64_t last_seen) {
    Node node;
    std::memcpy(node.id.data(), id, kIdSize);
    node.ip = ip;
    node.port = port;
    node.last_seen = last_seen;
    static_cast<RoutingTable*>(dht)->AddNode(node);
}

int dht_find_closest(DhtHandle* dht, const uint8_t* target,
                     uint8_t* out_ids, char* out_ips,
                     uint16_t* out_ports, int max_count) {
    NodeId target_id;
    std::memcpy(target_id.data(), target, kIdSize);
    auto result = static_cast<RoutingTable*>(dht)->FindClosest(target_id, max_count);

    int count = 0;
    for (const auto& node : result) {
        if (count >= max_count) break;
        std::memcpy(out_ids + count * kIdSize, node.id.data(), kIdSize);
        std::strcpy(out_ips + count * 32, node.ip.c_str());
        out_ports[count] = node.port;
        count++;
    }
    return count;
}

void dht_print(DhtHandle* dht) {
    static_cast<RoutingTable*>(dht)->Print();
}

int dht_get_size(DhtHandle* dht) {
    return static_cast<RoutingTable*>(dht)->GetTotalNodes();
}

void dht_clear(DhtHandle* dht) {
    static_cast<RoutingTable*>(dht)->Clear();
}

// ============================================================
// Bootstrap C API. 引导 C API。
// ============================================================

int dht_bootstrap_and_add(DhtHandle* dht, int sock, const uint8_t* own_id) {
    auto* table = static_cast<RoutingTable*>(dht);
    if (!table) return 0;

    NodeId id;
    std::memcpy(id.data(), own_id, kIdSize);

    int added = 0;
    auto callback = [&](const std::string& ip, uint16_t port, const std::vector<Node>& nodes) {
        for (const auto& node : nodes) {
            table->AddNode(node);
            added++;
        }
        (void)ip; (void)port;
    };

    BootstrapRoutingTable(sock, id, *table, callback);
    return added;
}

void dht_generate_node_id(uint8_t* out_id) {
    NodeId id = DhtClient::GenerateNodeId();
    std::memcpy(out_id, id.data(), kIdSize);
}

} // extern "C"