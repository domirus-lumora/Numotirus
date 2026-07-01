// core/p2p/dht_c.h
// C wrapper for DHT routing table with BitTorrent bootstrap.
// DHT 路由表的 C 封装，支持 BitTorrent 引导。
// SPDX-License-Identifier: Apache-2.0

#ifndef DHT_C_H
#define DHT_C_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DHT_ID_SIZE 20

typedef void DhtHandle;

// Create a DHT routing table with the given own node ID.
// 用给定的自己的节点 ID 创建 DHT 路由表。
DhtHandle* dht_create(const uint8_t* own_id);

// Destroy a DHT routing table and free resources.
// 销毁 DHT 路由表并释放资源。
void dht_destroy(DhtHandle* dht);

// Add a node to the routing table.
// 向路由表添加节点。
void dht_add_node(DhtHandle* dht, const uint8_t* id, const char* ip,
                  uint16_t port, uint64_t last_seen);

// Find the closest nodes to a target ID.
// 查找离目标 ID 最近的节点。
int dht_find_closest(DhtHandle* dht, const uint8_t* target,
                     uint8_t* out_ids, char* out_ips,
                     uint16_t* out_ports, int max_count);

// Print the routing table for debugging.
// 打印路由表用于调试。
void dht_print(DhtHandle* dht);

// Get the total number of nodes in the routing table.
// 获取路由表总节点数。
int dht_get_size(DhtHandle* dht);

// Clear the routing table.
// 清空路由表。
void dht_clear(DhtHandle* dht);

// Bootstrap DHT from default BitTorrent nodes and add to routing table.
// 从默认 BitTorrent 节点引导 DHT 并加入路由表。
int dht_bootstrap_and_add(DhtHandle* dht, int sock, const uint8_t* own_id);

// Bootstrap with spread: after initial bootstrap, query distant nodes to fill far buckets.
// 带扩散的引导：初始引导后，查询远端节点以填充远距离桶。
int dht_bootstrap_with_spread(DhtHandle* dht, int sock, const uint8_t* own_id);

// Generate a random node ID (20 bytes) using libsodium.
// 使用 libsodium 生成随机节点 ID（20 字节）。
void dht_generate_node_id(uint8_t* out_id);

#ifdef __cplusplus
}
#endif

#endif