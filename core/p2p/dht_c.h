// core/p2p/dht_c.h
// C wrapper for DHT routing table with BitTorrent bootstrap.
// DHT 路由表的 C 封装，支持 BitTorrent 引导。

#ifndef DHT_C_H
#define DHT_C_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DHT_ID_SIZE 20   // 20 bytes for BitTorrent DHT.

// ============================================================
// Routing Table C API. 路由表 C API。
// ============================================================

typedef void DhtHandle;

DhtHandle* dht_create(const uint8_t* own_id);
void dht_destroy(DhtHandle* dht);

void dht_add_node(DhtHandle* dht, const uint8_t* id, const char* ip,
                  uint16_t port, uint64_t last_seen);

int dht_find_closest(DhtHandle* dht, const uint8_t* target,
                     uint8_t* out_ids, char* out_ips,
                     uint16_t* out_ports, int max_count);

void dht_print(DhtHandle* dht);
int dht_get_size(DhtHandle* dht);
void dht_clear(DhtHandle* dht);

// ============================================================
// Bootstrap C API. 引导 C API。
// ============================================================

// Bootstrap DHT from default BitTorrent nodes and add to routing table.
// 从默认 BitTorrent 节点引导 DHT 并加入路由表。
int dht_bootstrap_and_add(DhtHandle* dht, int sock, const uint8_t* own_id);

// Generate a random node ID (20 bytes). 生成随机节点 ID（20 字节）。
void dht_generate_node_id(uint8_t* out_id);

#ifdef __cplusplus
}
#endif

#endif  // DHT_C_H