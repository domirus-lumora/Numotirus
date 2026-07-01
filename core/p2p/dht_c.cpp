// core/p2p/dht_c.cpp
// C wrapper implementation for DHT routing table.
// DHT 路由表的 C 封装实现。
// SPDX-License-Identifier: Apache-2.0

#include "dht_c.h"
#include "dht.hpp"
#include <cstring>
#include <cstdlib>
#include <vector>
#include <algorithm>
#include <thread>
#include <chrono>
#include <iostream>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#endif

using namespace numotirus::dht;

extern "C" {

DhtHandle* dht_create(const uint8_t* own_id) {
    NodeId id;
    std::memcpy(id.data(), own_id, DHT_ID_SIZE);
    return new RoutingTable(id);
}

void dht_destroy(DhtHandle* dht) {
    delete static_cast<RoutingTable*>(dht);
}

void dht_add_node(DhtHandle* dht, const uint8_t* id, const char* ip,
                  uint16_t port, uint64_t last_seen) {
    Node node;
    std::memcpy(node.id.data(), id, DHT_ID_SIZE);
    node.ip = ip;
    node.port = port;
    node.last_seen = last_seen;
    static_cast<RoutingTable*>(dht)->AddNode(node);
}

int dht_find_closest(DhtHandle* dht, const uint8_t* target,
                     uint8_t* out_ids, char* out_ips,
                     uint16_t* out_ports, int max_count) {
    NodeId target_id;
    std::memcpy(target_id.data(), target, DHT_ID_SIZE);
    auto result = static_cast<RoutingTable*>(dht)->FindClosest(target_id, max_count);

    int count = 0;
    for (const auto& node : result) {
        if (count >= max_count) break;
        std::memcpy(out_ids + count * DHT_ID_SIZE, node.id.data(), DHT_ID_SIZE);
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

static int send_find_node_and_parse(int sock, const std::string& ip, uint16_t port,
                                    const NodeId& own_id,
                                    std::vector<Node>& out_nodes) {
    DhtClient client;
    if (!client.Initialize(sock, own_id)) return -1;

    if (!client.SendFindNode(ip, port, own_id)) return -1;

    uint8_t buffer[4096];
    struct sockaddr_in from_addr;
#ifdef _WIN32
    int from_len = sizeof(from_addr);
#else
    socklen_t from_len = sizeof(from_addr);
#endif

#ifdef _WIN32
    DWORD timeout = 3000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv = {3, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    int n = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                     reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
    if (n <= 0) return -1;

    std::string tid;
    return client.DecodeResponse(buffer, static_cast<size_t>(n), out_nodes, tid) ? 0 : -1;
}

int dht_bootstrap_and_add(DhtHandle* dht, int sock, const uint8_t* own_id) {
    auto* table = static_cast<RoutingTable*>(dht);
    if (!table) return 0;

    NodeId id;
    std::memcpy(id.data(), own_id, DHT_ID_SIZE);

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

int dht_bootstrap_with_spread(DhtHandle* dht, int sock, const uint8_t* own_id) {
    auto* table = static_cast<RoutingTable*>(dht);
    if (!table) return 0;

    NodeId id;
    std::memcpy(id.data(), own_id, DHT_ID_SIZE);

    int total_added = 0;

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

    DhtClient client;
    if (!client.Initialize(sock, id)) return 0;

#ifdef _WIN32
    DWORD timeout = 5000;
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));
#else
    struct timeval tv = {5, 0};
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
#endif

    std::cout << "[DHT] Bootstrapping to " << bootstrap_nodes.size() << " nodes...\n";

    int success_count = 0;

    for (const auto& node_pair : bootstrap_nodes) {
        const std::string& ip = node_pair.first;
        uint16_t port = node_pair.second;
        std::cout << "[DHT] Querying " << ip << ":" << port << "...\n";

        if (!client.SendFindNode(ip, port, id)) {
            std::cout << "[DHT] Failed to send to " << ip << ":" << port << "\n";
            continue;
        }

        uint8_t buffer[4096];
        struct sockaddr_in from_addr;
#ifdef _WIN32
        int from_len = sizeof(from_addr);
#else
        socklen_t from_len = sizeof(from_addr);
#endif
        int n = recvfrom(sock, reinterpret_cast<char*>(buffer), sizeof(buffer), 0,
                         reinterpret_cast<struct sockaddr*>(&from_addr), &from_len);
        if (n <= 0) {
            std::cout << "[DHT] Timeout from " << ip << ":" << port << "\n";
            continue;
        }

        std::vector<Node> nodes;
        std::string tid;
        if (client.DecodeResponse(buffer, static_cast<size_t>(n), nodes, tid)) {
            std::cout << "[DHT] Got " << nodes.size() << " nodes from " << ip << ":" << port << "\n";
            for (const auto& node : nodes) {
                table->AddNode(node);
                total_added++;
            }
            success_count++;
        } else {
            std::cout << "[DHT] Failed to parse response from " << ip << ":" << port << "\n";
        }
    }

    std::cout << "[DHT] Initial bootstrap: " << success_count << "/" << bootstrap_nodes.size()
              << " successful, " << total_added << " nodes added.\n";

    int spread_added = 0;
    int rounds = 3;

    for (int round = 0; round < rounds; ++round) {
        auto all_nodes = table->GetAllNodes();
        if (all_nodes.empty()) break;

        std::sort(all_nodes.begin(), all_nodes.end(),
            [&id](const Node& a, const Node& b) {
                NodeId da, db;
                RoutingTable::XorDistance(a.id, id, da);
                RoutingTable::XorDistance(b.id, id, db);
                return RoutingTable::CompareId(da, db) > 0;
            });

        int pick_count = std::min(5, (int)all_nodes.size());
        int round_added = 0;

        for (int i = 0; i < pick_count; ++i) {
            const Node& target = all_nodes[i];
            std::cout << "[DHT] Spread round " << round + 1 << ": querying "
                      << target.ip << ":" << target.port << "\n";

            std::vector<Node> nodes;
            if (send_find_node_and_parse(sock, target.ip, target.port, id, nodes) == 0) {
                for (const auto& node : nodes) {
                    table->AddNode(node);
                    round_added++;
                }
                std::cout << "[DHT]   Got " << nodes.size() << " nodes.\n";
            }
        }

        spread_added += round_added;
        std::cout << "[DHT] Spread round " << round + 1 << " complete, added "
                  << round_added << " nodes.\n";

        if (round_added == 0) break;
    }

    std::cout << "[DHT] Bootstrap complete: " << total_added + spread_added
              << " total nodes added (" << total_added << " initial + "
              << spread_added << " spread).\n";
    return total_added + spread_added;
}

void dht_generate_node_id(uint8_t* out_id) {
    NodeId id = DhtClient::GenerateNodeId();
    std::memcpy(out_id, id.data(), DHT_ID_SIZE);
}

} // extern "C"