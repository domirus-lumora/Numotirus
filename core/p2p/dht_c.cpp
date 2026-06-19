// core/p2p/dht_c.cpp
// C wrapper for DHT routing table. DHT 路由表的 C 封装。

#include "dht.hpp"
#include <cstring>
#include <cstdlib>

using namespace numotirus::dht;

extern "C" {

void* dht_create(const uint8_t* own_id) {
    NodeId id;
    std::memcpy(id.data(), own_id, 32);
    return new RoutingTable(id);
}

void dht_destroy(void* dht) {
    delete static_cast<RoutingTable*>(dht);
}

void dht_add_node(void* dht, const uint8_t* id, const char* ip, uint16_t port, uint64_t last_seen) {
    Node node;
    std::memcpy(node.id.data(), id, 32);
    node.ip = ip;
    node.port = port;
    node.last_seen = last_seen;
    static_cast<RoutingTable*>(dht)->AddNode(node);
}

int dht_find_closest(void* dht, const uint8_t* target,
                     uint8_t* out_ids, char* out_ips, uint16_t* out_ports, int max_count) {
    NodeId target_id;
    std::memcpy(target_id.data(), target, 32);
    auto result = static_cast<RoutingTable*>(dht)->FindClosest(target_id, max_count);

    int count = 0;
    for (const auto& node : result) {
        if (count >= max_count) break;
        std::memcpy(out_ids + count * 32, node.id.data(), 32);
        std::strcpy(out_ips + count * 32, node.ip.c_str());
        out_ports[count] = node.port;
        count++;
    }
    return count;
}

void dht_print(void* dht) {
    static_cast<RoutingTable*>(dht)->Print();
}

} // extern "C"